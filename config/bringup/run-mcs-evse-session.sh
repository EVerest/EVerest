#!/usr/bin/env bash
# MCS ISO 15118-20 session test, EVSE side (TEST_PLAN HLC-01): starts the ChargeBridge
# daemon (status dashboard), and an EVerest instance running the FULL EVSE session stack -
# EvseManager + Evse15118D20 + McsDataLink + simulated DC power train - from
# config-CB-EVAL-MCS.yaml. No button panels: EvseManager owns the basic signalling.
#
# Prerequisites:
#  - a build with everything installed:  cmake --build <build-dir> --target install
#  - an MQTT broker on localhost:1883 (mosquitto)
#  - the MCS ChargeBridge (EVSE role) reachable (see the daemon config)
#  - the EV side: run-mcs-ev-session-netns.sh (netns - SDP/TCP must cross the wire)
#
# Usage: run-mcs-evse-session.sh [mcs|ccs] [dist-prefix]
#   mcs (default): config-CB-EVAL-MCS.yaml + daemon config-CB-MCS-EVSE.yaml (McsDataLink)
#   ccs:           config-CB-EVAL-CCS.yaml + daemon config-CB-EVAL.yaml (EvseSlac); the CCS twin
#   BENCH=mcs|ccs in the environment works too; the positional argument wins.
# Environment:
#   CB_CONFIG   ChargeBridge daemon config
#               (default: applications/pionix_chargebridge/config/config-CB-MCS-EVSE.yaml)

set -e
BENCH=${BENCH:-mcs}
case "${1:-}" in mcs | ccs) BENCH=$1; shift ;; esac
case "$BENCH" in
    mcs) EVEREST_CONFIG_NAME=config-CB-EVAL-MCS.yaml; CB_CONFIG_NAME=config-CB-MCS-EVSE.yaml ;;
    ccs) EVEREST_CONFIG_NAME=config-CB-EVAL-CCS.yaml; CB_CONFIG_NAME=config-CB-EVAL.yaml ;;
    *) echo "BENCH must be mcs or ccs, got '$BENCH'" >&2; exit 2 ;;
esac

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
if [ -d "$SCRIPT_DIR/../../applications/pionix_chargebridge" ]; then
    REPO_DIR=$(cd "$SCRIPT_DIR/../.." && pwd)
    DEFAULT_PREFIX=$REPO_DIR/build/dist
    DEFAULT_CB_CONFIG=$REPO_DIR/applications/pionix_chargebridge/config/$CB_CONFIG_NAME
else
    DEFAULT_PREFIX=$(cd "$SCRIPT_DIR/../.." && pwd)
    DEFAULT_CB_CONFIG=""
fi
PREFIX=${1:-$DEFAULT_PREFIX}
# The session config lives in config/ now, next to the other ChargeBridge configs; installed it
# lands flat in <etc>/everest alongside this script. Look in both places.
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
EVEREST_CONFIG=$(find_config $EVEREST_CONFIG_NAME)
CB_CONFIG=${CB_CONFIG:-$DEFAULT_CB_CONFIG}
if [ ! -f "$CB_CONFIG" ]; then
    echo "ChargeBridge daemon config not found ('$CB_CONFIG') - set CB_CONFIG=/path/to/config.yaml" >&2
    exit 1
fi
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

# Layout: daemon on top, EVerest manager below it (the manager produces by far the most
# output). Two panes only - drive the session from your own shell:
#   ip monitor link dev cb_plc   carrier as McsDataLink sees it
#   ip neigh show dev cb_plc     EV liveness view
#   tail -f /tmp/everest-logs/*  EvseManager session log
PANE_DAEMON=$($TMUX display-message -p -t $SESSION '#{pane_id}')
PANE_MANAGER=$($TMUX split-window -v -t "$PANE_DAEMON" -PF '#{pane_id}')

$TMUX send -t "$PANE_DAEMON" "env LD_LIBRARY_PATH=$PREFIX/lib $PREFIX/bin/pionix_chargebridge $CB_CONFIG" ENTER
$TMUX send -t "$PANE_MANAGER" "sleep 1 && $ENV $PREFIX/bin/manager --prefix $PREFIX --config $EVEREST_CONFIG" ENTER

$TMUX attach -t $SESSION
$TMUX kill-server 2>/dev/null || true
