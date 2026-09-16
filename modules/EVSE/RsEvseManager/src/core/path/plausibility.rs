// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

//! The voltage plausibility monitor, ported from
//! `modules/EVSE/EvseManager/voltage_plausibility/VoltagePlausibilityMonitor.cpp`.
//!
//! Four independent instruments measure the same DC voltage: the power supply,
//! the billing powermeter, the isolation monitor and the over voltage monitor.
//! They should agree. A spread between them that persists is not a reading, it
//! is an instrument disagreeing with the others, and on a DC charger that is
//! the difference between a measured voltage and a believed one.
//!
//! As with the over voltage watchdog the C++ keeps its deadline on a background
//! thread; here it is a timer the path arms.

use std::time::Duration;

/// Which instrument reported. Named rather than indexed so a source cannot be
/// filed under another's slot by a caller counting wrong.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Source {
    PowerSupply,
    Powermeter,
    IsolationMonitor,
    OverVoltageMonitor,
}

#[derive(Clone, Debug, PartialEq)]
pub enum Action {
    Nothing,
    Arm,
    Cancel,
    Fault { description: String },
}

/// `VoltagePlausibilityMonitor`.
pub struct Plausibility {
    threshold_v: f64,
    duration: Duration,
    running: bool,
    latched: bool,
    armed: bool,
    /// The last value each instrument reported since the last reset. A source
    /// that has reported once stays in the comparison with that value: the C++
    /// keeps a timestamp but only ever tests it against zero, so it asks
    /// whether a source has ever spoken and never whether it spoke recently.
    power_supply_v: Option<f64>,
    powermeter_v: Option<f64>,
    isolation_monitor_v: Option<f64>,
    over_voltage_monitor_v: Option<f64>,
}

impl Plausibility {
    pub fn new(threshold_v: f64, duration: Duration) -> Self {
        Self {
            threshold_v,
            duration,
            running: false,
            latched: false,
            armed: false,
            power_supply_v: None,
            powermeter_v: None,
            isolation_monitor_v: None,
            over_voltage_monitor_v: None,
        }
    }

    /// `reset()` then `start_monitor()`, which is the pair the current demand
    /// callback calls. `reset` is what drops the samples, so a new charge does
    /// not compare against an instrument that last spoke during the previous
    /// one.
    pub fn restart(&mut self) -> Action {
        self.latched = false;
        self.power_supply_v = None;
        self.powermeter_v = None;
        self.isolation_monitor_v = None;
        self.over_voltage_monitor_v = None;
        self.running = true;
        self.disarm()
    }

    pub fn stop(&mut self) -> Action {
        self.running = false;
        self.disarm()
    }

    fn disarm(&mut self) -> Action {
        if self.armed {
            self.armed = false;
            return Action::Cancel;
        }
        Action::Nothing
    }

    /// One instrument reported. Every update re-evaluates the whole set, which
    /// is what the C++ does at each of its four entry points.
    pub fn update(&mut self, source: Source, voltage_v: f64) -> Action {
        if !self.running || self.latched {
            return Action::Nothing;
        }
        match source {
            Source::PowerSupply => self.power_supply_v = Some(voltage_v),
            Source::Powermeter => self.powermeter_v = Some(voltage_v),
            Source::IsolationMonitor => self.isolation_monitor_v = Some(voltage_v),
            Source::OverVoltageMonitor => self.over_voltage_monitor_v = Some(voltage_v),
        }
        self.evaluate()
    }

    fn evaluate(&mut self) -> Action {
        // Fewer than two instruments have nothing to disagree about, and the C++
        // cancels a running deadline rather than letting it expire on a set it
        // can no longer judge.
        let Some(spread_v) = self.spread_v() else {
            return self.disarm();
        };
        if spread_v <= self.threshold_v {
            return self.disarm();
        }
        if self.duration.is_zero() {
            self.latch();
            return Action::Fault {
                description: format!(
                    "Voltage spread {spread_v:.2} V exceeded threshold {:.2} V and fault duration is 0 ms -> fault immediately",
                    self.threshold_v
                ),
            };
        }
        if self.armed {
            // The deadline stands. Re-arming on every disagreeing sample would
            // postpone the fault for as long as the instruments keep reporting.
            return Action::Nothing;
        }
        self.armed = true;
        Action::Arm
    }

    /// The deadline expired. The C++ re-reads the instruments here rather than
    /// reporting the spread that armed it: updates may have stopped, a source
    /// may have dropped out of the set, or the instruments may have come back
    /// into agreement while the deadline ran.
    pub fn on_deadline(&mut self) -> Action {
        if !self.armed {
            return Action::Nothing;
        }
        self.armed = false;
        if !self.running || self.latched {
            return Action::Nothing;
        }
        let Some(spread_v) = self.spread_v() else {
            return Action::Nothing;
        };
        if spread_v <= self.threshold_v {
            return Action::Nothing;
        }
        self.latch();
        Action::Fault {
            description: format!(
                "Voltage spread {spread_v:.2} V exceeded threshold {:.2} V for at least {} ms.",
                self.threshold_v,
                self.duration.as_millis()
            ),
        }
    }

    /// `max - min` across the instruments that have reported, or `None` where
    /// fewer than two have.
    fn spread_v(&self) -> Option<f64> {
        let reported: Vec<f64> = [
            self.power_supply_v,
            self.powermeter_v,
            self.isolation_monitor_v,
            self.over_voltage_monitor_v,
        ]
        .into_iter()
        .flatten()
        .collect();
        if reported.len() < 2 {
            return None;
        }
        let max_v = reported.iter().copied().fold(f64::MIN, f64::max);
        let min_v = reported.iter().copied().fold(f64::MAX, f64::min);
        Some(max_v - min_v)
    }

    fn latch(&mut self) {
        self.latched = true;
        self.armed = false;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn monitor(duration: Duration) -> Plausibility {
        let mut m = Plausibility::new(50.0, duration);
        m.restart();
        m
    }

    fn is_fault(action: &Action) -> bool {
        matches!(action, Action::Fault { .. })
    }

    /// One instrument cannot disagree with anything, so nothing is judged until
    /// a second has reported.
    #[test]
    fn a_single_source_is_never_implausible() {
        let mut m = monitor(Duration::from_millis(100));
        assert_eq!(m.update(Source::PowerSupply, 400.0), Action::Nothing);
        assert_eq!(m.update(Source::PowerSupply, 10_000.0), Action::Nothing);
    }

    #[test]
    fn agreeing_sources_arm_nothing() {
        let mut m = monitor(Duration::from_millis(100));
        m.update(Source::PowerSupply, 400.0);
        assert_eq!(m.update(Source::IsolationMonitor, 430.0), Action::Nothing);
    }

    /// Strictly greater, so a spread exactly at the threshold is tolerated.
    #[test]
    fn the_threshold_comparison_is_strict() {
        let mut m = monitor(Duration::from_millis(100));
        m.update(Source::PowerSupply, 400.0);
        assert_eq!(m.update(Source::IsolationMonitor, 450.0), Action::Nothing);
        assert_eq!(m.update(Source::IsolationMonitor, 451.0), Action::Arm);
    }

    #[test]
    fn a_zero_duration_faults_immediately() {
        let mut m = monitor(Duration::ZERO);
        m.update(Source::PowerSupply, 400.0);
        assert!(is_fault(&m.update(Source::IsolationMonitor, 500.0)));
    }

    #[test]
    fn a_persistent_spread_faults_when_the_deadline_expires() {
        let mut m = monitor(Duration::from_millis(100));
        m.update(Source::PowerSupply, 400.0);
        assert_eq!(m.update(Source::IsolationMonitor, 500.0), Action::Arm);

        assert!(is_fault(&m.on_deadline()));
    }

    /// The deadline re-reads the instruments rather than reporting the spread
    /// that armed it, so instruments that come back into agreement while it
    /// runs raise nothing.
    #[test]
    fn instruments_that_agree_again_before_the_deadline_raise_nothing() {
        let mut m = monitor(Duration::from_millis(100));
        m.update(Source::PowerSupply, 400.0);
        assert_eq!(m.update(Source::IsolationMonitor, 500.0), Action::Arm);

        // Back within the threshold. The deadline is cancelled outright.
        assert_eq!(m.update(Source::IsolationMonitor, 420.0), Action::Cancel);
        assert_eq!(m.on_deadline(), Action::Nothing);
    }

    /// A further disagreeing sample keeps the deadline it already has: re-arming
    /// on each one would postpone the fault for as long as the instruments keep
    /// reporting, which is exactly while it matters.
    #[test]
    fn a_running_deadline_is_not_restarted_by_another_disagreement() {
        let mut m = monitor(Duration::from_millis(100));
        m.update(Source::PowerSupply, 400.0);
        assert_eq!(m.update(Source::IsolationMonitor, 500.0), Action::Arm);

        assert_eq!(m.update(Source::IsolationMonitor, 600.0), Action::Nothing);
        assert_eq!(m.update(Source::OverVoltageMonitor, 700.0), Action::Nothing);

        assert!(is_fault(&m.on_deadline()));
    }

    #[test]
    fn a_stopped_monitor_judges_nothing_and_cancels_its_deadline() {
        let mut m = monitor(Duration::from_millis(100));
        m.update(Source::PowerSupply, 400.0);
        assert_eq!(m.update(Source::IsolationMonitor, 500.0), Action::Arm);

        assert_eq!(m.stop(), Action::Cancel);

        assert_eq!(m.update(Source::IsolationMonitor, 900.0), Action::Nothing);
        assert_eq!(m.on_deadline(), Action::Nothing);
    }

    #[test]
    fn the_fault_latches_until_a_restart() {
        let mut m = monitor(Duration::ZERO);
        m.update(Source::PowerSupply, 400.0);
        assert!(is_fault(&m.update(Source::IsolationMonitor, 500.0)));

        assert_eq!(m.update(Source::IsolationMonitor, 900.0), Action::Nothing);

        m.restart();
        m.update(Source::PowerSupply, 400.0);
        assert!(is_fault(&m.update(Source::IsolationMonitor, 500.0)));
    }

    /// `reset()` drops the samples, so the instruments of the previous charge
    /// are not compared against the first instrument of this one.
    #[test]
    fn a_restart_forgets_the_previous_charges_instruments() {
        let mut m = monitor(Duration::ZERO);
        m.update(Source::PowerSupply, 400.0);

        m.restart();

        // The supply's 400 V is gone, so this lone reading has nothing to
        // disagree with however far it is from it.
        assert_eq!(m.update(Source::IsolationMonitor, 900.0), Action::Nothing);
    }

    /// The spread is across all reporting instruments, not between the two most
    /// recent, so a third that sits outside the pair is what raises.
    #[test]
    fn the_spread_is_taken_across_every_reporting_instrument() {
        let mut m = monitor(Duration::from_millis(100));
        m.update(Source::PowerSupply, 400.0);
        assert_eq!(m.update(Source::Powermeter, 410.0), Action::Nothing);

        assert_eq!(m.update(Source::OverVoltageMonitor, 480.0), Action::Arm);
    }
}
