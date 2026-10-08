#!/usr/bin/env bash
# datagram_selftest.sh - privileged real-multicast self-test for the generic
# unconnected UDP client (udp_unconnected_client / udp_unconnected_socket).
#
# NOT a ctest case. The gtest suite (lib/everest/io/test/) covers parsing,
# loopback round-trips and connect-drop-absence on the loopback interface.
# This script exercises the one thing loopback cannot: a *real* multicast
# send on a dedicated NIC followed by a *unicast* reply from a source whose
# address differs from the multicast group. A connected socket would drop
# that reply; the unconnected client must deliver it.
#
# It is fully rootless: it runs inside a user+network namespace via
#   unshare --user --map-root-user --net
# so it needs no sudo and touches no host interface. It builds its own
# `dummy` NIC, runs a group-joining responder that answers unicast, and
# checks the example client receives the reply from the responder's unicast
# address (!= group) for both an IPv4 and an IPv6 multicast group.
#
# Usage:
#   lib/everest/io/test/datagram_selftest.sh [path-to-test_udp_unconnected_client]
#
# Exit status: 0 = both v4 and v6 passed; non-zero = failure.

set -euo pipefail

ROOT=$(git rev-parse --show-toplevel 2>/dev/null || echo "")
DEFAULT_BIN="${ROOT:-.}/build/lib/everest/io/examples/test_udp_unconnected_client"
CLIENT_BIN="${1:-$DEFAULT_BIN}"

if [ ! -x "$CLIENT_BIN" ]; then
    echo "ERROR: client example not found/executable: $CLIENT_BIN" >&2
    echo "Build it with: cmake ... -DBUILD_EXAMPLES=ON && ninja -C build test_udp_unconnected_client" >&2
    exit 2
fi

if ! unshare --user --map-root-user --net true 2>/dev/null; then
    echo "ERROR: rootless 'unshare --user --map-root-user --net' unavailable on this host" >&2
    exit 2
fi

# Everything below runs inside the fresh user+net namespace. The inner script
# is single-quoted on purpose: it is interpreted by the namespaced bash, where
# CLIENT_BIN arrives via the exported environment.
export CLIENT_BIN
# shellcheck disable=SC2016
exec unshare --user --map-root-user --net -- bash -euo pipefail -c '
IFACE=dt0
V4_GROUP=239.55.0.1
V4_IF_ADDR=10.55.0.1
V6_GROUP=ff12::55
V6_IF_ADDR=fd00:55::1
PORT=49555

ip link set lo up
ip link add "$IFACE" type dummy
ip addr add "$V4_IF_ADDR"/24 dev "$IFACE"
ip -6 addr add "$V6_IF_ADDR"/64 dev "$IFACE" nodad
ip link set "$IFACE" up
# Route the admin-scoped multicast ranges out the dummy NIC.
ip route add 239.0.0.0/8 dev "$IFACE"
ip -6 route add ff00::/8 dev "$IFACE"

RESPONDER=$(mktemp)
trap "rm -f \"$RESPONDER\"" EXIT
cat > "$RESPONDER" <<"PY"
import socket, struct, sys
fam = socket.AF_INET6 if sys.argv[1] == "6" else socket.AF_INET
group, ifaddr, port = sys.argv[2], sys.argv[3], int(sys.argv[4])
s = socket.socket(fam, socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
if fam == socket.AF_INET:
    s.bind(("", port))
    mreq = socket.inet_pton(socket.AF_INET, group) + socket.inet_aton(ifaddr)
    s.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
else:
    s.bind(("", port))
    ifidx = socket.if_nametoindex("dt0")
    mreq = socket.inet_pton(socket.AF_INET6, group) + struct.pack("I", ifidx)
    s.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_JOIN_GROUP, mreq)
s.settimeout(8)
# Reply unicast from our interface address (source != multicast group).
r = socket.socket(fam, socket.SOCK_DGRAM)
r.bind((ifaddr, 0))
try:
    while True:
        data, addr = s.recvfrom(2048)
        r.sendto(b"reply:" + data, addr)
except socket.timeout:
    pass
PY

run_case() {
    fam="$1"; group="$2"; ifaddr="$3"; label="$4"
    python3 "$RESPONDER" "$fam" "$group" "$ifaddr" "$PORT" &
    rpid=$!
    sleep 0.7
    out=$(stdbuf -oL timeout 4 stdbuf -oL "$CLIENT_BIN" "$group" "$PORT" "$IFACE" 2>&1 || true)
    wait "$rpid" 2>/dev/null || true
    echo "----- $label client output -----"
    echo "$out" | grep -aE "TX:|RX \(" | head -6 || true
    # Pass: an RX line whose source address is the responder unicast addr,
    # which is NOT the multicast group.
    if echo "$out" | grep -aq "RX (.*from \[${ifaddr}\]" && \
       ! echo "$out" | grep -aq "RX (.*from \[${group}\]"; then
        echo "PASS ($label): unicast reply from $ifaddr delivered (group $group)"
        return 0
    fi
    echo "FAIL ($label): no unicast reply from $ifaddr (group $group)"
    return 1
}

rc=0
run_case 4 "$V4_GROUP" "$V4_IF_ADDR" "IPv4" || rc=1
run_case 6 "$V6_GROUP" "$V6_IF_ADDR" "IPv6" || rc=1
if [ "$rc" -eq 0 ]; then
    echo "datagram_selftest: ALL PASS (v4 + v6)"
else
    echo "datagram_selftest: FAILURES" >&2
fi
exit "$rc"
'
