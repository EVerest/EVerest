#!/usr/bin/env bash
# MCS ISO 15118-20 session test, EV side, inside the mcs-ev network namespace.
# Thin wrapper: the whole namespace/veth/NAT/broker harness lives in
# run-mcs-ev-bringup-netns.sh; this only swaps the inner script for the session stack
# (EvManager + Ev15118 + McsEvDataLink) instead of the button panels.
#
# Usage: run-mcs-ev-session-netns.sh [dist-prefix]
#        run-mcs-ev-session-netns.sh --teardown

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
EV_INNER_SCRIPT="$SCRIPT_DIR/run-mcs-ev-session.sh" exec "$SCRIPT_DIR/run-mcs-ev-bringup-netns.sh" "$@"
