#!/usr/bin/env bash
# MCS ISO 15118-20 session test, EVSE side (TEST_PLAN HLC-01): starts the ChargeBridge
# daemon (status dashboard), and an EVerest instance running the FULL EVSE session stack -
# EvseManager + Evse15118D20 + McsDataLink + DC power train - from the EVerest config given as the
# first argument. No button panels: EvseManager owns the basic signalling.
#
# Prerequisites:
#  - a build with everything installed:  cmake --build <build-dir> --target install
#  - an MQTT broker on localhost:1883 (mosquitto)
#  - the MCS ChargeBridge (EVSE role) reachable (see the daemon config)
#  - the EV side: run-mcs-ev-session-netns.sh (netns - SDP/TCP must cross the wire)
#
# Usage: run-mcs-evse-session.sh [--control] <everest-config> [cb-config] [dist-prefix]
#   --control:      also start the mcs_evse_control panel in a pane next to the daemon (may be
#                   given anywhere on the command line).
#   everest-config: path to the EVerest config YAML, e.g. the testival session config. A bare
#                   file name that does not exist in the current directory is also looked up
#                   next to this script (e.g. config-CB-EVAL-MCS.yaml, config-CB-EVAL-CCS.yaml).
#   cb-config:      path to the ChargeBridge daemon config. Empty or omitted: $CB_CONFIG if set,
#                   else applications/pionix_chargebridge/config/config-CB-MCS-EVSE.yaml
#                   (use config-CB-EVAL.yaml for a CCS config with EvseSlac).
#   dist-prefix:    the EVerest install prefix (default: build/dist).

set -e
WITH_CONTROL=0
ARGS=()
for arg in "$@"; do
    case "$arg" in
        --control) WITH_CONTROL=1 ;;
        -*) echo "unknown option '$arg'" >&2; exit 2 ;;
        *) ARGS+=("$arg") ;;
    esac
done
set -- "${ARGS[@]}"
if [ $# -lt 1 ]; then
    echo "Usage: $0 [--control] <everest-config> [cb-config] [dist-prefix]" >&2
    exit 2
fi
EVEREST_CONFIG_ARG=$1
CB_CONFIG_ARG=${2:-}
PREFIX_ARG=${3:-}
CB_CONFIG_NAME=config-CB-MCS-EVSE.yaml

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
if [ -d "$SCRIPT_DIR/../../applications/pionix_chargebridge" ]; then
    REPO_DIR=$(cd "$SCRIPT_DIR/../.." && pwd)
    DEFAULT_PREFIX=$REPO_DIR/build/dist
    DEFAULT_CB_CONFIG=$REPO_DIR/applications/pionix_chargebridge/config/$CB_CONFIG_NAME
else
    DEFAULT_PREFIX=$(cd "$SCRIPT_DIR/../.." && pwd)
    DEFAULT_CB_CONFIG=""
fi
PREFIX=${PREFIX_ARG:-$DEFAULT_PREFIX}
# The bench configs live in config/, next to the other ChargeBridge configs; installed they land
# flat in <etc>/everest alongside this script. Look in both places.
find_config() {
    for candidate in "$SCRIPT_DIR/$1" "$SCRIPT_DIR/../$1"; do
        if [ -f "$candidate" ]; then
            echo "$candidate"
            return 0
        fi
    done
    echo "EVerest session config '$1' not found next to $SCRIPT_DIR or one level up" >&2
    return 1
}
# Absolute, because the panes run the command from the tmux server's working directory.
if [ -f "$EVEREST_CONFIG_ARG" ]; then
    EVEREST_CONFIG=$(realpath "$EVEREST_CONFIG_ARG")
else
    EVEREST_CONFIG=$(find_config "$EVEREST_CONFIG_ARG")
fi
CB_CONFIG=${CB_CONFIG_ARG:-${CB_CONFIG:-$DEFAULT_CB_CONFIG}}
if [ ! -f "$CB_CONFIG" ]; then
    echo "ChargeBridge daemon config not found ('$CB_CONFIG') - pass it as the second argument" >&2
    exit 1
fi
CB_CONFIG=$(realpath "$CB_CONFIG")
SESSION=mcs-evse-session

if [ ! -x "$PREFIX/bin/manager" ]; then
    echo "No EVerest manager under $PREFIX - build and install first" >&2
    exit 1
fi

ENV="LD_LIBRARY_PATH=$PREFIX/lib:\$LD_LIBRARY_PATH PATH=$PREFIX/bin:\$PATH"

# One sudo prompt: tmux server with ambient CAP_NET_ADMIN (TAP), CAP_NET_RAW and CAP_DAC_OVERRIDE
# (the serial bridges link their ptys as /dev/cb_uart, /dev/cb_rs485 - a symlink into root-owned
# /dev) - see run-mcs-evse-bringup.sh for the full rationale.
TMUX="tmux -L $SESSION"
$TMUX kill-server 2>/dev/null || true

# Zombie guard (see run-mcs-evse-bringup.sh).
sleep 0.5
if pgrep -af "pionix_chargebridge.*$(basename "$CB_CONFIG")" >/dev/null 2>&1; then
    echo "ERROR: a pionix_chargebridge with $(basename "$CB_CONFIG") is already running:" >&2
    pgrep -af "pionix_chargebridge.*$(basename "$CB_CONFIG")" >&2
    echo "kill it first (may need sudo), then re-run" >&2
    exit 1
fi
if [ "$(id -u)" = "0" ] || setpriv -d | grep 'Ambient capabilities:' | grep -q 'net_admin.*dac_override\|dac_override.*net_admin'; then
    $TMUX new-session -d -s $SESSION
else
    echo "one sudo prompt: starting the tmux server with ambient CAP_NET_ADMIN+CAP_NET_RAW+CAP_DAC_OVERRIDE"
    sudo setpriv --reuid="$(id -u)" --regid="$(id -g)" --init-groups \
        --inh-caps=+net_admin,+net_raw,+dac_override --ambient-caps=+net_admin,+net_raw,+dac_override -- \
        env HOME="$HOME" USER="$USER" LOGNAME="$USER" SHELL="${SHELL:-/bin/bash}" \
        TERM="${TERM:-xterm-256color}" PATH="$PATH" \
        tmux -L $SESSION new-session -d -s $SESSION
fi
$TMUX set -t $SESSION mouse on
$TMUX set -t $SESSION history-limit 50000

# Layout: daemon on top (with --control: daemon top left, mcs_evse_control top right), EVerest
# manager below across the full width (it produces by far the most output). Other useful views
# from your own shell:
#   ip monitor link dev cb_plc   carrier as McsDataLink sees it
#   ip neigh show dev cb_plc     EV liveness view
PANE_DAEMON=$($TMUX display-message -p -t $SESSION '#{pane_id}')
PANE_MANAGER=$($TMUX split-window -v -t "$PANE_DAEMON" -PF '#{pane_id}')

$TMUX send -t "$PANE_DAEMON" "env LD_LIBRARY_PATH=$PREFIX/lib $PREFIX/bin/pionix_chargebridge $CB_CONFIG" ENTER
$TMUX send -t "$PANE_MANAGER" "sleep 1 && $ENV $PREFIX/bin/manager --prefix $PREFIX --config $EVEREST_CONFIG" ENTER
# The panel needs evse_manager_consumer_API and external_energy_limits_consumer_API in the EVerest
# config (module ids evse_manager_api / evse_energy_limits_api); without them it shows no heartbeat.
# It reconnects by itself, so starting it before the manager is up is fine.
if [ "$WITH_CONTROL" = 1 ]; then
    PANE_CONTROL=$($TMUX split-window -h -t "$PANE_DAEMON" -PF '#{pane_id}')
    if [ -x "$PREFIX/bin/mcs_evse_control" ]; then
        $TMUX send -t "$PANE_CONTROL" "$ENV $PREFIX/bin/mcs_evse_control" ENTER
    else
        $TMUX send -t "$PANE_CONTROL" "echo 'mcs_evse_control not installed under $PREFIX/bin - build and install it'" ENTER
    fi
fi

$TMUX attach -t $SESSION
$TMUX kill-server 2>/dev/null || true
