#!/usr/bin/env bash
# Bring up physical CAN interfaces for InfyPower (125 kbit/s per protocol V1.15).
#
# Usage:
#   sudo ./setup_can.sh              # can0 and can1 @ 125000
#   sudo ./setup_can.sh can0         # single interface
#   sudo BITRATE=250000 ./setup_can.sh
#
# If you see "RTNETLINK answers: File exists":
#   - Stop candump / Everest (anything using the interface)
#   - Re-run this script (it is safe to run multiple times)
#
# Verify:
#   ip -details link show can0
#   candump can0

set -uo pipefail

BITRATE="${BITRATE:-125000}"
# Optional bus-off auto-restart (ms). Many gs_usb adapters reject this; leave empty to skip.
RESTART_MS="${RESTART_MS:-}"
INTERFACES=("$@")

if [[ ${#INTERFACES[@]} -eq 0 ]]; then
    INTERFACES=(can0 can1)
fi

if [[ "${EUID}" -ne 0 ]]; then
    echo "Run as root (e.g. sudo $0)" >&2
    exit 1
fi

modprobe can 2>/dev/null || true
modprobe can-raw 2>/dev/null || true

iface_bitrate() {
    ip -details link show "$1" 2>/dev/null | sed -n 's/.*bitrate \([0-9]*\).*/\1/p' | head -1
}

iface_is_can() {
    ip -details link show "$1" 2>/dev/null | grep -q 'link/can'
}

iface_is_up() {
    ip link show "$1" 2>/dev/null | grep -q 'state UP'
}

setup_iface() {
    local iface="$1"

    if ! ip link show "${iface}" &>/dev/null; then
        echo "Skip ${iface}: interface not found" >&2
        return 1
    fi

    echo "=== ${iface} ==="

    # Stop userspace consumers so DOWN + reconfigure succeeds.
    if iface_is_up "${iface}"; then
        echo "  bringing down (stop candump/Everest if this hangs)..."
        ip link set "${iface}" down 2>/dev/null || true
        sleep 0.2
    fi

    local current_bitrate
    current_bitrate="$(iface_bitrate "${iface}")"

    if iface_is_can "${iface}" && [[ "${current_bitrate}" == "${BITRATE}" ]]; then
        echo "  already CAN @ ${BITRATE} bit/s"
    else
        local bitrate_args=(type can bitrate "${BITRATE}")
        if [[ -n "${RESTART_MS}" ]]; then
            bitrate_args+=(restart-ms "${RESTART_MS}")
        fi
        echo "  setting ${bitrate_args[*]}"
        if ! ip link set "${iface}" "${bitrate_args[@]}" 2>&1; then
            # Idempotent re-run: interface may already be configured (EEXIST / busy).
            current_bitrate="$(iface_bitrate "${iface}")"
            if iface_is_can "${iface}" && [[ "${current_bitrate}" == "${BITRATE}" ]]; then
                echo "  bitrate already ${BITRATE} (ignored RTNETLINK error)"
            else
                echo "  failed to configure ${iface}" >&2
                echo "  current:" >&2
                ip -details link show "${iface}" >&2 || true
                echo "  hint: sudo lsof +f -- /sys/class/net/${iface} 2>/dev/null; stop those processes" >&2
                return 1
            fi
        fi
    fi

    ip link set "${iface}" txqueuelen 1000 2>/dev/null || true
    ip link set "${iface}" up

    ip -details link show "${iface}" | grep -E 'state|bitrate|link/can|restart-ms' || true
    echo
}

failed=0
for iface in "${INTERFACES[@]}"; do
    if ! setup_iface "${iface}"; then
        failed=1
    fi
done

if [[ "${failed}" -ne 0 ]]; then
    exit 1
fi

echo "Done. Point InfyPower can_device at can0 (125 kbit/s)."
