#!/usr/bin/env bash
# Simulate InfyPower module 0 responses on vcan0/can0.
#
# CAN ID: src (bits 7-0) | dst<<8 | cmd<<16 | dev(0x0A)<<22
#
#   Request  (controller 0xF0 -> module 0):  ...00F0  (e.g. 028300F0 for cmd 0x03)
#   Response (module 0 -> controller 0xF0):  ...F000  (e.g. 0283F000 for cmd 0x03)
#
# Usage:
#   ./sim_module0_responses.sh vcan0 once    # one burst after driver polls
#   ./sim_module0_responses.sh vcan0 loop    # continuous (default)

set -euo pipefail

IFACE="${1:-vcan0}"
MODE="${2:-loop}"

if ! ip link show "$IFACE" &>/dev/null; then
    echo "Interface $IFACE not found."
    exit 1
fi

send() {
    cansend "$IFACE" "$1"
}

send_responses() {
    # Module 0 -> controller 0xF0 (note F000 suffix, NOT 00F0)
    send "0283F000#43FA000040600000"   # 0x03 ReadModuleVI: 500 V, 3.5 A
    send "0284F000#000000021B004000"   # 0x04 PowerModuleStatus
    send "028CF000#0FA001F400000000"   # 0x0C VI after diode: 400.0 V, 50.0 A
    send "028AF000#01F400C803E803E8"   # 0x0A capabilities
    send "0282F000#0000010000000000"   # 0x02 module count = 1
    send "028BF000#5600000000000000"   # 0x0B barcode (dummy)
}

if [[ "$MODE" == "once" ]]; then
    send_responses
    echo "Sent module-0 responses on $IFACE (src 0x00 -> dst 0xF0)"
    exit 0
fi

echo "Looping module-0 responses on $IFACE every 0.5s (Ctrl+C to stop)"
while true; do
    send_responses
    sleep 0.5
done
