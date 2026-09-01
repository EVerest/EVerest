#!/usr/bin/env bash
# Tight HLC-01d wake-cycle loop (TEST_PLAN HLC-01d): hunts intermittent failures by running
# short wake-to-second-session cycles back to back and STOPPING on the first new [ERRO] line
# in the EVSE manager pane, so the evidence is frozen on screen instead of scrolled away.
#
# One cycle = CC.5.2.4 wake -> SDP -> MCS session -> cable check -> 3 s charge loop -> clean
# stop -> park (sleep 36000, NO unplug: the next wake starts from Car Paused immediately).
# ~20 s per cycle. Requires: both session stacks up, the EV mated and PARKED (its sim waiting
# on 'sleep'); if the EV is idle/unplugged instead, the script primes it with one
# execute_charging_session first (plug-in + first session, also parked).
#
# Usage: run-mcs-wake-cycles.sh [cycles]        (default 30)

set -u
BROKER=${BROKER:-10.200.0.1}
BASE_TOPIC='everest_external/nodered/1/carsim/cmd'
CYCLES=${1:-30}
LOOP_S=${LOOP_S:-3} # charge-loop seconds per cycle

SESSION_SEQ="iso_wait_slac_matched;iso_start_v2g_session DC;iso_wait_pwr_ready;iso_dc_power_on;iso_wait_for_stop $LOOP_S;iso_wait_v2g_session_stopped;sleep 36000"
WAKE_SEQ="cp_c_pulse 4;$SESSION_SEQ"

evse_pane() { tmux -L mcs-evse-session capture-pane -t mcs-evse-session:0.2 -p -S - 2>/dev/null; }
ev_pane() { tmux -L mcs-ev-session capture-pane -t mcs-ev-session:0.2 -p -S - 2>/dev/null; }
err_count() { evse_pane | grep -c ' \[ERRO\] '; }
park_count() { ev_pane | grep -c 'Simulation waiting on: sleep'; }
dead_count() { ev_pane | grep -c 'Finished simulation'; }

publish() {
    mosquitto_pub -h "$BROKER" -t "$BASE_TOPIC/$1" -m "$2" || {
        echo "mosquitto_pub failed - is the broker at $BROKER reachable?" >&2
        exit 1
    }
}

# Waits for a new park (sleep) after the given baseline; fails on new ERRO, a canceled sim
# ("Finished simulation" - a parked queue never finishes) or 90 s timeout. After the park it
# settles long enough for the EVSE's own wind-down (D-LINK_TERMINATE + the 1.55 s CP retain
# X1 + parking into Car Paused): the EV parks ~2 s BEFORE the EVSE is wake-ready, and a wake
# pulse fired into that window collides with the draining V2G teardown (bench-found: the late
# session-end handler resets SLAC and cancels the fresh queue, wedging the bench).
wait_cycle_done() {
    local parks_before=$1 errs_before=$2 deads_before=$3 t
    for t in $(seq 1 90); do
        local errs_now
        errs_now=$(err_count)
        if [ "$errs_now" -gt "$errs_before" ]; then
            echo "  NEW ERRO in the EVSE log - stopping, evidence frozen:"
            evse_pane | grep ' \[ERRO\] ' | tail -3 | sed 's/^/  /'
            return 2
        fi
        if [ "$(dead_count)" -gt "$deads_before" ]; then
            echo "  EV simulation canceled mid-cycle - stopping, evidence frozen."
            return 4
        fi
        if [ "$(park_count)" -gt "$parks_before" ]; then
            sleep 5 # EVSE wind-down settle - see above
            return 0
        fi
        sleep 1
    done
    echo "  cycle did not park within 90 s - wedge? Stopping, evidence frozen."
    return 3
}

errs=$(err_count)
parks=$(park_count)
deads=$(dead_count)

# Prime: if the EV sim is not currently parked, run a first-session plug-in cycle.
if ! ev_pane | tail -30 | grep -q 'Simulation waiting on: sleep'; then
    echo "$(date +%T) priming: EV not parked - running a first session (execute, resets the vehicle)"
    publish execute_charging_session "$SESSION_SEQ"
    wait_cycle_done "$parks" "$errs" "$deads" || exit $?
    parks=$(park_count)
    errs=$(err_count)
    deads=$(dead_count)
fi

for i in $(seq 1 "$CYCLES"); do
    echo "$(date +%T) wake cycle $i/$CYCLES"
    publish modify_charging_session "$WAKE_SEQ"
    wait_cycle_done "$parks" "$errs" "$deads" || exit $?
    parks=$(park_count)
    errs=$(err_count)
    deads=$(dead_count)
done
echo "$(date +%T) all $CYCLES cycles clean"
