#!/usr/bin/env bash
# MCS bring-up, EV side INSIDE A NETWORK NAMESPACE - the missing piece for neighbour
# discovery / EV-MAC / neighbour-liveness testing on a single bench host.
#
# Why: with both taps in one network stack, IP traffic between them (172.25.6.1 <->
# 172.25.6.2) is short-circuited by local routing and never touches the SPE wire, so
# ARP never crosses it and McsDataLink's neighbour machinery has nothing to see. Running
# the WHOLE EV vertical (daemon + EVerest instance + panels) in its own namespace makes
# the two tap stacks genuinely separate: the EV daemon creates cb_plc_ev inside the
# namespace, and every cross-link packet must go over the copper.
#
# Two lifelines reach into the namespace:
#  - a veth pair (host 10.200.0.1 <-> ns 10.200.0.2) with NAT, so the daemon's UDP to the
#    EV MCU (192.168.188.x, explicit IP - mDNS does not cross NAT) keeps working
#  - the MQTT broker, via a socat forward bound to the veth's host address, so
#    mosquitto's own config stays untouched. All EV-side "localhost" MQTT endpoints are
#    redirected: the daemon via a derived config (sed), manager and panels via
#    MQTT_SERVER_ADDRESS.
#
# Usage: sudo run-mcs-ev-bringup-netns.sh [dist-prefix]     (root: ip netns needs it)
#        sudo run-mcs-ev-bringup-netns.sh --teardown
#
# Inside the namespace this hands off to the plain run-mcs-ev-bringup.sh, dropped back
# to the invoking user with CAP_NET_ADMIN+CAP_NET_RAW ambient - that script recognizes
# the caps and starts its tmux server without any further prompt. The namespace and the
# NAT rule persist across runs (cheap, reusable); --teardown removes them.
#
# Bench-habit note: cb_plc_ev now lives in the namespace -
#   sudo ip netns exec mcs-ev cat /sys/class/net/cb_plc_ev/carrier
# and the neighbour proof on the EVSE side (root namespace) is
#   ip neigh show dev cb_plc

set -e

NS=mcs-ev
VETH_HOST=veth-mcs-ev
VETH_NS=eth-mcs-ev
HOST_IP=10.200.0.1
NS_IP=10.200.0.2
NET=10.200.0.0/24
SOCAT_PIDFILE=/run/mcs-ev-netns-socat.pid

if [ "$(id -u)" != "0" ]; then
    # Self-elevate: ip netns needs root. Still exactly one password for the whole session -
    # inside the namespace everything drops back to the invoking user with ambient caps, so the
    # inner script prompts for nothing. EV_INNER_SCRIPT (and CB_CONFIG) must survive the
    # elevation - sudo strips the environment, so pass them as VAR=value arguments.
    echo "one sudo prompt: namespace setup needs root"
    exec sudo -- env ${EV_INNER_SCRIPT:+EV_INNER_SCRIPT="$EV_INNER_SCRIPT"} \
        ${CB_CONFIG:+CB_CONFIG="$CB_CONFIG"} "$0" "$@"
fi

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)

stop_socat() {
    if [ -f "$SOCAT_PIDFILE" ]; then
        kill "$(cat "$SOCAT_PIDFILE")" 2>/dev/null || true
        rm -f "$SOCAT_PIDFILE"
    fi
}

if [ "${1:-}" = "--teardown" ]; then
    stop_socat
    ip netns del $NS 2>/dev/null || true # takes its veth end - and thereby the host end - with it
    iptables -t nat -D POSTROUTING -s $NET -j MASQUERADE 2>/dev/null || true
    iptables -D FORWARD -s $NET -j ACCEPT 2>/dev/null || true
    iptables -D FORWARD -d $NET -j ACCEPT 2>/dev/null || true
    iptables -D INPUT -i $VETH_HOST -j ACCEPT 2>/dev/null || true
    iptables -D OUTPUT -o $VETH_HOST -j ACCEPT 2>/dev/null || true
    ip rule del to $NET lookup main priority 5000 2>/dev/null || true
    echo "namespace '$NS' torn down"
    exit 0
fi

# --- idempotent namespace + plumbing ------------------------------------------------------------
ip netns add $NS 2>/dev/null || true
if ! ip link show $VETH_HOST >/dev/null 2>&1; then
    ip link add $VETH_HOST type veth peer name $VETH_NS netns $NS
fi
ip addr replace $HOST_IP/24 dev $VETH_HOST
ip link set $VETH_HOST up
ip -n $NS addr replace $NS_IP/24 dev $VETH_NS
ip -n $NS link set $VETH_NS up
ip -n $NS link set lo up
ip -n $NS route replace default via $HOST_IP
sysctl -qw net.ipv4.ip_forward=1
iptables -t nat -C POSTROUTING -s $NET -j MASQUERADE 2>/dev/null ||
    iptables -t nat -A POSTROUTING -s $NET -j MASQUERADE
# NAT alone is not enough where a firewall (ufw and friends) sets the FORWARD policy to DROP:
# the daemon's UDP to the MCU dies silently in the filter table. Inserted at the top so no
# distro chain gets there first; idempotent and harmless where the policy is ACCEPT anyway.
iptables -C FORWARD -s $NET -j ACCEPT 2>/dev/null || iptables -I FORWARD 1 -s $NET -j ACCEPT
iptables -C FORWARD -d $NET -j ACCEPT 2>/dev/null || iptables -I FORWARD 1 -d $NET -j ACCEPT
# The same firewall's "deny incoming" also silences traffic addressed to the HOST itself on the
# veth - the MQTT forward on 10.200.0.1 and any ping at it are INPUT, not FORWARD.
iptables -C INPUT -i $VETH_HOST -j ACCEPT 2>/dev/null || iptables -I INPUT 1 -i $VETH_HOST -j ACCEPT
iptables -C OUTPUT -o $VETH_HOST -j ACCEPT 2>/dev/null || iptables -I OUTPUT 1 -o $VETH_HOST -j ACCEPT
# Tailscale-style policy routing can capture the veth subnet into its own table - bench-found:
# replies to $NS_IP left via tailscale0 (table 52), so both ping and the daemon's MCU replies
# vanished after a perfectly accepted request. Pin the subnet to the main table (which holds the
# connected veth route) ahead of those rules; Tailscale's start at priority 5210.
ip rule del to $NET lookup main priority 5000 2>/dev/null || true
ip rule add to $NET lookup main priority 5000

# --- broker forward -----------------------------------------------------------------------------
if ! ss -ltn "sport = :1883" | grep -q 1883; then
    echo "warning: nothing listens on :1883 - is mosquitto running?" >&2
fi
stop_socat
socat TCP-LISTEN:1883,bind=$HOST_IP,fork,reuseaddr TCP:127.0.0.1:1883 &
echo $! >"$SOCAT_PIDFILE"

# --- derived daemon config: every localhost MQTT endpoint -> the veth --------------------------
CB_CONFIG_SRC=${CB_CONFIG:-$SCRIPT_DIR/../../applications/pionix_chargebridge/config/config-CB-MCS-EV.yaml}
if [ ! -f "$CB_CONFIG_SRC" ]; then
    echo "ChargeBridge daemon config not found ('$CB_CONFIG_SRC') - set CB_CONFIG" >&2
    exit 1
fi
DERIVED_CONFIG=$(mktemp /tmp/config-CB-MCS-EV-netns.XXXXXX.yaml)
trap 'rm -f "$DERIVED_CONFIG"' EXIT
sed -e "s/mqtt_remote: \"localhost\"/mqtt_remote: \"$HOST_IP\"/" \
    -e "s/mqtt_bind: 127.0.0.1/mqtt_bind: $NS_IP/" \
    "$CB_CONFIG_SRC" >"$DERIVED_CONFIG"
# mktemp under sudo creates the file root-owned mode 0600, and everything past the handoff runs
# as the invoking user again - who must be able to read their own daemon config.
chmod 0644 "$DERIVED_CONFIG"

# --- hand off into the namespace, dropped back to the invoking user ----------------------------
RUN_USER=${SUDO_USER:-root}
RUN_HOME=$(getent passwd "$RUN_USER" | cut -d: -f6)
ip netns exec $NS setpriv --reuid="$RUN_USER" --regid="$(id -g "$RUN_USER")" --init-groups \
    --inh-caps=+net_admin,+net_raw --ambient-caps=+net_admin,+net_raw -- \
    env HOME="$RUN_HOME" USER="$RUN_USER" LOGNAME="$RUN_USER" SHELL=/bin/bash \
    TERM="${TERM:-xterm-256color}" PATH="$PATH" \
    MQTT_SERVER_ADDRESS=$HOST_IP MQTT_SERVER_PORT=1883 CB_CONFIG="$DERIVED_CONFIG" \
    "${EV_INNER_SCRIPT:-$SCRIPT_DIR/run-mcs-ev-bringup.sh}" "$@"

# The inner script's tmux session has ended; the derived config is removed by the EXIT trap.
stop_socat
echo "session ended; namespace '$NS' kept for the next run ('$0 --teardown' removes it)"
