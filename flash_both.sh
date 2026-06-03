#!/bin/bash
# Build and flash both Board A (client) and Board B (server) in one go.
# Targets each Nucleo by ST-Link serial number — no cable swapping needed.
#
# Usage:  ./flash_both.sh
#
# Assign your ST-Link serials here (run: ls /dev/serial/by-id/ to check):
STLINK_BOARD_A="0029001F3235510837333439"
STLINK_BOARD_B="004700383235510937333439"

set -e

echo "=== Building Board A (echo client) ==="
west build -b nucleo_h745zi_q/stm32h745xx/m7 -d build_a \
    -- -DEXTRA_CONF_FILE=overlay-board-a.conf

echo "=== Flashing Board A (ST-Link ${STLINK_BOARD_A}) ==="
west flash -d build_a --cmd-pre-init "adapter serial ${STLINK_BOARD_A}"

echo "=== Building Board B (echo server) ==="
west build -b nucleo_h745zi_q/stm32h745xx/m7 -d build_b \
    -- -DEXTRA_CONF_FILE=overlay-board-b.conf

echo "=== Flashing Board B (ST-Link ${STLINK_BOARD_B}) ==="
west flash -d build_b --cmd-pre-init "adapter serial ${STLINK_BOARD_B}"

echo "=== Done — both boards flashed ==="
