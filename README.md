# NUCLEO-H755ZI-Q PPP — Board-to-Board

Two STM32 Nucleo H755ZI-Q boards connected over UART PPP. One board runs as a UDP echo server, the other as a client that sends packets and verifies the replies.

## Hardware Setup

Cross-connect USART2 between the two boards:

| Signal | Board A Pin       | Board B Pin       | Morpho             |
|--------|-------------------|-------------------|---------------------|
| TX→RX  | PD5 (USART2 TX)   | PD6 (USART2 RX)   | CN9-6 → CN9-4      |
| RX←TX  | PD6 (USART2 RX)   | PD5 (USART2 TX)   | CN9-4 → CN9-6      |
| GND    | GND               | GND               | CN9-12              |

The ST-Link VCP (USART3) on each board remains available for the Zephyr shell console at 115200 baud.

## Build & Flash

### Board A — echo client

```bash
west build -b nucleo_h755zi_q/stm32h755xx/m7 \
    -- -DEXTRA_CONF_FILE=overlay-client.conf
west flash
```

### Board B — echo server (flash first)

```bash
west build -b nucleo_h755zi_q/stm32h755xx/m7 \
    -- -DEXTRA_CONF_FILE=overlay-server.conf
west flash
```


## IP Addressing

| Board   | Role         | IP Address    |
|---------|--------------|---------------|
| Board A | Echo client  | 192.168.1.1   |
| Board B | Echo server  | 192.168.1.2   |

Addresses are proposed via IPCP negotiation. Each board's address comes from the overlay conf file (`CONFIG_NET_CONFIG_MY_IPV4_ADDR`).

## What Happens

1. Both boards boot and bring up PPP on USART2
2. LCP/IPCP negotiation happens automatically over the cross-wired UART
3. Once the link is up:
   - **Board B** starts the UDP echo server on port 7780
   - **Board A** sends 8 UDP packets to 192.168.1.2:7780 and verifies the echoed replies
4. Results are printed on each board's shell console
5. LEDs blink/toggle to indicate activity

## Shell Commands

Available on each board's ST-Link VCP console (USART3, 115200 baud):

```
udp send <ip> <port> <text>  — send a UDP packet and print any reply
net iface                   — show network interfaces and IP addresses
net ping <ip>               — ping the other board (e.g. net ping 192.168.1.2)
net stats                   — network statistics
help                        — list all commands
```

Example (from Board A console, Board B running echo server):

```
uart:~$ udp send 192.168.1.2 7780 hello
Sent 5 bytes to 192.168.1.2:7780
Reply (5 bytes): hello
```

100-byte payload test:

```
uart:~$ udp send 192.168.1.2 7780 abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKL
Sent 100 bytes to 192.168.1.2:7780
Reply (100 bytes): abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKL
```

## Project Structure

```
├── CMakeLists.txt
├── Kconfig                                            — App Kconfig (echo server toggle)
├── prj.conf                                           — Common Kconfig (PPP, networking, shell)
├── overlay-client.conf                                — Board A: echo client, 192.168.1.1
├── overlay-server.conf                                — Board B: echo server, 192.168.1.2
├── boards/
│   ├── nucleo_h755zi_q_stm32h755xx_m7.conf            — Board-specific Kconfig
│   └── nucleo_h755zi_q_stm32h755xx_m7.overlay         — DTS overlay (USART2 for PPP)
├── src/
│   └── main.c                                         — PPP application (client + server)
└── server/
    ├── te.py                                          — Host test server (for host-to-board use)
    ├── te_udp_echo.py                                 — UDP echo server (host side)
    └── te_udp_receive.py                              — UDP receive server (host side)
```
