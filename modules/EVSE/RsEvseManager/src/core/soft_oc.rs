// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

//! Soft overcurrent detection: a vehicle drawing more than it was offered.
//!
//! A port of `Charger::check_soft_over_current` (`Charger.cpp:1942-1976`).
//!
//! This is **not** the board's overcurrent limit. `Effect::SetOvercurrentLimit`
//! carries the current limit to `bsp.ac_set_overcurrent_limit_a`, where hardware
//! enforces it; the two share no state and no code path. What this module reads
//! is the current the port *signalled*, which is a different figure from the
//! limit, and its only output is an EVerest error.
//!
//! Detection is pure: no clock, no I/O. `now` arrives as a parameter and the
//! verdict comes back as an action the core discharges.

use std::time::{Duration, Instant};

use super::effect::TimerId;
use super::path::iec::AcState;

/// The wake-up that lets a crossing trip while the meter is silent.
///
/// The C++ needs no timer: `check_soft_over_current` runs from the 100 ms state
/// machine tick (`Charger.hpp:472` `MAINLOOP_UPDATE_RATE`) against the last
/// stored sample, so a meter that stops reporting mid crossing still trips the
/// timeout. Here the evaluation is driven by the meter record, so the deadline
/// needs an event of its own.
pub const TIMER_SOFT_OVER_CURRENT: TimerId = TimerId(401);

/// The three AC phase currents of one meter record.
///
/// Named fields rather than an array: every reader wants a particular phase and
/// the description names all three in order, which is exactly the shape a
/// positional literal gets wrong.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct PhaseCurrents {
    pub l1_a: f64,
    pub l2_a: f64,
    pub l3_a: f64,
}

impl PhaseCurrents {
    /// The largest magnitude of the three.
    ///
    /// `Charger.cpp:1948-1950` compares `std::fabs` of each phase against the
    /// limit and treats any one crossing as a crossing, which is the same test
    /// as the largest magnitude crossing. The magnitude is what makes a
    /// discharge of sixteen amperes read as a draw of sixteen.
    fn peak_magnitude_a(&self) -> f64 {
        self.l1_a.abs().max(self.l2_a.abs()).max(self.l3_a.abs())
    }
}

/// The three configuration keys, resolved once.
///
/// `Charger.cpp:1567-1568` and `:1564` assign these at `Charger::setup` and
/// nothing rewrites them, so they are values here rather than a settings
/// reference.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct SoftOverCurrentConfig {
    /// `soft_over_current_tolerance_percent`, default 10.0.
    pub tolerance_percent: f64,
    /// `soft_over_current_measurement_noise_A`, default 0.5. An offset rather
    /// than a factor, added before the tolerance is applied.
    pub measurement_noise_a: f64,
    /// `soft_over_current_timeout_ms`, default 7000, manifest minimum 6000.
    pub timeout: Duration,
}

/// What one evaluation concluded, for the core to discharge.
///
/// The error itself is not raised here: a raise has to reach the fault set as
/// well as the wire, and `Faults` lives above this module. The description
/// travels with the action because this is what knows the four numbers the C++
/// formats into it.
#[derive(Clone, Debug, PartialEq)]
pub enum SoftOverCurrentAction {
    /// The first sample over the limit. `Charger.cpp:1951-1961`: latch, stamp
    /// the crossing and log that the timer started.
    CrossingStarted { message: String },
    /// A sample back under the limit while a crossing stood.
    /// `Charger.cpp:1962`, which drops the latch and logs nothing.
    CrossingEnded,
    /// The crossing outlasted `timeout`. `Charger.cpp:1967-1975`: log, then
    /// raise `MREC4OverCurrentFailure`.
    Triggered { message: String },
}

/// Whether `check_soft_over_current` runs in this state.
///
/// Its two call sites are `Charger.cpp:852`, the `EvseState::Charging` case in
/// the `else` of `if (charge_mode == ChargeMode::DC)`, and `Charger.cpp:888`,
/// the `EvseState::ChargingPausedEV` case under `if (charge_mode ==
/// ChargeMode::AC)`. There is no third.
///
/// Exhaustive rather than a `matches!`, so a state added later is a decision
/// somebody has to make rather than a silent no.
pub fn state_runs_check(state: AcState) -> bool {
    match state {
        AcState::Charging | AcState::ChargingPausedEv => true,

        // No offer stands in any of these, so no draw is expected and the C++
        // measures none. `ChargingPausedEvse` is the notable one: the offer is
        // withdrawn there and the C++ still does not check, which is why a
        // crossing latched before the pause has to survive it. See
        // `Detection::evaluate`.
        AcState::Startup
        | AcState::Idle
        | AcState::WaitingForAuthentication
        | AcState::PrepareCharging
        | AcState::ChargingPausedEvse
        | AcState::SwitchPhases
        | AcState::StoppingCharging
        | AcState::Reinit
        | AcState::Finished
        | AcState::Disabled => false,
    }
}

/// The detector.
///
/// Built only for a charge mode that has soft overcurrent detection, which is
/// AC alone; see `Core::soft_oc`.
#[derive(Clone, Debug, PartialEq)]
pub struct Detection {
    config: SoftOverCurrentConfig,
    /// `shared_context.current_drawn_by_vehicle`, value initialized to zero
    /// (`Charger.cpp:57-59`).
    ///
    /// Retained across records the same way: `EvseManager.cpp:1157` only calls
    /// `set_current_drawn_by_vehicle` when all three phases are present, so a
    /// record missing one leaves all three previous values standing.
    drawn: PhaseCurrents,
    /// When the standing crossing began, or `None` for no standing crossing.
    ///
    /// The C++ holds this as two fields, `internal_context.over_current` and
    /// `last_over_current_event` (`Charger.hpp:417-418`), and deliberately does
    /// not reset the timestamp when the flag clears. One `Option` is equivalent
    /// and stronger: the timestamp is only ever written on the edge into a
    /// crossing (`Charger.cpp:1952-1954`) and only ever read under the flag
    /// (`:1967`), so there is no state in which the pair disagrees and no way
    /// here to read a stamp that belongs to a crossing already over.
    crossing_since: Option<Instant>,
}

impl Detection {
    pub fn new(config: SoftOverCurrentConfig) -> Self {
        Self {
            config,
            drawn: PhaseCurrents::default(),
            crossing_since: None,
        }
    }

    /// Store one meter record. `Charger::set_current_drawn_by_vehicle`
    /// (`Charger.cpp:1934-1940`).
    ///
    /// Separate from the evaluation, as it is in the C++: the meter callback
    /// stores from whatever state the port is in, and only the two charging
    /// states read the store back.
    pub fn note_phase_currents(&mut self, drawn: PhaseCurrents) {
        self.drawn = drawn;
    }

    /// One pass of the check against the stored record.
    ///
    /// `signalled_a` is `Charger::get_max_current_signalled_to_ev_internal`
    /// (`Charger.cpp:1925-1932`), which the power path answers because only it
    /// knows what is on the pilot.
    ///
    /// Called on a meter record and on the deadline, which is the C++ tick
    /// running the same function against the same store.
    pub fn evaluate(&mut self, signalled_a: f64, now: Instant) -> Option<SoftOverCurrentAction> {
        let limit_a = self.limit_a(signalled_a);

        if self.drawn.peak_magnitude_a() <= limit_a {
            // `Charger.cpp:1962`. The stamp goes with the latch here; see the
            // field's own comment for why that is the same behavior.
            return self
                .crossing_since
                .take()
                .map(|_| SoftOverCurrentAction::CrossingEnded);
        }

        let Some(since) = self.crossing_since else {
            self.crossing_since = Some(now);
            return Some(SoftOverCurrentAction::CrossingStarted {
                message: self.starting_message(limit_a),
            });
        };

        // `>=`, as `Charger.cpp:1968` has it, so a timeout of zero trips on the
        // pass that latched.
        if now.duration_since(since) >= self.config.timeout {
            return Some(SoftOverCurrentAction::Triggered {
                message: self.triggered_message(limit_a),
            });
        }

        None
    }

    /// Forget the session's crossing. The C++ clears the error itself on the
    /// unplug (`Charger::clear_errors_on_unplug`, `Charger.cpp:2285-2296`);
    /// what has to be dropped here is the latch, so the next vehicle is not
    /// measured against the previous one's crossing.
    ///
    /// The stored record is left alone, because the C++ leaves it alone too: no
    /// route zeroes `current_drawn_by_vehicle` after construction. A stale
    /// reading decides nothing on its own, since no state that reads it is
    /// reachable before the meter has reported again under a standing offer.
    pub fn end_session(&mut self) {
        self.crossing_since = None;
    }

    /// How long a crossing may stand before it trips, for the wake-up the core
    /// arms. Read off the config rather than passed in beside it, so the
    /// deadline and the comparison cannot be given different values.
    pub fn timeout(&self) -> Duration {
        self.config.timeout
    }

    /// `Charger.cpp:1945-1946`. The noise offset is added to the signalled
    /// current before the tolerance is applied to the sum, not after.
    fn limit_a(&self, signalled_a: f64) -> f64 {
        (signalled_a + self.config.measurement_noise_a)
            * (1.0 + self.config.tolerance_percent / 100.0)
    }

    /// `Charger.cpp:1955-1960`.
    fn starting_message(&self, limit_a: f64) -> String {
        format!(
            "Soft overcurrent event (L1:{}, L2:{}, L3:{}, limit {}), starting timer.",
            self.drawn.l1_a, self.drawn.l2_a, self.drawn.l3_a, limit_a
        )
    }

    /// `Charger.cpp:1969-1972`. The same string is logged and carried as the
    /// error's description, as the C++ carries one `errstr` into both.
    fn triggered_message(&self, limit_a: f64) -> String {
        format!(
            "Soft overcurrent event (L1:{}, L2:{}, L3:{}, limit {}) triggered",
            self.drawn.l1_a, self.drawn.l2_a, self.drawn.l3_a, limit_a
        )
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn config() -> SoftOverCurrentConfig {
        SoftOverCurrentConfig {
            tolerance_percent: 10.0,
            measurement_noise_a: 0.5,
            timeout: Duration::from_millis(7000),
        }
    }

    fn now() -> Instant {
        Instant::now()
    }

    fn phases(l1_a: f64, l2_a: f64, l3_a: f64) -> PhaseCurrents {
        PhaseCurrents { l1_a, l2_a, l3_a }
    }

    /// `(16 + 0.5) * 1.1`, the C++ formula against the manifest defaults.
    #[test]
    fn the_limit_adds_the_noise_offset_before_applying_the_tolerance() {
        let detection = Detection::new(config());
        assert!((detection.limit_a(16.0) - 18.15).abs() < 1e-9);
    }

    /// A tolerance applied to the signalled current alone and the offset added
    /// afterwards would give `16 * 1.1 + 0.5`, which is a different number.
    #[test]
    fn the_limit_is_not_the_tolerance_applied_before_the_offset() {
        let detection = Detection::new(config());
        assert!((detection.limit_a(16.0) - (16.0 * 1.1 + 0.5)).abs() > 1e-3);
    }

    #[test]
    fn a_draw_inside_the_tolerance_is_no_crossing() {
        let mut detection = Detection::new(config());
        detection.note_phase_currents(phases(18.0, 0.0, 0.0));
        assert_eq!(detection.evaluate(16.0, now()), None);
        // Nothing latched, so a second sub limit sample ends nothing either.
        assert_eq!(detection.evaluate(16.0, now()), None);
    }

    #[test]
    fn a_draw_exactly_at_the_limit_is_no_crossing() {
        let mut detection = Detection::new(config());
        detection.note_phase_currents(phases(18.15, 0.0, 0.0));
        assert_eq!(detection.evaluate(16.0, now()), None);
    }

    /// Any one phase decides, which is the `or` of three comparisons.
    #[test]
    fn each_phase_on_its_own_starts_a_crossing() {
        for drawn in [
            phases(20.0, 0.0, 0.0),
            phases(0.0, 20.0, 0.0),
            phases(0.0, 0.0, 20.0),
        ] {
            let mut detection = Detection::new(config());
            detection.note_phase_currents(drawn);
            let action = detection.evaluate(16.0, now());
            assert!(
                matches!(action, Some(SoftOverCurrentAction::CrossingStarted { .. })),
                "{drawn:?} gave {action:?}"
            );
        }
    }

    /// `std::fabs` on each phase, so a discharge reads as a draw.
    #[test]
    fn a_discharge_past_the_limit_starts_a_crossing() {
        let mut detection = Detection::new(config());
        detection.note_phase_currents(phases(-20.0, 0.0, 0.0));
        let action = detection.evaluate(16.0, now());
        assert!(matches!(
            action,
            Some(SoftOverCurrentAction::CrossingStarted { .. })
        ));
    }

    #[test]
    fn the_crossing_trips_once_the_timeout_has_passed() {
        let mut detection = Detection::new(config());
        let start = now();
        detection.note_phase_currents(phases(20.0, 0.0, 0.0));

        let started = detection.evaluate(16.0, start);
        assert!(matches!(
            started,
            Some(SoftOverCurrentAction::CrossingStarted { .. })
        ));

        // Still inside the window.
        assert_eq!(
            detection.evaluate(16.0, start + Duration::from_millis(6999)),
            None
        );

        let action = detection.evaluate(16.0, start + Duration::from_millis(7000));
        let message = match action {
            Some(SoftOverCurrentAction::Triggered { message }) => message,
            other => panic!("expected a trigger, got {other:?}"),
        };
        assert!(message.contains("triggered"), "{message}");
        assert!(message.contains("L1:20"), "{message}");
    }

    /// `Charger.cpp:1952` only stamps on the edge into a crossing, so a dip
    /// under the limit and a fresh crossing restart the whole window.
    #[test]
    fn a_dip_under_the_limit_restarts_the_window() {
        let mut detection = Detection::new(config());
        let start = now();
        detection.note_phase_currents(phases(20.0, 0.0, 0.0));
        detection.evaluate(16.0, start);

        detection.note_phase_currents(phases(10.0, 0.0, 0.0));
        assert_eq!(
            detection.evaluate(16.0, start + Duration::from_millis(6000)),
            Some(SoftOverCurrentAction::CrossingEnded)
        );

        detection.note_phase_currents(phases(20.0, 0.0, 0.0));
        assert!(matches!(
            detection.evaluate(16.0, start + Duration::from_millis(6100)),
            Some(SoftOverCurrentAction::CrossingStarted { .. })
        ));
        // 7 s after the first crossing but only 0.1 s after this one.
        assert_eq!(
            detection.evaluate(16.0, start + Duration::from_millis(7000)),
            None
        );
        assert!(matches!(
            detection.evaluate(16.0, start + Duration::from_millis(13100)),
            Some(SoftOverCurrentAction::Triggered { .. })
        ));
    }

    /// A sub limit sample with nothing standing has nothing to report, so the
    /// core is not asked to cancel a deadline it never armed.
    #[test]
    fn a_sub_limit_sample_with_no_crossing_reports_nothing() {
        let mut detection = Detection::new(config());
        detection.note_phase_currents(phases(1.0, 1.0, 1.0));
        assert_eq!(detection.evaluate(16.0, now()), None);
    }

    /// The signalled current is the figure that moves, and a withdrawn offer is
    /// zero. `(0 + 0.5) * 1.1` catches a draw the nominal limit would allow.
    #[test]
    fn a_withdrawn_offer_measures_against_the_noise_floor_alone() {
        let mut detection = Detection::new(config());
        detection.note_phase_currents(phases(6.0, 0.0, 0.0));
        assert_eq!(detection.evaluate(16.0, now()), None);
        assert!(matches!(
            detection.evaluate(0.0, now()),
            Some(SoftOverCurrentAction::CrossingStarted { .. })
        ));
    }

    /// A record missing a phase never reaches the store, so the previous three
    /// values stand and the check keeps deciding on them.
    #[test]
    fn the_stored_record_survives_until_the_next_complete_one() {
        let mut detection = Detection::new(config());
        let start = now();
        detection.note_phase_currents(phases(20.0, 0.0, 0.0));
        detection.evaluate(16.0, start);
        // No further `note_phase_currents`: the deadline decides on the record
        // that started the crossing, which is what lets a silent meter trip.
        assert!(matches!(
            detection.evaluate(16.0, start + Duration::from_millis(7000)),
            Some(SoftOverCurrentAction::Triggered { .. })
        ));
    }

    #[test]
    fn a_session_end_drops_the_standing_crossing() {
        let mut detection = Detection::new(config());
        let start = now();
        detection.note_phase_currents(phases(20.0, 0.0, 0.0));
        assert!(matches!(
            detection.evaluate(16.0, start),
            Some(SoftOverCurrentAction::CrossingStarted { .. })
        ));

        detection.end_session();

        // The next vehicle gets its own window rather than inheriting one that
        // has already expired: a fresh crossing rather than a trigger, even
        // thirty seconds past the original deadline.
        assert!(matches!(
            detection.evaluate(16.0, start + Duration::from_millis(30000)),
            Some(SoftOverCurrentAction::CrossingStarted { .. })
        ));
    }

    #[test]
    fn only_the_two_charging_states_run_the_check() {
        for state in [AcState::Charging, AcState::ChargingPausedEv] {
            assert!(state_runs_check(state), "{state:?}");
        }
        for state in [
            AcState::Startup,
            AcState::Idle,
            AcState::WaitingForAuthentication,
            AcState::PrepareCharging,
            AcState::ChargingPausedEvse,
            AcState::SwitchPhases,
            AcState::StoppingCharging,
            AcState::Finished,
            AcState::Disabled,
        ] {
            assert!(!state_runs_check(state), "{state:?}");
        }
    }

    /// A zero tolerance and a zero noise offset leave the signalled current
    /// itself as the limit, which is the configuration an operator sets to get
    /// no allowance at all.
    #[test]
    fn a_zero_allowance_measures_against_the_signalled_current_itself() {
        let mut detection = Detection::new(SoftOverCurrentConfig {
            tolerance_percent: 0.0,
            measurement_noise_a: 0.0,
            timeout: Duration::from_millis(7000),
        });
        detection.note_phase_currents(phases(16.0, 0.0, 0.0));
        assert_eq!(detection.evaluate(16.0, now()), None);
        detection.note_phase_currents(phases(16.001, 0.0, 0.0));
        assert!(matches!(
            detection.evaluate(16.0, now()),
            Some(SoftOverCurrentAction::CrossingStarted { .. })
        ));
    }
}
