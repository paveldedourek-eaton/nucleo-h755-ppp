/*
 * NUCLEO-H755ZI-Q PPP Demo — Board-to-Board
 *
 * Two Nucleo boards connected via UART PPP (USART2 PD5/PD6 cross-wired).
 *
 * Board A (client): sends UDP echo packets to Board B
 * Board B (server): echoes received UDP packets back
 *
 * Build:
 *   Board A: west build -b nucleo_h755zi_q/stm32h755xx/m7 \
 *              -- -DEXTRA_CONF_FILE=overlay-client.conf
 *   Board B: west build -b nucleo_h755zi_q/stm32h755xx/m7 \
 *              -- -DEXTRA_CONF_FILE=overlay-server.conf
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/ppp.h>
#include <zephyr/shell/shell.h>
#include <stdlib.h>
#include <string.h>

#define SLEEP_TIME_MS    1000
#define LED0_NODE        DT_ALIAS(led0)

#define ECHO_PORT        7780
#define ECHO_PACKET_SIZE 64
#define ECHO_PACKETS     8

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

static K_EVENT_DEFINE(ppp_events);
#define EVENT_L4_CONNECTED BIT(0)
#define EVENT_L4_DISCONNECTED BIT(1)

static struct net_mgmt_event_callback l4_cb;

static void l4_event_handler(struct net_mgmt_event_callback *cb,
			     uint32_t mgmt_event,
			     struct net_if *iface)
{
	if (mgmt_event == NET_EVENT_L4_CONNECTED) {
		printk("PPP L4 connected\n");
		k_event_post(&ppp_events, EVENT_L4_CONNECTED);
	} else if (mgmt_event == NET_EVENT_L4_DISCONNECTED) {
		printk("PPP L4 disconnected\n");
		k_event_post(&ppp_events, EVENT_L4_DISCONNECTED);
	}
}

#ifdef CONFIG_APP_ECHO_SERVER

static void udp_echo_server(void)
{
	struct sockaddr_in bind_addr = {
		.sin_family = AF_INET,
		.sin_port = htons(ECHO_PORT),
		.sin_addr.s_addr = htonl(INADDR_ANY),
	};
	uint8_t buf[ECHO_PACKET_SIZE];
	int sock;

	sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0) {
		printk("Failed to create socket: %d\n", errno);
		return;
	}

	if (zsock_bind(sock, (struct sockaddr *)&bind_addr,
		       sizeof(bind_addr)) < 0) {
		printk("Failed to bind: %d\n", errno);
		zsock_close(sock);
		return;
	}

	printk("UDP echo server listening on port %d\n", ECHO_PORT);

	while (1) {
		struct sockaddr_in client_addr;
		socklen_t addr_len = sizeof(client_addr);

		int len = zsock_recvfrom(sock, buf, sizeof(buf), 0,
					(struct sockaddr *)&client_addr,
					&addr_len);
		if (len < 0) {
			printk("recvfrom error: %d\n", errno);
			continue;
		}

		printk("  Received %d bytes, echoing back\n", len);
		gpio_pin_toggle_dt(&led);

		zsock_sendto(sock, buf, len, 0,
			     (struct sockaddr *)&client_addr, addr_len);
	}
}

#else /* CONFIG_APP_ECHO_SERVER */

static void udp_echo_test(void)
{
	struct sockaddr_in addr;
	int sock;
	uint8_t tx_buf[ECHO_PACKET_SIZE];
	uint8_t rx_buf[ECHO_PACKET_SIZE];
	int ret;

	addr.sin_family = AF_INET;
	addr.sin_port = htons(ECHO_PORT);
	zsock_inet_pton(AF_INET, CONFIG_APP_PEER_IPV4_ADDR,
			&addr.sin_addr);

	sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0) {
		printk("Failed to create socket: %d\n", errno);
		return;
	}

	/* Set receive timeout */
	struct zsock_timeval tv = { .tv_sec = 5, .tv_usec = 0 };
	zsock_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	printk("Starting UDP echo test to %s:%d\n",
	       CONFIG_APP_PEER_IPV4_ADDR, ECHO_PORT);

	int success = 0;

	for (int i = 0; i < ECHO_PACKETS; i++) {
		/* Fill buffer with pattern */
		memset(tx_buf, (uint8_t)(i + 1), sizeof(tx_buf));

		ret = zsock_sendto(sock, tx_buf, sizeof(tx_buf), 0,
				   (struct sockaddr *)&addr, sizeof(addr));
		if (ret < 0) {
			printk("  Packet %d: send failed (%d)\n", i + 1, errno);
			continue;
		}

		ret = zsock_recv(sock, rx_buf, sizeof(rx_buf), 0);
		if (ret < 0) {
			printk("  Packet %d: recv timeout\n", i + 1);
			continue;
		}

		if (ret == sizeof(tx_buf) &&
		    memcmp(tx_buf, rx_buf, sizeof(tx_buf)) == 0) {
			success++;
			printk("  Packet %d: echo OK (%d bytes)\n",
			       i + 1, ret);
		} else {
			printk("  Packet %d: mismatch (got %d bytes)\n",
			       i + 1, ret);
		}
	}

	printk("Echo test done: %d/%d packets OK\n", success, ECHO_PACKETS);
	zsock_close(sock);
}

#endif /* CONFIG_APP_ECHO_SERVER */

/* ---- Shell command: udp send <ip> <port> <text> ---- */

static int cmd_udp_send(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 4) {
		shell_error(sh, "Usage: udp send <ip> <port> <text>");
		return -EINVAL;
	}

	const char *ip = argv[1];
	uint16_t port = (uint16_t)atoi(argv[2]);
	const char *text = argv[3];
	size_t text_len = strlen(text);

	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_port = htons(port),
	};

	if (zsock_inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
		shell_error(sh, "Invalid IP: %s", ip);
		return -EINVAL;
	}

	int sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0) {
		shell_error(sh, "Socket error: %d", errno);
		return -EIO;
	}

	struct zsock_timeval tv = { .tv_sec = 3, .tv_usec = 0 };
	zsock_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	int ret = zsock_sendto(sock, text, text_len, 0,
			       (struct sockaddr *)&addr, sizeof(addr));
	if (ret < 0) {
		shell_error(sh, "Send failed: %d", errno);
		zsock_close(sock);
		return -EIO;
	}

	shell_print(sh, "Sent %d bytes to %s:%u", ret, ip, port);

	char rx_buf[256];

	ret = zsock_recv(sock, rx_buf, sizeof(rx_buf) - 1, 0);
	if (ret > 0) {
		rx_buf[ret] = '\0';
		shell_print(sh, "Reply (%d bytes): %s", ret, rx_buf);
	} else if (ret == 0) {
		shell_print(sh, "No reply (empty)");
	} else {
		shell_print(sh, "No reply (timeout)");
	}

	zsock_close(sock);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_udp,
	SHELL_CMD_ARG(send, NULL,
		      "Send UDP payload: udp send <ip> <port> <text>",
		      cmd_udp_send, 4, 0),
	SHELL_SUBCMD_SET_END
);
SHELL_CMD_REGISTER(udp, &sub_udp, "UDP commands", NULL);

int main(void)
{
	struct net_if *ppp_iface;

	if (!gpio_is_ready_dt(&led)) {
		printk("LED device not ready\n");
		return 0;
	}
	gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);

#ifdef CONFIG_APP_ECHO_SERVER
	printk("\n=== Board B | ECHO SERVER | %s ===\n\n",
	       CONFIG_APP_MY_IPV4_ADDR);
#else
	printk("\n=== Board A | ECHO CLIENT | %s ===\n\n",
	       CONFIG_APP_MY_IPV4_ADDR);
#endif

	/* Register for L4 connectivity events */
	net_mgmt_init_event_callback(&l4_cb, l4_event_handler,
				     NET_EVENT_L4_CONNECTED |
				     NET_EVENT_L4_DISCONNECTED);
	net_mgmt_add_event_callback(&l4_cb);

	/* Get the PPP network interface */
	ppp_iface = net_if_get_first_by_type(&NET_L2_GET_NAME(PPP));
	if (!ppp_iface) {
		printk("No PPP interface found\n");
		return 0;
	}

	printk("PPP interface found: %s\n",
	       net_if_get_device(ppp_iface)->name);

	/* Set local IP on the PPP interface before bringing it up.
	 * IPCP will propose this address to the peer during negotiation.
	 */
	struct in_addr my_addr;

	zsock_inet_pton(AF_INET, CONFIG_APP_MY_IPV4_ADDR, &my_addr);
	net_if_ipv4_addr_add(ppp_iface, &my_addr, NET_ADDR_MANUAL, 0);

	/* Bring up the PPP interface — LCP/IPCP negotiation starts */
	net_if_up(ppp_iface);

#ifdef CONFIG_APP_ECHO_SERVER
	printk("Role: ECHO SERVER (%s) — listening on port %d\n",
	       CONFIG_APP_MY_IPV4_ADDR, ECHO_PORT);
#else
	printk("Role: ECHO CLIENT (%s -> %s:%d)\n",
	       CONFIG_APP_MY_IPV4_ADDR,
	       CONFIG_APP_PEER_IPV4_ADDR, ECHO_PORT);
#endif

	printk("Waiting for PPP link...\n");

	/* Wait for L4 connectivity (IPCP done, IP assigned) */
	k_event_wait(&ppp_events, EVENT_L4_CONNECTED, false, K_FOREVER);
	printk("PPP link established\n");

	/* Give the link a moment to settle */
	k_msleep(1000);

#ifdef CONFIG_APP_ECHO_SERVER
	/* Echo server runs forever */
	udp_echo_server();
#else
	/* Run UDP echo test */
	udp_echo_test();
#endif

	/* Keep blinking LED to show we're alive */
	while (1) {
		gpio_pin_toggle_dt(&led);
		k_msleep(SLEEP_TIME_MS);
	}

	return 0;
}
