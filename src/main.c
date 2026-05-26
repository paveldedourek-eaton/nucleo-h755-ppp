/*
 * NUCLEO-H755ZI-Q PPP Client
 *
 * Establishes a PPP link over USART2 (PD5 TX / PD6 RX) to a host
 * running pppd, then performs a UDP echo test against the server
 * scripts in server/.
 *
 * Host side:
 *   sudo pppd /dev/ttyUSB0 115200 noauth local nodetach debug \
 *        nocrtscts 192.168.1.1:192.168.1.2 nodefaultroute
 *
 * Then run:
 *   python3 server/te.py
 *
 * Shell commands available: net iface, net ping <ip>, help
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/ppp.h>
#include <string.h>

#define SLEEP_TIME_MS   1000
#define LED0_NODE       DT_ALIAS(led0)

/* UDP echo server (te_udp_echo.py) address and port */
#define ECHO_SERVER_ADDR "192.168.1.1"
#define ECHO_SERVER_PORT 7780
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

static void udp_echo_test(void)
{
	struct sockaddr_in addr;
	int sock;
	uint8_t tx_buf[ECHO_PACKET_SIZE];
	uint8_t rx_buf[ECHO_PACKET_SIZE];
	int ret;

	addr.sin_family = AF_INET;
	addr.sin_port = htons(ECHO_SERVER_PORT);
	zsock_inet_pton(AF_INET, ECHO_SERVER_ADDR, &addr.sin_addr);

	sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0) {
		printk("Failed to create socket: %d\n", errno);
		return;
	}

	/* Set receive timeout */
	struct zsock_timeval tv = { .tv_sec = 5, .tv_usec = 0 };
	zsock_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	printk("Starting UDP echo test to %s:%d\n",
	       ECHO_SERVER_ADDR, ECHO_SERVER_PORT);

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

int main(void)
{
	struct net_if *ppp_iface;

	if (!gpio_is_ready_dt(&led)) {
		printk("LED device not ready\n");
		return 0;
	}
	gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);

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

	/* Bring up the PPP interface — LCP/IPCP negotiation starts */
	net_if_up(ppp_iface);
	printk("PPP interface up, waiting for link...\n");
	printk("Start pppd on host, then shell: net iface, net ping\n");

	/* Wait for L4 connectivity (IPCP done, IP assigned) */
	k_event_wait(&ppp_events, EVENT_L4_CONNECTED, false, K_FOREVER);
	printk("PPP link established\n");

	/* Give the link a moment to settle */
	k_msleep(1000);

	/* Run UDP echo test */
	udp_echo_test();

	/* Keep blinking LED to show we're alive */
	while (1) {
		gpio_pin_toggle_dt(&led);
		k_msleep(SLEEP_TIME_MS);
	}

	return 0;
}
