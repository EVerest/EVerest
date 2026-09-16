// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

//! DC power path.
//!
//! Cable check is a sequence of stages advanced by timers, isolation readings and
//! effect completions. The C++ equivalent runs on a detached thread whose only
//! exit condition is an FSM state read, which produced an unplug race that had to
//! be worked around explicitly. Here an unplug is an event like any other and the
//! sequence transitions to `Abort`, so that race is not representable.

use std::time::{Duration, Instant};

use crate::core::event::Severity;
use crate::core::faults;
use crate::core::effect::{
    ByPath, ChargingPhase, CpState, Effect, EffectId, EffectIds, EffectOutcome, ErrorReport,
    HlcUpdate, Issued, SupplyMode,
    TimerId,
};
use crate::core::config::Wiring;
use crate::core::event::{BspEvent, CpEdges, IsolationReading};
use crate::core::path::over_voltage;
use crate::core::path::plausibility;
use crate::core::hlc::cable_check::{self, IsolationStatus};
use crate::core::hlc::dc_limits::{
    self, DynamicModeRequest, EvMaximumLimits, MaximumLimits, MinimumLimits,
};
use crate::core::hlc::{ConnectorKind, DataLinkRequest};
use crate::core::path::ac::PWM_5_PERCENT;
use crate::core::path::iec::AcState;
use crate::core::path::{PathEvent, PowerPath, SessionDuty, SessionProgress, StoppingOutcome};
use crate::core::session::{Session, StopReason};

/// Below this the cable is considered de-energized. IEC 61851-23.
pub const SAFE_VOLTAGE_V: f64 = 60.0;

pub const TIMER_CABLE_CHECK: TimerId = TimerId(300);
pub const TIMER_CONTACTOR_CONFIRM: TimerId = TimerId(301);
pub const TIMER_ENFORCE_LIMITS: TimerId = TimerId(302);
/// The software over voltage watchdog's error deadline.
pub const TIMER_OVER_VOLTAGE_ERROR: TimerId = TimerId(303);
/// The voltage plausibility comparison's fault deadline.
pub const TIMER_PLAUSIBILITY: TimerId = TimerId(304);
/// How long a DC charge runs on with no EVSE limits behind it before it is
/// stopped, `hlc_charge_loop_without_energy_timeout_s`. Every DC session is a
/// high level one (`Charger.cpp:217-224`), so the C++ branch that stops a basic
/// session at once cannot be reached here.
pub const TIMER_NO_ENERGY: TimerId = TimerId(305);

/// Re-apply EVSE limits to the supply even when the EV goes quiet.
pub const ENFORCE_TARGET_LIMITS_INTERVAL: Duration = Duration::from_secs(5);

/// Bound on every wait for the supply voltage to reach or to fall below a
/// target. `EvseManager.cpp:2425` and `EvseManager.cpp:2534`.
const WAIT_VOLTAGE_TIMEOUT: Duration = Duration::from_secs(10);

/// A measured voltage this close to the target counts as reached.
/// `EvseManager.cpp:2439`.
const VOLTAGE_REACHED_TOLERANCE_V: f64 = 10.0;

/// Bound on the isolation monitor self test verdict. `EvseManager.hpp:420`.
const SELF_TEST_TIMEOUT: Duration = Duration::from_secs(30);

/// `REQUIRED_CONSECUTIVE_FAILURES` and `MIN_TIME_BETWEEN_FIRST_AND_LAST_FAILURE`
/// (`EvseManager.hpp:350-351`). Both have to hold, so a burst inside one second
/// raises nothing and a slow drift does.
/// IEC 61851-23:2023 6.3.1.112.2, the fixed ceiling for a supply rated at or
/// below 500 V.
const MAX_VOLTAGE_TO_EARTH_STATIC_V: f64 = 550.0;

const REQUIRED_VOLTAGE_TO_EARTH_FAILURES: u32 = 2;
const MIN_TIME_BETWEEN_FIRST_AND_LAST_FAILURE: Duration = Duration::from_secs(2);

/// Bound on each isolation measurement sample. `EvseManager.cpp:2237`.
const ISOLATION_SAMPLE_TIMEOUT: Duration = Duration::from_secs(5);

/// Fallback when the vehicle never reported a maximum voltage.
/// `EvseManager.cpp:2137`.
const EV_MAX_VOLTAGE_FALLBACK_V: f64 = 500.0;

/// Which of the optional cable check steps this hardware needs.
///
/// Every one of these is a manifest key on the C++ module, and every read of
/// one sits inside a cable check sequence, which `Dc::begin_cable_check` starts
/// only where an isolation monitor is wired. So they belong to
/// `IsolationMonitor` and are reached only through it: a port with no isolation
/// monitor has no cable check options, rather than holding three settings
/// nothing can act on.
///
/// They were exactly that until this type moved. `Dc::new` seeded
/// `CableCheckOptions::default()` and the only writer was a
/// `set_cable_check_options` setter with no caller outside tests, so all three
/// manifest keys parsed into `DcSettings` and were dropped on the floor.
/// `scripts/reachability.py` had been reporting the setter as `TEST-ONLY-FN`
/// throughout.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct CableCheckOptions {
    /// `cable_check_enable_imd_self_test`.
    pub imd_self_test: bool,
    /// `cable_check_enable_imd_self_test_relays_open`. Some hardware closes its
    /// relays only after a successful self test, so the test runs first at a
    /// separate voltage.
    pub imd_self_test_relays_open: bool,
    /// `cable_check_wait_below_60V_before_finish`. IEC 61851-23:2023 CC.4.1.2.
    pub wait_below_60v_before_finish: bool,
}

impl Default for CableCheckOptions {
    /// The C++ manifest defaults.
    fn default() -> Self {
        Self {
            imd_self_test: true,
            imd_self_test_relays_open: false,
            wait_below_60v_before_finish: true,
        }
    }
}

/// The isolation monitor, present only where `r_imd` is.
///
/// It owns the three optional cable check steps, because those are the only
/// thing this side of the monitor decides; the measurements themselves arrive
/// as events. Holding them here is what makes "no monitor, no options" a fact
/// about the type rather than a fact about the call order.
pub struct IsolationMonitor {
    options: CableCheckOptions,
}

impl IsolationMonitor {
    /// `None` when no isolation monitor is connected. `r_imd` is an optional
    /// requirement and every C++ read of it is guarded by `!r_imd.empty()`;
    /// `imd_stop` is the guard in one place (`EvseManager.cpp:2296`).
    pub fn for_wiring(wiring: &Wiring, options: CableCheckOptions) -> Option<Self> {
        wiring.imd.then_some(Self { options })
    }
}

/// The pair the over voltage monitor is told to watch for.
///
/// Its fields are private to this module and `OverVoltageMonitor::thresholds`
/// is the only constructor, so `Effect::OverVoltageLimits` cannot be written
/// by code that does not hold an over voltage monitor. That is the whole of
/// the T17 class expressed in the type system: the derivation and the effect
/// variant both existed for six days with no production producer, the census
/// said so in a line nobody was required to read, and the compiler said
/// nothing.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct OverVoltageThresholds {
    emergency_v: f64,
    error_v: f64,
}

impl OverVoltageThresholds {
    /// Emergency is evaluated first and immediately by the monitor.
    pub fn emergency_v(&self) -> f64 {
        self.emergency_v
    }

    /// Error is evaluated only after the monitor's configured duration.
    pub fn error_v(&self) -> f64 {
        self.error_v
    }
}

/// The over voltage monitor, present only where `r_over_voltage_monitor` is.
///
/// It carries no state. The monitor is another module and this side owns only
/// the threshold derivation, which is why the type exists at all: while the DC
/// path held a `bool` for this, `Dc::new` took two adjacent `bool` arguments
/// that a call site could transpose, and a path built claiming a monitor it had
/// nothing to talk to was a `Dc` the compiler accepted.
pub struct OverVoltageMonitor;

impl OverVoltageMonitor {
    /// `None` when no over voltage monitor is connected. The requirement is
    /// optional and the C++ reads it behind `r_over_voltage_monitor.empty()`
    /// (`EvseManager.cpp:851`).
    pub fn for_wiring(wiring: &Wiring) -> Option<Self> {
        wiring.over_voltage_monitor.then_some(Self)
    }

    /// The two thresholds the monitor is told to watch for, ported from
    /// `EvseManager::get_emergency_over_voltage_threshold`
    /// (`EvseManager.cpp:1930`) and `get_error_over_voltage_threshold`
    /// (`:1963`).
    ///
    /// The two are derived differently and deliberately so. The emergency
    /// limit is a step function of what the vehicle and the supply jointly
    /// negotiated, taken from IEC 61851-23 (2023) 6.3.1.106.2 Table 103 and,
    /// above 1000 V, from IEC 61851-23-3 (DRAFT 2025) Table 202. The error
    /// limit is the vehicle's own maximum voltage, unclamped by the supply.
    ///
    /// The asymmetry lets the error limit sit above the emergency limit when
    /// the supply is the weaker of the two, at which point the monitor's
    /// emergency branch fires first and the error branch is unreachable. That
    /// is the C++ behavior and it is reproduced rather than corrected: the
    /// monitor is a watchdog on a negotiated ceiling, and narrowing the
    /// emergency step to the vehicle's rating would raise it above what this
    /// supply can survive.
    pub fn thresholds(
        &self,
        ev_max_voltage_v: f64,
        evse_max_export_voltage_v: f64,
    ) -> OverVoltageThresholds {
        let negotiated_max_voltage_v = ev_max_voltage_v.min(evse_max_export_voltage_v);

        let emergency_v = if negotiated_max_voltage_v > 1000.0 {
            1375.0
        } else if negotiated_max_voltage_v > 850.0 {
            1100.0
        } else if negotiated_max_voltage_v > 750.0 {
            935.0
        } else if negotiated_max_voltage_v > 500.0 {
            825.0
        } else {
            550.0
        };

        OverVoltageThresholds {
            emergency_v,
            error_v: ev_max_voltage_v,
        }
    }
}

/// Cable check voltage per IEC 61851-23:2023 CC.4.1.2 formula CC.1, ported from
/// `EvseManager.cpp:1909-1927`.
pub fn cable_check_voltage_for(ev_max_cpd_v: f64, evse_max_cpd_v: f64) -> f64 {
    let mut voltage_v = 500.0;
    if ev_max_cpd_v <= 500.0 {
        if ev_max_cpd_v + 50.0 < voltage_v {
            voltage_v = ev_max_cpd_v + 50.0;
        }
        if evse_max_cpd_v < voltage_v {
            voltage_v = evse_max_cpd_v;
        }
    } else {
        voltage_v = evse_max_cpd_v;
        if 1.1 * ev_max_cpd_v < voltage_v {
            voltage_v = 1.1 * ev_max_cpd_v;
        }
    }
    voltage_v
}


#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum CableCheck {
    /// Not running.
    Idle,
    /// Waiting for the cable to fall below the safe threshold before energizing.
    AwaitSafeVoltage,
    /// Optional isolation self test performed with the relays still open.
    RelaysOpenSelfTest,
    /// Contactor close requested, waiting for the confirmed closed fact.
    AwaitContactorClosed,
    /// Ramping to the derived cable check voltage. Doubles as the short circuit
    /// test, so current stays capped.
    RampUp,
    /// Isolation self test with the cable energized.
    EnergizedSelfTest,
    /// Collecting isolation samples.
    Sampling {
        taken: u8,
    },
    /// Ramping back below the safe threshold before reporting success.
    RampDown,
    /// Any failure. Energy is removed and we wait below the safe threshold
    /// before reporting the failure upward.
    Abort {
        reported: bool,
    },
    Done,
}

#[derive(Clone, Copy, Debug)]
pub struct DcConfig {
    pub isolation_voltage_v: f64,
    pub relays_open_voltage_v: f64,
    pub relays_closed_timeout: Duration,
    pub imd_measurements: u8,
    pub ramp_ampere_per_second: f64,
    /// Current cap during cable check, chosen so the short circuit test keeps
    /// resistance within range.
    pub cable_check_current_limit_a: f64,
    /// Which connector this port has. It chooses the isolation fault
    /// resistance threshold (`EvseManager.cpp:2004-2007`) and nothing else on
    /// this path.
    pub connector: ConnectorKind,
    /// How long the voltage must stay above the error limit before the software
    /// watchdog raises. Zero raises on the first reading above it.
    pub internal_over_voltage_duration: Duration,
    /// How far the instruments measuring the DC voltage may disagree, and for
    /// how long, before the disagreement is a fault.
    pub plausibility_max_spread_v: f64,
    pub plausibility_fault_duration: Duration,
    /// `hlc_charge_loop_without_energy_timeout_s`, how long a charge with no
    /// EVSE limits behind it runs on before it is stopped. Zero stops it at
    /// once, which is the C++ `> 0` guard at `Charger.cpp:839`.
    pub no_energy_timeout: Duration,
}

pub struct Dc {
    config: DcConfig,
    /// The isolation monitor and its cable check options, or `None` on a port
    /// with none wired. Absence is what makes `begin_cable_check` report the
    /// check complete without performing one.
    imd: Option<IsolationMonitor>,
    /// The over voltage monitor, or `None` on a port with none wired. It is the
    /// only source of an `OverVoltageThresholds`, so no threshold can be
    /// derived here without a monitor to send it to.
    over_voltage: Option<OverVoltageMonitor>,
    cable_check: CableCheck,
    /// Last confirmed supply voltage, absent until the supply has reported one.
    /// Used for the safe threshold gates, and by the DC target power clamp,
    /// which is the only reader that distinguishes an absent reading from a
    /// reading of zero (`EvseManager.cpp:2602-2603` tests `has_value()`).
    present_voltage_v: Option<f64>,
    /// Contactor permission has two independent gates and both must hold.
    hlc_allows_close: bool,
    iec_allows_close: bool,
    /// Cleared whenever the supply is switched off, so a stale setpoint cannot
    /// be considered still applied.
    applied_setpoint: Option<(f64, f64, SupplyMode)>,
    supply_mode: SupplyMode,
    /// Confirmed closed fact, reported by the board support as power on.
    contactor_closed: bool,
    /// `power_supply_DC_charging_phase`. Set by the three phase openings and
    /// returned to `Other` when the supply goes off, which is where the C++
    /// resets it too.
    charging_phase: ChargingPhase,
    /// Where the stop in flight lands once the relays open. Meaningful only
    /// while the progress is resident in `StoppingCharging`; see
    /// `StoppingOutcome`.
    stopping: StoppingOutcome,
    /// The maxima the vehicle reported for itself. The voltage is an input to
    /// formula CC.1 and both clamp the vehicle's own target.
    ev_maximum: EvMaximumLimits,
    /// The EVSE limit set the vehicle's request is clamped against. Held here
    /// rather than on the port that derives it, because that is where the C++
    /// holds it: `Charger::inform_new_evse_max_hlc_limits` and its minimum
    /// counterpart (`Charger.cpp:2032-2052`) write onto the state machine, and
    /// `get_evse_max_hlc_limits` is what every clamp reads.
    max_hlc_limits: MaximumLimits,
    min_hlc_limits: MinimumLimits,
    /// Maximum export voltage the power supply reports, the other CC.1 input.
    evse_max_export_voltage_v: f64,
    /// `powersupply_capabilities.min_export_voltage_V`, read only by the
    /// voltage to earth check.
    evse_min_export_voltage_v: f64,
    /// The software watchdog that shadows the hardware over voltage monitor.
    /// Present whatever the wiring, and fed only where a hardware monitor is,
    /// which is where the C++ subscribes to the voltage stream at all.
    over_voltage_watchdog: over_voltage::Watchdog,
    /// The four instrument comparison. Fed by whichever of them this deployment
    /// has; fewer than two reporting is not a disagreement.
    plausibility: plausibility::Plausibility,
    /// The out of range run the voltage to earth check is in the middle of, as
    /// `(failures so far, when the first of them arrived)`. `None` is "the last
    /// reading was in range", which is what an in range reading restores.
    ///
    /// Held as a pair because the rule is about the relationship between two
    /// readings, not a count: two failures two seconds apart raise, and two
    /// hundred inside one second do not.
    voltage_to_earth_failures: Option<(u32, Instant)>,
    /// Target the supply is ramping towards while a stage waits for it.
    awaiting_voltage_target: Option<f64>,
    /// Whether the stage timeout is currently armed, so it is cancelled exactly
    /// when it was armed.
    cable_check_timer_armed: bool,
    /// Identity of the self test whose verdict the current stage is waiting for.
    /// A completion carrying any other identity belongs to something else.
    ///
    /// `Issued<ByPath>`, so the only thing that can fill it is this path's own
    /// allocation: neither the identity a completion carries back nor one the
    /// core issued for its billing start has a type this slot accepts.
    pending_self_test: Option<Issued<ByPath>>,
    /// Source of the identities this path asks the loop to carry back.
    ///
    /// `None` until `adopt_effect_ids` runs, which `Core::new` does before any
    /// session moves. It used to be built with a private counter of its own,
    /// which is the second space that once let a powermeter reply pass an
    /// isolation monitor self test; there is no such counter to fall back on
    /// now, so absence is the honest state and a stage that reaches it fails
    /// rather than asking for a verdict nobody can attribute.
    effect_ids: Option<EffectIds<ByPath>>,
    /// True between `HlcEvent::CurrentDemandStarted` and
    /// `HlcEvent::CurrentDemandFinished`. One of the three conditions the import
    /// direction needs. `EvseManager.cpp:576` and `:593`.
    current_demand_active: bool,
    /// An availability decision took the port out of service and has not been
    /// reversed. The DC path holds no IEC reducer, so it keeps the one fact the
    /// reducer would have kept for it: without it, an enable would start a
    /// board that something else had stopped.
    disable_requested: bool,
    /// Energy management reports the session is actually exporting to grid.
    /// `EvseManager.hpp:234`, assigned from the enforced limits.
    exporting_to_grid: bool,
    /// The vehicle's target as it sent it, kept unclamped because the re-apply
    /// watchdog clamps again against whatever EVSE limit is in force when it
    /// fires. That is how an energy management change reaches the supply while
    /// the vehicle is quiet.
    raw_target_voltage_v: f64,
    raw_target_current_a: f64,
    /// Clamped voltage last applied, the C++ `latest_target_voltage`. Read back
    /// when the vehicle sends a zero voltage (`EvseManager.cpp:2595-2600`).
    latest_target_voltage_v: f64,
    /// Whether the five percent duty cycle is standing on the pilot,
    /// `internal_context.update_pwm_last_duty_cycle` narrowed to the one duty
    /// cycle a DC port ever offers. It is what makes the offer sites
    /// `update_pwm_now_if_changed` rather than `update_pwm_now`.
    five_percent_offered: bool,
    /// Whether the no budget deadline is running,
    /// `internal_context.hlc_charge_loop_no_energy_timeout_running`
    /// (`Charger.cpp:840`), so a run of empty limit sets arms it once.
    no_energy_deadline: bool,
    /// Clamped current the ramp is travelling towards.
    target_current_a: f64,
    /// Rate limited current actually written to the supply.
    ramped_current_a: f64,
    /// When the ramp last advanced. `None` until the first target arrives, which
    /// is what makes the first target reach the supply unlimited, as it does in
    /// the C++ where the timestamp predates the session.
    ramp_advanced_at: Option<Instant>,
    /// Whether the re-apply watchdog is armed, so it is cancelled exactly when
    /// it was armed.
    enforce_timer_armed: bool,
    /// Where the session has got to, in the mode independent vocabulary of
    /// `Charger::EvseState`. The DC triggers that move it are high level
    /// communication stages rather than control pilot levels, which is the
    /// whole of what is mode specific about it.
    progress: SessionProgress,
    /// Authorization has arrived and has not been cleared by the end of a
    /// session. Held because it and the vehicle arrive as separate events in
    /// either order, and whichever is last completes the decision, which is
    /// what re-reading `flag_authorized` on every pass through
    /// `WaitingForAuthentication` does in the C++.
    authorized: bool,
}

impl Dc {
    /// The two optional monitors arrive as the collaborators themselves and
    /// not as flags. They were two adjacent `bool` arguments until this change,
    /// which a call site could transpose without the compiler noticing, and
    /// which let a `Dc` claim a monitor while holding nothing to talk to.
    pub fn new(
        config: DcConfig,
        imd: Option<IsolationMonitor>,
        over_voltage: Option<OverVoltageMonitor>,
    ) -> Self {
        Self {
            config,
            imd,
            over_voltage,
            cable_check: CableCheck::Idle,
            present_voltage_v: None,
            hlc_allows_close: false,
            iec_allows_close: false,
            applied_setpoint: None,
            supply_mode: SupplyMode::Off,
            contactor_closed: false,
            charging_phase: ChargingPhase::Other,
            stopping: StoppingOutcome::Finished,
            ev_maximum: EvMaximumLimits::default(),
            max_hlc_limits: MaximumLimits::default(),
            min_hlc_limits: MinimumLimits::default(),
            evse_max_export_voltage_v: EV_MAX_VOLTAGE_FALLBACK_V,
            over_voltage_watchdog: over_voltage::Watchdog::new(
                config.internal_over_voltage_duration,
            ),
            plausibility: plausibility::Plausibility::new(
                config.plausibility_max_spread_v,
                config.plausibility_fault_duration,
            ),
            evse_min_export_voltage_v: 0.0,
            voltage_to_earth_failures: None,
            awaiting_voltage_target: None,
            cable_check_timer_armed: false,
            pending_self_test: None,
            effect_ids: None,
            current_demand_active: false,
            disable_requested: false,
            exporting_to_grid: false,
            raw_target_voltage_v: 0.0,
            raw_target_current_a: 0.0,
            latest_target_voltage_v: 0.0,
            target_current_a: 0.0,
            five_percent_offered: false,
            no_energy_deadline: false,
            ramped_current_a: 0.0,
            ramp_advanced_at: None,
            enforce_timer_armed: false,
            progress: SessionProgress::new(),
            authorized: false,
        }
    }

    /// Whether the wired isolation monitor asks for an optional cable check
    /// step.
    ///
    /// False without a monitor, and that is not a default answer standing in
    /// for a real one: all four call sites sit inside a sequence
    /// `begin_cable_check` starts only where a monitor is, so the `None` case
    /// is unreachable from them rather than answered by them. It is spelled as
    /// a conjunction so that adding a fifth reader outside the sequence gets
    /// the honest answer instead of `CableCheckOptions::default()`.
    fn imd_asks(&self, step: impl FnOnce(&CableCheckOptions) -> bool) -> bool {
        self.imd
            .as_ref()
            .is_some_and(|monitor| step(&monitor.options))
    }

    /// The vehicle reports these during charge parameter discovery
    /// (`subscribe_dc_ev_maximum_limits`, `EvseManager.cpp:851-870`).
    pub fn set_ev_maximum_limits(&mut self, maximum: EvMaximumLimits) {
        self.ev_maximum = maximum;
    }

    /// `Charger::inform_new_evse_max_hlc_limits` and
    /// `inform_new_evse_min_hlc_limits` (`Charger.cpp:2032-2052`), which the
    /// C++ calls as a pair from one place and which are therefore one call
    /// here: a port holding a maximum set from one derivation and a minimum set
    /// from another is not a state the C++ can reach.
    pub fn set_evse_hlc_limits(&mut self, maximum: MaximumLimits, minimum: MinimumLimits) {
        self.max_hlc_limits = maximum;
        self.min_hlc_limits = minimum;
    }

    /// Last confirmed supply voltage, or zero where none has been reported. The
    /// safe threshold gates read it this way, which is what the C++ value
    /// initialized member gives them.
    fn present_voltage_or_zero(&self) -> f64 {
        self.present_voltage_v.unwrap_or(0.0)
    }

    /// Live power supply capability, not configuration.
    pub fn set_evse_max_export_voltage_v(&mut self, voltage_v: f64) {
        self.evse_max_export_voltage_v = voltage_v;
    }

    pub fn cable_check_stage(&self) -> CableCheck {
        self.cable_check
    }

    /// Both gates must hold. Neither alone may close the contactor.
    /// `Charger.cpp:699-703`.
    ///
    /// The high level half has two writers, which is what the C++ has too. The
    /// cable check grants it once the cable is de-energized
    /// (`EvseManager.cpp:2044-2046`, inside the cable check thread), and the
    /// stack can raise or withdraw it at any time through `ac_close_contactor`,
    /// `ac_open_contactor` and the three data link requests. Only the second of
    /// those two writers is mode independent in name as well as in effect.
    ///
    /// The IEC half comes off the control pilot: state C or D out of B is the
    /// vehicle closing S2, and state B back out of either is it opening again
    /// (`Charger.cpp:1179-1183` and `:1186-1190`).
    ///
    /// Read by `grant_contactor_permission`, which is the only actuation site.
    pub fn may_close_contactor(&self) -> bool {
        self.hlc_allows_close && self.iec_allows_close
    }

    /// `Charger.cpp:739-742`, the `PrepareCharging` arm's contactor permission.
    ///
    /// The C++ re-evaluates this on every pass of `run_state_machine`, so the
    /// grant lands on whichever condition is satisfied last without any site
    /// having to know which that was. There is no tick here, so the same
    /// property is obtained by calling this from each site that can complete
    /// the set: the three gate writers, which are the cable check's own grant,
    /// the stack's `ac_close_contactor` and the control pilot reaching C or D,
    /// and the entry into the arm itself, for a session that already holds both
    /// gates when it arrives. Level triggered rather than edge triggered for
    /// that reason, and idempotent: re-granting an already granted permission
    /// is what the C++ does every tick and what the board support expects.
    ///
    /// Gated on `PrepareCharging` because that is the arm the C++ puts it in.
    /// The cable check runs inside that state, which is what makes this the
    /// step that lets `AwaitContactorClosed` complete.
    fn grant_contactor_permission(&mut self) -> Vec<Effect> {
        if self.progress.state() != AcState::PrepareCharging || !self.may_close_contactor() {
            return Vec::new();
        }
        vec![Effect::AllowPowerOn(true)]
    }

    /// Cable check voltage per IEC 61851-23:2023 CC.4.1.2 formula CC.1, derived
    /// from what the vehicle and the supply can each do. A configured
    /// `dc_isolation_voltage_V` above zero overrides the derivation, which is
    /// what `EvseManager.cpp:2154-2157` does.
    pub fn cable_check_voltage_v(&self) -> f64 {
        if self.config.isolation_voltage_v > 0.0 {
            return self.config.isolation_voltage_v;
        }
        cable_check_voltage_for(
            self.ev_maximum
                .maximum_voltage_v
                .unwrap_or(EV_MAX_VOLTAGE_FALLBACK_V),
            self.evse_max_export_voltage_v,
        )
    }

    /// The pair the given monitor is told to watch for, from the two facts this
    /// path already holds: the vehicle's reported maximum voltage and the
    /// supply's reported maximum export voltage. Both fall back to
    /// `EV_MAX_VOLTAGE_FALLBACK_V` when the vehicle reported no maximum, which
    /// is what the C++ logs an error and does.
    ///
    /// The monitor is a parameter rather than a field read, so this cannot be
    /// called on a port that has none: there would be nothing to hand the
    /// answer to.
    ///
    /// The supply capability is read raw rather than derated, which is what the
    /// C++ does too: `get_powersupply_capabilities` derates current and power
    /// only (`EvseManager.cpp:2692-2695`) and never touches
    /// `max_export_voltage_V`.
    fn over_voltage_thresholds(&self, monitor: &OverVoltageMonitor) -> OverVoltageThresholds {
        monitor.thresholds(
            self.ev_maximum
                .maximum_voltage_v
                .unwrap_or(EV_MAX_VOLTAGE_FALLBACK_V),
            self.evse_max_export_voltage_v,
        )
    }

    /// Current stays capped through cable check so the short circuit test keeps
    /// the measured resistance inside range.
    pub fn cable_check_current_a(&self) -> f64 {
        self.config.cable_check_current_limit_a
    }

    /// Switching the supply off invalidates any applied setpoint. Keeping a stale
    /// cache here silently re-applies old values on the next comparison.
    fn supply_off(&mut self) -> Vec<Effect> {
        self.applied_setpoint = None;
        self.supply_mode = SupplyMode::Off;
        // `powersupply_DC_off` returns the phase to `Other` on its way out, and
        // the cable check failure routes assign it before they call that, so
        // every route out of a phase ends at `Other` either way.
        self.charging_phase = ChargingPhase::Other;
        vec![Effect::SupplyOff]
    }

    /// Writes a target to a supply that is **already on**, switching direction
    /// first if it changed. Mode, then setpoint.
    ///
    /// This is `powersupply_DC_set`: `EvseManager.cpp:2337-2342` issues
    /// `setMode(Import)` and falls through to `setImportVoltageCurrent` at
    /// `:2364`, and `:2373-2379` issues `setMode(Export)` and falls through to
    /// `setExportVoltageCurrent` at `:2400`. A direction switch has to reach the
    /// supply before the target it applies to.
    ///
    /// An unchanged setpoint is not re-sent, which is the C++ cache at `:2307`.
    pub fn set_setpoint(
        &mut self,
        voltage_v: f64,
        current_a: f64,
        mode: SupplyMode,
    ) -> Vec<Effect> {
        let mut effects = Vec::new();
        if self.supply_mode != mode {
            self.supply_mode = mode;
            effects.push(Effect::SetSupplyMode {
                mode,
                phase: self.charging_phase,
            });
        }
        if self.applied_setpoint != Some((voltage_v, current_a, mode)) {
            self.applied_setpoint = Some((voltage_v, current_a, mode));
            effects.push(Effect::SetSupplySetpoint {
                mode,
                voltage_v,
                current_a,
            });
        }
        effects
    }

    /// Energizes a supply that is **off**. Setpoint, then mode.
    ///
    /// The C++ orders this at the caller rather than inside
    /// `powersupply_DC_set`: `powersupply_DC_set` at `EvseManager.cpp:2057` and
    /// `:2162` comes first and only writes a target, then `powersupply_DC_on` at
    /// `:2064` and `:2170`, and `powersupply_DC_on` at `:2295-2302` is the only
    /// thing that issues `setMode(Export)`. `process_dc_ev_target_voltage_current`
    /// does the same at `:2618` before `:2622`.
    ///
    /// The order is load bearing. `powersupply_DC_off` at `:2409-2422` records
    /// that the supply resets its internal targets on the off transition, so
    /// switching on before writing a target energizes the cable at whatever
    /// target the supply defaults to rather than the one intended.
    pub fn energize(&mut self, voltage_v: f64, current_a: f64, mode: SupplyMode) -> Vec<Effect> {
        let mut effects = self.store_target(voltage_v, current_a, mode);
        if self.supply_mode != mode {
            self.supply_mode = mode;
            effects.push(Effect::SetSupplyMode {
                mode,
                phase: self.charging_phase,
            });
        }
        effects
    }

    /// Writes a target and nothing else, which is the whole of what
    /// `powersupply_DC_set` does: `setMode` is issued by `powersupply_DC_on`
    /// (`EvseManager.cpp:2327-2335`) and by the two direction switches inside
    /// `powersupply_DC_set`, and by nothing else. So a target written to a
    /// supply that is off updates the target it holds and leaves it off.
    fn store_target(&mut self, voltage_v: f64, current_a: f64, mode: SupplyMode) -> Vec<Effect> {
        if self.applied_setpoint == Some((voltage_v, current_a, mode)) {
            return Vec::new();
        }
        self.applied_setpoint = Some((voltage_v, current_a, mode));
        vec![Effect::SetSupplySetpoint {
            mode,
            voltage_v,
            current_a,
        }]
    }

    /// The one place the two orderings are chosen between, so no call site has
    /// to know which case it is in.
    ///
    /// The discriminator is `supply_mode`, which is this port's
    /// `powersupply_dc_is_on`: the C++ gates its in-place mode switch on
    /// `powersupply_dc_is_on` at `EvseManager.cpp:2337` and `:2370`, and gates
    /// `powersupply_DC_on` on `not powersupply_dc_is_on` at `:2295`. A reader
    /// tells the case of a call site by asking whether the supply is off there,
    /// exactly as the C++ does.
    fn apply_target(&mut self, voltage_v: f64, current_a: f64, mode: SupplyMode) -> Vec<Effect> {
        if self.supply_mode == SupplyMode::Off {
            self.energize(voltage_v, current_a, mode)
        } else {
            self.set_setpoint(voltage_v, current_a, mode)
        }
    }

    /// Import needs all three of a bidirectional session, a running current
    /// demand and an actual export to grid. `EvseManager.cpp:2334-2336`. Cable
    /// check and precharge are therefore always export.
    fn direction_mode(&self, session: &Session) -> SupplyMode {
        if session.profile.bidirectional && self.current_demand_active && self.exporting_to_grid {
            SupplyMode::Import
        } else {
            SupplyMode::Export
        }
    }

    /// Clamps the vehicle's raw request against the limits in force now, a port
    /// of `process_dc_ev_target_voltage_current` (`EvseManager.cpp:2563-2612`).
    ///
    /// Re-clamped on every apply rather than once at intake, because that is
    /// what lets the watchdog carry a limit change to the supply while the
    /// vehicle is quiet.
    ///
    /// Five clamps in the C++ order, and the order matters twice: the vehicle's
    /// own maxima come before the EVSE maxima, and the zero voltage cache comes
    /// before the power clamp, whose conversion voltage it can supply.
    ///
    /// The clamp is against **maxima only**. There is no minimum clamp anywhere
    /// in this function, and the asymmetry is not an omission: the minimum limit
    /// set has exactly one reader in the whole module, the crossed bounds guard
    /// of the dynamic control mode, and the minimum voltage has a second in that
    /// same handler's export voltage rule.
    ///
    /// The Skoda Enyaq early return at `:2571-2574` is not ported. It is driven
    /// by `hack_skoda_enyaq`, one of the six manifest keys this port rejects
    /// outright, so there is no setting that could turn it on.
    fn clamped_target(&self, _session: &Session) -> (f64, f64) {
        let mut voltage_v = self.raw_target_voltage_v;
        let mut current_a = self.raw_target_current_a;

        // `:2576-2586`, "limit voltage/current for broken EV implementations".
        if let Some(max_a) = self.ev_maximum.maximum_current_a {
            current_a = current_a.min(max_a);
        }
        if let Some(max_v) = self.ev_maximum.maximum_voltage_v {
            voltage_v = voltage_v.min(max_v);
        }

        // `:2582-2585`. The ISO limit set, not the enforced AC current limit:
        // `Limits::max_current_a` comes from `ac_max_current_a`, which the
        // energy management DC branch never fills.
        current_a = current_a.min(self.max_hlc_limits.maximum_current_a);

        // `:2589-2600`, ISO 15118-20 [V2G20-2183]. A vehicle is allowed to send
        // only one of the pair, and some send a zero voltage mid loop, so the
        // last non zero voltage stands in. The guard is on the cached value
        // being positive, so the first target of a session has nothing to reuse
        // and stays at zero, which `apply_new_target` then refuses to write.
        if voltage_v == 0.0 && self.latest_target_voltage_v > 0.0 {
            voltage_v = self.latest_target_voltage_v;
        }

        // `:2602-2609`. The power ceiling becomes a current ceiling at the
        // voltage on the cable, or at the target voltage while the supply has
        // reported nothing. No division guard, and none in the C++ either: with
        // a conversion voltage of zero the product is zero, so a non negative
        // power ceiling is never exceeded and the branch is not taken. A
        // negative ceiling, which the wire type permits for the SAE
        // bidirectional case, would reach the division; that case has no
        // producer in this port.
        let conversion_voltage_v = self.present_voltage_v.unwrap_or(voltage_v);
        if current_a * conversion_voltage_v > self.max_hlc_limits.maximum_power_w {
            current_a = self.max_hlc_limits.maximum_power_w / conversion_voltage_v;
        }

        (voltage_v, current_a)
    }

    /// Rate limits the delivered current at `dc_ramp_ampere_per_second`, ported
    /// from `EvseManager.cpp:2640-2662`.
    ///
    /// Only a rise is limited: the C++ compares the signed difference against
    /// the allowance, so a fall reaches the supply at once. `now` arrives as a
    /// parameter where the C++ reads `steady_clock::now()` at `:2643`, which is
    /// what makes a ramp assertable without sleeping.
    fn advance_ramp(&mut self, now: Instant) -> f64 {
        let elapsed_ms = self
            .ramp_advanced_at
            .map(|at| now.saturating_duration_since(at).as_millis() as f64)
            .unwrap_or(0.0);
        let allowance_a = elapsed_ms / 1000.0 * self.config.ramp_ampere_per_second;
        let diff_a = self.target_current_a - self.ramped_current_a;
        if elapsed_ms > 0.0 && diff_a > allowance_a {
            self.ramped_current_a += allowance_a;
        } else {
            self.ramped_current_a = self.target_current_a;
        }
        self.ramp_advanced_at = Some(now);
        self.ramped_current_a
    }

    /// `EvseManager.cpp:2639-2666`. The clamped target reaches the supply rate
    /// limited, and the re-apply watchdog restarts because a target has just
    /// been applied.
    fn apply_new_target(&mut self, session: &Session, now: Instant) -> Vec<Effect> {
        let (voltage_v, current_a) = self.clamped_target(session);
        self.latest_target_voltage_v = voltage_v;
        self.target_current_a = current_a;

        let mut effects = Vec::new();
        // `:2641`. The gate reads the **clamped** voltage, so a vehicle that
        // sent a zero and had one cached still reaches the supply, and one that
        // sent a zero with nothing cached does not.
        if voltage_v > 0.0 {
            let ramped_a = self.advance_ramp(now);
            let mode = self.direction_mode(session);
            // `:2662`. The supply is switched **on** only with the contactor
            // confirmed closed; the target itself is written either way,
            // because `apply_new_target_voltage_current` at `:2659` runs above
            // the gate. So a target that arrives while the supply is off and
            // the relays are open is stored and not exported, which is what
            // keeps a target after a `PowerOff` from energizing an open
            // contactor.
            //
            // Only the off case is gated, as in the C++: `powersupply_DC_on`
            // early returns on a supply that is already on (`:2328`), so the
            // gate below it can only decide whether an off supply comes on.
            effects.extend(
                if self.supply_mode == SupplyMode::Off && !self.contactor_closed {
                    self.store_target(voltage_v, ramped_a, mode)
                } else {
                    self.apply_target(voltage_v, ramped_a, mode)
                },
            );
        }
        effects.extend(self.arm_enforce_limits());
        effects
    }

    /// The C++ resets a timestamp unconditionally at `EvseManager.cpp:2665` and
    /// its FSM only compares that timestamp while the DC charging state runs
    /// (`Charger.cpp:844-851`), so the watchdog exists exactly while current
    /// demand does. Re-arming replaces the previous generation.
    fn arm_enforce_limits(&mut self) -> Vec<Effect> {
        if !self.current_demand_active {
            return Vec::new();
        }
        self.enforce_timer_armed = true;
        vec![Effect::StartTimer {
            id: TIMER_ENFORCE_LIMITS,
            after: ENFORCE_TARGET_LIMITS_INTERVAL,
        }]
    }

    fn cancel_enforce_limits(&mut self) -> Vec<Effect> {
        if !self.enforce_timer_armed {
            return Vec::new();
        }
        self.enforce_timer_armed = false;
        vec![Effect::CancelTimer {
            id: TIMER_ENFORCE_LIMITS,
        }]
    }

    /// Takes an in flight discharge to zero without turning the supply round,
    /// the ramp down half of ADR-0018.
    ///
    /// Only a running discharge costs anything: the guard is the supply being
    /// in the import direction, which is the only state in which current is
    /// flowing out of the vehicle. The applied setpoint cache makes the second
    /// call free, so a supply repeating its withdrawal reports it once.
    ///
    /// The voltage is left where it is. Zero is the current, not the target,
    /// and a supply told to hold zero volts as well would collapse the cable
    /// rather than stop the flow through it.
    fn stop_discharge(&mut self) -> Vec<Effect> {
        // One match rather than two guards. The import direction and an
        // applied setpoint are the same fact: every writer of `supply_mode`
        // writes `applied_setpoint` with it, and `supply_off` clears both, so
        // a separate `None` guard would be an arm no state can reach. Asked
        // together, the fallthrough is the one live arm covering a supply that
        // is off or exporting.
        let (SupplyMode::Import, Some((voltage_v, _, _))) =
            (self.supply_mode, self.applied_setpoint)
        else {
            return Vec::new();
        };
        // The ramp position, and not the target. `apply_new_target` rewrites
        // `target_current_a` from the vehicle's request before every ramp
        // advance, so a write here would be dead; the ramp position is the
        // only carried state, and left standing at the discharge magnitude it
        // would let the next charge target reach the supply at once instead of
        // ramping up from zero. A mutation sweep found the target write
        // surviving, which is what says which of the two is load bearing.
        self.ramped_current_a = 0.0;
        self.set_setpoint(voltage_v, 0.0, SupplyMode::Import)
    }

    /// A new session must not inherit the previous session's ramp position or
    /// target.
    fn reset_charge_loop(&mut self) {
        self.current_demand_active = false;
        self.exporting_to_grid = false;
        self.raw_target_voltage_v = 0.0;
        self.raw_target_current_a = 0.0;
        self.latest_target_voltage_v = 0.0;
        self.target_current_a = 0.0;
        self.ramped_current_a = 0.0;
        self.ramp_advanced_at = None;
    }

    /// Energy management reports the direction of the actual power flow.
    pub fn set_exporting_to_grid(&mut self, exporting: bool) {
        self.exporting_to_grid = exporting;
    }

    /// The vehicle's requested DC target. `EvseManager.cpp:2563` through
    /// `:2620`. Stored raw and applied immediately.
    pub fn set_ev_target(
        &mut self,
        session: &Session,
        voltage_v: f64,
        current_a: f64,
        now: Instant,
    ) -> Vec<Effect> {
        self.raw_target_voltage_v = voltage_v;
        self.raw_target_current_a = current_a;
        self.apply_new_target(session, now)
    }

    /// The ISO 15118-20 dynamic control mode request
    /// (`subscribe_d20_dc_dynamic_charge_mode`, `EvseManager.cpp:740-825`).
    ///
    /// The clamp itself lives in `hlc::dc_limits`, because it reads the EVSE
    /// limit set that port derives; what this arm owns is the two live facts it
    /// needs, the running current demand and the voltage on the cable, and what
    /// it does with the answer.
    ///
    /// A refusal produces **no effects at all**, not even a re-armed watchdog:
    /// the C++ returns before it touches `raw_ev_target_voltage`, so the
    /// previous target stands and the previous watchdog generation keeps
    /// running.
    ///
    /// The conversion voltage is the present reading or, absent one, the last
    /// clamped target (`:815`, `ev_info.present_voltage.value_or(latest_target_voltage)`).
    pub fn on_dynamic_charge_mode(
        &mut self,
        session: &Session,
        request: DynamicModeRequest,
        now: Instant,
    ) -> Vec<Effect> {
        let actual_voltage_v = self
            .present_voltage_v
            .unwrap_or(self.latest_target_voltage_v);
        let Some(target) = dc_limits::dynamic_mode_target(
            request,
            self.max_hlc_limits,
            self.min_hlc_limits,
            self.exporting_to_grid,
            self.current_demand_active,
            actual_voltage_v,
        ) else {
            return Vec::new();
        };
        // `:822-824`. The resolved figures become the raw target and go through
        // the ordinary clamp and ramp, so the vehicle's request is clamped
        // twice against the maxima and once is not enough: the second clamp is
        // what the watchdog re-runs.
        self.set_ev_target(session, target.voltage_v, target.current_a, now)
    }

    /// Direction the supply is currently set to. `SupplyMode::Off` means it is
    /// not energized, which is the discriminator `apply_target` reads.
    pub fn supply_mode(&self) -> SupplyMode {
        self.supply_mode
    }

    pub fn begin_cable_check(&mut self) -> Vec<Effect> {
        // `subscribe_start_cable_check` assigns the phase immediately before it
        // runs the sequence, so every mode change the sequence makes carries
        // `CableCheck`.
        self.charging_phase = ChargingPhase::CableCheck;
        // With no isolation monitor wired, cable check is reported complete
        // without being performed. This is deliberate and matches the C++, which
        // only warns at startup.
        if self.imd.is_none() {
            self.cable_check = CableCheck::Done;
            // `EvseManager.cpp:2025-2026`. The vehicle is told isolation was
            // never checked before it is told the check is complete, so a
            // consumer reading the pair in order never sees a bare success it
            // could take for a measured one.
            return vec![
                Effect::HlcUpdate(HlcUpdate::IsolationStatus(IsolationStatus::NoImd)),
                Effect::HlcUpdate(HlcUpdate::CableCheckFinished(true)),
            ];
        }

        self.cable_check = CableCheck::AwaitSafeVoltage;
        self.pending_self_test = None;
        self.awaiting_voltage_target = None;
        let mut effects = self.advance_from_safe_voltage();
        if self.cable_check == CableCheck::AwaitSafeVoltage {
            effects.extend(self.arm_stage_timeout(WAIT_VOLTAGE_TIMEOUT));
        }
        effects
    }

    /// Arms the one stage timeout. Re-arming replaces the previous generation, so
    /// a stage that waits per sample simply arms again.
    fn arm_stage_timeout(&mut self, after: Duration) -> Vec<Effect> {
        self.cable_check_timer_armed = true;
        vec![Effect::StartTimer {
            id: TIMER_CABLE_CHECK,
            after,
        }]
    }

    /// Cancels the stage timeout only where it was armed, so the effect order
    /// stays a faithful record of what the sequence waited for.
    fn cancel_stage_timeout(&mut self) -> Vec<Effect> {
        if !self.cable_check_timer_armed {
            return Vec::new();
        }
        self.cable_check_timer_armed = false;
        vec![Effect::CancelTimer {
            id: TIMER_CABLE_CHECK,
        }]
    }

    /// `EvseManager.cpp:2037-2047`. Below the safe threshold the contactor may
    /// be allowed to close, then the optional relays open self test runs.
    ///
    /// This function **is** the transition out of `AwaitSafeVoltage`, and the
    /// contactor permission is granted here because of that, not merely
    /// alongside it. The C++ grants it at `:2047`, inside the detached thread
    /// `cable_check()` spawns at `:2030`, which is why the C++ has to defend
    /// the grant against the thread outliving its session: nothing else knows
    /// the grant happened, so `cable_check_should_exit()` is polled at every
    /// wait to catch an unplug that already withdrew it.
    ///
    /// A structural consequence of the stage machine: here the permission is
    /// held by the same object that holds the stage, and every route out of the
    /// stage is an event this path already handles, so a withdrawal
    /// (`PathEvent::DataLink`, an unplug, `to_safe_state`) cannot interleave
    /// with the grant. The race is not defended against, it is not
    /// representable.
    fn advance_from_safe_voltage(&mut self) -> Vec<Effect> {
        if self.present_voltage_or_zero() >= SAFE_VOLTAGE_V {
            return Vec::new();
        }
        let mut effects = self.cancel_stage_timeout();

        // Allow closing from the HLC side. The IEC state machine still has to
        // agree, which is why this is a gate and not an actuation.
        self.hlc_allows_close = true;
        effects.extend(self.grant_contactor_permission());

        if self.imd_asks(|options| options.imd_self_test_relays_open) {
            // The self test voltage is configured separately because no
            // resistance is measured here. `EvseManager.cpp:2053-2071`.
            self.cable_check = CableCheck::RelaysOpenSelfTest;
            let voltage_v = self.config.relays_open_voltage_v;
            self.awaiting_voltage_target = Some(voltage_v);
            effects.extend(self.apply_target(
                voltage_v,
                self.config.cable_check_current_limit_a,
                SupplyMode::Export,
            ));
            effects.extend(self.arm_stage_timeout(WAIT_VOLTAGE_TIMEOUT));
        } else {
            effects.extend(self.enter_await_contactor_closed());
        }
        effects
    }

    /// `EvseManager.cpp:2113-2127`. Contactors are normally already closed on
    /// entry, so this is a bounded wait for the confirmed fact rather than an
    /// actuation.
    fn enter_await_contactor_closed(&mut self) -> Vec<Effect> {
        if self.contactor_closed {
            return self.enter_ramp_up();
        }
        self.cable_check = CableCheck::AwaitContactorClosed;
        vec![Effect::StartTimer {
            id: TIMER_CONTACTOR_CONFIRM,
            after: self.config.relays_closed_timeout,
        }]
    }

    /// `EvseManager.cpp:2129-2185`. The ramp doubles as the short circuit test of
    /// IEC 61851-23:2023 6.3.1.109, which is why the current stays capped: the
    /// cap keeps the current below the 110 Ohm short circuit limit at every
    /// voltage, so side B falls below the safe threshold inside 2.5 seconds
    /// without the core having to time anything.
    fn enter_ramp_up(&mut self) -> Vec<Effect> {
        self.cable_check = CableCheck::RampUp;
        let voltage_v = self.cable_check_voltage_v();
        self.awaiting_voltage_target = Some(voltage_v);
        let mut effects = self.apply_target(
            voltage_v,
            self.config.cable_check_current_limit_a,
            SupplyMode::Export,
        );
        effects.extend(self.arm_stage_timeout(WAIT_VOLTAGE_TIMEOUT));
        effects
    }

    /// `EvseManager.cpp:2187-2223`, IEC 61851-23:2023 CC.4.1.3.
    ///
    /// The C++ starts the relays open self test and then waits for its verdict
    /// only here, concurrently with the contactor wait and the ramp. This port
    /// waits for the verdict in the stage that asked for it, so the cable is
    /// never taken above the relays open voltage on an unverified isolation
    /// monitor, and so the verdict cannot be attributed to the wrong stage.
    fn enter_energized_self_test(&mut self) -> Vec<Effect> {
        if self.imd_asks(|options| !options.imd_self_test || options.imd_self_test_relays_open) {
            return self.enter_sampling();
        }
        self.cable_check = CableCheck::EnergizedSelfTest;
        self.request_self_test(self.cable_check_voltage_v())
    }

    /// Asks for a self test under a fresh identity and remembers it, so only the
    /// verdict on this request can advance the stage that made it.
    fn request_self_test(&mut self, voltage_v: f64) -> Vec<Effect> {
        let id = match self.effect_ids.as_mut() {
            Some(ids) => ids.allocate(),
            // A `Dc` that never adopted the core's space can name no request
            // the core could correlate, so the verdict would land on whatever
            // else is awaiting. `Core::new` adopts before any session moves, so
            // reaching this is a broken invariant and not a port shape: fail
            // the stage rather than energize under an identity nobody issued.
            None => {
                log::error!(
                    "the DC path holds no source of effect identities, so the isolation \
                     monitor self test cannot be correlated; failing the cable check stage"
                );
                return self.fail_from_stage();
            }
        };
        self.pending_self_test = Some(id);
        let mut effects = vec![Effect::ImdSelfTest {
            id: id.effect_id(),
            voltage_v,
        }];
        effects.extend(self.arm_stage_timeout(SELF_TEST_TIMEOUT));
        effects
    }

    /// `EvseManager.cpp:2225-2258`, IEC 61851-23:2023 CC.4.1.4.
    fn enter_sampling(&mut self) -> Vec<Effect> {
        let mut effects = vec![Effect::ImdStart];
        if self.config.imd_measurements == 0 {
            // `EvseManager.cpp:2259-2261`. Nothing to wait for, so the self
            // test alone stands in for a measurement and isolation is declared
            // valid without any resistance having been read. The monitor is
            // still started above, because the C++ starts it at `:2226` before
            // the branch and leaves it running into precharge.
            effects.push(Effect::HlcUpdate(HlcUpdate::IsolationStatus(
                IsolationStatus::Valid,
            )));
            effects.extend(self.finish_cable_check());
            return effects;
        }
        self.cable_check = CableCheck::Sampling { taken: 0 };
        effects.extend(self.arm_stage_timeout(ISOLATION_SAMPLE_TIMEOUT));
        effects
    }

    /// `EvseManager.cpp:2260-2286`. The isolation monitor keeps running into
    /// precharge; only the rejected `hack_pause_imd_during_precharge` stopped it.
    fn finish_cable_check(&mut self) -> Vec<Effect> {
        let mut effects = self.cancel_stage_timeout();
        if self.imd_asks(|options| options.wait_below_60v_before_finish) {
            self.cable_check = CableCheck::RampDown;
            effects.extend(self.supply_off());
            effects.extend(self.arm_stage_timeout(WAIT_VOLTAGE_TIMEOUT));
            return effects;
        }
        self.cable_check = CableCheck::Done;
        effects.push(Effect::HlcUpdate(HlcUpdate::CableCheckFinished(true)));
        effects
    }

    /// The self test verdict arrived and was positive.
    fn on_self_test_passed(&mut self) -> Vec<Effect> {
        self.pending_self_test = None;
        let mut effects = self.cancel_stage_timeout();
        match self.cable_check {
            CableCheck::RelaysOpenSelfTest => {
                if self.imd_asks(|options| !options.imd_self_test) {
                    // The test voltage is removed again before the contactor
                    // wait. `EvseManager.cpp:2103`.
                    effects.extend(self.supply_off());
                }
                effects.extend(self.enter_await_contactor_closed());
            }
            CableCheck::EnergizedSelfTest => {
                effects.extend(self.enter_sampling());
            }
            _ => {}
        }
        effects
    }

    /// One instrument reported to the plausibility comparison.
    fn note_plausibility(&mut self, source: plausibility::Source, voltage_v: f64) -> Vec<Effect> {
        let action = self.plausibility.update(source, voltage_v);
        self.plausibility_effects(action)
    }

    fn plausibility_effects(&mut self, action: plausibility::Action) -> Vec<Effect> {
        match action {
            plausibility::Action::Nothing => Vec::new(),
            plausibility::Action::Arm => vec![Effect::StartTimer {
                id: TIMER_PLAUSIBILITY,
                after: self.config.plausibility_fault_duration,
            }],
            plausibility::Action::Cancel => vec![Effect::CancelTimer {
                id: TIMER_PLAUSIBILITY,
            }],
            plausibility::Action::Fault { description } => vec![
                Effect::CancelTimer {
                    id: TIMER_PLAUSIBILITY,
                },
                Effect::RaiseError(ErrorReport {
                    error_type: faults::VOLTAGE_PLAUSIBILITY.to_owned(),
                    sub_type: String::new(),
                    severity: Severity::High,
                    vendor_id: String::new(),
                    description,
                    message: String::new(),
                }),
            ],
        }
    }

    /// Tells the software watchdog the thresholds the hardware monitor is
    /// being told. A port with no monitor derives none and arms nothing.
    fn arm_watchdog_limits(&mut self) {
        let thresholds = match self.over_voltage.as_ref() {
            Some(monitor) => self.over_voltage_thresholds(monitor),
            None => return,
        };
        self.over_voltage_watchdog
            .set_limits(thresholds.emergency_v(), thresholds.error_v());
    }

    /// `reset()` then `start_monitor()`, which is the pair the current demand
    /// callback calls.
    fn over_voltage_watchdog_started(&mut self) -> over_voltage::Action {
        self.over_voltage_watchdog.start()
    }

    /// Turns the watchdog's decision into the timer and the error it implies.
    ///
    /// The deadline is a timer here rather than the C++'s background thread, so
    /// the watchdog stays a state machine and the expiry arrives as an ordinary
    /// timer event.
    fn watchdog_effects(&mut self, action: over_voltage::Action) -> Vec<Effect> {
        match action {
            over_voltage::Action::Nothing => Vec::new(),
            over_voltage::Action::Arm => vec![Effect::StartTimer {
                id: TIMER_OVER_VOLTAGE_ERROR,
                after: self.config.internal_over_voltage_duration,
            }],
            over_voltage::Action::Cancel => vec![Effect::CancelTimer {
                id: TIMER_OVER_VOLTAGE_ERROR,
            }],
            over_voltage::Action::Fault { kind, description } => {
                vec![
                    Effect::CancelTimer {
                        id: TIMER_OVER_VOLTAGE_ERROR,
                    },
                    Effect::RaiseError(ErrorReport {
                        error_type: faults::MREC5_OVER_VOLTAGE.to_owned(),
                        sub_type: String::new(),
                        // `raise_over_voltage_error` takes the severity from the
                        // fault type: an emergency shutdown against an error
                        // shutdown, IEC 61851-23 table CC.10.
                        severity: match kind {
                            over_voltage::FaultKind::Emergency => Severity::High,
                            over_voltage::FaultKind::Error => Severity::Medium,
                        },
                        vendor_id: String::new(),
                        description,
                        message: String::new(),
                    }),
                ]
            }
        }
    }

    /// A measurement taken while the session is charging.
    ///
    /// `check_isolation_resistance_in_range` emits the status on every
    /// measurement and its caller raises `MREC22ResistanceFault` under the
    /// `Resistance` sub type when it answers false. The raise is the whole
    /// point: the cable check is long over by then, so nothing else is
    /// watching the cable.
    ///
    /// There is no clear here, and that is the C++ shape too: the subscriber
    /// raises and the error's own lifecycle takes it down.
    fn on_isolation_while_charging(
        &mut self,
        reading: IsolationReading,
        now: Instant,
    ) -> Vec<Effect> {
        let resistance_ohm = reading.resistance_ohm;
        let status = cable_check::isolation_verdict(resistance_ohm, self.config.connector);
        let mut effects = vec![Effect::HlcUpdate(HlcUpdate::IsolationStatus(status))];
        if status == IsolationStatus::Fault {
            effects.push(Effect::RaiseError(ErrorReport {
                error_type: faults::MREC22_RESISTANCE.to_owned(),
                sub_type: "Resistance".to_owned(),
                severity: Severity::Medium,
                vendor_id: String::new(),
                description: format!(
                    "Isolation resistance too low during charging: {resistance_ohm} Ohm"
                ),
                message: String::new(),
            }));
        }
        effects.extend(self.on_voltage_to_earth(reading, now));
        effects
    }

    /// IEC 61851-23:2023 6.3.1.112.2, the second half of
    /// `subscribe_isolation_measurement`.
    ///
    /// Raising takes two readings out of range **and** at least two seconds
    /// between the first of them and the one in hand. A single out of range
    /// reading raises nothing, a burst inside the window raises nothing, and one
    /// reading back in range abandons the run. Once the pair holds, every
    /// further failure in the same run raises again: the C++ keeps counting and
    /// re-raising rather than latching.
    fn on_voltage_to_earth(&mut self, reading: IsolationReading, now: Instant) -> Vec<Effect> {
        if self.voltage_to_earth_in_range(&reading) {
            self.voltage_to_earth_failures = None;
            return Vec::new();
        }
        let (count, first) = match self.voltage_to_earth_failures {
            None => {
                // The first failure records when it arrived and nothing else:
                // the rule needs a second reading to have a relationship with.
                self.voltage_to_earth_failures = Some((1, now));
                return Vec::new();
            }
            Some((count, first)) => (count + 1, first),
        };
        self.voltage_to_earth_failures = Some((count, first));
        // The count comparison cannot fail while the constant is 2, because the
        // first failure returned above and every reading that reaches here is
        // therefore at least the second. It is kept because the C++ keeps it,
        // and it is what would bite first if the constant were raised. No test
        // covers it for the same reason: there is no reachable state where it
        // decides the answer.
        if count < REQUIRED_VOLTAGE_TO_EARTH_FAILURES
            || now.duration_since(first) < MIN_TIME_BETWEEN_FIRST_AND_LAST_FAILURE
        {
            return Vec::new();
        }
        vec![Effect::RaiseError(ErrorReport {
            error_type: faults::MREC22_RESISTANCE.to_owned(),
            sub_type: "VoltageToEarth".to_owned(),
            severity: Severity::Medium,
            vendor_id: String::new(),
            description: format!(
                "Voltage to earth too high during charging L1e: {} V, L2e: {} V",
                reading.voltage_to_earth_l1e_v.unwrap_or(f64::NAN),
                reading.voltage_to_earth_l2e_v.unwrap_or(f64::NAN)
            ),
            message: String::new(),
        })]
    }

    /// `check_voltage_to_protective_earth_in_range`. Absent readings answer "in
    /// range": what cannot be checked is not a failure. Below the supply's
    /// minimum export voltage the readings are not trusted either, because the
    /// monitor may be reporting them against a voltage already ramped down.
    fn voltage_to_earth_in_range(&self, reading: &IsolationReading) -> bool {
        let (Some(voltage_v), Some(l1e_v), Some(l2e_v)) = (
            reading.voltage_v,
            reading.voltage_to_earth_l1e_v,
            reading.voltage_to_earth_l2e_v,
        ) else {
            return true;
        };
        if voltage_v <= self.evse_min_export_voltage_v {
            return true;
        }
        // Above a 500 V rating the ceiling follows the measured voltage; at or
        // below it the standard's fixed 550 V stands.
        let ceiling_v = if self.evse_max_export_voltage_v > 500.0 {
            voltage_v * 1.1
        } else {
            MAX_VOLTAGE_TO_EARTH_STATIC_V
        };
        l1e_v.abs() < ceiling_v && l2e_v.abs() < ceiling_v
    }

    /// The verdict itself. `EvseManager.cpp` fails the cable check on a false
    /// one at both self test stages, with its own message at each; a verdict
    /// that never arrives is the separate stage timeout failure.
    fn on_self_test_verdict(&mut self, passed: bool) -> Vec<Effect> {
        if self.pending_self_test.is_none() {
            // No stage is waiting. The C++ stores every verdict into
            // `selftest_result` and only a waiting stage reads it.
            return Vec::new();
        }
        if passed {
            return self.on_self_test_passed();
        }
        log::error!("IMD self test failed during cable check");
        self.pending_self_test = None;
        self.fail_from_stage()
    }

    /// The one entry into `Abort`, so that every route into a failed attempt
    /// carries the same bound on the de-energize wait that follows it.
    ///
    /// Splitting it would be the whole defect: a route that set the stage
    /// without arming the bound would report no verdict at all whenever the
    /// supply also stopped reporting a voltage, and nothing in the sequence
    /// would say so.
    ///
    /// The bound is the same one every other stage wait gets.
    /// `EvseManager.cpp:2536-2539` reaches the same place from the other side:
    /// it logs that the voltage did not drop in time and sends the verdict
    /// anyway.
    fn enter_abort(&mut self) -> Vec<Effect> {
        self.cable_check = CableCheck::Abort { reported: false };
        self.cable_check_timer_armed = false;
        self.awaiting_voltage_target = None;
        self.pending_self_test = None;
        self.arm_stage_timeout(WAIT_VOLTAGE_TIMEOUT)
    }

    fn fail(&mut self) -> Vec<Effect> {
        let mut effects = self.supply_off();
        effects.push(Effect::CancelTimer {
            id: TIMER_CABLE_CHECK,
        });
        effects.push(Effect::CancelTimer {
            id: TIMER_CONTACTOR_CONFIRM,
        });
        effects.extend(self.enter_abort());
        effects
    }

    /// Energy removal comes first, then the isolation monitor is stopped where it
    /// was running. The C++ stops the monitor first at its two call sites
    /// (`EvseManager.cpp:2238` and `:2254`); removing energy first is the
    /// invariant that matters and the two effects run on separate lanes anyway.
    fn fail_from_stage(&mut self) -> Vec<Effect> {
        let imd_running = matches!(
            self.cable_check,
            CableCheck::Sampling { .. } | CableCheck::RampDown
        );
        let mut effects = self.fail();
        if imd_running {
            effects.push(Effect::ImdStop);
        }
        effects
    }

    /// A stage waiting for the supply to reach a target reads it here. Anything
    /// short of the target is just another reading; the stage timeout is the only
    /// thing that gives up.
    fn on_ramp_voltage_reading(&mut self) -> Vec<Effect> {
        let Some(target_v) = self.awaiting_voltage_target else {
            return Vec::new();
        };
        if (self.present_voltage_or_zero() - target_v).abs() >= VOLTAGE_REACHED_TOLERANCE_V {
            return Vec::new();
        }
        self.awaiting_voltage_target = None;
        let mut effects = self.cancel_stage_timeout();
        match self.cable_check {
            CableCheck::RelaysOpenSelfTest => {
                effects.extend(self.request_self_test(self.config.relays_open_voltage_v));
            }
            CableCheck::RampUp => {
                effects.extend(self.enter_energized_self_test());
            }
            _ => {}
        }
        effects
    }

    /// The failure is only reported once the cable is confirmed de-energized.
    fn maybe_report_failure(&mut self) -> Vec<Effect> {
        if self.present_voltage_or_zero() >= SAFE_VOLTAGE_V {
            return Vec::new();
        }
        self.report_failure()
    }

    /// The completion verdict for a failed attempt.
    ///
    /// The `reported` flag is the whole of the once-per-attempt guarantee, and
    /// it lives here rather than at the two call sites so that neither route
    /// can reach the emission without passing it. Both routes into a reported
    /// abort go through this function, and both hand it **either** abort state
    /// rather than pre-filtering: the de-energized reading and the abort's own
    /// bound. Pre-filtering at the call sites is what an earlier revision did,
    /// and it made this guard unreachable, so the invariant was really being
    /// enforced three times and pinned nowhere.
    ///
    /// This is a deliberate divergence from the C++, which sends the verdict
    /// twice on one of its abort routes. See `docs/architecture.md` under
    /// "Intentional divergences".
    fn report_failure(&mut self) -> Vec<Effect> {
        if self.cable_check != (CableCheck::Abort { reported: false }) {
            return Vec::new();
        }
        self.cable_check = CableCheck::Abort { reported: true };
        let mut effects = self.cancel_stage_timeout();
        effects.push(Effect::HlcUpdate(HlcUpdate::CableCheckFinished(false)));
        effects
    }
}

impl Dc {
    /// `Charger.cpp:511-568`: on DC an authorization carries
    /// `WaitingForAuthentication` straight to `PrepareCharging`, with none of
    /// the pilot detours the AC branch above it needs. The billing record is
    /// not a second condition here: the C++ opens one itself when none is open
    /// (`Charger.cpp:513-517`), and the core does the same before it calls in,
    /// so reading `session.transaction_active` would be a second derivation of
    /// the authorization this path already holds.
    ///
    /// Re-entrant on purpose. The authorization and the vehicle arrive as
    /// separate events in either order, and whichever is last completes the
    /// decision, which is what re-reading `flag_authorized` on every pass
    /// through the state does in the C++.
    fn run_authorization_loop(&mut self) -> Vec<Effect> {
        if !self.authorized || self.progress.state() != AcState::WaitingForAuthentication {
            return Vec::new();
        }
        self.enter_prepare_charging()
    }

    /// The five percent duty cycle that says "high level communication" rather
    /// than an ampacity, which is the only duty cycle a DC port offers.
    ///
    /// Three C++ sites raise it and all three are
    /// `update_pwm_now_if_changed(PWM_5_PERCENT)`: the
    /// `WaitingForAuthentication` entry (`Charger.cpp:302-315`), the
    /// `PrepareCharging` entry (`:749`) and the `Charging` entry (`:904`). So a
    /// standing offer is not re-sent, which is what the flag is for.
    ///
    /// `sleep_before_enabling_pwm_hlc_mode_ms` is not ported. The
    /// C++ sleeps that long inside the `WaitingForAuthentication` entry before
    /// raising the offer, so SLAC is ready for the first packet a fast vehicle
    /// sends in reply; the setting is parsed here and has no reader. Ceiling: a
    /// vehicle that answers the offer faster than this port's SLAC comes up may
    /// have to retry matching. Upgrade path: defer the offer behind a timer
    /// armed on the entry, on this path and on `AcHlc`, which raises the same
    /// offer from the same entry. Owner: RsEvseManager.
    fn offer_five_percent(&mut self) -> Vec<Effect> {
        if self.five_percent_offered {
            return Vec::new();
        }
        self.five_percent_offered = true;
        vec![Effect::PwmOn(PWM_5_PERCENT)]
    }

    /// The pilot said the offer is gone. `cp_state_X1` (`Charger.cpp:1297-1304`)
    /// which every route out of an offer reaches: the two data link requests
    /// that end a session, and the error.
    ///
    /// The command is issued whether or not an offer was standing, as the two
    /// requests issue it, and the flag follows so the next entry re-offers.
    fn withdraw_five_percent(&mut self) -> Vec<Effect> {
        self.five_percent_offered = false;
        vec![Effect::SetCpState(CpState::X1)]
    }

    /// What the `PrepareCharging` entry actuates on DC, said once for both
    /// routes into that state: the authorization that starts a charge and the
    /// resume that restarts one.
    ///
    /// `Charger.cpp:747-750`. The arm falls through to the pilot for either
    /// charge mode, and on DC the duty is always five percent. Without it the
    /// vehicle's state C never becomes a closed contactor: IEC 61851-1 Table
    /// A.6 sequence 4 needs the pilot running for the relay to follow, which is
    /// what makes this half of the same defect as the permission beside it.
    fn enter_prepare_charging(&mut self) -> Vec<Effect> {
        self.progress.enter(AcState::PrepareCharging);
        let mut effects = self.offer_five_percent();
        effects.extend(self.grant_contactor_permission());
        effects
    }

    /// Every field whose lifetime is one session, cleared in one place, as the
    /// `Idle` entry at `Charger.cpp:214-235` clears its set. Both routes out of
    /// a session, the unplug and the port leaving service, come through here.
    fn end_session(&mut self) {
        self.authorized = false;
        // The offer goes with the session, so the next vehicle is offered the
        // pilot from its own entry. This is the **only** clear: both routes
        // that drop the pilot on this path, the unplug and the disable, come
        // through here, and the C++ likewise leaves the duty cycle behind on
        // the `Idle` entry its own `cp_state_X1` is part of (`:212-235`). A
        // second clear beside either `SetCpState` was removed after a sweep
        // found that neither changed anything.
        self.five_percent_offered = false;
    }

    /// True while a transaction holds the port, whether or not energy flows.
    ///
    /// The states whose arms in `Charger::run_state_machine` all test the same
    /// seven stop reasons: `PrepareCharging`, `Charging` and
    /// `ChargingPausedEVSE`. `ChargingPausedEV` is not among them because no DC
    /// route reaches it: the vehicle side pause is a control pilot fact and
    /// this path has no reducer to observe one.
    fn charging_session_live(&self) -> bool {
        matches!(
            self.progress.state(),
            AcState::PrepareCharging | AcState::Charging | AcState::ChargingPausedEvse
        )
    }

    /// The `Charging` arm's no energy test on DC (`Charger.cpp:836-866`).
    ///
    /// Every DC session is a high level one, which `Charger.cpp:217-224` states
    /// outright and this path reports as `hlc_charging_active`, so only the
    /// branch that grants `hlc_charge_loop_without_energy_timeout_s` is
    /// reachable here; a configured zero stops the charge at once, which is
    /// that branch's own `else`. Reports whether the charge was stopped, so the
    /// caller does not then re-apply a target into a supply just switched off.
    ///
    /// The deadline is cancelled by limits arriving, and the return of the
    /// limits also resumes a charge this stopped: that is the paused arm
    /// finding its reason list empty (`:1004-1032`), which on DC is the same
    /// preparation entry a resume request runs.
    fn reassess_energy(&mut self) -> (Vec<Effect>, bool) {
        if self.power_available() {
            let mut effects = Vec::new();
            if std::mem::take(&mut self.no_energy_deadline) {
                effects.push(Effect::CancelTimer {
                    id: TIMER_NO_ENERGY,
                });
            }
            if self.progress.state() == AcState::ChargingPausedEvse
                && self.stopping == StoppingOutcome::NoEnergy
            {
                effects.extend(self.enter_prepare_charging());
            }
            return (effects, false);
        }
        if self.progress.state() != AcState::Charging {
            return (Vec::new(), false);
        }
        if self.config.no_energy_timeout.is_zero() {
            return (self.stop_for_no_energy(), true);
        }
        if self.no_energy_deadline {
            return (Vec::new(), false);
        }
        self.no_energy_deadline = true;
        (
            vec![Effect::StartTimer {
                id: TIMER_NO_ENERGY,
                after: self.config.no_energy_timeout,
            }],
            false,
        )
    }

    /// The stop a lost budget begins, which is the pause's route with its own
    /// destination: the supply goes off, the relays are released, the re-apply
    /// watchdog stops, and the `StoppingCharging` exit settles into
    /// `ChargingPausedEVSE` because `not power_available()` is the first of its
    /// three disjuncts (`:1088`).
    fn stop_for_no_energy(&mut self) -> Vec<Effect> {
        self.no_energy_deadline = false;
        self.begin_stopping(StoppingOutcome::NoEnergy);
        let mut effects = self.supply_off();
        effects.push(Effect::AllowPowerOn(false));
        effects.extend(self.cancel_enforce_limits());
        self.settle_stopping();
        effects
    }

    /// The one way into `StoppingCharging`, and the one place that says where
    /// the stop lands when the relays are observed open.
    ///
    /// Three routes come through it and each states its own destination: the
    /// external stop and the unplug end the session, and the EVSE pause does
    /// not. `Dc` has no control pilot reducer, so the announcement the entry
    /// owes travels on the progress edge alone.
    fn begin_stopping(&mut self, outcome: StoppingOutcome) {
        self.stopping = outcome;
        self.progress.enter(AcState::StoppingCharging);
    }

    /// A stop in flight completes on the relays being observed open and on
    /// nothing else, which is the `StoppingCharging` exit.
    ///
    /// Run both from the board fact and from the routes that begin a stop, for
    /// the reason `Charger::run_state_machine`'s settle loop re-runs its
    /// switch: a stop that begins with the relays already reported open has
    /// nothing left to wait for, and waiting anyway would park the session in
    /// `StoppingCharging` behind a board fact that has been and gone.
    ///
    /// Only the pause moves the progress on. Everything else parks in
    /// `StoppingCharging` until the unplug, which is the pre-existing shape of
    /// every DC stop: that exit's own `Finished` branch would enter it here,
    /// and this path has no `Finished` entry to run. Ceiling: a stopped DC
    /// session announces no `Finished` and closes its record on the unplug edge
    /// instead. Owner: RsEvseManager.
    fn settle_stopping(&mut self) {
        if self.progress.state() != AcState::StoppingCharging || self.contactor_closed {
            return;
        }
        // Two of the three disjuncts of the C++ exit's pause branch (`:1088`),
        // each as the destination the stop was begun for.
        if matches!(
            self.stopping,
            StoppingOutcome::PausedByEvse | StoppingOutcome::NoEnergy
        ) {
            self.progress.enter(AcState::ChargingPausedEvse);
        }
    }

    /// Where a session ends up when it ends. `Charger.cpp:237-238`: a disable
    /// outstanding sends the `Idle` entry straight back out of service, so an
    /// unplug never returns a disabled port to advertising availability.
    fn resting_state(&self) -> AcState {
        if self.disable_requested {
            AcState::Disabled
        } else {
            AcState::Idle
        }
    }
}

impl PowerPath for Dc {
    /// The DC path awaits the isolation monitor self test, so it is the one
    /// path that needs the core's space rather than a private counter. Without
    /// this the self test and the metering transaction start both begin at zero
    /// and answer each other.
    ///
    /// The one writer of this field. `EffectIds<ByPath>` cannot be built, only
    /// delegated, so what lands here is always a handle on the core's counter.
    fn adopt_effect_ids(&mut self, ids: EffectIds<ByPath>) {
        self.effect_ids = Some(ids);
    }

    fn take_entered_states(&mut self) -> Vec<AcState> {
        self.progress.take_entered()
    }

    fn state(&self) -> AcState {
        self.progress.state()
    }

    fn target_voltage_v(&self) -> f64 {
        self.latest_target_voltage_v
    }

    fn name(&self) -> &'static str {
        "Dc"
    }

    /// Always true on DC, and a constant rather than a field because the C++
    /// has no route that makes it anything else. `Charger.cpp:217-224` writes
    /// it directly on the `Idle` entry and reads "for DC, it is always HLC
    /// mode"; the other writer, `Charger::set_hlc_charging_active`
    /// (`:2118-2121`), only sets it, so on DC it can only re-assert what the
    /// entry already decided.
    ///
    /// Read by the enforced limits handler, which refuses a phase change on it,
    /// and by `Core::ask_vehicle_to_stop` and `Core::stop_error_for`, which are
    /// the two mode independent signalling sites this answer reaches.
    /// A DC deployment never presents fake DC: the mode is AC hardware
    /// pretending, and `config.ac_with_soc` on a DC port changes nothing
    /// because `charge_mode` decides the subscriptions first
    /// (`EvseManager::ready` reaches `if (config.ac_with_soc)` only after
    /// that). This port presents real DC.
    fn presents_fake_dc(&self) -> bool {
        false
    }

    fn hlc_charging_active(&self) -> bool {
        true
    }

    /// Whether the EVSE limits in force are ones a charge can run on.
    /// `Charger::power_available`'s DC branch (`Charger.cpp:2128-2129`), which
    /// reads the same enforced limit set this holds and tests both figures.
    ///
    /// On the trait rather than beside this path's own no energy stop, which is
    /// its other reader, so the pause reason set above the trait and the stop
    /// here cannot disagree about whether there is a budget.
    fn power_available(&self) -> bool {
        self.max_hlc_limits.maximum_current_a > 0.0 && self.max_hlc_limits.maximum_power_w > 0.0
    }

    /// There is no control pilot duty cycle on DC and nothing asks this.
    ///
    /// `get_max_current_signalled_to_ev_internal` has exactly one caller,
    /// `Charger::check_soft_over_current`, and the DC branch of the charging
    /// state does not call it (`Charger.cpp:839-850` takes the enforce target
    /// route instead). This port keeps that structural: soft overcurrent
    /// detection is built for AC alone, so no detector exists to ask a DC path
    /// this question. Zero is the honest answer to "what current did the pilot
    /// signal", not a threshold anything measures against.
    fn signalled_current_a(&self) -> f64 {
        0.0
    }

    fn on_startup(&mut self) -> Vec<Effect> {
        // No control pilot reducer here, so this is the board support output
        // and nothing else. `Charger.cpp:95-100` is mode independent.
        // `Charger.cpp:54` starts the state machine resting in `Idle`.
        self.progress.enter(AcState::Idle);
        vec![Effect::BspEnable(true)]
    }

    fn on_session_start(&mut self, _session: &Session, _now: Instant) -> Vec<Effect> {
        self.cable_check = CableCheck::Idle;
        self.hlc_allows_close = false;
        self.iec_allows_close = false;
        self.awaiting_voltage_target = None;
        self.cable_check_timer_armed = false;
        self.pending_self_test = None;
        self.reset_charge_loop();
        let mut effects = self.supply_off();
        effects.extend(self.cancel_enforce_limits());
        // `Charger.cpp:1129-1133`, the plug in seen from `Idle`. Guarded on the
        // resting state for the same reason the C++ handles it only in that
        // case: a port out of service does not open a session.
        //
        // After the session reset above, not before it: the authorization loop
        // can grant the contactor permission, and the two gates it reads are
        // cleared here. Granting first would read the previous session's.
        if self.progress.state() == AcState::Idle {
            self.progress.enter(AcState::WaitingForAuthentication);
            // `Charger.cpp:302-315`. The entry raises the five percent offer
            // for a session that has a vehicle at it, **before** any
            // authorization: `hlc_use_5percent_current_session` is always true
            // on DC. Plug and charge depends on it, and on nothing else -- the
            // authorization it is waiting for arrives over a data link the
            // vehicle cannot establish until the pilot is running, so an offer
            // deferred until after the authorization is a wait that cannot
            // end.
            effects.extend(self.offer_five_percent());
            // An authorization that was the first user interaction is already
            // held, and the vehicle just completed the pair.
            effects.extend(self.run_authorization_loop());
        }
        effects
    }

    fn on_authorized(&mut self, _session: &Session, _now: Instant) -> Vec<Effect> {
        self.authorized = true;
        self.run_authorization_loop()
    }

    fn on_bsp(
        &mut self,
        _session: &Session,
        event: &BspEvent,
        edges: CpEdges,
        _now: Instant,
    ) -> Vec<Effect> {
        match event {
            // A control pilot state A reading and a disconnected cable are
            // one fact, which `CpEvent::is_unplug` names. Acting on only one of
            // them leaves the supply energized on the cable just pulled.
            BspEvent::Cp(cp) if cp.is_unplug() => {
                // An unplug during cable check is an ordinary event here.
                let mut effects = self.fail();
                effects.extend(self.to_safe_state());
                // `Charger::run_state_machine`'s `Charging` arm reaches
                // `StoppingCharging` on `not flag_ev_plugged_in` for either
                // charge mode, and that state's entry is what asks the vehicle
                // to end the session. Crossing it here is what makes a DC
                // unplug tell the vehicle as much as an AC one does; the
                // resting edge below still carries the record close, which
                // `duties_for_edge` attaches to an arrival from anywhere but
                // `Finished`.
                if self.charging_session_live() {
                    self.begin_stopping(StoppingOutcome::Finished);
                }
                self.end_session();
                let resting = self.resting_state();
                self.progress.enter(resting);
                effects
            }
            // `Charger.cpp:1179-1183` and `:1186-1188`,
            // `CPEvent::CarRequestedPower`. The vehicle closed S2, which is the
            // IEC half of the contactor permission. The C++ raises the event
            // only for C or D arrived at from B (`IECStateMachine.cpp:241-243`);
            // the simplified mode entry straight from A is a plug in and grants
            // nothing, and DC is never in simplified mode anyway.
            BspEvent::Cp(crate::core::event::CpEvent::C | crate::core::event::CpEvent::D)
                if edges.requested_power =>
            {
                self.iec_allows_close = true;
                self.grant_contactor_permission()
            }

            // `Charger.cpp:1181-1183`, `CPEvent::CarRequestedStopPower`: state B
            // back out of C or D is the vehicle opening S2 again.
            //
            // Only the permission is withdrawn here. The C++
            // `PrepareCharging` arm also switches the supply off and hops to
            // `ChargingPausedEVSE` (`:1189-1191`); that hop is the DC pause,
            // which this path does not carry (see `PathEvent::ResumeRequested`).
            // Clearing the gate without it is still correct: it cannot leave a
            // stale grant standing. Owner: RsEvseManager.
            BspEvent::Cp(crate::core::event::CpEvent::B) if edges.requested_stop_power => {
                self.iec_allows_close = false;
                Vec::new()
            }

            BspEvent::Cp(crate::core::event::CpEvent::PowerOn) => {
                // The confirmed contactor closed fact.
                self.contactor_closed = true;
                if self.cable_check == CableCheck::AwaitContactorClosed {
                    let mut effects = vec![Effect::CancelTimer {
                        id: TIMER_CONTACTOR_CONFIRM,
                    }];
                    effects.extend(self.enter_ramp_up());
                    return effects;
                }
                Vec::new()
            }
            BspEvent::Cp(crate::core::event::CpEvent::PowerOff) => {
                self.contactor_closed = false;
                // `EvseManager.cpp:1156-1159` drops both held targets with the
                // contactor. The voltage is the load bearing one: it is the
                // zero voltage cache at `:2638`, so a vehicle that sends a
                // zero target after the relays open has nothing to be given in
                // its place, the `voltage > 0` gate at `:2683` refuses the
                // write, and the supply keeps no target from the charge that
                // just ended.
                //
                // **The C++'s companion write of the current is not ported**,
                // because here it cannot be observed: `apply_new_target`
                // rewrites `target_current_a` from the vehicle's request
                // before every ramp advance reads it, and nothing else reads
                // it at all. A mutation sweep confirmed it - deleting the
                // write reddened nothing in 1526 tests. The ramp position is
                // not reset either, and that one is parity: the C++ does not
                // reset its low pass on this arm.
                self.latest_target_voltage_v = 0.0;
                // The `StoppingCharging` arm waits on the relays: their being
                // observed open is what completes a stop in flight.
                self.settle_stopping();
                Vec::new()
            }
            _ => Vec::new(),
        }
    }

    fn on_limits_changed(&mut self, _session: &Session, _now: Instant) -> Vec<Effect> {
        Vec::new()
    }

    fn on_stop(&mut self, _session: &Session, _reason: StopReason, _now: Instant) -> Vec<Effect> {
        // The stop tests in the `PrepareCharging`, `Charging` and
        // `ChargingPausedEVSE` arms of `Charger::run_state_machine`. A charge
        // already under way is stopped through `StoppingCharging` rather than
        // dropped, so the vehicle is told before the record closes. Nothing is
        // under way in any other state, so nothing moves.
        //
        // A cancel arriving on a session the EVSE had paused re-enters the same
        // state and restates the destination, which is what keeps the pause
        // from settling back into `ChargingPausedEvse` behind it.
        if self.charging_session_live() {
            self.begin_stopping(StoppingOutcome::Finished);
        }
        self.to_safe_state()
    }

    fn on_timer(&mut self, session: &Session, id: TimerId, now: Instant) -> Vec<Effect> {
        if id == TIMER_ENFORCE_LIMITS && self.enforce_timer_armed {
            // Five seconds of vehicle silence. The EVSE limits in force now
            // reach the supply anyway, which is what `Charger.cpp:844-851`
            // exists for.
            return self.apply_new_target(session, now);
        }
        if id == TIMER_NO_ENERGY && self.no_energy_deadline {
            // `Charger.cpp:855-860`. The session was given its configured run
            // without limits and it is over. The limits are re-read, so a
            // deadline that fires in the same pass as a limit set arriving
            // decides on the limits.
            self.no_energy_deadline = false;
            if self.progress.state() == AcState::Charging && !self.power_available() {
                return self.stop_for_no_energy();
            }
            return Vec::new();
        }
        if id == TIMER_PLAUSIBILITY {
            let expired = self.plausibility.on_deadline();
            return self.plausibility_effects(expired);
        }
        if id == TIMER_OVER_VOLTAGE_ERROR {
            // The voltage stayed above the error limit for the whole window.
            let expired = self.over_voltage_watchdog.on_deadline();
            return self.watchdog_effects(expired);
        }
        if id == TIMER_CONTACTOR_CONFIRM && self.cable_check == CableCheck::AwaitContactorClosed {
            // Contactor never confirmed closed. Give up rather than energize.
            return self.fail();
        }
        if id == TIMER_CABLE_CHECK && self.cable_check_timer_armed {
            if matches!(self.cable_check, CableCheck::Abort { .. }) {
                // The failure is already decided and the cable never confirmed
                // itself de-energized. Report it anyway rather than aborting an
                // abort. The flag is cleared here because this timer has fired,
                // so nothing is left to cancel.
                self.cable_check_timer_armed = false;
                return self.report_failure();
            }
            // Whatever the stage was waiting for did not arrive.
            return self.fail_from_stage();
        }
        Vec::new()
    }

    fn on_effect_done(
        &mut self,
        _session: &Session,
        id: Option<EffectId>,
        outcome: &EffectOutcome,
        _now: Instant,
    ) -> Vec<Effect> {
        // The isolation monitor self test verdict reaches the core as the
        // outcome of the effect that asked for it, and that effect carries the
        // identity this path chose when it asked. A completion under any other
        // identity, including an abandoned request reissued since, is not this
        // stage's verdict.
        //
        // The identity is checked before the outcome, and that order is the
        // guard. Matching `Failed` first made every failed completion this
        // path's stage failing, so a refused metering transaction start could
        // abort a cable check that had nothing to do with the meter. That it
        // did not was a property of `Core::apply`'s routing, which offers each
        // completion to `answer_transaction_start` first: one file's safety
        // resting on another file's order. Nothing this path did not ask for
        // reaches the failure path now, whatever it says.
        //
        // No real failure is lost by ignoring the rest: an effect this path
        // never awaited cannot report the verdict a stage waits for, and every
        // waiting stage arms a bound, so a stalled sequence still ends in a
        // reported failure. Both halves are pinned below.
        if !self
            .pending_self_test
            .is_some_and(|awaited| awaited.answers(id))
        {
            return Vec::new();
        }

        // A failed dispatch is the stage's failure: the monitor never took the
        // request, so no verdict is coming.
        if let EffectOutcome::Failed(_) = outcome {
            return self.fail_from_stage();
        }
        // A completed command says the monitor accepted the request and nothing
        // more. `EvseManager.cpp` waits for the published verdict after
        // `call_start_self_test` returns, so the stage stays pending until
        // `PathEvent::IsolationSelfTest` arrives or `SELF_TEST_TIMEOUT` expires.
        Vec::new()
    }

    fn on_path_event(&mut self, session: &Session, event: PathEvent, now: Instant) -> Vec<Effect> {
        match event {
            // `subscribe_start_cable_check` sets the charging phase and runs the
            // sequence. This is its only production entry point.
            PathEvent::CableCheckRequired => self.begin_cable_check(),

            // `subscribe_start_pre_charge` assigns `power_supply_DC_charging_phase`
            // and nothing else, so this actuates nothing by itself: the next
            // mode change carries the phase. In particular precharge must not
            // clear `current_demand_active`; only the current demand start
            // does that.
            PathEvent::PreChargeStarted => {
                self.charging_phase = ChargingPhase::PreCharge;
                Vec::new()
            }

            // `EvseManager.cpp:574-586`: the target is applied first, then the
            // over voltage monitor starts. The supply is off here because cable
            // check ended below the safe threshold, so this apply is an
            // energize.
            PathEvent::CurrentDemandStarted => {
                // `Charger::notify_currentdemand_started` (`Charger.cpp:2024`),
                // the only route into `Charging` on DC and guarded on
                // `PrepareCharging` there too.
                if self.progress.state() == AcState::PrepareCharging {
                    self.progress.enter(AcState::Charging);
                }
                // `subscribe_current_demand_started` assigns the phase before it
                // applies the target, so the energize below carries `Charging`.
                self.charging_phase = ChargingPhase::Charging;
                self.current_demand_active = true;
                let mut effects = self.apply_new_target(session, now);
                if self.over_voltage.is_some() {
                    effects.push(Effect::OverVoltageStart);
                }
                // `reset()` then `start_monitor()` on the same callback, beside
                // the hardware monitor's `call_start`.
                let started = self.over_voltage_watchdog_started();
                effects.extend(self.watchdog_effects(started));
                let restarted = self.plausibility.restart();
                effects.extend(self.plausibility_effects(restarted));
                effects
            }

            // Two independent C++ subscriptions to the same variable, folded
            // into one path here: `:592-598` clears the charge loop flags and
            // stops the over voltage and voltage plausibility monitors, and
            // `:849` switches the DC supply off.
            //
            // The fold is safe, and it is not safe merely because the two touch
            // disjoint state. Their relative order is **deterministic**:
            // everest-framework keeps the handlers for one topic in a
            // `std::vector` filled by `push_back`
            // (`lib/everest/framework/lib/message_handler.cpp:362`) and
            // dispatches them with a plain forward loop (`:410`, `:434`,
            // `:446`), so insertion order is registration order on every run
            // and `:592` always precedes `:849`. This arm reproduces that
            // order: the flag clear and the energy removal, then the monitor
            // stop.
            //
            // Energy removal precedes the monitor stop rather than following
            // the C++ interleaving, because the off invalidates the applied
            // setpoint cache and nothing reads the monitors in between.
            //
            // The session state deliberately does not move: neither
            // subscription touches `shared_context.current_state`. The charge
            // ends through the stop that follows, not through this.
            PathEvent::CurrentDemandFinished => {
                self.current_demand_active = false;
                let mut effects = self.supply_off();
                if self.over_voltage.is_some() {
                    effects.push(Effect::OverVoltageStop);
                }
                let stopped = self.over_voltage_watchdog.stop();
                effects.extend(self.watchdog_effects(stopped));
                let plausibility_stopped = self.plausibility.stop();
                effects.extend(self.plausibility_effects(plausibility_stopped));
                effects.extend(self.cancel_enforce_limits());
                effects
            }

            // `EvseManager.cpp:827-832`. The comment there is the whole of the
            // decision: "Car requests DC contactor open. We don't actually open
            // but switch off DC supply. Opening will be done by Charger on C->B
            // CP event." So the relays are not touched and the session state
            // does not move; the energy behind the relays goes away.
            PathEvent::OpenContactorDc => {
                let mut effects = self.supply_off();
                if self.imd.is_some() {
                    effects.push(Effect::ImdStop);
                }
                effects
            }

            // ADR-0018, divergence 4. The supply withdrew its bidirectional
            // capability mid session. The C++ decides nothing here: its three
            // read sites recompute a disjunction that never consulted the
            // capability at all, so a withdrawal reaches the supply as
            // whichever direction the next target happens to resolve to, with
            // the previous discharge current still applied across the turn
            // round.
            //
            // The port takes the discharge to zero first and leaves the
            // direction alone, so the turn round that the next target performs
            // happens at zero current. The refusal of any later discharge is
            // not enforced here: it is the resolution's, which the core has
            // already recomputed into `session.profile.bidirectional` before
            // routing this.
            PathEvent::BidirectionalWithdrawn => self.stop_discharge(),

            PathEvent::MatchingStarted(_) | PathEvent::SetupFinished => Vec::new(),

            // The matched half of the SLAC state and the vehicle's state of
            // charge. `EvseManager` subscribes the state of charge only inside
            // `if (config.ac_with_soc)`, which a DC port is not, and the
            // matched flag has one reader, the reinitialization, which no DC
            // route starts.
            PathEvent::SlacMatched(_) | PathEvent::StateOfCharge { .. } => Vec::new(),

            // The SLAC error routine is not ported on DC.
            // `Charger::request_error_sequence` (`Charger.cpp:2133-2147`) is
            // not charge mode gated, and `hlc_use_5percent_current_session` is
            // unconditionally true on DC (`Charger.cpp:293`), so the C++ runs
            // the `T_step_EF` detour here too and returns to the state it left
            // with the five percent offer restored. `Dc` has no state for that
            // hold: this path's progress has no arm that owns a timed pilot
            // level, which is the same obstacle the data link error's
            // reinitialization meets above. The SLAC reset is deliberately not
            // sent on its own: the C++ signals it from inside the same
            // function as the kick, and a reset without the kick drops the
            // link without inviting the rematch it exists to invite, which is
            // a state the C++ never reaches. Ceiling: a DC vehicle whose SLAC
            // asks for the error routine gets no pilot kick and stays on the
            // link it has. Upgrade path: the same timed pilot arm the data link
            // error's ceiling names. Owner: RsEvseManager.
            PathEvent::SlacErrorRoutine => Vec::new(),

            // `EvseManager.cpp:395-403` wires `ac_close_contactor` and
            // `ac_open_contactor` straight onto
            // `Charger::set_hlc_allow_close_contactor` with no charge mode
            // branch, and `Charger.cpp:699-703` reads the flag on the DC side.
            // The variable names say AC; the writer and the reader do not.
            PathEvent::AllowCloseContactor(allow) => {
                self.hlc_allows_close = allow;
                self.grant_contactor_permission()
            }

            // `Charger::dlink_pause` (`Charger.cpp:2195-2200`),
            // `dlink_terminate` (`:2203-2208`) and `dlink_error`
            // (`:2210-2276`) each open by clearing the same permission, none of
            // them behind a charge mode branch.
            //
            // The pilot goes with it. The first two follow the clear with an
            // unconditional `cp_state_X1()`, and the error follows it with one
            // too on a DC port, because its `hlc_use_5percent_current_session`
            // branch is always taken there. So all three leave the port
            // signalling X1, which is what says the offer is withdrawn.
            //
            // The error's **matching recovery** is still not ported.
            // The C++ ends `dlink_error` with `start_reinit()` on the same
            // always taken branch, which holds the configured pilot level for
            // `reinit_duration_ms` and restarts the session from
            // `WaitingForAuthentication` ([V2G3-M07-04] through [V2G3-M07-07]).
            // `Dc` has no state for that hold: the reducer's `Reinit` is the
            // AC paths', and this path's progress has no arm that owns a timed
            // pilot level. Ceiling: a DC vehicle whose data link errors is left
            // at X1 with its permission withdrawn, and recovers by unplugging
            // rather than by rematching. Upgrade path: give `Dc`'s progress a
            // reinit arm with the deadline the reducer's carries. Owner:
            // RsEvseManager.
            PathEvent::DataLink(request) => {
                self.hlc_allows_close = false;
                // `:2227-2242`. A link error that arrives while the session is
                // stopping for good is the high level session shutting down
                // rather than a fault to recover from, so it takes the energy
                // and the vehicle's permission with it instead of signalling a
                // withdrawal the session will not act on.
                let stopping_for_good = request == DataLinkRequest::Error
                    && self.progress.state() == AcState::StoppingCharging
                    && (!session.transaction_active
                        || !self.authorized
                        || self.disable_requested);
                let mut effects = self.withdraw_five_percent();
                if stopping_for_good {
                    effects.extend(self.supply_off());
                    effects.push(Effect::AllowPowerOn(false));
                }
                effects
            }

            // `EvseManager.cpp:734-738`. The raw target is stored and applied
            // at once.
            PathEvent::DcEvTarget {
                voltage_v,
                current_a,
            } => self.set_ev_target(session, voltage_v, current_a, now),

            PathEvent::DcDynamicChargeMode(request) => {
                self.on_dynamic_charge_mode(session, request, now)
            }

            // `EvseManager.cpp:851-870`. What it stores is read by the DC
            // target clamp, by the cable check voltage derivation and by the
            // over voltage thresholds, all three here. The `ev_info`
            // republication beside it is not ported.
            //
            // This is the only site in the C++ that pushes thresholds
            // (`:861-869`), and so it is the only one here. In particular the
            // supply capability arm does not push, even though the emergency
            // threshold reads that capability: a capability report that arrives
            // after this one leaves the monitor on the thresholds this arm last
            // derived, which is what the C++ leaves it on.
            PathEvent::DcEvMaximumLimits(maximum) => {
                self.set_ev_maximum_limits(maximum);
                // The same pair reaches both monitors here, as
                // `subscribe_dc_ev_maximum_limits` sets both. Kept beside the
                // emission rather than inside it: the shape below is the one
                // `scripts/unconstructable.py` patches to prove a port with no
                // monitor cannot derive thresholds at all.
                self.arm_watchdog_limits();
                match &self.over_voltage {
                    Some(monitor) => vec![Effect::OverVoltageLimits(
                        self.over_voltage_thresholds(monitor),
                    )],
                    None => Vec::new(),
                }
            }

            PathEvent::OverVoltageMeasurement { voltage_v } => {
                let action = self.over_voltage_watchdog.update_voltage(voltage_v);
                let mut effects = self.watchdog_effects(action);
                effects.extend(self.note_plausibility(
                    plausibility::Source::OverVoltageMonitor,
                    voltage_v,
                ));
                effects
            }

            PathEvent::MeterVoltage { voltage_v } => {
                self.note_plausibility(plausibility::Source::Powermeter, voltage_v)
            }

            // `Charger::inform_new_evse_max_hlc_limits` and its minimum
            // counterpart (`Charger.cpp:2032-2052`), then
            // `mod->is_actually_exporting_to_grid` and the target re-apply the
            // same C++ block writes beside them (`energyImpl.cpp:667-675`).
            // The re-apply is conditional there and so it is here; the
            // watchdog would carry the new limits within five seconds either
            // way, but only the re-apply carries them at the moment the
            // direction changes.
            PathEvent::DcEnforcedLimits {
                maximum,
                minimum,
                exporting_to_grid,
                reapply_target,
            } => {
                self.set_evse_hlc_limits(maximum, minimum);
                self.set_exporting_to_grid(exporting_to_grid);
                // The availability test comes first, as it does in the C++ arm,
                // and a charge it stopped is not then handed a fresh target:
                // the supply has just been switched off with the relays still
                // reported closed, so a re-apply would energize it again.
                let (mut effects, stopped) = self.reassess_energy();
                if reapply_target && !stopped {
                    effects.extend(self.apply_new_target(session, now));
                }
                effects
            }

            PathEvent::DcExportVoltageRange { min_v, max_v } => {
                self.evse_min_export_voltage_v = min_v;
                self.set_evse_max_export_voltage_v(max_v);
                Vec::new()
            }

            PathEvent::SupplyVoltage { voltage_v } => {
                self.present_voltage_v = Some(voltage_v);
                // The supply is one of the four instruments the plausibility
                // comparison reads, so every reading it publishes is reported
                // whatever the cable check is doing with it.
                let mut effects =
                    self.note_plausibility(plausibility::Source::PowerSupply, voltage_v);
                effects.extend(match self.cable_check {
                    CableCheck::AwaitSafeVoltage => self.advance_from_safe_voltage(),
                    CableCheck::RelaysOpenSelfTest | CableCheck::RampUp => {
                        self.on_ramp_voltage_reading()
                    }
                    CableCheck::RampDown if self.present_voltage_or_zero() < SAFE_VOLTAGE_V => {
                        let mut effects = self.cancel_stage_timeout();
                        self.cable_check = CableCheck::Done;
                        effects.push(Effect::HlcUpdate(HlcUpdate::CableCheckFinished(true)));
                        effects
                    }
                    // Both abort states, so that whether a verdict has already
                    // gone out is decided in `report_failure` and nowhere else.
                    // Matching only `reported: false` here would work equally
                    // well and would make that guard unreachable, which is the
                    // same invariant written twice: a mutation deleting the
                    // guard would then survive the whole suite.
                    CableCheck::Abort { .. } => self.maybe_report_failure(),
                    // Stages that wait on something other than a voltage
                    // reading: a contactor fact, a self test verdict, an
                    // isolation sample, or no sequence running at all. The ramp
                    // down is here too, for a reading that has not yet fallen
                    // below the safe threshold the guarded arm above tests.
                    CableCheck::Idle
                    | CableCheck::Done
                    | CableCheck::AwaitContactorClosed
                    | CableCheck::EnergizedSelfTest
                    | CableCheck::Sampling { .. }
                    | CableCheck::RampDown => Vec::new(),
                });
                effects
            }
            // The DC path holds no IEC reducer, so the transition out of
            // service is assembled here rather than routed through one. It is
            // the same three statements the shared C++ handler issues for
            // either charge mode (`Charger.cpp:203-204`): energy out, the
            // control pilot says unavailable, then the board output stops. The
            // vehicle is deliberately not released; an occupied port has
            // already been carried through stopping by the time this arrives,
            // and the latch is popped by the unplug.
            PathEvent::Disable => {
                self.disable_requested = true;
                self.end_session();
                // `Charger.cpp:1082` reaches `Disabled` out of `Finished`, so a
                // port leaving service under a vehicle closes its record on the
                // way.
                self.progress.enter(AcState::Disabled);
                let mut effects = self.to_safe_state();
                effects.push(Effect::SetCpState(CpState::F));
                effects.push(Effect::BspEnable(false));
                effects
            }

            // `Charger.cpp:1730` starts the board and the Idle entry it hands
            // to signals availability at `:230`. Gated on an outstanding
            // request so an enable cannot start a board a fault stopped.
            PathEvent::Enable => {
                if !self.disable_requested {
                    return Vec::new();
                }
                self.disable_requested = false;
                self.progress.enter(AcState::Idle);
                vec![Effect::BspEnable(true), Effect::SetCpState(CpState::X1)]
            }

            PathEvent::IsolationSelfTest(passed) => self.on_self_test_verdict(passed),

            PathEvent::Isolation(reading) => {
                if let CableCheck::Sampling { taken } = self.cable_check {
                    let taken = taken + 1;
                    if taken < self.config.imd_measurements {
                        self.cable_check = CableCheck::Sampling { taken };
                        // Each sample gets its own bound.
                        return self.arm_stage_timeout(ISOLATION_SAMPLE_TIMEOUT);
                    }
                    // Only the sample the sequence waited for is trusted, which
                    // is what `EvseManager.cpp:2236-2258` checks.
                    //
                    // The status is derived once and both the emission and the
                    // branch read that one answer, so the vehicle cannot be
                    // told `Valid` on an attempt that goes on to abort. The C++
                    // splits them: `check_isolation_resistance_in_range`
                    // (`:2003-2018`) emits the status and returns a separate
                    // `bool` its caller branches on.
                    let status =
                        cable_check::isolation_verdict(reading.resistance_ohm, self.config.connector);
                    let mut effects =
                        vec![Effect::HlcUpdate(HlcUpdate::IsolationStatus(status))];
                    match status {
                        IsolationStatus::Fault => effects.extend(self.fail_from_stage()),
                        IsolationStatus::Valid | IsolationStatus::NoImd => {
                            effects.extend(self.finish_cable_check())
                        }
                    }
                    return effects;
                }
                // Outside the cable check the same measurement is still read,
                // and while charging it is still acted on: the isolation
                // monitor runs on into the session and a resistance that falls
                // below the threshold there is a fault, not a reading nobody
                // wanted. `subscribe_isolation_measurement` checks
                // `get_current_state() == Charging` for exactly this.
                //
                // Its own voltage is the third instrument of the plausibility
                // comparison, and the C++ feeds that before it tests the state,
                // so a disagreement is judged whatever the charge is doing.
                let mut effects = match reading.voltage_v {
                    Some(voltage_v) => {
                        self.note_plausibility(plausibility::Source::IsolationMonitor, voltage_v)
                    }
                    None => Vec::new(),
                };
                if self.progress.state() == AcState::Charging {
                    effects.extend(self.on_isolation_while_charging(reading, now));
                }
                effects
            }

            // The EVSE withdraws power with the transaction left open.
            //
            // `Charger::run_state_machine`'s `Charging` arm leaves for
            // `StoppingCharging` on `flag_paused_by_evse` among its seven
            // reasons, and that state's entry is what asks an ISO 15118-20
            // vehicle to pause. That arm reads no charge mode, so this is the
            // same hop the AC reducer makes; what differs is what a DC port has
            // to withdraw.
            //
            // The order is the DC hazard order, the one `to_safe_state` and the
            // end of current demand both keep: the energy goes first and the
            // relay permission after it, so no step releases the vehicle onto a
            // cable that is still live. The re-apply watchdog is cancelled last
            // of all, which is what keeps that ordering independent of whether
            // it happened to be armed, and stopping it is what keeps a paused
            // port from writing fresh targets into a supply it just switched
            // off.
            //
            // The monitors are deliberately left running. The session is still
            // open and the cable is still connected, so the isolation and over
            // voltage watches have something to watch; the C++ stops them from
            // `subscribe_current_demand_finished`, which is the vehicle's
            // answer to this request rather than part of it.
            //
            // Only `Charging` hops, which is the only state whose C++ arm tests
            // the flag. Ceiling: a pause arriving during a DC preparation is
            // dropped rather than deferred. The C++ keeps the flag standing and
            // the `Charging` arm acts on it on the pass after current demand
            // starts; there is no second read site here to do that. Owner:
            // RsEvseManager.
            PathEvent::PauseRequested => {
                if self.progress.state() != AcState::Charging {
                    return Vec::new();
                }
                self.begin_stopping(StoppingOutcome::PausedByEvse);
                let mut effects = self.supply_off();
                effects.push(Effect::AllowPowerOn(false));
                effects.extend(self.cancel_enforce_limits());
                self.settle_stopping();
                effects
            }

            // The `ChargingPausedEVSE` arm leaves for `PrepareCharging` once no
            // reason to pause is left (`Charger.cpp:1019-1026`), and that entry
            // is the whole of the restart: the five percent duty cycle that
            // says high level communication, and the relay permission for a
            // session that still holds both gates. It is the same entry the
            // authorization loop runs, so the two say it once.
            //
            // What follows is the vehicle's. The charge resumes when it starts
            // current demand again, which is the arm that already exists; the
            // relays close on the permission and the supply is energized by
            // the first target after the confirmation.
            //
            // The SLAC wake up the C++ sends beside the transition
            // (`:1021-1023`) is not this path's: `Core`'s resume command owes
            // it on whichever path actually left the paused state, and it reads
            // the state on both sides of this call to decide. So a DC session
            // that leaves here now gets one where before it could not, without
            // a second producer of the fact.
            PathEvent::ResumeRequested => {
                if self.progress.state() != AcState::ChargingPausedEvse {
                    return Vec::new();
                }
                self.enter_prepare_charging()
            }

            // Unreachable, and deliberately inert rather than a fall-through.
            // `Charger::switch_three_phases_while_charging` refuses outright
            // while `hlc_charging_active` (`Charger.cpp:1528-1530`), which this
            // path reports as always true, so the enforced limits handler
            // never accepts a phase change here and never routes one. There
            // are no phases to switch on a DC port in any case.
            PathEvent::SwitchPhases { .. } => Vec::new(),

            // A DC port learns matching completed from the cable check request
            // that follows it, and it has no five percent offer to withdraw, so
            // the setup itself changes nothing. `AcHlc` is the only consumer.
            PathEvent::HlcSessionSetup => Vec::new(),

            // The stop travels as a session decision above this path
            // (`Core::stop`), which reaches here as `on_stop`. Acting on it
            // twice would remove energy on a route that does not also end the
            // transaction.
            PathEvent::StopFromEv => Vec::new(),
        }
    }

    /// What the session progress edges this pass crossed are due.
    ///
    /// `ChargingPausedEV` is never reached on DC. It is the C++
    /// answer to the vehicle opening S2, which is a control pilot fact, and
    /// this path has no reducer to observe one. Ceiling: a DC session announces
    /// no vehicle side pause. Upgrade path: none worth taking; the fact does
    /// not exist on this path. Owner: RsEvseManager.
    fn take_session_duties(&mut self) -> Vec<SessionDuty> {
        self.progress.take_duties()
    }

    /// Energy removal precedes vehicle release, and the isolation monitor and
    /// over voltage monitor are stopped with it.
    ///
    /// The session progress is deliberately not moved here. Every
    /// route that ends a session moves it itself, but the fault route does not:
    /// `Core` answers a fatal error with this call alone. Ceiling: a DC charge
    /// stopped by a fault announces no `StoppingCharging`, though its billing
    /// record still closes on the unplug that follows, and `Charger.cpp:812-833`
    /// leaves the record open across that fault too. Upgrade path: give the
    /// fault route its own entry point that enters `StoppingCharging` and then
    /// `ChargingPausedEvse`, rather than widening this one, which is also
    /// reached from four routes that have already moved the state. Owner:
    /// RsEvseManager.
    fn to_safe_state(&mut self) -> Vec<Effect> {
        // A running sequence stops with the energy. Leaving the stage alone
        // would let the next reading advance it and report success on a cable
        // that was just de-energized, which is the shape of the race the
        // detached C++ thread had.
        let mut effects = Vec::new();
        if !matches!(
            self.cable_check,
            CableCheck::Idle | CableCheck::Done | CableCheck::Abort { .. }
        ) {
            effects.extend(self.enter_abort());
        }
        effects.extend(self.supply_off());
        effects.push(Effect::AllowPowerOn(false));
        if self.imd.is_some() {
            effects.push(Effect::ImdStop);
        }
        if self.over_voltage.is_some() {
            effects.push(Effect::OverVoltageStop);
        }
        // Cancelled last, so the energy removal ordering above is unaffected.
        self.current_demand_active = false;
        effects.extend(self.cancel_enforce_limits());
        effects
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::core::event::{CpEvent, IsolationReading};
    use crate::core::session::{Limits, PwmStart};

    fn config() -> DcConfig {
        DcConfig {
            isolation_voltage_v: 500.0,
            relays_open_voltage_v: 500.0,
            relays_closed_timeout: Duration::from_secs(5),
            imd_measurements: 3,
            ramp_ampere_per_second: 20.0,
            cable_check_current_limit_a: 2.0,
            connector: ConnectorKind::Other,
            internal_over_voltage_duration: Duration::from_millis(0),
            plausibility_max_spread_v: 50.0,
            plausibility_fault_duration: Duration::from_millis(0),
            no_energy_timeout: Duration::from_secs(5),
        }
    }


    /// A `Dc` over the wiring named, which is what `config::resolve` builds
    /// from a deployment's `Wiring`.
    ///
    /// The two monitors reach `Dc::new` as the collaborators themselves rather
    /// than as two adjacent `bool` arguments, so a fixture can no longer claim
    /// a monitor it holds nothing to talk to, and cannot transpose the two.
    fn dc_port(config: DcConfig, imd: bool, over_voltage_monitor: bool) -> Dc {
        dc_port_with(config, imd, over_voltage_monitor, CableCheckOptions::default())
    }

    /// The same, with the optional cable check steps named. They belong to the
    /// isolation monitor, so a port built without one has nowhere to put them
    /// and this helper cannot be asked for that shape.
    fn dc_port_with(
        config: DcConfig,
        imd: bool,
        over_voltage_monitor: bool,
        options: CableCheckOptions,
    ) -> Dc {
        let wiring = Wiring {
            imd,
            over_voltage_monitor,
            ..Wiring::default()
        };
        let mut dc = Dc::new(
            config,
            IsolationMonitor::for_wiring(&wiring, options),
            OverVoltageMonitor::for_wiring(&wiring),
        );
        // The one thing `Core::new` does for a path that a fixture must do for
        // itself: hand it a delegate on a real space. There is no private
        // counter to fall back on, so a fixture that skips this asks for a self
        // test it can never be given a verdict on, and the stage fails.
        dc.adopt_effect_ids(EffectIds::one_space_for_tests().delegate());
        dc
    }

    /// The monitor a wired deployment gets, which is the only source of an
    /// `OverVoltageThresholds` and therefore the only way to write
    /// `Effect::OverVoltageLimits`.
    fn wired_monitor() -> OverVoltageMonitor {
        OverVoltageMonitor::for_wiring(&Wiring {
            over_voltage_monitor: true,
            ..Wiring::default()
        })
        .expect("a wired monitor")
    }

    fn session() -> Session {
        Session::new(
            PwmStart::Nominal,
            Limits {
                max_current_a: 0.0,
                nr_of_phases_available: 3,
            },
        )
    }

    fn now() -> Instant {
        Instant::now()
    }

    fn disable() -> PathEvent {
        PathEvent::Disable
    }

    fn enable() -> PathEvent {
        PathEvent::Enable
    }

    /// `power_supply_DC_charging_phase`. The three ISO 15118 openings each
    /// assign it and the supply going off returns it to `Other`, so every mode
    /// change reports what it is for.
    ///
    /// It matters because a driver may act on it: `types/power_supply_DC.yaml`
    /// defines `Other` as "switching it off or any other internal testing not
    /// related to real charging", so a supply told to export under `Other` is
    /// entitled to decline. The cable check half is pinned by
    /// `the_full_cable_check_sequence_runs_in_the_ported_order`.
    #[test]
    fn each_charging_phase_rides_on_the_mode_change_it_opened() {
        let phase_of = |effects: &[Effect]| {
            effects.iter().find_map(|effect| match effect {
                Effect::SetSupplyMode { phase, .. } => Some(*phase),
                _ => None,
            })
        };

        // Precharge assigns the phase and actuates nothing itself, so the next
        // mode change is what carries it.
        let mut dc = dc_port(config(), true, false);
        let opened = dc.on_path_event(&session(), PathEvent::PreChargeStarted, now());
        assert!(opened.is_empty(), "precharge actuates nothing, got {opened:?}");
        assert_eq!(
            phase_of(&dc.set_setpoint(400.0, 10.0, SupplyMode::Export)),
            Some(ChargingPhase::PreCharge)
        );

        // The supply going off returns it, so a mode change that belongs to no
        // phase reports none.
        assert_eq!(phase_of(&dc.supply_off()), None);
        assert_eq!(
            phase_of(&dc.set_setpoint(400.0, 10.0, SupplyMode::Export)),
            Some(ChargingPhase::Other)
        );

        // Current demand assigns `Charging` before it applies the target, so
        // the energize it drives carries it.
        let mut dc = dc_port(config(), true, false);
        dc.present_voltage_v = Some(400.0);
        dc.raw_target_voltage_v = 400.0;
        dc.raw_target_current_a = 10.0;
        relays_confirmed_closed(&mut dc, &session());
        let started = dc.on_path_event(&session(), PathEvent::CurrentDemandStarted, now());
        assert_eq!(phase_of(&started), Some(ChargingPhase::Charging), "got {started:?}");
    }

    /// `EvseManager.cpp:827-832`: the vehicle asking for the DC contactor to
    /// open does not open it. The relays stay closed and are opened by the CP
    /// C to B edge; what this removes is the energy behind them.
    #[test]
    fn the_vehicle_asking_to_open_the_dc_contactor_removes_energy_and_stops_the_monitor() {
        let mut dc = dc_port(config(), true, false);
        dc.set_setpoint(400.0, 10.0, SupplyMode::Export);

        let effects = dc.on_path_event(&session(), PathEvent::OpenContactorDc, now());

        assert_eq!(
            effects,
            vec![Effect::SupplyOff, Effect::ImdStop],
            "the supply goes off and the isolation monitor stops, in that order"
        );
        // The effect list alone does not pin this: switching the supply off has
        // to invalidate the applied setpoint cache, or the next identical target
        // is suppressed as unchanged and never written. A literal
        // `vec![Effect::SupplyOff]` in place of `self.supply_off()` produces the
        // same list and leaves a stale setpoint standing for the rest of the
        // session.
        assert_eq!(
            dc.set_setpoint(400.0, 10.0, SupplyMode::Export),
            vec![
                Effect::SetSupplyMode { mode: SupplyMode::Export, phase: ChargingPhase::Other },
                Effect::SetSupplySetpoint {
                    mode: SupplyMode::Export,
                    voltage_v: 400.0,
                    current_a: 10.0
                }
            ],
            "the same target must be written again after the supply went off"
        );
    }

    /// The C++ calls `imd_stop()` unconditionally, and `imd_stop` itself is the
    /// guard: `EvseManager.cpp` only reaches the monitor through
    /// `r_imd`, which is empty when none is wired.
    #[test]
    fn a_port_without_an_isolation_monitor_only_removes_energy() {
        let mut dc = dc_port(config(), false, false);

        let effects = dc.on_path_event(&session(), PathEvent::OpenContactorDc, now());

        assert_eq!(effects, vec![Effect::SupplyOff]);
    }

    /// The cell the two optional monitors' cross product was missing.
    ///
    /// `Dc` fixtures drove three of the four corners: both wired, neither
    /// wired, and an isolation monitor alone. A port with an **over voltage
    /// monitor and no isolation monitor** was never driven anywhere, and it is
    /// a real deployment: the two are separate optional requirements and
    /// nothing in the manifest couples them.
    ///
    /// It matters because the cable check sequence is the one thing the
    /// isolation monitor's absence short circuits, and the over voltage
    /// monitor's own three commands hang off stages either side of it. So the
    /// question this answers is whether the monitor still gets configured,
    /// started and stopped on a port whose cable check never runs.
    #[test]
    fn an_over_voltage_monitor_works_on_a_port_with_no_isolation_monitor() {
        let mut dc = dc_port(deriving_config(), false, true);
        let s = session();

        // Configured. The vehicle's maximum is the only site that pushes
        // thresholds and it is not gated on the isolation monitor.
        let configured = dc.on_path_event(
            &s,
            PathEvent::DcEvMaximumLimits(EvMaximumLimits {
                maximum_current_a: Some(200.0),
                maximum_voltage_v: Some(700.0),
            }),
            now(),
        );
        assert!(
            configured
                .iter()
                .any(|effect| matches!(effect, Effect::OverVoltageLimits(_))),
            "a monitor nobody configures is silently inert: {configured:?}"
        );

        // The cable check short circuits, and says so rather than reporting a
        // measured success.
        let skipped = dc.begin_cable_check();
        assert_eq!(
            skipped,
            vec![
                Effect::HlcUpdate(HlcUpdate::IsolationStatus(IsolationStatus::NoImd)),
                Effect::HlcUpdate(HlcUpdate::CableCheckFinished(true)),
            ]
        );
        assert!(
            !skipped.contains(&Effect::ImdStart),
            "no monitor to start: {skipped:?}"
        );

        // Started, on the stage after the check that did not run.
        let started = dc.on_path_event(&s, PathEvent::CurrentDemandStarted, now());
        assert!(
            started.contains(&Effect::OverVoltageStart),
            "the monitor has to run over the delivery: {started:?}"
        );

        // Stopped, and the isolation monitor is not stopped with it.
        let finished = dc.on_path_event(&s, PathEvent::CurrentDemandFinished, now());
        assert!(
            finished.contains(&Effect::OverVoltageStop),
            "{finished:?}"
        );
        assert!(
            !finished.contains(&Effect::ImdStop),
            "there is no isolation monitor to stop: {finished:?}"
        );

        // And safe state stops it too, still without an isolation monitor stop.
        let safe = dc.to_safe_state();
        assert!(safe.contains(&Effect::OverVoltageStop), "{safe:?}");
        assert!(
            !safe.contains(&Effect::ImdStop),
            "there is no isolation monitor to stop: {safe:?}"
        );
    }

    /// The mirror cell: an isolation monitor and no over voltage monitor must
    /// configure, start and stop nothing on the monitor it does not have.
    ///
    /// Driven by many fixtures already; asserted once here so the four corners
    /// of the cross product are all named in one place.
    #[test]
    fn no_over_voltage_monitor_means_no_over_voltage_effect_anywhere() {
        let mut dc = dc_port(deriving_config(), true, false);
        let s = session();

        let mut all = dc.on_path_event(
            &s,
            PathEvent::DcEvMaximumLimits(EvMaximumLimits {
                maximum_current_a: Some(200.0),
                maximum_voltage_v: Some(700.0),
            }),
            now(),
        );
        all.extend(dc.begin_cable_check());
        all.extend(dc.on_path_event(&s, PathEvent::CurrentDemandStarted, now()));
        all.extend(dc.on_path_event(&s, PathEvent::CurrentDemandFinished, now()));
        all.extend(dc.to_safe_state());

        for effect in &all {
            assert!(
                !matches!(
                    effect,
                    Effect::OverVoltageLimits(_) | Effect::OverVoltageStart | Effect::OverVoltageStop
                ),
                "a port with no over voltage monitor addressed one: {effect:?}"
            );
        }
    }

    #[test]
    fn a_winning_disable_removes_energy_before_it_stops_the_dc_board() {
        let mut dc = dc_port(config(), true, true);

        let effects = dc.on_path_event(&session(), disable(), now());

        let supply_off = effects
            .iter()
            .position(|effect| *effect == Effect::SupplyOff)
            .expect("energy must be withdrawn");
        let cp = effects
            .iter()
            .position(|effect| *effect == Effect::SetCpState(CpState::F))
            .expect("a disabled port signals it is unavailable");
        let stop = effects
            .iter()
            .position(|effect| *effect == Effect::BspEnable(false))
            .expect("a disabled port is stopped at the board");
        assert!(supply_off < cp && cp < stop, "got {effects:?}");
        assert!(
            !effects.contains(&Effect::UnlockConnector),
            "an availability change never releases the vehicle, got {effects:?}"
        );
    }

    #[test]
    fn a_winning_disable_abandons_a_cable_check_under_way() {
        let mut dc = dc_port(config(), true, false);
        dc.begin_cable_check();

        dc.on_path_event(&session(), disable(), now());

        assert_eq!(
            dc.cable_check_stage(),
            CableCheck::Abort { reported: false }
        );
    }

    #[test]
    fn a_winning_enable_returns_the_dc_port_to_service() {
        let mut dc = dc_port(config(), true, true);
        dc.on_path_event(&session(), disable(), now());

        let effects = dc.on_path_event(&session(), enable(), now());

        assert_eq!(
            effects,
            vec![Effect::BspEnable(true), Effect::SetCpState(CpState::X1)],
            "got {effects:?}"
        );
    }

    #[test]
    fn an_enable_with_no_disable_outstanding_leaves_the_dc_port_alone() {
        // Nothing took the port out of service, so nothing puts it back. An
        // unconditional enable would start a board a fault had stopped.
        let mut dc = dc_port(config(), true, true);

        let effects = dc.on_path_event(&session(), enable(), now());

        assert!(effects.is_empty(), "got {effects:?}");
    }

    #[test]
    fn a_command_that_is_not_an_availability_change_leaves_the_dc_port_alone() {
        let mut dc = dc_port(config(), true, true);

        let effects = dc.on_path_event(&session(), PathEvent::PauseRequested, now());

        assert!(effects.is_empty(), "got {effects:?}");
    }

    /// Plug and charge on DC cannot start without the pilot, and the pilot was
    /// raised only after an authorization that plug and charge could not
    /// obtain: the contract it authorizes with arrives over a data link the
    /// vehicle establishes on the five percent offer. So the wait could not
    /// end, and only an external authorization broke it.
    ///
    /// `Charger.cpp:302-315` raises it from the `WaitingForAuthentication`
    /// entry instead, on the plug in.
    #[test]
    fn a_dc_plug_in_raises_the_pilot_before_any_authorization() {
        let mut dc = dc_port(config(), true, false);
        dc.on_startup();

        let plugged_in = dc.on_session_start(&session(), now());

        assert_eq!(
            dc.progress.state(),
            AcState::WaitingForAuthentication,
            "the control"
        );
        assert!(
            plugged_in.contains(&Effect::PwmOn(PWM_5_PERCENT)),
            "the vehicle has nothing to establish a data link on: {plugged_in:?}"
        );
    }

    /// The offer is not re-sent while it stands, which every C++ site says by
    /// being `update_pwm_now_if_changed`. A port re-sending it writes the same
    /// duty cycle to the board on every entry a session crosses.
    #[test]
    fn a_standing_five_percent_offer_is_not_raised_again() {
        let mut dc = dc_port(config(), true, false);
        dc.on_startup();
        dc.on_session_start(&session(), now());

        let authorized = dc.on_authorized(&session(), now());

        assert_eq!(dc.progress.state(), AcState::PrepareCharging);
        assert!(
            !authorized.contains(&Effect::PwmOn(PWM_5_PERCENT)),
            "{authorized:?}"
        );
    }

    /// The pilot is dropped by a disable and by the enable that follows it, so
    /// the offer has to be raised again for the next session. Kept standing,
    /// the flag would suppress the one offer a re-enabled port owes.
    #[test]
    fn a_disable_and_enable_leaves_the_next_session_its_own_offer() {
        let mut dc = dc_port(config(), true, false);
        dc.on_startup();
        dc.on_session_start(&session(), now());
        assert!(dc.five_percent_offered, "the control");

        dc.on_path_event(&session(), disable(), now());
        assert!(
            !dc.five_percent_offered,
            "the disable ends the session, and the offer goes with it"
        );

        dc.on_path_event(&session(), enable(), now());
        dc.on_bsp(
            &session(),
            &BspEvent::Cp(CpEvent::A),
            CpEdges::default(),
            now(),
        );
        let again = dc.on_session_start(&session(), now());

        assert!(
            again.contains(&Effect::PwmOn(PWM_5_PERCENT)),
            "{again:?}"
        );
    }

    /// A paused DC session and the request that restarts it.
    ///
    /// The C++ `ChargingPausedEVSE` arm leaves for `PrepareCharging` once no
    /// reason to pause is left, and that entry re-offers the five percent duty
    /// cycle and re-permits the relays. Before this the request reached a path
    /// that did nothing with it, so the session reported a successful resume
    /// and then sat paused until the cable came out.
    fn paused_dc_charge() -> (Dc, Session) {
        let mut dc = charging_dc();
        let session = charging_session(200.0);
        dc.progress.enter(AcState::PrepareCharging);
        // Both contactor gates, as a real session holds them: the stack grants
        // its half and the vehicle asking for power grants the other.
        dc.on_path_event(&session, PathEvent::AllowCloseContactor(true), now());
        dc.on_bsp(
            &session,
            &BspEvent::Cp(CpEvent::C),
            CpEdges {
                requested_power: true,
                ..CpEdges::default()
            },
            now(),
        );
        assert!(dc.may_close_contactor(), "the fixture holds both gates");
        relays_confirmed_closed(&mut dc, &session);
        dc.on_path_event(&session, PathEvent::CurrentDemandStarted, now());
        dc.set_ev_target(&session, 400.0, 40.0, now());
        assert_eq!(dc.progress.state(), AcState::Charging, "the fixture charges");

        dc.on_path_event(&session, PathEvent::PauseRequested, now());
        // The board opens the relays behind the withdrawn permission, which is
        // what completes the stop.
        dc.on_bsp(
            &session,
            &BspEvent::Cp(CpEvent::PowerOff),
            CpEdges::default(),
            now(),
        );
        assert_eq!(dc.progress.state(), AcState::ChargingPausedEvse);
        (dc, session)
    }

    #[test]
    fn a_paused_dc_session_resumes_on_the_request() {
        let (mut dc, session) = paused_dc_charge();

        let resumed = dc.on_path_event(&session, PathEvent::ResumeRequested, now());

        assert_eq!(dc.progress.state(), AcState::PrepareCharging);
        assert!(
            resumed.contains(&Effect::PwmOn(PWM_5_PERCENT)),
            "the pilot says high level communication again: {resumed:?}"
        );
        assert!(
            resumed.contains(&Effect::AllowPowerOn(true)),
            "and the relays are permitted again: {resumed:?}"
        );
    }

    /// The restart itself is the vehicle's. What the resume owes is a state the
    /// next current demand can leave, and the supply is energized by the first
    /// target after the relays confirm.
    #[test]
    fn a_resumed_dc_session_charges_again_on_the_next_current_demand() {
        let (mut dc, session) = paused_dc_charge();
        dc.on_path_event(&session, PathEvent::ResumeRequested, now());

        dc.on_bsp(
            &session,
            &BspEvent::Cp(CpEvent::PowerOn),
            CpEdges::default(),
            now(),
        );
        dc.on_path_event(&session, PathEvent::CurrentDemandStarted, now());
        assert_eq!(dc.progress.state(), AcState::Charging);

        let energized = dc.set_ev_target(&session, 400.0, 40.0, now());

        assert_eq!(dc.supply_mode(), SupplyMode::Export, "{energized:?}");
    }

    #[test]
    fn a_resume_on_a_dc_session_that_is_not_paused_changes_nothing() {
        let mut dc = charging_dc();
        let session = charging_session(200.0);
        dc.progress.enter(AcState::PrepareCharging);
        dc.on_path_event(&session, PathEvent::CurrentDemandStarted, now());
        assert_eq!(dc.progress.state(), AcState::Charging, "the control");

        let resumed = dc.on_path_event(&session, PathEvent::ResumeRequested, now());

        assert!(resumed.is_empty(), "{resumed:?}");
        assert_eq!(dc.progress.state(), AcState::Charging);
    }

    /// A DC charge whose EVSE limits go empty.
    ///
    /// `Charger.cpp:836-866`: every DC session is a high level one, so the
    /// charge is given `hlc_charge_loop_without_energy_timeout_s` to see limits
    /// arrive before it is stopped. The limit set reaching the path changed
    /// nothing at all before this.
    fn charging_dc_with_limits(timeout: Duration) -> (Dc, Session) {
        let mut dc = dc_port(
            DcConfig {
                no_energy_timeout: timeout,
                ..deriving_config()
            },
            true,
            true,
        );
        dc.set_ev_maximum_limits(EvMaximumLimits {
            maximum_voltage_v: Some(900.0),
            ..EvMaximumLimits::default()
        });
        dc.set_evse_max_export_voltage_v(950.0);
        let (maximum, minimum) = roomy_hlc_limits();
        dc.set_evse_hlc_limits(maximum, minimum);
        let session = charging_session(200.0);
        dc.progress.enter(AcState::PrepareCharging);
        relays_confirmed_closed(&mut dc, &session);
        dc.on_path_event(&session, PathEvent::CurrentDemandStarted, now());
        dc.set_ev_target(&session, 400.0, 40.0, now());
        assert_eq!(dc.progress.state(), AcState::Charging, "the fixture charges");
        assert_eq!(dc.supply_mode(), SupplyMode::Export, "and is energized");
        (dc, session)
    }

    fn enforced_limits(current_a: f64, power_w: f64, reapply_target: bool) -> PathEvent {
        PathEvent::DcEnforcedLimits {
            maximum: MaximumLimits {
                maximum_current_a: current_a,
                maximum_voltage_v: 950.0,
                maximum_power_w: power_w,
                maximum_discharge_current_a: None,
                maximum_discharge_power_w: None,
            },
            minimum: MinimumLimits::default(),
            exporting_to_grid: false,
            reapply_target,
        }
    }

    /// `power_available` on DC tests both figures, so an empty set is one the
    /// C++ answers false for.
    fn empty_limits(reapply_target: bool) -> PathEvent {
        enforced_limits(0.0, 0.0, reapply_target)
    }

    fn ample_limits() -> PathEvent {
        enforced_limits(400.0, 500_000.0, false)
    }

    fn arms_timer(effects: &[Effect], timer: TimerId) -> bool {
        effects
            .iter()
            .any(|e| matches!(e, Effect::StartTimer { id, .. } if *id == timer))
    }

    #[test]
    fn a_dc_charge_with_no_limits_left_is_given_its_configured_run() {
        let (mut dc, session) = charging_dc_with_limits(Duration::from_secs(5));

        let waiting = dc.on_path_event(&session, empty_limits(false), now());

        assert_eq!(
            dc.progress.state(),
            AcState::Charging,
            "the charge runs on while the deadline does"
        );
        assert!(arms_timer(&waiting, TIMER_NO_ENERGY), "{waiting:?}");

        let again = dc.on_path_event(&session, empty_limits(false), now());
        assert!(
            !arms_timer(&again, TIMER_NO_ENERGY),
            "a second empty set does not restart the deadline: {again:?}"
        );

        let expired = dc.on_timer(&session, TIMER_NO_ENERGY, now());

        assert_eq!(dc.progress.state(), AcState::StoppingCharging);
        assert!(
            expired.contains(&Effect::SupplyOff) && expired.contains(&Effect::AllowPowerOn(false)),
            "the energy goes before the vehicle is released: {expired:?}"
        );

        dc.on_bsp(
            &session,
            &BspEvent::Cp(CpEvent::PowerOff),
            CpEdges::default(),
            now(),
        );
        assert_eq!(
            dc.progress.state(),
            AcState::ChargingPausedEvse,
            "the relays opened, so the stop settles as a pause"
        );
    }

    /// `power_available` on DC is a conjunction (`Charger.cpp:2128-2129`), so
    /// either figure at zero is a charge with nothing behind it. An energy
    /// manager can zero one and not the other, and a disjunction here would let
    /// such a set charge on.
    #[test]
    fn either_limit_at_zero_is_a_charge_with_no_energy() {
        for (current_a, power_w) in [(0.0, 500_000.0), (400.0, 0.0), (0.0, 0.0)] {
            let (mut dc, session) = charging_dc_with_limits(Duration::ZERO);

            let stopping =
                dc.on_path_event(&session, enforced_limits(current_a, power_w, false), now());

            assert_eq!(
                dc.progress.state(),
                AcState::StoppingCharging,
                "{current_a} A and {power_w} W must stop the charge: {stopping:?}"
            );
        }

        // And the control: both figures above zero keep it charging.
        let (mut dc, session) = charging_dc_with_limits(Duration::ZERO);
        dc.on_path_event(&session, enforced_limits(1.0, 1.0, false), now());
        assert_eq!(dc.progress.state(), AcState::Charging);
    }

    #[test]
    fn a_zero_deadline_stops_a_dc_charge_at_once() {
        let (mut dc, session) = charging_dc_with_limits(Duration::ZERO);

        let stopping = dc.on_path_event(&session, empty_limits(false), now());

        assert_eq!(dc.progress.state(), AcState::StoppingCharging);
        assert!(!arms_timer(&stopping, TIMER_NO_ENERGY), "{stopping:?}");
        assert!(stopping.contains(&Effect::SupplyOff), "{stopping:?}");
    }

    /// Limits arriving cancel the deadline, and limits arriving after the stop
    /// resume the charge: on DC that is the same preparation entry a resume
    /// request runs.
    #[test]
    fn limits_arriving_cancel_the_deadline_and_resume_a_charge_they_stopped() {
        let (mut dc, session) = charging_dc_with_limits(Duration::from_secs(5));
        dc.on_path_event(&session, empty_limits(false), now());

        let restored = dc.on_path_event(&session, ample_limits(), now());
        assert!(
            restored
                .iter()
                .any(|e| matches!(e, Effect::CancelTimer { id } if *id == TIMER_NO_ENERGY)),
            "{restored:?}"
        );
        assert_eq!(dc.progress.state(), AcState::Charging);

        let outlived = dc.on_timer(&session, TIMER_NO_ENERGY, now());
        assert!(
            outlived.is_empty(),
            "a deadline that outlived its cause decides nothing: {outlived:?}"
        );

        // Now take the limits away for good and let the stop complete.
        dc.on_path_event(&session, empty_limits(false), now());
        dc.on_timer(&session, TIMER_NO_ENERGY, now());
        dc.on_bsp(
            &session,
            &BspEvent::Cp(CpEvent::PowerOff),
            CpEdges::default(),
            now(),
        );
        assert_eq!(dc.progress.state(), AcState::ChargingPausedEvse, "the control");

        let resumed = dc.on_path_event(&session, ample_limits(), now());

        assert_eq!(dc.progress.state(), AcState::PrepareCharging);
        assert!(resumed.contains(&Effect::PwmOn(PWM_5_PERCENT)), "{resumed:?}");
    }

    /// The stop switches the supply off with the relays still reported closed,
    /// so a target re-applied in the same pass would energize it again. The
    /// availability test runs first and the re-apply is skipped, which is the
    /// order the C++ arm has.
    #[test]
    fn a_stop_for_no_limits_does_not_re_apply_the_target_beside_it() {
        let (mut dc, session) = charging_dc_with_limits(Duration::ZERO);

        let stopping = dc.on_path_event(&session, empty_limits(true), now());

        assert_eq!(dc.supply_mode(), SupplyMode::Off);
        assert!(
            !stopping.iter().any(|effect| matches!(
                effect,
                Effect::SetSupplySetpoint { .. } | Effect::SetSupplyMode { .. }
            )),
            "nothing re-energizes the supply the stop just switched off: {stopping:?}"
        );
    }

    #[test]
    fn the_cable_check_request_starts_the_stage() {
        // `EvseManager.cpp:563-567` subscribes `start_cable_check` and runs the
        // sequence from it. Nothing else starts it, so a request that does not
        // reach `begin_cable_check` leaves the whole stage unreachable in a
        // real deployment.
        let mut dc = dc_port(config(), true, false);

        let effects = dc.on_path_event(&session(), PathEvent::CableCheckRequired, now());

        assert_ne!(
            dc.cable_check_stage(),
            CableCheck::Idle,
            "the request must start the sequence, got {effects:?}"
        );
        assert!(!effects.is_empty(), "got {effects:?}");
    }

    #[test]
    fn cable_check_is_reported_complete_without_an_isolation_monitor() {
        let mut dc = dc_port(config(), false, false);
        let effects = dc.begin_cable_check();

        assert_eq!(dc.cable_check_stage(), CableCheck::Done);
        assert_eq!(
            effects,
            vec![
                Effect::HlcUpdate(HlcUpdate::IsolationStatus(IsolationStatus::NoImd)),
                Effect::HlcUpdate(HlcUpdate::CableCheckFinished(true)),
            ]
        );
    }

    #[test]
    fn cable_check_waits_for_a_de_energized_cable_before_energizing() {
        let mut dc = dc_port(config(), true, false);
        dc.present_voltage_v = Some(400.0);

        let effects = dc.begin_cable_check();

        assert_eq!(dc.cable_check_stage(), CableCheck::AwaitSafeVoltage);
        assert_eq!(
            effects,
            vec![Effect::StartTimer {
                id: TIMER_CABLE_CHECK,
                after: WAIT_VOLTAGE_TIMEOUT
            }],
            "the wait is bounded and nothing is energized"
        );
    }

    #[test]
    fn cable_check_proceeds_to_the_contactor_wait_once_below_the_safe_threshold() {
        // The relays open self test is off by default, so the sequence goes
        // straight to waiting for the confirmed contactor closed fact.
        let mut dc = dc_port(config(), true, false);
        dc.present_voltage_v = Some(400.0);
        dc.begin_cable_check();

        let effects = dc.on_path_event(
            &session(),
            PathEvent::SupplyVoltage { voltage_v: 10.0 },
            now(),
        );

        assert_eq!(dc.cable_check_stage(), CableCheck::AwaitContactorClosed);
        assert_eq!(
            effects,
            vec![
                Effect::CancelTimer {
                    id: TIMER_CABLE_CHECK
                },
                Effect::StartTimer {
                    id: TIMER_CONTACTOR_CONFIRM,
                    after: Duration::from_secs(5)
                }
            ]
        );
    }

    #[test]
    fn cable_check_energizes_at_the_isolation_voltage_with_capped_current() {
        let dc = dc_port(config(), true, false);

        assert_eq!(dc.cable_check_voltage_v(), 500.0);
        assert_eq!(
            dc.cable_check_current_a(),
            2.0,
            "cable check current is the ported CABLECHECK_CURRENT_LIMIT"
        );
    }

    #[test]
    fn contactor_needs_both_gates() {
        let mut dc = dc_port(config(), true, false);
        assert!(!dc.may_close_contactor());
        dc.hlc_allows_close = true;
        assert!(!dc.may_close_contactor(), "hlc gate alone is insufficient");
        dc.iec_allows_close = true;
        assert!(dc.may_close_contactor());
    }

    /// `EvseManager.cpp:395-403` subscribes `ac_close_contactor` and
    /// `ac_open_contactor` straight onto `set_hlc_allow_close_contactor`, with
    /// no charge mode branch, and `Charger.cpp:699-703` reads the flag on the
    /// DC side: `if (hlc_allow_close_contactor and iec_allow_close_contactor)`.
    /// The names say AC, the writer and the reader do not.
    #[test]
    fn the_high_level_stack_may_withdraw_the_dc_contactor_permission() {
        let mut h = Harness::new(all_options());
        h.dc.iec_allows_close = true;
        h.begin(400.0);
        h.voltage(10.0);
        assert!(h.dc.may_close_contactor(), "the cable check granted it");

        h.dc.on_path_event(&h.session, PathEvent::AllowCloseContactor(false), now());

        assert!(!h.dc.may_close_contactor());

        h.dc.on_path_event(&h.session, PathEvent::AllowCloseContactor(true), now());

        assert!(h.dc.may_close_contactor(), "and may grant it again");
    }

    /// `Charger::dlink_pause` (`Charger.cpp:2055-2061`),
    /// `Charger::dlink_terminate` (`:2063-2069`) and `Charger::dlink_error`
    /// (`:2070-2073`) each open with `hlc_allow_close_contactor = false`, none
    /// of them behind a charge mode branch. So all three withdraw the
    /// permission on a DC port too.
    #[test]
    fn every_data_link_request_withdraws_the_dc_contactor_permission() {
        for request in [
            DataLinkRequest::Pause,
            DataLinkRequest::Terminate,
            DataLinkRequest::Error,
        ] {
            let mut h = Harness::new(all_options());
            h.dc.iec_allows_close = true;
            h.begin(400.0);
            h.voltage(10.0);
            assert!(h.dc.may_close_contactor(), "{request:?}");

            h.dc.on_path_event(&h.session, PathEvent::DataLink(request), now());

            assert!(!h.dc.may_close_contactor(), "{request:?}");
        }
    }

    /// The pilot goes with the permission. `dlink_pause` and `dlink_terminate`
    /// issue an unconditional `cp_state_X1()` beside their clear, and on a DC
    /// port `dlink_error` reaches one too, because the branch that issues it is
    /// gated on a flag DC always has set.
    ///
    /// Before this the port withdrew the permission and left the five percent
    /// offer standing, so a vehicle that had asked to pause was still being
    /// invited to talk.
    #[test]
    fn every_data_link_request_withdraws_the_dc_pilot() {
        for request in [
            DataLinkRequest::Pause,
            DataLinkRequest::Terminate,
            DataLinkRequest::Error,
        ] {
            let mut dc = dc_port(config(), true, false);
            dc.on_startup();
            let mut live = session();
            live.transaction_active = true;
            dc.on_session_start(&live, now());
            dc.on_authorized(&live, now());
            assert!(dc.five_percent_offered, "the control for {request:?}");

            let withdrawn = dc.on_path_event(&live, PathEvent::DataLink(request), now());

            assert!(
                withdrawn.contains(&Effect::SetCpState(CpState::X1)),
                "{request:?}: {withdrawn:?}"
            );
            assert!(!dc.five_percent_offered, "{request:?}");
            assert!(
                !withdrawn.contains(&Effect::SupplyOff),
                "{request:?} on a live session takes no energy with it: {withdrawn:?}"
            );
        }
    }

    /// `Charger.cpp:2227-2242`. A link error arriving while the session is
    /// stopping for good is the high level session shutting down rather than a
    /// fault to recover from, so it takes the energy and the vehicle's
    /// permission with it.
    #[test]
    fn a_link_error_while_the_session_stops_for_good_takes_the_energy_with_it() {
        let mut dc = dc_port(config(), true, false);
        dc.on_startup();
        let mut live = session();
        live.transaction_active = true;
        dc.on_session_start(&live, now());
        dc.on_authorized(&live, now());
        // The stop is under way and the transaction is already closed, which is
        // one of the three facts that make it final.
        dc.begin_stopping(StoppingOutcome::Finished);
        live.transaction_active = false;

        let stopping = dc.on_path_event(
            &live,
            PathEvent::DataLink(DataLinkRequest::Error),
            now(),
        );

        assert!(
            stopping.contains(&Effect::SupplyOff)
                && stopping.contains(&Effect::AllowPowerOn(false)),
            "{stopping:?}"
        );
    }

    /// The branch reads the request as well as the three session facts, and
    /// both halves of that are load bearing.
    ///
    /// An error during a stop the session could still come back from removes
    /// no energy, because none of the three facts holds. And a **pause** in the
    /// state where an error would remove it removes none either: `dlink_pause`
    /// has no such branch at all, so reading the state without the request
    /// would de-energize a port on the vehicle's request to pause.
    #[test]
    fn only_a_link_error_and_only_a_final_stop_removes_the_energy() {
        for (request, outcome, transaction_active) in [
            (DataLinkRequest::Error, StoppingOutcome::PausedByEvse, true),
            (DataLinkRequest::Pause, StoppingOutcome::Finished, false),
            (DataLinkRequest::Terminate, StoppingOutcome::Finished, false),
        ] {
            let mut dc = dc_port(config(), true, false);
            dc.on_startup();
            let mut live = session();
            live.transaction_active = true;
            dc.on_session_start(&live, now());
            dc.on_authorized(&live, now());
            dc.begin_stopping(outcome);
            live.transaction_active = transaction_active;

            let stopping = dc.on_path_event(&live, PathEvent::DataLink(request), now());

            assert!(
                !stopping.contains(&Effect::SupplyOff),
                "{request:?} in {outcome:?}: {stopping:?}"
            );
        }
    }

    /// A DC port keeps retrying high level communication after a link error,
    /// and that is deliberate rather than an unported fallback.
    ///
    /// `Charger::dlink_error` does set `hlc_failed` on a DC port, but nothing
    /// on the DC side reads it: the entry into `WaitingForAuthentication`
    /// assigns `hlc_use_5percent_current_session` unconditionally in its DC
    /// branch and never derives `ac_hlc_enabled_current_session` there, and
    /// the `PrepareCharging` duty cycle choice that reads the latch is inside
    /// `if (config_context.charge_mode == ChargeMode::AC)`. So `main` has no DC
    /// fallback either, and this port must not grow one for symmetry with
    /// `AcHlc`.
    #[test]
    fn a_dc_port_keeps_retrying_high_level_communication_after_a_link_error() {
        let mut h = Harness::new(all_options());
        h.dc.iec_allows_close = true;
        h.begin(400.0);
        h.voltage(10.0);
        assert!(h.dc.may_close_contactor(), "the cable check granted it");

        h.dc.on_path_event(
            &h.session,
            PathEvent::DataLink(DataLinkRequest::Error),
            now(),
        );
        assert!(
            !h.dc.may_close_contactor(),
            "the permission goes with the link"
        );

        // The stack restarts matching and grants it again inside the same plug
        // in. Nothing on this path remembers that the link failed once.
        h.dc.on_path_event(
            &h.session,
            PathEvent::AllowCloseContactor(true),
            now(),
        );

        assert!(
            h.dc.may_close_contactor(),
            "no latch stands in the way of the retry"
        );
    }

    #[test]
    fn switching_the_supply_off_invalidates_the_applied_setpoint() {
        let mut dc = dc_port(config(), true, false);
        dc.set_setpoint(400.0, 10.0, SupplyMode::Export);
        assert_eq!(dc.applied_setpoint, Some((400.0, 10.0, SupplyMode::Export)));

        dc.supply_off();
        assert_eq!(dc.applied_setpoint, None);

        // The same setpoint must be re-sent after an off, not suppressed.
        let effects = dc.set_setpoint(400.0, 10.0, SupplyMode::Export);
        assert!(effects.contains(&Effect::SetSupplySetpoint {
            mode: SupplyMode::Export,
            voltage_v: 400.0,
            current_a: 10.0
        }));
    }

    #[test]
    fn identical_zero_setpoints_in_opposite_directions_reach_both_apis() {
        use crate::boundary::supply::tests::RecordingSupply;

        // Exercise both emission paths and both directions. Numeric equality
        // must suppress only a repeated write to the same API.
        for write in [Dc::set_setpoint, Dc::energize] {
            let mut dc = dc_port(config(), true, false);
            let supply = RecordingSupply::default();
            for mode in [SupplyMode::Import, SupplyMode::Export, SupplyMode::Import] {
                supply.apply_setpoints(write(&mut dc, 400.0, 0.0, mode));
                supply.apply_setpoints(write(&mut dc, 400.0, 0.0, mode));
            }
            assert_eq!(
                supply.calls(),
                vec![
                    (SupplyMode::Import, 0.0, 400.0),
                    (SupplyMode::Export, 0.0, 400.0),
                    (SupplyMode::Import, 0.0, 400.0),
                ]
            );
        }
    }

    #[test]
    fn supply_mode_precedes_the_setpoint() {
        let mut dc = dc_port(config(), true, false);
        let effects = dc.set_setpoint(400.0, 10.0, SupplyMode::Import);

        let mode_at = effects
            .iter()
            .position(|e| matches!(e, Effect::SetSupplyMode { .. }))
            .expect("mode effect");
        let setpoint_at = effects
            .iter()
            .position(|e| matches!(e, Effect::SetSupplySetpoint { .. }))
            .expect("setpoint effect");

        assert!(mode_at < setpoint_at, "mode must be applied first");
    }

    #[test]
    fn an_unplug_leaves_a_disabled_dc_port_out_of_service() {
        // The unplug is a session fact, not an availability one. It must not
        // return the port to service and must not signal the availability the
        // disable withdrew. `Charger.cpp:237-238` sends the Idle entry straight
        // back to Disabled for the same reason.
        let mut dc = dc_port(config(), true, true);
        dc.on_path_event(&session(), disable(), now());

        let effects = dc.on_bsp(&session(), &BspEvent::Cp(CpEvent::Disconnected), CpEdges::default(), now());

        assert!(
            dc.disable_requested,
            "the outstanding request must survive the unplug"
        );
        assert!(
            !effects
                .iter()
                .any(|effect| matches!(effect, Effect::SetCpState(_))),
            "the port stays on the state F the disable set, got {effects:?}"
        );
        assert!(
            !effects.contains(&Effect::BspEnable(true)),
            "got {effects:?}"
        );

        let returning = dc.on_path_event(&session(), enable(), now());

        assert_eq!(
            returning,
            vec![Effect::BspEnable(true), Effect::SetCpState(CpState::X1)],
            "the enable is what returns the port to service"
        );
    }

    #[test]
    fn unplug_during_cable_check_aborts_without_a_race() {
        let mut dc = dc_port(config(), true, false);
        dc.present_voltage_v = Some(10.0);
        dc.begin_cable_check();
        dc.cable_check = CableCheck::Sampling { taken: 1 };

        let effects = dc.on_bsp(&session(), &BspEvent::Cp(CpEvent::Disconnected), CpEdges::default(), now());

        assert!(matches!(
            dc.cable_check_stage(),
            CableCheck::Abort { reported: false }
        ));
        assert!(effects.contains(&Effect::SupplyOff));
        assert!(effects.contains(&Effect::AllowPowerOn(false)));
    }

    #[test]
    fn failure_is_reported_only_once_the_cable_is_de_energized() {
        let mut dc = dc_port(config(), true, false);
        dc.present_voltage_v = Some(400.0);
        dc.begin_cable_check();
        dc.fail();

        // Still energized: nothing reported yet.
        let effects = dc.on_path_event(
            &session(),
            PathEvent::SupplyVoltage { voltage_v: 400.0 },
            now(),
        );
        assert!(effects.is_empty());

        let effects = dc.on_path_event(
            &session(),
            PathEvent::SupplyVoltage { voltage_v: 20.0 },
            now(),
        );
        assert_eq!(
            effects,
            vec![
                Effect::CancelTimer {
                    id: TIMER_CABLE_CHECK
                },
                Effect::HlcUpdate(HlcUpdate::CableCheckFinished(false)),
            ]
        );
    }

    #[test]
    fn contactor_confirmation_timeout_gives_up_rather_than_energizing() {
        let mut dc = dc_port(config(), true, false);
        dc.cable_check = CableCheck::AwaitContactorClosed;

        let effects = dc.on_timer(&session(), TIMER_CONTACTOR_CONFIRM, now());

        assert!(matches!(
            dc.cable_check_stage(),
            CableCheck::Abort { reported: false }
        ));
        assert!(effects.contains(&Effect::SupplyOff));
    }

    #[test]
    fn safe_state_removes_energy_before_releasing_the_vehicle() {
        let mut dc = dc_port(config(), true, true);
        let effects = dc.to_safe_state();

        let supply_off_at = effects.iter().position(|e| *e == Effect::SupplyOff);
        let allow_off_at = effects
            .iter()
            .position(|e| *e == Effect::AllowPowerOn(false));

        assert!(supply_off_at.unwrap() < allow_off_at.unwrap());
    }

    fn deriving_config() -> DcConfig {
        DcConfig {
            // Zero means derive the cable check voltage rather than override it.
            isolation_voltage_v: 0.0,
            relays_open_voltage_v: 48.0,
            ..config()
        }
    }

    /// Drives one `Dc` and keeps every effect it produced, in order.
    struct Harness {
        dc: Dc,
        session: Session,
        effects: Vec<Effect>,
    }

    impl Harness {
        fn new(options: CableCheckOptions) -> Self {
            Self::with_connector(options, ConnectorKind::Other)
        }

        fn with_connector(options: CableCheckOptions, connector: ConnectorKind) -> Self {
            let mut dc = dc_port_with(
                DcConfig {
                    connector,
                    ..deriving_config()
                },
                true,
                false,
                options,
            );
            dc.set_ev_maximum_limits(EvMaximumLimits {
                maximum_voltage_v: Some(400.0),
                ..EvMaximumLimits::default()
            });
            dc.set_evse_max_export_voltage_v(950.0);
            Self {
                dc,
                session: session(),
                effects: Vec::new(),
            }
        }

        fn record(&mut self, produced: Vec<Effect>) -> Vec<Effect> {
            self.effects.extend(produced.iter().cloned());
            produced
        }

        fn begin(&mut self, present_voltage_v: f64) -> Vec<Effect> {
            self.dc.present_voltage_v = Some(present_voltage_v);
            let produced = self.dc.begin_cable_check();
            self.record(produced)
        }

        fn voltage(&mut self, voltage_v: f64) -> Vec<Effect> {
            let produced =
                self.dc
                    .on_path_event(&self.session, PathEvent::SupplyVoltage { voltage_v }, now());
            self.record(produced)
        }

        fn isolation(&mut self, resistance_ohm: f64) -> Vec<Effect> {
            let produced = self.dc.on_path_event(
                &self.session,
                PathEvent::Isolation(IsolationReading { resistance_ohm, ..Default::default() }),
                now(),
            );
            self.record(produced)
        }

        /// The monitor takes the request and then answers it. Completing the
        /// command alone advances nothing: the verdict is what the stage waits
        /// for, as `EvseManager.cpp` waits on `selftest_result`.
        fn self_test_passed(&mut self) -> Vec<Effect> {
            let id = self.awaited_self_test();
            self.complete(id);
            self.self_test_verdict(true)
        }

        /// The published verdict on its own, for the tests that need the
        /// command and the answer separated.
        fn self_test_verdict(&mut self, passed: bool) -> Vec<Effect> {
            let produced =
                self.dc
                    .on_path_event(&self.session, PathEvent::IsolationSelfTest(passed), now());
            self.record(produced)
        }

        /// A successful completion of one named effect, as the loop delivers it.
        fn complete(&mut self, id: Option<EffectId>) -> Vec<Effect> {
            let produced = self
                .dc
                .on_effect_done(&self.session, id, &EffectOutcome::Ok, now());
            self.record(produced)
        }

        /// The identity the loop would carry back for the self test most
        /// recently asked for, read off the effect itself rather than out of the
        /// path's own state.
        fn awaited_self_test(&self) -> Option<EffectId> {
            self.effects
                .iter()
                .rev()
                .find_map(|effect| match effect {
                    Effect::ImdSelfTest { id, .. } => Some(*id),
                    _ => None,
                })
                .or_else(|| panic!("no self test has been asked for"))
        }

        fn contactor_closed(&mut self) -> Vec<Effect> {
            let produced = self
                .dc
                .on_bsp(&self.session, &BspEvent::Cp(CpEvent::PowerOn), CpEdges::default(), now());
            self.record(produced)
        }

        fn unplug(&mut self) -> Vec<Effect> {
            let produced =
                self.dc
                    .on_bsp(&self.session, &BspEvent::Cp(CpEvent::Disconnected), CpEdges::default(), now());
            self.record(produced)
        }

        fn timer(&mut self, id: TimerId) -> Vec<Effect> {
            let produced = self.dc.on_timer(&self.session, id, now());
            self.record(produced)
        }

        fn stage(&self) -> CableCheck {
            self.dc.cable_check_stage()
        }
    }

    /// The five outcomes of one cable check attempt, each asserted on the
    /// whole recorded effect sequence rather than on the last call's return, so
    /// a second emission anywhere in the attempt is visible.
    ///
    /// The completion verdict count is the property under test and it is a
    /// **deliberate divergence** from the C++, recorded in
    /// `docs/architecture.md`. See `Dc::report_failure` for the path that
    /// double sends there.
    mod cable_check_reporting {
        use super::*;

        /// Every `CableCheckFinished` payload the attempt produced, in order.
        fn verdicts(effects: &[Effect]) -> Vec<bool> {
            effects
                .iter()
                .filter_map(|effect| match effect {
                    Effect::HlcUpdate(HlcUpdate::CableCheckFinished(ok)) => Some(*ok),
                    _ => None,
                })
                .collect()
        }

        /// Every isolation status the attempt produced, in order.
        fn statuses(effects: &[Effect]) -> Vec<IsolationStatus> {
            effects
                .iter()
                .filter_map(|effect| match effect {
                    Effect::HlcUpdate(HlcUpdate::IsolationStatus(status)) => Some(*status),
                    _ => None,
                })
                .collect()
        }

        /// Runs a whole attempt to success on a port with an isolation monitor
        /// and the given number of awaited samples.
        fn succeed(measurements: u8) -> Harness {
            let mut h = Harness::new(CableCheckOptions {
                imd_self_test: false,
                imd_self_test_relays_open: false,
                wait_below_60v_before_finish: false,
            });
            h.dc.config.imd_measurements = measurements;
            h.begin(10.0);
            h.contactor_closed();
            h.voltage(450.0);
            for _ in 0..measurements {
                h.isolation(500_000.0);
            }
            h
        }

        /// `EvseManager.cpp:2020-2028`. No isolation monitor is wired, so the
        /// status says so and the attempt is complete without being performed.
        #[test]
        fn the_no_isolation_monitor_skip_says_so_and_succeeds_once() {
            let mut dc = dc_port(config(), false, false);

            let effects = dc.begin_cable_check();

            assert_eq!(
                effects,
                vec![
                    Effect::HlcUpdate(HlcUpdate::IsolationStatus(IsolationStatus::NoImd)),
                    Effect::HlcUpdate(HlcUpdate::CableCheckFinished(true)),
                ],
                "the status precedes the verdict, as `:2025-2026` sends them"
            );
        }

        /// `EvseManager.cpp:2015` then `:2287`.
        #[test]
        fn a_measured_isolation_in_range_is_valid_and_succeeds_once() {
            let h = succeed(3);

            assert_eq!(h.stage(), CableCheck::Done);
            assert_eq!(statuses(&h.effects), vec![IsolationStatus::Valid]);
            assert_eq!(verdicts(&h.effects), vec![true]);
        }

        /// `EvseManager.cpp:2011` then `:2539`. The fault status goes out as
        /// soon as the sample is judged; the verdict waits for the cable to be
        /// confirmed de-energized.
        #[test]
        fn a_measured_isolation_fault_is_a_fault_and_fails_once() {
            let mut h = Harness::new(CableCheckOptions {
                imd_self_test: false,
                imd_self_test_relays_open: false,
                wait_below_60v_before_finish: false,
            });
            h.dc.config.imd_measurements = 1;
            h.begin(10.0);
            h.contactor_closed();
            h.voltage(450.0);

            h.isolation(1_000.0);
            assert_eq!(statuses(&h.effects), vec![IsolationStatus::Fault]);
            assert_eq!(verdicts(&h.effects), Vec::<bool>::new());

            h.voltage(10.0);
            assert_eq!(verdicts(&h.effects), vec![false]);

            // Further readings on a reported attempt add nothing.
            h.voltage(5.0);
            h.voltage(1.0);
            assert_eq!(verdicts(&h.effects), vec![false]);
            assert_eq!(statuses(&h.effects), vec![IsolationStatus::Fault]);
        }

        /// `EvseManager.cpp:2228` and `:2259-2261`. With no measurement awaited
        /// the self test alone stands in for one, so the status is `Valid`
        /// without any resistance having been read.
        #[test]
        fn the_self_test_only_mode_reports_valid_and_succeeds_once() {
            let mut h = Harness::new(CableCheckOptions {
                imd_self_test: true,
                imd_self_test_relays_open: false,
                wait_below_60v_before_finish: false,
            });
            h.dc.config.imd_measurements = 0;
            h.begin(10.0);
            h.contactor_closed();
            h.voltage(450.0);
            h.self_test_passed();

            assert_eq!(h.stage(), CableCheck::Done);
            assert_eq!(statuses(&h.effects), vec![IsolationStatus::Valid]);
            assert_eq!(verdicts(&h.effects), vec![true]);
        }

        /// A stage timeout aborts, and the abort reports exactly once even
        /// though two things could each report it: the de-energized reading and
        /// the abort's own bound.
        #[test]
        fn a_stage_timeout_fails_once_however_the_cable_de_energizes() {
            let mut h = Harness::new(CableCheckOptions {
                imd_self_test: false,
                imd_self_test_relays_open: false,
                wait_below_60v_before_finish: false,
            });
            h.begin(10.0);
            h.contactor_closed();

            h.timer(TIMER_CABLE_CHECK);
            assert!(matches!(h.stage(), CableCheck::Abort { reported: false }));
            assert_eq!(verdicts(&h.effects), Vec::<bool>::new());

            h.voltage(10.0);
            assert_eq!(verdicts(&h.effects), vec![false]);

            // The abort's own bound expires afterwards and must not repeat it.
            h.timer(TIMER_CABLE_CHECK);
            assert_eq!(verdicts(&h.effects), vec![false]);
            assert!(
                statuses(&h.effects).is_empty(),
                "no sample was ever judged, so no isolation status was earned"
            );
        }

        /// The no isolation monitor skip is decided before any option is read,
        /// so the self test settings cannot reach it. "No monitor" and "a
        /// monitor but no measurement awaited" are two different routes to a
        /// success and only one of them may say `NoImd`.
        ///
        /// The crossing this test used to drive is now a compile error rather
        /// than an assertion. It built a port with `imd_wired = false` and then
        /// called `set_cable_check_options` on it, which is exactly the
        /// unreachable construction: three settings handed to a port that skips
        /// the whole sequence they configure. `CableCheckOptions` lives on
        /// `IsolationMonitor` now, so `dc_port_with(config, false, .., options)`
        /// has nowhere to put them and `set_cable_check_options` does not
        /// exist. What is left to assert is the skip itself, over both
        /// measurement counts.
        #[test]
        fn the_no_monitor_skip_ignores_the_self_test_settings() {
            for measurements in [0, 3] {
                let mut dc = dc_port(
                    DcConfig {
                        imd_measurements: measurements,
                        ..config()
                    },
                    false,
                    false,
                );

                let effects = dc.begin_cable_check();

                assert_eq!(
                    effects,
                    vec![
                        Effect::HlcUpdate(HlcUpdate::IsolationStatus(IsolationStatus::NoImd)),
                        Effect::HlcUpdate(HlcUpdate::CableCheckFinished(true)),
                    ],
                    "{measurements} measurement(s)"
                );
                assert!(
                    !effects.contains(&Effect::ImdStart),
                    "a port with no monitor must not start one"
                );
            }
        }

        /// An abort arriving mid wait, then the wait's own bound firing behind
        /// it. Two things that each report a failure, one attempt, one verdict.
        ///
        /// This is the combination the C++ double sends on: its wait helper
        /// emits the verdict directly when the cancellation is seen
        /// (`EvseManager.cpp:2462`) and then returns false, on which its caller
        /// runs `fail_cable_check`, which emits a second one (`:2539`).
        #[test]
        fn an_abort_mid_wait_followed_by_that_waits_bound_still_fails_once() {
            let mut h = Harness::new(CableCheckOptions {
                imd_self_test: false,
                imd_self_test_relays_open: false,
                wait_below_60v_before_finish: true,
            });
            h.dc.config.imd_measurements = 1;
            h.begin(10.0);
            h.contactor_closed();
            h.voltage(450.0);
            h.isolation(500_000.0);
            assert_eq!(h.stage(), CableCheck::RampDown, "waiting to de-energize");

            // The unplug lands while that wait is outstanding.
            h.unplug();
            assert!(matches!(h.stage(), CableCheck::Abort { reported: false }));

            h.timer(TIMER_CABLE_CHECK);
            assert_eq!(verdicts(&h.effects), vec![false]);

            h.voltage(10.0);
            h.timer(TIMER_CABLE_CHECK);
            assert_eq!(
                verdicts(&h.effects),
                vec![false],
                "one attempt, one verdict, whatever arrives afterwards"
            );
            // The sample was judged before the abort, so its status stands.
            assert_eq!(statuses(&h.effects), vec![IsolationStatus::Valid]);
        }

        /// The defect that made ocpp21 `bidirectional::test_q01` loop on
        /// `DcCableCheckReq` until the vehicle gave up.
        ///
        /// The cable check granted the HLC half of the contactor permission and
        /// then waited in `AwaitContactorClosed` for a fact nothing could
        /// produce: the port actuated neither the permission nor the pilot, so
        /// `YetiSimulator.cpp:490-496` never closed the relay, the five second
        /// bound expired and the attempt reported `CableCheckFinished(false)`
        /// forever. Measured on 2026-09-10: 531 `CableCheckReq` against the
        /// C++'s 60, and zero `allow_power_on` grants against its two.
        ///
        /// Driven the way `Core` drives it, because the defect was in the
        /// composition and not in any one step: each half looked complete on
        /// its own.
        #[test]
        fn a_dc_cable_check_closes_the_contactor_it_then_waits_for() {
            let mut dc = dc_port_with(
                deriving_config(),
                true,
                false,
                CableCheckOptions::default(),
            );
            let mut live = session();
            dc.on_startup();

            // The plug in raises the pilot, from the `WaitingForAuthentication`
            // entry (`Charger.cpp:302-315`) and before any authorization.
            let plugged_in = dc.on_session_start(&live, now());
            assert!(
                plugged_in.contains(&Effect::PwmOn(PWM_5_PERCENT)),
                "no pilot, so a vehicle in state C can never close: {plugged_in:?}"
            );
            live.transaction_active = true;

            // Authorization carries a DC session into `PrepareCharging`, whose
            // arm raises the same offer (`:749`) and therefore re-sends
            // nothing, because all three C++ sites are
            // `update_pwm_now_if_changed`.
            let prepared = dc.on_authorized(&live, now());
            assert!(
                !prepared.contains(&Effect::PwmOn(PWM_5_PERCENT)),
                "a standing offer is not re-sent: {prepared:?}"
            );
            assert!(
                !prepared.contains(&Effect::AllowPowerOn(true)),
                "granted before the vehicle asked for power: {prepared:?}"
            );

            // The vehicle closes S2. One gate alone still grants nothing.
            let requested = dc.on_bsp(
                &live,
                &BspEvent::Cp(CpEvent::C),
                CpEdges {
                    requested_power: true,
                    ..CpEdges::default()
                },
                now(),
            );
            assert!(
                !requested.contains(&Effect::AllowPowerOn(true)),
                "granted on the IEC gate alone: {requested:?}"
            );

            // The cable check's own grant completes the pair, and the grant is
            // what the stage it enters is waiting for.
            dc.present_voltage_v = Some(10.0);
            let begun = dc.begin_cable_check();
            assert!(
                begun.contains(&Effect::AllowPowerOn(true)),
                "the cable check waits for a contactor it never asks to close: {begun:?}"
            );
            assert_eq!(dc.cable_check, CableCheck::AwaitContactorClosed);

            // And the board answering that grant is what lets it move on,
            // rather than the five second bound expiring.
            dc.on_bsp(&live, &BspEvent::Cp(CpEvent::PowerOn), CpEdges::default(), now());
            assert_eq!(
                dc.cable_check,
                CableCheck::RampUp,
                "the closed contactor did not advance the stage"
            );
        }

        /// A cable check cancelled after the contactor permission was already
        /// granted leaves the permission standing until the next session opens.
        ///
        /// That is the C++ behavior and not an oversight on either side: the
        /// only clear is the `Idle` entry (`Charger.cpp:225`), which is the same
        /// set `on_session_start` clears here. The permission is one of two
        /// gates and the other is withdrawn by the unplug, so nothing can close
        /// on it in between.
        #[test]
        fn an_abort_after_the_permission_was_granted_leaves_it_for_the_next_session() {
            let mut h = Harness::new(CableCheckOptions {
                imd_self_test: false,
                imd_self_test_relays_open: false,
                wait_below_60v_before_finish: false,
            });
            h.begin(10.0);
            assert!(h.dc.hlc_allows_close, "the safe voltage grants it");

            h.unplug();
            assert!(matches!(h.stage(), CableCheck::Abort { reported: false }));
            assert!(
                h.dc.hlc_allows_close,
                "the abort does not withdraw it, as `Charger.cpp` does not either"
            );
            assert!(
                !h.dc.may_close_contactor(),
                "and it cannot close on one gate alone"
            );

            h.dc.on_session_start(&h.session, now());
            assert!(!h.dc.hlc_allows_close, "the next session starts it cleared");
        }

        /// The mirror of the case above: the cable never reports itself
        /// de-energized, so the abort's own bound is what reports the failure.
        /// `EvseManager.cpp:2536-2539` sends the verdict "anyway" on that
        /// timeout, and an attempt that reported nothing at all would leave the
        /// vehicle waiting out its own timer.
        #[test]
        fn an_abort_whose_cable_never_de_energizes_still_fails_once() {
            let mut h = Harness::new(CableCheckOptions {
                imd_self_test: false,
                imd_self_test_relays_open: false,
                wait_below_60v_before_finish: false,
            });
            h.begin(10.0);
            h.contactor_closed();

            h.timer(TIMER_CABLE_CHECK);
            h.voltage(400.0);
            assert_eq!(verdicts(&h.effects), Vec::<bool>::new());

            h.timer(TIMER_CABLE_CHECK);
            assert_eq!(verdicts(&h.effects), vec![false]);
            assert!(matches!(h.stage(), CableCheck::Abort { reported: true }));

            // And still exactly once once the cable does come down.
            h.voltage(10.0);
            assert_eq!(verdicts(&h.effects), vec![false]);
        }
    }

    /// A `Dc` that never adopted the core's space cannot name a request the
    /// core could correlate, so it must not make one.
    ///
    /// The counter it used to build for itself was the second space, and every
    /// fixture above adopts a real one now. This is the only place the absent
    /// case is driven, and it is driven because the branch exists: `Dc::new` is
    /// `pub` and `adopt_effect_ids` is a separate call, so the state is
    /// reachable by construction even though `Core::new` never leaves a path
    /// in it.
    #[test]
    fn a_path_with_no_adopted_space_fails_the_stage_instead_of_asking() {
        let wiring = Wiring {
            imd: true,
            ..Wiring::default()
        };
        let mut dc = Dc::new(
            config(),
            IsolationMonitor::for_wiring(&wiring, all_options()),
            OverVoltageMonitor::for_wiring(&wiring),
        );
        dc.present_voltage_v = Some(10.0);

        dc.begin_cable_check();
        // The relays open stage ramps first and asks for the self test only
        // when the supply reports the target, so the request needs the reading.
        let effects = dc.on_path_event(
            &session(),
            PathEvent::SupplyVoltage { voltage_v: 500.0 },
            now(),
        );

        assert!(
            !effects
                .iter()
                .any(|effect| matches!(effect, Effect::ImdSelfTest { .. })),
            "a self test nobody can attribute must not be asked for, got {effects:?}"
        );
        assert!(
            matches!(dc.cable_check_stage(), CableCheck::Abort { .. }),
            "the stage fails instead, got {:?}",
            dc.cable_check_stage()
        );
    }

    fn all_options() -> CableCheckOptions {
        CableCheckOptions {
            imd_self_test: true,
            imd_self_test_relays_open: true,
            wait_below_60v_before_finish: true,
        }
    }

    #[test]
    fn the_cable_check_voltage_follows_formula_cc_one() {
        // A vehicle at or below 500 V gets fifty volts of headroom, capped by
        // the five hundred volt ceiling and by what the supply can export.
        assert_eq!(cable_check_voltage_for(400.0, 950.0), 450.0);
        assert_eq!(cable_check_voltage_for(500.0, 950.0), 500.0);
        assert_eq!(cable_check_voltage_for(400.0, 300.0), 300.0);
        // Above five hundred volts the ceiling is ten percent of the vehicle
        // maximum, still capped by the supply.
        assert!((cable_check_voltage_for(800.0, 950.0) - 880.0).abs() < 1e-9);
        assert_eq!(cable_check_voltage_for(900.0, 950.0), 950.0);
    }

    #[test]
    fn the_derived_cable_check_voltage_is_used_when_no_override_is_configured() {
        let mut dc = dc_port(deriving_config(), true, false);
        dc.set_ev_maximum_limits(EvMaximumLimits {
            maximum_voltage_v: Some(400.0),
            ..EvMaximumLimits::default()
        });
        dc.set_evse_max_export_voltage_v(950.0);
        assert_eq!(dc.cable_check_voltage_v(), 450.0);
    }

    #[test]
    fn a_vehicle_that_reported_no_maximum_voltage_falls_back_to_five_hundred() {
        let mut dc = dc_port(deriving_config(), true, false);
        dc.set_evse_max_export_voltage_v(950.0);
        assert_eq!(dc.cable_check_voltage_v(), 500.0);
    }

    #[test]
    fn a_configured_isolation_voltage_overrides_the_derivation() {
        let mut dc = dc_port(config(), true, false);
        dc.set_ev_maximum_limits(EvMaximumLimits {
            maximum_voltage_v: Some(400.0),
            ..EvMaximumLimits::default()
        });
        dc.set_evse_max_export_voltage_v(950.0);
        assert_eq!(dc.cable_check_voltage_v(), 500.0);
    }

    #[test]
    fn the_full_cable_check_sequence_runs_in_the_ported_order() {
        let mut h = Harness::new(all_options());

        h.begin(400.0);
        h.voltage(10.0);
        h.voltage(48.0);
        h.self_test_passed();
        h.contactor_closed();
        h.voltage(450.0);
        h.isolation(500_000.0);
        h.isolation(500_000.0);
        h.isolation(500_000.0);
        h.voltage(20.0);

        // Read off the request rather than written: a fixture cannot mint an
        // identity, and the sequence asserts that the self test went out under
        // the one the path allocated. `self_test_passed` above is what proves
        // it is the identity the verdict has to carry.
        let self_test = h
            .awaited_self_test()
            .expect("the sequence asks for a self test");
        assert_eq!(h.stage(), CableCheck::Done);
        assert_eq!(
            h.effects,
            vec![
                Effect::StartTimer {
                    id: TIMER_CABLE_CHECK,
                    after: WAIT_VOLTAGE_TIMEOUT
                },
                Effect::CancelTimer {
                    id: TIMER_CABLE_CHECK
                },
                Effect::SetSupplySetpoint {
                    mode: SupplyMode::Export,
                    voltage_v: 48.0,
                    current_a: 2.0
                },
                Effect::SetSupplyMode { mode: SupplyMode::Export, phase: ChargingPhase::CableCheck },
                Effect::StartTimer {
                    id: TIMER_CABLE_CHECK,
                    after: WAIT_VOLTAGE_TIMEOUT
                },
                Effect::CancelTimer {
                    id: TIMER_CABLE_CHECK
                },
                Effect::ImdSelfTest {
                    id: self_test,
                    voltage_v: 48.0
                },
                Effect::StartTimer {
                    id: TIMER_CABLE_CHECK,
                    after: SELF_TEST_TIMEOUT
                },
                Effect::CancelTimer {
                    id: TIMER_CABLE_CHECK
                },
                Effect::StartTimer {
                    id: TIMER_CONTACTOR_CONFIRM,
                    after: Duration::from_secs(5)
                },
                Effect::CancelTimer {
                    id: TIMER_CONTACTOR_CONFIRM
                },
                Effect::SetSupplySetpoint {
                    mode: SupplyMode::Export,
                    voltage_v: 450.0,
                    current_a: 2.0
                },
                Effect::StartTimer {
                    id: TIMER_CABLE_CHECK,
                    after: WAIT_VOLTAGE_TIMEOUT
                },
                Effect::CancelTimer {
                    id: TIMER_CABLE_CHECK
                },
                Effect::ImdStart,
                Effect::StartTimer {
                    id: TIMER_CABLE_CHECK,
                    after: ISOLATION_SAMPLE_TIMEOUT
                },
                Effect::StartTimer {
                    id: TIMER_CABLE_CHECK,
                    after: ISOLATION_SAMPLE_TIMEOUT
                },
                Effect::StartTimer {
                    id: TIMER_CABLE_CHECK,
                    after: ISOLATION_SAMPLE_TIMEOUT
                },
                // The third sample is the one judged, and the status it earned
                // goes out before the ramp down the verdict waits on.
                Effect::HlcUpdate(HlcUpdate::IsolationStatus(IsolationStatus::Valid)),
                Effect::CancelTimer {
                    id: TIMER_CABLE_CHECK
                },
                Effect::SupplyOff,
                Effect::StartTimer {
                    id: TIMER_CABLE_CHECK,
                    after: WAIT_VOLTAGE_TIMEOUT
                },
                Effect::CancelTimer {
                    id: TIMER_CABLE_CHECK
                },
                Effect::HlcUpdate(HlcUpdate::CableCheckFinished(true)),
            ]
        );
    }

    #[test]
    fn the_supply_is_never_energized_before_the_contactor_is_confirmed_closed() {
        let mut h = Harness::new(CableCheckOptions {
            imd_self_test_relays_open: false,
            ..all_options()
        });

        h.begin(400.0);
        h.voltage(10.0);

        assert_eq!(h.stage(), CableCheck::AwaitContactorClosed);
        assert!(
            !h.effects
                .iter()
                .any(|e| matches!(e, Effect::SetSupplySetpoint { .. })),
            "no setpoint may be applied before the contactor is confirmed closed: {:?}",
            h.effects
        );

        let effects = h.contactor_closed();
        assert_eq!(h.stage(), CableCheck::RampUp);
        assert!(effects.contains(&Effect::SetSupplySetpoint {
            mode: SupplyMode::Export,
            voltage_v: 450.0,
            current_a: 2.0
        }));
    }

    #[test]
    fn the_hlc_contactor_gate_opens_only_once_the_cable_is_de_energized() {
        let mut h = Harness::new(all_options());
        h.dc.iec_allows_close = true;

        h.begin(400.0);
        assert!(
            !h.dc.may_close_contactor(),
            "the gate must stay shut while the cable is energized"
        );

        h.voltage(10.0);
        assert!(h.dc.may_close_contactor());
    }

    #[test]
    fn an_energized_self_test_runs_when_the_relays_open_one_is_disabled() {
        let mut h = Harness::new(CableCheckOptions {
            imd_self_test: true,
            imd_self_test_relays_open: false,
            wait_below_60v_before_finish: false,
        });

        h.begin(10.0);
        h.contactor_closed();
        let effects = h.voltage(450.0);

        assert_eq!(h.stage(), CableCheck::EnergizedSelfTest);
        assert!(effects.contains(&Effect::ImdSelfTest {
            id: h.awaited_self_test().expect("a self test was asked for"),
            voltage_v: 450.0
        }));

        let effects = h.self_test_passed();
        assert!(matches!(h.stage(), CableCheck::Sampling { taken: 0 }));
        assert!(effects.contains(&Effect::ImdStart));
    }

    #[test]
    fn a_relays_open_self_test_alone_removes_energy_before_the_contactor_wait() {
        let mut h = Harness::new(CableCheckOptions {
            imd_self_test: false,
            imd_self_test_relays_open: true,
            wait_below_60v_before_finish: true,
        });

        h.begin(10.0);
        h.voltage(48.0);
        let effects = h.self_test_passed();

        assert_eq!(h.stage(), CableCheck::AwaitContactorClosed);
        assert!(
            effects.contains(&Effect::SupplyOff),
            "the early self test voltage is removed again: {effects:?}"
        );
    }

    #[test]
    fn skipping_both_self_tests_samples_straight_after_the_ramp_up() {
        let mut h = Harness::new(CableCheckOptions {
            imd_self_test: false,
            imd_self_test_relays_open: false,
            wait_below_60v_before_finish: true,
        });

        h.begin(10.0);
        h.contactor_closed();
        let effects = h.voltage(450.0);

        assert!(matches!(h.stage(), CableCheck::Sampling { taken: 0 }));
        assert_eq!(
            effects[0],
            Effect::CancelTimer {
                id: TIMER_CABLE_CHECK
            }
        );
        assert!(effects.contains(&Effect::ImdStart));
        assert!(
            !effects
                .iter()
                .any(|e| matches!(e, Effect::ImdSelfTest { .. })),
            "no self test was asked for"
        );
    }

    #[test]
    fn cable_check_reports_finished_without_the_below_sixty_volt_wait_when_disabled() {
        let mut h = Harness::new(CableCheckOptions {
            imd_self_test: false,
            imd_self_test_relays_open: false,
            wait_below_60v_before_finish: false,
        });

        h.begin(10.0);
        h.contactor_closed();
        h.voltage(450.0);
        h.isolation(500_000.0);
        h.isolation(500_000.0);
        let effects = h.isolation(500_000.0);

        assert_eq!(h.stage(), CableCheck::Done);
        assert_eq!(
            effects.last(),
            Some(&Effect::HlcUpdate(HlcUpdate::CableCheckFinished(true)))
        );
    }

    #[test]
    fn zero_configured_samples_reports_finished_without_measuring() {
        let mut dc = dc_port_with(
            DcConfig {
                imd_measurements: 0,
                ..deriving_config()
            },
            true,
            false,
            CableCheckOptions {
                imd_self_test: false,
                imd_self_test_relays_open: false,
                wait_below_60v_before_finish: false,
            },
        );
        let s = session();
        dc.present_voltage_v = Some(10.0);
        dc.begin_cable_check();
        dc.on_bsp(&s, &BspEvent::Cp(CpEvent::PowerOn), CpEdges::default(), now());
        let effects = dc.on_path_event(&s, PathEvent::SupplyVoltage { voltage_v: 500.0 }, now());

        assert_eq!(dc.cable_check_stage(), CableCheck::Done);
        assert!(effects.contains(&Effect::ImdStart));
        assert_eq!(
            effects.last(),
            Some(&Effect::HlcUpdate(HlcUpdate::CableCheckFinished(true)))
        );
    }

    #[test]
    fn isolation_resistance_below_the_fault_threshold_fails_the_cable_check() {
        let mut h = Harness::new(CableCheckOptions {
            imd_self_test: false,
            imd_self_test_relays_open: false,
            wait_below_60v_before_finish: false,
        });

        h.begin(10.0);
        h.contactor_closed();
        h.voltage(450.0);
        h.isolation(500_000.0);
        h.isolation(500_000.0);
        let effects = h.isolation(99_000.0);

        assert!(matches!(h.stage(), CableCheck::Abort { reported: false }));
        assert!(effects.contains(&Effect::SupplyOff));
        assert!(effects.contains(&Effect::ImdStop));
    }

    #[test]
    fn an_mcs_connector_uses_the_higher_isolation_fault_threshold() {
        for (connector, expect_abort) in [(ConnectorKind::Other, false), (ConnectorKind::Mcs, true)]
        {
            let mut h = Harness::with_connector(
                CableCheckOptions {
                    imd_self_test: false,
                    imd_self_test_relays_open: false,
                    wait_below_60v_before_finish: false,
                },
                connector,
            );

            h.begin(10.0);
            h.contactor_closed();
            h.voltage(450.0);
            h.isolation(110_000.0);
            h.isolation(110_000.0);
            h.isolation(110_000.0);

            let aborted = matches!(h.stage(), CableCheck::Abort { .. });
            assert_eq!(aborted, expect_abort, "connector {connector:?}");
        }
    }

    #[test]
    fn a_self_test_verdict_that_never_arrives_fails_the_cable_check() {
        let mut h = Harness::new(all_options());

        h.begin(10.0);
        h.voltage(48.0);
        assert_eq!(h.stage(), CableCheck::RelaysOpenSelfTest);

        let effects = h.timer(TIMER_CABLE_CHECK);
        assert!(matches!(h.stage(), CableCheck::Abort { reported: false }));
        assert!(effects.contains(&Effect::SupplyOff));
    }

    /// The other half of the C++ pair. `EvseManager.cpp` fails the cable check
    /// on a false verdict at both self test stages, with a message of its own,
    /// separately from the timeout that covers a verdict never arriving. The
    /// command completing first is what makes the two distinct: the monitor
    /// took the request and then answered no.
    #[test]
    fn a_failed_self_test_verdict_fails_the_cable_check() {
        let mut h = Harness::new(all_options());

        h.begin(10.0);
        h.voltage(48.0);
        assert_eq!(h.stage(), CableCheck::RelaysOpenSelfTest);

        h.complete(h.awaited_self_test());
        assert_eq!(
            h.stage(),
            CableCheck::RelaysOpenSelfTest,
            "the command completing is not the verdict"
        );

        let effects = h.self_test_verdict(false);

        assert!(matches!(h.stage(), CableCheck::Abort { reported: false }));
        assert!(effects.contains(&Effect::SupplyOff), "got {effects:?}");
    }

    #[test]
    fn an_isolation_sample_that_never_arrives_fails_the_cable_check() {
        let mut h = Harness::new(CableCheckOptions {
            imd_self_test: false,
            imd_self_test_relays_open: false,
            wait_below_60v_before_finish: false,
        });

        h.begin(10.0);
        h.contactor_closed();
        h.voltage(450.0);

        let effects = h.timer(TIMER_CABLE_CHECK);
        assert!(matches!(h.stage(), CableCheck::Abort { reported: false }));
        assert!(effects.contains(&Effect::SupplyOff));
        assert!(effects.contains(&Effect::ImdStop));
    }

    #[test]
    fn a_ramp_up_that_never_reaches_the_target_fails_the_cable_check() {
        let mut h = Harness::new(CableCheckOptions {
            imd_self_test: false,
            imd_self_test_relays_open: false,
            wait_below_60v_before_finish: false,
        });

        h.begin(10.0);
        h.contactor_closed();
        assert_eq!(h.stage(), CableCheck::RampUp);

        let effects = h.timer(TIMER_CABLE_CHECK);
        assert!(matches!(h.stage(), CableCheck::Abort { reported: false }));
        assert!(effects.contains(&Effect::SupplyOff));
    }

    #[test]
    fn a_ramp_down_that_never_completes_fails_the_cable_check() {
        let mut h = Harness::new(CableCheckOptions {
            imd_self_test: false,
            imd_self_test_relays_open: false,
            wait_below_60v_before_finish: true,
        });

        h.begin(10.0);
        h.contactor_closed();
        h.voltage(450.0);
        h.isolation(500_000.0);
        h.isolation(500_000.0);
        h.isolation(500_000.0);
        assert_eq!(h.stage(), CableCheck::RampDown);

        let effects = h.timer(TIMER_CABLE_CHECK);
        assert!(matches!(h.stage(), CableCheck::Abort { reported: false }));
        assert!(effects.contains(&Effect::ImdStop));
    }

    #[test]
    fn two_outstanding_effects_complete_in_either_order_and_are_attributed_correctly() {
        // At the energized self test the ramp up setpoint is still outstanding
        // alongside the self test whose verdict the stage waits for. Whichever
        // completes first, only the self test may advance the stage.
        for self_test_first in [false, true] {
            let mut h = Harness::new(CableCheckOptions {
                imd_self_test: true,
                imd_self_test_relays_open: false,
                wait_below_60v_before_finish: false,
            });
            h.begin(10.0);
            h.contactor_closed();
            h.voltage(450.0);
            assert_eq!(h.stage(), CableCheck::EnergizedSelfTest);

            let setpoint = None;
            let self_test = h.awaited_self_test();

            if self_test_first {
                h.complete(self_test);
                let verdict = h.self_test_verdict(true);
                assert!(
                    matches!(h.stage(), CableCheck::Sampling { taken: 0 }),
                    "the verdict advances the stage that asked for it"
                );
                assert!(verdict.contains(&Effect::ImdStart));

                let late = h.complete(setpoint);
                assert!(
                    late.is_empty(),
                    "a completion no stage awaits produces nothing: {late:?}"
                );
                assert!(matches!(h.stage(), CableCheck::Sampling { taken: 0 }));
            } else {
                let other = h.complete(setpoint);
                assert!(
                    other.is_empty(),
                    "the setpoint completion is not a self test verdict: {other:?}"
                );
                assert_eq!(
                    h.stage(),
                    CableCheck::EnergizedSelfTest,
                    "the stage still waits for the verdict it asked for"
                );

                h.complete(self_test);
                let verdict = h.self_test_verdict(true);
                assert!(matches!(h.stage(), CableCheck::Sampling { taken: 0 }));
                assert!(verdict.contains(&Effect::ImdStart));
            }
        }
    }

    /// The same verdict delivered twice. `on_self_test_passed` clears the slot,
    /// so the second delivery is nobody's and advances nothing.
    ///
    /// The core side has had this since the port
    /// (`the_awaited_identity_is_answered_only_once`); the path side did not.
    /// It matters here for the same reason: the stage the verdict advanced into
    /// arms its own timeout and counts its own samples, and a verdict replayed
    /// after that would re-enter a stage that has already moved on.
    #[test]
    fn a_self_test_verdict_delivered_twice_advances_the_stage_once() {
        let mut h = Harness::new(CableCheckOptions {
            imd_self_test: true,
            imd_self_test_relays_open: false,
            wait_below_60v_before_finish: false,
        });
        h.begin(10.0);
        h.contactor_closed();
        h.voltage(450.0);
        assert_eq!(h.stage(), CableCheck::EnergizedSelfTest);
        h.complete(h.awaited_self_test());

        let first = h.self_test_verdict(true);
        assert!(
            first.contains(&Effect::ImdStart),
            "the verdict advances the stage that asked for it, got {first:?}"
        );
        assert!(matches!(h.stage(), CableCheck::Sampling { taken: 0 }));

        let again = h.self_test_verdict(true);
        assert!(
            again.is_empty(),
            "a replayed verdict belongs to nothing, got {again:?}"
        );
        assert!(matches!(h.stage(), CableCheck::Sampling { taken: 0 }));
    }

    #[test]
    fn a_verdict_from_an_earlier_cable_check_does_not_advance_a_later_one() {
        let options = CableCheckOptions {
            imd_self_test: true,
            imd_self_test_relays_open: false,
            wait_below_60v_before_finish: false,
        };
        let mut h = Harness::new(options);

        h.begin(10.0);
        h.contactor_closed();
        h.voltage(450.0);
        assert_eq!(h.stage(), CableCheck::EnergizedSelfTest);
        let abandoned = h.awaited_self_test();

        // The session ends before the verdict arrives and a new one starts.
        h.unplug();
        h.dc.on_session_start(&h.session, now());
        h.dc.contactor_closed = true;
        h.begin(10.0);
        h.voltage(450.0);
        assert_eq!(h.stage(), CableCheck::EnergizedSelfTest);
        assert_ne!(
            h.awaited_self_test(),
            abandoned,
            "a fresh request is a fresh identity"
        );

        let late = h.complete(abandoned);
        assert!(
            late.is_empty(),
            "the abandoned verdict belongs to nothing: {late:?}"
        );
        assert_eq!(h.stage(), CableCheck::EnergizedSelfTest);
    }

    /// A stage reached with a self test verdict outstanding.
    fn awaiting_a_self_test() -> Harness {
        let mut h = Harness::new(CableCheckOptions {
            imd_self_test: true,
            imd_self_test_relays_open: false,
            wait_below_60v_before_finish: false,
        });
        h.begin(10.0);
        h.contactor_closed();
        h.voltage(450.0);
        assert_eq!(h.stage(), CableCheck::EnergizedSelfTest);
        h
    }

    /// A failed completion this path never asked for is not its stage failing.
    ///
    /// This test used to assert the opposite: that a `Failed` outcome under no
    /// identity at all aborted the check and removed energy. That was the
    /// defect rather than the contract. `on_effect_done` matched the outcome
    /// before the identity, so any failed completion drove this failure path,
    /// and a refused metering transaction start reached it as an abort of a
    /// cable check that had nothing to do with the meter. It stayed harmless
    /// only because `Core::apply` offers each completion to
    /// `answer_transaction_start` first.
    #[test]
    fn a_failing_effect_this_path_never_awaited_leaves_the_cable_check_alone() {
        // Both shapes a completion that is not this stage's verdict can take:
        // an effect nobody correlated, and an identity no effect was issued
        // under. `Issued::answers` rejects the two through the same comparison,
        // and the gate is what is under test, so neither stands for the other.
        //
        // The second is drawn from this port's own space, which is what makes
        // it certainly different from the awaited one rather than probably:
        // a second allocator beginning at zero is the collision that once let a
        // powermeter reply pass an isolation monitor self test.
        for unissued in [false, true] {
            let mut h = awaiting_a_self_test();
            let foreign = unissued.then(|| {
                h.dc.effect_ids
                    .as_mut()
                    .expect("the fixture adopted a space")
                    .allocate()
                    .effect_id()
            });
            assert_ne!(foreign, h.awaited_self_test(), "for unissued={unissued}");

            let effects = h.dc.on_effect_done(
                &h.session,
                foreign,
                &EffectOutcome::Failed("meter busy".into()),
                now(),
            );

            assert!(effects.is_empty(), "{foreign:?} moved something: {effects:?}");
            assert_eq!(h.stage(), CableCheck::EnergizedSelfTest, "for {foreign:?}");
        }
    }

    /// The premise the gate above rests on, pinned rather than argued: every
    /// stage the check waits in arms a bound. Ignoring a failure that belongs
    /// to nobody is only safe while that holds, so a stage added without one
    /// must fail here rather than in the field.
    #[test]
    fn every_waiting_cable_check_stage_arms_a_bound() {
        let waiting = [
            CableCheck::AwaitSafeVoltage,
            CableCheck::RelaysOpenSelfTest,
            CableCheck::RelaysOpenSelfTest,
            CableCheck::AwaitContactorClosed,
            CableCheck::RampUp,
            CableCheck::Sampling { taken: 0 },
            CableCheck::Sampling { taken: 1 },
            CableCheck::Sampling { taken: 2 },
            CableCheck::RampDown,
        ];

        for (steps, stage) in waiting.iter().enumerate() {
            let h = stage_reached_by(steps + 1);
            assert_eq!(h.stage(), *stage, "after {} steps", steps + 1);

            // The contactor wait is bounded by its own timer; every other stage
            // uses the one stage timeout.
            let bounded = if *stage == CableCheck::AwaitContactorClosed {
                h.effects.iter().any(|effect| {
                    matches!(effect, Effect::StartTimer { id, .. } if *id == TIMER_CONTACTOR_CONFIRM)
                })
            } else {
                h.dc.cable_check_timer_armed
            };
            assert!(bounded, "{stage:?} waits with nothing to give up on");
        }
    }

    /// The other half, and the one a spurious abort must not be traded for: the
    /// verdict this stage **did** ask for still fails it.
    #[test]
    fn a_failing_self_test_verdict_fails_the_cable_check() {
        let mut h = awaiting_a_self_test();
        let verdict = h.awaited_self_test();

        let effects = h.dc.on_effect_done(
            &h.session,
            verdict,
            &EffectOutcome::Failed("monitor refused".into()),
            now(),
        );

        assert!(matches!(h.stage(), CableCheck::Abort { reported: false }));
        assert!(effects.contains(&Effect::SupplyOff), "got {effects:?}");
    }

    /// And a failure that belongs to nobody is not lost, only left to the bound
    /// that every waiting stage arms. Ignoring it above cannot strand the
    /// check: whatever the stage was waiting for still never arrives, and the
    /// stage timeout reports the failure.
    #[test]
    fn an_ignored_failure_still_fails_the_check_through_the_stage_bound() {
        let mut h = awaiting_a_self_test();
        h.dc.on_effect_done(
            &h.session,
            None,
            &EffectOutcome::Failed("meter busy".into()),
            now(),
        );

        let expired = h.timer(TIMER_CABLE_CHECK);

        assert!(matches!(h.stage(), CableCheck::Abort { reported: false }));
        assert!(expired.contains(&Effect::SupplyOff), "got {expired:?}");
    }

    /// Replays the success sequence up to a chosen number of steps, so an unplug
    /// can be injected at every stage it passes through.
    fn stage_reached_by(steps: usize) -> Harness {
        let mut h = Harness::new(all_options());
        for step in 0..steps {
            match step {
                0 => h.begin(400.0),
                1 => h.voltage(10.0),
                2 => h.voltage(48.0),
                3 => h.self_test_passed(),
                4 => h.contactor_closed(),
                5 => h.voltage(450.0),
                _ => h.isolation(500_000.0),
            };
        }
        h
    }

    #[test]
    fn an_unplug_at_every_cable_check_stage_aborts_and_removes_energy() {
        let expected = [
            CableCheck::AwaitSafeVoltage,
            CableCheck::RelaysOpenSelfTest,
            CableCheck::RelaysOpenSelfTest,
            CableCheck::AwaitContactorClosed,
            CableCheck::RampUp,
            CableCheck::Sampling { taken: 0 },
            CableCheck::Sampling { taken: 1 },
            CableCheck::Sampling { taken: 2 },
            CableCheck::RampDown,
        ];

        for (steps, stage) in expected.iter().enumerate() {
            let mut h = stage_reached_by(steps + 1);
            assert_eq!(h.stage(), *stage, "after {} steps", steps + 1);

            let effects = h.unplug();
            assert!(
                matches!(h.stage(), CableCheck::Abort { reported: false }),
                "unplug in {stage:?} must abort, stage is {:?}",
                h.stage()
            );
            assert!(
                effects.contains(&Effect::SupplyOff),
                "unplug in {stage:?} must remove energy: {effects:?}"
            );
            assert!(
                effects.contains(&Effect::AllowPowerOn(false)),
                "unplug in {stage:?} must release the contactor: {effects:?}"
            );
            assert!(
                effects.contains(&Effect::ImdStop),
                "unplug in {stage:?} must stop the isolation monitor: {effects:?}"
            );
            let supply_off_at = effects.iter().position(|e| *e == Effect::SupplyOff);
            let report_at = effects
                .iter()
                .position(|e| matches!(e, Effect::HlcUpdate(HlcUpdate::CableCheckFinished(_))));
            assert!(
                report_at.is_none() || supply_off_at < report_at,
                "energy is removed before any failure is reported"
            );
        }
    }

    #[test]
    fn driving_to_safe_state_stops_the_sequence_from_advancing() {
        // A raised error reaches the path as `to_safe_state` only. Without the
        // sequence aborting with it, the next isolation sample would carry on
        // and could report success on a cable that was just de-energized.
        let mut h = stage_reached_by(6);
        assert!(matches!(h.stage(), CableCheck::Sampling { taken: 0 }));

        h.dc.to_safe_state();
        assert!(matches!(h.stage(), CableCheck::Abort { reported: false }));

        let effects = h.isolation(500_000.0);
        assert!(
            effects.is_empty(),
            "the sequence must not advance after safe state: {effects:?}"
        );

        let effects = h.voltage(20.0);
        assert_eq!(
            effects,
            vec![
                // The abort's own bound on this wait, given back.
                Effect::CancelTimer {
                    id: TIMER_CABLE_CHECK
                },
                Effect::HlcUpdate(HlcUpdate::CableCheckFinished(false)),
            ]
        );
    }

    #[test]
    fn an_unplug_reports_failure_once_the_cable_is_de_energized() {
        let mut h = stage_reached_by(6);
        h.unplug();
        assert!(matches!(h.stage(), CableCheck::Abort { reported: false }));

        let effects = h.voltage(20.0);
        assert_eq!(
            effects,
            vec![
                // The abort's own bound on this wait, given back.
                Effect::CancelTimer {
                    id: TIMER_CABLE_CHECK
                },
                Effect::HlcUpdate(HlcUpdate::CableCheckFinished(false)),
            ]
        );
    }

    #[test]
    fn isolation_sampling_completes_after_the_configured_count() {
        let mut dc = dc_port(config(), true, false);
        dc.cable_check = CableCheck::Sampling { taken: 0 };
        let s = session();

        for _ in 0..2 {
            dc.on_path_event(
                &s,
                PathEvent::Isolation(IsolationReading { resistance_ohm: 500_000.0, ..Default::default() }),
                now(),
            );
        }
        assert!(matches!(
            dc.cable_check_stage(),
            CableCheck::Sampling { taken: 2 }
        ));

        let effects = dc.on_path_event(
            &s,
            PathEvent::Isolation(IsolationReading { resistance_ohm: 500_000.0, ..Default::default() }),
            now(),
        );
        assert_eq!(dc.cable_check_stage(), CableCheck::RampDown);
        assert!(effects.contains(&Effect::SupplyOff));
    }

    /// The enforced limits a session carries. They are the **AC** limits and no
    /// DC clamp reads them, which is why every value here is irrelevant to the
    /// DC target: the DC clamp reads the ISO limit set that
    /// `set_evse_hlc_limits` installs.
    fn charging_session(max_current_a: f64) -> Session {
        Session::new(
            PwmStart::Nominal,
            Limits {
                max_current_a,
                nr_of_phases_available: 3,
            },
        )
    }

    /// The EVSE limit set a supply of some size implies, with enough headroom
    /// that it is not itself the clamp under test.
    fn roomy_hlc_limits() -> (MaximumLimits, MinimumLimits) {
        (
            MaximumLimits {
                maximum_current_a: 400.0,
                maximum_voltage_v: 950.0,
                maximum_power_w: 300_000.0,
                maximum_discharge_current_a: Some(300.0),
                maximum_discharge_power_w: Some(250_000.0),
            },
            MinimumLimits {
                minimum_current_a: 2.0,
                minimum_voltage_v: 150.0,
                minimum_power_w: 300.0,
                minimum_discharge_current_a: Some(4.0),
                minimum_discharge_power_w: Some(800.0),
            },
        )
    }

    /// Confirms the relays closed, which is what `CPEvent::PowerOn` carries.
    ///
    /// Every route into a charge loop passes it: the cable check waits for the
    /// confirmation before it tests the isolation
    /// (`EvseManager.cpp:2149-2160`), and the supply is switched on only with
    /// `contactor_open` false (`:2662`). A fixture that skips it holds a state
    /// no session reaches, and since that gate is ported such a fixture cannot
    /// energize at all, so the tests below say the relays are closed rather
    /// than leaving it to be assumed.
    fn relays_confirmed_closed(dc: &mut Dc, session: &Session) {
        dc.on_bsp(
            session,
            &BspEvent::Cp(CpEvent::PowerOn),
            CpEdges::default(),
            now(),
        );
    }

    fn charging_dc() -> Dc {
        let mut dc = dc_port(deriving_config(), true, true);
        dc.set_ev_maximum_limits(EvMaximumLimits {
            maximum_voltage_v: Some(900.0),
            ..EvMaximumLimits::default()
        });
        dc.set_evse_max_export_voltage_v(950.0);
        let (maximum, minimum) = roomy_hlc_limits();
        dc.set_evse_hlc_limits(maximum, minimum);
        dc
    }

    /// The isolation monitor runs on into the session, and
    /// `subscribe_isolation_measurement` acts on what it reports while the
    /// state is `Charging`: the status goes to the vehicle every time and a
    /// resistance under the threshold raises `MREC22ResistanceFault` under the
    /// `Resistance` sub type. The cable check is long over by then, so this is
    /// the only thing still watching the cable.
    #[test]
    fn a_resistance_drop_while_charging_raises_the_resistance_fault() {
        let mut dc = charging_dc();
        dc.progress.enter(AcState::PrepareCharging);
        dc.on_path_event(&charging_session(32.0), PathEvent::CurrentDemandStarted, now());
        assert_eq!(dc.progress.state(), AcState::Charging);

        let healthy = dc.on_path_event(
            &charging_session(32.0),
            PathEvent::Isolation(IsolationReading { resistance_ohm: 500_000.0, ..Default::default() }),
            now(),
        );
        assert!(
            healthy.contains(&Effect::HlcUpdate(HlcUpdate::IsolationStatus(
                IsolationStatus::Valid
            ))),
            "the vehicle is told on every measurement, got {healthy:?}"
        );
        assert!(
            !healthy
                .iter()
                .any(|e| matches!(e, Effect::RaiseError(_))),
            "a resistance in range raises nothing, got {healthy:?}"
        );

        let dropped = dc.on_path_event(
            &charging_session(32.0),
            PathEvent::Isolation(IsolationReading { resistance_ohm: 1.0, ..Default::default() }),
            now(),
        );

        assert!(
            dropped.contains(&Effect::HlcUpdate(HlcUpdate::IsolationStatus(
                IsolationStatus::Fault
            ))),
            "got {dropped:?}"
        );
        let raised: Vec<&ErrorReport> = dropped
            .iter()
            .filter_map(|e| match e {
                Effect::RaiseError(report) => Some(report),
                _ => None,
            })
            .collect();
        assert_eq!(raised.len(), 1, "got {dropped:?}");
        assert_eq!(raised[0].error_type, faults::MREC22_RESISTANCE);
        assert_eq!(raised[0].sub_type, "Resistance");
        assert_eq!(raised[0].severity, Severity::Medium);
    }

    fn earth_reading(voltage_v: f64, l1e_v: f64, l2e_v: f64) -> IsolationReading {
        IsolationReading {
            resistance_ohm: 500_000.0,
            voltage_v: Some(voltage_v),
            voltage_to_earth_l1e_v: Some(l1e_v),
            voltage_to_earth_l2e_v: Some(l2e_v),
        }
    }

    fn charging_dc_watching_earth() -> Dc {
        let mut dc = charging_dc();
        dc.evse_min_export_voltage_v = 150.0;
        dc.progress.enter(AcState::PrepareCharging);
        dc.on_path_event(&charging_session(32.0), PathEvent::CurrentDemandStarted, now());
        assert_eq!(dc.progress.state(), AcState::Charging);
        dc
    }

    fn earth_faults(effects: &[Effect]) -> usize {
        effects
            .iter()
            .filter(|e| {
                matches!(e, Effect::RaiseError(report)
                    if report.sub_type == "VoltageToEarth")
            })
            .count()
    }

    /// IEC 61851-23:2023 6.3.1.112.2 as the C++ debounces it: two readings out
    /// of range AND at least two seconds between the first and the last. Each
    /// half alone raises nothing.
    #[test]
    fn a_burst_of_voltage_to_earth_failures_inside_the_window_raises_nothing() {
        let mut dc = charging_dc_watching_earth();
        let t0 = now();
        let session = charging_session(32.0);

        // Ten failures inside one second. The count is met many times over and
        // the elapsed time never is.
        let mut raised = 0;
        for step in 0..10 {
            let at = t0 + Duration::from_millis(step * 100);
            raised += earth_faults(&dc.on_path_event(
                &session,
                PathEvent::Isolation(earth_reading(400.0, 900.0, 900.0)),
                at,
            ));
        }

        assert_eq!(raised, 0, "a burst inside the window is not a fault");
    }

    #[test]
    fn a_slow_drift_out_of_range_raises_the_voltage_to_earth_fault() {
        let mut dc = charging_dc_watching_earth();
        let t0 = now();
        let session = charging_session(32.0);
        let out_of_range = earth_reading(400.0, 900.0, 900.0);

        let first = dc.on_path_event(&session, PathEvent::Isolation(out_of_range), t0);
        assert_eq!(earth_faults(&first), 0, "one reading is not a relationship");

        let second = dc.on_path_event(
            &session,
            PathEvent::Isolation(out_of_range),
            t0 + Duration::from_secs(2),
        );

        assert_eq!(earth_faults(&second), 1, "got {second:?}");
    }

    #[test]
    fn a_reading_back_in_range_abandons_the_run() {
        let mut dc = charging_dc_watching_earth();
        let t0 = now();
        let session = charging_session(32.0);
        let out_of_range = earth_reading(400.0, 900.0, 900.0);

        dc.on_path_event(&session, PathEvent::Isolation(out_of_range), t0);
        // In range: the run so far is abandoned, so the failure after it is a
        // first failure again and has nothing to be two seconds after.
        dc.on_path_event(
            &session,
            PathEvent::Isolation(earth_reading(400.0, 10.0, 10.0)),
            t0 + Duration::from_secs(1),
        );

        let after = dc.on_path_event(
            &session,
            PathEvent::Isolation(out_of_range),
            t0 + Duration::from_secs(3),
        );

        assert_eq!(
            earth_faults(&after),
            0,
            "the clock restarts with the run, got {after:?}"
        );
    }

    /// A reading that cannot be checked answers "in range", and the C++ takes
    /// that answer at the same place it takes a real one: it resets the failure
    /// counter. So a measurement with a field missing does not merely fail to
    /// raise, it abandons the run in progress.
    #[test]
    fn an_uncheckable_reading_abandons_the_run_as_an_in_range_one_does() {
        let mut dc = charging_dc_watching_earth();
        let t0 = now();
        let session = charging_session(32.0);
        let out_of_range = earth_reading(400.0, 900.0, 900.0);
        let uncheckable = IsolationReading {
            voltage_to_earth_l1e_v: None,
            ..out_of_range
        };

        dc.on_path_event(&session, PathEvent::Isolation(out_of_range), t0);
        dc.on_path_event(
            &session,
            PathEvent::Isolation(uncheckable),
            t0 + Duration::from_secs(1),
        );

        let after = dc.on_path_event(
            &session,
            PathEvent::Isolation(out_of_range),
            t0 + Duration::from_secs(3),
        );

        assert_eq!(earth_faults(&after), 0, "got {after:?}");
    }

    /// What cannot be checked is not a failure, and a voltage at or below the
    /// supply's minimum export voltage is not trusted: the monitor may be
    /// reporting earth readings against a voltage already ramped down.
    #[test]
    fn an_uncheckable_or_ramped_down_reading_is_in_range() {
        let mut dc = charging_dc_watching_earth();
        let t0 = now();
        let session = charging_session(32.0);

        let absent = IsolationReading {
            resistance_ohm: 500_000.0,
            voltage_v: Some(400.0),
            voltage_to_earth_l1e_v: None,
            voltage_to_earth_l2e_v: Some(900.0),
        };
        for step in 0..4 {
            let at = t0 + Duration::from_secs(step * 2);
            assert_eq!(
                earth_faults(&dc.on_path_event(&session, PathEvent::Isolation(absent), at)),
                0,
                "a reading that cannot be checked is not a failure"
            );
        }

        // Below the minimum export voltage, with both earth readings wild.
        for step in 0..4 {
            let at = t0 + Duration::from_secs(20 + step * 2);
            assert_eq!(
                earth_faults(&dc.on_path_event(
                    &session,
                    PathEvent::Isolation(earth_reading(100.0, 900.0, 900.0)),
                    at
                )),
                0,
                "below the minimum export voltage the earth readings are not trusted"
            );
        }
    }

    /// The ceiling follows the measured voltage above a 500 V rating and is the
    /// standard's fixed 550 V at or below it.
    #[test]
    fn the_ceiling_follows_the_rating() {
        let session = charging_session(32.0);
        let t0 = now();

        // Rated above 500: the ceiling is 110% of the measured voltage, so 430
        // is in range against 400 and 450 is not.
        let mut high = charging_dc_watching_earth();
        assert!(high.voltage_to_earth_in_range(&earth_reading(400.0, 430.0, 430.0)));
        assert!(!high.voltage_to_earth_in_range(&earth_reading(400.0, 450.0, 450.0)));
        let _ = high.on_path_event(&session, PathEvent::Isolation(earth_reading(400.0, 430.0, 430.0)), t0);

        // Rated at or below 500: the fixed 550 stands, so 540 is in range even
        // though it is far above 110% of the measured voltage.
        let mut low = charging_dc_watching_earth();
        low.set_evse_max_export_voltage_v(500.0);
        assert!(low.voltage_to_earth_in_range(&earth_reading(400.0, 540.0, 540.0)));
        assert!(!low.voltage_to_earth_in_range(&earth_reading(400.0, 560.0, 560.0)));
    }

    /// The software watchdog end to end through the path: the thresholds reach
    /// it from the vehicle's maximum limits, the current demand starts it, and
    /// a reading over the emergency limit raises MREC5OverVoltage at the
    /// emergency severity.
    ///
    /// The state machine's own rules are covered in `core::path::over_voltage`.
    /// What this covers is that it is wired to anything at all: it was fed by a
    /// callback that discarded every reading.
    #[test]
    fn the_software_watchdog_raises_on_a_voltage_over_the_emergency_limit() {
        let session = charging_session(32.0);
        let mut dc = charging_dc();
        dc.progress.enter(AcState::PrepareCharging);

        // The same arm that tells the hardware monitor its limits.
        let limits = dc.on_path_event(
            &session,
            PathEvent::DcEvMaximumLimits(EvMaximumLimits {
                maximum_voltage_v: Some(500.0),
                ..EvMaximumLimits::default()
            }),
            now(),
        );
        assert!(
            limits
                .iter()
                .any(|e| matches!(e, Effect::OverVoltageLimits(_))),
            "the hardware monitor is still told, got {limits:?}"
        );

        dc.on_path_event(&session, PathEvent::CurrentDemandStarted, now());

        // Below both limits: nothing.
        let quiet = dc.on_path_event(
            &session,
            PathEvent::OverVoltageMeasurement { voltage_v: 400.0 },
            now(),
        );
        assert!(
            !quiet.iter().any(|e| matches!(e, Effect::RaiseError(_))),
            "got {quiet:?}"
        );

        // The emergency limit for a 500 V negotiated maximum is 550 V.
        let over = dc.on_path_event(
            &session,
            PathEvent::OverVoltageMeasurement { voltage_v: 600.0 },
            now(),
        );

        let raised: Vec<&ErrorReport> = over
            .iter()
            .filter_map(|e| match e {
                Effect::RaiseError(report) => Some(report),
                _ => None,
            })
            .collect();
        assert_eq!(raised.len(), 1, "got {over:?}");
        assert_eq!(raised[0].error_type, faults::MREC5_OVER_VOLTAGE);
        assert_eq!(raised[0].severity, Severity::High);
    }

    /// The plausibility comparison end to end: three of the four instruments
    /// reporting through their own path events, a disagreement beyond the
    /// threshold arming the deadline, and the deadline raising.
    ///
    /// The comparison's own rules are covered in `core::path::plausibility`.
    /// What this covers is that each instrument is wired to it: the meter's DC
    /// voltage did not reach the path at all before, and the monitor's own
    /// voltage was dropped at intake.
    #[test]
    fn the_instruments_disagreeing_raise_the_plausibility_fault() {
        let session = charging_session(32.0);
        let mut dc = charging_dc();
        dc.config.plausibility_max_spread_v = 50.0;
        dc.config.plausibility_fault_duration = Duration::from_secs(2);
        dc.plausibility =
            plausibility::Plausibility::new(50.0, dc.config.plausibility_fault_duration);
        dc.progress.enter(AcState::PrepareCharging);
        dc.on_path_event(&session, PathEvent::CurrentDemandStarted, now());

        // One instrument alone disagrees with nothing.
        let supply = dc.on_path_event(
            &session,
            PathEvent::SupplyVoltage { voltage_v: 400.0 },
            now(),
        );
        assert!(!armed_plausibility(&supply), "got {supply:?}");

        // The meter agrees closely enough.
        let meter = dc.on_path_event(&session, PathEvent::MeterVoltage { voltage_v: 420.0 }, now());
        assert!(!armed_plausibility(&meter), "got {meter:?}");

        // The isolation monitor does not, and the spread is taken across all
        // three rather than between the last two.
        let imd = dc.on_path_event(
            &session,
            PathEvent::Isolation(IsolationReading {
                resistance_ohm: 500_000.0,
                voltage_v: Some(600.0),
                ..Default::default()
            }),
            now(),
        );
        assert!(armed_plausibility(&imd), "got {imd:?}");

        let expired = dc.on_timer(&session, TIMER_PLAUSIBILITY, now());

        let raised: Vec<&ErrorReport> = expired
            .iter()
            .filter_map(|e| match e {
                Effect::RaiseError(report) => Some(report),
                _ => None,
            })
            .collect();
        assert_eq!(raised.len(), 1, "got {expired:?}");
        assert_eq!(raised[0].error_type, faults::VOLTAGE_PLAUSIBILITY);
        assert_eq!(raised[0].severity, Severity::High);
    }

    fn plausibility_dc() -> Dc {
        let mut dc = charging_dc();
        dc.config.plausibility_max_spread_v = 50.0;
        dc.config.plausibility_fault_duration = Duration::from_secs(2);
        dc.plausibility = plausibility::Plausibility::new(50.0, Duration::from_secs(2));
        dc.progress.enter(AcState::PrepareCharging);
        dc.on_path_event(&charging_session(32.0), PathEvent::CurrentDemandStarted, now());
        dc
    }

    fn imd_at(voltage_v: f64) -> PathEvent {
        PathEvent::Isolation(IsolationReading {
            resistance_ohm: 500_000.0,
            voltage_v: Some(voltage_v),
            ..Default::default()
        })
    }

    /// Each instrument is checked against one partner only, so the arming
    /// depends on that instrument having been reported. A wider set would still
    /// disagree without it and the test would pass with the feed removed.
    #[test]
    fn the_supply_is_one_of_the_compared_instruments() {
        let session = charging_session(32.0);
        let mut dc = plausibility_dc();

        // The monitor alone has no partner.
        let alone = dc.on_path_event(&session, imd_at(600.0), now());
        assert!(!armed_plausibility(&alone), "got {alone:?}");

        let paired = dc.on_path_event(
            &session,
            PathEvent::SupplyVoltage { voltage_v: 400.0 },
            now(),
        );

        assert!(armed_plausibility(&paired), "got {paired:?}");
    }

    #[test]
    fn the_billing_meter_is_one_of_the_compared_instruments() {
        let session = charging_session(32.0);
        let mut dc = plausibility_dc();

        let alone = dc.on_path_event(&session, imd_at(600.0), now());
        assert!(!armed_plausibility(&alone), "got {alone:?}");

        let paired =
            dc.on_path_event(&session, PathEvent::MeterVoltage { voltage_v: 400.0 }, now());

        assert!(armed_plausibility(&paired), "got {paired:?}");
    }

    fn armed_plausibility(effects: &[Effect]) -> bool {
        effects.iter().any(|e| {
            matches!(e, Effect::StartTimer { id, .. } if *id == TIMER_PLAUSIBILITY)
        })
    }

    fn setpoint_current(effects: &[Effect]) -> Option<f64> {
        effects.iter().rev().find_map(|e| match e {
            Effect::SetSupplySetpoint { current_a, .. } => Some(*current_a),
            _ => None,
        })
    }

    fn index_of_mode(effects: &[Effect]) -> Option<usize> {
        effects
            .iter()
            .position(|e| matches!(e, Effect::SetSupplyMode { .. }))
    }

    fn index_of_setpoint(effects: &[Effect]) -> Option<usize> {
        effects
            .iter()
            .position(|e| matches!(e, Effect::SetSupplySetpoint { .. }))
    }

    #[test]
    fn the_setpoint_precedes_the_supply_mode_when_energizing_from_off() {
        // The sibling of `supply_mode_precedes_the_setpoint`, which pins the
        // direction switch. Energizing is the opposite order because the supply
        // resets its internal targets on the off transition, so switching on
        // first energizes the cable at a target nobody chose.
        let mut dc = dc_port(config(), true, false);
        let effects = dc.energize(400.0, 10.0, SupplyMode::Export);

        assert!(
            index_of_setpoint(&effects).unwrap() < index_of_mode(&effects).unwrap(),
            "the target must be written before the supply is switched on: {effects:?}"
        );
    }

    #[test]
    fn switching_the_supply_off_between_targets_energizes_setpoint_first_again() {
        let mut dc = charging_dc();
        let session = charging_session(200.0);
        let t0 = now();

        relays_confirmed_closed(&mut dc, &session);
        dc.set_ev_target(&session, 400.0, 50.0, t0);
        dc.supply_off();

        let effects = dc.set_ev_target(&session, 400.0, 50.0, t0 + Duration::from_secs(1));
        assert!(
            index_of_setpoint(&effects).unwrap() < index_of_mode(&effects).unwrap(),
            "an off makes the next target an energize again: {effects:?}"
        );
    }

    /// `EvseManager.cpp:2659-2662`. The target is written whatever the relays
    /// are doing and the supply is switched **on** only with them confirmed
    /// closed, so a vehicle target that arrives with the contactor open leaves
    /// the supply holding a target and off.
    #[test]
    fn a_target_with_the_relays_open_writes_the_target_and_does_not_energize() {
        let mut dc = charging_dc();
        let session = charging_session(200.0);

        let effects = dc.set_ev_target(&session, 400.0, 50.0, now());

        assert_eq!(
            effects,
            vec![Effect::SetSupplySetpoint {
                mode: SupplyMode::Export,
                voltage_v: 400.0,
                current_a: 50.0,
            }],
            "the target alone, with no mode change behind it"
        );
        assert_eq!(
            dc.supply_mode(),
            SupplyMode::Off,
            "an open contactor is not energized"
        );
    }

    /// The gate lifts on the confirmation and not on anything else, so the
    /// first target after it is an energize with the usual setpoint first
    /// ordering.
    #[test]
    fn the_first_target_after_the_relays_close_energizes() {
        let mut dc = charging_dc();
        let session = charging_session(200.0);
        let t0 = now();
        dc.set_ev_target(&session, 400.0, 50.0, t0);

        relays_confirmed_closed(&mut dc, &session);
        let effects = dc.set_ev_target(&session, 400.0, 40.0, t0 + Duration::from_secs(10));

        assert_eq!(dc.supply_mode(), SupplyMode::Export);
        assert!(
            index_of_setpoint(&effects).unwrap() < index_of_mode(&effects).unwrap(),
            "the target must be written before the supply is switched on: {effects:?}"
        );
    }

    /// The defect this gate closes. The relays open under a running charge and
    /// the supply is switched off with them; the vehicle is still asking for a
    /// target, and that target must not bring the supply back with nothing
    /// behind the contactor.
    #[test]
    fn a_target_after_the_relays_open_cannot_energize_the_open_contactor() {
        let mut dc = charging_dc();
        let session = charging_session(200.0);
        let t0 = now();
        relays_confirmed_closed(&mut dc, &session);
        dc.on_path_event(&session, PathEvent::CurrentDemandStarted, t0);
        dc.set_ev_target(&session, 400.0, 40.0, t0);
        assert_eq!(dc.supply_mode(), SupplyMode::Export, "the control");

        dc.on_bsp(
            &session,
            &BspEvent::Cp(CpEvent::PowerOff),
            CpEdges::default(),
            t0 + Duration::from_millis(100),
        );
        dc.supply_off();

        let effects = dc.set_ev_target(&session, 400.0, 40.0, t0 + Duration::from_secs(1));

        assert_eq!(
            dc.supply_mode(),
            SupplyMode::Off,
            "the supply stays off: {effects:?}"
        );
        assert!(
            !effects
                .iter()
                .any(|effect| matches!(effect, Effect::SetSupplyMode { .. })),
            "no mode change reaches the supply: {effects:?}"
        );
    }

    /// The other half of the C++ `PowerOff` arm (`:1156-1159`): both held
    /// targets are dropped with the contactor. The voltage is the one that
    /// matters, because it is the cache a vehicle sending a zero target is
    /// given in its place, so dropping it means such a target writes nothing
    /// at all rather than re-writing the voltage of the charge that just
    /// ended.
    #[test]
    fn the_relays_opening_drops_the_held_voltage_a_zero_target_would_reuse() {
        let mut dc = charging_dc();
        let session = charging_session(200.0);
        let t0 = now();
        relays_confirmed_closed(&mut dc, &session);
        dc.set_ev_target(&session, 400.0, 40.0, t0);
        assert_eq!(dc.target_voltage_v(), 400.0, "the control");

        dc.on_bsp(
            &session,
            &BspEvent::Cp(CpEvent::PowerOff),
            CpEdges::default(),
            t0 + Duration::from_millis(100),
        );
        assert_eq!(dc.target_voltage_v(), 0.0);

        let effects = dc.set_ev_target(&session, 0.0, 40.0, t0 + Duration::from_secs(1));

        assert!(
            !effects
                .iter()
                .any(|effect| matches!(effect, Effect::SetSupplySetpoint { .. })),
            "no voltage to reuse, so nothing is written: {effects:?}"
        );
    }

    #[test]
    fn a_direction_switch_on_a_live_supply_precedes_the_setpoint_that_follows_it() {
        let mut dc = charging_dc();
        let mut session = charging_session(200.0);
        session.profile.bidirectional = true;
        let t0 = now();

        relays_confirmed_closed(&mut dc, &session);
        dc.set_ev_target(&session, 400.0, 50.0, t0);
        dc.on_path_event(&session, PathEvent::CurrentDemandStarted, t0);
        dc.set_exporting_to_grid(true);

        let effects = dc.set_ev_target(&session, 400.0, 40.0, t0 + Duration::from_secs(1));

        assert_eq!(dc.supply_mode(), SupplyMode::Import);
        assert!(
            index_of_mode(&effects).unwrap() < index_of_setpoint(&effects).unwrap(),
            "the supply must know the direction before the target: {effects:?}"
        );
    }

    /// ADR-0018, and a deliberate divergence from the C++.
    ///
    /// The supply withdraws its bidirectional capability while a discharge is
    /// running. The discharge is taken to zero **in the import direction** and
    /// only then may the supply be turned round, so the cable is never reversed
    /// under load, and no later target may discharge again.
    mod bidirectional_withdrawal {
        use super::*;

        /// A live discharge: current demand running, exporting to grid, a
        /// bidirectional session and a non zero import target applied.
        fn a_running_discharge() -> (Dc, Session, Instant) {
            let mut dc = charging_dc();
            let mut session = charging_session(200.0);
            session.profile.bidirectional = true;
            let t0 = now();

            relays_confirmed_closed(&mut dc, &session);
            dc.on_path_event(&session, PathEvent::CurrentDemandStarted, t0);
            dc.set_exporting_to_grid(true);
            dc.set_ev_target(&session, 400.0, 40.0, t0);
            assert_eq!(dc.supply_mode(), SupplyMode::Import, "the control");

            (dc, session, t0)
        }

        #[test]
        fn the_withdrawal_takes_the_discharge_to_zero_before_turning_the_supply_round() {
            let (mut dc, mut session, t0) = a_running_discharge();

            // The core resolves the fact once and routes the withdrawal; the
            // path is told both in that order.
            session.profile.bidirectional = false;
            let ramp_down = dc.on_path_event(
                &session,
                PathEvent::BidirectionalWithdrawn,
                t0 + Duration::from_millis(100),
            );

            assert_eq!(
                ramp_down,
                vec![Effect::SetSupplySetpoint {
                    mode: SupplyMode::Import,
                    voltage_v: 400.0,
                    current_a: 0.0,
                }],
                "the discharge falls to zero and the direction is untouched"
            );
            assert_eq!(
                dc.supply_mode(),
                SupplyMode::Import,
                "the supply is not turned round while current was flowing"
            );
        }

        #[test]
        fn bidirectional_withdrawal_calls_the_import_zero_api() {
            use crate::boundary::supply::tests::RecordingSupply;

            let (mut dc, mut session, t0) = a_running_discharge();
            session.profile.bidirectional = false;
            let effects = dc.on_path_event(
                &session,
                PathEvent::BidirectionalWithdrawn,
                t0 + Duration::from_millis(100),
            );
            let supply = RecordingSupply::default();
            supply.apply_setpoints(effects);
            assert_eq!(supply.calls(), vec![(SupplyMode::Import, 0.0, 400.0)]);
        }

        #[test]
        fn the_next_target_after_the_ramp_down_charges_however_the_vehicle_asks() {
            let (mut dc, mut session, t0) = a_running_discharge();
            session.profile.bidirectional = false;
            dc.on_path_event(
                &session,
                PathEvent::BidirectionalWithdrawn,
                t0 + Duration::from_millis(100),
            );

            let effects = dc.set_ev_target(&session, 400.0, 40.0, t0 + Duration::from_secs(1));

            assert_eq!(
                dc.supply_mode(),
                SupplyMode::Export,
                "the session continues unidirectionally"
            );
            assert!(
                index_of_mode(&effects).unwrap() < index_of_setpoint(&effects).unwrap(),
                "the turn round happens at zero current, before the new target: {effects:?}"
            );
        }

        /// The ramp position is reset with the discharge, so the charge that
        /// follows ramps up from zero rather than reaching the supply at the
        /// magnitude the discharge was running at.
        ///
        /// The configured rate is 20 A/s and a second passes, so a request for
        /// 40 A reaches the supply at 20 A. Without the reset the ramp would
        /// already be at 40 and the difference would be zero, so the full 40 A
        /// would be written in one step, in the direction just reversed.
        #[test]
        fn the_charge_after_a_withdrawal_ramps_up_from_zero() {
            let (mut dc, mut session, t0) = a_running_discharge();
            session.profile.bidirectional = false;
            dc.on_path_event(
                &session,
                PathEvent::BidirectionalWithdrawn,
                t0 + Duration::from_millis(100),
            );

            let effects = dc.set_ev_target(&session, 400.0, 40.0, t0 + Duration::from_secs(1));

            assert!(
                effects.contains(&Effect::SetSupplySetpoint {
                    mode: SupplyMode::Export,
                    voltage_v: 400.0,
                    current_a: 20.0,
                }),
                "one second at 20 A/s from zero: {effects:?}"
            );
        }

        /// The refusal is the fact ADR-0018 names: no new discharge for the
        /// rest of the session. It is enforced by the resolution, which the
        /// core recomputes and the path reads, so a path handed a session that
        /// still claims to be bidirectional would import again. This asserts
        /// the path half; `hlc::bpt` asserts the resolution half.
        #[test]
        fn a_later_export_to_grid_does_not_discharge_again() {
            let (mut dc, mut session, t0) = a_running_discharge();
            session.profile.bidirectional = false;
            dc.on_path_event(
                &session,
                PathEvent::BidirectionalWithdrawn,
                t0 + Duration::from_millis(100),
            );

            dc.set_exporting_to_grid(false);
            dc.set_ev_target(&session, 400.0, 40.0, t0 + Duration::from_secs(1));
            dc.set_exporting_to_grid(true);
            dc.set_ev_target(&session, 400.0, 40.0, t0 + Duration::from_secs(2));

            assert_eq!(
                dc.supply_mode(),
                SupplyMode::Export,
                "an export to grid on a withdrawn session is not a discharge"
            );
        }

        /// The resume half of ADR-0018 as revised: the capability returns, the
        /// core restores the fact, and the discharge comes back **ramped**
        /// rather than stepping from zero to the magnitude it was running at.
        ///
        /// The ramp is the C++'s own (`EvseManager.cpp:2681-2703` rate limits
        /// every rise in the target magnitude, whichever direction the supply
        /// is in), so this is the ported behaviour and not a second ramp added
        /// beside it. What the withdrawal contributes is the zero the rise
        /// starts from.
        ///
        /// 20 A/s configured and one second elapsed, so a request for 40 A
        /// reaches the supply at 20 A in the import direction; the second
        /// target a second later carries the rest.
        #[test]
        fn the_discharge_resumes_ramped_when_the_capability_returns() {
            let (mut dc, mut session, t0) = a_running_discharge();
            session.profile.bidirectional = false;
            dc.on_path_event(
                &session,
                PathEvent::BidirectionalWithdrawn,
                t0 + Duration::from_millis(100),
            );

            // What `Core::on_supply_capabilities` does with a report that
            // carries the capability again.
            session.profile.bidirectional = true;

            let resumed = dc.set_ev_target(&session, 400.0, 40.0, t0 + Duration::from_secs(1));
            assert!(
                resumed.contains(&Effect::SetSupplySetpoint {
                    mode: SupplyMode::Import,
                    voltage_v: 400.0,
                    current_a: 20.0,
                }),
                "one second at 20 A/s from the zero the withdrawal left: {resumed:?}"
            );

            let further = dc.set_ev_target(&session, 400.0, 40.0, t0 + Duration::from_secs(2));
            assert!(
                further.contains(&Effect::SetSupplySetpoint {
                    mode: SupplyMode::Import,
                    voltage_v: 400.0,
                    current_a: 40.0,
                }),
                "the second second carries the rest: {further:?}"
            );
        }

        /// A withdrawal on a session that was not discharging costs nothing.
        /// The C++ has no such transition at all, so the cheapest wrong thing
        /// this could do is actuate a supply that was charging normally.
        #[test]
        fn a_withdrawal_while_charging_normally_actuates_nothing() {
            let mut dc = charging_dc();
            let session = charging_session(200.0);
            let t0 = now();

            relays_confirmed_closed(&mut dc, &session);
            dc.on_path_event(&session, PathEvent::CurrentDemandStarted, t0);
            dc.set_ev_target(&session, 400.0, 40.0, t0);
            assert_eq!(dc.supply_mode(), SupplyMode::Export, "the control");

            let effects = dc.on_path_event(
                &session,
                PathEvent::BidirectionalWithdrawn,
                t0 + Duration::from_millis(100),
            );

            assert!(effects.is_empty(), "nothing to ramp down: {effects:?}");
            assert_eq!(dc.supply_mode(), SupplyMode::Export);
        }

        /// A withdrawal reaching a supply that is off, which is the other
        /// half of the guard: precharge and cable check run before current
        /// demand, so a session can be bidirectional with nothing energized.
        #[test]
        fn a_withdrawal_on_a_supply_that_is_off_actuates_nothing() {
            let mut dc = charging_dc();
            let mut session = charging_session(200.0);
            session.profile.bidirectional = true;
            let t0 = now();

            assert_eq!(dc.supply_mode(), SupplyMode::Off, "the control");

            let effects = dc.on_path_event(&session, PathEvent::BidirectionalWithdrawn, t0);

            assert!(effects.is_empty(), "nothing is energized: {effects:?}");
            assert_eq!(dc.supply_mode(), SupplyMode::Off);
        }

        /// Exporting to grid without a running current demand keeps the supply
        /// in the export direction (`EvseManager.cpp:2334-2336` needs all
        /// three), so a withdrawal there has no discharge to stop either.
        /// Drives the combination the two guard tests above do not: the export
        /// flag set and the current demand absent.
        #[test]
        fn a_withdrawal_while_exporting_before_current_demand_actuates_nothing() {
            let mut dc = charging_dc();
            let mut session = charging_session(200.0);
            session.profile.bidirectional = true;
            let t0 = now();

            relays_confirmed_closed(&mut dc, &session);
            dc.set_exporting_to_grid(true);
            dc.set_ev_target(&session, 400.0, 40.0, t0);
            assert_eq!(
                dc.supply_mode(),
                SupplyMode::Export,
                "no current demand, so no import"
            );

            let effects = dc.on_path_event(&session, PathEvent::BidirectionalWithdrawn, t0);

            assert!(effects.is_empty(), "no discharge to stop: {effects:?}");
        }

        /// A repeated withdrawal must not re-emit a zero setpoint: the applied
        /// setpoint cache already holds one, and a second emission would look
        /// to a reader like a second event.
        #[test]
        fn a_repeated_withdrawal_emits_nothing_further() {
            let (mut dc, mut session, t0) = a_running_discharge();
            session.profile.bidirectional = false;
            dc.on_path_event(
                &session,
                PathEvent::BidirectionalWithdrawn,
                t0 + Duration::from_millis(100),
            );

            let again = dc.on_path_event(
                &session,
                PathEvent::BidirectionalWithdrawn,
                t0 + Duration::from_millis(200),
            );

            assert!(
                again.is_empty(),
                "the discharge is already at zero: {again:?}"
            );
        }
    }

    #[test]
    fn precharge_holds_the_supply_direction_at_export_even_while_discharging() {
        // Import needs current demand as well as an export to grid, and
        // precharge runs before current demand, so a precharge target is always
        // export. `EvseManager.cpp:2334-2336`.
        let mut dc = charging_dc();
        let mut session = charging_session(200.0);
        session.profile.bidirectional = true;
        dc.set_exporting_to_grid(true);
        let t0 = now();

        relays_confirmed_closed(&mut dc, &session);

        let effects = dc.on_path_event(&session, PathEvent::PreChargeStarted, t0);
        assert!(effects.is_empty(), "precharge itself actuates nothing");

        dc.set_ev_target(&session, 400.0, 20.0, t0);
        assert_eq!(dc.supply_mode(), SupplyMode::Export);
    }

    /// A precharge notification arriving **after** current demand has already
    /// started must not take the session out of the import direction.
    ///
    /// The ordinary ISO 15118 order is cable check, precharge, current demand,
    /// so this is out of order on the wire, but it is representable and the C++
    /// tolerates it: `EvseManager.cpp:570-571` assigns the charging phase and
    /// nothing else, and the only writer that clears the running current demand
    /// is `subscribe_current_demand_finished` at `:593`. A port that also
    /// cleared it here would flip a discharging session back to export mid
    /// charge, and the direction is what decides which limit set the vehicle's
    /// request is clamped against.
    ///
    /// Drives the one combination the test above does not: the two events in
    /// the other order.
    #[test]
    fn a_precharge_after_current_demand_started_leaves_the_direction_alone() {
        let mut dc = charging_dc();
        let mut session = charging_session(200.0);
        session.profile.bidirectional = true;
        dc.set_exporting_to_grid(true);
        let t0 = now();

        relays_confirmed_closed(&mut dc, &session);
        dc.on_path_event(&session, PathEvent::CurrentDemandStarted, t0);
        dc.set_ev_target(&session, 400.0, -20.0, t0);
        assert_eq!(
            dc.supply_mode(),
            SupplyMode::Import,
            "a discharging session is importing before the precharge arrives"
        );

        let effects = dc.on_path_event(&session, PathEvent::PreChargeStarted, t0);
        assert!(effects.is_empty(), "precharge itself actuates nothing");

        dc.set_ev_target(&session, 400.0, -20.0, t0);
        assert_eq!(
            dc.supply_mode(),
            SupplyMode::Import,
            "and it is still importing afterwards"
        );
    }

    #[test]
    fn the_current_ramp_does_not_exceed_the_configured_rate() {
        // Twenty amperes per second, so one second of elapsed time may move the
        // delivered current by twenty amperes and no more.
        let mut dc = charging_dc();
        let session = charging_session(500.0);
        let t0 = now();

        dc.set_ev_target(&session, 400.0, 0.0, t0);

        let mut delivered = 0.0;
        for step in 1..=4u32 {
            let at = t0 + Duration::from_millis(500 * u64::from(step));
            let effects = dc.set_ev_target(&session, 400.0, 400.0, at);
            let next = setpoint_current(&effects).expect("a target reaches the supply");
            assert!(
                next - delivered <= 0.5 * 20.0 + f64::EPSILON,
                "step {step} moved the current by {} A in half a second",
                next - delivered
            );
            delivered = next;
        }
        assert_eq!(delivered, 40.0, "four half seconds at twenty amperes");
    }

    #[test]
    fn a_falling_target_reaches_the_supply_without_being_rate_limited() {
        // The C++ compares the signed difference against the allowance, so only
        // a rise is limited. `EvseManager.cpp:2648-2653`.
        let mut dc = charging_dc();
        let session = charging_session(500.0);
        let t0 = now();

        dc.set_ev_target(&session, 400.0, 0.0, t0);
        dc.set_ev_target(&session, 400.0, 400.0, t0 + Duration::from_secs(10));
        let effects = dc.set_ev_target(&session, 400.0, 5.0, t0 + Duration::from_millis(10_001));

        assert_eq!(setpoint_current(&effects), Some(5.0));
    }

    #[test]
    fn the_watchdog_re_applies_the_evse_limit_after_five_seconds_of_silence() {
        let mut dc = charging_dc();
        let session = charging_session(200.0);
        let t0 = now();

        dc.set_ev_target(&session, 400.0, 100.0, t0);
        let started = dc.on_path_event(&session, PathEvent::CurrentDemandStarted, t0);
        assert!(
            started.contains(&Effect::StartTimer {
                id: TIMER_ENFORCE_LIMITS,
                after: ENFORCE_TARGET_LIMITS_INTERVAL
            }),
            "current demand arms the re-apply watchdog: {started:?}"
        );

        // Energy management lowers the limit. Nothing is applied at that moment,
        // exactly as in the C++ where the new limit is only stored:
        // `energyImpl.cpp:669-670` informs the charger and the charger's
        // watchdog is what carries it to the supply.
        let (maximum, minimum) = roomy_hlc_limits();
        dc.set_evse_hlc_limits(
            MaximumLimits {
                maximum_current_a: 50.0,
                ..maximum
            },
            minimum,
        );
        assert!(dc.on_limits_changed(&session, t0).is_empty());

        let effects = dc.on_timer(
            &session,
            TIMER_ENFORCE_LIMITS,
            t0 + ENFORCE_TARGET_LIMITS_INTERVAL,
        );
        assert_eq!(
            setpoint_current(&effects),
            Some(50.0),
            "the limit reaches the supply though the vehicle stayed quiet: {effects:?}"
        );
        assert!(
            effects.contains(&Effect::StartTimer {
                id: TIMER_ENFORCE_LIMITS,
                after: ENFORCE_TARGET_LIMITS_INTERVAL
            }),
            "the watchdog re-arms itself"
        );
    }

    #[test]
    fn a_fresh_target_resets_the_watchdog() {
        let mut dc = charging_dc();
        let session = charging_session(200.0);
        let t0 = now();

        dc.set_ev_target(&session, 400.0, 100.0, t0);
        dc.on_path_event(&session, PathEvent::CurrentDemandStarted, t0);

        let effects = dc.set_ev_target(&session, 400.0, 90.0, t0 + Duration::from_secs(1));
        assert!(
            effects.contains(&Effect::StartTimer {
                id: TIMER_ENFORCE_LIMITS,
                after: ENFORCE_TARGET_LIMITS_INTERVAL
            }),
            "the vehicle spoke, so the five seconds start again: {effects:?}"
        );
    }

    #[test]
    fn the_watchdog_runs_only_while_current_demand_does() {
        let mut dc = charging_dc();
        let session = charging_session(200.0);
        let t0 = now();

        // Before current demand there is nothing to enforce.
        let effects = dc.set_ev_target(&session, 400.0, 100.0, t0);
        assert!(!effects.iter().any(|e| matches!(
            e,
            Effect::StartTimer {
                id: TIMER_ENFORCE_LIMITS,
                ..
            }
        )));

        dc.on_path_event(&session, PathEvent::CurrentDemandStarted, t0);
        let finished = dc.on_path_event(
            &session,
            PathEvent::CurrentDemandFinished,
            t0 + Duration::from_secs(1),
        );
        assert!(finished.contains(&Effect::CancelTimer {
            id: TIMER_ENFORCE_LIMITS
        }));

        assert!(
            dc.on_timer(&session, TIMER_ENFORCE_LIMITS, t0 + Duration::from_secs(9))
                .is_empty(),
            "a late expiry of a cancelled watchdog actuates nothing"
        );
    }

    fn over_voltage_limits(effects: &[Effect]) -> Option<(f64, f64)> {
        effects.iter().find_map(|e| match e {
            Effect::OverVoltageLimits(thresholds) => {
                Some((thresholds.emergency_v(), thresholds.error_v()))
            }
            _ => None,
        })
    }

    fn ev_maximum_voltage(voltage_v: f64) -> PathEvent {
        PathEvent::DcEvMaximumLimits(EvMaximumLimits {
            maximum_voltage_v: Some(voltage_v),
            ..EvMaximumLimits::default()
        })
    }

    /// The whole point of the task. `EvseManager.cpp:861-869` pushes both
    /// thresholds from this handler, so this arm has to emit and not merely
    /// store.
    #[test]
    fn the_vehicle_maximum_pushes_both_over_voltage_thresholds() {
        let mut dc = charging_dc();
        let session = charging_session(200.0);

        let effects = dc.on_path_event(&session, ev_maximum_voltage(900.0), now());

        assert_eq!(
            over_voltage_limits(&effects),
            Some((1100.0, 900.0)),
            "the monitor is told what to watch for: {effects:?}"
        );
    }

    /// IEC 61851-23 (2023) Table 103 and IEC 61851-23-3 (DRAFT 2025) Table 202,
    /// as `EvseManager.cpp:1944-1957` writes them. Every boundary is driven from
    /// both sides, because the C++ comparisons are all strictly greater and an
    /// off by one there moves a protection threshold.
    #[test]
    fn the_emergency_threshold_walks_the_iec_step_table() {
        // (negotiated maximum, emergency limit)
        let table = [
            (0.0, 550.0),
            (400.0, 550.0),
            (500.0, 550.0),
            (500.1, 825.0),
            (750.0, 825.0),
            (750.1, 935.0),
            (850.0, 935.0),
            (850.1, 1100.0),
            (1000.0, 1100.0),
            (1000.1, 1375.0),
            (1500.0, 1375.0),
        ];

        for (negotiated_v, expected_v) in table {
            // Supply well above the vehicle, so the vehicle maximum is the
            // negotiated one and is also the error limit.
            let thresholds = wired_monitor().thresholds(negotiated_v, 100_000.0);
            let (emergency_v, error_v) = (thresholds.emergency_v(), thresholds.error_v());
            assert_eq!(
                emergency_v, expected_v,
                "negotiated {negotiated_v} V belongs to the {expected_v} V step"
            );
            assert_eq!(error_v, negotiated_v);
        }
    }

    /// The supply clamps the emergency step and does not clamp the error limit.
    /// `get_emergency_over_voltage_threshold` takes the minimum of the two
    /// (`:1942`); `get_error_over_voltage_threshold` returns the vehicle
    /// maximum alone (`:1963-1971`). The asymmetry is reproduced, not corrected.
    #[test]
    fn the_supply_clamps_the_emergency_step_but_not_the_error_limit() {
        let thresholds = wired_monitor().thresholds(900.0, 400.0);
        let (emergency_v, error_v) = (thresholds.emergency_v(), thresholds.error_v());

        assert_eq!(
            emergency_v, 550.0,
            "the 400 V supply, not the 900 V vehicle, picks the step"
        );
        assert_eq!(
            error_v, 900.0,
            "the error limit is the vehicle's own rating"
        );
        assert!(
            error_v > emergency_v,
            "and it can sit above the emergency limit, which the monitor \
             evaluates first, leaving the error branch unreachable"
        );
    }

    /// `EvseManager.cpp:1934-1938` and `:1966-1970` both fall back to 500 V and
    /// log, rather than leaving the monitor unconfigured. A vehicle that
    /// reports no maximum still gets a ceiling.
    #[test]
    fn a_vehicle_without_a_reported_maximum_still_gets_thresholds() {
        let mut dc = dc_port(deriving_config(), true, true);
        dc.set_evse_max_export_voltage_v(950.0);

        let effects = dc.on_path_event(
            &charging_session(200.0),
            PathEvent::DcEvMaximumLimits(EvMaximumLimits::default()),
            now(),
        );

        assert_eq!(
            over_voltage_limits(&effects),
            Some((550.0, 500.0)),
            "the 500 V fallback negotiates to the bottom step: {effects:?}"
        );
    }

    /// A port with no over voltage monitor requirement wired has nothing to
    /// tell, the way `EvseManager.cpp:862` guards on a non empty requirement.
    #[test]
    fn an_unwired_over_voltage_monitor_is_never_told_the_thresholds() {
        let mut dc = dc_port(deriving_config(), true, false);
        dc.set_evse_max_export_voltage_v(950.0);

        let effects = dc.on_path_event(&charging_session(200.0), ev_maximum_voltage(900.0), now());

        assert_eq!(over_voltage_limits(&effects), None, "{effects:?}");
    }

    /// The C++ recomputes on every `dc_ev_maximum_limits` message and on no
    /// other, so a vehicle that revises its maximum mid session moves the
    /// thresholds with it.
    #[test]
    fn a_revised_vehicle_maximum_moves_the_thresholds() {
        let mut dc = charging_dc();
        let session = charging_session(200.0);
        let t0 = now();

        let first = dc.on_path_event(&session, ev_maximum_voltage(900.0), t0);
        assert_eq!(over_voltage_limits(&first), Some((1100.0, 900.0)));

        let second = dc.on_path_event(
            &session,
            ev_maximum_voltage(400.0),
            t0 + Duration::from_secs(1),
        );
        assert_eq!(
            over_voltage_limits(&second),
            Some((550.0, 400.0)),
            "the lowered maximum lowers both: {second:?}"
        );
    }

    /// Every field of `DcEvMaximumLimits` is optional on the wire
    /// (`types/iso15118.yaml:349-369` declares no `required` list), and the C++
    /// assigns the optional straight through
    /// (`ev_info.maximum_voltage_limit = l.dc_ev_maximum_voltage_limit`,
    /// `EvseManager.cpp:857`). So a vehicle that reported a maximum and then
    /// sends a message without one reverts to the fallback, widening the error
    /// limit and lowering the emergency step. Reachable, and reproduced.
    #[test]
    fn a_maximum_withdrawn_mid_session_reverts_to_the_fallback() {
        let mut dc = charging_dc();
        let session = charging_session(200.0);
        let t0 = now();

        let first = dc.on_path_event(&session, ev_maximum_voltage(900.0), t0);
        assert_eq!(over_voltage_limits(&first), Some((1100.0, 900.0)));

        let withdrawn = dc.on_path_event(
            &session,
            PathEvent::DcEvMaximumLimits(EvMaximumLimits::default()),
            t0 + Duration::from_secs(1),
        );
        assert_eq!(
            over_voltage_limits(&withdrawn),
            Some((550.0, 500.0)),
            "the store is a replace, not a merge: {withdrawn:?}"
        );
    }

    /// `interfaces/over_voltage_monitor.yaml` is explicit that `set_limits`
    /// "will be called any time the voltage limits change, independent of
    /// whether the monitoring is currently active or not", and the C++ handler
    /// consults no monitor state. A push that arrives while the monitor is
    /// stopped still has to travel, because the device caches it for the next
    /// start.
    #[test]
    fn the_thresholds_travel_while_the_monitor_is_stopped() {
        let mut dc = charging_dc();
        let session = charging_session(200.0);
        let t0 = now();

        dc.set_ev_target(&session, 400.0, 100.0, t0);
        dc.on_path_event(&session, PathEvent::CurrentDemandStarted, t0);
        let finished = dc.on_path_event(
            &session,
            PathEvent::CurrentDemandFinished,
            t0 + Duration::from_secs(1),
        );
        assert!(finished.contains(&Effect::OverVoltageStop));

        let effects = dc.on_path_event(
            &session,
            ev_maximum_voltage(900.0),
            t0 + Duration::from_secs(2),
        );
        assert_eq!(
            over_voltage_limits(&effects),
            Some((1100.0, 900.0)),
            "a stopped monitor is still told: {effects:?}"
        );
    }

    /// Parity, and the reason this is worth a test rather than a comment: the
    /// emergency threshold reads the supply capability, so it is tempting to
    /// push on the capability arm as well. The C++ has exactly one push site
    /// (`:861-869`) and this reproduces that. A capability that lands after the
    /// vehicle maximum leaves the monitor on the previously derived pair until
    /// the vehicle speaks again.
    #[test]
    fn the_supply_capability_arm_does_not_push_thresholds_on_its_own() {
        let mut dc = charging_dc();
        let session = charging_session(200.0);
        let t0 = now();

        let effects = dc.on_path_event(
            &session,
            PathEvent::DcExportVoltageRange { min_v: 0.0, max_v: 400.0 },
            t0,
        );
        assert_eq!(over_voltage_limits(&effects), None, "{effects:?}");

        // But the next vehicle message picks the new capability up.
        let next = dc.on_path_event(
            &session,
            ev_maximum_voltage(900.0),
            t0 + Duration::from_secs(1),
        );
        assert_eq!(over_voltage_limits(&next), Some((550.0, 900.0)));
    }

    #[test]
    fn current_demand_applies_the_target_before_starting_the_over_voltage_monitor() {
        let mut dc = charging_dc();
        let session = charging_session(200.0);
        let t0 = now();

        // Cable check leaves the supply off, so the target has to be written
        // again when current demand starts.
        dc.set_ev_target(&session, 400.0, 100.0, t0);
        dc.supply_off();

        let effects = dc.on_path_event(
            &session,
            PathEvent::CurrentDemandStarted,
            t0 + Duration::from_secs(1),
        );

        let setpoint_at = index_of_setpoint(&effects).expect("the target is re-applied");
        let monitor_at = effects
            .iter()
            .position(|e| *e == Effect::OverVoltageStart)
            .expect("the over voltage monitor starts");
        assert!(
            setpoint_at < monitor_at,
            "ported order is target then monitor: {effects:?}"
        );
    }

    #[test]
    fn current_demand_finished_removes_energy_before_stopping_the_over_voltage_monitor() {
        let mut dc = charging_dc();
        let session = charging_session(200.0);
        let t0 = now();

        dc.set_ev_target(&session, 400.0, 100.0, t0);
        dc.on_path_event(&session, PathEvent::CurrentDemandStarted, t0);

        let effects = dc.on_path_event(
            &session,
            PathEvent::CurrentDemandFinished,
            t0 + Duration::from_secs(1),
        );

        let off_at = effects.iter().position(|e| *e == Effect::SupplyOff);
        let monitor_at = effects.iter().position(|e| *e == Effect::OverVoltageStop);
        assert!(
            off_at.unwrap() < monitor_at.unwrap(),
            "energy removal comes first: {effects:?}"
        );
        assert_eq!(dc.applied_setpoint, None, "the off invalidates the cache");
    }

    #[test]
    fn shutdown_switches_the_supply_off_before_opening_the_contactor() {
        // `Charger.cpp:2255-2286` shuts the supply down and only then opens the
        // contactors, and `EvseManager.cpp:829-849` is what carries the off.
        let mut dc = charging_dc();
        let session = charging_session(200.0);
        let t0 = now();

        dc.set_ev_target(&session, 400.0, 100.0, t0);
        dc.on_path_event(&session, PathEvent::CurrentDemandStarted, t0);

        let effects = dc.to_safe_state();

        let off_at = effects.iter().position(|e| *e == Effect::SupplyOff);
        let contactor_at = effects
            .iter()
            .position(|e| *e == Effect::AllowPowerOn(false));
        assert!(off_at.unwrap() < contactor_at.unwrap());
        assert!(
            effects.contains(&Effect::CancelTimer {
                id: TIMER_ENFORCE_LIMITS
            }),
            "the watchdog does not outlive the session: {effects:?}"
        );
    }

    #[test]
    fn no_target_yet_means_nothing_is_written_to_the_supply() {
        // `EvseManager.cpp:2641` gates the whole apply on a positive target
        // voltage, so a watchdog expiry before the vehicle has ever asked for
        // anything must not energize the cable at zero volts.
        let mut dc = charging_dc();
        let session = charging_session(200.0);
        let t0 = now();

        let started = dc.on_path_event(&session, PathEvent::CurrentDemandStarted, t0);
        assert!(index_of_setpoint(&started).is_none(), "{started:?}");

        let expired = dc.on_timer(
            &session,
            TIMER_ENFORCE_LIMITS,
            t0 + ENFORCE_TARGET_LIMITS_INTERVAL,
        );
        assert!(index_of_setpoint(&expired).is_none(), "{expired:?}");
        assert_eq!(dc.supply_mode(), SupplyMode::Off);
    }

    #[test]
    fn a_new_session_starts_the_ramp_from_zero() {
        let mut dc = charging_dc();
        let session = charging_session(500.0);
        let t0 = now();

        dc.set_ev_target(&session, 400.0, 0.0, t0);
        dc.set_ev_target(&session, 400.0, 400.0, t0 + Duration::from_secs(1));
        assert_eq!(dc.ramped_current_a, 20.0);

        dc.on_session_start(&session, t0 + Duration::from_secs(2));
        assert_eq!(
            dc.ramped_current_a, 0.0,
            "a session must not inherit the previous ramp position"
        );
        assert_eq!(dc.raw_target_voltage_v, 0.0);
    }
    #[test]
    fn the_startup_transition_enables_the_board_support_output() {
        // No control pilot reducer on this path, so the boot output is the board
        // enable and nothing else. `Charger.cpp:95-100` is mode independent.
        let mut dc = dc_port(config(), true, true);

        assert_eq!(dc.on_startup(), vec![Effect::BspEnable(true)]);
    }

    /// The session progress a DC port travels, and the duties its edges are
    /// due. `Charger::run_state_machine` holds one `EvseState` for both charge
    /// modes; what differs on DC is which trigger moves it, which is why these
    /// drive `Dc` through its own entry points rather than a control pilot
    /// reducer.
    mod progress {
        use super::*;
        use crate::core::path::SessionDuty;
        use crate::core::session::SessionEvent;

        /// A session that reached the charge loop, driven the way `Core` drives
        /// it: boot, plug in, authorization with a record open, then the high
        /// level communication reporting current demand.
        fn charging() -> (Dc, Session) {
            let mut dc = dc_port(config(), true, true);
            let mut live = session();
            dc.on_startup();
            dc.on_session_start(&live, now());
            live.transaction_active = true;
            dc.on_authorized(&live, now());
            dc.on_path_event(&live, PathEvent::CurrentDemandStarted, now());
            dc.take_session_duties();
            (dc, live)
        }

        #[test]
        fn a_port_coming_up_is_the_end_of_nothing() {
            let mut dc = dc_port(config(), true, true);

            dc.on_startup();

            assert_eq!(dc.take_session_duties(), Vec::new());
        }

        #[test]
        fn a_plug_in_announces_nothing_of_its_own() {
            // `Charger.cpp:272` publishes `AuthRequired` on the
            // `WaitingForAuthentication` entry, and the core publishes that
            // from the plug in it already owns.
            let mut dc = dc_port(config(), true, true);
            dc.on_startup();

            dc.on_session_start(&session(), now());

            assert_eq!(dc.take_session_duties(), Vec::new());
        }

        /// `Charger.cpp:566-568`: on DC an authorization carries
        /// `WaitingForAuthentication` straight to `PrepareCharging`, whose
        /// entry signals the event at `Charger.cpp:675`.
        #[test]
        fn authorization_moves_a_dc_session_into_prepare_charging() {
            let mut dc = dc_port(config(), true, true);
            let mut live = session();
            dc.on_startup();
            dc.on_session_start(&live, now());
            live.transaction_active = true;

            dc.on_authorized(&live, now());

            assert_eq!(
                dc.take_session_duties(),
                vec![
                    SessionDuty::StartTransaction,
                    SessionDuty::Publish(SessionEvent::PrepareCharging)
                ]
            );
        }

        /// The authorization can be the first user interaction, arriving before
        /// the vehicle. `Charger.cpp` re-reads `flag_authorized` on every pass
        /// through `WaitingForAuthentication`, so whichever fact arrives last
        /// completes the decision.
        #[test]
        fn an_authorization_that_precedes_the_vehicle_still_prepares_charging() {
            let mut dc = dc_port(config(), true, true);
            let mut live = session();
            dc.on_startup();
            live.transaction_active = true;
            dc.on_authorized(&live, now());
            assert_eq!(dc.take_session_duties(), Vec::new(), "no vehicle yet");

            dc.on_session_start(&live, now());

            assert_eq!(
                dc.take_session_duties(),
                vec![
                    SessionDuty::StartTransaction,
                    SessionDuty::Publish(SessionEvent::PrepareCharging)
                ]
            );
        }

        /// `Charger::notify_currentdemand_started` (`Charger.cpp:2024-2029`) is
        /// the only route into `Charging` on DC, and that entry signals the
        /// event at `Charger.cpp:763`.
        #[test]
        fn current_demand_started_announces_charging_started() {
            let mut dc = dc_port(config(), true, true);
            let mut live = session();
            dc.on_startup();
            dc.on_session_start(&live, now());
            live.transaction_active = true;
            dc.on_authorized(&live, now());
            dc.take_session_duties();

            dc.on_path_event(&live, PathEvent::CurrentDemandStarted, now());

            assert_eq!(
                dc.take_session_duties(),
                vec![SessionDuty::Publish(SessionEvent::ChargingStarted)]
            );
        }

        /// `Charger.cpp:2027` guards on `PrepareCharging`, so current demand
        /// reported from anywhere else moves nothing.
        #[test]
        fn current_demand_started_outside_prepare_charging_announces_nothing() {
            let mut dc = dc_port(config(), true, true);
            dc.on_startup();

            dc.on_path_event(&session(), PathEvent::CurrentDemandStarted, now());

            assert_eq!(dc.take_session_duties(), Vec::new());
        }

        /// `EvseManager.cpp:592-598` clears the charge loop flags and stops the
        /// monitors. It never touches `Charger::shared_context.current_state`,
        /// so the session stays in `Charging` and nothing is due.
        #[test]
        fn current_demand_finished_is_no_session_progress() {
            let (mut dc, live) = charging();

            dc.on_path_event(&live, PathEvent::CurrentDemandFinished, now());

            assert_eq!(dc.take_session_duties(), Vec::new());
        }

        /// `Charger.cpp:792` leaves `Charging` for `StoppingCharging`, whose
        /// entry signals the event at `Charger.cpp:1012`.
        #[test]
        fn a_stop_announces_stopping_charging() {
            let (mut dc, live) = charging();

            dc.on_stop(&live, StopReason::Local, now());

            assert_eq!(
                dc.take_session_duties(),
                vec![
                    SessionDuty::Publish(SessionEvent::StoppingCharging),
                    SessionDuty::AskVehicleToStop,
                ]
            );
        }

        #[test]
        fn a_stop_with_no_charge_under_way_announces_nothing() {
            let mut dc = dc_port(config(), true, true);
            dc.on_startup();

            dc.on_stop(&session(), StopReason::Local, now());

            assert_eq!(dc.take_session_duties(), Vec::new());
        }

        /// The reason this matters: a DC charge that ends the ordinary way,
        /// the driver unplugs, must close the billing record the authorization
        /// opened. `Charger.cpp:1063-1067`.
        ///
        /// It crosses the stopping entry on the way, which is what asks the
        /// vehicle to end the session: `Charger::run_state_machine`'s
        /// `Charging` arm reaches `StoppingCharging` on
        /// `not flag_ev_plugged_in` for either charge mode, and that state's
        /// entry then asks.
        #[test]
        fn an_unplug_during_dc_charging_closes_the_billing_record() {
            for cp in [CpEvent::A, CpEvent::Disconnected] {
                let (mut dc, live) = charging();

                dc.on_bsp(&live, &BspEvent::Cp(cp), CpEdges::default(), now());

                assert_eq!(
                    dc.take_session_duties(),
                    vec![
                        SessionDuty::Publish(SessionEvent::StoppingCharging),
                        SessionDuty::AskVehicleToStop,
                        SessionDuty::StopTransaction,
                        SessionDuty::EndSession
                    ],
                    "{cp:?}"
                );
            }
        }

        /// `Charger.cpp:237-238`: an unplug with a disable outstanding lands in
        /// `Disabled`, not `Idle`, and closes the record just the same.
        #[test]
        fn an_unplug_with_a_disable_outstanding_still_closes_the_record() {
            let (mut dc, live) = charging();
            dc.on_path_event(&live, PathEvent::Disable, now());
            dc.take_session_duties();

            dc.on_bsp(&live, &BspEvent::Cp(CpEvent::A), CpEdges::default(), now());

            assert_eq!(
                dc.take_session_duties(),
                Vec::new(),
                "closed by the disable"
            );
        }

        /// `Charger.cpp:1082` reaches `Disabled` out of `Finished`, so the port
        /// leaving service under a vehicle closes the record too.
        #[test]
        fn a_disable_during_dc_charging_closes_the_billing_record() {
            let (mut dc, live) = charging();

            dc.on_path_event(&live, PathEvent::Disable, now());

            assert_eq!(
                dc.take_session_duties(),
                vec![SessionDuty::StopTransaction, SessionDuty::EndSession]
            );
        }

        /// One drive, one report. The accumulator is drained by the core, so a
        /// duty raised once is discharged once.
        #[test]
        fn duties_are_reported_once() {
            let (mut dc, live) = charging();

            dc.on_bsp(&live, &BspEvent::Cp(CpEvent::A), CpEdges::default(), now());
            assert_eq!(
                dc.take_session_duties(),
                vec![
                    SessionDuty::Publish(SessionEvent::StoppingCharging),
                    SessionDuty::AskVehicleToStop,
                    SessionDuty::StopTransaction,
                    SessionDuty::EndSession
                ]
            );
            assert_eq!(dc.take_session_duties(), Vec::new());
        }

        /// The whole ordinary DC session, in order.
        #[test]
        fn an_ordinary_dc_session_reports_its_progress_in_order() {
            let mut dc = dc_port(config(), true, true);
            let mut live = session();
            let mut seen = Vec::new();
            dc.on_startup();
            seen.extend(dc.take_session_duties());
            dc.on_session_start(&live, now());
            seen.extend(dc.take_session_duties());
            live.transaction_active = true;
            dc.on_authorized(&live, now());
            seen.extend(dc.take_session_duties());
            dc.on_path_event(&live, PathEvent::CableCheckRequired, now());
            seen.extend(dc.take_session_duties());
            dc.on_path_event(&live, PathEvent::PreChargeStarted, now());
            seen.extend(dc.take_session_duties());
            dc.on_path_event(&live, PathEvent::CurrentDemandStarted, now());
            seen.extend(dc.take_session_duties());
            dc.on_path_event(&live, PathEvent::CurrentDemandFinished, now());
            seen.extend(dc.take_session_duties());
            dc.on_stop(&live, StopReason::Local, now());
            seen.extend(dc.take_session_duties());
            dc.on_bsp(&live, &BspEvent::Cp(CpEvent::A), CpEdges::default(), now());
            seen.extend(dc.take_session_duties());

            assert_eq!(
                seen,
                vec![
                    SessionDuty::StartTransaction,
                    SessionDuty::Publish(SessionEvent::PrepareCharging),
                    SessionDuty::Publish(SessionEvent::ChargingStarted),
                    SessionDuty::Publish(SessionEvent::StoppingCharging),
                    SessionDuty::AskVehicleToStop,
                    SessionDuty::StopTransaction,
                    SessionDuty::EndSession,
                ]
            );
        }

        /// A second session on the same port starts from the resting state,
        /// with the previous authorization gone.
        #[test]
        fn a_replug_does_not_inherit_the_previous_authorization() {
            let (mut dc, mut live) = charging();
            dc.on_bsp(&live, &BspEvent::Cp(CpEvent::A), CpEdges::default(), now());
            dc.take_session_duties();
            live.transaction_active = false;

            dc.on_session_start(&live, now());

            assert_eq!(
                dc.take_session_duties(),
                Vec::new(),
                "the new session waits for its own authorization"
            );
        }

        /// An unplug ends the authorization with the session, so the next
        /// vehicle waits for its own. `Charger.cpp:231-232` clears the same
        /// flag on the way back to `Idle`.
        #[test]
        fn an_authorization_does_not_carry_into_the_next_session() {
            let (mut dc, live) = charging();
            dc.on_bsp(&live, &BspEvent::Cp(CpEvent::A), CpEdges::default(), now());
            dc.take_session_duties();

            dc.on_session_start(&live, now());

            assert_eq!(
                dc.take_session_duties(),
                Vec::new(),
                "the second session waits for an authorization of its own"
            );
        }

        /// The port leaving service ends the authorization with the session.
        /// A vehicle arriving after the port comes back must be authorized
        /// again, or it would reach `PrepareCharging` on the strength of an
        /// authorization the last session was given.
        #[test]
        fn a_disable_does_not_leave_an_authorization_behind_it() {
            let (mut dc, live) = charging();
            dc.on_path_event(&live, PathEvent::Disable, now());
            dc.on_path_event(&live, PathEvent::Enable, now());
            dc.take_session_duties();

            dc.on_session_start(&live, now());

            assert_eq!(
                dc.take_session_duties(),
                Vec::new(),
                "the next vehicle waits for an authorization of its own"
            );
        }

        /// `Charger.cpp:1726-1731` hands a re-enabled port back to `Idle`, and
        /// a port that stayed in `Disabled` instead would refuse every plug in
        /// that followed for the rest of its life.
        #[test]
        fn a_port_returned_to_service_serves_the_next_session() {
            let (mut dc, mut live) = charging();
            dc.on_path_event(&live, PathEvent::Disable, now());
            dc.on_path_event(&live, PathEvent::Enable, now());
            dc.take_session_duties();

            dc.on_session_start(&live, now());
            live.transaction_active = true;
            dc.on_authorized(&live, now());

            assert_eq!(
                dc.take_session_duties(),
                vec![
                    SessionDuty::StartTransaction,
                    SessionDuty::Publish(SessionEvent::PrepareCharging)
                ]
            );
        }

        /// A control pilot state A unplug is an unplug. It reaches the path as
        /// `CpEvent::A` rather than `Disconnected` whenever the board still
        /// reports a pilot level, and a DC port that ignored it would leave the
        /// supply energized on the cable the driver just pulled.
        #[test]
        fn a_state_a_unplug_removes_energy() {
            let (mut dc, live) = charging();

            let effects = dc.on_bsp(&live, &BspEvent::Cp(CpEvent::A), CpEdges::default(), now());

            assert!(effects.contains(&Effect::SupplyOff), "got {effects:?}");
            assert!(
                effects.contains(&Effect::AllowPowerOn(false)),
                "got {effects:?}"
            );
        }
    }

    /// The DC target clamp, `process_dc_ev_target_voltage_current`
    /// (`EvseManager.cpp:2563-2638`).
    mod dc_target_clamp {
        use super::*;

        fn evse_maximum(current_a: f64, power_w: f64) -> MaximumLimits {
            MaximumLimits {
                maximum_current_a: current_a,
                maximum_voltage_v: 950.0,
                maximum_power_w: power_w,
                maximum_discharge_current_a: None,
                maximum_discharge_power_w: None,
            }
        }

        /// `:2582-2585`. The EVSE limit set is what caps the vehicle, not the
        /// AC current limit the enforced limits carry: `Limits::max_current_a`
        /// is filled from `ac_max_current_a` (`enforce_limits`), which the
        /// energy management DC branch does not set at all, so reading it on a
        /// DC port would clamp every target to zero.
        #[test]
        fn the_evse_maximum_current_caps_the_vehicles_request() {
            let mut dc = charging_dc();
            dc.set_evse_hlc_limits(evse_maximum(80.0, 500_000.0), MinimumLimits::default());
            let session = charging_session(0.0);

            let effects = dc.set_ev_target(&session, 400.0, 100.0, now());

            assert_eq!(setpoint_current(&effects), Some(80.0));
        }

        /// `:2576-2581`, the clamp the C++ comments as being for broken EV
        /// implementations: the vehicle's own reported maximum caps its own
        /// request.
        #[test]
        fn the_vehicles_own_reported_maximum_current_caps_its_request() {
            let mut dc = charging_dc();
            dc.set_evse_hlc_limits(evse_maximum(400.0, 500_000.0), MinimumLimits::default());
            dc.set_ev_maximum_limits(EvMaximumLimits {
                maximum_current_a: Some(60.0),
                maximum_voltage_v: Some(900.0),
            });
            let session = charging_session(0.0);

            let effects = dc.set_ev_target(&session, 400.0, 100.0, now());

            assert_eq!(setpoint_current(&effects), Some(60.0));
        }

        /// `:2602-2609`. The power ceiling is turned into a current ceiling at
        /// the voltage actually on the cable.
        #[test]
        fn the_evse_maximum_power_caps_the_current_at_the_present_voltage() {
            let mut dc = charging_dc();
            dc.set_evse_hlc_limits(evse_maximum(400.0, 20_000.0), MinimumLimits::default());
            let session = charging_session(0.0);
            dc.on_path_event(
                &session,
                PathEvent::SupplyVoltage { voltage_v: 400.0 },
                now(),
            );

            let effects = dc.set_ev_target(&session, 800.0, 100.0, now());

            // 20 kW over the 400 V on the cable, not over the 800 V asked for.
            assert_eq!(setpoint_current(&effects), Some(50.0));
        }

        /// `:2602-2603` reads the present voltage through `has_value()`, so
        /// before the supply has reported anything the power clamp converts at
        /// the target voltage instead. An absent reading and a reading of zero
        /// are therefore different inputs, which is why the field is an option.
        #[test]
        fn the_power_clamp_converts_at_the_target_voltage_until_the_supply_reports() {
            let mut dc = charging_dc();
            dc.set_evse_hlc_limits(evse_maximum(400.0, 20_000.0), MinimumLimits::default());
            let session = charging_session(0.0);

            let effects = dc.set_ev_target(&session, 800.0, 100.0, now());

            // 20 kW over the 800 V asked for.
            assert_eq!(setpoint_current(&effects), Some(25.0));
        }

        /// `:2589-2600`, ISO 15118-20 [V2G20-2183]: a vehicle may send only one
        /// of the two, and some send a zero voltage in the charge loop. The
        /// last non zero voltage is reused rather than the cable being taken to
        /// zero volts.
        #[test]
        fn a_zero_target_voltage_reuses_the_last_non_zero_one() {
            let mut dc = charging_dc();
            dc.set_evse_hlc_limits(evse_maximum(400.0, 500_000.0), MinimumLimits::default());
            let session = charging_session(0.0);
            let t0 = now();

            dc.set_ev_target(&session, 400.0, 100.0, t0);
            let effects = dc.set_ev_target(&session, 0.0, 100.0, t0 + Duration::from_secs(1));

            assert_eq!(
                effects.iter().find_map(|effect| match effect {
                    Effect::SetSupplySetpoint { voltage_v, .. } => Some(*voltage_v),
                    _ => None,
                }),
                None,
                "the reused voltage is the applied one, so nothing changed: {effects:?}"
            );
            assert_eq!(dc.latest_target_voltage_v, 400.0);
        }

        /// The apply gate at `:2641` reads the **clamped** voltage, not the raw
        /// one, and the zero voltage cache is the only place the two differ
        /// while one of them is positive. So a vehicle that sends a zero
        /// voltage with a changed current still reaches the supply.
        ///
        /// Drawn out separately from the test above because that one asserts a
        /// setpoint that did not change, which a gate on the raw voltage would
        /// also produce.
        #[test]
        fn a_zero_target_voltage_with_a_changed_current_still_reaches_the_supply() {
            let mut dc = charging_dc();
            dc.set_evse_hlc_limits(evse_maximum(400.0, 500_000.0), MinimumLimits::default());
            let session = charging_session(0.0);
            let t0 = now();

            dc.set_ev_target(&session, 400.0, 100.0, t0);
            let effects = dc.set_ev_target(&session, 0.0, 90.0, t0 + Duration::from_secs(1));

            assert_eq!(
                effects.iter().find_map(|effect| match effect {
                    Effect::SetSupplySetpoint {
                        voltage_v,
                        current_a,
                        ..
                    } => Some((*voltage_v, *current_a)),
                    _ => None,
                }),
                Some((400.0, 90.0)),
                "the cached voltage carries the new current: {effects:?}"
            );
        }

        /// The vehicle's maxima arrive as a path event, and the arm that stores
        /// them is one line. Driven end to end here, because a clamp that reads
        /// a field nothing ever writes is indistinguishable from no clamp.
        #[test]
        fn the_vehicles_maxima_arrive_as_a_path_event() {
            let mut dc = charging_dc();
            dc.set_evse_hlc_limits(evse_maximum(400.0, 500_000.0), MinimumLimits::default());
            let session = charging_session(0.0);
            let t0 = now();

            dc.on_path_event(
                &session,
                PathEvent::DcEvMaximumLimits(EvMaximumLimits {
                    maximum_current_a: Some(60.0),
                    maximum_voltage_v: Some(450.0),
                }),
                t0,
            );
            let effects = dc.on_path_event(
                &session,
                PathEvent::DcEvTarget {
                    voltage_v: 800.0,
                    current_a: 100.0,
                },
                t0,
            );

            assert_eq!(
                effects.iter().find_map(|effect| match effect {
                    Effect::SetSupplySetpoint {
                        voltage_v,
                        current_a,
                        ..
                    } => Some((*voltage_v, *current_a)),
                    _ => None,
                }),
                Some((450.0, 60.0)),
                "both of the vehicle's own maxima clamp its target: {effects:?}"
            );
        }

        /// The EVSE limit set arrives as a path event too, and the same
        /// argument applies: a stored copy nothing writes clamps nothing.
        #[test]
        fn the_evse_limit_set_arrives_as_a_path_event() {
            let mut dc = charging_dc();
            let session = charging_session(0.0);
            let t0 = now();

            dc.on_path_event(
                &session,
                PathEvent::DcEnforcedLimits {
                    maximum: evse_maximum(75.0, 500_000.0),
                    minimum: MinimumLimits::default(),
                    exporting_to_grid: false,
                    reapply_target: false,
                },
                t0,
            );
            let effects = dc.on_path_event(
                &session,
                PathEvent::DcEvTarget {
                    voltage_v: 400.0,
                    current_a: 100.0,
                },
                t0,
            );

            assert_eq!(setpoint_current(&effects), Some(75.0), "{effects:?}");
        }

        /// The first target of a session is a zero voltage, so there is nothing
        /// cached to reuse and `:2641` refuses to write anything at all. Drawn
        /// out separately because the cache guard is `value_or(0.f) > 0.f`, and
        /// a port that reused an uninitialized zero would energize the cable at
        /// zero volts.
        #[test]
        fn a_zero_target_voltage_with_nothing_cached_writes_nothing() {
            let mut dc = charging_dc();
            dc.set_evse_hlc_limits(evse_maximum(400.0, 500_000.0), MinimumLimits::default());
            let session = charging_session(0.0);

            let effects = dc.set_ev_target(&session, 0.0, 100.0, now());

            assert_eq!(index_of_setpoint(&effects), None, "{effects:?}");
            assert_eq!(dc.supply_mode(), SupplyMode::Off);
        }
    }

    /// The ISO 15118-20 dynamic control mode arm,
    /// `subscribe_d20_dc_dynamic_charge_mode` (`EvseManager.cpp:740-825`).
    mod dynamic_control_mode_arm {
        use super::*;

        fn request() -> DynamicModeRequest {
            DynamicModeRequest {
                max_charge_power_w: 40_000.0,
                min_charge_power_w: 1_000.0,
                max_charge_current_a: 250.0,
                max_voltage_v: 800.0,
                min_voltage_v: 300.0,
                max_discharge_power_w: None,
                min_discharge_power_w: None,
                max_discharge_current_a: None,
            }
        }

        fn generous_limits() -> (MaximumLimits, MinimumLimits) {
            (
                MaximumLimits {
                    maximum_current_a: 400.0,
                    maximum_voltage_v: 950.0,
                    maximum_power_w: 300_000.0,
                    maximum_discharge_current_a: None,
                    maximum_discharge_power_w: None,
                },
                MinimumLimits {
                    minimum_current_a: 2.0,
                    minimum_voltage_v: 150.0,
                    minimum_power_w: 300.0,
                    minimum_discharge_current_a: None,
                    minimum_discharge_power_w: None,
                },
            )
        }

        /// `:822-824`. The resolved target goes through the same clamp and the
        /// same ramp the vehicle's own target does.
        #[test]
        fn a_dynamic_mode_request_reaches_the_supply_as_a_setpoint() {
            let mut dc = charging_dc();
            let (maximum, minimum) = generous_limits();
            dc.set_evse_hlc_limits(maximum, minimum);
            let session = charging_session(0.0);
            let t0 = now();
            dc.on_path_event(&session, PathEvent::SupplyVoltage { voltage_v: 400.0 }, t0);

            let effects = dc.on_dynamic_charge_mode(&session, request(), t0);

            assert_eq!(
                effects.iter().find_map(|effect| match effect {
                    Effect::SetSupplySetpoint {
                        voltage_v,
                        current_a,
                        ..
                    } => Some((*voltage_v, *current_a)),
                    _ => None,
                }),
                // The vehicle's maximum voltage, and 40 kW over the 400 V on
                // the cable.
                Some((800.0, 100.0))
            );
        }

        /// The mandatory guard, at the arm rather than at the clamp: an
        /// abandoned update must leave the supply exactly as it was, not write
        /// a partial one. Asserted as no effects at all, because an update that
        /// reset only the re-apply watchdog would still be a partial one.
        #[test]
        fn a_request_with_crossed_bounds_leaves_the_supply_untouched() {
            let mut dc = charging_dc();
            let (maximum, minimum) = generous_limits();
            dc.set_evse_hlc_limits(maximum, minimum);
            let session = charging_session(0.0);
            let t0 = now();
            dc.on_path_event(&session, PathEvent::CurrentDemandStarted, t0);
            let before = dc.applied_setpoint;

            let effects = dc.on_dynamic_charge_mode(
                &session,
                DynamicModeRequest {
                    max_charge_power_w: 10_000.0,
                    min_charge_power_w: 15_000.0,
                    ..request()
                },
                t0,
            );

            assert!(effects.is_empty(), "{effects:?}");
            assert_eq!(dc.applied_setpoint, before);
            assert_eq!(dc.supply_mode(), SupplyMode::Off);
        }

        /// A request arriving before the power supply has reported any
        /// capability finds the EVSE limit set at its zeros, so the maxima
        /// clamp the vehicle down to no power and no current. Nothing is
        /// energized, and in particular nothing panics: the vehicle's minimum
        /// charge power is above the EVSE maximum of zero, so this is a crossed
        /// bound pair too.
        #[test]
        fn a_request_before_any_capability_report_is_abandoned() {
            let mut dc = charging_dc();
            // The state `Charger` holds before the first energy management
            // pass: `DcEvseMaximumLimits` and `DcEvseMinimumLimits` value
            // initialized, so every maximum is zero.
            dc.set_evse_hlc_limits(MaximumLimits::default(), MinimumLimits::default());
            let session = charging_session(0.0);

            let effects = dc.on_dynamic_charge_mode(&session, request(), now());

            assert!(effects.is_empty(), "{effects:?}");
            assert_eq!(dc.supply_mode(), SupplyMode::Off);
        }

        /// `:815` converts at `ev_info.present_voltage.value_or(latest_target_voltage)`,
        /// so before the supply has reported anything the last clamped target
        /// stands in. Driven with a target already applied and no reading yet,
        /// which is the only state where the fallback is observable.
        #[test]
        fn a_dynamic_request_before_any_supply_reading_converts_at_the_last_target() {
            let mut dc = charging_dc();
            let (maximum, minimum) = generous_limits();
            dc.set_evse_hlc_limits(maximum, minimum);
            let session = charging_session(0.0);
            let t0 = now();

            // A target is applied, so `latest_target_voltage` is 500 V, and the
            // supply has still reported nothing.
            dc.set_ev_target(&session, 500.0, 10.0, t0);

            let effects = dc.on_dynamic_charge_mode(&session, request(), t0);

            // 40 kW over the 500 V last targeted, not over zero and not over
            // the 800 V this request asks for.
            assert_eq!(setpoint_current(&effects), Some(80.0), "{effects:?}");
        }

        /// The same request with a zero minimum charge power does not cross the
        /// bounds, so it is applied, and what it resolves to is no current at
        /// all rather than a refusal.
        #[test]
        fn a_zero_minimum_request_before_any_capability_report_asks_for_no_current() {
            let mut dc = charging_dc();
            dc.set_evse_hlc_limits(MaximumLimits::default(), MinimumLimits::default());
            let session = charging_session(0.0);
            let t0 = now();

            let effects = dc.on_dynamic_charge_mode(
                &session,
                DynamicModeRequest {
                    min_charge_power_w: 0.0,
                    ..request()
                },
                t0,
            );

            assert_eq!(setpoint_current(&effects), Some(0.0), "{effects:?}");
        }
    }

    /// The seven DC safety orderings, each pinned by an assertion that a
    /// deliberately broken ordering makes fail.
    ///
    /// Written against a mutation campaign over the whole module: 43 broken
    /// orderings, one per way each hazard can plausibly be got wrong. Forty one
    /// were already caught by the tests above. Two were not, and both are
    /// pinned here for the first time: `to_safe_state` could stop both monitors
    /// before removing energy, and `fail` could arm the abort's de-energize
    /// bound before switching the supply off, with all 1228 tests still green.
    ///
    /// Every assertion in this module has been watched to fail against its own
    /// break and to pass again once restored. A hazard test that cannot go red
    /// is worth less than no test at all, because it reads as cover.
    mod hazards {
        use super::*;

        /// Position of the first effect matching `pred`, or a panic naming the
        /// whole list, so a failure says what the sequence actually was rather
        /// than that an `Option` was `None`.
        fn at(effects: &[Effect], what: &str, pred: impl Fn(&Effect) -> bool) -> usize {
            effects
                .iter()
                .position(pred)
                .unwrap_or_else(|| panic!("no {what} in {effects:?}"))
        }

        /// The same, restricted to what follows `from`. The cable check arms the
        /// same stage timeout at several stages, so a bare search finds the
        /// first arming and not the one under test.
        fn at_after(
            effects: &[Effect],
            from: usize,
            what: &str,
            pred: impl Fn(&Effect) -> bool,
        ) -> usize {
            effects
                .iter()
                .enumerate()
                .skip(from + 1)
                .find(|(_, e)| pred(e))
                .map(|(i, _)| i)
                .unwrap_or_else(|| panic!("no {what} after index {from} in {effects:?}"))
        }

        fn setpoint(effect: &Effect) -> bool {
            matches!(effect, Effect::SetSupplySetpoint { .. })
        }

        fn mode(effect: &Effect) -> bool {
            matches!(effect, Effect::SetSupplyMode { .. })
        }

        /// Hazard 1. The cable check energizes only behind two gates, in this
        /// order: the cable is confirmed below the safe threshold, and the
        /// contactor is confirmed closed. Then the ramp up, then the self test
        /// on a monitor that has proved itself, then the samples, and the
        /// success verdict only after the ramp down.
        ///
        /// `EvseManager::cable_check` is the whole sequence. Its first act is
        /// `wait_powersupply_DC_below_voltage(CABLECHECK_SAFE_VOLTAGE)`, and it
        /// waits on `contactor_open` before `powersupply_DC_on`.
        #[test]
        fn the_cable_check_energizes_only_behind_the_safe_voltage_and_contactor_gates() {
            let mut h = Harness::new(CableCheckOptions::default());

            // A cable still above the safe threshold is not advanced past. The
            // only thing that happens is the bound on the wait.
            h.begin(400.0);
            assert_eq!(
                h.stage(),
                CableCheck::AwaitSafeVoltage,
                "an energized cable is not advanced past: {:?}",
                h.effects
            );
            assert_eq!(
                h.effects,
                vec![Effect::StartTimer {
                    id: TIMER_CABLE_CHECK,
                    after: WAIT_VOLTAGE_TIMEOUT
                }],
                "and nothing is actuated while it waits"
            );

            h.voltage(10.0);
            h.contactor_closed();
            h.voltage(450.0);
            h.self_test_passed();
            h.isolation(500_000.0);
            h.isolation(500_000.0);
            h.isolation(500_000.0);
            h.voltage(20.0);
            assert_eq!(h.stage(), CableCheck::Done);

            let e = &h.effects;
            let contactor_confirmed = at(e, "contactor confirmation", |x| {
                *x == Effect::CancelTimer {
                    id: TIMER_CONTACTOR_CONFIRM,
                }
            });
            let energized = at(e, "ramp up setpoint", setpoint);
            // Anchored to the contactor confirmation rather than to the
            // setpoint: searching forward from the setpoint runs on to the ramp
            // down bound, which makes the assertion below unfailable.
            let ramp_bound = at_after(e, contactor_confirmed, "ramp up bound", |x| {
                *x == Effect::StartTimer {
                    id: TIMER_CABLE_CHECK,
                    after: WAIT_VOLTAGE_TIMEOUT,
                }
            });
            let self_test = at(e, "self test", |x| matches!(x, Effect::ImdSelfTest { .. }));
            let sampling = at(e, "isolation monitor start", |x| *x == Effect::ImdStart);
            let ramp_down = at(e, "ramp down", |x| *x == Effect::SupplyOff);
            let verdict = at(e, "success verdict", |x| {
                *x == Effect::HlcUpdate(HlcUpdate::CableCheckFinished(true))
            });

            assert!(
                contactor_confirmed < energized,
                "the supply is energized only once the contactor is confirmed closed: {e:?}"
            );
            assert!(
                energized < ramp_bound,
                "the target is written before the wait on it is bounded: {e:?}"
            );
            assert!(
                energized < self_test,
                "the self test runs on an energized cable: {e:?}"
            );
            assert_eq!(
                e[energized],
                Effect::SetSupplySetpoint {
                    mode: SupplyMode::Export,
                    voltage_v: 450.0,
                    current_a: 2.0
                },
                "the ramp up doubles as the short circuit test, so its current \
                 stays at the cable check cap: {e:?}"
            );
            assert!(
                self_test < sampling,
                "isolation is sampled only on a monitor that has passed its self test: {e:?}"
            );
            assert!(
                ramp_down < verdict,
                "success is reported only once the cable is de-energized: {e:?}"
            );

            // The unwired shortcut is a step ordering too. `EvseManager::cable_check`
            // reports the status before the completion, so a consumer reading the
            // pair in order never mistakes a skipped check for a measured one.
            let mut dc = dc_port(config(), false, false);
            let effects = dc.begin_cable_check();
            assert_eq!(
                effects,
                vec![
                    Effect::HlcUpdate(HlcUpdate::IsolationStatus(IsolationStatus::NoImd)),
                    Effect::HlcUpdate(HlcUpdate::CableCheckFinished(true)),
                ],
                "isolation was never checked is said before the check is complete"
            );
        }

        /// Hazard 2. The contactor permission is two independent gates and both
        /// must hold. `Charger::run_state_machine`, the `EvseState::PrepareCharging`
        /// arm, calls `allow_power_on(true)` only under
        /// `hlc_allow_close_contactor and iec_allow_close_contactor`.
        ///
        /// The high level half is granted inside the cable check and only once
        /// the cable is de-energized, and any of the three data link requests
        /// or an explicit `ac_open_contactor` withdraws it again.
        #[test]
        fn the_contactor_needs_both_gates_and_neither_half_alone_will_do() {
            for (hlc, iec, expected) in [
                (false, false, false),
                (true, false, false),
                (false, true, false),
                (true, true, true),
            ] {
                let mut dc = dc_port(config(), true, false);
                dc.hlc_allows_close = hlc;
                dc.iec_allows_close = iec;
                assert_eq!(
                    dc.may_close_contactor(),
                    expected,
                    "hlc={hlc} iec={iec} must give {expected}"
                );
            }

            // The high level grant is behind the safe voltage gate, not merely
            // alongside it: `EvseManager::cable_check` calls
            // `set_hlc_allow_close_contactor(true)` after its first wait.
            let mut h = Harness::new(CableCheckOptions::default());
            h.begin(400.0);
            assert!(
                !h.dc.hlc_allows_close,
                "a cable still above the safe threshold earns no permission"
            );
            h.voltage(10.0);
            assert!(h.dc.hlc_allows_close, "a de-energized one earns it");

            // And it is withdrawable from both writers.
            h.dc.on_path_event(
                &h.session,
                PathEvent::AllowCloseContactor(false),
                now(),
            );
            assert!(
                !h.dc.hlc_allows_close,
                "an explicit withdrawal takes the permission away"
            );

            for request in [
                DataLinkRequest::Pause,
                DataLinkRequest::Terminate,
                DataLinkRequest::Error,
            ] {
                let mut h = Harness::new(CableCheckOptions::default());
                h.begin(10.0);
                assert!(h.dc.hlc_allows_close);
                h.dc
                    .on_path_event(&h.session, PathEvent::DataLink(request), now());
                assert!(
                    !h.dc.hlc_allows_close,
                    "{request:?} withdraws the permission"
                );
            }
        }

        /// Hazard 3. The isolation monitor starts before the samples it
        /// produces, and stays running past the end of the cable check into
        /// precharge. `EvseManager::cable_check` calls `imd_start()` before its
        /// sample loop and does not stop it on the success path; only the
        /// rejected `hack_pause_imd_during_precharge` did.
        ///
        /// Staying armed is the half that is easy to lose: a monitor stopped at
        /// the verdict leaves precharge unwatched.
        #[test]
        fn the_isolation_monitor_starts_before_the_samples_and_runs_on_into_precharge() {
            let mut h = Harness::new(CableCheckOptions::default());
            h.begin(400.0);
            h.voltage(10.0);
            h.contactor_closed();
            h.voltage(450.0);
            h.self_test_passed();

            let start = at(&h.effects, "isolation monitor start", |x| {
                *x == Effect::ImdStart
            });

            h.isolation(500_000.0);
            h.isolation(500_000.0);
            h.isolation(500_000.0);
            h.voltage(20.0);
            assert_eq!(h.stage(), CableCheck::Done);

            let sample_bound = at_after(&h.effects, start, "isolation sample bound", |x| {
                *x == Effect::StartTimer {
                    id: TIMER_CABLE_CHECK,
                    after: ISOLATION_SAMPLE_TIMEOUT,
                }
            });
            assert!(
                start < sample_bound,
                "the monitor is running before a sample is waited for: {:?}",
                h.effects
            );
            assert!(
                !h.effects[start..].contains(&Effect::ImdStop),
                "the monitor is never stopped between its start and the verdict, \
                 so it is still running when precharge begins: {:?}",
                h.effects
            );

            // It stops with the energy behind it, not before it.
            let effects = h
                .dc
                .on_path_event(&h.session, PathEvent::OpenContactorDc, now());
            let off = at(&effects, "supply off", |x| *x == Effect::SupplyOff);
            let stop = at(&effects, "isolation monitor stop", |x| {
                *x == Effect::ImdStop
            });
            assert!(
                off < stop,
                "the energy goes before the monitor watching it stops: {effects:?}"
            );
        }

        /// Hazard 4. The two over voltage thresholds are derived differently and
        /// reported in a fixed argument order.
        ///
        /// `EvseManager::get_emergency_over_voltage_threshold` steps off the
        /// **negotiated** maximum, the lesser of what the vehicle and the supply
        /// can do. `EvseManager::get_error_over_voltage_threshold` returns the
        /// vehicle's own maximum, unclamped by the supply. The call site passes
        /// them emergency first.
        ///
        /// The last row is the asymmetry that makes the pair worth pinning: with
        /// a supply weaker than the vehicle, the error limit sits **above** the
        /// emergency limit. A derivation that clamped both, or that swapped
        /// them, still looks plausible on every other row.
        #[test]
        fn the_over_voltage_thresholds_walk_the_step_table_and_keep_their_argument_order() {
            for (ev_max_v, evse_max_v, emergency_v, error_v) in [
                // Below the first step, and the step boundaries are strict.
                (400.0, 950.0, 550.0, 400.0),
                (500.0, 950.0, 550.0, 500.0),
                (501.0, 950.0, 825.0, 501.0),
                (750.0, 950.0, 825.0, 750.0),
                (751.0, 950.0, 935.0, 751.0),
                (850.0, 950.0, 935.0, 850.0),
                (851.0, 950.0, 1100.0, 851.0),
                (1000.0, 1500.0, 1100.0, 1000.0),
                (1001.0, 1500.0, 1375.0, 1001.0),
                // The supply is the weaker of the two: the emergency step
                // follows the negotiated minimum, the error limit does not.
                (900.0, 600.0, 825.0, 900.0),
            ] {
                let thresholds = wired_monitor().thresholds(ev_max_v, evse_max_v);
                assert_eq!(
                    (thresholds.emergency_v(), thresholds.error_v()),
                    (emergency_v, error_v),
                    "ev {ev_max_v} V against a supply of {evse_max_v} V"
                );
            }
        }

        /// Hazard 5. The delivered current is rate limited on the way up and not
        /// on the way down. `EvseManager::process_dc_ev_target_voltage_current`
        /// compares the **signed** difference against the allowance, so a fall
        /// reaches the supply at once while a rise is paid for in elapsed time.
        #[test]
        fn the_current_ramp_limits_every_rise_and_delays_no_fall() {
            // `deriving_config` ramps at twenty amperes per second, so half a
            // second buys ten amperes and no more.
            let mut dc = charging_dc();
            let session = charging_session(500.0);
            let t0 = now();

            dc.set_ev_target(&session, 400.0, 0.0, t0);

            let mut delivered = 0.0;
            for step in 1..=4u32 {
                let at_t = t0 + Duration::from_millis(500 * u64::from(step));
                let effects = dc.set_ev_target(&session, 400.0, 400.0, at_t);
                let next = setpoint_current(&effects).expect("a target reaches the supply");
                assert!(
                    next > delivered,
                    "step {step} made no progress towards the target"
                );
                assert!(
                    next - delivered <= 0.5 * 20.0 + f64::EPSILON,
                    "step {step} moved the current by {} A in half a second",
                    next - delivered
                );
                delivered = next;
            }
            assert_eq!(delivered, 40.0, "four half seconds at twenty amperes");

            let effects = dc.set_ev_target(&session, 400.0, 5.0, t0 + Duration::from_millis(2100));
            assert_eq!(
                setpoint_current(&effects),
                Some(5.0),
                "a fall is not rate limited and reaches the supply whole: {effects:?}"
            );
        }

        /// Hazard 6. Which of the two orderings applies depends on whether the
        /// supply is already on, and the applied setpoint cache has to be
        /// invalidated by the off that sits between them.
        ///
        /// Energizing writes the target **before** switching on, because
        /// `EvseManager::powersupply_DC_off` records that the supply resets its
        /// internal targets on the off transition; switching on first energizes
        /// the cable at whatever the supply defaults to. On a live supply the
        /// direction switch goes **first**, because it applies to the target
        /// that follows it: `EvseManager::powersupply_DC_set` issues
        /// `setMode(Import)` and falls through to `setImportVoltageCurrent`.
        ///
        /// The cache is the part most easily got wrong, because a stale one
        /// fails silently: the setpoint simply is not re-sent.
        #[test]
        fn a_direction_switch_precedes_its_setpoint_and_an_off_invalidates_the_cache() {
            let mut dc = charging_dc();

            let effects = dc.energize(400.0, 10.0, SupplyMode::Export);
            assert!(
                at(&effects, "setpoint", setpoint) < at(&effects, "supply mode", mode),
                "energizing writes the target before switching the supply on: {effects:?}"
            );

            let effects = dc.set_setpoint(400.0, 20.0, SupplyMode::Import);
            assert!(
                at(&effects, "supply mode", mode) < at(&effects, "setpoint", setpoint),
                "a direction switch on a live supply precedes the target it applies to: {effects:?}"
            );

            // The cache is real: an unchanged setpoint is not re-sent.
            assert!(
                dc.set_setpoint(400.0, 20.0, SupplyMode::Import).is_empty(),
                "an unchanged setpoint is not re-sent"
            );

            // And the off invalidates it, so the same pair is written again
            // rather than assumed still applied.
            dc.supply_off();
            let effects = dc.energize(400.0, 20.0, SupplyMode::Export);
            assert!(
                effects.iter().any(setpoint),
                "the off invalidated the cache, so re-energizing writes the target again: {effects:?}"
            );
        }

        /// Hazard 7, first half. Every shutdown removes the energy before it
        /// stops the monitors that would observe a fault during the removal, and
        /// before it releases the vehicle.
        ///
        /// The monitor half was unpinned until this test: the whole suite stayed
        /// green with both stops moved ahead of the `SupplyOff`.
        #[test]
        fn the_safe_state_removes_energy_before_stopping_the_monitors_or_releasing_the_vehicle() {
            let mut dc = dc_port(config(), true, true);

            let effects = dc.to_safe_state();

            let off = at(&effects, "supply off", |x| *x == Effect::SupplyOff);
            let released = at(&effects, "power on withdrawal", |x| {
                *x == Effect::AllowPowerOn(false)
            });
            let imd = at(&effects, "isolation monitor stop", |x| {
                *x == Effect::ImdStop
            });
            let over_voltage = at(&effects, "over voltage monitor stop", |x| {
                *x == Effect::OverVoltageStop
            });

            assert!(
                off < released,
                "the energy goes before the vehicle is released: {effects:?}"
            );
            assert!(
                off < imd,
                "the energy goes before the isolation monitor stops watching it: {effects:?}"
            );
            assert!(
                off < over_voltage,
                "the energy goes before the over voltage monitor stops watching it: {effects:?}"
            );

            // The end of current demand is a shutdown on the same rule.
            // `EvseManager` clears the charge loop and stops the monitor at
            // `subscribe_current_demand_finished`, and switches the supply off
            // from a second subscription to the same variable.
            let mut dc = dc_port(config(), true, true);
            let session = charging_session(500.0);
            let t0 = now();
            dc.on_path_event(&session, PathEvent::CurrentDemandStarted, t0);
            let effects = dc.on_path_event(
                &session,
                PathEvent::CurrentDemandFinished,
                t0 + Duration::from_secs(1),
            );
            let off = at(&effects, "supply off", |x| *x == Effect::SupplyOff);
            let over_voltage = at(&effects, "over voltage monitor stop", |x| {
                *x == Effect::OverVoltageStop
            });
            assert!(
                off < over_voltage,
                "current demand ends with the energy out before the monitor stops: {effects:?}"
            );

            // And the re-apply watchdog is cancelled last of all, which is what
            // keeps the energy removal ordering above independent of whether it
            // happened to be armed.
            let mut dc = dc_port(config(), true, true);
            dc.on_path_event(&session, PathEvent::CurrentDemandStarted, t0);
            let effects = dc.to_safe_state();
            let off = at(&effects, "supply off", |x| *x == Effect::SupplyOff);
            let cancelled = at(&effects, "watchdog cancellation", |x| {
                *x == Effect::CancelTimer {
                    id: TIMER_ENFORCE_LIMITS,
                }
            });
            assert!(
                off < cancelled,
                "the watchdog is cancelled after the energy is gone, not before: {effects:?}"
            );
        }

        /// Hazard 7, second half. A failed cable check removes the energy before
        /// it enters the abort, and entering the abort is what arms the bound on
        /// the de-energize wait the verdict is held behind.
        ///
        /// Arming that bound first starts the clock on a cable that is still
        /// live, so the timeout can expire and release a failure verdict while
        /// the supply has not yet been told to switch off. This was the second
        /// ordering the campaign broke with the whole suite green.
        #[test]
        fn a_failed_cable_check_removes_energy_before_it_arms_the_abort_bound() {
            let mut h = Harness::new(CableCheckOptions::default());
            h.begin(400.0);
            h.voltage(10.0);
            h.contactor_closed();
            h.voltage(450.0);

            // The self test verdict never arrives.
            let effects = h.timer(TIMER_CABLE_CHECK);
            assert_eq!(h.stage(), CableCheck::Abort { reported: false });

            let off = at(&effects, "supply off", |x| *x == Effect::SupplyOff);
            let bound = at(&effects, "de-energize bound", |x| {
                *x == Effect::StartTimer {
                    id: TIMER_CABLE_CHECK,
                    after: WAIT_VOLTAGE_TIMEOUT,
                }
            });

            assert!(
                off < bound,
                "the energy goes before the wait on it is bounded: {effects:?}"
            );
        }
    }
}
