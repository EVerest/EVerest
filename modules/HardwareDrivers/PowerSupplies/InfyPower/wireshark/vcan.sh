#!/usr/bin/env bash
# Create and bring up a **virtual** CAN interface (loopback / Wireshark testing).
#
# For physical USB CAN (can0, can1 @ 125 kbit/s) use:
#   sudo ../setup_can.sh can0
#
# Usage:
#   sudo ./vcan.sh           # vcan0
#   sudo ./vcan.sh vcan1

set -uo pipefail

IFACE="${1:-vcan0}"

if [[ "${IFACE}" == can0 || "${IFACE}" == can1 ]]; then
    echo "Error: ${IFACE} is a physical interface, not virtual CAN." >&2
    echo "Use: sudo $(dirname "$0")/../setup_can.sh ${IFACE}" >&2
    exit 1
fi

if [[ "${EUID}" -ne 0 ]]; then
    echo "Run as root (e.g. sudo $0)" >&2
    exit 1
fi

modprobe vcan 2>/dev/null || true

if ip link show "${IFACE}" &>/dev/null; then
    echo "${IFACE} already exists"
else
    echo "Creating ${IFACE}"
    ip link add dev "${IFACE}" type vcan
fi

ip link set "${IFACE}" up
ip -details link show "${IFACE}" | grep -E 'state|link/vcan' || true
echo "Done. Use can_device: ${IFACE} in Everest config for simulation."
