#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
#
# Run a command inside a private network namespace that supplies the two
# facilities the HLC / ISO 15118 SIL tests need from the host: an interface
# carrying an IPv6 link-local address, and an MQTT broker on port 1883.
#
# Usage:
#   ./run-in-netns.sh COMMAND [ARGS...]
#
# Examples:
#   ./run-in-netns.sh ./run-tests.sh ocpp16
#   ./run-in-netns.sh python3 -m pytest ocpp_tests/test_sets/ocpp16/plug_and_charge_tests.py
#
# Why it matters. EvseV2G and the EV simulator are configured with
# "device: auto", which binds the first interface that has an IPv6 link-local
# address (choose_first_ipv6_interface in modules/EVSE/EvseV2G/tools.cpp). On a
# developer machine that resolves to wlan0 or eth0, and with no link-local
# interface at all the SECC does not start. Either way the ISO 15118 tests fail
# on downstream assertions, which read like charging regressions rather than a
# networking problem. One measured sweep lost 41 tests to it. Inside this
# namespace v2g0 is the only candidate, so "auto" can only resolve to it. See
# tests/README.md.
#
# Why a wrapper rather than a setup/teardown pair like setup-network-isolation.sh:
# an unprivileged network namespace exists only while a process holds it, and
# naming one (ip netns add) needs root. There is nothing a later, unrelated
# process could enter, so running the command inside the namespace is the only
# way to hand it the interface. It also means the broker and the interface go
# away with the command, whatever it exits on.
#
# Nothing outside the namespace is touched and no root is needed, but the
# namespace has no route to the host network: everything a command needs must
# live inside it. The everest-core SIL suites qualify, since their CSMS, broker
# and EV simulator are all started by the test process.
#
# Exit status: the command's own, or 70 when the namespace could not be prepared.

set -euo pipefail

# Exported, because the namespace payload below is a single-quoted string that
# expands nothing at spawn time. Prefixed, because the wrapped command inherits
# this environment.
export NETNS_FAILURE=70
export NETNS_IFACE=v2g0
export NETNS_SELF="${BASH_SOURCE##*/}"

die() {
    echo "$NETNS_SELF: $*" >&2
    exit "$NETNS_FAILURE"
}

if [[ $# -eq 0 ]]; then
    die "no command given. Usage: $NETNS_SELF COMMAND [ARGS...]"
fi

for tool in unshare ip mosquitto; do
    command -v "$tool" >/dev/null 2>&1 \
        || die "'$tool' was not found on PATH, and it is needed to build the test network namespace."
done

unshare -rn true 2>/dev/null \
    || die "'unshare -rn true' failed, so unprivileged user namespaces are not permitted here."

NETNS_RUN_DIR="$(mktemp -d)"
export NETNS_RUN_DIR
trap 'rm -rf "$NETNS_RUN_DIR"' EXIT

cat > "$NETNS_RUN_DIR/mosquitto.conf" <<'CONF'
listener 1883
allow_anonymous true
# Inside "unshare -r" we are uid 0, so mosquitto wants to drop privileges and
# exits because the user it would drop to does not exist in the namespace.
user root
CONF

# Deliberately not exec: this shell stays alive to run the EXIT trap above.
status=0
unshare -rn bash -c '
set -euo pipefail

die() {
    echo "$NETNS_SELF: $*" >&2
    exit "$NETNS_FAILURE"
}

ip link set lo up || die "could not bring up lo inside the namespace."

ip link add "$NETNS_IFACE" type veth peer name "$NETNS_IFACE-peer" \
    || die "could not create the $NETNS_IFACE veth pair inside the namespace."
# Both ends get a link-local address: one carries the SECC, and neighbour
# discovery needs somebody to answer on the other.
ip addr add fe80::1/64 dev "$NETNS_IFACE" nodad
ip addr add fe80::2/64 dev "$NETNS_IFACE-peer" nodad
ip link set "$NETNS_IFACE" up
ip link set "$NETNS_IFACE-peer" up

mosquitto -c "$NETNS_RUN_DIR/mosquitto.conf" > "$NETNS_RUN_DIR/mosquitto.log" 2>&1 &

broker_ready() { (exec 3<>/dev/tcp/127.0.0.1/1883) 2>/dev/null; }

ready=false
for ((i = 0; i < 50; i++)); do
    if broker_ready; then ready=true; break; fi
    sleep 0.2
done
if [[ "$ready" != true ]]; then
    echo "--- mosquitto log ---" >&2
    cat "$NETNS_RUN_DIR/mosquitto.log" >&2 || true
    die "the MQTT broker did not accept connections on 127.0.0.1:1883 within 10s."
fi

echo "$NETNS_SELF: $NETNS_IFACE veth pair is up (fe80::1, fe80::2), MQTT broker on 127.0.0.1:1883"
exec "$@"
' bash "$@" || status=$?
exit "$status"
