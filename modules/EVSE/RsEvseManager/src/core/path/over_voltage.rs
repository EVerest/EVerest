// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

//! The software over voltage watchdog, ported from
//! `modules/EVSE/EvseManager/over_voltage/OverVoltageMonitor.cpp`.
//!
//! It shadows the hardware monitor rather than replacing it: `EvseManager`
//! feeds the same `voltage_measurement_V` stream to both, arms both from the
//! same pair of thresholds, and starts and stops both with the current demand.
//! The hardware monitor is another module and may fail or be absent; this one
//! runs here.
//!
//! The C++ keeps its deadline on a background thread with a condition
//! variable. Here the deadline is a timer the path arms, so the watchdog stays
//! a pure state machine and the only clock is the one every other stage uses.

use std::time::Duration;

/// Which limit was crossed. The two are raised under the same error type and
/// differ by severity, as `raise_over_voltage_error` takes it.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum FaultKind {
    /// Above the error limit for the configured duration.
    Error,
    /// Above the emergency limit, immediately.
    Emergency,
}

/// What the caller must do with the timer and the error, so the watchdog owns
/// the decision and the path owns the effects.
#[derive(Clone, Debug, PartialEq)]
pub enum Action {
    Nothing,
    /// Start the error deadline. Only ever returned when none is running.
    Arm,
    /// Stop a running deadline.
    Cancel,
    /// Latched. Raise the error and stop evaluating until a restart.
    Fault {
        kind: FaultKind,
        description: String,
    },
}

/// `OverVoltageMonitor`. Evaluates nothing until it has been given limits,
/// which is the C++ `limits_valid_` gate.
pub struct Watchdog {
    duration: Duration,
    limits: Option<(f64, f64)>,
    running: bool,
    latched: bool,
    /// The highest voltage seen since the deadline was armed, or `None` when no
    /// deadline is running. The C++ holds the pair as `timer_armed_` and
    /// `timer_voltage_snapshot_`; one option cannot report a snapshot for a
    /// deadline that is not running.
    armed_peak_v: Option<f64>,
}

impl Watchdog {
    pub fn new(duration: Duration) -> Self {
        Self {
            duration,
            limits: None,
            running: false,
            latched: false,
            armed_peak_v: None,
        }
    }

    /// `set_limits`, which is also what makes the watchdog able to evaluate at
    /// all. `EvseManager` calls it from the vehicle's maximum limits, beside
    /// the hardware monitor's `set_limits`.
    pub fn set_limits(&mut self, emergency_v: f64, error_v: f64) {
        self.limits = Some((emergency_v, error_v));
    }

    /// `start_monitor`, preceded by `reset` at the one call site.
    pub fn start(&mut self) -> Action {
        self.latched = false;
        self.running = true;
        self.disarm()
    }

    /// `stop_monitor`. A latched fault survives it; only a start clears one.
    pub fn stop(&mut self) -> Action {
        self.running = false;
        self.disarm()
    }

    fn disarm(&mut self) -> Action {
        match self.armed_peak_v.take() {
            Some(_) => Action::Cancel,
            None => Action::Nothing,
        }
    }

    /// `update_voltage`.
    pub fn update_voltage(&mut self, voltage_v: f64) -> Action {
        let Some((emergency_v, error_v)) = self.limits else {
            return Action::Nothing;
        };
        if !self.running || self.latched {
            return Action::Nothing;
        }

        if voltage_v >= emergency_v {
            self.latch();
            return Action::Fault {
                kind: FaultKind::Emergency,
                description: format!(
                    "Voltage {voltage_v:.2} V exceeded emergency limit {emergency_v:.2} V."
                ),
            };
        }

        if voltage_v < error_v {
            return self.disarm();
        }

        // Above the error limit. A deadline already running keeps its own
        // expiry and only raises its snapshot, so a voltage that climbs during
        // the window is reported at its worst rather than at its last.
        if self.duration.is_zero() {
            self.latch();
            return self.error_fault(voltage_v);
        }
        match self.armed_peak_v {
            Some(peak_v) => {
                self.armed_peak_v = Some(peak_v.max(voltage_v));
                Action::Nothing
            }
            None => {
                self.armed_peak_v = Some(voltage_v);
                Action::Arm
            }
        }
    }

    /// The error deadline expired.
    pub fn on_deadline(&mut self) -> Action {
        let Some(peak_v) = self.armed_peak_v.take() else {
            return Action::Nothing;
        };
        if !self.running || self.latched {
            return Action::Nothing;
        }
        self.latch();
        self.error_fault(peak_v)
    }

    fn error_fault(&self, voltage_v: f64) -> Action {
        let error_v = self.limits.map(|(_, error_v)| error_v).unwrap_or_default();
        Action::Fault {
            kind: FaultKind::Error,
            description: format!(
                "Voltage {voltage_v:.2} V exceeded limit {error_v:.2} V for at least {} ms.",
                self.duration.as_millis()
            ),
        }
    }

    fn latch(&mut self) {
        self.latched = true;
        self.running = false;
        self.armed_peak_v = None;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The C++ fixture's pair (`OverVoltageMonitorTest`).
    fn watchdog(duration: Duration) -> Watchdog {
        let mut w = Watchdog::new(duration);
        w.set_limits(450.0, 420.0);
        w.start();
        w
    }

    fn fault_kind(action: &Action) -> Option<FaultKind> {
        match action {
            Action::Fault { kind, .. } => Some(*kind),
            _ => None,
        }
    }

    /// Nothing is evaluated before `set_limits`, which is the C++
    /// `limits_valid_` gate and the reason the watchdog is inert on a port
    /// whose vehicle never reported a maximum voltage.
    #[test]
    fn without_limits_nothing_is_evaluated() {
        let mut w = Watchdog::new(Duration::from_millis(100));
        w.start();
        assert_eq!(w.update_voltage(10_000.0), Action::Nothing);
    }

    #[test]
    fn no_fault_below_limits() {
        let mut w = watchdog(Duration::from_millis(100));
        assert_eq!(w.update_voltage(400.0), Action::Nothing);
    }

    #[test]
    fn emergency_fault_triggers_immediately() {
        let mut w = watchdog(Duration::from_millis(100));
        assert_eq!(
            fault_kind(&w.update_voltage(460.0)),
            Some(FaultKind::Emergency)
        );
    }

    #[test]
    fn error_fault_triggers_after_the_duration() {
        let mut w = watchdog(Duration::from_millis(100));

        // Above the error limit arms the deadline and raises nothing yet.
        assert_eq!(w.update_voltage(430.0), Action::Arm);
        assert_eq!(fault_kind(&w.on_deadline()), Some(FaultKind::Error));
    }

    #[test]
    fn a_voltage_drop_cancels_the_error_deadline() {
        let mut w = watchdog(Duration::from_millis(100));
        assert_eq!(w.update_voltage(430.0), Action::Arm);

        assert_eq!(w.update_voltage(400.0), Action::Cancel);

        // The deadline is gone, so its expiry decides nothing.
        assert_eq!(w.on_deadline(), Action::Nothing);
    }

    #[test]
    fn a_zero_duration_triggers_immediately() {
        let mut w = watchdog(Duration::ZERO);
        assert_eq!(fault_kind(&w.update_voltage(430.0)), Some(FaultKind::Error));
    }

    #[test]
    fn the_fault_is_latched_until_a_restart() {
        let mut w = watchdog(Duration::from_millis(100));
        assert_eq!(
            fault_kind(&w.update_voltage(460.0)),
            Some(FaultKind::Emergency)
        );

        // Latched: nothing further is evaluated, however bad the reading.
        assert_eq!(w.update_voltage(10_000.0), Action::Nothing);

        // A restart clears it and the same reading faults again.
        w.start();
        assert_eq!(
            fault_kind(&w.update_voltage(460.0)),
            Some(FaultKind::Emergency)
        );
    }

    #[test]
    fn a_stopped_watchdog_evaluates_nothing() {
        let mut w = watchdog(Duration::from_millis(100));
        assert_eq!(w.stop(), Action::Nothing);
        assert_eq!(w.update_voltage(460.0), Action::Nothing);
    }

    /// Stopping cancels a running deadline, and the expiry that arrives anyway
    /// decides nothing: a timer cancelled here can still be in flight.
    #[test]
    fn stopping_cancels_a_running_deadline() {
        let mut w = watchdog(Duration::from_millis(100));
        assert_eq!(w.update_voltage(430.0), Action::Arm);

        assert_eq!(w.stop(), Action::Cancel);

        assert_eq!(w.on_deadline(), Action::Nothing);
    }

    /// `multiple_voltage_updates_update_timer_snapshot`. The deadline keeps its
    /// own expiry and the report carries the worst voltage seen, not the last.
    #[test]
    fn further_readings_raise_the_snapshot_without_moving_the_deadline() {
        let mut w = watchdog(Duration::from_millis(100));

        assert_eq!(w.update_voltage(430.0), Action::Arm);
        // Still above the error limit, so the deadline stands rather than
        // restarting; a re-arm here would postpone the fault indefinitely under
        // a voltage that never comes down.
        assert_eq!(w.update_voltage(440.0), Action::Nothing);
        assert_eq!(w.update_voltage(435.0), Action::Nothing);

        let Action::Fault { description, .. } = w.on_deadline() else {
            panic!("the deadline raises");
        };
        assert!(
            description.contains("440.00 V"),
            "the worst reading is reported, got {description}"
        );
    }
}
