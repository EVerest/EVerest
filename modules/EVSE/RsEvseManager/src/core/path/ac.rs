// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

//! AC power paths.
//!
//! Two implementations sharing one `Iec` reducer. Neither contains a test for
//! whether high level communication is enabled: that question was answered when
//! the implementation was selected.

use std::time::{Duration, Instant};

use crate::core::effect::{Effect, EffectId, EffectOutcome, SlacUpdate, TimerId};
use crate::core::event::{BspEvent, CpEdges, CpEvent};
use crate::core::hlc::{dlink, DataLinkRequest, DlinkPilot, PresentedMode};
use crate::core::path::iec::{
    pwm_duty_for_current_a, AcState, AcTimer, Iec, IecCommand, IecConfig, IecInput,
};
use crate::core::path::{PathEvent, PowerPath, SessionDuty, SessionProgress};
use crate::core::session::{PwmStart, Session, StopReason};

/// The five percent duty cycle that offers high level communication rather than
/// a current. `Charger.hpp` `PWM_5_PERCENT`.
pub const PWM_5_PERCENT: f64 = 0.05;

pub const TIMER_FIVE_PERCENT_FALLBACK: TimerId = TimerId(200);
pub const TIMER_WAIT_FOR_ENERGY: TimerId = TimerId(201);
pub const TIMER_STOPPING_CHARGING: TimerId = TimerId(202);
pub const TIMER_C1: TimerId = TimerId(203);
pub const TIMER_CP_STATE_F_UNLOCK: TimerId = TimerId(204);
/// One identity for the whole pilot detour, shared by all three of its steps.
pub const TIMER_T_STEP: TimerId = TimerId(205);
pub const TIMER_SWITCH_PHASES: TimerId = TimerId(206);
/// The reinitialization hold. `ac_with_soc` is the only producer today; see
/// `path::ac_with_soc`.
pub const TIMER_REINIT: TimerId = TimerId(207);
/// How long a high level session charges on with no budget behind it. `AcHlc`
/// is the only producer: the basic path has no high level session to grant it
/// to and stops at once.
pub const TIMER_HLC_NO_ENERGY: TimerId = TimerId(209);

/// A five percent offer falls back to a nominal duty cycle if the vehicle does
/// not establish high level communication in time.
pub const FIVE_PERCENT_FALLBACK: Duration = Duration::from_secs(10);

/// Bounds the wait for the energy manager to supply power before the
/// authorization loop gives up waiting and proceeds anyway.
pub const WAIT_FOR_ENERGY_IN_AUTH_LOOP: Duration = Duration::from_secs(5);

/// Bounds the graceful stop. When it expires, energy is removed under load
/// rather than waiting further. `Charger.cpp:1027-1032`.
pub const STOPPING_CHARGING_TIMEOUT: Duration = Duration::from_secs(20);

/// State E or F is held at least this long so the vehicle side notices it.
/// ISO 15118-3 table 3.
pub const T_STEP_EF: Duration = Duration::from_millis(4000);

/// X1 is held at least this long per IEC 61851-1.
pub const T_STEP_X1: Duration = Duration::from_millis(3000);

/// X1 is held briefly after leaving state F. This is an EV READY certification
/// requirement rather than an IEC one, which is the only reason the value
/// exists.
pub const STAY_IN_X1_AFTER_T_STEP_EF: Duration = Duration::from_millis(750);

/// The vehicle has this long to stop drawing power after the offer went away
/// before power is removed under load. `IECStateMachine.hpp:159`.
pub const POWER_OFF_UNDER_LOAD_IN_C1: Duration = Duration::from_secs(6);

/// The connector stays locked this long while the port signals state F.
/// `IECStateMachine.hpp:160`.
pub const UNLOCK_IN_STATE_F: Duration = Duration::from_secs(5);

/// Which identity a named timer carries.
pub fn timer_id(timer: AcTimer) -> TimerId {
    match timer {
        AcTimer::C1 => TIMER_C1,
        AcTimer::CpStateFUnlock => TIMER_CP_STATE_F_UNLOCK,
        AcTimer::StoppingCharging => TIMER_STOPPING_CHARGING,
        AcTimer::FivePercentFallback => TIMER_FIVE_PERCENT_FALLBACK,
        AcTimer::WaitForEnergy => TIMER_WAIT_FOR_ENERGY,
        AcTimer::TStepEf | AcTimer::TStepEfX1Pause | AcTimer::TStepX1 => TIMER_T_STEP,
        AcTimer::SwitchPhases(_) => TIMER_SWITCH_PHASES,
        AcTimer::Reinit(_) => TIMER_REINIT,
        AcTimer::HlcNoEnergy(_) => TIMER_HLC_NO_ENERGY,
    }
}

/// How long a named timer runs. The single table, so no arming site holds a
/// duration of its own.
pub fn timer_after(timer: AcTimer) -> Duration {
    match timer {
        AcTimer::C1 => POWER_OFF_UNDER_LOAD_IN_C1,
        AcTimer::CpStateFUnlock => UNLOCK_IN_STATE_F,
        AcTimer::StoppingCharging => STOPPING_CHARGING_TIMEOUT,
        AcTimer::FivePercentFallback => FIVE_PERCENT_FALLBACK,
        AcTimer::WaitForEnergy => WAIT_FOR_ENERGY_IN_AUTH_LOOP,
        AcTimer::TStepEf => T_STEP_EF,
        AcTimer::TStepEfX1Pause => STAY_IN_X1_AFTER_T_STEP_EF,
        AcTimer::TStepX1 => T_STEP_X1,
        // The three configured durations, so each travels on its timer rather
        // than being looked up here: `switch_3ph1ph_delay_s`,
        // `reinit_duration_ms` and `hlc_charge_loop_without_energy_timeout_s`.
        AcTimer::SwitchPhases(after) => after,
        AcTimer::Reinit(after) => after,
        AcTimer::HlcNoEnergy(after) => after,
    }
}

/// Timers the reducer owns the meaning of. Both AC paths route them the same
/// way, so the mapping exists once.
fn reducer_timer_input(id: TimerId) -> Option<IecInput> {
    match id {
        TIMER_C1 => Some(IecInput::CpStateC1TimeoutExpired),
        TIMER_CP_STATE_F_UNLOCK => Some(IecInput::CpStateFUnlockTimerExpired),
        TIMER_STOPPING_CHARGING => Some(IecInput::StoppingChargingTimeoutExpired),
        TIMER_SWITCH_PHASES => Some(IecInput::SwitchPhasesDelayExpired),
        _ => None,
    }
}

/// What the authorization loop knows when it decides how to leave a five percent
/// or X1 start. Ported from `Charger.cpp:377-570`, whose two halves differ only
/// in these facts.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct AcStartFacts {
    /// The offer currently on the pilot is five percent rather than nominal.
    pub five_percent: bool,
    /// Five percent that never falls back. Not IEC compliant, opt in only.
    pub enforced: bool,
    /// Authorization arrived over ISO 15118 rather than externally.
    pub pnc: bool,
    /// SLAC matching had started before authorization arrived.
    pub matching_started: bool,
    /// The vehicle already asked for power over ISO 15118, so an ISO session is
    /// live and dropping the offer would kill it.
    pub hlc_charging_active: bool,
    /// The bounded chance for high level communication to reach its charge loop
    /// has already been given.
    pub fallback_elapsed: bool,
    /// The mode an `ac_with_soc` port is presenting, or `None` on every other
    /// AC port.
    ///
    /// It short circuits the whole tree below, and that is the shape of the
    /// C++: `Charger::run_state_machine`'s `WaitingForAuthentication` case
    /// branches on `charge_mode` inside **both** authorization arms, and both
    /// of its DC branches take one line, "we always stay within 5 percent mode
    /// anyway", straight to `PrepareCharging`. Hoisting the branch above the
    /// authorization split is the same decision written once instead of twice.
    pub with_soc: Option<PresentedMode>,
}

/// How the authorization loop leaves the start variant it began with.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum AcStartDecision {
    /// Straight to preparing to charge. `keep_five_percent` says whether the
    /// offer stays at five percent or becomes nominal.
    Proceed { keep_five_percent: bool },
    /// State F for `T_STEP_EF`, X1 for `STAY_IN_X1_AFTER_T_STEP_EF`, then
    /// nominal. ISO 15118-3 figure 3.
    StepThroughEf,
    /// X1 for `T_STEP_X1`, then nominal. ISO 15118-3 figure 5.
    StepThroughX1,
    /// Give high level communication a bounded chance to reach its charge loop
    /// before the five percent offer is dropped.
    AwaitFallback,
}

/// The EIM and Plug and Charge decision tree, `Charger.cpp:377-570`.
///
/// The two authorization sources are two separate trees in the C++ and stay two
/// here. They are not the same tree with one differing leaf: EIM branches on
/// whether SLAC matching had started and honours the ISO 15118-3 figures, while
/// Plug and Charge ignores matching entirely and instead watches for a charge
/// loop that some vehicles never open. Merging them loses the second tree.
pub fn decide_ac_start(facts: AcStartFacts) -> AcStartDecision {
    // The `ac_with_soc` port, both of whose presented modes proceed at once and
    // neither of which ever runs a pilot detour.
    //
    // Presenting DC takes the DC branch of both authorization arms, which keeps
    // the five percent offer up. Presenting AC takes the AC branch with
    // `ac_hlc_enabled_current_session` false, because
    // `EvseManager::setup_AC_mode` is reached from `switch_AC_mode` and from
    // the `subscribe_dlink_error` arm and both pass `ac_hlc_enabled` false; the
    // C++ arm for that case says "HLC is disabled for this session. simply
    // proceed to PrepareCharging" and offers the nominal duty cycle.
    if let Some(presented) = facts.with_soc {
        return AcStartDecision::Proceed {
            keep_five_percent: presented == PresentedMode::Dc,
        };
    }

    // The enforced offer is deliberately not tested here. `Charger.cpp:396`
    // reads it from the external half only, and the Plug and Charge half at
    // `Charger.cpp:530-535` must keep its escape hatch: a vehicle that uses only
    // the Plug and Charge part of ISO 15118 and then closes the connection never
    // charges at all if the five percent offer is held up, and `ac_enforce_hlc`
    // is set on shipped hardware configurations. Losing a real vehicle's ability
    // to charge outweighs the inconsistency with the option's own wording.
    if facts.pnc {
        decide_plug_and_charge_start(facts)
    } else {
        decide_external_start(facts)
    }
}

/// Authorization arrived from outside ISO 15118. `Charger.cpp:404-487`.
///
/// This is the half the ISO 15118-3 figures describe, and the only half that
/// reads whether SLAC matching had started.
fn decide_external_start(facts: AcStartFacts) -> AcStartDecision {
    if facts.enforced {
        // Not standard compliant: the offer stays at five percent for the whole
        // session and is withdrawn only by a data link error.
        // `Charger.cpp:396-403`.
        return AcStartDecision::Proceed {
            keep_five_percent: true,
        };
    }

    if !facts.matching_started {
        // Nothing was listening on the pilot when authorization arrived.
        if facts.five_percent {
            // Withdraw the offer at once rather than waiting for a charge loop
            // that cannot start. Figure 3.
            return AcStartDecision::StepThroughEf;
        }
        // Figure 4 asks for a state F step on the way from X1 to nominal, which
        // the C++ declines at `Charger.cpp:425-433` on the grounds that basic
        // charging does X1 to nominal without one. Declined here too.
        return AcStartDecision::Proceed {
            keep_five_percent: false,
        };
    }

    if !facts.five_percent {
        // Already where nominal signalling begins, so there is nothing to step
        // through. Figure 6, `Charger.cpp:479-487`.
        return AcStartDecision::Proceed {
            keep_five_percent: false,
        };
    }

    if facts.hlc_charging_active {
        // The vehicle already asked for power over ISO 15118. Dropping to X1
        // would end that session, and vehicles that support the AC charge loop
        // then require a replug to recover. `Charger.cpp:459-465`.
        return AcStartDecision::Proceed {
            keep_five_percent: true,
        };
    }

    if !facts.fallback_elapsed {
        return AcStartDecision::AwaitFallback;
    }

    // Figure 5, `Charger.cpp:466-472`.
    AcStartDecision::StepThroughX1
}

/// Authorization arrived over ISO 15118. `Charger.cpp:527-564`.
///
/// This half reads neither the figures nor whether matching started. Some
/// vehicles use only the Plug and Charge part of ISO 15118 and then close the
/// connection, expecting basic charging to carry the session, so the offer is
/// held for a bounded observation window and the pilot detour is run if no
/// charge loop opens. `Charger.cpp:530-535` records that this contradicts the
/// standard on purpose, to keep Plug and Charge and EIM behaving alike, so the
/// detour is a compatibility requirement rather than a wasted step.
///
/// The enforced offer is not read here, matching `Charger.cpp:527-564`. Holding
/// five percent up on an enforcing port would close the escape hatch this half
/// exists to provide.
fn decide_plug_and_charge_start(facts: AcStartFacts) -> AcStartDecision {
    if facts.hlc_charging_active {
        // `Charger.cpp:536-541`, which raises the five percent offer rather than
        // merely keeping it: a session that started on X1 is moved onto the
        // offer a live ISO session expects.
        return AcStartDecision::Proceed {
            keep_five_percent: true,
        };
    }

    if !facts.fallback_elapsed {
        // `Charger.cpp:560-563` opens the observation window. It is opened for
        // an X1 start as much as a five percent one: the guard at
        // `Charger.cpp:545` reads only the window, never the start variant.
        return AcStartDecision::AwaitFallback;
    }

    // `Charger.cpp:545-559`. The pilot sequence is the nudge those vehicles
    // need, so it runs whichever offer the session started on.
    AcStartDecision::StepThroughEf
}

/// The bounded wait for the energy manager to supply a budget, shared by both
/// AC paths.
///
/// A vehicle told it may draw zero amperes reacts badly, so the loop waits.
/// Waiting without a bound is worse, so it gives up and proceeds.
///
/// `Charger.cpp:344-364` is the only site the constant appears at, and it gates
/// the whole wait on DC. AC instead waits unbounded later, in
/// `Charger.cpp:719-723`, where the loop simply breaks until a budget arrives.
/// Applying the same bound to AC is the one behavioral difference here, and it
/// only ever shortens a wait: a session that would have hung waiting for an
/// energy manager that never answers now starts instead, and the offer it
/// starts with is the invalid argument duty cycle `pwm_duty_for_current_a`
/// returns below the minimum, which is what the C++ `PrepareCharging` entry
/// signals for a budget it has not got (`:747`).
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
enum EnergyWait {
    #[default]
    NotWaiting,
    Waiting,
    /// The wait was given up on. This session will not wait again.
    GaveUp,
}

impl EnergyWait {
    /// Commands to emit, and whether the caller may proceed.
    ///
    /// The availability is the reducer's answer rather than one derived from
    /// the session here, because the C++ asks `power_available()`, which reads
    /// the stored maximum. That figure is a **magnitude**
    /// (`Charger.cpp:1382-1397`), so a discharge allowance is power available
    /// and a session limit read directly would read it as a negative.
    fn poll(&mut self, available: bool) -> (Vec<IecCommand>, bool) {
        if available {
            let cancel = if *self == EnergyWait::Waiting {
                vec![IecCommand::CancelTimer(AcTimer::WaitForEnergy)]
            } else {
                Vec::new()
            };
            *self = EnergyWait::NotWaiting;
            return (cancel, true);
        }
        match *self {
            EnergyWait::GaveUp => (Vec::new(), true),
            EnergyWait::Waiting => (Vec::new(), false),
            EnergyWait::NotWaiting => {
                *self = EnergyWait::Waiting;
                (vec![IecCommand::ArmTimer(AcTimer::WaitForEnergy)], false)
            }
        }
    }

    fn gave_up(&mut self) {
        if *self == EnergyWait::Waiting {
            *self = EnergyWait::GaveUp;
        }
    }
}

/// Translate reducer commands into effects. Shared by both AC paths so the
/// mapping exists once.
pub fn to_effects(commands: Vec<IecCommand>) -> Vec<Effect> {
    commands
        .into_iter()
        .map(|command| match command {
            IecCommand::Enable(on) => Effect::BspEnable(on),
            IecCommand::CpStateX1 => Effect::SetCpState(crate::core::effect::CpState::X1),
            IecCommand::CpStateF => Effect::SetCpState(crate::core::effect::CpState::F),
            IecCommand::CpStateE => Effect::SetCpState(crate::core::effect::CpState::E),
            IecCommand::AllowPowerOn(allow) => Effect::AllowPowerOn(allow),
            IecCommand::PwmOn(duty) => Effect::PwmOn(duty),
            IecCommand::PwmOff => Effect::PwmOff,
            IecCommand::SetOvercurrentLimitA(limit) => Effect::SetOvercurrentLimit(limit),
            IecCommand::SwitchThreePhases(three) => Effect::SwitchThreePhases(three),
            IecCommand::LockConnector => Effect::LockConnector,
            IecCommand::UnlockConnector => Effect::UnlockConnector,
            IecCommand::ArmTimer(timer) => Effect::StartTimer {
                id: timer_id(timer),
                after: timer_after(timer),
            },
            IecCommand::CancelTimer(timer) => Effect::CancelTimer {
                id: timer_id(timer),
            },
        })
        .collect()
}

/// The reducer input one pilot reading makes, given what the transition memory
/// derived from it.
///
/// State B is the one reading whose meaning is not in the reading:
/// `IECStateMachine::state_machine` (`IECStateMachine.cpp:198-204`) withdraws
/// power only out of C or D, and the
/// same level is also how a vehicle arrives (`:208-213`, delivered as
/// `on_session_start`) and how matching restarts (`:215-218`, delivered to the
/// SLAC layer). Those two reach the reducer by their own routes, so all this
/// owes them is the lock assertion every B makes.
fn cp_to_input(event: CpEvent, edges: CpEdges) -> IecInput {
    match event {
        CpEvent::A => IecInput::CarUnplugged,
        CpEvent::B if edges.requested_stop_power => IecInput::CarRequestedStopPower,
        CpEvent::B => IecInput::CpStateB,
        CpEvent::C => IecInput::CarRequestedPower,
        CpEvent::D => IecInput::CarRequestedVentilatedPower,
        CpEvent::E => IecInput::CpStateE,
        CpEvent::F => IecInput::CpStateF,
        CpEvent::PowerOn => IecInput::PowerOn,
        CpEvent::PowerOff => IecInput::PowerOff,
        CpEvent::Disconnected => IecInput::CarUnplugged,
    }
}

/// The resolved availability decision, as the arbitrated command rather than
/// the report that caused it. A losing report never gets this far, so a path
/// acts on every one it is handed.
/// The reducer input an AC path takes from a path event, or `None` where the
/// path has nothing to hand the reducer. Shared by both AC implementations so
/// neither can route one of the four and drop another.
fn iec_input(event: PathEvent) -> Option<IecInput> {
    match event {
        PathEvent::Enable => Some(IecInput::Enable),
        PathEvent::Disable => Some(IecInput::Disable),
        PathEvent::PauseRequested => Some(IecInput::PauseRequested),
        PathEvent::ResumeRequested => Some(IecInput::ResumeRequested),
        PathEvent::SwitchPhases { three_phases } => {
            Some(IecInput::SwitchPhasesRequested(three_phases))
        }
        // The DC readings and the high level communication stages. An AC path
        // has no supply and no isolation monitor to read, and the stages are
        // handled by `AcHlc` directly rather than by the reducer.
        PathEvent::HlcSessionSetup
        | PathEvent::MatchingStarted(_)
        | PathEvent::SlacMatched(_)
        | PathEvent::SlacErrorRoutine
        | PathEvent::StateOfCharge { .. }
        | PathEvent::SetupFinished
        | PathEvent::AllowCloseContactor(_)
        | PathEvent::DataLink(_)
        | PathEvent::OpenContactorDc
        | PathEvent::CableCheckRequired
        | PathEvent::PreChargeStarted
        | PathEvent::CurrentDemandStarted
        | PathEvent::CurrentDemandFinished
        | PathEvent::StopFromEv
        | PathEvent::SupplyVoltage { .. }
        | PathEvent::DcEvTarget { .. }
        | PathEvent::DcDynamicChargeMode(_)
        | PathEvent::DcEvMaximumLimits(_)
        | PathEvent::DcEnforcedLimits { .. }
        | PathEvent::DcExportVoltageRange { .. }
        | PathEvent::Isolation(_)
        | PathEvent::IsolationSelfTest(_)
        | PathEvent::OverVoltageMeasurement { .. }
        | PathEvent::MeterVoltage { .. }
        | PathEvent::BidirectionalWithdrawn => None,
    }
}

/// Whether a reducer command takes the offer off the pilot.
///
/// `Charger::cp_state_X1` (`Charger.cpp:1258-1265`) and `Charger::cp_state_F`
/// (`:1267-1274`) are each one action in the C++: both clear
/// `shared_context.pwm_running` and signal the pilot in the same call. The
/// reducer splits that into two commands, so anything deciding about the
/// withdrawal has to decide about the whole of it. One predicate, so it cannot
/// be half applied.
fn withdraws_offer(command: &IecCommand) -> bool {
    matches!(
        command,
        IecCommand::PwmOff | IecCommand::CpStateX1 | IecCommand::CpStateF | IecCommand::CpStateE
    )
}

/// The edge detector both AC paths run their reducer through.
///
/// It owns the `Iec` rather than sitting beside it, so there is no route to
/// `handle` that skips the observation. One derivation, shared: giving each AC
/// path a copy would recreate the multiple derivation problem the single
/// `CpTracker` above the three paths was introduced to avoid.
pub struct IecEdge {
    iec: Iec,
    progress: SessionProgress,
    /// Whether an unread `StoppingCharging` entry is outstanding.
    ///
    /// The state entered is not enough on its own: the C++ discharges the
    /// entry's duties inside `if (initialize_state)`, so a second input arriving
    /// while the session is still stopping crosses no edge and owes nothing.
    ///
    /// Accumulated rather than assigned per call. One route can run `handle`
    /// twice before emitting anything (`AcHlc::proceed`), and an entry that the
    /// second call overwrote would be lost with nothing to notice it. The cost
    /// is that a path which never reads it, which is `AcBasic`, leaves it
    /// standing; that is inert, and reading it there would be reading a fact
    /// about a session that path cannot have.
    entered_stopping_charging: bool,
    /// Whether an unread `ChargingPausedEvse` entry is outstanding.
    ///
    /// Recorded for the same reason and on the same terms as the entry above.
    /// The C++ entry withdraws the offer under its own gate
    /// (`Charger.cpp:933-938`), which is a different gate from the stop
    /// entry's, so the two edges are reported apart rather than as one.
    entered_charging_paused_evse: bool,
}

impl IecEdge {
    pub fn new(config: IecConfig) -> Self {
        Self {
            iec: Iec::new(config),
            progress: SessionProgress::new(),
            entered_stopping_charging: false,
            entered_charging_paused_evse: false,
        }
    }

    /// Whether the last `handle` entered `StoppingCharging`, and clear.
    ///
    /// Taken rather than read so that the entry's duties are discharged once,
    /// which is what `initialize_state` buys the C++.
    fn take_entered_stopping_charging(&mut self) -> bool {
        std::mem::take(&mut self.entered_stopping_charging)
    }

    /// Whether the last `handle` entered `ChargingPausedEvse`, and clear.
    fn take_entered_charging_paused_evse(&mut self) -> bool {
        std::mem::take(&mut self.entered_charging_paused_evse)
    }

    /// Read from the progress rather than from the reducer, so the state this
    /// path reports is the one that only `SessionProgress::enter` writes. A
    /// transition the observation below failed to walk would show up here as a
    /// stale state rather than only as a missing duty.
    pub fn state(&self) -> AcState {
        self.progress.state()
    }

    /// Run one reducer input and record what its state edges are due.
    ///
    /// One input can cross more than one edge: an unplug during a charge routes
    /// `Charging` to `StoppingCharging` to `Finished` to a resting state inside
    /// a single call, the way `Charger.cpp:1092` re-runs its switch until the
    /// state settles. Comparing only the state before the call with the state
    /// after it would collapse that into one edge and lose the stopping
    /// announcement the middle state owes, so the reducer reports the states it
    /// entered and this walks them in order.
    ///
    /// The commands come back unchanged, so a call site that shapes or extends
    /// them is unaffected by the observation.
    pub fn handle(&mut self, input: IecInput) -> Vec<IecCommand> {
        let commands = self.iec.handle(input);
        for entered in self.iec.take_transitions() {
            self.entered_stopping_charging |= entered == AcState::StoppingCharging;
            self.entered_charging_paused_evse |= entered == AcState::ChargingPausedEvse;
            self.progress.enter(entered);
        }
        debug_assert_eq!(self.progress.state(), self.iec.state());
        commands
    }

    /// The current limit, which changes no state and therefore raises no duty.
    pub fn set_current_limit_a(&mut self, limit_a: f64) -> Vec<IecCommand> {
        self.iec.set_current_limit_a(limit_a)
    }

    /// What the pilot is offering. Forwarded rather than reached through, so
    /// `iec` stays private to this edge detector.
    pub fn signalled_current_a(&self) -> f64 {
        self.iec.signalled_current_a()
    }

    /// The negotiated limit, for the caller that wants it instead of the
    /// pilot's offer.
    pub fn current_limit_a(&self) -> f64 {
        self.iec.current_limit_a()
    }

    /// The cable's rating, forwarded for the same reason the limit is.
    pub fn set_cable_rating_a(&mut self, ampacity_a: f64) -> Vec<IecCommand> {
        self.iec.set_cable_rating_a(ampacity_a)
    }

    /// Whether that limit is one a charge can run on. Forwarded for the same
    /// reason the two above are.
    pub fn power_available(&self) -> bool {
        self.iec.power_available()
    }

    pub fn take_duties(&mut self) -> Vec<SessionDuty> {
        self.progress.take_duties()
    }

    pub fn take_entered(&mut self) -> Vec<AcState> {
        self.progress.take_entered()
    }
}

/// AC without high level communication. IEC 61851 only.
pub struct AcBasic {
    iec: IecEdge,
    authorized: bool,
    energy_wait: EnergyWait,
}

impl AcBasic {
    pub fn new(config: IecConfig) -> Self {
        Self {
            iec: IecEdge::new(config),
            authorized: false,
            energy_wait: EnergyWait::default(),
        }
    }

    pub fn state(&self) -> AcState {
        self.iec.state()
    }

    /// The authorization loop an IEC only port needs, which is the C++ loop with
    /// every high level communication branch removed: authorization has arrived
    /// and there is a budget to offer, so start the transaction and offer the
    /// nominal duty cycle.
    ///
    /// Re-entrant on purpose. Both facts it waits on arrive as separate events,
    /// and whichever arrives last completes the decision.
    /// Every field whose lifetime is one session, cleared in one place.
    ///
    /// The C++ clears the same set at one point too, the `Idle` entry at
    /// `Charger.cpp:214-235`, which is the end of a session rather than the
    /// start of the next one. Both routes into it, the unplug and the port
    /// leaving service, come through here, so a new per session field is
    /// cleared by being declared here rather than by every future route
    /// remembering it.
    fn end_session(&mut self) {
        self.authorized = false;
        self.energy_wait = EnergyWait::default();
    }

    fn run_authorization_loop(&mut self) -> Vec<Effect> {
        if !self.authorized || self.iec.state() != AcState::WaitingForAuthentication {
            return Vec::new();
        }
        let (mut commands, proceed) = self.energy_wait.poll(self.iec.power_available());
        if !proceed {
            return to_effects(commands);
        }
        commands.extend(self.iec.handle(IecInput::AuthorizationAccepted));
        commands.extend(self.iec.handle(IecInput::TransactionStarted));
        to_effects(commands)
    }
}

impl PowerPath for AcBasic {
    fn take_entered_states(&mut self) -> Vec<AcState> {
        self.iec.take_entered()
    }

    fn state(&self) -> AcState {
        self.iec.state()
    }

    fn name(&self) -> &'static str {
        "AcBasic"
    }

    /// `hlc_charging_active` is false on this path by construction, so
    /// `Charger.cpp:1928-1929` takes its first branch and the answer is the
    /// pilot's alone.
    fn signalled_current_a(&self) -> f64 {
        self.iec.signalled_current_a()
    }

    /// No DC supply on this path, so there is no voltage target to name. Zero
    /// is the honest answer rather than a placeholder: the enforced limits
    /// handler multiplies by it only on a path that has one.
    fn target_voltage_v(&self) -> f64 {
        0.0
    }

    /// `Charger.cpp:217-224` decides this at the `Idle` entry from the charge
    /// mode, and a basic AC port is never in the branch that raises it: the
    /// only writer on AC is `v2g_setup_finished`, which needs a stack this
    /// deployment does not have. `config::resolve` pairs this path with no
    /// high level communication port, so nothing can raise it here.
    fn hlc_charging_active(&self) -> bool {
        false
    }

    /// The AC branch of `Charger::power_available`, which is the reducer's own
    /// answer: it holds the limit and the cable rating the C++ compares.
    fn power_available(&self) -> bool {
        self.iec.power_available()
    }

    /// `EvseManager::init` seeds `fake_dc_enabled` from `config.ac_with_soc`,
    /// and that key selects `AcWithSoc` instead of this path.
    fn presents_fake_dc(&self) -> bool {
        false
    }

    fn on_startup(&mut self) -> Vec<Effect> {
        to_effects(self.iec.handle(IecInput::StartupComplete))
    }

    fn on_session_start(&mut self, _session: &Session, _now: Instant) -> Vec<Effect> {
        let mut effects = to_effects(self.iec.handle(IecInput::CarPluggedIn));
        // An authorization that was the first user interaction is already held
        // and the vehicle has just supplied the other half of the pair. The
        // C++ needs no equivalent because `run_state_machine` re-reads
        // `flag_authorized` on every pass through `WaitingForAuthentication`;
        // here the decision runs from whichever of the two facts arrives last,
        // and without this the plug in is not one of them. `Dc` already runs
        // its loop from this entry point for the same reason.
        effects.extend(self.run_authorization_loop());
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
            BspEvent::Cp(cp) => {
                let input = cp_to_input(*cp, edges);
                if input == IecInput::CarUnplugged {
                    // `Charger.cpp:231-232`. The reducer clears its own
                    // authorization and transaction; this clears the path's.
                    // Errors are cleared on an unplug too, in `faults`, which
                    // owns them.
                    self.end_session();
                }
                to_effects(self.iec.handle(input))
            }
            // The cable's own rating, which caps what a socket may offer.
            BspEvent::PpAmpacity(ampacity_a) => {
                to_effects(self.iec.set_cable_rating_a(*ampacity_a))
            }
            _ => Vec::new(),
        }
    }

    fn on_limits_changed(&mut self, session: &Session, _now: Instant) -> Vec<Effect> {
        let mut commands = self.iec.set_current_limit_a(session.limits.max_current_a);
        // `Charger.cpp:867-871`, the basic AC branch of the `Charging` arm's no
        // energy test: "Stop immediately in basic AC mode". No high level
        // session can be running on this path, so the timeout the other branch
        // grants never applies here.
        commands.extend(self.iec.handle(if self.iec.power_available() {
            IecInput::EnergyRestored
        } else {
            IecInput::EnergyWithdrawn
        }));
        let mut effects = to_effects(commands);
        effects.extend(self.run_authorization_loop());
        effects
    }

    fn on_stop(&mut self, _session: &Session, _reason: StopReason, _now: Instant) -> Vec<Effect> {
        to_effects(self.iec.handle(IecInput::StopRequested))
    }

    fn on_timer(&mut self, _session: &Session, id: TimerId, _now: Instant) -> Vec<Effect> {
        if let Some(input) = reducer_timer_input(id) {
            return to_effects(self.iec.handle(input));
        }
        if id == TIMER_WAIT_FOR_ENERGY {
            self.energy_wait.gave_up();
            return self.run_authorization_loop();
        }
        Vec::new()
    }

    fn on_effect_done(
        &mut self,
        _session: &Session,
        _id: Option<EffectId>,
        _outcome: &EffectOutcome,
        _now: Instant,
    ) -> Vec<Effect> {
        Vec::new()
    }

    fn on_path_event(
        &mut self,
        _session: &Session,
        event: PathEvent,
        _now: Instant,
    ) -> Vec<Effect> {
        // Every arm this path acts on is a reducer input, so the mapping is
        // shared rather than restated. `iec_input` is itself exhaustive, which
        // is where a new variant becomes a compile error for both AC paths.
        let Some(input) = iec_input(event) else {
            // Nothing an IEC only port can act on: `config::resolve` pairs it
            // with no high level communication port, so no ISO 15118 stage
            // reaches it, and it drives no DC supply or isolation monitor.
            // SLAC's own facts, the error routine among them, are out of reach
            // for the same reason one step further back: `EvseManager.cpp:174-177`
            // clears `slac_enabled` unless one of `ac_hlc_enabled`,
            // `ac_with_soc` or DC mode is set, and this path is what is left
            // when none of them is.
            return Vec::new();
        };
        if input == IecInput::Disable {
            self.end_session();
        }
        to_effects(self.iec.handle(input))
    }

    fn take_session_duties(&mut self) -> Vec<SessionDuty> {
        self.iec.take_duties()
    }

    fn to_safe_state(&mut self) -> Vec<Effect> {
        to_effects(self.iec.handle(IecInput::ErrorShutdown))
    }
}

/// AC with ISO 15118 high level communication over the control pilot.
///
/// The five percent choice is a field rather than a separate implementation
/// because it changes while a session is live: a five percent offer falls back
/// to nominal on timeout, while an enforced offer never does.
pub struct AcHlc {
    iec: IecEdge,
    five_percent: bool,
    enforced: bool,
    authorized: bool,
    pnc: bool,
    matching_started: bool,
    hlc_charging_active: bool,
    hlc_allow_close_contactor: bool,
    /// High level communication failed on this plug in, so it is not offered
    /// again until the cable comes out. `hlc_failed` in the C++.
    ///
    /// Set by `Charger::dlink_error` and cleared at the `Idle` entry of
    /// `Charger::run_state_machine`, which is the unplug and **not** the end of
    /// a session: `EvseState::Finished` reaches `Idle` only once
    /// `flag_ev_plugged_in` has gone. A session that failed therefore does not
    /// get a fresh attempt while the same vehicle is still connected, which is
    /// what makes a vehicle that cannot carry ISO 15118 finish as IEC 61851
    /// basic charging instead of being offered the five percent duty cycle for
    /// the life of the plug in.
    ///
    /// Written in exactly two places, one for each edge, and both of them the
    /// C++'s own: `on_data_link` sets it and `end_session` clears it. Read in
    /// exactly the two places the C++ reads it, `derive_offer` and `shape`.
    hlc_failed: bool,
    fallback: Fallback,
    energy_wait: EnergyWait,
    pilot_step: PilotStep,
    /// Why the pilot detour is running, which decides what its last step does.
    detour: Detour,
    /// Whether a duty cycle currently stands on the pilot.
    ///
    /// `shared_context.pwm_running` in the C++, which the data link error
    /// branches on (`Charger.cpp:2075`). The reducer holds one of these too,
    /// but only for the states it considers eligible to carry an offer, and the
    /// five percent offer is raised in `WaitingForAuthentication`, which is not
    /// one of them. So this path tracks the offer it actually makes, written in
    /// exactly one place: `emit`, which every effect this path produces passes
    /// through.
    pwm_offered: bool,
    /// A power request the contactor gate is holding until high level
    /// communication grants its half of the permission.
    withheld_power_request: Option<IecInput>,
    /// Whether the no budget deadline is running,
    /// `internal_context.hlc_charge_loop_no_energy_timeout_running`
    /// (`Charger.cpp:840`). Held so the deadline is armed once for a run of
    /// short budgets rather than restarted by each one, which is what the C++
    /// flag buys its arm.
    no_energy_deadline: bool,
    /// How long that deadline runs, `hlc_charge_loop_without_energy_timeout_s`.
    /// Read off the reducer's config at construction, because the duration
    /// travels on the timer it arms.
    no_energy_timeout: Duration,
    /// The mode an `ac_with_soc` port is presenting, `None` on an ordinary AC
    /// port.
    ///
    /// The one runtime mode in the module, and this is a reader rather than its
    /// owner: `path::ac_with_soc::AcWithSoc` holds the mode and
    /// `set_presented` is the only writer here. Two answers depend on it, and
    /// both are the same C++ `charge_mode` branch: whether a five percent offer
    /// stands (the `WaitingForAuthentication` entry of `Charger::run_state_machine`) and how the authorization loop leaves the
    /// start (`decide_ac_start`).
    with_soc: Option<PresentedMode>,
}

/// Why the pilot detour is running.
///
/// The C++ carries the same fact as data, `internal_context.t_step_EF_return_state`,
/// which the authorization loop sets to `PrepareCharging`
/// (`Charger.cpp:415`, `:551`) and `Charger::dlink_error` sets to
/// `WaitingForAuthentication` (`:2101`).
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
enum Detour {
    /// Withdrawing the offer on the way into charging.
    #[default]
    IntoCharging,
    /// `[V2G3-M07-09]`: back to waiting for authentication, because the data
    /// link failed and the session restarts.
    BackToAuthentication,
    /// Back to the state the detour left, with the offer it left standing
    /// restored and nothing else moved.
    ///
    /// `Charger::request_error_sequence` (`Charger.cpp:2133-2147`) sets the
    /// return state to whichever of the two live states the port was already
    /// in, so the detour changes no state at all: SLAC asked for a pilot kick
    /// so matching could start again, not for a session decision.
    BackToTheStateItLeft,
}

/// The pilot detour that withdraws a five percent offer, and the step of it
/// currently under way. `Charger.cpp:607-670`.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
enum PilotStep {
    #[default]
    None,
    /// X1 held for `T_STEP_X1` ahead of state F, which is the order
    /// `[V2G3-M07-05]` asks for and the order `Charger::dlink_error` builds at
    /// `Charger.cpp:2096-2099`.
    X1BeforeStateF,
    /// State F held for `T_STEP_EF` so the vehicle side notices the offer went
    /// away.
    StateF,
    /// X1 held for `STAY_IN_X1_AFTER_T_STEP_EF` on the way out of state F.
    X1AfterStateF,
    /// X1 held for `T_STEP_X1`, with no state F before it.
    X1,
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
enum Fallback {
    #[default]
    NotArmed,
    Armed,
    /// The charge loop was given its bounded chance and did not start.
    Elapsed,
}

impl AcHlc {
    pub fn new(config: IecConfig, pwm_start: PwmStart) -> Self {
        Self {
            iec: IecEdge::new(config),
            five_percent: matches!(
                pwm_start,
                PwmStart::FivePercent | PwmStart::FivePercentEnforced
            ),
            enforced: matches!(pwm_start, PwmStart::FivePercentEnforced),
            authorized: false,
            pnc: false,
            matching_started: false,
            hlc_charging_active: false,
            hlc_allow_close_contactor: false,
            hlc_failed: false,
            fallback: Fallback::default(),
            energy_wait: EnergyWait::default(),
            pilot_step: PilotStep::default(),
            detour: Detour::default(),
            pwm_offered: false,
            withheld_power_request: None,
            no_energy_deadline: false,
            no_energy_timeout: config.hlc_no_energy_timeout,
            with_soc: None,
        }
    }

    /// The `Charging` arm's no budget test, `Charger.cpp:836-875`.
    ///
    /// The split the C++ makes there is between a session the vehicle is
    /// talking to and one it is not: a high level session is given
    /// `hlc_charge_loop_without_energy_timeout_s` to see a budget arrive before
    /// the charge is stopped, a basic one stops at once (`:867-871`), and a
    /// configured zero also stops at once (`:861-866`). Which of the two this
    /// path is running is the same pair of flags the C++ reads,
    /// `hlc_charging_active or hlc_use_5percent_current_session` (`:837`).
    ///
    /// Only from `Charging`, because that is the only arm the test lives in.
    /// The deadline is cancelled by a budget arriving, which is the C++ `else`
    /// at `:874`, and it is armed once for a run of short budgets rather than
    /// restarted by each one.
    ///
    /// **The trigger is narrower than the C++'s.** This runs when a budget is
    /// reported, where the C++ re-tests on every pass of a loop that polls, so
    /// a budget that stops being available without a new report arriving is not
    /// seen here. There is one such producer: the expired budget fallback at
    /// `:2112-2123`, which zeroes the maximum when `valid_until` has passed.
    /// Nothing in this port expires a budget yet, so the producer does not
    /// exist here either; a reader that adds one owes this the same call.
    fn reassess_energy(&mut self) -> Vec<IecCommand> {
        if self.iec.power_available() {
            let mut commands = Vec::new();
            if std::mem::take(&mut self.no_energy_deadline) {
                commands.push(IecCommand::CancelTimer(AcTimer::HlcNoEnergy(
                    self.no_energy_timeout,
                )));
            }
            commands.extend(self.iec.handle(IecInput::EnergyRestored));
            return commands;
        }
        if self.iec.state() != AcState::Charging {
            return Vec::new();
        }
        let hlc_session = self.hlc_charging_active || self.five_percent;
        if !hlc_session || self.no_energy_timeout.is_zero() {
            return self.iec.handle(IecInput::EnergyWithdrawn);
        }
        if self.no_energy_deadline {
            return Vec::new();
        }
        self.no_energy_deadline = true;
        vec![IecCommand::ArmTimer(AcTimer::HlcNoEnergy(
            self.no_energy_timeout,
        ))]
    }

    /// The `ac_with_soc` mode this path is now carrying.
    ///
    /// Written by `path::ac_with_soc::AcWithSoc` and by nothing else, which is
    /// what keeps one writer of the mode in the module. It changes no state and
    /// produces no effects: the two answers it moves are both re-derived at
    /// their own read sites, the offer on the next entry into
    /// `WaitingForAuthentication` and the start decision on the next pass of
    /// the authorization loop, exactly as `Charger::setup` leaves them.
    pub fn set_presented(&mut self, mode: PresentedMode) {
        self.with_soc = Some(mode);
    }

    /// Enter the reinitialization state. See `iec::Iec::reinit_started`.
    ///
    /// The decision to start one is `AcWithSoc`'s, because its two refusals
    /// read the SLAC link and the board's control pilot state E support and
    /// neither is a fact this path holds. What is here is the entry itself.
    pub fn start_reinit(&mut self) -> Vec<Effect> {
        let commands = self.iec.handle(IecInput::ReinitStarted);
        self.emit(commands)
    }

    pub fn state(&self) -> AcState {
        self.iec.state()
    }

    pub fn five_percent_active(&self) -> bool {
        self.five_percent
    }

    /// High level communication grants or withdraws its half of the contactor
    /// permission. `Charger.cpp:2123-2127`, reached from
    /// `EvseManager.cpp:395-403`.
    fn allow_close_contactor(&mut self, allow: bool) -> Vec<Effect> {
        self.hlc_allow_close_contactor = allow;
        if !allow {
            return Vec::new();
        }
        match self.withheld_power_request.take() {
            Some(input) => {
                let commands = self.iec.handle(input);
                self.emit(commands)
            }
            None => Vec::new(),
        }
    }

    /// The single conversion point from reducer commands to effects.
    ///
    /// It shapes the duty cycle for a standing five percent offer and records
    /// whether one now stands on the pilot. Every effect this path produces
    /// passes through here, so `pwm_offered` cannot go stale the way a field
    /// updated at each call site would.
    fn emit(&mut self, commands: Vec<IecCommand>) -> Vec<Effect> {
        let mut commands = self.shape(commands);
        // Both edges are taken on every pass, never conditionally, so neither
        // can be left standing for a later pass to misread.
        let paused = self.iec.take_entered_charging_paused_evse();
        let stopping = self.iec.take_entered_stopping_charging();
        if self.hlc_charging_active && (stopping || paused) {
            // `Charger.cpp:1013-1023` and `:933-938`: withdrawing the offer is
            // the branch **not** taken while high level communication carries
            // the session. It would kill the ISO session before the vehicle
            // could act on the request it is about to receive, and
            // `Charger::error_shutdown` (`:2277`) says the same thing for the
            // fault route that also lands in stopping.
            commands.retain(|command| !withdraws_offer(command));
        }
        for command in &commands {
            if matches!(command, IecCommand::PwmOn(_)) {
                self.pwm_offered = true;
            } else if withdraws_offer(command) {
                self.pwm_offered = false;
            }
        }
        to_effects(commands)
    }

    /// The two per session offer facts, derived together because the entry
    /// into `WaitingForAuthentication` assigns them together and gates both on
    /// the same conjunction.
    ///
    /// `ac_hlc_enabled_current_session = config_context.ac_hlc_enabled and not
    /// hlc_failed`, and both `ac_enforce_hlc`'s effect and
    /// `hlc_use_5percent_current_session` are reachable only inside it: the
    /// authorization loop's enforced branch sits under
    /// `if (ac_hlc_enabled_current_session)` and the offer is assigned under
    /// the same test. `ac_hlc_enabled` is the setting that selected this path
    /// rather than `AcBasic`, so the latch is all that is left of the
    /// conjunction here.
    ///
    /// This is the first of the latch's two read sites. Both of its callers are
    /// an entry into `WaitingForAuthentication`: the plug in and the data link
    /// error restart.
    fn derive_offer(&mut self, session: &Session) {
        self.enforced = !self.hlc_failed
            && matches!(session.profile.pwm_start, PwmStart::FivePercentEnforced);
        // Order matters and is the C++'s: the enforced fact is assigned first
        // and the offer's "already authorized" escape then reads it.
        self.five_percent = self.derive_five_percent(session);
    }

    /// The five percent offer as the entry into `WaitingForAuthentication`
    /// derives it.
    ///
    /// `hlc_use_5percent_current_session` is re-derived from configuration on
    /// every entry into that state and then withdrawn again when an
    /// authorization is already in hand and the deployment does not enforce the
    /// offer. Both the plug in and the data link error restart run through that
    /// entry, so both read it here, through `derive_offer`, which is this
    /// function's only caller.
    fn derive_five_percent(&self, session: &Session) -> bool {
        // The entry derives the offer from the charge mode first, and its DC
        // branch sets the flag unconditionally rather than reading
        // `ac_hlc_use_5percent` at all. The AC branch's own
        // "already authorized, so no five percent" escape is explicitly
        // `charge_mode == ChargeMode::AC`, so presenting DC keeps the offer
        // whatever else is true.
        //
        // Presenting AC withdraws it for the same reason, one branch up:
        // `hlc_use_5percent_current_session` is assigned only inside
        // `if (ac_hlc_enabled_current_session)`, and the flip's
        // `setup_AC_mode(false)` leaves that false.
        if let Some(presented) = self.with_soc {
            return presented == PresentedMode::Dc;
        }
        if self.hlc_failed {
            // "HLC is disabled for this session. simply proceed to
            // PrepareCharging", which is the branch the authorization loop
            // takes once `ac_hlc_enabled_current_session` is false, and it
            // offers the nominal duty cycle. The mode branch above this is
            // deliberately higher: the charge mode's DC arm assigns the offer
            // unconditionally and derives
            // `ac_hlc_enabled_current_session` not at all, which is why the
            // latch has no DC reader and `main` has no DC fallback.
            return false;
        }
        let asked = matches!(
            session.profile.pwm_start,
            PwmStart::FivePercent | PwmStart::FivePercentEnforced
        );
        asked && (!self.authorized || self.enforced)
    }

    fn facts(&self) -> AcStartFacts {
        AcStartFacts {
            five_percent: self.five_percent,
            enforced: self.enforced,
            pnc: self.pnc,
            matching_started: self.matching_started,
            hlc_charging_active: self.hlc_charging_active,
            fallback_elapsed: self.fallback == Fallback::Elapsed,
            with_soc: self.with_soc,
        }
    }

    /// The authorization loop. Re-entrant on purpose: every fact the decision
    /// reads arrives as its own event, so whichever arrives last runs the
    /// decision that the C++ reaches by polling all of them every 100 ms.
    fn run_authorization_loop(&mut self) -> Vec<Effect> {
        if !self.authorized
            || self.pilot_step != PilotStep::None
            || self.iec.state() != AcState::WaitingForAuthentication
        {
            return Vec::new();
        }

        let (mut commands, proceed) = self.energy_wait.poll(self.iec.power_available());
        if !proceed {
            return self.emit(commands);
        }

        match decide_ac_start(self.facts()) {
            AcStartDecision::Proceed { keep_five_percent } => {
                self.five_percent = keep_five_percent;
                commands.extend(self.proceed());
            }
            AcStartDecision::StepThroughEf => {
                self.five_percent = false;
                self.detour = Detour::IntoCharging;
                commands.extend(self.enter_pilot_step(PilotStep::StateF));
            }
            AcStartDecision::StepThroughX1 => {
                self.five_percent = false;
                self.detour = Detour::IntoCharging;
                commands.extend(self.enter_pilot_step(PilotStep::X1));
            }
            AcStartDecision::AwaitFallback => {
                if self.fallback == Fallback::NotArmed {
                    self.fallback = Fallback::Armed;
                    commands.push(IecCommand::ArmTimer(AcTimer::FivePercentFallback));
                }
            }
        }
        self.emit(commands)
    }

    /// `Charger::request_error_sequence` (`Charger.cpp:2133-2147`), which SLAC
    /// asks for when matching needs to start again.
    ///
    /// The pilot steps through state F and comes back to the state it left,
    /// with the offer it left standing restored; SLAC is reset on the way in,
    /// which the C++ signals from inside this same function and therefore only
    /// when the state admitted the request. The contactor permission is
    /// deliberately untouched, which is what makes this a different route from
    /// the data link error beside it.
    ///
    /// Refused from every other state, as the C++ refuses it: the two it names
    /// are the two in which a pilot kick can still lead anywhere.
    fn request_error_sequence(&mut self) -> Vec<Effect> {
        if !matches!(
            self.iec.state(),
            AcState::WaitingForAuthentication | AcState::PrepareCharging
        ) {
            return Vec::new();
        }
        self.detour = Detour::BackToTheStateItLeft;
        let commands = self.enter_pilot_step(PilotStep::StateF);
        let mut effects = self.emit(commands);
        effects.push(Effect::SlacUpdate(SlacUpdate::Reset));
        effects
    }

    /// The pilot level the port carries once the error sequence returns.
    ///
    /// Two C++ statements, and one place for them here. The `T_step_EF` exit
    /// restores `t_step_EF_return_pwm` (`Charger.cpp:637-646`), which
    /// `request_error_sequence` set to five percent for a five percent session
    /// and to zero otherwise; then the state it returns to runs its own entry
    /// with `initialize_state` true, because the return assignment is a real
    /// state change, and that entry signals the level the state owes.
    ///
    /// So the answer is the returning state's, not the detour's. Only the two
    /// states `request_error_sequence` admits are reachable here under an
    /// ordinary return; anything else is the reducer having moved on a control
    /// pilot event while the detour ran, and X1 is the safe answer for all of
    /// them.
    fn returned_pilot_level(&self) -> IecCommand {
        match self.iec.state() {
            // `Charger.cpp:746-750`, the `PrepareCharging` entry's duty cycle
            // choice. Written as the nominal ampere because `shape` applies the
            // five percent replacement, and the conjunction it applies it on is
            // the one the C++ branches on in that very statement.
            AcState::PrepareCharging => {
                IecCommand::PwmOn(pwm_duty_for_current_a(self.iec.current_limit_a()))
            }
            // `Charger.cpp:302-316`: the `WaitingForAuthentication` entry offers
            // five percent and nothing else, so a session that is not on that
            // offer stays at the X1 the exit left it at (`:639-640`).
            _ if self.five_percent => IecCommand::PwmOn(PWM_5_PERCENT),
            _ => IecCommand::CpStateX1,
        }
    }

    /// The data link half a power path can act on: the permission withdrawal
    /// every request owes and whatever the pilot owes.
    ///
    /// The SLAC relay travels with it but is sent by the high level
    /// communication port, because the C++ sends it from the callback rather
    /// than as a consequence of what the charger decided.
    fn on_data_link(&mut self, _session: &Session, request: DataLinkRequest) -> Vec<Effect> {
        // The latch's only writer of the failure, as `Charger::dlink_error` is
        // the C++'s. It is read before the pilot decision below, which reads
        // the offer variant the error does not touch.
        if request == DataLinkRequest::Error {
            self.hlc_failed = true;
        }
        let pilot = dlink::pilot_for(request, self.pwm_offered, self.five_percent);
        let mut effects = self.allow_close_contactor(false);
        match pilot {
            DlinkPilot::None => {}
            DlinkPilot::X1 => {
                let commands = vec![IecCommand::CpStateX1];
                effects.extend(self.emit(commands));
            }
            DlinkPilot::RestartThroughX1ThenEf => {
                self.detour = Detour::BackToAuthentication;
                let commands = self.enter_pilot_step(PilotStep::X1BeforeStateF);
                effects.extend(self.emit(commands));
            }
        }
        effects
    }

    /// `[V2G3-M07-09]`: the session restarts from waiting for authentication.
    ///
    /// The C++ reaches it by returning from `T_step_EF` into
    /// `EvseState::WaitingForAuthentication` with a zero duty cycle
    /// (`Charger.cpp:2101-2107`), which runs that state's entry block and
    /// re-derives the offer. The reducer moves the state and the offer is
    /// re-derived here, from the same two facts the C++ entry reads.
    fn restart_session(&mut self, session: &Session) -> Vec<Effect> {
        let mut commands = self.iec.handle(IecInput::SessionRestart);
        self.derive_offer(session);
        commands.push(if self.five_percent {
            IecCommand::PwmOn(PWM_5_PERCENT)
        } else {
            IecCommand::PwmOff
        });
        let mut effects = self.emit(commands);
        effects.extend(self.run_authorization_loop());
        effects
    }

    /// Start the transaction and offer a duty cycle. The reducer computes the
    /// nominal duty from the current limit; `shape` replaces it while a five
    /// percent offer stands.
    fn proceed(&mut self) -> Vec<IecCommand> {
        let mut commands = self.cancel_fallback();
        commands.extend(self.iec.handle(IecInput::AuthorizationAccepted));
        commands.extend(self.iec.handle(IecInput::TransactionStarted));
        commands
    }

    fn cancel_fallback(&mut self) -> Vec<IecCommand> {
        if self.fallback != Fallback::Armed {
            return Vec::new();
        }
        self.fallback = Fallback::NotArmed;
        vec![IecCommand::CancelTimer(AcTimer::FivePercentFallback)]
    }

    /// Enter one step of the pilot detour: signal the pilot state and arm the
    /// deadline that ends the step. The steps share one timer identity, so a new
    /// step supersedes the previous one rather than racing it.
    fn enter_pilot_step(&mut self, step: PilotStep) -> Vec<IecCommand> {
        let mut commands = self.cancel_fallback();
        self.pilot_step = step;
        match step {
            PilotStep::X1BeforeStateF => {
                commands.push(IecCommand::CpStateX1);
                commands.push(IecCommand::ArmTimer(AcTimer::TStepX1));
            }
            PilotStep::StateF => {
                commands.push(IecCommand::CpStateF);
                commands.push(IecCommand::ArmTimer(AcTimer::TStepEf));
            }
            PilotStep::X1AfterStateF => {
                commands.push(IecCommand::CpStateX1);
                commands.push(IecCommand::ArmTimer(AcTimer::TStepEfX1Pause));
            }
            PilotStep::X1 => {
                commands.push(IecCommand::CpStateX1);
                commands.push(IecCommand::ArmTimer(AcTimer::TStepX1));
            }
            PilotStep::None => {}
        }
        commands
    }

    /// A five percent session offers high level communication rather than a
    /// current, so the duty cycle the reducer derived from the current limit is
    /// replaced for as long as the offer stands.
    fn shape(&self, commands: Vec<IecCommand>) -> Vec<IecCommand> {
        // The second of the latch's two read sites, and the `or hlc_failed`
        // half of the `PrepareCharging` duty cycle choice: `if (charge_mode ==
        // ChargeMode::AC and (not hlc_use_5percent_current_session or
        // hlc_failed))` takes the nominal ampere. It carries the fallback on
        // the one route that re-derives nothing, a data link error arriving
        // with the offer already off the pilot, where `Charger::dlink_error`
        // finds `pwm_running` false and runs no detour back into
        // `WaitingForAuthentication`.
        if !self.five_percent || self.hlc_failed {
            return commands;
        }
        commands
            .into_iter()
            .map(|command| match command {
                IecCommand::PwmOn(_) => IecCommand::PwmOn(PWM_5_PERCENT),
                other => other,
            })
            .collect()
    }

    /// Every field whose lifetime is one session, cleared in one place.
    ///
    /// The C++ clears the same set at one point too, the `Idle` entry at
    /// `Charger.cpp:214-235`, which clears `hlc_charging_active`,
    /// `hlc_allow_close_contactor` and the authorization together rather than
    /// leaving each route to remember its own. Both routes into it, the unplug
    /// and the port leaving service, come through here: a permission,
    /// a held power request or a pilot detour that survived would be acted on
    /// against the next vehicle, and the detour's next step signals X1, which
    /// advertises the availability a disabled port must not offer.
    ///
    /// `pnc` is absent on purpose. It is assigned at the entry of its only read
    /// path, so it has no window in which a stale value can be read.
    ///
    /// `hlc_failed` is present because the C++ clears it here and only here.
    /// This is the sole writer of that clear, and the field's own comment says
    /// why the unplug rather than the end of a session is the right edge for
    /// it.
    fn end_session(&mut self) -> Vec<IecCommand> {
        self.authorized = false;
        self.matching_started = false;
        self.hlc_charging_active = false;
        self.hlc_allow_close_contactor = false;
        self.hlc_failed = false;
        self.withheld_power_request = None;
        self.energy_wait = EnergyWait::default();
        let mut commands = self.cancel_fallback();
        self.detour = Detour::default();
        if self.pilot_step != PilotStep::None {
            self.pilot_step = PilotStep::None;
            // The four steps share one identity, so any of them cancels it.
            commands.push(IecCommand::CancelTimer(AcTimer::TStepEf));
        }
        commands
    }

    /// The port leaves or returns to service.
    ///
    /// The four inputs `iec_input` yields, and nothing else. A shutdown does
    /// not arrive here: it reaches the reducer through `to_safe_state`, and the
    /// error the vehicle is told about travels with it from `HlcPort`, which is
    /// where the C++ gate `r_hlc.empty()` lives rather than on a power path.
    fn on_reducer_input(&mut self, input: IecInput) -> Vec<Effect> {
        // Only the disable tears the session down. A pause and a resume both
        // leave the transaction open (`Charger.cpp:1331`, `:1339` act on a live
        // transaction and nothing else), so clearing the high level
        // communication state here would lose the matching a resume needs.
        if input != IecInput::Disable {
            let commands = self.iec.handle(input);
            return self.emit(commands);
        }
        let mut commands = self.end_session();
        commands.extend(self.iec.handle(input));
        self.emit(commands)
    }

    /// Both permissions gate the contactor while a five percent offer carries
    /// the session. A nominal offer needs only the IEC half, because a vehicle
    /// on nominal signalling can fall back to basic charging at any time.
    /// `Charger.cpp:722-727`.
    fn contactor_gate_shut(&self) -> bool {
        self.five_percent && !self.hlc_allow_close_contactor
    }
}

impl PowerPath for AcHlc {
    fn take_entered_states(&mut self) -> Vec<AcState> {
        self.iec.take_entered()
    }

    fn state(&self) -> AcState {
        self.iec.state()
    }

    fn name(&self) -> &'static str {
        "AcHlc"
    }

    /// Written from `PathEvent::SetupFinished` and cleared on the idle entry,
    /// the two writers `Charger::set_hlc_charging_active` (`Charger.cpp:2118`)
    /// and `Charger.cpp:214-235` have.
    fn hlc_charging_active(&self) -> bool {
        self.hlc_charging_active
    }

    /// The same AC branch `AcBasic` answers, through the same reducer. The
    /// high level session changes nothing about it: `Charger::power_available`
    /// branches on the charge mode and not on whether a stack is talking.
    fn power_available(&self) -> bool {
        self.iec.power_available()
    }

    /// Both branches of `Charger.cpp:1928-1931`.
    ///
    /// Once the vehicle has taken over the session the negotiated limit is what
    /// it was told, whatever duty the pilot happens to carry. Getting that
    /// branch wrong the other way would measure a vehicle charging at thirty
    /// two amperes over ISO 15118 against the noise floor and stop every such
    /// session.
    ///
    /// Before that, the offer is the pilot's, and a five percent offer signals
    /// no current: the reducer never issues one, so it is not offering. See
    /// `Iec::signalled_current_a` for why that is one derivation rather than a
    /// second test of `five_percent` here.
    fn signalled_current_a(&self) -> f64 {
        if self.hlc_charging_active {
            // The limit itself, not the pilot's offer: [V2G3-M07-20] takes the
            // duty cycle off a paused ISO 15118 session.
            return self.iec.current_limit_a();
        }
        self.iec.signalled_current_a()
    }

    /// No DC supply on this path either; see `AcBasic::target_voltage_v`.
    fn target_voltage_v(&self) -> f64 {
        0.0
    }

    /// The state of charge flip is `AcWithSoc`'s, and `config.ac_with_soc`
    /// selects that path rather than this one.
    fn presents_fake_dc(&self) -> bool {
        false
    }

    fn on_startup(&mut self) -> Vec<Effect> {
        let commands = self.iec.handle(IecInput::StartupComplete);
        self.emit(commands)
    }

    fn on_session_start(&mut self, session: &Session, _now: Instant) -> Vec<Effect> {
        // The offer variant is per session, `hlc_use_5percent_current_session`
        // in the C++, and the authorization loop withdraws it as the session
        // runs. It is restored from the profile the session carries rather than
        // from a copy here, so the two cannot disagree.
        self.derive_offer(session);
        let mut commands = self.iec.handle(IecInput::CarPluggedIn);
        if self.five_percent {
            // The offer that invites high level communication. The deadline that
            // withdraws it is armed by the authorization loop, which is where
            // the C++ arms it too, so it measures the wait for a charge loop
            // rather than the wait for a driver.
            commands.push(IecCommand::PwmOn(PWM_5_PERCENT));
        }
        let mut effects = self.emit(commands);
        // The other half of the pair, for the reason `AcBasic` gives at its own
        // session start: an authorization held since before the vehicle arrived
        // is acted on by whichever fact lands last, and that is this one.
        effects.extend(self.run_authorization_loop());
        effects
    }

    fn on_authorized(&mut self, session: &Session, _now: Instant) -> Vec<Effect> {
        self.authorized = true;
        self.pnc = session.authorized_plug_and_charge;
        self.run_authorization_loop()
    }

    fn on_bsp(
        &mut self,
        _session: &Session,
        event: &BspEvent,
        edges: CpEdges,
        _now: Instant,
    ) -> Vec<Effect> {
        let BspEvent::Cp(cp) = event else {
            // The cable's own rating, which caps what a socket may offer.
            if let BspEvent::PpAmpacity(ampacity_a) = event {
                let commands = self.iec.set_cable_rating_a(*ampacity_a);
                return self.emit(commands);
            }
            return Vec::new();
        };
        let input = cp_to_input(*cp, edges);

        if matches!(
            input,
            IecInput::CarRequestedPower | IecInput::CarRequestedVentilatedPower
        ) && self.contactor_gate_shut()
        {
            // Only the IEC half of the permission is present. Hold the request
            // rather than letting the reducer report charging on half a
            // permission, and answer the vehicle the same way a refusal does.
            self.withheld_power_request = Some(input);
            return vec![Effect::LockConnector, Effect::AllowPowerOn(false)];
        }

        if input != IecInput::CarUnplugged {
            self.withheld_power_request = None;
            let commands = self.iec.handle(input);
            return self.emit(commands);
        }

        // The reducer runs **before** the session is torn down, which is the
        // C++ order and not a detail. `process_cp_events_independent`
        // (`Charger.cpp:1202-1203`) clears `flag_ev_plugged_in` and nothing
        // else, so the `StoppingCharging` the state machine then reaches still
        // sees `hlc_charging_active` and owes the vehicle a stop request
        // (`:1014`). The facts this path holds are cleared at the `Idle` entry
        // (`:212-232`), which is downstream of that.
        let commands = self.iec.handle(input);
        let mut effects = self.emit(commands);

        // `Charger.cpp:212-232`. The reducer clears its own authorization and
        // transaction; this clears everything the session negotiated above it.
        // Errors are cleared on an unplug too, in `faults`, which owns them.
        let commands = self.end_session();
        effects.extend(self.emit(commands));
        effects
    }

    fn on_limits_changed(&mut self, session: &Session, _now: Instant) -> Vec<Effect> {
        let mut commands = self.iec.set_current_limit_a(session.limits.max_current_a);
        commands.extend(self.reassess_energy());
        let mut effects = self.emit(commands);
        effects.extend(self.run_authorization_loop());
        effects
    }

    fn on_stop(&mut self, _session: &Session, _reason: StopReason, _now: Instant) -> Vec<Effect> {
        // The reason itself is not read here. `Charger::cancel_transaction`
        // (`Charger.cpp:1355-1363`) names it to the vehicle from outside the
        // state machine and under gates no power path owns, so the core sends
        // it; what reaches the reducer is the stop alone.
        let commands = self.iec.handle(IecInput::StopRequested);
        self.emit(commands)
    }

    fn on_timer(&mut self, session: &Session, id: TimerId, _now: Instant) -> Vec<Effect> {
        if id == TIMER_T_STEP {
            return match self.pilot_step {
                PilotStep::X1BeforeStateF => {
                    let commands = self.enter_pilot_step(PilotStep::StateF);
                    self.emit(commands)
                }
                PilotStep::StateF => {
                    let commands = self.enter_pilot_step(PilotStep::X1AfterStateF);
                    self.emit(commands)
                }
                PilotStep::X1AfterStateF | PilotStep::X1 => {
                    self.pilot_step = PilotStep::None;
                    match self.detour {
                        Detour::IntoCharging => {
                            let commands = self.proceed();
                            self.emit(commands)
                        }
                        Detour::BackToAuthentication => {
                            self.detour = Detour::IntoCharging;
                            self.restart_session(session)
                        }
                        // `Charger.cpp:637-646`: the exit restores the offer it
                        // captured on the way in, which is five percent for a
                        // five percent session and nothing at all otherwise.
                        //
                        // The authorization loop runs behind it for the reason
                        // the reinitialization gives below: the C++ returns to
                        // the state it left with `initialize_state` true, so
                        // that state's entry and checks run again on the next
                        // pass of a 100 ms loop, and here nothing else would
                        // re-enter them. It is what re-arms the nominal
                        // fallback the detour cancelled on the way in
                        // (`Charger.cpp:272`), and what acts on an
                        // authorization that arrived while the pilot was down.
                        Detour::BackToTheStateItLeft => {
                            self.detour = Detour::IntoCharging;
                            let commands = vec![self.returned_pilot_level()];
                            let mut effects = self.emit(commands);
                            effects.extend(self.run_authorization_loop());
                            effects
                        }
                    }
                }
                PilotStep::None => Vec::new(),
            };
        }

        if id == TIMER_REINIT {
            // The reinitialization held its pilot level long enough. The
            // reducer restarts the session from `WaitingForAuthentication`, and
            // the authorization loop has to run behind it: the C++ reaches that
            // state's checks on the next pass of a loop that polls every 100 ms,
            // while here nothing else would re-enter it. This is the same pair
            // `restart_session` makes for the data link error route, and for
            // the same reason.
            let commands = self.iec.handle(IecInput::ReinitFinished);
            let mut effects = self.emit(commands);
            effects.extend(self.run_authorization_loop());
            return effects;
        }

        if id == TIMER_FIVE_PERCENT_FALLBACK {
            // Only an armed window can elapse. Gating on the enforced flag
            // instead would swallow the expiry on the Plug and Charge path,
            // which does arm this window on an enforcing port, leaving the
            // offer up forever.
            if self.fallback != Fallback::Armed {
                return Vec::new();
            }
            self.fallback = Fallback::Elapsed;
            return self.run_authorization_loop();
        }

        if id == TIMER_HLC_NO_ENERGY {
            // `Charger.cpp:855-860`. The high level session was given its
            // configured run without a budget and it is over, so the charge
            // stops. The reducer re-reads the budget it holds, so a deadline
            // that fires in the same pass as a budget arriving decides on the
            // budget rather than on the deadline.
            self.no_energy_deadline = false;
            let commands = self.iec.handle(IecInput::EnergyWithdrawn);
            return self.emit(commands);
        }

        if id == TIMER_WAIT_FOR_ENERGY {
            // The wait is given up on and the loop proceeds. The vehicle is
            // told nothing: `signal_hlc_no_energy_available` sits inside
            // `if (config_context.charge_mode == ChargeMode::DC)` at both of
            // its call sites (`Charger.cpp:346-362` and `:679-685`), so an AC
            // port never sends it. See `docs/architecture.md` for why the DC
            // producer is not ported either.
            self.energy_wait.gave_up();
            return self.run_authorization_loop();
        }

        if let Some(input) = reducer_timer_input(id) {
            let commands = self.iec.handle(input);
            return self.emit(commands);
        }

        Vec::new()
    }

    fn on_effect_done(
        &mut self,
        _session: &Session,
        _id: Option<EffectId>,
        _outcome: &EffectOutcome,
        _now: Instant,
    ) -> Vec<Effect> {
        Vec::new()
    }

    fn on_path_event(&mut self, session: &Session, event: PathEvent, _now: Instant) -> Vec<Effect> {
        if let Some(input) = iec_input(event) {
            return self.on_reducer_input(input);
        }
        match event {
            // `EvseManager.cpp:1215-1225`, which is where the C++ derives it
            // too, from the SLAC state and nothing else. Every fact the
            // authorization decision reads reruns it, because the C++ reaches
            // the same answer by polling all of them every 100 ms.
            PathEvent::MatchingStarted(started) => {
                self.matching_started = started;
                self.run_authorization_loop()
            }

            // `EvseManager.cpp:394` into `Charger::set_hlc_charging_active`
            // (`Charger.cpp:2118-2121`), the only producer of that fact. On AC
            // it means the vehicle asked for power over ISO 15118, and it is
            // what keeps the five percent offer up rather than dropping to X1
            // and ending a session the vehicle would need a replug to recover
            // from.
            PathEvent::SetupFinished => {
                self.hlc_charging_active = true;
                self.run_authorization_loop()
            }

            PathEvent::AllowCloseContactor(allow) => self.allow_close_contactor(allow),

            PathEvent::DataLink(request) => self.on_data_link(session, request),

            // The matched half of the SLAC state and the vehicle's state of
            // charge. Both have exactly one reader, `path::ac_with_soc`, which
            // holds them itself rather than through this path: the first gates
            // the reinitialization and the second triggers the flip, and both
            // decisions are the mode's, not the pilot's. Matched explicitly so
            // the absence is a stated fact rather than a fall-through.
            PathEvent::SlacMatched(_) | PathEvent::StateOfCharge { .. } => Vec::new(),

            PathEvent::SlacErrorRoutine => self.request_error_sequence(),

            // Deliberately inert. A session setup is evidence that matching
            // completed, but late and one directional: it cannot precede a
            // match, and it never reports the return to unmatched. SLAC's own
            // state is the producer, above.
            PathEvent::HlcSessionSetup => Vec::new(),

            // Deliberately inert on AC. `EvseManager.cpp:573-586` subscribes
            // `current_demand_started` inside the `charge_mode == "DC"` branch,
            // so an AC port never receives it, and
            // `Charger::notify_currentdemand_started` (`Charger.cpp:2024-2030`)
            // moves the state without touching `hlc_charging_active`, so it is
            // not the AC power delivery fact either.
            PathEvent::CurrentDemandStarted | PathEvent::CurrentDemandFinished => Vec::new(),

            // `Charger.cpp:1064` and the whole `Finished` route end
            // the session when the vehicle terminates high level
            // communication. This clears one flag. Ceiling: a vehicle ending
            // the ISO session leaves the port charging on nominal signalling
            // rather than stopping. Upgrade path: the session lifecycle owner
            // routes it through a stop. Owner: RsEvseManager.
            PathEvent::StopFromEv => {
                self.hlc_charging_active = false;
                Vec::new()
            }

            // `EvseManager.cpp:827-832` subscribes `dc_open_contactor` inside
            // the `charge_mode == "DC"` branch, so an AC port never receives
            // it, and it drives no DC supply to remove.
            PathEvent::OpenContactorDc => Vec::new(),

            // AC never runs a cable check and never precharges: the C++
            // subscribes `start_cable_check` and `start_pre_charge` only inside
            // its `charge_mode == "DC"` branch (`EvseManager.cpp:528` onward),
            // so on an AC port neither is ever published. Matched explicitly so
            // the absence is a stated fact rather than a fall-through.
            PathEvent::CableCheckRequired | PathEvent::PreChargeStarted => Vec::new(),

            // No DC supply and no isolation monitor is wired to an AC port, so
            // neither reading has a producer here.
            PathEvent::SupplyVoltage { .. }
            | PathEvent::Isolation(_)
            | PathEvent::IsolationSelfTest(_)
            | PathEvent::OverVoltageMeasurement { .. }
            | PathEvent::MeterVoltage { .. } => Vec::new(),

            // The bidirectional capability that can be withdrawn is the DC
            // power supply's, and `EvseManager.cpp:214-216` subscribes that
            // report inside the `charge_mode == "DC"` branch, so an AC port
            // never hears one. An AC port has no supply direction to turn
            // round in any case.
            PathEvent::BidirectionalWithdrawn => Vec::new(),

            // The DC target, the DC limit sets and the ISO 15118-20 dynamic
            // control mode. Every one of the C++ subscriptions behind these
            // sits inside the `charge_mode == "DC"` branch
            // (`EvseManager.cpp:528` onward), and an AC port has no DC supply
            // to program in any case. Matched explicitly so the absence is a
            // stated fact rather than a fall-through.
            //
            // The ISO 15118-20 **AC** dynamic control mode is a different
            // variable (`ac_ev_dynamic_control_mode`) and is still dropped at
            // the boundary; nothing here stands in for it.
            PathEvent::DcEvTarget { .. }
            | PathEvent::DcDynamicChargeMode(_)
            | PathEvent::DcEvMaximumLimits(_)
            | PathEvent::DcEnforcedLimits { .. }
            | PathEvent::DcExportVoltageRange { .. } => Vec::new(),

            // Handled above by `iec_input`, which is the only route into the
            // reducer.
            PathEvent::Enable
            | PathEvent::Disable
            | PathEvent::PauseRequested
            | PathEvent::ResumeRequested
            | PathEvent::SwitchPhases { .. } => Vec::new(),
        }
    }

    fn take_session_duties(&mut self) -> Vec<SessionDuty> {
        self.iec.take_duties()
    }

    fn to_safe_state(&mut self) -> Vec<Effect> {
        let commands = self.iec.handle(IecInput::ErrorShutdown);
        self.emit(commands)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::core::session::SessionEvent;

    #[test]
    fn every_control_pilot_reading_the_core_calls_an_unplug_reaches_the_reducer_as_one() {
        // The core decides where to clear this module's errors from
        // `CpEvent::is_unplug`, and this file decides which readings end the
        // session in the reducer. Two lists, held in step here rather than by
        // whoever edits one of them next.
        for cp in [
            CpEvent::A,
            CpEvent::B,
            CpEvent::C,
            CpEvent::D,
            CpEvent::E,
            CpEvent::F,
            CpEvent::PowerOn,
            CpEvent::PowerOff,
            CpEvent::Disconnected,
        ] {
            // Both edge answers, because the edge decides what state B means
            // and must not be able to turn any reading into a departure.
            for requested_stop_power in [false, true] {
                let edges = CpEdges {
                    requested_stop_power,
                    ..CpEdges::default()
                };
                assert_eq!(
                    cp_to_input(cp, edges) == IecInput::CarUnplugged,
                    cp.is_unplug(),
                    "{cp:?} is an unplug on one side only"
                );
            }
        }
    }
    use crate::core::config::SwitchCpState;
    use crate::core::effect::{CpState, HlcUpdate};
    use crate::core::path::iec::pwm_duty_for_current_a;
    use crate::core::session::Limits;

    /// The transition memory's answer for a state B out of C or D: the
    /// vehicle opened S2. Every other reading these tests drive makes no edge
    /// a path reads, which is why `CpEdges::default()` is right everywhere
    /// else and wrong here.
    fn s2_opened() -> CpEdges {
        CpEdges {
            requested_stop_power: true,
            ..CpEdges::default()
        }
    }

    fn config() -> IecConfig {
        IecConfig {
            initial_current_limit_a: 16.0,
            has_ventilation: true,
            lock_connector_in_state_b: true,
            switch_phases_cp_state: SwitchCpState::X1,
            switch_phases_delay: Duration::from_secs(10),
            reinit_method: crate::core::config::ReinitMethod::CpStateF,
            reinit_duration: Duration::from_millis(3000),
            hlc_no_energy_timeout: Duration::from_secs(5),
            type2_socket: false,
        }
    }

    fn session() -> Session {
        session_for(PwmStart::Nominal)
    }

    /// The session a port configured for `pwm_start` is handed. Production
    /// derives both from one setting, so a test must not let them disagree.
    fn session_for(pwm_start: PwmStart) -> Session {
        Session::new(
            pwm_start,
            Limits {
                max_current_a: 16.0,
                nr_of_phases_available: 3,
            },
        )
    }

    fn now() -> Instant {
        Instant::now()
    }

    fn session_with_current(max_current_a: f64) -> Session {
        Session::new(
            PwmStart::Nominal,
            Limits {
                max_current_a,
                nr_of_phases_available: 3,
            },
        )
    }

    /// Both paths start in `Startup` and only leave it on the startup fact, so
    /// every test that drives a session past a plug in needs this first.
    fn hlc(pwm_start: PwmStart) -> AcHlc {
        let mut path = AcHlc::new(config(), pwm_start);
        path.iec.handle(IecInput::StartupComplete);
        path
    }

    /// A session the energy manager has given nothing, which is what makes the
    /// authorization loop wait at all: `power_available` needs more than 5.9 A.
    fn session_without_energy() -> Session {
        session_with_current(0.0)
    }

    /// A charge the vehicle is talking to over ISO 15118, which is the branch
    /// the C++ grants the no budget deadline to.
    fn charging_hlc_session() -> AcHlc {
        charging_hlc_session_with(Duration::from_secs(5))
    }

    fn charging_hlc_session_with(timeout: Duration) -> AcHlc {
        let mut path = AcHlc::new(
            IecConfig {
                hlc_no_energy_timeout: timeout,
                type2_socket: false,
                ..config()
            },
            PwmStart::Nominal,
        );
        path.iec.handle(IecInput::StartupComplete);
        let s = session();
        path.on_session_start(&s, now());
        path.on_authorized(&s, now());
        path.on_bsp(&s, &BspEvent::Cp(CpEvent::C), CpEdges::default(), now());
        relays_confirmed_closed(&mut path, &s);
        assert_eq!(path.state(), AcState::Charging, "the fixture charges");
        path.hlc_charging_active = true;
        path
    }

    fn basic() -> AcBasic {
        let mut path = AcBasic::new(config());
        path.iec.handle(IecInput::StartupComplete);
        path
    }

    /// SLAC reports matching started, which is the producer of that fact
    /// (`EvseManager.cpp:1215-1225`).
    fn matched() -> PathEvent {
        PathEvent::MatchingStarted(true)
    }

    fn pause() -> PathEvent {
        PathEvent::PauseRequested
    }

    fn resume() -> PathEvent {
        PathEvent::ResumeRequested
    }

    /// A path learns its budget the way production hands it one: through the
    /// enforced limits, which is also what writes the reducer's copy of it.
    ///
    /// A session constructed with a budget the path was never told about leaves
    /// the two copies disagreeing, and availability is the reducer's answer,
    /// as the C++ asks `power_available()` of its one stored maximum. So a test
    /// that means to drive an empty budget reports it.
    fn budget_reported(path: &mut impl PowerPath, session: &Session) {
        path.on_limits_changed(session, now());
    }

    /// The relay confirmation a board sends once the contactor closes. A
    /// charge with the relays still reported open settles a stop in the pass
    /// that begins it, which is not the shape a real session has.
    fn relays_confirmed_closed(path: &mut impl PowerPath, session: &Session) {
        path.on_bsp(
            session,
            &BspEvent::Cp(CpEvent::PowerOn),
            CpEdges::default(),
            now(),
        );
    }

    fn arms(effects: &[Effect], timer: TimerId) -> bool {
        effects
            .iter()
            .any(|e| matches!(e, Effect::StartTimer { id, .. } if *id == timer))
    }

    #[test]
    fn a_nominal_start_reaches_charging_without_a_pilot_detour() {
        let mut path = hlc(PwmStart::Nominal);
        let s = session();
        path.on_session_start(&s, now());

        let decided = path.on_authorized(&s, now());

        assert!(decided.contains(&Effect::PwmOn(pwm_duty_for_current_a(16.0))));
        assert!(!decided.iter().any(|e| matches!(e, Effect::SetCpState(_))));
        assert_eq!(path.state(), AcState::PrepareCharging);

        let charging = path.on_bsp(&s, &BspEvent::Cp(CpEvent::C), CpEdges::default(), now());

        assert!(charging.contains(&Effect::AllowPowerOn(true)));
        assert_eq!(path.state(), AcState::Charging);
    }

    /// The cable's rating arrives as a board reading, and both AC paths have to
    /// hand it to the reducer for the cap to exist on a real port at all.
    ///
    /// This test exists because a mutation found the hole: deleting the arm
    /// that forwards the reading reddened nothing, since every other test set
    /// the rating on the reducer directly. The cap was pinned and its one
    /// producer was not.
    #[test]
    fn the_proximity_pilot_reading_reaches_the_reducer_on_both_paths() {
        let socket = || IecConfig {
            type2_socket: true,
            ..config()
        };
        let s = session_with_current(32.0);

        let mut basic_path = AcBasic::new(socket());
        basic_path.iec.handle(IecInput::StartupComplete);
        basic_path.on_session_start(&s, now());

        let narrowed = basic_path.on_bsp(
            &s,
            &BspEvent::PpAmpacity(13.0),
            CpEdges::default(),
            now(),
        );
        assert!(
            narrowed.contains(&Effect::SetOvercurrentLimit(13.0)),
            "the basic path: {narrowed:?}"
        );

        let mut hlc_path = AcHlc::new(socket(), PwmStart::Nominal);
        hlc_path.iec.handle(IecInput::StartupComplete);
        hlc_path.on_session_start(&s, now());

        let narrowed = hlc_path.on_bsp(
            &s,
            &BspEvent::PpAmpacity(13.0),
            CpEdges::default(),
            now(),
        );
        assert!(
            narrowed.contains(&Effect::SetOvercurrentLimit(13.0)),
            "the high level path: {narrowed:?}"
        );
    }

    /// The interaction between the discharge magnitude and the no budget test:
    /// an AC_BPT allowance of -20 A is twenty amperes of power available, not
    /// a budget that has gone. Availability asks the reducer, which stores the
    /// magnitude, so the charge runs on and the pilot carries the duty cycle
    /// for 20 A.
    ///
    /// Read from the session instead and this session would be stopped for
    /// want of energy while the vehicle was discharging into the grid.
    #[test]
    fn a_discharge_allowance_is_not_a_budget_that_has_gone() {
        let mut path = charging_hlc_session();

        let discharging = path.on_limits_changed(&session_with_current(-20.0), now());

        assert_eq!(path.state(), AcState::Charging, "{discharging:?}");
        assert!(
            discharging.contains(&Effect::PwmOn(pwm_duty_for_current_a(20.0))),
            "the pilot offers the magnitude: {discharging:?}"
        );
        assert!(
            !arms(&discharging, TIMER_HLC_NO_ENERGY),
            "and no deadline is started: {discharging:?}"
        );
    }

    /// A charge running on the basic path when its budget goes.
    ///
    /// `Charger.cpp:867-871`: "Stop immediately in basic AC mode". No high
    /// level session can be running here, so the deadline the other branch
    /// grants never applies and the charge stops on the spot.
    #[test]
    fn a_basic_charge_stops_at_once_when_its_budget_goes() {
        let mut path = basic();
        let s = session();
        path.on_session_start(&s, now());
        path.on_authorized(&s, now());
        path.on_bsp(&s, &BspEvent::Cp(CpEvent::C), CpEdges::default(), now());
        relays_confirmed_closed(&mut path, &s);
        assert_eq!(path.state(), AcState::Charging, "the control");

        let short = session_without_energy();
        let stopping = path.on_limits_changed(&short, now());

        assert_eq!(path.state(), AcState::StoppingCharging);
        assert!(
            !arms(&stopping, TIMER_HLC_NO_ENERGY),
            "no deadline is granted to a basic session: {stopping:?}"
        );
        assert!(
            !stopping
                .iter()
                .any(|effect| matches!(effect, Effect::PwmOn(_))),
            "and nothing is offered on the pilot: {stopping:?}"
        );
    }

    /// The same budget on a high level session, which is given
    /// `hlc_charge_loop_without_energy_timeout_s` first (`:837-861`). The
    /// charge continues until the deadline expires.
    #[test]
    fn a_high_level_charge_is_given_its_configured_run_before_it_stops() {
        let mut path = charging_hlc_session();
        let short = session_without_energy();

        let waiting = path.on_limits_changed(&short, now());

        assert_eq!(
            path.state(),
            AcState::Charging,
            "the session charges on while the deadline runs"
        );
        assert!(arms(&waiting, TIMER_HLC_NO_ENERGY), "{waiting:?}");

        let again = path.on_limits_changed(&short, now());
        assert!(
            !arms(&again, TIMER_HLC_NO_ENERGY),
            "a second short budget does not restart the deadline: {again:?}"
        );

        let expired = path.on_timer(&short, TIMER_HLC_NO_ENERGY, now());

        assert_eq!(path.state(), AcState::StoppingCharging);
        assert!(
            expired.contains(&Effect::AllowPowerOn(false)),
            "power is withdrawn: {expired:?}"
        );
        assert!(
            !expired
                .iter()
                .any(|effect| matches!(effect, Effect::PwmOff | Effect::SetCpState(_))),
            "and the offer deliberately is not, because the ISO session has to \
             outlive the stop request the entry sends: {expired:?}"
        );
    }

    /// The deadline is cancelled by a budget arriving, which is the C++ `else`
    /// clearing its running flag at `:874`. Without that a budget that came
    /// and went would stop a charge the second time on the first deadline.
    #[test]
    fn a_budget_arriving_cancels_the_deadline_it_started() {
        let mut path = charging_hlc_session();
        let short = session_without_energy();
        path.on_limits_changed(&short, now());

        let ample = session();
        let restored = path.on_limits_changed(&ample, now());

        assert!(
            restored
                .iter()
                .any(|effect| matches!(effect, Effect::CancelTimer { id } if *id == TIMER_HLC_NO_ENERGY)),
            "{restored:?}"
        );
        assert_eq!(path.state(), AcState::Charging);

        let expired = path.on_timer(&ample, TIMER_HLC_NO_ENERGY, now());
        assert!(
            expired.is_empty(),
            "a deadline that outlived its cause decides nothing: {expired:?}"
        );
        assert_eq!(path.state(), AcState::Charging);
    }

    /// A configured zero is the C++ `> 0` guard failing (`:861-866`): "no
    /// timeout configured, stopping charging immediately".
    #[test]
    fn a_zero_deadline_stops_a_high_level_charge_at_once() {
        let mut path = charging_hlc_session_with(Duration::ZERO);
        let short = session_without_energy();

        let stopping = path.on_limits_changed(&short, now());

        assert_eq!(path.state(), AcState::StoppingCharging);
        assert!(!arms(&stopping, TIMER_HLC_NO_ENERGY), "{stopping:?}");
    }

    /// And a session on the high level path that is *not* talking to the
    /// vehicle takes the basic branch, because the C++ reads the two session
    /// flags rather than the charge mode (`:837`).
    #[test]
    fn a_high_level_path_running_a_basic_session_stops_at_once() {
        let mut path = hlc(PwmStart::Nominal);
        let s = session();
        path.on_session_start(&s, now());
        path.on_authorized(&s, now());
        path.on_bsp(&s, &BspEvent::Cp(CpEvent::C), CpEdges::default(), now());
        relays_confirmed_closed(&mut path, &s);
        assert_eq!(path.state(), AcState::Charging, "the control");
        assert!(
            !path.hlc_charging_active && !path.five_percent,
            "neither flag the C++ reads is set"
        );

        let stopping = path.on_limits_changed(&session_without_energy(), now());

        assert_eq!(path.state(), AcState::StoppingCharging);
        assert!(!arms(&stopping, TIMER_HLC_NO_ENERGY), "{stopping:?}");
    }

    #[test]
    fn a_five_percent_start_reaches_charging_through_the_state_f_sequence() {
        let mut path = hlc(PwmStart::FivePercent);
        let s = session_for(PwmStart::FivePercent);

        let start = path.on_session_start(&s, now());
        assert!(start.contains(&Effect::PwmOn(PWM_5_PERCENT)));

        // No matching started, so the offer must be withdrawn through state F.
        let decided = path.on_authorized(&s, now());
        assert_eq!(
            decided,
            vec![
                Effect::SetCpState(CpState::F),
                Effect::StartTimer {
                    id: TIMER_T_STEP,
                    after: T_STEP_EF
                },
            ]
        );

        let paused_in_x1 = path.on_timer(&s, TIMER_T_STEP, now());
        assert_eq!(
            paused_in_x1,
            vec![
                Effect::SetCpState(CpState::X1),
                Effect::StartTimer {
                    id: TIMER_T_STEP,
                    after: STAY_IN_X1_AFTER_T_STEP_EF
                },
            ]
        );

        let resumed = path.on_timer(&s, TIMER_T_STEP, now());
        assert!(resumed.contains(&Effect::PwmOn(pwm_duty_for_current_a(16.0))));
        assert!(!path.five_percent_active());
        assert_eq!(path.state(), AcState::PrepareCharging);

        let charging = path.on_bsp(&s, &BspEvent::Cp(CpEvent::C), CpEdges::default(), now());
        assert!(charging.contains(&Effect::AllowPowerOn(true)));
        assert_eq!(path.state(), AcState::Charging);
    }

    /// `Charger::request_error_sequence` (`Charger.cpp:2133-2147`) from the
    /// five percent offer: state F, a SLAC reset, and the offer back on the
    /// pilot at the end of it.
    ///
    /// The state is never left, so nothing is published. That is the whole
    /// difference from the data link error beside it, which restarts the
    /// session from `WaitingForAuthentication` and publishes for it.
    #[test]
    fn the_slac_error_routine_kicks_the_pilot_and_puts_the_offer_back() {
        let mut path = hlc(PwmStart::FivePercent);
        let s = session_for(PwmStart::FivePercent);
        path.on_session_start(&s, now());
        path.take_session_duties();
        assert_eq!(path.state(), AcState::WaitingForAuthentication, "the control");

        let kicked = path.on_path_event(&s, PathEvent::SlacErrorRoutine, now());

        assert_eq!(
            kicked,
            vec![
                Effect::SetCpState(CpState::F),
                Effect::StartTimer {
                    id: TIMER_T_STEP,
                    after: T_STEP_EF
                },
                Effect::SlacUpdate(SlacUpdate::Reset),
            ]
        );
        assert_eq!(path.state(), AcState::WaitingForAuthentication);

        let paused_in_x1 = path.on_timer(&s, TIMER_T_STEP, now());
        assert_eq!(
            paused_in_x1,
            vec![
                Effect::SetCpState(CpState::X1),
                Effect::StartTimer {
                    id: TIMER_T_STEP,
                    after: STAY_IN_X1_AFTER_T_STEP_EF
                },
            ]
        );

        let returned = path.on_timer(&s, TIMER_T_STEP, now());

        assert_eq!(returned, vec![Effect::PwmOn(PWM_5_PERCENT)]);
        assert!(path.five_percent_active(), "the offer is the one it left on");
        assert_eq!(path.state(), AcState::WaitingForAuthentication);
        assert!(
            path.take_session_duties().is_empty(),
            "the state was never left, so nothing is published"
        );
    }

    /// The other admitted state, and the return that is not the detour's own.
    ///
    /// The `T_step_EF` exit restores `t_step_EF_return_pwm`, which is zero for
    /// a session that is not on the five percent offer, so the pilot would be
    /// left at X1. What puts the nominal duty back is the `PrepareCharging`
    /// entry running again (`Charger.cpp:746-750`), because the return
    /// assignment is a real state change with `initialize_state` true. A port
    /// that only replayed the exit would leave a nominal high level session
    /// signalling nothing for the rest of its life.
    #[test]
    fn the_slac_error_routine_returns_prepare_charging_to_its_nominal_duty() {
        let mut path = hlc(PwmStart::Nominal);
        let s = session();
        path.on_session_start(&s, now());
        path.on_authorized(&s, now());
        assert_eq!(path.state(), AcState::PrepareCharging, "the control");
        path.take_session_duties();

        let kicked = path.on_path_event(&s, PathEvent::SlacErrorRoutine, now());

        assert_eq!(
            kicked,
            vec![
                Effect::SetCpState(CpState::F),
                Effect::StartTimer {
                    id: TIMER_T_STEP,
                    after: T_STEP_EF
                },
                Effect::SlacUpdate(SlacUpdate::Reset),
            ]
        );
        assert_eq!(path.state(), AcState::PrepareCharging);

        path.on_timer(&s, TIMER_T_STEP, now());
        let returned = path.on_timer(&s, TIMER_T_STEP, now());

        assert_eq!(returned, vec![Effect::PwmOn(pwm_duty_for_current_a(16.0))]);
        assert_eq!(path.state(), AcState::PrepareCharging);
        assert!(
            path.take_session_duties().is_empty(),
            "the state was never left, so nothing is published"
        );
    }

    /// The contactor permission the data link error withdraws is deliberately
    /// untouched here: `request_error_sequence` does not clear
    /// `hlc_allow_close_contactor`, which is what makes it a pilot kick rather
    /// than a session decision.
    #[test]
    fn the_slac_error_routine_leaves_the_contactor_permission_alone() {
        let mut path = hlc(PwmStart::FivePercentEnforced);
        let s = session_for(PwmStart::FivePercentEnforced);
        path.on_session_start(&s, now());
        path.on_authorized(&s, now());
        let granted = path.on_path_event(&s, PathEvent::AllowCloseContactor(true), now());
        assert_eq!(path.state(), AcState::PrepareCharging, "the control");
        assert!(
            !granted.contains(&Effect::AllowPowerOn(true)),
            "the vehicle has not asked for power yet: {granted:?}"
        );

        let kicked = path.on_path_event(&s, PathEvent::SlacErrorRoutine, now());

        assert!(
            !kicked
                .iter()
                .any(|effect| matches!(effect, Effect::AllowPowerOn(_))),
            "the permission travelled with the kick: {kicked:?}"
        );
        // The permission is still held, so the vehicle asking for power once
        // the offer is back closes the relays without a second grant.
        path.on_timer(&s, TIMER_T_STEP, now());
        path.on_timer(&s, TIMER_T_STEP, now());
        let requested = path.on_bsp(&s, &BspEvent::Cp(CpEvent::C), CpEdges::default(), now());
        assert!(requested.contains(&Effect::AllowPowerOn(true)), "{requested:?}");
    }

    /// The return is a real state change in the C++, so the state it returns to
    /// runs its entry and its checks again on the next pass of the 100 ms loop.
    /// Two consequences, and this pins both: the nominal fallback deadline the
    /// detour cancelled on the way in is re-armed (`Charger.cpp:272` clears the
    /// flag the state's own body then sets), and an authorization that arrived
    /// while the pilot was down is acted on rather than waiting for another
    /// input that may never come.
    #[test]
    fn the_slac_error_routine_leaves_the_state_it_returns_to_running_again() {
        let mut path = hlc(PwmStart::FivePercent);
        let s = session_for(PwmStart::FivePercent);
        path.on_session_start(&s, now());
        path.on_path_event(&s, matched(), now());
        let awaited = path.on_authorized(&s, now());
        assert!(
            arms(&awaited, TIMER_FIVE_PERCENT_FALLBACK),
            "the control: {awaited:?}"
        );

        let kicked = path.on_path_event(&s, PathEvent::SlacErrorRoutine, now());
        assert!(
            kicked.contains(&Effect::CancelTimer {
                id: TIMER_FIVE_PERCENT_FALLBACK
            }),
            "the detour left the deadline it interrupts armed: {kicked:?}"
        );

        path.on_timer(&s, TIMER_T_STEP, now());
        let returned = path.on_timer(&s, TIMER_T_STEP, now());

        assert!(
            arms(&returned, TIMER_FIVE_PERCENT_FALLBACK),
            "the returning state owes its deadline again: {returned:?}"
        );
    }

    /// A state and the drive that reaches it from a cold port.
    type StateDrive = (AcState, &'static dyn Fn(&mut AcHlc, &Session));

    /// Every state the reducer has, and the drive that reaches it. Each row is
    /// the same path type so the verdict is the state's and nothing else.
    fn every_state_and_its_drive() -> Vec<StateDrive> {
        fn charging(path: &mut AcHlc, s: &Session) {
            path.on_session_start(s, now());
            path.on_authorized(s, now());
            path.on_bsp(s, &BspEvent::Cp(CpEvent::C), CpEdges::default(), now());
        }
        fn stopping(path: &mut AcHlc, s: &Session) {
            charging(path, s);
            path.on_stop(s, StopReason::Local, now());
        }
        vec![
            // A cold port, before the board reported itself ready. `hlc` is
            // what leaves this state, so this row is the only one that skips
            // it.
            (AcState::Startup, &|_path, _s| {}),
            (AcState::Idle, &|path: &mut AcHlc, _s: &Session| {
                path.iec.handle(IecInput::StartupComplete);
            }),
            (
                AcState::WaitingForAuthentication,
                &|path: &mut AcHlc, s: &Session| {
                    path.iec.handle(IecInput::StartupComplete);
                    path.on_session_start(s, now());
                },
            ),
            (AcState::PrepareCharging, &|path: &mut AcHlc, s: &Session| {
                path.iec.handle(IecInput::StartupComplete);
                path.on_session_start(s, now());
                path.on_authorized(s, now());
            }),
            (AcState::Charging, &|path: &mut AcHlc, s: &Session| {
                path.iec.handle(IecInput::StartupComplete);
                charging(path, s);
            }),
            (AcState::ChargingPausedEv, &|path: &mut AcHlc, s: &Session| {
                path.iec.handle(IecInput::StartupComplete);
                charging(path, s);
                path.on_bsp(s, &BspEvent::Cp(CpEvent::B), s2_opened(), now());
            }),
            (
                AcState::ChargingPausedEvse,
                &|path: &mut AcHlc, s: &Session| {
                    path.iec.handle(IecInput::StartupComplete);
                    charging(path, s);
                    path.on_path_event(s, PathEvent::PauseRequested, now());
                },
            ),
            (AcState::SwitchPhases, &|path: &mut AcHlc, s: &Session| {
                path.iec.handle(IecInput::StartupComplete);
                charging(path, s);
                path.on_path_event(
                    s,
                    PathEvent::SwitchPhases {
                        three_phases: false,
                    },
                    now(),
                );
            }),
            (AcState::StoppingCharging, &|path: &mut AcHlc, s: &Session| {
                path.iec.handle(IecInput::StartupComplete);
                stopping(path, s);
            }),
            (AcState::Reinit, &|path: &mut AcHlc, s: &Session| {
                path.iec.handle(IecInput::StartupComplete);
                path.on_session_start(s, now());
                path.start_reinit();
            }),
            (AcState::Finished, &|path: &mut AcHlc, s: &Session| {
                path.iec.handle(IecInput::StartupComplete);
                stopping(path, s);
                path.on_bsp(s, &BspEvent::Cp(CpEvent::PowerOff), CpEdges::default(), now());
            }),
            (AcState::Disabled, &|path: &mut AcHlc, s: &Session| {
                path.iec.handle(IecInput::StartupComplete);
                charging(path, s);
                path.on_path_event(s, PathEvent::Disable, now());
            }),
        ]
    }

    /// The table above is the reducer's whole state space, counted off the enum
    /// rather than written down, so a new state cannot be added without
    /// deciding whether the error sequence is admitted from it.
    #[test]
    fn the_drive_table_reaches_every_state_the_reducer_has() {
        let body = include_str!("iec.rs")
            .split_once("pub enum AcState {")
            .expect("`AcState` moved")
            .1
            .split_once("\n}")
            .expect("unterminated `AcState`")
            .0;
        let variants = body
            .lines()
            .map(str::trim)
            .filter(|line| line.ends_with(',') && !line.starts_with("///"))
            .count();

        assert_eq!(
            every_state_and_its_drive().len(),
            variants,
            "an `AcState` variant has no drive"
        );
    }

    /// The refusal is as much of `request_error_sequence` as the kick is:
    /// `Charger.cpp:2135-2136` tests for the two states and returns having done
    /// nothing from anywhere else. Driven for every state rather than sampled.
    #[test]
    fn the_slac_error_routine_is_admitted_from_two_states_and_no_others() {
        for (state, reach) in every_state_and_its_drive() {
            let mut path = AcHlc::new(config(), PwmStart::Nominal);
            let s = session();
            reach(&mut path, &s);
            assert_eq!(path.state(), state, "the drive reaches {state:?}");
            path.take_session_duties();

            let effects = path.on_path_event(&s, PathEvent::SlacErrorRoutine, now());

            let admitted = matches!(
                state,
                AcState::WaitingForAuthentication | AcState::PrepareCharging
            );
            assert_eq!(
                !effects.is_empty(),
                admitted,
                "{state:?} produced {effects:?}"
            );
            if !admitted {
                assert_eq!(path.state(), state, "{state:?} was moved by a refusal");
            }
        }
    }

    #[test]
    fn an_enforced_five_percent_start_reaches_charging_on_both_permissions() {
        let mut path = hlc(PwmStart::FivePercentEnforced);
        let s = session_for(PwmStart::FivePercentEnforced);
        path.on_session_start(&s, now());

        let decided = path.on_authorized(&s, now());
        assert!(decided.contains(&Effect::PwmOn(PWM_5_PERCENT)));
        assert!(path.five_percent_active());
        assert!(!decided.iter().any(|e| matches!(e, Effect::SetCpState(_))));

        let requested = path.on_bsp(&s, &BspEvent::Cp(CpEvent::C), CpEdges::default(), now());
        assert!(
            !requested.contains(&Effect::AllowPowerOn(true)),
            "the high level communication permission is still missing"
        );

        let granted = path.on_path_event(&s, PathEvent::AllowCloseContactor(true), now());
        assert!(granted.contains(&Effect::AllowPowerOn(true)));
        assert_eq!(path.state(), AcState::Charging);
    }

    #[test]
    fn the_contactor_gate_needs_the_iec_permission_as_well() {
        let mut path = hlc(PwmStart::FivePercentEnforced);
        let s = session_for(PwmStart::FivePercentEnforced);
        path.on_session_start(&s, now());
        path.on_authorized(&s, now());

        let granted = path.on_path_event(&s, PathEvent::AllowCloseContactor(true), now());
        assert!(
            !granted.contains(&Effect::AllowPowerOn(true)),
            "the vehicle has not asked for power yet"
        );

        let requested = path.on_bsp(&s, &BspEvent::Cp(CpEvent::C), CpEdges::default(), now());
        assert!(requested.contains(&Effect::AllowPowerOn(true)));
        assert_eq!(path.state(), AcState::Charging);
    }

    #[test]
    fn a_nominal_offer_needs_only_the_iec_permission() {
        let mut path = hlc(PwmStart::Nominal);
        let s = session();
        path.on_session_start(&s, now());
        path.on_authorized(&s, now());

        let requested = path.on_bsp(&s, &BspEvent::Cp(CpEvent::C), CpEdges::default(), now());

        assert!(requested.contains(&Effect::AllowPowerOn(true)));
    }

    #[test]
    fn a_five_percent_start_with_matching_already_started_falls_back_through_x1() {
        let mut path = hlc(PwmStart::FivePercent);
        let s = session_for(PwmStart::FivePercent);
        path.on_session_start(&s, now());
        path.on_path_event(&s, matched(), now());

        let awaited = path.on_authorized(&s, now());
        assert_eq!(
            awaited,
            vec![Effect::StartTimer {
                id: TIMER_FIVE_PERCENT_FALLBACK,
                after: FIVE_PERCENT_FALLBACK
            }]
        );
        assert!(path.five_percent_active());
        assert_eq!(path.state(), AcState::WaitingForAuthentication);

        let stepped = path.on_timer(&s, TIMER_FIVE_PERCENT_FALLBACK, now());
        assert_eq!(
            stepped,
            vec![
                Effect::SetCpState(CpState::X1),
                Effect::StartTimer {
                    id: TIMER_T_STEP,
                    after: T_STEP_X1
                },
            ]
        );
        assert!(!path.five_percent_active());

        let resumed = path.on_timer(&s, TIMER_T_STEP, now());
        assert!(resumed.contains(&Effect::PwmOn(pwm_duty_for_current_a(16.0))));
        assert_eq!(path.state(), AcState::PrepareCharging);
    }

    #[test]
    fn a_live_charge_loop_keeps_the_five_percent_offer_rather_than_killing_the_session() {
        let mut path = hlc(PwmStart::FivePercent);
        let s = session_for(PwmStart::FivePercent);
        path.on_session_start(&s, now());
        path.on_path_event(&s, matched(), now());
        path.on_path_event(&s, PathEvent::SetupFinished, now());

        let decided = path.on_authorized(&s, now());

        assert!(decided.contains(&Effect::PwmOn(PWM_5_PERCENT)));
        assert!(path.five_percent_active());
        assert!(!decided.iter().any(|e| matches!(e, Effect::SetCpState(_))));
        assert_eq!(path.state(), AcState::PrepareCharging);
    }

    #[test]
    fn plug_and_charge_without_a_charge_loop_steps_through_state_f() {
        let mut path = hlc(PwmStart::FivePercent);
        let mut s = session_for(PwmStart::FivePercent);
        path.on_session_start(&s, now());
        s.authorized_plug_and_charge = true;

        let awaited = path.on_authorized(&s, now());
        assert_eq!(
            awaited,
            vec![Effect::StartTimer {
                id: TIMER_FIVE_PERCENT_FALLBACK,
                after: FIVE_PERCENT_FALLBACK
            }]
        );

        let stepped = path.on_timer(&s, TIMER_FIVE_PERCENT_FALLBACK, now());
        assert_eq!(
            stepped,
            vec![
                Effect::SetCpState(CpState::F),
                Effect::StartTimer {
                    id: TIMER_T_STEP,
                    after: T_STEP_EF
                },
            ]
        );
    }

    #[test]
    fn an_enforced_five_percent_offer_never_arms_the_fallback() {
        let mut path = hlc(PwmStart::FivePercentEnforced);
        let s = session_for(PwmStart::FivePercentEnforced);
        let mut effects = path.on_session_start(&s, now());
        effects.extend(path.on_path_event(&s, matched(), now()));
        effects.extend(path.on_authorized(&s, now()));

        assert!(!arms(&effects, TIMER_FIVE_PERCENT_FALLBACK));

        // And an expiry delivered anyway leaves the offer alone.
        path.on_timer(&s, TIMER_FIVE_PERCENT_FALLBACK, now());
        assert!(path.five_percent_active());
    }

    /// The `ac_with_soc` short circuit, over the whole cross product of the
    /// facts it ignores.
    ///
    /// Both presented modes proceed at once. Presenting DC takes the DC branch
    /// of both authorization arms of the C++ `WaitingForAuthentication` case,
    /// which keeps the five percent offer; presenting AC takes the AC branch
    /// with high level communication disabled for the session, which proceeds
    /// on the nominal duty cycle. Neither reads the figures, the authorization
    /// source, whether matching started or whether the fallback window elapsed,
    /// so a decision that changed with any of them would be reading a fact the
    /// C++ never consults on this path.
    #[test]
    fn an_ac_with_soc_port_proceeds_at_once_in_both_presented_modes() {
        for five_percent in [false, true] {
            for enforced in [false, true] {
                for pnc in [false, true] {
                    for matching_started in [false, true] {
                        for hlc_charging_active in [false, true] {
                            for fallback_elapsed in [false, true] {
                                for (presented, keep_five_percent) in
                                    [(PresentedMode::Dc, true), (PresentedMode::Ac, false)]
                                {
                                    let facts = AcStartFacts {
                                        five_percent,
                                        enforced,
                                        pnc,
                                        matching_started,
                                        hlc_charging_active,
                                        fallback_elapsed,
                                        with_soc: Some(presented),
                                    };
                                    assert_eq!(
                                        decide_ac_start(facts),
                                        AcStartDecision::Proceed { keep_five_percent },
                                        "{facts:?}"
                                    );
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    #[test]
    fn the_start_decision_follows_the_five_percent_and_x1_figures() {
        let facts = |five_percent, pnc, matching_started, hlc_charging_active, fallback_elapsed| {
            AcStartFacts {
                five_percent,
                enforced: false,
                pnc,
                matching_started,
                hlc_charging_active,
                fallback_elapsed,
                with_soc: None,
            }
        };

        // Enforced keeps the offer up whatever else is true.
        assert_eq!(
            decide_ac_start(AcStartFacts {
                enforced: true,
                ..facts(true, false, true, false, true)
            }),
            AcStartDecision::Proceed {
                keep_five_percent: true
            }
        );

        // Five percent start, no matching yet: withdraw through state F.
        assert_eq!(
            decide_ac_start(facts(true, false, false, false, false)),
            AcStartDecision::StepThroughEf
        );

        // X1 start: the pilot is already where nominal signalling begins.
        assert_eq!(
            decide_ac_start(facts(false, false, false, false, false)),
            AcStartDecision::Proceed {
                keep_five_percent: false
            }
        );
        assert_eq!(
            decide_ac_start(facts(false, false, true, false, false)),
            AcStartDecision::Proceed {
                keep_five_percent: false
            }
        );

        // Five percent start with matching under way: give the charge loop a
        // bounded chance, then withdraw through X1.
        assert_eq!(
            decide_ac_start(facts(true, false, true, false, false)),
            AcStartDecision::AwaitFallback
        );
        assert_eq!(
            decide_ac_start(facts(true, false, true, false, true)),
            AcStartDecision::StepThroughX1
        );

        // A live charge loop is never interrupted.
        assert_eq!(
            decide_ac_start(facts(true, false, true, true, true)),
            AcStartDecision::Proceed {
                keep_five_percent: true
            }
        );

        // Plug and Charge waits the same bounded time, then withdraws through
        // state F rather than X1.
        assert_eq!(
            decide_ac_start(facts(true, true, false, false, false)),
            AcStartDecision::AwaitFallback
        );
        assert_eq!(
            decide_ac_start(facts(true, true, false, false, true)),
            AcStartDecision::StepThroughEf
        );
        assert_eq!(
            decide_ac_start(facts(true, true, false, true, false)),
            AcStartDecision::Proceed {
                keep_five_percent: true
            }
        );

        // An X1 start gets the same Plug and Charge treatment. The compatibility
        // workaround at `Charger.cpp:530-535` reads only the observation window,
        // never the start variant, so it applies here too.
        assert_eq!(
            decide_ac_start(facts(false, true, false, false, false)),
            AcStartDecision::AwaitFallback
        );
        assert_eq!(
            decide_ac_start(facts(false, true, false, false, true)),
            AcStartDecision::StepThroughEf
        );
        // And a live charge loop moves an X1 start onto the five percent offer
        // rather than leaving it on nominal. `Charger.cpp:540`.
        assert_eq!(
            decide_ac_start(facts(false, true, false, true, false)),
            AcStartDecision::Proceed {
                keep_five_percent: true
            }
        );

        // Matching state is read by the external half only. Plug and Charge
        // gives the same answer either way.
        for matching_started in [false, true] {
            assert_eq!(
                decide_ac_start(facts(true, true, matching_started, false, true)),
                AcStartDecision::StepThroughEf
            );
        }
    }

    #[test]
    fn plug_and_charge_on_an_x1_start_still_steps_through_state_f() {
        // Vehicles that use only the Plug and Charge half of ISO 15118 close the
        // connection once authorized and expect basic charging to carry the
        // session. The pilot sequence is the nudge they need, and it does not
        // depend on the session having started at five percent.
        let mut path = hlc(PwmStart::Nominal);
        let mut s = session();
        path.on_session_start(&s, now());
        s.authorized_plug_and_charge = true;

        let awaited = path.on_authorized(&s, now());
        assert_eq!(
            awaited,
            vec![Effect::StartTimer {
                id: TIMER_FIVE_PERCENT_FALLBACK,
                after: FIVE_PERCENT_FALLBACK
            }],
            "the observation window opens for an X1 start too"
        );
        assert_eq!(path.state(), AcState::WaitingForAuthentication);

        let stepped = path.on_timer(&s, TIMER_FIVE_PERCENT_FALLBACK, now());
        assert_eq!(
            stepped,
            vec![
                Effect::SetCpState(CpState::F),
                Effect::StartTimer {
                    id: TIMER_T_STEP,
                    after: T_STEP_EF
                },
            ]
        );

        let paused_in_x1 = path.on_timer(&s, TIMER_T_STEP, now());
        assert_eq!(
            paused_in_x1,
            vec![
                Effect::SetCpState(CpState::X1),
                Effect::StartTimer {
                    id: TIMER_T_STEP,
                    after: STAY_IN_X1_AFTER_T_STEP_EF
                },
            ]
        );

        let resumed = path.on_timer(&s, TIMER_T_STEP, now());
        assert!(resumed.contains(&Effect::PwmOn(pwm_duty_for_current_a(16.0))));
        assert_eq!(path.state(), AcState::PrepareCharging);
    }

    #[test]
    fn a_live_charge_loop_moves_an_x1_start_onto_the_five_percent_offer() {
        // `Charger.cpp:536-541` raises the offer rather than keeping it, so a
        // session that started on nominal signalling is moved onto the offer the
        // live ISO session expects.
        let mut path = hlc(PwmStart::Nominal);
        let mut s = session();
        path.on_session_start(&s, now());
        s.authorized_plug_and_charge = true;
        path.on_path_event(&s, PathEvent::SetupFinished, now());

        let decided = path.on_authorized(&s, now());

        assert!(path.five_percent_active());
        assert!(decided.contains(&Effect::PwmOn(PWM_5_PERCENT)));
        assert!(!decided.iter().any(|e| matches!(e, Effect::SetCpState(_))));
    }

    #[test]
    fn an_enforcing_port_still_falls_back_under_plug_and_charge() {
        // `ac_enforce_hlc` is read by the external half only, at
        // `Charger.cpp:396`. The Plug and Charge half must keep its escape
        // hatch: holding five percent up would mean a vehicle that uses only the
        // Plug and Charge part of ISO 15118 and then closes the connection never
        // charges at all, and `ac_enforce_hlc` is set on shipped hardware
        // configurations. This test is what stops that drifting back.
        let mut path = hlc(PwmStart::FivePercentEnforced);
        let mut s = session_for(PwmStart::FivePercentEnforced);
        path.on_session_start(&s, now());
        s.authorized_plug_and_charge = true;

        let awaited = path.on_authorized(&s, now());
        assert_eq!(
            awaited,
            vec![Effect::StartTimer {
                id: TIMER_FIVE_PERCENT_FALLBACK,
                after: FIVE_PERCENT_FALLBACK
            }],
            "an enforcing port opens the observation window under Plug and Charge"
        );
        assert!(path.five_percent_active());

        let stepped = path.on_timer(&s, TIMER_FIVE_PERCENT_FALLBACK, now());
        assert_eq!(
            stepped,
            vec![
                Effect::SetCpState(CpState::F),
                Effect::StartTimer {
                    id: TIMER_T_STEP,
                    after: T_STEP_EF
                },
            ]
        );

        let paused_in_x1 = path.on_timer(&s, TIMER_T_STEP, now());
        assert_eq!(
            paused_in_x1,
            vec![
                Effect::SetCpState(CpState::X1),
                Effect::StartTimer {
                    id: TIMER_T_STEP,
                    after: STAY_IN_X1_AFTER_T_STEP_EF
                },
            ]
        );

        let resumed = path.on_timer(&s, TIMER_T_STEP, now());
        assert!(resumed.contains(&Effect::PwmOn(pwm_duty_for_current_a(16.0))));
        assert!(
            !path.five_percent_active(),
            "the offer becomes nominal so a basic charging vehicle can draw"
        );
        assert_eq!(path.state(), AcState::PrepareCharging);
    }

    #[test]
    fn an_enforcing_port_never_falls_back_on_the_external_path() {
        // The guard the C++ does have, at `Charger.cpp:396-403`, unaffected by
        // the Plug and Charge half above.
        let mut path = hlc(PwmStart::FivePercentEnforced);
        let s = session_for(PwmStart::FivePercentEnforced);
        let mut effects = path.on_session_start(&s, now());
        effects.extend(path.on_path_event(&s, matched(), now()));
        effects.extend(path.on_authorized(&s, now()));

        assert!(!arms(&effects, TIMER_FIVE_PERCENT_FALLBACK));
        assert!(path.five_percent_active());
        assert_eq!(path.state(), AcState::PrepareCharging);
        assert!(effects.contains(&Effect::PwmOn(PWM_5_PERCENT)));

        // And an expiry that reached it anyway changes nothing, because the
        // window was never opened.
        assert!(path
            .on_timer(&s, TIMER_FIVE_PERCENT_FALLBACK, now())
            .is_empty());
        assert!(path.five_percent_active());
    }

    #[test]
    fn the_authorization_loop_waits_a_bounded_time_for_energy() {
        let mut path = hlc(PwmStart::Nominal);
        let s = session_with_current(0.0);
        path.on_session_start(&s, now());
        budget_reported(&mut path, &s);

        let waiting = path.on_authorized(&s, now());

        assert_eq!(
            waiting,
            vec![Effect::StartTimer {
                id: TIMER_WAIT_FOR_ENERGY,
                after: WAIT_FOR_ENERGY_IN_AUTH_LOOP
            }]
        );
        assert_eq!(path.state(), AcState::WaitingForAuthentication);

        let proceeded = path.on_timer(&s, TIMER_WAIT_FOR_ENERGY, now());

        assert_eq!(path.state(), AcState::PrepareCharging);
        assert!(proceeded.iter().any(|e| matches!(e, Effect::PwmOn(_))));
    }

    #[test]
    fn a_second_look_while_waiting_for_energy_neither_proceeds_nor_re_arms() {
        // The loop is re-entered by every fact that could complete it, so the
        // wait has to be idempotent: it must not proceed on a budget that is
        // still zero, and it must not restart its own deadline each time.
        let mut path = hlc(PwmStart::Nominal);
        let empty = session_with_current(0.0);
        path.on_session_start(&empty, now());
        budget_reported(&mut path, &empty);
        path.on_authorized(&empty, now());

        let again = path.on_limits_changed(&empty, now());

        assert_eq!(path.state(), AcState::WaitingForAuthentication);
        assert!(!arms(&again, TIMER_WAIT_FOR_ENERGY));
        assert!(!again.iter().any(|e| matches!(e, Effect::PwmOn(_))));
    }

    #[test]
    fn energy_arriving_ends_the_wait_and_prepares_charging() {
        let mut path = hlc(PwmStart::Nominal);
        let empty = session_with_current(0.0);
        path.on_session_start(&empty, now());
        budget_reported(&mut path, &empty);
        path.on_authorized(&empty, now());

        let effects = path.on_limits_changed(&session(), now());

        assert!(effects.contains(&Effect::CancelTimer {
            id: TIMER_WAIT_FOR_ENERGY
        }));
        assert_eq!(path.state(), AcState::PrepareCharging);
    }

    #[test]
    fn the_basic_path_also_bounds_its_wait_for_energy() {
        let mut path = basic();
        let empty = session_with_current(0.0);
        path.on_session_start(&empty, now());
        budget_reported(&mut path, &empty);

        assert_eq!(
            path.on_authorized(&empty, now()),
            vec![Effect::StartTimer {
                id: TIMER_WAIT_FOR_ENERGY,
                after: WAIT_FOR_ENERGY_IN_AUTH_LOOP
            }]
        );

        path.on_timer(&empty, TIMER_WAIT_FOR_ENERGY, now());

        assert_eq!(path.state(), AcState::PrepareCharging);
    }

    #[test]
    fn a_stop_arms_the_stopping_charging_timeout_and_the_timeout_removes_energy() {
        let mut path = basic();
        let s = session();
        path.on_session_start(&s, now());
        path.on_authorized(&s, now());
        path.on_bsp(&s, &BspEvent::Cp(CpEvent::C), CpEdges::default(), now());

        let stopping = path.on_stop(&s, StopReason::Local, now());
        assert!(stopping.contains(&Effect::StartTimer {
            id: TIMER_STOPPING_CHARGING,
            after: STOPPING_CHARGING_TIMEOUT
        }));

        let forced = path.on_timer(&s, TIMER_STOPPING_CHARGING, now());

        assert_eq!(
            forced,
            vec![Effect::AllowPowerOn(false)],
            "the timeout removes energy under load rather than ending the session"
        );
        assert_eq!(path.state(), AcState::StoppingCharging);
    }

    #[test]
    fn every_named_timer_has_its_own_identity_apart_from_the_pilot_detour() {
        let named = [
            AcTimer::C1,
            AcTimer::CpStateFUnlock,
            AcTimer::StoppingCharging,
            AcTimer::FivePercentFallback,
            AcTimer::WaitForEnergy,
        ];
        for (index, first) in named.iter().enumerate() {
            for second in &named[index + 1..] {
                assert_ne!(timer_id(*first), timer_id(*second), "{first:?} {second:?}");
            }
            assert_ne!(timer_id(*first), TIMER_T_STEP, "{first:?}");
        }

        for step in [AcTimer::TStepEf, AcTimer::TStepEfX1Pause, AcTimer::TStepX1] {
            assert_eq!(timer_id(step), TIMER_T_STEP);
        }
    }

    #[test]
    fn enforced_five_percent_never_falls_back() {
        let mut path = AcHlc::new(config(), PwmStart::FivePercentEnforced);

        path.on_timer(&session(), TIMER_FIVE_PERCENT_FALLBACK, now());

        assert!(
            path.five_percent_active(),
            "an enforced offer must not fall back"
        );
    }

    /// `AcHlc` takes the break too, and its command shaping does not disturb
    /// it.
    ///
    /// Every core level break test drives `AcBasic`. This is the same feature
    /// on the other AC path, which is a real deployment: `ac_hlc_enabled` with
    /// a nominal duty cycle, the vehicle charging on basic IEC because high
    /// level communication has not taken the session over, so
    /// `hlc_charging_active` is false and the request is not refused.
    #[test]
    fn the_hlc_path_takes_the_switching_break_and_returns_from_it() {
        let s = session();
        let mut path = hlc(PwmStart::Nominal);
        path.on_session_start(&s, now());
        path.on_authorized(&s, now());
        path.on_bsp(&s, &BspEvent::Cp(CpEvent::C), CpEdges::default(), now());
        assert_eq!(path.state(), AcState::Charging);
        assert!(!path.hlc_charging_active(), "the request would be refused");

        let entered = path.on_path_event(
            &s,
            PathEvent::SwitchPhases {
                three_phases: false,
            },
            now(),
        );

        assert_eq!(path.state(), AcState::SwitchPhases);
        assert!(
            entered.contains(&Effect::SetCpState(CpState::X1)),
            "the offer stayed on the pilot: {entered:?}"
        );
        assert!(
            !entered
                .iter()
                .any(|e| matches!(e, Effect::SwitchThreePhases(_))),
            "the relays moved under load: {entered:?}"
        );

        let done = path.on_timer(&s, TIMER_SWITCH_PHASES, now());

        assert!(done.contains(&Effect::SwitchThreePhases(false)), "{done:?}");
        assert!(
            done.contains(&Effect::PwmOn(pwm_duty_for_current_a(16.0))),
            "the offer was not restored: {done:?}"
        );
        assert_eq!(path.state(), AcState::PrepareCharging);
    }

    /// The one place `AcHlc::shape` and the break meet.
    ///
    /// The break's exit re-derives the offer from the current limit, and under
    /// a five percent session that has to come back as five percent rather
    /// than as a nominal duty: `Charger.cpp:706-711` picks between the two on
    /// `hlc_use_5percent_current_session`, and here `shape` is what does it. A
    /// nominal duty restored to a five percent session would invite a vehicle
    /// that has no high level communication to draw the full limit.
    #[test]
    fn the_break_restores_a_five_percent_offer_as_five_percent() {
        let five = session_for(PwmStart::FivePercent);
        let mut path = hlc(PwmStart::FivePercent);
        path.on_session_start(&five, now());
        // Straight into charging without running the authorization loop, which
        // is what would withdraw the five percent offer.
        path.iec.handle(IecInput::AuthorizationAccepted);
        path.iec.handle(IecInput::TransactionStarted);
        path.iec.handle(IecInput::CarRequestedPower);
        assert_eq!(path.state(), AcState::Charging);
        assert!(path.five_percent_active());

        path.on_path_event(
            &five,
            PathEvent::SwitchPhases {
                three_phases: false,
            },
            now(),
        );
        let done = path.on_timer(&five, TIMER_SWITCH_PHASES, now());

        assert!(done.contains(&Effect::SwitchThreePhases(false)), "{done:?}");
        assert!(
            done.contains(&Effect::PwmOn(PWM_5_PERCENT)),
            "a five percent session got a nominal offer back: {done:?}"
        );
        assert!(
            !done.contains(&Effect::PwmOn(pwm_duty_for_current_a(16.0))),
            "{done:?}"
        );
    }

    /// The three branches of `AcHlc::signalled_current_a`, which is what soft
    /// overcurrent detection measures against.
    ///
    /// The middle one is the safety critical one. A five percent session that
    /// the vehicle has taken over has negotiated a real current over ISO 15118
    /// while the pilot still carries five percent, and reading the pilot there
    /// would measure it against the noise floor and stop every such session.
    #[test]
    fn a_five_percent_session_signals_the_negotiated_limit_once_the_vehicle_takes_over() {
        let five = session_for(PwmStart::FivePercent);
        let mut path = hlc(PwmStart::FivePercent);
        path.on_session_start(&five, now());

        // The offer is five percent and the vehicle has not taken over: no
        // current has been signalled. The authorization loop is what would
        // withdraw the offer, and it has not run.
        assert!(path.five_percent_active());
        assert!(!path.hlc_charging_active());
        assert_eq!(path.signalled_current_a(), 0.0);

        // `EvseManager.cpp:394` into `Charger::set_hlc_charging_active`. The
        // offer stays at five percent across it, which is the shape that makes
        // the second branch load bearing.
        path.on_path_event(&five, PathEvent::SetupFinished, now());
        assert!(path.hlc_charging_active());
        assert!(path.five_percent_active());
        assert_eq!(path.signalled_current_a(), 16.0);
    }

    /// The invariant `Iec::signalled_current_a` rests on: this reducer never
    /// issues a five percent offer, so `pwm_running` is false for the whole of
    /// one and the zero comes from there rather than from a second test of
    /// `five_percent`.
    ///
    /// A limit change is what would break it. If the five percent offer were
    /// ever routed through the reducer, `pwm_running` would be true, this limit
    /// change would re-derive a nominal duty, and soft overcurrent detection
    /// would start measuring a five percent session against twenty amperes
    /// instead of against the noise floor. That is the failure this pins.
    #[test]
    fn a_five_percent_offer_signals_no_current_even_after_a_limit_change() {
        let five = session_for(PwmStart::FivePercent);
        let mut path = hlc(PwmStart::FivePercent);
        path.on_session_start(&five, now());
        assert!(path.five_percent_active());

        // The limit is real and is not the reason the answer is zero.
        assert_eq!(path.iec.current_limit_a(), 16.0);
        assert_eq!(path.signalled_current_a(), 0.0);

        path.on_limits_changed(&session_with_current(20.0), now());
        assert_eq!(path.iec.current_limit_a(), 20.0);
        assert_eq!(
            path.signalled_current_a(),
            0.0,
            "a five percent offer signalled a nominal current"
        );
    }

    /// A nominal AC high level communication session signals its offer the same
    /// way basic charging does, until the vehicle takes over.
    #[test]
    fn a_nominal_hlc_session_signals_the_pilot_offer_before_the_vehicle_takes_over() {
        let mut path = hlc(PwmStart::Nominal);
        path.on_session_start(&session(), now());
        path.on_authorized(&session(), now());
        assert_eq!(path.state(), AcState::PrepareCharging);
        assert!(!path.five_percent_active());

        assert_eq!(path.signalled_current_a(), 16.0);
    }

    /// `AcBasic` has no high level communication and no five percent offer, so
    /// its answer is the pilot's alone: the limit while an offer stands, zero
    /// once it is withdrawn.
    #[test]
    fn the_basic_path_signals_its_pilot_offer_and_nothing_once_it_is_withdrawn() {
        let mut path = basic();
        path.on_session_start(&session(), now());
        path.on_authorized(&session(), now());
        path.on_bsp(&session(), &BspEvent::Cp(CpEvent::C), CpEdges::default(), now());
        assert_eq!(path.state(), AcState::Charging);
        assert_eq!(path.signalled_current_a(), 16.0);

        path.on_path_event(&session(), pause(), now());
        assert_eq!(path.state(), AcState::ChargingPausedEvse);
        assert_eq!(path.signalled_current_a(), 0.0);
    }

    fn disable() -> PathEvent {
        PathEvent::Disable
    }

    fn enable() -> PathEvent {
        PathEvent::Enable
    }

    #[test]
    fn a_winning_disable_stops_the_basic_board_and_holds_the_vehicle() {
        // The whole point of the handover: the arbitrated decision has to reach
        // the board, not only the announcement. `Charger.cpp:203-204` signals
        // the control pilot and stops the output, and never unlocks.
        let mut path = basic();

        let effects = path.on_path_event(&session(), disable(), now());

        assert_eq!(path.state(), AcState::Disabled);
        let cp = effects
            .iter()
            .position(|effect| *effect == Effect::SetCpState(CpState::F))
            .expect("a disabled port signals it is unavailable");
        let stop = effects
            .iter()
            .position(|effect| *effect == Effect::BspEnable(false))
            .expect("a disabled port is stopped at the board");
        assert!(cp < stop, "got {effects:?}");
        assert!(
            !effects.contains(&Effect::UnlockConnector),
            "an availability change never releases the vehicle, got {effects:?}"
        );
    }

    #[test]
    fn a_winning_disable_stops_the_high_level_board_and_holds_the_vehicle() {
        let mut path = hlc(PwmStart::Nominal);

        let effects = path.on_path_event(&session(), disable(), now());

        assert_eq!(path.state(), AcState::Disabled);
        assert!(
            effects.contains(&Effect::SetCpState(CpState::F))
                && effects.contains(&Effect::BspEnable(false)),
            "got {effects:?}"
        );
        assert!(
            !effects.contains(&Effect::UnlockConnector),
            "got {effects:?}"
        );
    }

    #[test]
    fn a_disable_on_a_live_session_releases_the_vehicle_only_on_the_unplug() {
        // An operator disabling an occupied port routes the session through
        // stopping. The latch is popped by the unplug that follows and by
        // nothing before it.
        let mut path = basic();
        path.on_session_start(&session(), now());
        path.on_authorized(&session(), now());
        path.on_bsp(&session(), &BspEvent::Cp(CpEvent::C), CpEdges::default(), now());

        let stopping = path.on_stop(&session(), StopReason::EvseDisabled, now());
        let disabling = path.on_path_event(&session(), disable(), now());

        assert!(
            !stopping.contains(&Effect::UnlockConnector)
                && !disabling.contains(&Effect::UnlockConnector),
            "stopping {stopping:?}, disabling {disabling:?}"
        );
        assert!(
            disabling.contains(&Effect::AllowPowerOn(false)),
            "got {disabling:?}"
        );

        let unplugged = path.on_bsp(&session(), &BspEvent::Cp(CpEvent::A), CpEdges::default(), now());

        assert!(
            unplugged.contains(&Effect::UnlockConnector),
            "the unplug is what releases the vehicle, got {unplugged:?}"
        );
    }

    #[test]
    fn a_disable_on_a_live_high_level_session_removes_energy_and_holds_the_vehicle() {
        let mut path = hlc(PwmStart::Nominal);
        path.on_session_start(&session(), now());
        path.on_authorized(&session(), now());
        path.on_bsp(&session(), &BspEvent::Cp(CpEvent::C), CpEdges::default(), now());
        assert_eq!(
            path.state(),
            AcState::Charging,
            "the session must be live for this test to mean anything"
        );

        let stopping = path.on_stop(&session(), StopReason::EvseDisabled, now());
        let disabling = path.on_path_event(&session(), disable(), now());

        assert_eq!(path.state(), AcState::Disabled);
        assert!(
            disabling.contains(&Effect::AllowPowerOn(false))
                && disabling.contains(&Effect::SetCpState(CpState::F))
                && disabling.contains(&Effect::BspEnable(false)),
            "got {disabling:?}"
        );
        assert!(
            !stopping.contains(&Effect::UnlockConnector)
                && !disabling.contains(&Effect::UnlockConnector),
            "stopping {stopping:?}, disabling {disabling:?}"
        );

        let unplugged = path.on_bsp(&session(), &BspEvent::Cp(CpEvent::A), CpEdges::default(), now());

        assert!(
            unplugged.contains(&Effect::UnlockConnector),
            "got {unplugged:?}"
        );
    }

    #[test]
    fn a_winning_enable_returns_the_basic_port_to_service() {
        // `Charger.cpp:1730` starts the board and the Idle entry that follows
        // signals availability at `:230`. Without the second half the port
        // stays on state F and never invites a vehicle again.
        let mut path = basic();
        path.on_path_event(&session(), disable(), now());

        let effects = path.on_path_event(&session(), enable(), now());

        assert_eq!(path.state(), AcState::Idle);
        assert_eq!(
            effects,
            vec![Effect::BspEnable(true), Effect::SetCpState(CpState::X1)],
            "got {effects:?}"
        );
    }

    #[test]
    fn a_winning_enable_returns_the_high_level_port_to_service() {
        let mut path = hlc(PwmStart::Nominal);
        path.on_path_event(&session(), disable(), now());

        let effects = path.on_path_event(&session(), enable(), now());

        assert_eq!(path.state(), AcState::Idle);
        assert_eq!(
            effects,
            vec![Effect::BspEnable(true), Effect::SetCpState(CpState::X1)],
            "got {effects:?}"
        );
    }

    #[test]
    fn a_disable_cancels_a_pilot_detour_still_under_way() {
        // The detour's next step signals X1, which advertises availability. A
        // disabled port must not reach that step.
        let mut path = hlc(PwmStart::FivePercent);
        path.on_session_start(&session_for(PwmStart::FivePercent), now());
        path.on_authorized(&session_for(PwmStart::FivePercent), now());
        assert!(
            arms(
                &path.on_path_event(&session_for(PwmStart::FivePercent), matched(), now()),
                TIMER_T_STEP
            ) || path.pilot_step != PilotStep::None,
            "the detour must be under way for this test to mean anything"
        );

        let effects = path.on_path_event(&session_for(PwmStart::FivePercent), disable(), now());

        assert!(
            effects.contains(&Effect::CancelTimer { id: TIMER_T_STEP }),
            "got {effects:?}"
        );

        let stepped = path.on_timer(&session_for(PwmStart::FivePercent), TIMER_T_STEP, now());

        assert!(
            !stepped
                .iter()
                .any(|effect| matches!(effect, Effect::SetCpState(_))),
            "a disabled port must not signal a pilot state from a stale detour, got {stepped:?}"
        );
    }

    #[test]
    fn a_disable_clears_the_authorization_the_loop_was_holding() {
        // The loop's own copy of the authorization outlives the reducer's. Left
        // set, the next budget update after the port returns to service starts
        // a transaction nobody authorized.
        let mut path = basic();
        path.on_session_start(&session(), now());
        path.on_authorized(&session(), now());
        path.on_path_event(&session(), disable(), now());
        path.on_path_event(&session(), enable(), now());
        path.on_session_start(&session(), now());

        let effects = path.on_limits_changed(&session(), now());

        assert_eq!(
            path.state(),
            AcState::WaitingForAuthentication,
            "got {effects:?}"
        );
        assert!(
            !effects
                .iter()
                .any(|effect| matches!(effect, Effect::PwmOn(_))),
            "an unauthorized port must not offer a duty cycle, got {effects:?}"
        );
    }

    #[test]
    fn a_disable_clears_the_high_level_authorization_the_loop_was_holding() {
        let mut path = hlc(PwmStart::Nominal);
        path.on_session_start(&session(), now());
        path.on_authorized(&session(), now());
        path.on_path_event(&session(), disable(), now());
        path.on_path_event(&session(), enable(), now());
        path.on_session_start(&session(), now());

        let effects = path.on_limits_changed(&session(), now());

        assert_eq!(path.state(), AcState::WaitingForAuthentication);
        assert!(
            !effects
                .iter()
                .any(|effect| matches!(effect, Effect::PwmOn(_))),
            "an unauthorized port must not offer a duty cycle, got {effects:?}"
        );
    }

    #[test]
    fn a_port_returning_to_service_decides_the_next_start_on_fresh_facts() {
        // The start decision reads what the session negotiated. A fact that
        // survived the port leaving service would decide the next one.
        let mut path = hlc(PwmStart::FivePercent);
        path.on_session_start(&session_for(PwmStart::FivePercent), now());
        path.on_path_event(&session_for(PwmStart::FivePercent), matched(), now());
        path.on_path_event(
            &session_for(PwmStart::FivePercent),
            PathEvent::SetupFinished,
            now(),
        );

        path.on_path_event(&session_for(PwmStart::FivePercent), disable(), now());
        path.on_path_event(&session_for(PwmStart::FivePercent), enable(), now());

        assert_eq!(path.facts(), hlc(PwmStart::FivePercent).facts());
    }

    #[test]
    fn a_disable_withdraws_the_high_level_half_of_the_contactor_permission() {
        // The permission belongs to the session that was granted it. Surviving,
        // it would open the gate for the next vehicle before high level
        // communication had said anything.
        let mut path = hlc(PwmStart::FivePercent);
        path.on_session_start(&session_for(PwmStart::FivePercent), now());
        path.on_path_event(
            &session_for(PwmStart::FivePercent),
            PathEvent::AllowCloseContactor(true),
            now(),
        );

        path.on_path_event(&session_for(PwmStart::FivePercent), disable(), now());
        path.on_path_event(&session_for(PwmStart::FivePercent), enable(), now());

        assert!(
            path.contactor_gate_shut(),
            "the gate must be shut again after the port returns to service"
        );
    }

    #[test]
    fn a_disable_drops_a_power_request_the_contactor_gate_was_holding() {
        // Replaying it once high level communication grants its half would
        // close the contactor for a vehicle that has since been disconnected
        // from the decision.
        let mut path = hlc(PwmStart::FivePercent);
        path.on_session_start(&session_for(PwmStart::FivePercent), now());
        path.on_bsp(
            &session_for(PwmStart::FivePercent),
            &BspEvent::Cp(CpEvent::C),
            CpEdges::default(),
            now(),
        );

        path.on_path_event(&session_for(PwmStart::FivePercent), disable(), now());
        path.on_path_event(&session_for(PwmStart::FivePercent), enable(), now());
        let replayed = path.on_path_event(
            &session_for(PwmStart::FivePercent),
            PathEvent::AllowCloseContactor(true),
            now(),
        );

        assert!(
            replayed.is_empty(),
            "a held request must not survive the port leaving service, got {replayed:?}"
        );
    }

    #[test]
    fn a_disable_ends_the_wait_for_energy_the_session_had_given_up_on() {
        // The wait is given up on once per session. Carried over, the next
        // session starts on a budget it never waited for.
        let mut path = basic();
        let empty = session_with_current(0.0);
        path.on_session_start(&session(), now());
        budget_reported(&mut path, &empty);
        path.on_authorized(&empty, now());
        path.on_timer(&empty, TIMER_WAIT_FOR_ENERGY, now());

        path.on_path_event(&session(), disable(), now());
        path.on_path_event(&session(), enable(), now());
        path.on_session_start(&session(), now());
        budget_reported(&mut path, &empty);
        let effects = path.on_authorized(&empty, now());

        assert!(
            arms(&effects, TIMER_WAIT_FOR_ENERGY),
            "the next session waits for its own budget, got {effects:?}"
        );
    }

    #[test]
    fn a_pause_request_withdraws_the_offer_on_both_ac_paths() {
        // `Charger::pause_charging` (`Charger.cpp:1330-1336`) raises the flag
        // that `Charger.cpp:782-786` reads to leave `Charging`. Without a route
        // for the command, `AcState::ChargingPausedEvse` is unreachable in a
        // real deployment.
        let s = session();
        let mut basic_path = basic();
        basic_path.on_session_start(&s, now());
        basic_path.on_authorized(&s, now());
        basic_path.on_bsp(&s, &BspEvent::Cp(CpEvent::C), CpEdges::default(), now());
        assert_eq!(basic_path.state(), AcState::Charging);

        let mut hlc_path = hlc(PwmStart::Nominal);
        hlc_path.on_session_start(&s, now());
        hlc_path.on_authorized(&s, now());
        hlc_path.on_bsp(&s, &BspEvent::Cp(CpEvent::C), CpEdges::default(), now());
        assert_eq!(hlc_path.state(), AcState::Charging);

        let paused_basic = basic_path.on_path_event(&s, pause(), now());
        assert_eq!(basic_path.state(), AcState::ChargingPausedEvse);
        assert!(
            paused_basic.contains(&Effect::PwmOff),
            "got {paused_basic:?}"
        );

        let paused_hlc = hlc_path.on_path_event(&s, pause(), now());
        assert_eq!(hlc_path.state(), AcState::ChargingPausedEvse);
        assert!(paused_hlc.contains(&Effect::PwmOff), "got {paused_hlc:?}");
    }

    #[test]
    fn a_resume_request_restores_the_offer_on_both_ac_paths() {
        // `Charger::resume_charging` (`Charger.cpp:1338-1345`) clears the flag,
        // and `Charger.cpp:975-990` leaves `ChargingPausedEVSE` once no reason
        // remains.
        let s = session();
        let mut basic_path = basic();
        basic_path.on_session_start(&s, now());
        basic_path.on_authorized(&s, now());
        basic_path.on_bsp(&s, &BspEvent::Cp(CpEvent::C), CpEdges::default(), now());
        basic_path.on_path_event(&s, pause(), now());

        let mut hlc_path = hlc(PwmStart::Nominal);
        hlc_path.on_session_start(&s, now());
        hlc_path.on_authorized(&s, now());
        hlc_path.on_bsp(&s, &BspEvent::Cp(CpEvent::C), CpEdges::default(), now());
        hlc_path.on_path_event(&s, pause(), now());

        let resumed_basic = basic_path.on_path_event(&s, resume(), now());
        assert!(
            resumed_basic.contains(&Effect::PwmOn(pwm_duty_for_current_a(16.0))),
            "got {resumed_basic:?}"
        );

        let resumed_hlc = hlc_path.on_path_event(&s, resume(), now());
        assert!(
            resumed_hlc.contains(&Effect::PwmOn(pwm_duty_for_current_a(16.0))),
            "got {resumed_hlc:?}"
        );
    }

    #[test]
    fn a_dc_only_stage_leaves_the_ac_paths_alone() {
        // Neither `start_cable_check` nor `start_pre_charge` is subscribed
        // outside the C++ `charge_mode == "DC"` branch, so on an AC port these
        // have no producer. Both paths are parked in an authorization loop
        // that would emit on a nudge, so an arm that quietly ran the loop from
        // one of these would show here.
        let waiting = session_with_current(0.0);
        let mut basic_path = basic();
        basic_path.on_session_start(&waiting, now());
        budget_reported(&mut basic_path, &waiting);
        basic_path.on_authorized(&waiting, now());
        assert_eq!(basic_path.state(), AcState::WaitingForAuthentication);

        let mut hlc_path = hlc(PwmStart::Nominal);
        hlc_path.on_session_start(&waiting, now());
        budget_reported(&mut hlc_path, &waiting);
        hlc_path.on_authorized(&waiting, now());
        assert_eq!(hlc_path.state(), AcState::WaitingForAuthentication);

        // A budget is present now, so the loop would proceed if it ran.
        for stage in [PathEvent::CableCheckRequired, PathEvent::PreChargeStarted] {
            for effects in [
                basic_path.on_path_event(&session(), stage, now()),
                hlc_path.on_path_event(&session(), stage, now()),
            ] {
                assert!(effects.is_empty(), "{stage:?} got {effects:?}");
            }
        }
        assert_eq!(basic_path.state(), AcState::WaitingForAuthentication);
        assert_eq!(hlc_path.state(), AcState::WaitingForAuthentication);
    }

    #[test]
    fn a_pause_does_not_end_the_high_level_communication_session() {
        // `Charger::pause_charging` raises one flag and leaves the transaction
        // open (`Charger.cpp:1330-1336`), so everything the session negotiated
        // has to survive it. Routed through the disable arm instead, the
        // contactor permission is dropped and the vehicle is refused power
        // after the resume.
        let mut path = hlc(PwmStart::FivePercentEnforced);
        let s = session_for(PwmStart::FivePercentEnforced);
        path.on_session_start(&s, now());
        path.on_authorized(&s, now());
        path.on_bsp(&s, &BspEvent::Cp(CpEvent::C), CpEdges::default(), now());
        path.on_path_event(&s, PathEvent::AllowCloseContactor(true), now());
        assert_eq!(path.state(), AcState::Charging);

        path.on_path_event(&s, pause(), now());
        assert_eq!(path.state(), AcState::ChargingPausedEvse);
        path.on_path_event(&s, resume(), now());

        let requested = path.on_bsp(&s, &BspEvent::Cp(CpEvent::C), CpEdges::default(), now());

        assert!(
            requested.contains(&Effect::AllowPowerOn(true)),
            "the contactor permission must survive the pause, got {requested:?}"
        );
        assert_eq!(path.state(), AcState::Charging);
    }

    #[test]
    fn safe_state_removes_energy_first_on_both_ac_paths() {
        for effects in [
            AcBasic::new(config()).to_safe_state(),
            AcHlc::new(config(), PwmStart::Nominal).to_safe_state(),
        ] {
            let allow_off = effects
                .iter()
                .position(|e| *e == Effect::AllowPowerOn(false))
                .expect("power must be withdrawn");
            let cp_f = effects
                .iter()
                .position(|e| matches!(e, Effect::SetCpState(_)))
                .expect("cp state must be signalled");
            assert!(allow_off < cp_f);
        }
    }
    #[test]
    fn the_board_support_enable_carries_the_direction_it_was_asked_for() {
        // `IecCommand::Enable(bool)` is the board support control pilot output
        // enable (`Charger.cpp:96`, `:204`, `:1730`). Discarding the bool would
        // turn a stop into a start.
        assert_eq!(
            to_effects(vec![IecCommand::Enable(true)]),
            vec![Effect::BspEnable(true)]
        );
        assert_eq!(
            to_effects(vec![IecCommand::Enable(false)]),
            vec![Effect::BspEnable(false)]
        );
    }

    #[test]
    fn leaving_startup_starts_the_board_before_anything_else() {
        // `Charger::main_thread` enables the output first (`Charger.cpp:96`).
        // The core's own startup transition emits the same enable, so this is
        // the reducer's copy of it; two enables of the same direction are
        // harmless and losing this one would be silent.
        let mut path = AcBasic::new(config());

        let effects = to_effects(path.iec.handle(IecInput::StartupComplete));

        assert_eq!(
            effects.first(),
            Some(&Effect::BspEnable(true)),
            "got {effects:?}"
        );
    }

    #[test]
    fn the_disabled_transition_stops_the_board_and_not_only_the_state() {
        let mut path = basic();
        path.iec.handle(IecInput::CarPluggedIn);

        let effects = to_effects(path.iec.handle(IecInput::Disable));

        assert_eq!(path.state(), AcState::Disabled);
        assert!(
            effects.contains(&Effect::BspEnable(false)),
            "a disabled port must be stopped at the board, got {effects:?}"
        );
    }

    #[test]
    fn the_disabled_transition_signals_the_control_pilot_before_it_stops_the_board() {
        // `Charger.cpp:203-204` calls `cp_state_F()` then `bsp->enable(false)`,
        // and the comment at `:1763` says the pair is placed in that handler to
        // guarantee that order.
        let mut path = basic();
        path.iec.handle(IecInput::CarPluggedIn);

        let effects = to_effects(path.iec.handle(IecInput::Disable));

        let cp = effects
            .iter()
            .position(|effect| matches!(effect, Effect::SetCpState(_)))
            .expect("the control pilot state must be signalled");
        let stop = effects
            .iter()
            .position(|effect| *effect == Effect::BspEnable(false))
            .expect("the board must be stopped");
        assert!(cp < stop, "got {effects:?}");
    }

    #[test]
    fn re_enabling_starts_the_board_again() {
        // `Charger.cpp:1728-1730` clears the disable flag and re-enables the
        // board before the state machine sees Idle, whose entry then signals
        // availability at `:230`.
        let mut path = basic();
        path.iec.handle(IecInput::Disable);

        let effects = to_effects(path.iec.handle(IecInput::Enable));

        assert_eq!(
            effects,
            vec![Effect::BspEnable(true), Effect::SetCpState(CpState::X1)]
        );
        assert_eq!(path.state(), AcState::Idle);
    }

    #[test]
    fn no_transition_inside_a_session_publishes_ready() {
        // `ready` is the module's own boot announcement and belongs to the
        // startup transition alone. A board support enable is not it.
        let mut path = basic();
        let mut seen = Vec::new();
        for input in [
            IecInput::CarPluggedIn,
            IecInput::AuthorizationAccepted,
            IecInput::TransactionStarted,
            IecInput::CarRequestedPower,
            IecInput::Disable,
            IecInput::Enable,
            IecInput::CarUnplugged,
        ] {
            seen.extend(to_effects(path.iec.handle(input)));
        }

        assert!(
            !seen
                .iter()
                .any(|effect| matches!(effect, Effect::PublishReady(_))),
            "got {seen:?}"
        );
    }
    #[test]
    fn an_unplug_clears_the_authorization_the_loop_was_holding() {
        // `Charger.cpp:231`: the Idle entry deauthorizes. The path keeps its own
        // copy of the authorization, which the reducer cannot clear for it, so
        // without this the next vehicle's first budget update starts a
        // transaction nobody authorized.
        let mut path = basic();
        let s = session();
        path.on_session_start(&s, now());
        path.on_authorized(&s, now());
        path.on_bsp(&s, &BspEvent::Cp(CpEvent::A), CpEdges::default(), now());

        assert!(!path.authorized, "the unplug is what clears it");

        path.on_session_start(&s, now());
        let effects = path.on_limits_changed(&s, now());

        assert_eq!(
            path.state(),
            AcState::WaitingForAuthentication,
            "got {effects:?}"
        );
        assert!(
            !effects
                .iter()
                .any(|effect| matches!(effect, Effect::PwmOn(_))),
            "an unauthorized port must not offer a duty cycle, got {effects:?}"
        );
    }

    #[test]
    fn an_unplug_clears_the_high_level_authorization_the_loop_was_holding() {
        let mut path = hlc(PwmStart::Nominal);
        let s = session();
        path.on_session_start(&s, now());
        path.on_authorized(&s, now());
        path.on_bsp(&s, &BspEvent::Cp(CpEvent::A), CpEdges::default(), now());
        path.on_session_start(&s, now());

        let effects = path.on_limits_changed(&s, now());

        assert_eq!(
            path.state(),
            AcState::WaitingForAuthentication,
            "got {effects:?}"
        );
        assert!(
            !effects
                .iter()
                .any(|effect| matches!(effect, Effect::PwmOn(_))),
            "an unauthorized port must not offer a duty cycle, got {effects:?}"
        );
    }

    #[test]
    fn a_disabled_port_unplugged_and_replugged_holds_no_authorization() {
        // The full cycle the two clears have to survive together: the port
        // leaves service holding an authorization, the vehicle goes, the port
        // returns to service and the next vehicle arrives.
        let mut path = basic();
        let s = session();
        path.on_session_start(&s, now());
        path.on_authorized(&s, now());
        path.on_path_event(&s, disable(), now());
        path.on_bsp(&s, &BspEvent::Cp(CpEvent::A), CpEdges::default(), now());

        assert_eq!(
            path.state(),
            AcState::Disabled,
            "an unplug does not return a disabled port to service"
        );

        path.on_path_event(&s, enable(), now());
        path.on_session_start(&s, now());
        let effects = path.on_limits_changed(&s, now());

        assert_eq!(
            path.state(),
            AcState::WaitingForAuthentication,
            "got {effects:?}"
        );
    }

    #[test]
    fn an_unplug_clears_the_wait_for_energy_the_previous_session_gave_up_on() {
        // Giving up is a decision about one session. Carried over, the next
        // session skips the wait entirely and offers zero amperes at once.
        let mut path = basic();
        let s = session_with_current(0.0);
        path.on_session_start(&s, now());
        budget_reported(&mut path, &s);
        path.on_authorized(&s, now());
        path.on_timer(&s, TIMER_WAIT_FOR_ENERGY, now());
        path.on_bsp(&s, &BspEvent::Cp(CpEvent::A), CpEdges::default(), now());

        path.on_session_start(&s, now());
        let effects = path.on_authorized(&s, now());

        assert_eq!(path.state(), AcState::WaitingForAuthentication);
        assert!(
            arms(&effects, TIMER_WAIT_FOR_ENERGY),
            "the new session waits on its own account, got {effects:?}"
        );
    }

    #[test]
    fn an_unplug_clears_the_high_level_wait_for_energy_the_session_gave_up_on() {
        let mut path = hlc(PwmStart::Nominal);
        let s = session_with_current(0.0);
        path.on_session_start(&s, now());
        budget_reported(&mut path, &s);
        path.on_authorized(&s, now());
        path.on_timer(&s, TIMER_WAIT_FOR_ENERGY, now());
        path.on_bsp(&s, &BspEvent::Cp(CpEvent::A), CpEdges::default(), now());

        path.on_session_start(&s, now());
        let effects = path.on_authorized(&s, now());

        assert_eq!(path.state(), AcState::WaitingForAuthentication);
        assert!(
            arms(&effects, TIMER_WAIT_FOR_ENERGY),
            "the new session waits on its own account, got {effects:?}"
        );
    }

    #[test]
    fn an_unplug_closes_the_fallback_window_so_the_next_session_can_open_one() {
        // The window is armed once per session. Left armed across an unplug the
        // next session never opens one, and the five percent offer it makes
        // then has no deadline that withdraws it.
        let mut path = hlc(PwmStart::FivePercent);
        let s = session_for(PwmStart::FivePercent);
        path.on_session_start(&s, now());
        path.on_path_event(&s, matched(), now());
        let opened = path.on_authorized(&s, now());

        assert!(
            arms(&opened, TIMER_FIVE_PERCENT_FALLBACK),
            "the window must be open for this to mean anything, got {opened:?}"
        );

        let unplugged = path.on_bsp(&s, &BspEvent::Cp(CpEvent::A), CpEdges::default(), now());

        assert!(
            unplugged.contains(&Effect::CancelTimer {
                id: TIMER_FIVE_PERCENT_FALLBACK
            }),
            "got {unplugged:?}"
        );

        path.on_session_start(&s, now());
        path.on_path_event(&s, matched(), now());
        let reopened = path.on_authorized(&s, now());

        assert!(
            arms(&reopened, TIMER_FIVE_PERCENT_FALLBACK),
            "the next session opens a window of its own, got {reopened:?}"
        );
    }

    #[test]
    fn a_control_pilot_event_that_is_not_an_unplug_keeps_the_authorization() {
        // The vehicle sits at state B while the loop waits for a budget.
        // Treating every control pilot event as the end of a session loses the
        // authorization the loop is waiting on, and the budget then arrives to
        // nothing.
        for path in [
            &mut basic() as &mut dyn PowerPath,
            &mut hlc(PwmStart::Nominal),
        ] {
            let starved = session_with_current(0.0);
            path.on_session_start(&starved, now());
            path.on_authorized(&starved, now());
            path.on_bsp(&starved, &BspEvent::Cp(CpEvent::B), CpEdges::default(), now());

            let effects = path.on_limits_changed(&session(), now());

            assert!(
                effects.contains(&Effect::PwmOn(pwm_duty_for_current_a(16.0))),
                "{} lost the authorization to a state B, got {effects:?}",
                path.name()
            );
        }
    }

    #[test]
    fn an_enable_on_a_port_already_in_service_keeps_the_authorization() {
        // Arbitration can resolve to available again without the port ever
        // having left service. That is not the end of a session.
        for path in [
            &mut basic() as &mut dyn PowerPath,
            &mut hlc(PwmStart::Nominal),
        ] {
            let starved = session_with_current(0.0);
            path.on_session_start(&starved, now());
            path.on_authorized(&starved, now());
            path.on_path_event(&starved, enable(), now());

            let effects = path.on_limits_changed(&session(), now());

            assert!(
                effects.contains(&Effect::PwmOn(pwm_duty_for_current_a(16.0))),
                "{} lost the authorization to a redundant enable, got {effects:?}",
                path.name()
            );
        }
    }

    #[test]
    fn the_session_profile_decides_the_offer_and_not_the_port_it_was_built_with() {
        // Production derives both from one setting. Should they ever disagree,
        // the session the port was handed wins, so there is one answer rather
        // than two.
        let mut path = hlc(PwmStart::FivePercent);

        let start = path.on_session_start(&session(), now());

        assert!(!path.five_percent_active());
        assert!(
            !start
                .iter()
                .any(|effect| matches!(effect, Effect::PwmOn(_))),
            "got {start:?}"
        );
    }

    #[test]
    fn a_second_session_still_offers_five_percent() {
        // The offer is per session, `hlc_use_5percent_current_session` in the
        // C++. The first session withdraws it; the second must still make it,
        // or high level communication is never invited again.
        let mut path = hlc(PwmStart::FivePercent);
        let s = session_for(PwmStart::FivePercent);
        path.on_session_start(&s, now());
        path.on_authorized(&s, now());

        assert!(
            !path.five_percent_active(),
            "the first session must withdraw the offer for this to mean anything"
        );

        path.on_bsp(&s, &BspEvent::Cp(CpEvent::A), CpEdges::default(), now());
        let start = path.on_session_start(&s, now());

        assert!(path.five_percent_active());
        assert!(
            start.contains(&Effect::PwmOn(PWM_5_PERCENT)),
            "got {start:?}"
        );
    }

    #[test]
    fn a_second_session_on_a_nominal_port_still_starts_nominal() {
        let mut path = hlc(PwmStart::Nominal);
        let s = session();
        path.on_session_start(&s, now());
        path.on_authorized(&s, now());
        path.on_bsp(&s, &BspEvent::Cp(CpEvent::A), CpEdges::default(), now());

        let start = path.on_session_start(&s, now());

        assert!(!path.five_percent_active());
        assert!(
            !start
                .iter()
                .any(|effect| matches!(effect, Effect::PwmOn(_))),
            "got {start:?}"
        );
    }

    #[test]
    fn an_unplug_clears_the_facts_the_next_start_decision_reads() {
        // The same reset the port applies when it leaves service. A charge loop
        // or a completed match belonging to the previous vehicle would decide
        // the next vehicle's start.
        let mut path = hlc(PwmStart::FivePercent);
        let s = session_for(PwmStart::FivePercent);
        path.on_session_start(&s, now());
        path.on_path_event(&s, matched(), now());
        path.on_path_event(&s, PathEvent::SetupFinished, now());
        path.on_bsp(&s, &BspEvent::Cp(CpEvent::A), CpEdges::default(), now());

        assert_eq!(
            path.facts(),
            hlc(PwmStart::FivePercent).facts(),
            "the unplug is what clears them"
        );
    }

    #[test]
    fn a_fatal_error_leaves_an_out_of_service_basic_port_out_of_service() {
        // A fault says nothing about availability. `Charger.cpp:201-209` leaves
        // the disabled arm on an enable and on nothing else, so a port that
        // faults while out of service is still out of service afterwards.
        let mut path = basic();
        path.on_path_event(&session(), disable(), now());

        let effects = path.to_safe_state();

        assert_eq!(path.state(), AcState::Disabled, "got {effects:?}");
        assert!(
            !effects.contains(&Effect::BspEnable(true)),
            "a fault never restarts the board, got {effects:?}"
        );

        let returning = path.on_path_event(&session(), enable(), now());

        assert_eq!(
            returning,
            vec![Effect::BspEnable(true), Effect::SetCpState(CpState::X1)],
            "the enable is what returns the port to service"
        );
        assert_eq!(path.state(), AcState::Idle);
    }

    #[test]
    fn a_fatal_error_leaves_an_out_of_service_high_level_port_out_of_service() {
        let mut path = hlc(PwmStart::Nominal);
        path.on_path_event(&session(), disable(), now());

        let effects = path.to_safe_state();

        assert_eq!(path.state(), AcState::Disabled, "got {effects:?}");

        path.on_path_event(&session(), enable(), now());

        assert_eq!(path.state(), AcState::Idle);
    }

    /// Every reducer input, driven from a plugged in port and from a disabled
    /// one, so the pairing invariant is checked on transitions no other test
    /// reaches. The disabled setup is what makes the re-enable arm reachable:
    /// `enable()` returns nothing unless a disable is outstanding.
    fn every_transition() -> Vec<(&'static [IecInput], IecInput)> {
        const PLUGGED_IN: &[IecInput] = &[IecInput::CarPluggedIn];
        const DISABLED: &[IecInput] = &[IecInput::CarPluggedIn, IecInput::Disable];
        EVERY_IEC_INPUT
            .into_iter()
            .flat_map(|input| [(PLUGGED_IN, input), (DISABLED, input)])
            .collect()
    }

    const EVERY_IEC_INPUT: [IecInput; 22] = [
        IecInput::Enable,
        IecInput::Disable,
        IecInput::CarPluggedIn,
        IecInput::CarUnplugged,
        IecInput::AuthorizationAccepted,
        IecInput::TransactionStarted,
        IecInput::CarRequestedPower,
        IecInput::CarRequestedVentilatedPower,
        IecInput::CarRequestedStopPower,
        IecInput::CpStateE,
        IecInput::CpStateF,
        IecInput::PowerOn,
        IecInput::PowerOff,
        IecInput::StopRequested,
        IecInput::PauseRequested,
        IecInput::ResumeRequested,
        IecInput::EmergencyShutdown,
        IecInput::ErrorShutdown,
        IecInput::FaultStateFExpired,
        IecInput::CpStateFUnlockTimerExpired,
        IecInput::StoppingChargingTimeoutExpired,
        IecInput::SessionRestart,
    ];

    #[test]
    fn a_board_stop_is_never_emitted_without_a_control_pilot_state_ahead_of_it() {
        // The pairing the C++ comment at `Charger.cpp:1763` describes, checked
        // over every transition rather than over the disabled one alone. The
        // converse does not hold and must not be asserted: an error shutdown
        // signals state F without stopping the board, exactly as the C++ does.
        for (setup, input) in every_transition() {
            let mut path = basic();
            for step in setup {
                path.iec.handle(*step);
            }

            let effects = to_effects(path.iec.handle(input));

            let stop = effects
                .iter()
                .position(|effect| *effect == Effect::BspEnable(false));
            if let Some(stop) = stop {
                let cp = effects
                    .iter()
                    .position(|effect| matches!(effect, Effect::SetCpState(_)));
                assert!(
                    cp.is_some_and(|cp| cp < stop),
                    "{setup:?} then {input:?} stopped the board with no control pilot state ahead of it, got {effects:?}"
                );
            }
        }
    }

    #[test]
    fn no_single_transition_starts_and_stops_the_board_at_once() {
        for (setup, input) in every_transition() {
            let mut path = basic();
            for step in setup {
                path.iec.handle(*step);
            }

            let effects = to_effects(path.iec.handle(input));

            assert!(
                !(effects.contains(&Effect::BspEnable(true))
                    && effects.contains(&Effect::BspEnable(false))),
                "{setup:?} then {input:?} produced both directions, got {effects:?}"
            );
        }
    }
    #[test]
    fn the_startup_transition_leaves_both_paths_idle_and_offering_availability() {
        // Every path starts in `Startup` and the reducer refuses a plug in from
        // it, so this is what makes a port usable at all.
        let mut basic = AcBasic::new(config());
        let mut hlc = AcHlc::new(config(), PwmStart::Nominal);

        let basic_effects = basic.on_startup();
        let hlc_effects = hlc.on_startup();

        assert_eq!(basic.state(), AcState::Idle);
        assert_eq!(hlc.state(), AcState::Idle);
        for effects in [&basic_effects, &hlc_effects] {
            assert_eq!(
                effects.first(),
                Some(&Effect::BspEnable(true)),
                "the board output comes first (`Charger.cpp:95-100`), got {effects:?}"
            );
            assert!(
                effects.contains(&Effect::SetCpState(CpState::X1)),
                "{effects:?}"
            );
            assert!(
                effects.contains(&Effect::AllowPowerOn(false)),
                "{effects:?}"
            );
        }
    }

    #[test]
    fn a_second_startup_transition_changes_nothing() {
        // The single writer publishes ready at most once, but a path must not
        // depend on that to stay out of a second Idle entry mid session.
        let mut path = AcBasic::new(config());
        path.on_startup();
        path.on_bsp(&session(), &BspEvent::Cp(CpEvent::B), CpEdges::default(), now());

        let again = path.on_startup();

        assert!(again.is_empty(), "got {again:?}");
    }

    #[test]
    fn a_plug_in_from_the_startup_state_is_refused() {
        // The reducer guard that made the whole port inert before the startup
        // transition was driven. Named so a regression reads as this and not as
        // a missing session event somewhere above.
        let mut path = AcBasic::new(config());

        let effects = path.on_session_start(&session(), now());

        assert!(effects.is_empty(), "got {effects:?}");
        assert_eq!(path.state(), AcState::Startup);
    }

    /// Session events derived from state edges, and the lifecycle duties
    /// that travel with them.
    mod edge {
        use super::*;

        /// The same drive against both AC paths must derive the same events. This
        /// is the property one shared helper buys and two copies would lose.
        #[test]
        fn both_ac_paths_derive_the_same_duties_from_the_same_drive() {
            let s = session();
            let mut ac_basic = basic();
            let mut ac_hlc = hlc(PwmStart::Nominal);
            let paths: [&mut dyn PowerPath; 2] = [&mut ac_basic, &mut ac_hlc];
            let mut seen = Vec::new();
            for path in paths {
                path.on_session_start(&s, now());
                path.on_authorized(&s, now());
                path.on_bsp(&s, &BspEvent::Cp(CpEvent::C), CpEdges::default(), now());
                path.on_bsp(&s, &BspEvent::Cp(CpEvent::B), s2_opened(), now());
                path.on_bsp(&s, &BspEvent::Cp(CpEvent::A), CpEdges::default(), now());
                seen.push(path.take_session_duties());
            }
            assert_eq!(seen[0], seen[1], "one derivation, shared by both paths");
            assert_eq!(
                seen[0],
                vec![
                    SessionDuty::StartTransaction,
                    SessionDuty::Publish(SessionEvent::PrepareCharging),
                    SessionDuty::Publish(SessionEvent::ChargingStarted),
                    SessionDuty::Publish(SessionEvent::ChargingPausedEv),
                    // The unplug ends a live session, and a session that ends
                    // announces that it is stopping first.
                    // `Charger.cpp:880-884` routes the paused state into
                    // `StoppingCharging`, whose entry publishes.
                    SessionDuty::Publish(SessionEvent::StoppingCharging),
                    SessionDuty::AskVehicleToStop,
                    SessionDuty::StopTransaction,
                    SessionDuty::EndSession,
                ]
            );
        }

        #[test]
        fn a_state_entered_twice_in_a_row_derives_one_event() {
            let mut path = basic();
            let s = session();
            path.on_session_start(&s, now());
            path.on_authorized(&s, now());
            path.on_bsp(&s, &BspEvent::Cp(CpEvent::C), CpEdges::default(), now());
            assert_eq!(
                path.take_session_duties(),
                vec![
                    SessionDuty::StartTransaction,
                    SessionDuty::Publish(SessionEvent::PrepareCharging),
                    SessionDuty::Publish(SessionEvent::ChargingStarted),
                ]
            );

            // A second reading of the same control pilot state. The reducer stays
            // in `Charging`, so no entry happened and nothing is due.
            path.on_bsp(&s, &BspEvent::Cp(CpEvent::C), CpEdges::default(), now());
            assert_eq!(path.state(), AcState::Charging);
            assert_eq!(path.take_session_duties(), Vec::new());
        }

        #[test]
        fn a_pause_and_a_resume_within_one_charge_derive_each_edge_once() {
            let mut path = basic();
            let s = session();
            path.on_session_start(&s, now());
            path.on_authorized(&s, now());
            path.on_bsp(&s, &BspEvent::Cp(CpEvent::C), CpEdges::default(), now());
            path.take_session_duties();

            path.on_path_event(&s, PathEvent::PauseRequested, now());
            path.on_path_event(&s, PathEvent::ResumeRequested, now());

            // The pause out of `Charging` takes the stop route to reach the
            // paused state (`Charger::run_state_machine`'s `Charging` arm), so
            // it is two edges: the stopping entry, which announces itself and
            // asks the vehicle, and the paused state the relays being open
            // settles it into. The relays are open throughout here, because
            // nothing reported them closed, so both fall in one pass. The
            // resume then re-enters
            // `PrepareCharging`, which is where the `ChargingPausedEVSE` arm
            // sends a pause whose reasons have all gone. There is no separate
            // resumed event on either side.
            assert_eq!(
                path.take_session_duties(),
                vec![
                    SessionDuty::Publish(SessionEvent::StoppingCharging),
                    SessionDuty::AskVehicleToStop,
                    SessionDuty::Publish(SessionEvent::ChargingPausedEvse),
                    SessionDuty::Publish(SessionEvent::PrepareCharging),
                ]
            );
        }

        /// An EV side resume announces the preparation before the charge.
        ///
        /// `Charger::process_cp_events_state`'s `ChargingPausedEV` arm answers
        /// a fresh control pilot state C by assigning `PrepareCharging` rather
        /// than `Charging`, and `Charger::run_state_machine`'s
        /// `PrepareCharging` arm then carries it on to `Charging` in the same
        /// loop. So a C++ deployment emits both events on every EV side
        /// resume, and anything counting session events, OCPP included, sees
        /// that pair.
        ///
        /// An arriving vehicle rests at state B, and resting is not pausing.
        ///
        /// `IECStateMachine::state_machine` (`IECStateMachine.cpp:198-204`)
        /// withdraws power only out of C or D, so the plug in's own pilot
        /// reading reaches the reducer as the lock
        /// assertion alone. The port used to read it as `CarRequestedStopPower`
        /// as well, and with the plug in reaching `PrepareCharging` in the same
        /// pass the vehicle announced `ChargingPausedEv` before it had been
        /// offered any power. Driven against both AC paths because
        /// `cp_to_input` is shared and either could regrow the unconditional
        /// mapping.
        #[test]
        fn an_arriving_vehicle_resting_at_state_b_announces_no_pause() {
            let s = session();
            let mut ac_basic = basic();
            let mut ac_hlc = hlc(PwmStart::Nominal);
            let paths: [&mut dyn PowerPath; 2] = [&mut ac_basic, &mut ac_hlc];
            for path in paths {
                path.on_session_start(&s, now());
                path.on_authorized(&s, now());
                path.take_session_duties();

                // The same reading the writer already read as the arrival.
                let resting = path.on_bsp(&s, &BspEvent::Cp(CpEvent::B), CpEdges::default(), now());

                assert_eq!(path.state(), AcState::PrepareCharging);
                assert_eq!(path.take_session_duties(), Vec::new());
                // The lock assertion every state B makes survives the guard:
                // `IECStateMachine::state_machine` `:189-193` is outside it,
                // and an E or F
                // excursion releases a connector this reading must reclaim.
                // `config()` locks in state B, so that is the direction owed.
                assert!(
                    resting.contains(&Effect::LockConnector),
                    "the state B lock assertion is owed whatever preceded it, got {resting:?}"
                );
            }
        }

        /// The destination is the same either way, so this asserts the ordered
        /// sequence rather than the state: the divergence was the missing
        /// `PrepareCharging`, not where the session ended up.
        #[test]
        fn a_resume_by_the_car_announces_the_preparation_before_the_charge() {
            let s = session();
            let mut ac_basic = basic();
            let mut ac_hlc = hlc(PwmStart::Nominal);
            let paths: [&mut dyn PowerPath; 2] = [&mut ac_basic, &mut ac_hlc];
            let mut seen = Vec::new();
            for path in paths {
                path.on_session_start(&s, now());
                path.on_authorized(&s, now());
                path.on_bsp(&s, &BspEvent::Cp(CpEvent::C), CpEdges::default(), now());
                path.take_session_duties();

                // The vehicle opens S2 and closes it again. The offer stayed
                // up across the pause, which is what lets the resume happen
                // with no fresh handshake.
                path.on_bsp(&s, &BspEvent::Cp(CpEvent::B), s2_opened(), now());
                path.on_bsp(&s, &BspEvent::Cp(CpEvent::C), CpEdges::default(), now());

                assert_eq!(path.state(), AcState::Charging);
                seen.push(path.take_session_duties());
            }
            assert_eq!(seen[0], seen[1], "one derivation, shared by both paths");
            assert_eq!(
                seen[0],
                vec![
                    SessionDuty::Publish(SessionEvent::ChargingPausedEv),
                    SessionDuty::Publish(SessionEvent::PrepareCharging),
                    SessionDuty::Publish(SessionEvent::ChargingStarted),
                ]
            );
        }

        /// The EVSE side resume, driven for the same ordered sequence.
        ///
        /// The asymmetry this pins is the point: `ChargingPausedEVSE` already
        /// reached `Charging` through `PrepareCharging`, because
        /// `Iec::resume_requested` restores the offer and leaves the state
        /// there for a control pilot state C to finish. Both paused states now
        /// emit the same pair, which is what
        /// `Charger::run_state_machine`'s `PrepareCharging` arm gives a C++
        /// deployment from either of them.
        #[test]
        fn a_resume_by_the_charger_announces_the_preparation_before_the_charge() {
            let mut path = basic();
            let s = session();
            path.on_session_start(&s, now());
            path.on_authorized(&s, now());
            path.on_bsp(&s, &BspEvent::Cp(CpEvent::C), CpEdges::default(), now());
            path.take_session_duties();

            path.on_path_event(&s, PathEvent::PauseRequested, now());
            assert_eq!(path.state(), AcState::ChargingPausedEvse);
            path.take_session_duties();

            path.on_path_event(&s, PathEvent::ResumeRequested, now());
            // The offer is back up but the relays are open, so the session
            // waits in the preparation for the vehicle to close S2 again.
            assert_eq!(path.state(), AcState::PrepareCharging);
            path.on_bsp(&s, &BspEvent::Cp(CpEvent::C), CpEdges::default(), now());

            assert_eq!(path.state(), AcState::Charging);
            assert_eq!(
                path.take_session_duties(),
                vec![
                    SessionDuty::Publish(SessionEvent::PrepareCharging),
                    SessionDuty::Publish(SessionEvent::ChargingStarted),
                ]
            );
        }

        #[test]
        fn the_relays_opening_after_a_stop_closes_the_billing_record() {
            let mut path = basic();
            let s = session();
            path.on_session_start(&s, now());
            path.on_authorized(&s, now());
            path.on_bsp(&s, &BspEvent::Cp(CpEvent::C), CpEdges::default(), now());
            path.on_stop(&s, StopReason::Local, now());
            path.take_session_duties();

            path.on_bsp(&s, &BspEvent::Cp(CpEvent::PowerOff), CpEdges::default(), now());

            assert_eq!(path.state(), AcState::Finished);
            assert_eq!(
                path.take_session_duties(),
                vec![SessionDuty::StopTransaction]
            );
        }

        #[test]
        fn an_unplug_while_stopping_ends_the_session_and_closes_the_record() {
            let mut path = basic();
            let s = session();
            path.on_session_start(&s, now());
            path.on_authorized(&s, now());
            path.on_bsp(&s, &BspEvent::Cp(CpEvent::C), CpEdges::default(), now());
            path.on_stop(&s, StopReason::Local, now());
            assert_eq!(path.state(), AcState::StoppingCharging);
            path.take_session_duties();

            path.on_bsp(&s, &BspEvent::Cp(CpEvent::A), CpEdges::default(), now());

            assert_eq!(path.state(), AcState::Idle);
            assert_eq!(
                path.take_session_duties(),
                vec![SessionDuty::StopTransaction, SessionDuty::EndSession]
            );
        }

        /// A disable reaches a resting state from a live session too, so it owes
        /// the same duties. `Charger.cpp:1080-1084` ends the session on the way
        /// into `Disabled` as much as on the way into `Idle`.
        #[test]
        fn a_disable_during_charging_ends_the_session_and_closes_the_record() {
            let mut path = hlc(PwmStart::Nominal);
            let s = session();
            path.on_session_start(&s, now());
            path.on_authorized(&s, now());
            path.on_bsp(&s, &BspEvent::Cp(CpEvent::C), CpEdges::default(), now());
            path.take_session_duties();

            path.on_path_event(&s, PathEvent::Disable, now());

            assert_eq!(path.state(), AcState::Disabled);
            assert_eq!(
                path.take_session_duties(),
                vec![SessionDuty::StopTransaction, SessionDuty::EndSession]
            );
        }

        /// The same fact from either pause. `Iec::disable` reaches the resting
        /// state from any live session directly rather than through
        /// `StoppingCharging`, so a paused session owes the close on this edge
        /// and on no other.
        ///
        /// Driven here rather than through `Core`, which cannot produce this
        /// edge: a disable arriving with a live session is answered by a stop
        /// first (`Core::apply_enable_disable`), so the route through the
        /// coordinator is the paused state to `StoppingCharging` to `Disabled`.
        /// `PathEvent::Disable` reaching a paused path is still an input this
        /// trait must answer, and the existing charging case above is driven
        /// the same way.
        #[test]
        fn a_disable_during_either_pause_ends_the_session_and_closes_the_record() {
            for (name, reach_pause) in [
                (
                    "ev",
                    &(|path: &mut AcHlc, s: &Session| {
                        path.on_bsp(s, &BspEvent::Cp(CpEvent::B), s2_opened(), now());
                    }) as &dyn Fn(&mut AcHlc, &Session),
                ),
                (
                    "evse",
                    &(|path: &mut AcHlc, s: &Session| {
                        path.on_path_event(s, PathEvent::PauseRequested, now());
                    }) as &dyn Fn(&mut AcHlc, &Session),
                ),
            ] {
                let mut path = hlc(PwmStart::Nominal);
                let s = session();
                path.on_session_start(&s, now());
                path.on_authorized(&s, now());
                path.on_bsp(&s, &BspEvent::Cp(CpEvent::C), CpEdges::default(), now());
                reach_pause(&mut path, &s);
                assert!(
                    matches!(
                        path.state(),
                        AcState::ChargingPausedEv | AcState::ChargingPausedEvse
                    ),
                    "{name}: got {:?}",
                    path.state()
                );
                path.take_session_duties();

                path.on_path_event(&s, PathEvent::Disable, now());

                assert_eq!(path.state(), AcState::Disabled, "{name}");
                assert_eq!(
                    path.take_session_duties(),
                    vec![SessionDuty::StopTransaction, SessionDuty::EndSession],
                    "{name}"
                );
            }
        }

        /// The port coming up is not the end of a session.
        #[test]
        fn the_startup_entry_into_idle_owes_nothing() {
            let mut path = AcBasic::new(config());
            path.on_startup();
            assert_eq!(path.state(), AcState::Idle);
            assert_eq!(path.take_session_duties(), Vec::new());
        }

        /// Draining is what makes a duty impossible to discharge twice.
        #[test]
        fn duties_are_reported_once() {
            let mut path = basic();
            let s = session();
            path.on_session_start(&s, now());
            path.on_authorized(&s, now());
            assert_eq!(
                path.take_session_duties(),
                vec![
                    SessionDuty::StartTransaction,
                    SessionDuty::Publish(SessionEvent::PrepareCharging)
                ]
            );
            assert_eq!(path.take_session_duties(), Vec::new());
        }

        /// The five percent start reaches `PrepareCharging` from `proceed()`
        /// at the end of the pilot detour rather than from the authorization
        /// itself, which is a different call site into the same helper.
        #[test]
        fn a_five_percent_start_derives_its_prepare_charging_from_the_detour() {
            let mut path = hlc(PwmStart::FivePercent);
            let s = session_for(PwmStart::FivePercent);
            path.on_session_start(&s, now());
            path.on_authorized(&s, now());
            assert_eq!(path.state(), AcState::WaitingForAuthentication);
            assert_eq!(
                path.take_session_duties(),
                Vec::new(),
                "the detour enters no publishable state"
            );

            path.on_timer(&s, TIMER_T_STEP, now());
            path.on_timer(&s, TIMER_T_STEP, now());

            assert_eq!(path.state(), AcState::PrepareCharging);
            assert_eq!(
                path.take_session_duties(),
                vec![
                    SessionDuty::StartTransaction,
                    SessionDuty::Publish(SessionEvent::PrepareCharging)
                ]
            );
        }

        /// A power request held back by the contactor gate reaches the reducer
        /// from `PathEvent::AllowCloseContactor`.
        #[test]
        fn a_withheld_power_request_derives_its_charging_started_when_released() {
            let mut path = hlc(PwmStart::FivePercent);
            let s = session_for(PwmStart::FivePercent);
            path.on_session_start(&s, now());
            path.on_path_event(&s, matched(), now());
            path.on_path_event(&s, PathEvent::SetupFinished, now());
            path.on_authorized(&s, now());
            assert_eq!(path.state(), AcState::PrepareCharging);
            path.take_session_duties();

            // Only the IEC half of the permission. The request is held.
            path.on_bsp(&s, &BspEvent::Cp(CpEvent::C), CpEdges::default(), now());
            assert_eq!(path.state(), AcState::PrepareCharging);
            assert_eq!(path.take_session_duties(), Vec::new());

            path.on_path_event(&s, PathEvent::AllowCloseContactor(true), now());

            assert_eq!(path.state(), AcState::Charging);
            assert_eq!(
                path.take_session_duties(),
                vec![SessionDuty::Publish(SessionEvent::ChargingStarted)]
            );
        }

        /// A second charge in the same process crosses every edge again.
        #[test]
        fn a_second_charge_derives_the_same_events_as_the_first() {
            let mut path = basic();
            let s = session();
            let charge = |path: &mut AcBasic| {
                path.on_session_start(&s, now());
                path.on_authorized(&s, now());
                path.on_bsp(&s, &BspEvent::Cp(CpEvent::C), CpEdges::default(), now());
                path.on_bsp(&s, &BspEvent::Cp(CpEvent::A), CpEdges::default(), now());
                path.take_session_duties()
            };
            let first = charge(&mut path);
            let second = charge(&mut path);
            assert_eq!(first, second, "the port does not tire");
            assert_eq!(
                first,
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

        /// An unplug during a charge is a stop, and a stop is announced.
        /// `Charger.cpp:782-792` routes `Charging` to `StoppingCharging` on the
        /// plug flag rather than to a resting state, and the
        /// `StoppingCharging` entry at `Charger.cpp:1012` is what publishes.
        #[test]
        fn an_unplug_during_a_charge_announces_the_stop_before_it_ends_the_session() {
            let mut path = basic();
            let s = session();
            path.on_session_start(&s, now());
            path.on_authorized(&s, now());
            path.on_bsp(&s, &BspEvent::Cp(CpEvent::C), CpEdges::default(), now());
            assert_eq!(path.state(), AcState::Charging);
            path.take_session_duties();

            path.on_bsp(&s, &BspEvent::Cp(CpEvent::A), CpEdges::default(), now());

            assert_eq!(path.state(), AcState::Idle);
            assert_eq!(
                path.take_session_duties(),
                vec![
                    SessionDuty::Publish(SessionEvent::StoppingCharging),
                    SessionDuty::AskVehicleToStop,
                    SessionDuty::StopTransaction,
                    SessionDuty::EndSession,
                ]
            );
        }

        /// Every state the unplug crosses owes its own entry, so the edge
        /// observation has to see each one rather than the first and the last.
        #[test]
        fn an_unplug_that_crosses_three_states_reports_all_three_edges() {
            let mut path = basic();
            let s = session();
            path.on_session_start(&s, now());
            path.on_authorized(&s, now());
            path.on_bsp(&s, &BspEvent::Cp(CpEvent::C), CpEdges::default(), now());
            path.take_session_duties();

            // One call, three transitions: Charging, StoppingCharging,
            // Finished, Idle.
            path.on_bsp(&s, &BspEvent::Cp(CpEvent::A), CpEdges::default(), now());

            let duties = path.take_session_duties();
            assert!(
                duties.contains(&SessionDuty::Publish(SessionEvent::StoppingCharging)),
                "the intermediate state was swallowed, got {duties:?}"
            );
            assert!(
                duties.contains(&SessionDuty::EndSession),
                "the last state was swallowed, got {duties:?}"
            );
        }

        /// An unplug that arrives before the vehicle ever drew power still has
        /// a record open, because the authorization opened one.
        #[test]
        fn an_unplug_while_preparing_closes_the_record() {
            let mut path = basic();
            let s = session();
            path.on_session_start(&s, now());
            path.on_authorized(&s, now());
            assert_eq!(path.state(), AcState::PrepareCharging);
            path.take_session_duties();

            path.on_bsp(&s, &BspEvent::Cp(CpEvent::A), CpEdges::default(), now());

            assert_eq!(
                path.take_session_duties(),
                vec![
                    // `Charger.cpp:690-695`: charging had been initialized, so
                    // the stop runs through `StoppingCharging` rather than
                    // going straight to rest.
                    SessionDuty::Publish(SessionEvent::StoppingCharging),
                    SessionDuty::AskVehicleToStop,
                    SessionDuty::StopTransaction,
                    SessionDuty::EndSession,
                ]
            );
        }
    }

    /// The high level communication facts the C++ receives on its own
    /// subscriptions, and what each one costs the authorization loop.
    mod high_level_communication_facts {
        use super::*;
        use crate::core::hlc::DataLinkRequest;

        fn allow(allow: bool) -> PathEvent {
            PathEvent::AllowCloseContactor(allow)
        }

        /// A five percent port with authorization granted, matching started
        /// and the vehicle already asking for power over ISO 15118, which is
        /// the state a real AC HLC session reaches before the relays close.
        fn preparing_five_percent() -> (AcHlc, Session) {
            let mut path = hlc(PwmStart::FivePercent);
            let s = session_for(PwmStart::FivePercent);
            path.on_session_start(&s, now());
            path.on_path_event(&s, PathEvent::MatchingStarted(true), now());
            path.on_path_event(&s, PathEvent::SetupFinished, now());
            path.on_authorized(&s, now());
            assert_eq!(path.state(), AcState::PrepareCharging);
            (path, s)
        }

        /// A charging five percent session with the relays closed, which is
        /// the state the stop signalling decision is taken from.
        fn charging_five_percent() -> (AcHlc, Session) {
            let (mut path, s) = preparing_five_percent();
            path.on_path_event(&s, allow(true), now());
            path.on_bsp(&s, &BspEvent::Cp(CpEvent::C), CpEdges::default(), now());
            assert_eq!(path.state(), AcState::Charging);
            (path, s)
        }

        fn hlc_updates(effects: &[Effect]) -> Vec<HlcUpdate> {
            effects
                .iter()
                .filter_map(|effect| match effect {
                    Effect::HlcUpdate(update) => Some(update.clone()),
                    _ => None,
                })
                .collect()
        }

        /// What this path owes the core after the drive above it.
        ///
        /// The stop request itself is the core's to send: this path's share of
        /// `Charger.cpp:1013-1023` is raising the duty on the entry and leaving
        /// the pilot offer standing, and both halves are asserted here. What
        /// the duty becomes on the wire is pinned in `core::tests`, where the
        /// gates it reads live.
        fn stop_duties(path: &mut AcHlc) -> Vec<SessionDuty> {
            path.take_session_duties()
        }

        fn owes_a_stop_request(path: &mut AcHlc) -> bool {
            stop_duties(path).contains(&SessionDuty::AskVehicleToStop)
        }

        /// `Charger.cpp:1013-1023`: entering `StoppingCharging` asks the
        /// vehicle to end the session over ISO 15118 **instead of** dropping
        /// the pilot to X1. The X1 is the `else` branch, so a live high level
        /// session must not see it: the drop would kill the ISO session before
        /// the vehicle could act on the request.
        #[test]
        fn a_live_high_level_session_is_asked_to_stop_rather_than_dropped_to_x1() {
            let (mut path, s) = charging_five_percent();

            let effects = path.on_stop(&s, StopReason::Local, now());

            assert!(
                owes_a_stop_request(&mut path),
                "the vehicle must be asked to end the session, got {effects:?}"
            );
            assert!(
                !effects.contains(&Effect::SetCpState(CpState::X1)),
                "the pilot drop is the branch not taken, got {effects:?}"
            );
            // `cp_state_X1()` is one action in the C++ (`Charger.cpp:1258-1265`):
            // it clears `pwm_running` and signals the pilot in the same call. The
            // reducer splits it into two commands, so a filter that removes only
            // the pilot half still takes the duty cycle to zero, the vehicle
            // leaves X2 and the data link dies before the request lands.
            assert!(
                !effects.contains(&Effect::PwmOff),
                "the duty cycle is the other half of the same branch, got {effects:?}"
            );
        }

        /// An unplug from a live high level session owes the same request as
        /// any other stop.
        ///
        /// `Charger::process_cp_events_independent` (`Charger.cpp:1202-1203`)
        /// clears `flag_ev_plugged_in` and nothing else, and the `Charging` arm
        /// then reaches `StoppingCharging` (`:880-885`) with
        /// `hlc_charging_active` still standing, so the entry at `:1014` takes
        /// the request branch. The fact is cleared later, at the `Idle` entry
        /// (`:220`), which the state machine reaches through `Finished`.
        ///
        /// Clearing it on the way in instead makes the entry read `false` and
        /// the request is never owed.
        #[test]
        fn an_unplug_from_a_live_high_level_session_still_asks_the_vehicle_to_stop() {
            let (mut path, s) = charging_five_percent();

            path.on_bsp(&s, &BspEvent::Cp(CpEvent::A), CpEdges::default(), now());

            assert!(owes_a_stop_request(&mut path));
        }

        /// The converse: the session state is still torn down by the unplug, so
        /// the next vehicle inherits nothing. A second stop on the same edge
        /// would mean the fact survived.
        #[test]
        fn an_unplug_still_ends_the_high_level_session() {
            let (mut path, s) = charging_five_percent();
            path.on_bsp(&s, &BspEvent::Cp(CpEvent::A), CpEdges::default(), now());
            stop_duties(&mut path);

            path.on_stop(&s, StopReason::Local, now());

            assert!(!owes_a_stop_request(&mut path));
        }

        /// The C++ discharges the entry's duties inside `if (initialize_state)`
        /// (`Charger.cpp:1008`), so the request belongs to the edge and not to
        /// the state. A vehicle that has already been asked once and is being
        /// waited on must not be asked again on every unrelated event.
        #[test]
        fn the_stop_request_belongs_to_the_edge_and_not_to_the_state() {
            let (mut path, s) = charging_five_percent();
            path.on_stop(&s, StopReason::Local, now());
            assert_eq!(path.state(), AcState::StoppingCharging);
            assert!(owes_a_stop_request(&mut path), "the entry owes it once");

            path.on_limits_changed(&s, now());

            assert!(!owes_a_stop_request(&mut path), "and not again");
        }

        /// The same entry without a live high level session takes the `else`
        /// branch, which is the X1 the basic port has always signalled.
        #[test]
        fn a_session_the_vehicle_never_took_over_still_drops_to_x1() {
            let mut path = hlc(PwmStart::Nominal);
            let s = session_for(PwmStart::Nominal);
            path.on_session_start(&s, now());
            path.on_authorized(&s, now());
            path.on_bsp(&s, &BspEvent::Cp(CpEvent::C), CpEdges::default(), now());

            let effects = path.on_stop(&s, StopReason::Local, now());

            assert!(
                hlc_updates(&effects).is_empty(),
                "no ISO session to ask, got {effects:?}"
            );
            assert!(
                !path.hlc_charging_active(),
                "which is the gate the core reads, not the duty"
            );
            assert!(
                effects.contains(&Effect::SetCpState(CpState::X1)),
                "got {effects:?}"
            );
        }

        /// The reason an external stop carries is not this path's to name.
        /// `Charger::cancel_transaction` (`Charger.cpp:1355-1363`) sends it from
        /// outside the state machine, under `flag_transaction_active` and
        /// `hlc_charging_active`, neither of which a path owns; `core::tests`
        /// pins what it sends. What is pinned here is that no reason reaches
        /// the vehicle from the path, so the send cannot be issued twice.
        #[test]
        fn no_stop_reason_is_named_to_the_vehicle_from_the_path() {
            for reason in [StopReason::EmergencyStop, StopReason::PowerLoss] {
                let (mut path, s) = charging_five_percent();

                let effects = path.on_stop(&s, reason, now());

                assert!(
                    !hlc_updates(&effects)
                        .iter()
                        .any(|update| matches!(update, HlcUpdate::SendError(_))),
                    "{reason:?}: got {effects:?}"
                );
            }
        }

        /// The stop decision reads `hlc_charging_active` and nothing about the
        /// offer, so a nominal duty session the vehicle took over gets the same
        /// answer as a five percent one. This is figure 6 of ISO 15118-3, an X1
        /// start with the vehicle running ISO on top of a nominal duty cycle,
        /// and it is the combination the five percent helper above never
        /// reaches.
        #[test]
        fn a_nominal_duty_session_the_vehicle_took_over_is_asked_to_stop_too() {
            let mut path = hlc(PwmStart::Nominal);
            let s = session_for(PwmStart::Nominal);
            path.on_session_start(&s, now());
            path.on_path_event(&s, PathEvent::SetupFinished, now());
            path.on_authorized(&s, now());
            path.on_bsp(&s, &BspEvent::Cp(CpEvent::C), CpEdges::default(), now());

            let effects = path.on_stop(&s, StopReason::Local, now());

            assert!(owes_a_stop_request(&mut path), "got {effects:?}");
            assert!(
                path.hlc_charging_active(),
                "which is the gate the core reads"
            );
            assert!(
                !effects.contains(&Effect::SetCpState(CpState::X1)),
                "got {effects:?}"
            );
            assert!(!effects.contains(&Effect::PwmOff), "got {effects:?}");
        }

        /// `Charger.cpp:934-938`: the pause entry drops the pilot only when
        /// `not hlc_charging_active or not flag_transaction_active`. A pause is
        /// reachable in this reducer only from a live charging session, so the
        /// transaction flag is already true and the high level session is the
        /// whole of the gate.
        ///
        /// Without it an OCPP pause of an AC port inside the ISO charge loop
        /// withdraws the offer, the link dies, and the resume re-offers a duty
        /// cycle to a vehicle whose session is gone.
        #[test]
        fn pausing_a_live_high_level_session_leaves_the_offer_standing() {
            let (mut path, s) = charging_five_percent();

            let effects = path.on_path_event(&s, PathEvent::PauseRequested, now());

            assert_eq!(path.state(), AcState::ChargingPausedEvse);
            assert!(
                !effects.contains(&Effect::SetCpState(CpState::X1)),
                "got {effects:?}"
            );
            assert!(!effects.contains(&Effect::PwmOff), "got {effects:?}");
            assert!(
                effects.contains(&Effect::AllowPowerOn(false)),
                "the energy still goes away, got {effects:?}"
            );
        }

        /// A resume out of a paused five percent session restores the five
        /// percent offer, not the nominal duty the reducer computes from the
        /// current limit: the shaping is what keeps a five percent session at
        /// five percent across a resume.
        ///
        /// The relay half is driven on the window it survives in. A pause out
        /// of `Charging` crosses the stopping entry, whose arm waits there for
        /// the relays, so that route reaches the paused state with them
        /// confirmed open. The vehicle here never left state C, so its request
        /// still stands and the resume re-offers the pilot **and** allows power
        /// again, which is what `Charger.cpp:763-767` does with the same latch.
        /// Before the latch this asserted the opposite, on the relays being
        /// open, and the vehicle was left waiting for a pilot edge that was
        /// never coming.
        #[test]
        fn a_resume_restores_the_five_percent_offer_it_withdrew() {
            let (mut path, s) = charging_five_percent();
            path.on_bsp(&s, &BspEvent::Cp(CpEvent::PowerOn), CpEdges::default(), now());
            path.on_path_event(&s, PathEvent::PauseRequested, now());
            assert_eq!(path.state(), AcState::StoppingCharging);
            path.on_bsp(&s, &BspEvent::Cp(CpEvent::PowerOff), CpEdges::default(), now());
            assert_eq!(path.state(), AcState::ChargingPausedEvse);

            let resumed = path.on_path_event(&s, PathEvent::ResumeRequested, now());

            assert!(
                resumed.contains(&Effect::PwmOn(PWM_5_PERCENT)),
                "the five percent offer is restored, got {resumed:?}"
            );
            assert!(
                resumed.contains(&Effect::AllowPowerOn(true)),
                "the vehicle never withdrew its request, got {resumed:?}"
            );
        }

        /// The two fixes composed: a pause leaves the high level session
        /// standing, so an unplug out of the paused state still owes the
        /// vehicle a stop request. `hlc_charging_active` survives the pause
        /// (`Charger.cpp:1331-1336` touches one flag and nothing else) and is
        /// cleared only at the `Idle` entry the unplug reaches last.
        #[test]
        fn an_unplug_from_a_paused_high_level_session_still_asks_the_vehicle_to_stop() {
            let (mut path, s) = charging_five_percent();
            path.on_path_event(&s, PathEvent::PauseRequested, now());
            stop_duties(&mut path);

            path.on_bsp(&s, &BspEvent::Cp(CpEvent::A), CpEdges::default(), now());

            assert!(owes_a_stop_request(&mut path));
        }

        /// The negative half: without a high level session the pause takes the
        /// branch the basic port has always taken.
        #[test]
        fn pausing_a_session_the_vehicle_never_took_over_still_drops_to_x1() {
            let mut path = hlc(PwmStart::Nominal);
            let s = session_for(PwmStart::Nominal);
            path.on_session_start(&s, now());
            path.on_authorized(&s, now());
            path.on_bsp(&s, &BspEvent::Cp(CpEvent::C), CpEdges::default(), now());

            let effects = path.on_path_event(&s, PathEvent::PauseRequested, now());

            assert_eq!(path.state(), AcState::ChargingPausedEvse);
            assert!(
                effects.contains(&Effect::SetCpState(CpState::X1)),
                "got {effects:?}"
            );
        }

        /// `Charger::error_shutdown` (`Charger.cpp:2277`) states it outright:
        /// "we keep the PWM on. This allows us to keep the HLC session active
        /// and send the error to the EV". Neither shutdown calls `cp_state_F`,
        /// and the only state F the C++ signals under a fault is the one in
        /// `ChargingPausedEVSE` (`Charger.cpp:949`), itself gated on
        /// `not hlc_charging_active`.
        #[test]
        fn driving_a_live_high_level_session_to_safe_state_leaves_the_offer_standing() {
            let (mut path, _s) = charging_five_percent();

            let effects = path.to_safe_state();

            assert!(
                !effects.contains(&Effect::SetCpState(CpState::F)),
                "got {effects:?}"
            );
            assert!(!effects.contains(&Effect::PwmOff), "got {effects:?}");
            assert!(
                effects.contains(&Effect::AllowPowerOn(false)),
                "the energy still goes away, got {effects:?}"
            );
        }

        /// `Charger.cpp:1357-1358` nests the reason test inside
        /// `if (hlc_charging_active)`, so a session the vehicle never took over
        /// has nobody to tell: the ISO stack is not carrying it.
        #[test]
        fn an_emergency_on_a_session_the_vehicle_never_took_over_names_nothing() {
            let mut path = hlc(PwmStart::Nominal);
            let s = session_for(PwmStart::Nominal);
            path.on_session_start(&s, now());
            path.on_authorized(&s, now());
            path.on_bsp(&s, &BspEvent::Cp(CpEvent::C), CpEdges::default(), now());

            let effects = path.on_stop(&s, StopReason::EmergencyStop, now());

            assert!(
                !hlc_updates(&effects)
                    .iter()
                    .any(|update| matches!(update, HlcUpdate::SendError(_))),
                "got {effects:?}"
            );
        }

        /// Every other reason carries no error. `Charger.cpp:1359-1362` names
        /// two and nothing else, so a local stop tells the vehicle to stop and
        /// says nothing about why.
        #[test]
        fn an_ordinary_stop_names_no_error() {
            let (mut path, s) = charging_five_percent();

            let effects = path.on_stop(&s, StopReason::Local, now());

            assert!(
                !hlc_updates(&effects)
                    .iter()
                    .any(|update| matches!(update, HlcUpdate::SendError(_))),
                "got {effects:?}"
            );
        }

        /// `signal_hlc_no_energy_available` has exactly two call sites, and
        /// both nest inside `if (config_context.charge_mode == ChargeMode::DC)`:
        /// the authorization loop's bounded wait (`Charger.cpp:346-362`) and the
        /// `PrepareCharging` entry (`:679-685`). An AC port therefore never asks
        /// the vehicle to pause, however its deployment is configured.
        ///
        /// The wait itself is not a DC fact and stays: it still bounds how long
        /// the loop holds for a budget before proceeding.
        #[test]
        fn giving_up_on_energy_tells_an_ac_vehicle_nothing() {
            let mut path = hlc(PwmStart::Nominal);
            let s = session_without_energy();
            path.on_session_start(&s, now());
            path.on_authorized(&s, now());

            let effects = path.on_timer(&s, TIMER_WAIT_FOR_ENERGY, now());

            assert!(
                hlc_updates(&effects).is_empty(),
                "the pause is a DC only signal, got {effects:?}"
            );
        }

        #[test]
        fn the_contactor_permission_arrives_as_an_event_and_releases_a_held_request() {
            let (mut path, s) = preparing_five_percent();

            // Only the IEC half of the permission, so the request is held.
            path.on_bsp(&s, &BspEvent::Cp(CpEvent::C), CpEdges::default(), now());
            assert_eq!(path.state(), AcState::PrepareCharging);

            let effects = path.on_path_event(&s, allow(true), now());

            assert_eq!(path.state(), AcState::Charging);
            assert!(
                effects.contains(&Effect::AllowPowerOn(true)),
                "the released request must energize, got {effects:?}"
            );
        }

        #[test]
        fn a_permission_granted_and_then_revoked_shuts_the_gate_again() {
            let (mut path, s) = preparing_five_percent();
            path.on_path_event(&s, allow(true), now());
            path.on_path_event(&s, allow(false), now());

            // `Charger.cpp:724-726`: a five percent session needs both halves,
            // so a withdrawn permission holds the request the way an absent
            // one does.
            let effects = path.on_bsp(&s, &BspEvent::Cp(CpEvent::C), CpEdges::default(), now());

            assert_eq!(path.state(), AcState::PrepareCharging);
            assert!(
                effects.contains(&Effect::AllowPowerOn(false)),
                "a held request answers the vehicle with a refusal, got {effects:?}"
            );
        }

        #[test]
        fn a_permission_arriving_twice_releases_the_request_once() {
            let (mut path, s) = preparing_five_percent();
            path.on_bsp(&s, &BspEvent::Cp(CpEvent::C), CpEdges::default(), now());

            let first = path.on_path_event(&s, allow(true), now());
            let again = path.on_path_event(&s, allow(true), now());

            assert!(first.contains(&Effect::AllowPowerOn(true)));
            assert_eq!(
                again,
                Vec::new(),
                "the request was already released, got {again:?}"
            );
        }

        #[test]
        fn the_power_delivery_request_is_what_keeps_the_five_percent_offer_up() {
            // `Charger.cpp:452-458`: matching started before authorization
            // arrived, and the vehicle has already asked for power, so the
            // offer stays rather than dropping to X1 and killing the session.
            let (path, _s) = preparing_five_percent();
            assert!(
                path.five_percent_active(),
                "the offer must survive the authorization"
            );
        }

        #[test]
        fn a_current_demand_start_does_not_stand_in_for_a_power_delivery_request() {
            // `Charger::notify_currentdemand_started` (`Charger.cpp:2024-2030`)
            // moves the state and touches `hlc_charging_active` not at all, so
            // a DC charge loop start cannot make an AC port keep its offer.
            let mut path = hlc(PwmStart::FivePercent);
            let s = session_for(PwmStart::FivePercent);
            path.on_session_start(&s, now());
            path.on_path_event(&s, PathEvent::MatchingStarted(true), now());
            path.on_path_event(&s, PathEvent::CurrentDemandStarted, now());
            path.on_authorized(&s, now());

            // With matching started and no power delivery request, the loop
            // opens the bounded observation window instead of proceeding.
            assert_eq!(path.state(), AcState::WaitingForAuthentication);
        }

        #[test]
        fn a_v2g_session_setup_no_longer_stands_in_for_a_slac_match() {
            // The setup is late evidence: it cannot precede a match, so it
            // reports the fact only after matching finished and never reports
            // the return to unmatched. `EvseManager.cpp:1215-1225` is the
            // producer, and it is the only one.
            let mut path = hlc(PwmStart::FivePercent);
            let s = session_for(PwmStart::FivePercent);
            path.on_session_start(&s, now());
            path.on_path_event(&s, PathEvent::HlcSessionSetup, now());
            path.on_authorized(&s, now());

            // Matching not started, five percent offer up: figure 3, the offer
            // is withdrawn through state F at once.
            assert_eq!(path.state(), AcState::WaitingForAuthentication);
            assert!(!path.five_percent_active());
        }

        #[test]
        fn slac_reporting_unmatched_takes_the_fact_back() {
            let mut path = hlc(PwmStart::FivePercent);
            let s = session_for(PwmStart::FivePercent);
            path.on_session_start(&s, now());
            path.on_path_event(&s, PathEvent::MatchingStarted(true), now());
            path.on_path_event(&s, PathEvent::MatchingStarted(false), now());
            path.on_authorized(&s, now());

            // Back to figure 3.
            assert!(!path.five_percent_active());
        }

        #[test]
        fn a_fact_arriving_after_the_authorization_reruns_the_decision() {
            // The C++ polls all of these facts every 100 ms, so whichever
            // arrives last runs the decision. Here the authorization arrives
            // with matching already started and no power delivery request, so
            // the loop opens the observation window and waits; the request
            // then arrives and the loop runs again and proceeds.
            let mut path = hlc(PwmStart::FivePercent);
            let s = session_for(PwmStart::FivePercent);
            path.on_session_start(&s, now());
            path.on_path_event(&s, PathEvent::MatchingStarted(true), now());
            let waiting = path.on_authorized(&s, now());
            assert!(
                arms(&waiting, TIMER_FIVE_PERCENT_FALLBACK),
                "the window must be open, got {waiting:?}"
            );
            assert_eq!(path.state(), AcState::WaitingForAuthentication);

            let proceeded = path.on_path_event(&s, PathEvent::SetupFinished, now());

            assert_eq!(path.state(), AcState::PrepareCharging);
            assert!(path.five_percent_active(), "got {proceeded:?}");
        }

        #[test]
        fn a_fact_arriving_mid_detour_does_not_undo_the_detour() {
            // Once the loop has committed to withdrawing the offer the C++
            // has already left `WaitingForAuthentication` for `T_step_EF`, so
            // the loop cannot run again until the detour lands. A power
            // delivery request arriving in the middle therefore does not
            // restore the offer.
            let mut path = hlc(PwmStart::FivePercent);
            let s = session_for(PwmStart::FivePercent);
            path.on_session_start(&s, now());
            path.on_authorized(&s, now());
            assert!(!path.five_percent_active(), "figure 3 ran on arrival");

            path.on_path_event(&s, PathEvent::MatchingStarted(true), now());
            let late = path.on_path_event(&s, PathEvent::SetupFinished, now());

            assert!(!path.five_percent_active(), "got {late:?}");
        }

        #[test]
        fn every_data_link_request_withdraws_the_contactor_permission() {
            for request in [
                DataLinkRequest::Error,
                DataLinkRequest::Pause,
                DataLinkRequest::Terminate,
            ] {
                let (mut path, s) = preparing_five_percent();
                path.on_path_event(&s, allow(true), now());
                path.on_path_event(&s, PathEvent::DataLink(request), now());

                let effects = path.on_bsp(&s, &BspEvent::Cp(CpEvent::C), CpEdges::default(), now());
                assert!(
                    effects.contains(&Effect::AllowPowerOn(false)),
                    "{request:?} must leave the gate shut, got {effects:?}"
                );
            }
        }

        #[test]
        fn a_data_link_pause_and_terminate_signal_x1() {
            // `Charger.cpp:2058` and `:2066`, unconditional in both.
            for request in [DataLinkRequest::Pause, DataLinkRequest::Terminate] {
                let (mut path, s) = preparing_five_percent();
                let effects = path.on_path_event(&s, PathEvent::DataLink(request), now());

                assert!(
                    effects.contains(&Effect::SetCpState(CpState::X1)),
                    "{request:?} must signal X1, got {effects:?}"
                );
            }
        }

        #[test]
        fn a_data_link_error_on_a_running_five_percent_offer_restarts_the_session() {
            // `[V2G3-M07-05]` through `[V2G3-M07-09]`: X1 for `T_STEP_X1`,
            // then E/F for `T_STEP_EF`, then back to waiting for
            // authentication with the offer withdrawn.
            let (mut path, s) = preparing_five_percent();
            let effects =
                path.on_path_event(&s, PathEvent::DataLink(DataLinkRequest::Error), now());

            assert!(
                effects.contains(&Effect::SetCpState(CpState::X1)),
                "the sequence opens on X1, got {effects:?}"
            );
            assert!(arms(&effects, TIMER_T_STEP), "the step must be bounded");

            let ef = path.on_timer(&s, TIMER_T_STEP, now());
            assert!(
                ef.contains(&Effect::SetCpState(CpState::F)),
                "X1 is followed by state E/F, got {ef:?}"
            );

            // Out of state F through the brief X1, then back to waiting, whose
            // entry re-derives the offer and whose checks then carry the
            // session on. `Charger::dlink_error`'s return into `WaitingForAuthentication` returns into
            // `WaitingForAuthentication` with a zero duty cycle and the C++
            // reaches that state's own arms on the same pass: the restart set
            // `hlc_failed`, so `ac_hlc_enabled_current_session` is false and
            // the external arm's "HLC is disabled for this session, simply
            // proceed to PrepareCharging" branch runs. The record is still
            // open, which is what `transaction_started` no longer refuses on.
            path.on_timer(&s, TIMER_T_STEP, now());
            let back = path.on_timer(&s, TIMER_T_STEP, now());

            assert_eq!(path.state(), AcState::PrepareCharging);
            assert!(
                !path.five_percent_active(),
                "the offer is withdrawn, got {back:?}"
            );
            assert!(
                back.contains(&Effect::PwmOff),
                "the pilot comes back at a zero duty cycle, got {back:?}"
            );
            // And then at the nominal one the basic charging session needs.
            assert!(
                back.iter()
                    .any(|effect| matches!(effect, Effect::PwmOn(duty) if *duty > PWM_5_PERCENT)),
                "the session resumes on nominal signalling, got {back:?}"
            );
        }

        #[test]
        fn a_data_link_error_before_the_pilot_offers_anything_touches_the_pilot_not_at_all() {
            // `[V2G3-M07-04]`: the state machine already holds X1.
            let mut path = hlc(PwmStart::FivePercent);
            let s = session_for(PwmStart::FivePercent);
            let effects =
                path.on_path_event(&s, PathEvent::DataLink(DataLinkRequest::Error), now());
            assert_eq!(effects, Vec::new(), "got {effects:?}");
        }

        #[test]
        fn a_data_link_error_does_not_interrupt_a_nominal_duty_cycle_charge() {
            // `[V2G3-M07-12]`, the option the C++ picks at
            // `Charger.cpp:2109-2113`.
            let mut path = hlc(PwmStart::Nominal);
            let s = session_for(PwmStart::Nominal);
            path.on_session_start(&s, now());
            path.on_authorized(&s, now());
            path.on_bsp(&s, &BspEvent::Cp(CpEvent::C), CpEdges::default(), now());
            assert_eq!(path.state(), AcState::Charging);

            let effects =
                path.on_path_event(&s, PathEvent::DataLink(DataLinkRequest::Error), now());

            assert_eq!(path.state(), AcState::Charging, "got {effects:?}");
            assert!(!effects.iter().any(|effect| matches!(
                effect,
                Effect::SetCpState(_) | Effect::PwmOff | Effect::AllowPowerOn(false)
            )));
        }

        #[test]
        fn a_data_link_error_after_the_session_stopped_changes_nothing() {
            let mut path = hlc(PwmStart::FivePercent);
            let s = session_for(PwmStart::FivePercent);
            path.on_session_start(&s, now());
            path.on_bsp(&s, &BspEvent::Cp(CpEvent::A), CpEdges::default(), now());
            assert_eq!(path.state(), AcState::Idle);

            let effects =
                path.on_path_event(&s, PathEvent::DataLink(DataLinkRequest::Error), now());

            assert_eq!(path.state(), AcState::Idle, "got {effects:?}");
        }

        #[test]
        fn a_dc_contactor_open_request_is_inert_on_an_ac_port() {
            // `EvseManager.cpp:827-832` subscribes `dc_open_contactor` inside
            // the `charge_mode == "DC"` branch, so an AC port never receives
            // it, and it drives no supply to remove.
            let (mut path, s) = preparing_five_percent();
            let effects = path.on_path_event(&s, PathEvent::OpenContactorDc, now());
            assert_eq!(effects, Vec::new(), "got {effects:?}");
        }

        /// The per plug in high level communication failure latch.
        ///
        /// `Charger::dlink_error` sets `hlc_failed`, the entry into
        /// `WaitingForAuthentication` reads it into
        /// `ac_hlc_enabled_current_session`, `PrepareCharging` reads it beside
        /// the five percent offer, and the `Idle` entry clears it. A vehicle
        /// that cannot carry ISO 15118 on this plug in therefore finishes the
        /// session as IEC 61851 basic charging rather than being offered
        /// another attempt for the life of the plug in.
        mod the_high_level_communication_failure_latch {
            use super::*;

            /// The cell the latch is the only answer for: a five percent
            /// session whose link fails before any authorization arrives.
            ///
            /// The session start derivation's own escape reads the
            /// authorization, so with none in hand it re-derives the offer on
            /// every restart and the port invites ISO 15118 again, and again,
            /// until the cable comes out.
            #[test]
            fn a_link_error_before_authorization_gives_up_on_iso_for_the_plug_in() {
                // Both offer variants, because the enforced one reaches the
                // same answer through a different half of the derivation: the
                // ordinary offer loses its `!authorized` term and the enforced
                // one loses `ac_enforce_hlc` itself.
                for pwm_start in [PwmStart::FivePercent, PwmStart::FivePercentEnforced] {
                    let mut path = hlc(pwm_start);
                    let s = session_for(pwm_start);
                    let start = path.on_session_start(&s, now());
                    assert!(
                        start.contains(&Effect::PwmOn(PWM_5_PERCENT)),
                        "{pwm_start:?}: the offer that invites matching, got {start:?}"
                    );
                    path.on_path_event(&s, PathEvent::MatchingStarted(true), now());

                    path.on_path_event(&s, PathEvent::DataLink(DataLinkRequest::Error), now());
                    path.on_timer(&s, TIMER_T_STEP, now());
                    path.on_timer(&s, TIMER_T_STEP, now());
                    let back = path.on_timer(&s, TIMER_T_STEP, now());

                    assert_eq!(path.state(), AcState::WaitingForAuthentication);
                    assert!(
                        !path.five_percent_active(),
                        "{pwm_start:?}: the offer must not be re-derived, got {back:?}"
                    );
                    assert!(
                        !back.contains(&Effect::PwmOn(PWM_5_PERCENT)),
                        "{pwm_start:?}: ISO 15118 must not be invited again, got {back:?}"
                    );

                    // And the session the vehicle could not carry over ISO
                    // 15118 completes as basic charging, with no replug.
                    let decided = path.on_authorized(&s, now());
                    assert!(
                        decided.iter().any(
                            |effect| matches!(effect, Effect::PwmOn(duty) if *duty > PWM_5_PERCENT)
                        ),
                        "{pwm_start:?}: nominal signalling, got {decided:?}"
                    );
                    assert_eq!(path.state(), AcState::PrepareCharging);

                    let charging = path.on_bsp(&s, &BspEvent::Cp(CpEvent::C), CpEdges::default(), now());
                    assert!(
                        charging.contains(&Effect::AllowPowerOn(true)),
                        "{pwm_start:?}: got {charging:?}"
                    );
                    assert_eq!(path.state(), AcState::Charging);
                }
            }

            /// The Plug and Charge half raises the offer again, and the latch
            /// still keeps the pilot on nominal.
            ///
            /// The C++ Plug and Charge arm of the authorization loop does not
            /// read `ac_hlc_enabled_current_session` at all: its live charge
            /// loop branch assigns `hlc_use_5percent_current_session = true`
            /// whatever the latch says. `PrepareCharging` then reads the latch
            /// beside the offer and picks the nominal ampere anyway, which is
            /// the second read site carrying the fallback on a route the
            /// session start derivation cannot.
            #[test]
            fn plug_and_charge_may_raise_the_offer_again_and_the_pilot_stays_nominal() {
                let mut path = hlc(PwmStart::FivePercent);
                let mut s = session_for(PwmStart::FivePercent);
                path.on_session_start(&s, now());
                path.on_path_event(&s, PathEvent::MatchingStarted(true), now());
                path.on_path_event(&s, PathEvent::DataLink(DataLinkRequest::Error), now());
                path.on_timer(&s, TIMER_T_STEP, now());
                path.on_timer(&s, TIMER_T_STEP, now());
                path.on_timer(&s, TIMER_T_STEP, now());
                assert!(!path.five_percent_active(), "the latch is set");

                // A vehicle may still open an ISO 15118 session on nominal
                // signalling, and this port never lowers `hlc_charging_active`.
                path.on_path_event(&s, PathEvent::SetupFinished, now());
                s.authorized_plug_and_charge = true;

                let decided = path.on_authorized(&s, now());

                assert!(
                    path.five_percent_active(),
                    "the Plug and Charge arm raises the offer, got {decided:?}"
                );
                assert!(
                    !decided.contains(&Effect::PwmOn(PWM_5_PERCENT)),
                    "and the latch keeps the pilot off it, got {decided:?}"
                );
                assert!(
                    decided
                        .iter()
                        .any(|effect| matches!(effect, Effect::PwmOn(duty) if *duty > PWM_5_PERCENT)),
                    "nominal signalling, got {decided:?}"
                );
            }

            /// The enforced offer falls to the latch too.
            ///
            /// `ac_enforce_hlc` keeps five percent up "until a dlink error is
            /// signalled", and the C++ delivers exactly that through the latch
            /// rather than through a test of its own: the branch that holds the
            /// offer up sits inside `if (ac_hlc_enabled_current_session)`,
            /// which the latch turns off, so the next entry into
            /// `WaitingForAuthentication` takes the "HLC is disabled for this
            /// session" branch and goes straight to nominal signalling.
            #[test]
            fn an_enforcing_port_gives_up_on_iso_after_a_link_error() {
                let mut path = hlc(PwmStart::FivePercentEnforced);
                let s = session_for(PwmStart::FivePercentEnforced);
                path.on_session_start(&s, now());
                path.on_path_event(&s, PathEvent::MatchingStarted(true), now());
                path.on_authorized(&s, now());
                assert!(path.five_percent_active(), "the enforced offer stands");
                assert_eq!(path.state(), AcState::PrepareCharging);

                path.on_path_event(&s, PathEvent::DataLink(DataLinkRequest::Error), now());
                path.on_timer(&s, TIMER_T_STEP, now());
                path.on_timer(&s, TIMER_T_STEP, now());
                let back = path.on_timer(&s, TIMER_T_STEP, now());

                assert!(
                    !path.five_percent_active(),
                    "the enforced offer is withdrawn, got {back:?}"
                );
                assert!(
                    !back.contains(&Effect::PwmOn(PWM_5_PERCENT)),
                    "ISO 15118 must not be invited a second time, got {back:?}"
                );
                assert!(
                    back.iter()
                        .any(|effect| matches!(effect, Effect::PwmOn(duty) if *duty > PWM_5_PERCENT)),
                    "nominal signalling, got {back:?}"
                );
                assert_eq!(path.state(), AcState::PrepareCharging);

                let charging = path.on_bsp(&s, &BspEvent::Cp(CpEvent::C), CpEdges::default(), now());
                assert!(
                    charging.contains(&Effect::AllowPowerOn(true)),
                    "the contactor gate no longer waits on a permission that \
                     will not come, got {charging:?}"
                );
                assert_eq!(path.state(), AcState::Charging);
            }

            /// A second link error inside the same plug in.
            ///
            /// `[V2G3-M07-12]`: the basic charging session the first one fell
            /// back to is not interrupted, and with the latch set there is
            /// nothing left to re-derive either.
            #[test]
            fn a_second_link_error_in_one_plug_in_leaves_the_basic_session_alone() {
                let mut path = hlc(PwmStart::FivePercent);
                let s = session_for(PwmStart::FivePercent);
                path.on_session_start(&s, now());
                path.on_path_event(&s, PathEvent::MatchingStarted(true), now());
                path.on_path_event(&s, PathEvent::DataLink(DataLinkRequest::Error), now());
                path.on_timer(&s, TIMER_T_STEP, now());
                path.on_timer(&s, TIMER_T_STEP, now());
                path.on_timer(&s, TIMER_T_STEP, now());
                path.on_authorized(&s, now());
                path.on_bsp(&s, &BspEvent::Cp(CpEvent::C), CpEdges::default(), now());
                assert_eq!(path.state(), AcState::Charging);

                let again = path.on_path_event(&s, PathEvent::DataLink(DataLinkRequest::Error), now());

                assert_eq!(path.state(), AcState::Charging, "got {again:?}");
                assert!(
                    !again.iter().any(|effect| matches!(
                        effect,
                        Effect::SetCpState(_) | Effect::PwmOff | Effect::PwmOn(_)
                    )),
                    "no second pilot detour, got {again:?}"
                );
            }

            /// The unplug is the clear, and the only one a plugged in vehicle
            /// can reach.
            #[test]
            fn the_unplug_clears_the_latch_and_the_next_vehicle_is_offered_iso_again() {
                let mut path = hlc(PwmStart::FivePercent);
                let s = session_for(PwmStart::FivePercent);
                path.on_session_start(&s, now());
                path.on_path_event(&s, PathEvent::DataLink(DataLinkRequest::Error), now());
                path.on_timer(&s, TIMER_T_STEP, now());
                path.on_timer(&s, TIMER_T_STEP, now());
                path.on_timer(&s, TIMER_T_STEP, now());
                assert!(!path.five_percent_active(), "the latch is set");

                path.on_bsp(&s, &BspEvent::Cp(CpEvent::A), CpEdges::default(), now());
                assert_eq!(path.state(), AcState::Idle);

                let replug = path.on_session_start(&s, now());

                assert!(
                    path.five_percent_active(),
                    "the next vehicle gets a fresh attempt, got {replug:?}"
                );
                assert!(
                    replug.contains(&Effect::PwmOn(PWM_5_PERCENT)),
                    "got {replug:?}"
                );
            }

            /// The port leaving service is the other route into the `Idle`
            /// entry, and the C++ clears there too: `Disabled` sets `Idle` as
            /// soon as the request goes away, and that entry is the clear.
            #[test]
            fn the_port_leaving_service_clears_the_latch_as_well() {
                let mut path = hlc(PwmStart::FivePercent);
                let s = session_for(PwmStart::FivePercent);
                path.on_session_start(&s, now());
                path.on_path_event(&s, PathEvent::DataLink(DataLinkRequest::Error), now());
                path.on_timer(&s, TIMER_T_STEP, now());
                path.on_timer(&s, TIMER_T_STEP, now());
                path.on_timer(&s, TIMER_T_STEP, now());
                assert!(!path.five_percent_active(), "the latch is set");

                path.on_path_event(&s, PathEvent::Disable, now());
                path.on_path_event(&s, PathEvent::Enable, now());

                let next = path.on_session_start(&s, now());

                assert!(path.five_percent_active(), "got {next:?}");
                assert!(next.contains(&Effect::PwmOn(PWM_5_PERCENT)), "got {next:?}");
            }

            /// The second read site: the nominal duty cycle choice in
            /// preparing, which is `(not hlc_use_5percent_current_session or
            /// hlc_failed)` in the C++.
            ///
            /// The route that needs it is a link error arriving with the offer
            /// already off the pilot. `Charger::dlink_error` then finds
            /// `pwm_running` false, runs no pilot detour (`[V2G3-M07-04]`) and
            /// so re-derives nothing: the offer variant still stands and only
            /// this read site is left to keep the pilot off five percent.
            #[test]
            fn a_link_error_with_the_offer_already_down_keeps_the_resume_on_nominal() {
                let mut path = hlc(PwmStart::FivePercentEnforced);
                let s = session_for(PwmStart::FivePercentEnforced);
                path.on_session_start(&s, now());
                path.on_authorized(&s, now());
                assert!(path.five_percent_active());
                assert_eq!(path.state(), AcState::PrepareCharging);

                let paused = path.on_path_event(&s, pause(), now());
                assert!(paused.contains(&Effect::PwmOff), "got {paused:?}");

                let errored =
                    path.on_path_event(&s, PathEvent::DataLink(DataLinkRequest::Error), now());
                assert!(
                    !errored.iter().any(|effect| matches!(effect, Effect::SetCpState(_))),
                    "[V2G3-M07-04]: the pilot already holds X1, got {errored:?}"
                );

                let resumed = path.on_path_event(&s, resume(), now());

                assert!(
                    !resumed.contains(&Effect::PwmOn(PWM_5_PERCENT)),
                    "the resume must not put the offer back, got {resumed:?}"
                );
                assert!(
                    resumed
                        .iter()
                        .any(|effect| matches!(effect, Effect::PwmOn(duty) if *duty > PWM_5_PERCENT)),
                    "nominal signalling, got {resumed:?}"
                );
                assert!(
                    path.five_percent_active(),
                    "`Charger::dlink_error` does not touch the offer variant \
                     itself, and neither does this"
                );

                // And so the contactor gate is still the five percent one,
                // waiting on a permission the failed link will not grant. That
                // is the C++ behavior on this route and a preserved defect, not
                // evidence about the latch: see `docs/architecture.md`.
                let requested = path.on_bsp(&s, &BspEvent::Cp(CpEvent::C), CpEdges::default(), now());
                assert!(
                    requested.contains(&Effect::AllowPowerOn(false)),
                    "got {requested:?}"
                );
                assert_eq!(path.state(), AcState::PrepareCharging);
            }

            /// The latch has no DC reader, and that is deliberate.
            ///
            /// The entry into `WaitingForAuthentication` sets
            /// `hlc_use_5percent_current_session` unconditionally in its DC
            /// branch and never derives `ac_hlc_enabled_current_session`
            /// there, so `hlc_failed` is written on a DC port and read by
            /// nothing. `main` has no DC fallback and this port must not grow
            /// one for symmetry.
            #[test]
            fn a_port_presenting_dc_keeps_offering_five_percent_after_a_link_error() {
                let mut path = hlc(PwmStart::Nominal);
                path.set_presented(PresentedMode::Dc);
                let s = session_for(PwmStart::Nominal);
                path.on_session_start(&s, now());
                assert!(
                    path.five_percent_active(),
                    "the DC branch offers five percent whatever the setting says"
                );

                path.on_path_event(&s, PathEvent::DataLink(DataLinkRequest::Error), now());
                path.on_timer(&s, TIMER_T_STEP, now());
                path.on_timer(&s, TIMER_T_STEP, now());
                let back = path.on_timer(&s, TIMER_T_STEP, now());

                assert!(
                    path.five_percent_active(),
                    "a DC port keeps retrying, got {back:?}"
                );
                assert!(
                    back.contains(&Effect::PwmOn(PWM_5_PERCENT)),
                    "got {back:?}"
                );
            }
        }
    }
}
