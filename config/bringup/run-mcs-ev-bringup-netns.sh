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
#  - the EV MCU link. The daemon config addresses the MCU as an IPv6 LINK-LOCAL literal
#    scoped to its USB-Ethernet interface ("fe80::...%eth1"). Link-local traffic can never
#    be routed or NAT'ed, and the scope name must resolve INSIDE the namespace - so the
#    harness moves that interface itself into the namespace for the session (parsed from
#    charge_bridge.ip; override with EV_MCU_IFACE=<if>) and hands it back to the root
#    namespace when the session ends (NetworkManager re-adopts it). Only when the config
#    holds no "%iface" (legacy IPv4 / mDNS setups) the old lifeline applies: a veth pair
#    (host 10.200.0.1 <-> ns 10.200.0.2) with NAT for the daemon's UDP to an explicit
#    IPv4 MCU address (mDNS does not cross NAT).
#  - the LAN leg for mDNS discovery. With charge_bridge.ip = ANY_EV the daemon has to HEAR the
#    board's announcement, and multicast never crosses the veth/NAT. So the harness gives the
#    namespace its own presence on the bench LAN: a macvlan child of the host's LAN interface
#    (mcs-ev-lan, default parent = the default-route interface, EV_LAN_IFACE overrides; empty
#    disables), addressed by DHCP through dhcpcd (EV_LAN_ADDR=<cidr> for a static address
#    instead). Also used when EV_LAN_IFACE is set explicitly with a literal MCU address - the
#    MCU traffic then leaves on-link instead of through NAT. Kept across runs like the
#    namespace; --teardown removes it (and its dhcpcd). Wired parents only: macvlan does not
#    work on Wi-Fi. The host cannot reach its own macvlan child over the parent - irrelevant
#    here, the board is an external device.
#    TRAP: the CB firmware uses ONE MAC for both ends of its USB CDC link, so the kernel's
#    default EUI-64 link-local for the moved interface would be the MCU's own address and
#    fail DAD. The harness therefore disables autoconf on it (addr_gen_mode=1) and assigns
#    a fixed fe80::2 before bringing it up. (On the host NetworkManager sidesteps the
#    same clash with stable-privacy addresses.)
#  - the MQTT broker, via a socat forward bound to the veth's host address, so
#    mosquitto's own config stays untouched. All EV-side "localhost" MQTT endpoints are
#    redirected: the daemon via a derived config (sed), manager and panels via
#    MQTT_SERVER_ADDRESS.
#
# Usage: sudo run-mcs-ev-bringup-netns.sh [dist-prefix]     (root: ip netns needs it)
#        sudo run-mcs-ev-bringup-netns.sh --teardown
# Environment:
#   CB_CONFIG      daemon config (default: applications/pionix_chargebridge/config/config-CB-MCS-EV.yaml)
#   EV_MCU_IFACE   interface to move into the namespace for a "%iface" MCU address (see above)
#   EV_LAN_IFACE   parent for the LAN macvlan; default: default-route interface when the config
#                  uses ANY_EV, else off. Set explicitly to force it on, set EMPTY to force it off.
#   EV_LAN_ADDR    "dhcp" (default) or a static <addr>/<prefix> for the LAN macvlan
#
# Inside the namespace this hands off to the plain run-mcs-ev-bringup.sh, dropped back
# to the invoking user with CAP_NET_ADMIN+CAP_NET_RAW ambient - that script recognizes
# the caps and starts its tmux server without any further prompt. The namespace and the
# NAT rule persist across runs (cheap, reusable); --teardown removes them.
#
# Bench-habit note: cb_plc_ev - and, while a session runs, the EV MCU's USB interface -
# live in the namespace:
#   sudo ip netns exec mcs-ev cat /sys/class/net/cb_plc_ev/carrier
#   sudo ip netns exec mcs-ev ping fe80::46b7:d0ff:fec8:fb7b%eth1
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
# Fixed link-local for the moved MCU interface inside the namespace (see the header TRAP).
MCU_IF_LL=fe80::2
# LAN leg (mDNS discovery): macvlan child of the host's LAN interface, living in the namespace.
LAN_IF_NS=mcs-ev-lan

if [ "$(id -u)" != "0" ]; then
    # Self-elevate: ip netns needs root. Still exactly one password for the whole session -
    # inside the namespace everything drops back to the invoking user with ambient caps, so the
    # inner script prompts for nothing. EV_INNER_SCRIPT (and CB_CONFIG) must survive the
    # elevation - sudo strips the environment, so pass them as VAR=value arguments
    # (EV_MCU_IFACE too, including an explicitly EMPTY one = "do not move any interface").
    echo "one sudo prompt: namespace setup needs root"
    exec sudo -- env ${EV_INNER_SCRIPT:+EV_INNER_SCRIPT="$EV_INNER_SCRIPT"} \
        ${CB_CONFIG:+CB_CONFIG="$CB_CONFIG"} ${EV_MCU_IFACE+EV_MCU_IFACE="$EV_MCU_IFACE"} \
        ${EV_LAN_IFACE+EV_LAN_IFACE="$EV_LAN_IFACE"} ${EV_LAN_ADDR:+EV_LAN_ADDR="$EV_LAN_ADDR"} "$0" "$@"
fi

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)

stop_socat() {
    if [ -f "$SOCAT_PIDFILE" ]; then
        kill "$(cat "$SOCAT_PIDFILE")" 2>/dev/null || true
        rm -f "$SOCAT_PIDFILE"
    fi
}

# Physical (non-veth) interfaces parked in the namespace go back to the root namespace;
# NetworkManager re-adopts them there. 'netns 1' = PID 1's namespace.
return_phys_ifaces() {
    local ifc
    for ifc in $(ip -n $NS -o link show 2>/dev/null | awk -F': ' '{print $2}' | cut -d@ -f1); do
        case "$ifc" in lo | "$VETH_NS" | "$LAN_IF_NS") continue ;; esac
        ip -n $NS link set "$ifc" netns 1 2>/dev/null || true
    done
}

# dhcpcd for the LAN macvlan runs inside the namespace and outlives sessions (the lease keeps
# renewing between runs); only --teardown stops it. Same /run, so its control socket is reachable.
stop_lan_dhcp() {
    ip netns exec $NS dhcpcd -k "$LAN_IF_NS" 2>/dev/null || pkill -f "dhcpcd.* $LAN_IF_NS\$" 2>/dev/null || true
}

if [ "${1:-}" = "--teardown" ]; then
    stop_socat
    return_phys_ifaces
    stop_lan_dhcp
    ip netns del $NS 2>/dev/null || true # takes its veth end - and thereby the host end - with it
    rm -f /tmp/config-CB-MCS-EV-netns.yaml
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
# Deterministic path, NOT mktemp, and deliberately NOT removed when this wrapper exits: the
# daemon inside the namespace reads it, and restarting that daemon from its pane after the
# wrapper has returned used to fail with "FAILED to parse configuration" because the file was
# already gone. --teardown removes it, together with the namespace it belongs to.
DERIVED_CONFIG=/tmp/config-CB-MCS-EV-netns.yaml
# Relative paths in the daemon config resolve against the config file's own directory, and the
# derived copy does not live there. fw_file is the one that matters: left relative it becomes
# /tmp/./firmware/... , the upload fails, and the daemon retries it every ~10 s instead of
# bringing its bridges up - the board is connected the whole time, which makes it look like a
# namespace networking fault. Absolutise it against the source config's directory.
CB_CONFIG_DIR=$(cd "$(dirname "$CB_CONFIG_SRC")" && pwd)
sed -e "s/mqtt_remote: \"localhost\"/mqtt_remote: \"$HOST_IP\"/" \
    -e "s/mqtt_bind: 127.0.0.1/mqtt_bind: $NS_IP/" \
    -e "s#^\([[:space:]]*fw_file:[[:space:]]*\)\./#\1$CB_CONFIG_DIR/#" \
    -e "s#^\([[:space:]]*fw_file:[[:space:]]*\)\([^/[:space:]]\)#\1$CB_CONFIG_DIR/\2#" \
    "$CB_CONFIG_SRC" >"$DERIVED_CONFIG"
# mktemp under sudo creates the file root-owned mode 0600, and everything past the handoff runs
# as the invoking user again - who must be able to read their own daemon config.
chmod 0644 "$DERIVED_CONFIG"

# --- EV MCU interface into the namespace (IPv6 link-local addressing) ---------------------------
# charge_bridge.ip "fe80::...%eth1" -> eth1. Only the first '%'-scoped address counts (plc.ip is
# the tap's plain IPv4). EV_MCU_IFACE overrides; empty = legacy veth/NAT lifeline, nothing moved.
MCU_IFACE=${EV_MCU_IFACE-$(sed -nE 's/^[[:space:]]*ip:[[:space:]]*"?\[?[^"[:space:]]*%([A-Za-z0-9_.-]+).*/\1/p' "$CB_CONFIG_SRC" | head -n1)}
cleanup() {
    stop_socat
    return_phys_ifaces
}
trap cleanup EXIT
if [ -n "$MCU_IFACE" ]; then
    # A daemon started from the SOURCE config in the root namespace is using that interface right
    # now (the inner script's zombie guard only knows the derived config's name).
    if pgrep -af "pionix_chargebridge.*$(basename "$CB_CONFIG_SRC")" >/dev/null 2>&1; then
        echo "ERROR: a pionix_chargebridge with $(basename "$CB_CONFIG_SRC") is running in the root namespace" >&2
        echo "       and holds $MCU_IFACE - stop it (or the plain run-mcs-ev-*.sh session) first:" >&2
        pgrep -af "pionix_chargebridge.*$(basename "$CB_CONFIG_SRC")" >&2
        exit 1
    fi
    if ip link show "$MCU_IFACE" >/dev/null 2>&1; then
        ip link set "$MCU_IFACE" down
        ip link set "$MCU_IFACE" netns $NS
    elif ! ip -n $NS link show "$MCU_IFACE" >/dev/null 2>&1; then
        echo "ERROR: MCU interface '$MCU_IFACE' (from charge_bridge.ip in $(basename "$CB_CONFIG_SRC"))" >&2
        echo "       exists neither in the root namespace nor in '$NS' - is the EV board's USB plugged in?" >&2
        exit 1
    fi
    # No kernel autoconf (would EUI-64 into the MCU's own address - same MAC on both ends of the
    # USB link), fixed link-local instead, then up and wait for DAD to clear it.
    ip -n $NS link set "$MCU_IFACE" down
    ip netns exec $NS sysctl -qw "net.ipv6.conf.$MCU_IFACE.addr_gen_mode=1"
    ip -n $NS addr replace "$MCU_IF_LL/64" dev "$MCU_IFACE" scope link
    ip -n $NS link set "$MCU_IFACE" up
    for _ in $(seq 1 50); do
        ip -n $NS -6 addr show dev "$MCU_IFACE" tentative | grep -q inet6 || break
        sleep 0.1
    done
    if ip -n $NS -6 addr show dev "$MCU_IFACE" dadfailed | grep -q inet6; then
        echo "ERROR: DAD failed for $MCU_IF_LL on $MCU_IFACE inside '$NS' - something else on that link" >&2
        echo "       already uses it; pick another MCU_IF_LL in $0" >&2
        exit 1
    fi
    if ip -n $NS -6 addr show dev "$MCU_IFACE" tentative | grep -q inet6; then
        echo "warning: $MCU_IF_LL on $MCU_IFACE still tentative after 5 s (link down? carrier=$(ip -n $NS -o link show "$MCU_IFACE" | grep -o 'state [A-Z]*'))" >&2
    fi
    echo "moved $MCU_IFACE into '$NS' ($MCU_IF_LL/64, returned to the root namespace when the session ends)"
fi

# --- LAN leg: macvlan into the namespace for mDNS discovery -------------------------------------
# On by default when the daemon config discovers the board (charge_bridge.ip = ANY_EV...): mDNS
# cannot work over the veth/NAT lifeline. EV_LAN_IFACE set = on with that parent, set empty = off.
if [ "${EV_LAN_IFACE+set}" = set ]; then
    LAN_IFACE=$EV_LAN_IFACE
elif grep -Eq '^[[:space:]]*ip:[[:space:]]*"?ANY_' "$CB_CONFIG_SRC"; then
    LAN_IFACE=$(ip -o route show default | awk '{for (i = 1; i <= NF; i++) if ($i == "dev") {print $(i + 1); exit}}')
    if [ -z "$LAN_IFACE" ]; then
        echo "warning: config uses mDNS discovery but no default route found - set EV_LAN_IFACE=<lan-if>," >&2
        echo "         otherwise the daemon in '$NS' cannot hear the board's announcement" >&2
    fi
else
    LAN_IFACE=
fi
if [ -n "$LAN_IFACE" ]; then
    if ! ip -n $NS link show "$LAN_IF_NS" >/dev/null 2>&1; then
        if ! ip link show "$LAN_IFACE" >/dev/null 2>&1; then
            echo "ERROR: LAN interface '$LAN_IFACE' for the macvlan does not exist (EV_LAN_IFACE)" >&2
            exit 1
        fi
        # Bridge mode: children and the wire see each other; the parent itself does not, which
        # is fine - the board is not the host.
        ip link add "$LAN_IF_NS" link "$LAN_IFACE" type macvlan mode bridge
        ip link set "$LAN_IF_NS" netns $NS
    fi
    ip -n $NS link set "$LAN_IF_NS" up
    if ip -n $NS -4 -o addr show dev "$LAN_IF_NS" | grep -q inet; then
        : # addressed from an earlier run (lease still held by the namespace's dhcpcd, or static)
    elif [ "${EV_LAN_ADDR:-dhcp}" != dhcp ]; then
        ip -n $NS addr replace "$EV_LAN_ADDR" dev "$LAN_IF_NS"
    elif command -v dhcpcd >/dev/null 2>&1; then
        # -4: IPv4 lease only (kernel SLAAC covers the link-local IPv6 mDNS needs); -G: no default
        # route from the lease, the namespace's default stays on the veth; -w: return once the
        # address is up; no resolv.conf hook - the namespace shares the host's /etc.
        if ! ip netns exec $NS dhcpcd -4 -G -w -q --nohook resolv.conf "$LAN_IF_NS"; then
            echo "warning: no DHCP lease on $LAN_IF_NS via $LAN_IFACE - set EV_LAN_ADDR=<addr>/<prefix> for a static one" >&2
        fi
    else
        echo "ERROR: dhcpcd not installed - set EV_LAN_ADDR=<addr>/<prefix> for the LAN macvlan (or EV_LAN_IFACE= to disable)" >&2
        exit 1
    fi
    LAN_ADDR_NOW=$(ip -n $NS -4 -o addr show dev "$LAN_IF_NS" | awk '{print $4}' | head -n1)
    echo "LAN leg $LAN_IF_NS (macvlan on $LAN_IFACE) up in '$NS' with ${LAN_ADDR_NOW:-no IPv4 address} - mDNS discovery can hear the board"
fi

# --- hand off into the namespace, dropped back to the invoking user ----------------------------
RUN_USER=${SUDO_USER:-root}
RUN_HOME=$(getent passwd "$RUN_USER" | cut -d: -f6)
ip netns exec $NS setpriv --reuid="$RUN_USER" --regid="$(id -g "$RUN_USER")" --init-groups \
    --inh-caps=+net_admin,+net_raw --ambient-caps=+net_admin,+net_raw -- \
    env HOME="$RUN_HOME" USER="$RUN_USER" LOGNAME="$RUN_USER" SHELL=/bin/bash \
    TERM="${TERM:-xterm-256color}" PATH="$PATH" \
    MQTT_SERVER_ADDRESS=$HOST_IP MQTT_SERVER_PORT=1883 CB_CONFIG="$DERIVED_CONFIG" \
    "${EV_INNER_SCRIPT:-$SCRIPT_DIR/run-mcs-ev-bringup.sh}" "$@"

# The inner script's tmux session has ended; the EXIT trap stops socat and hands the MCU interface
# back to the root namespace. The LAN macvlan stays with the namespace.
echo "session ended; namespace '$NS' kept for the next run ('$0 --teardown' removes it)"
