#!/bin/bash

# --------------------------------------------------------------------------
#
#  WARNING: This script is an example and not intended for production use.
#
#  This script is provided as a starting point for setting up internet
#  access for an EV. It is not a complete solution and may require
#  modification to work in your specific environment.
#
#  It is the responsibility of the user to ensure that the script is
#  secure and does not introduce any security vulnerabilities.
#
# --------------------------------------------------------------------------

# This script manages the full network setup to provide internet access
# to an EV. It handles IP forwarding, NAT, a DHCP server, and a
# router advertisement daemon.

set -euo pipefail

# --- Configuration ---
DHCP_STATIC_IP="172.18.200.1"
SUBNET_MASK="24"
RADVD_IPV6_PREFIX="fd00::/64"

# --- File Paths ---
PID_DIR="/var/run"
DHCP_PID_FILE="${PID_DIR}/vas_udhcpd.pid"
RADVD_PID_FILE="${PID_DIR}/vas_radvd.pid"

CONF_DIR="/tmp"
FORWARDING_STATE_DIR="${PID_DIR}"

# Resolvers announced to the EV; DNS to them is the only traffic besides PORTS that is forwarded
DNS_SERVERS_V4="8.8.8.8 8.8.4.4"
DNS_SERVERS_V6="2001:4860:4860::8888 2001:4860:4860::8844"

# The EV never reaches non-public destinations. Add the backend networks (CSMS, management)
# that are reachable via the WAN interface in /etc/default/vas-internet.
BLOCKED_DESTINATIONS_V4="0.0.0.0/8 10.0.0.0/8 100.64.0.0/10 127.0.0.0/8 169.254.0.0/16 172.16.0.0/12 192.0.0.0/24 192.168.0.0/16 198.18.0.0/15 224.0.0.0/4 240.0.0.0/4"
BLOCKED_DESTINATIONS_V6="::/128 ::1/128 ::ffff:0:0/96 64:ff9b::/96 100::/64 fc00::/7 fe80::/10 ff00::/8"
EXTRA_BLOCKED_DESTINATIONS_V4=""
EXTRA_BLOCKED_DESTINATIONS_V6=""
DHCP_CONF_FILE="${CONF_DIR}/vas_udhcpd.conf"
RADVD_CONF_FILE="${CONF_DIR}/vas_radvd.conf"

# Load optional configuration from /etc/default/vas-internet
[ -f /etc/default/vas-internet ] && . /etc/default/vas-internet

# --- Helper Functions ---
need_root() {
  if [[ $EUID -ne 0 ]]; then
    echo "Please run as root (use sudo)." >&2
    exit 1
  fi
}

cmd_exists() {
  command -v "$1" >/dev/null 2>&1
}

check_deps() {
    local use_ipv4=$1
    local deps="ip ip6tables radvd"
    if [ "$use_ipv4" = true ]; then
        deps="$deps iptables udhcpd"
    fi

    for cmd in $deps; do
        if ! cmd_exists "$cmd"; then
            echo "Error: Command '$cmd' not found. Please install it." >&2
            exit 1
        fi
    done
}

# Forwarding is enabled for every interface, so the previous value is restored on "down"
save_and_set_sysctl() {
  local key=$1
  local state_file="${FORWARDING_STATE_DIR}/vas_${key//./_}.prev"
  [ -f "$state_file" ] || sysctl -n "$key" > "$state_file"
  sysctl -w "$key=1" >/dev/null
}

restore_sysctl() {
  local key=$1
  local state_file="${FORWARDING_STATE_DIR}/vas_${key//./_}.prev"
  if [ -f "$state_file" ]; then
    sysctl -w "$key=$(cat "$state_file")" >/dev/null
    rm -f "$state_file"
  fi
}

# VAS_FORWARD sees everything the EV sends through the charger, VAS_RETURN everything forwarded
# towards the EV. Both end in DROP, so the FORWARD policy of the system does not matter.
setup_filter() {
  local tables=$1
  local lan=$2
  local wan=$3
  local ports=$4
  local ev_subnet=$5
  local blocked=$6
  local dns_servers=$7

  local chain
  for chain in VAS_FORWARD VAS_RETURN; do
    $tables -N "$chain" 2>/dev/null || $tables -F "$chain"
  done

  $tables -A VAS_FORWARD ! -o "$wan" -j DROP
  local destination
  for destination in $blocked; do
    $tables -A VAS_FORWARD -d "$destination" -j DROP
  done
  local server
  for server in $dns_servers; do
    $tables -A VAS_FORWARD -d "$server" -p udp --dport 53 -j ACCEPT
    $tables -A VAS_FORWARD -d "$server" -p tcp --dport 53 -j ACCEPT
  done
  $tables -A VAS_FORWARD -p tcp -m multiport --dports "$ports" -j ACCEPT
  $tables -A VAS_FORWARD -j DROP

  $tables -A VAS_RETURN -i "$wan" -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT
  $tables -A VAS_RETURN -j DROP

  $tables -C FORWARD -i "$lan" -j VAS_FORWARD 2>/dev/null || $tables -I FORWARD 1 -i "$lan" -j VAS_FORWARD
  $tables -C FORWARD -o "$lan" -j VAS_RETURN 2>/dev/null || $tables -I FORWARD 1 -o "$lan" -j VAS_RETURN

  $tables -t nat -C POSTROUTING -s "$ev_subnet" -o "$wan" -j MASQUERADE 2>/dev/null || \
    $tables -t nat -A POSTROUTING -s "$ev_subnet" -o "$wan" -j MASQUERADE
}

remove_filter() {
  local tables=$1
  local lan=$2
  local wan=$3
  local ev_subnet=$4

  $tables -D FORWARD -i "$lan" -j VAS_FORWARD 2>/dev/null || true
  $tables -D FORWARD -o "$lan" -j VAS_RETURN 2>/dev/null || true
  local chain
  for chain in VAS_FORWARD VAS_RETURN; do
    $tables -F "$chain" 2>/dev/null || true
    $tables -X "$chain" 2>/dev/null || true
  done
  $tables -t nat -D POSTROUTING -s "$ev_subnet" -o "$wan" -j MASQUERADE 2>/dev/null || true
}

# --- Core Logic Functions ---

start_services() {
  local LAN_IFACE=$1
  local WAN_IFACE=$2
  local PORTS=${3:-}
  local USE_IPV4=$4

  echo "[+] Starting all internet VAS services for $LAN_IFACE -> $WAN_IFACE"

  # 1. Enable IP Forwarding, remembering the previous setting for "down"
  echo "  [1/4] Enabling IP forwarding"
  if [ "$USE_IPV4" = true ]; then
    save_and_set_sysctl net.ipv4.ip_forward
  fi
  save_and_set_sysctl net.ipv6.conf.all.forwarding

  # 2. Setup NAT and Forwarding Rules
  echo "  [2/4] Setting up NAT and FORWARD rules on $WAN_IFACE"
  echo "    - Allowing outgoing traffic on TCP ports: $PORTS"
  if [ "$USE_IPV4" = true ]; then
    setup_filter iptables "$LAN_IFACE" "$WAN_IFACE" "$PORTS" "${DHCP_STATIC_IP%.*}.0/${SUBNET_MASK}" \
      "$BLOCKED_DESTINATIONS_V4 $EXTRA_BLOCKED_DESTINATIONS_V4" "$DNS_SERVERS_V4"
  fi
  setup_filter ip6tables "$LAN_IFACE" "$WAN_IFACE" "$PORTS" "$RADVD_IPV6_PREFIX" \
    "$BLOCKED_DESTINATIONS_V6 $EXTRA_BLOCKED_DESTINATIONS_V6" "$DNS_SERVERS_V6"

  # 3. Start DHCP server
  if [ "$USE_IPV4" = true ]; then
    echo "  [3/4] Starting DHCPv4 server on $LAN_IFACE"
    local IP_WITH_SUBNET="${DHCP_STATIC_IP}/${SUBNET_MASK}"
    ip addr show dev "${LAN_IFACE}" | grep -q "${IP_WITH_SUBNET}" || ip addr add "${IP_WITH_SUBNET}" dev "${LAN_IFACE}"

    local NETWORK_BASE=$(echo "$DHCP_STATIC_IP" | cut -d. -f1-3)
    cat > "${DHCP_CONF_FILE}" << EOF
interface   ${LAN_IFACE}
start       ${NETWORK_BASE}.2
end         ${NETWORK_BASE}.200
option      subnet  255.255.255.0
option      router  ${DHCP_STATIC_IP}
option      dns     ${DNS_SERVERS_V4}
option      lease   864000
pidfile     ${DHCP_PID_FILE}
EOF
    udhcpd -S "${DHCP_CONF_FILE}"
  else
    echo "  [3/4] Skipping DHCPv4 server (IPv4 disabled)"
  fi

  # 4. Start RADVD server
  echo "  [4/4] Starting DHCPv6/RADVD server on $LAN_IFACE"
  cat > "${RADVD_CONF_FILE}" << EOF
interface ${LAN_IFACE}
{
  AdvSendAdvert on;
  MinRtrAdvInterval 30;
  MaxRtrAdvInterval 100;
  prefix ${RADVD_IPV6_PREFIX}
  {
    AdvOnLink on;
    AdvAutonomous on;
    AdvRouterAddr off;
  };
  RDNSS ${DNS_SERVERS_V6}
  {
    AdvRDNSSLifetime 3600;
  };
};
EOF
  radvd -C "${RADVD_CONF_FILE}" -p "${RADVD_PID_FILE}"

  echo "[✓] All services started."
}

stop_services() {
  local LAN_IFACE=$1
  local WAN_IFACE=$2
  local PORTS=$3
  local USE_IPV4=$4

  echo "[+] Stopping all internet VAS services for $LAN_IFACE -> $WAN_IFACE"

  # 1. Stop daemons
  if [ "$USE_IPV4" = true ]; then
    echo "  [1/3] Stopping DHCPv4 server"
    if [ -f "$DHCP_PID_FILE" ]; then
      kill "$(cat "$DHCP_PID_FILE")" || echo "DHCP server already stopped."
      rm -f "$DHCP_PID_FILE"
    else
      echo "DHCP PID file not found. Maybe it was not running."
    fi
  fi

  echo "  [2/3] Stopping DHCPv6/RADVD server"
  if [ -f "$RADVD_PID_FILE" ]; then
    kill "$(cat "$RADVD_PID_FILE")" || echo "RADVD server already stopped."
    rm -f "$RADVD_PID_FILE"
  else
    echo "RADVD PID file not found. Maybe it was not running."
  fi

  # 2. Remove NAT and FORWARD rules
  echo "  [3/3] Removing NAT and FORWARD rules"

  if [ "$USE_IPV4" = true ]; then
    remove_filter iptables "$LAN_IFACE" "$WAN_IFACE" "${DHCP_STATIC_IP%.*}.0/${SUBNET_MASK}"
    restore_sysctl net.ipv4.ip_forward
  fi
  remove_filter ip6tables "$LAN_IFACE" "$WAN_IFACE" "$RADVD_IPV6_PREFIX"
  restore_sysctl net.ipv6.conf.all.forwarding

  echo "[✓] All services stopped and rules removed."
}

usage() {
  cat <<EOF
Usage:
  sudo $0 up <LAN_IFACE> <WAN_IFACE> <PORTS> [--ipv4]
  sudo $0 down <LAN_IFACE> <WAN_IFACE> <PORTS> [--ipv4]

Commands:
  up    - Configures interfaces, enables NAT/forwarding, and starts DHCP/RADVD.
  down  - Stops daemons and removes all created network rules.

Arguments:
  PORTS - Comma-separated list of TCP ports to allow (e.g., "80,443").
  --ipv4 - Optional flag to enable all IPv4-related setup (disabled by default).
EOF
}

main() {
  need_root

  local use_ipv4=false
  local args=()
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --ipv4)
        use_ipv4=true
        shift
        ;;
      *)
        args+=("$1")
        shift
        ;;
    esac
  done

  check_deps "$use_ipv4"

  local cmd="${args[0]:-}"
  if [[ ${#args[@]} -ne 4 ]]; then
    usage
    exit 1
  fi

  case "$cmd" in
    up)
      start_services "${args[1]}" "${args[2]}" "${args[3]}" "$use_ipv4"
      ;;
    down)
      stop_services "${args[1]}" "${args[2]}" "${args[3]}" "$use_ipv4"
      ;;
    *)
      usage
      exit 1
      ;;
  esac
}

main "$@"
