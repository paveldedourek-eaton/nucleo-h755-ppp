#!/usr/bin/env python3
"""Query both Nucleo boards via their ST-Link VCP consoles.

Usage:
    python3 board_status.py              # PPP status on both boards
    python3 board_status.py "net iface"  # run any shell command
    python3 board_status.py boot         # reset Board A and capture boot log
"""

import serial
import sys
import time

BOARDS = {
    "Board A (client)": "/dev/serial/by-id/usb-STMicroelectronics_STLINK-V3_0029001F3235510837333439-if02",
    "Board B (server)": "/dev/serial/by-id/usb-STMicroelectronics_STLINK-V3_004700383235510937333439-if02",
}
BAUD = 115200


def run_cmd(port, cmd, timeout=3):
    """Send a shell command and return the output."""
    ser = serial.Serial(port, BAUD, timeout=timeout)
    ser.reset_input_buffer()
    ser.write(b"\r\n")
    time.sleep(0.3)
    ser.reset_input_buffer()
    ser.write(f"{cmd}\r\n".encode())
    time.sleep(timeout)
    out = ser.read(ser.in_waiting or 4096)
    ser.close()
    return out.decode(errors="replace")


def capture_boot(port, seconds=15):
    """Reset board via shell and capture boot output."""
    ser = serial.Serial(port, BAUD, timeout=1)
    ser.reset_input_buffer()
    ser.write(b"\r\nkernel reboot cold\r\n")
    start = time.time()
    buf = b""
    while time.time() - start < seconds:
        chunk = ser.read(ser.in_waiting or 1)
        if chunk:
            buf += chunk
    ser.close()
    return buf.decode(errors="replace")


def main():
    cmd = " ".join(sys.argv[1:]) if len(sys.argv) > 1 else "net ppp status"

    if cmd == "boot":
        name, port = list(BOARDS.items())[0]
        print(f"=== Rebooting {name} — capturing {15}s of output ===")
        print(capture_boot(port))
        return

    for name, port in BOARDS.items():
        print(f"=== {name} ===")
        try:
            print(run_cmd(port, cmd))
        except Exception as e:
            print(f"  Error: {e}\n")


if __name__ == "__main__":
    main()
