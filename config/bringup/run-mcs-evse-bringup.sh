#!/usr/bin/env bash
# MCS bring-up, EVSE side: starts the ChargeBridge daemon (with its status dashboard),
# an EVerest instance (evse_board_support_API + McsDataLink) and the two bring-up
# panels (BUEvseBoardSupport, BUSlac) in one tmux session.
#
# Prerequisites:
#  - a build with the BringUp modules installed:  cmake --build <build-dir> --target install
#  - an MQTT broker on localhost:1883 (mosquitto)
#  - the MCS ChargeBridge (EVSE role) reachable - config-CB-MCS-EVSE.yaml discovers it
#    via mDNS (ANY_EVSE) by default
#
# Usage: run-mcs-evse-bringup.sh [dist-prefix]
#   dist-prefix   EVerest install prefix (default: <repo>/build/dist)
# Environment:
#   CB_CONFIG     ChargeBridge daemon config
#                 (default: applications/pionix_chargebridge/config/config-CB-MCS-EVSE.yaml)
#
# The EV side (run-mcs-ev-bringup.sh) can run at the same time on the same host and
# broker; the two EVerest instances use different MQTT prefixes. Note that with both
# taps on one host, cross-link IP traffic (ping, EV-MAC via neighbour discovery) is
# short-circuited by local routing - see the note in the daemon config.

set -e
# CCS boards: BENCH=ccs (or first argument ccs) picks the CCS daemon config; the bring-up
# EVerest config (BSP panels) is protocol-neutral.
BENCH=${BENCH:-mcs}
case "${1:-}" in mcs | ccs) BENCH=$1; shift ;; esac
case "$BENCH" in mcs) CB_CONFIG_NAME=config-CB-MCS-EVSE.yaml ;; ccs) CB_CONFIG_NAME=config-CB-EVAL.yaml ;; *) echo "BENCH must be mcs or ccs" >&2; exit 2 ;; esac

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
if [ -d "$SCRIPT_DIR/../../applications/pionix_chargebridge" ]; then
    # running from the source tree (config/bringup/)
    REPO_DIR=$(cd "$SCRIPT_DIR/../.." && pwd)
    DEFAULT_PREFIX=$REPO_DIR/build/dist
    DEFAULT_CB_CONFIG=$REPO_DIR/applications/pionix_chargebridge/config/${CB_CONFIG_NAME:-config-CB-MCS-EVSE.yaml}
else
    # running from an installed copy (<prefix>/etc/everest/): the prefix is two levels
    # up, but no daemon config is installed there - CB_CONFIG must be set explicitly
    DEFAULT_PREFIX=$(cd "$SCRIPT_DIR/../.." && pwd)
    DEFAULT_CB_CONFIG=""
fi
PREFIX=${1:-$DEFAULT_PREFIX}
EVEREST_CONFIG=$SCRIPT_DIR/config-bringup-mcs-evse.yaml
CB_CONFIG=${CB_CONFIG:-$DEFAULT_CB_CONFIG}
if [ ! -f "$CB_CONFIG" ]; then
    echo "ChargeBridge daemon config not found ('$CB_CONFIG') - set CB_CONFIG=/path/to/config.yaml" >&2
    exit 1
fi
MODULES_DIR=$PREFIX/libexec/everest/modules
SESSION=mcs-evse-bringup

if [ ! -x "$PREFIX/bin/manager" ]; then
    echo "No EVerest manager under $PREFIX - build and install first" >&2
    exit 1
fi
if [ ! -d "$MODULES_DIR/BUSlac" ]; then
    echo "BringUp modules not installed under $PREFIX (missing $MODULES_DIR/BUSlac)" >&2
    exit 1
fi

ENV="LD_LIBRARY_PATH=$PREFIX/lib:\$LD_LIBRARY_PATH PATH=$PREFIX/bin:\$PATH"

# --- privileges: ONE sudo prompt, no sudo in any pane ------------------------------------------
# The daemon needs CAP_NET_ADMIN (TAP create/carrier/link) and, with the serial bridges enabled,
# CAP_DAC_OVERRIDE (their ptys are linked as /dev/cb_uart and /dev/cb_rs485 - a symlink into the
# root-owned /dev); the slac panel needs CAP_NET_RAW (the link-echo raw socket). Instead of one sudo prompt per pane (credential caching is per-tty), the
# tmux SERVER is started once via `sudo setpriv` with those capabilities AMBIENT: they inherit
# across fork/exec into every pane while everything keeps running as the invoking user. Ambient
# caps do not trigger secure-execution mode, so LD_LIBRARY_PATH keeps working - the reason plain
# setcap on the binaries is no alternative. A dedicated tmux socket (-L) guarantees the panes are
# children of THIS capability-armed server, not of a pre-existing per-user one.
# Already root (e.g. under `ip netns exec`, see run-mcs-ev-bringup-netns.sh) or already
# capability-armed (re-run from inside such a session): no prompt at all.
TMUX="tmux -L $SESSION"
$TMUX kill-server 2>/dev/null || true

# Zombie guard. A daemon instance surviving from an earlier session is the bench's most
# repeatable trap: it silently eats BSP commands, holds the tap with stale carrier policy, or -
# in a netns setup - keeps a root-namespace twin of the tap whose local address answers pings
# that never touch the wire. Refuse to stack a second instance on the same config; old-era
# zombies may be root-owned, so this reports instead of killing.
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

# Panes are addressed by their immutable pane ids (%N) - positional indices are
# renumbered on every split, which is exactly the trap this avoids.
PANE_DAEMON=$($TMUX display-message -p -t $SESSION '#{pane_id}')
PANE_MANAGER=$($TMUX split-window -h -t "$PANE_DAEMON" -PF '#{pane_id}')
PANE_BSP=$($TMUX split-window -v -t "$PANE_DAEMON" -PF '#{pane_id}')
PANE_SLAC=$($TMUX split-window -v -t "$PANE_MANAGER" -PF '#{pane_id}')

# daemon pane (status dashboard: link status, CE/ID states, latched role). CAP_NET_ADMIN for the
# TAP device comes ambient from the tmux server - no sudo here.
$TMUX send -t "$PANE_DAEMON" "env LD_LIBRARY_PATH=$PREFIX/lib $PREFIX/bin/pionix_chargebridge $CB_CONFIG" ENTER

# EVerest manager
$TMUX send -t "$PANE_MANAGER" "sleep 1 && $ENV $PREFIX/bin/manager --prefix $PREFIX --config $EVEREST_CONFIG" ENTER

# the two standalone bring-up panels. The slac panel waits for the daemon's tap to exist (daemon
# up, config accepted) instead of guessing with a fixed delay - its link-echo needs the device,
# and CAP_NET_RAW comes ambient from the tmux server.
$TMUX send -t "$PANE_BSP" "sleep 4 && $ENV $MODULES_DIR/BUEvseBoardSupport/BUEvseBoardSupport --module bu_bsp" ENTER
$TMUX send -t "$PANE_SLAC" "echo 'waiting for the daemon (tap cb_plc)...' && until ip link show cb_plc >/dev/null 2>&1; do sleep 0.5; done && sleep 2 && $ENV $MODULES_DIR/BUSlac/BUSlac --module bu_slac" ENTER

$TMUX select-layout -t $SESSION tiled

# Attach returns on detach (Ctrl+B d) or when the session dies on its own; either way
# the bench session should not linger in the background - tear everything down.
$TMUX attach -t $SESSION
$TMUX kill-server 2>/dev/null || true
