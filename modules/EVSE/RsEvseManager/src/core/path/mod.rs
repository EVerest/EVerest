// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

//! The power path: what physically delivers energy.
//!
//! Bound once at construction. `charge_mode` cannot change at runtime: the one
//! mode that reconfigures AC hardware as DC, `ac_with_soc`, is an implementor
//! of this trait rather than a setting the others read, so the trait object
//! selected in the constructor stays sound for the whole run.
//!
//! `ac_with_soc::AcWithSoc` is the mode that flips. What flips inside it is
//! what the vehicle is told, `hlc::PresentedMode`, and it is written in exactly
//! one place. The hardware under it is AC throughout.
//!
//! Facts negotiated per session are passed in through `&Session` rather than
//! selecting a different implementation, because they change while a session is
//! live.

pub mod over_voltage;
pub mod plausibility;
pub mod ac;
pub mod ac_with_soc;
pub mod dc;
pub mod iec;

use std::time::Instant;

use super::effect::{ByPath, Effect, EffectId, EffectIds, EffectOutcome, TimerId};
use super::event::{BspEvent, CpEdges, IsolationReading};
use super::hlc::dc_limits::{DynamicModeRequest, EvMaximumLimits, MaximumLimits, MinimumLimits};
use super::hlc::{DataLinkRequest, PresentedMode};
use super::session::{Session, SessionEvent, StopReason};
use iec::AcState;

/// The narrow input a power path can act on.
///
/// Modelled on `iec::IecInput`: a purpose built input rather than the whole
/// `Event`, so the reducer below it cannot widen into a second coordinator and
/// so a new variant is a compile error in every implementation rather than a
/// silent no-op. `Core::apply` is the single place that derives one from an
/// `Event`, and an event no path can act on never becomes a `PathEvent` at all.
///
/// Payload is carried only where an implementation reads it. `StopFromEv`
/// drops its reason and `SupplyVoltage` drops the present current for that
/// reason; both are still on `Event` for whoever wires a consumer.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum PathEvent {
    /// Availability arbitration resolved to available.
    Enable,
    /// Availability arbitration resolved to unavailable.
    Disable,
    /// The EVSE withdraws its offer while the transaction stays open.
    /// `Charger::pause_charging` (`Charger.cpp:1330-1336`).
    PauseRequested,
    /// The EVSE restores the offer it withdrew.
    /// `Charger::resume_charging` (`Charger.cpp:1338-1345`).
    ResumeRequested,
    /// The energy manager's phase count changed, and the charger state owes the
    /// vehicle a pilot break before the relays move
    /// (`Charger.cpp:1531-1541`). `three_phases` is
    /// `switch_3ph1ph_threephase`.
    ///
    /// Only the two states that take the break reach a path this way. The
    /// direct board call every other state takes (`Charger.cpp:1542`) is
    /// emitted by the enforced limits handler as its own effect, because it
    /// touches no state and needs no path.
    SwitchPhases { three_phases: bool },
    /// A V2G session setup.
    HlcSessionSetup,
    /// SLAC reported whether matching has started, which is the fact the AC
    /// authorization loop branches on (`Charger.cpp:401`, `:439`).
    MatchingStarted(bool),
    /// SLAC reported whether the link is matched, which is the narrower fact
    /// the reinitialization waits on. See `event::HlcEvent::SlacMatched` for
    /// why it is not the same fact as the one above.
    SlacMatched(bool),
    /// SLAC asked for the error sequence: a pilot kick so matching can start
    /// again. `EvseManager.cpp:1253-1256` into
    /// `Charger::request_error_sequence` (`Charger.cpp:2133-2147`).
    ///
    /// A path event because the two states it is allowed from are the
    /// charger's, and the reset it sends travels with it for the same reason:
    /// the C++ signals that from inside the same function, so only a request
    /// the state admitted resets anything.
    SlacErrorRoutine,
    /// The vehicle reported its battery state of charge. The `ac_with_soc`
    /// flip out of the fake DC mode, and nothing else, acts on it.
    StateOfCharge { percent: f64 },
    /// The vehicle's ISO 15118 power delivery request landed and the stack
    /// finished setting the session up (`EvseManager.cpp:394`).
    SetupFinished,
    /// The high level communication half of the contactor permission
    /// (`EvseManager.cpp:395-403`).
    AllowCloseContactor(bool),
    /// One of the three `D-LINK_*` requests. The SLAC relay it also owes is
    /// sent by the high level communication port, so what reaches a path is
    /// only the pilot and permission half.
    DataLink(DataLinkRequest),
    /// The vehicle asked for the DC supply to be removed
    /// (`EvseManager.cpp:827-832`).
    OpenContactorDc,
    /// The vehicle asked for the DC cable check stage.
    /// `EvseManager.cpp:563-567`.
    CableCheckRequired,
    /// Precharge started. `EvseManager.cpp:569-571`.
    PreChargeStarted,
    /// `EvseManager.cpp:573-586`.
    CurrentDemandStarted,
    /// `EvseManager.cpp:588-598`.
    CurrentDemandFinished,
    /// The vehicle terminated the high level communication session.
    StopFromEv,
    /// A reading of the DC supply's present output voltage.
    SupplyVoltage { voltage_v: f64 },
    /// The vehicle's requested DC target (`EvseManager.cpp:734-738`).
    DcEvTarget { voltage_v: f64, current_a: f64 },
    /// The vehicle's ISO 15118-20 dynamic control mode request
    /// (`EvseManager.cpp:740-825`).
    DcDynamicChargeMode(DynamicModeRequest),
    /// The maxima the vehicle reports for itself
    /// (`EvseManager.cpp:851-870`).
    DcEvMaximumLimits(EvMaximumLimits),
    /// The EVSE limit set the DC target clamp reads. It reaches the path rather
    /// than being read off a port, because the C++ holds it on `Charger`
    /// (`Charger::inform_new_evse_max_hlc_limits` and its minimum counterpart)
    /// and every clamp reads it from there.
    /// The EVSE limit set the energy manager's allowance resolves to, with the
    /// two facts the same C++ block decides beside it
    /// (`energyImpl.cpp:566-675`). One event rather than four, because the C++
    /// emits all of it under a single change gate.
    DcEnforcedLimits {
        maximum: MaximumLimits,
        minimum: MinimumLimits,
        exporting_to_grid: bool,
        reapply_target: bool,
    },
    /// The export voltage range the DC supply reports. The maximum is an input
    /// to the cable check voltage derivation; the minimum gates the voltage to
    /// earth check, and both come off the one capability report.
    DcExportVoltageRange { min_v: f64, max_v: f64 },
    /// An isolation monitor reading.
    Isolation(IsolationReading),
    /// The billing meter's own DC voltage, for the plausibility comparison.
    MeterVoltage { voltage_v: f64 },
    /// A voltage sample from the hardware over voltage monitor. The C++ feeds
    /// the same stream to its software watchdog and to the plausibility
    /// monitor.
    OverVoltageMeasurement { voltage_v: f64 },
    /// The isolation monitor's verdict on a self test. A false one fails the
    /// cable check where the stage is waiting for it; the stage timeout covers
    /// a verdict that never arrives.
    IsolationSelfTest(bool),
    /// The power supply withdrew its bidirectional capability while a session
    /// was live, so an in flight discharge must stop. ADR-0018, and a
    /// deliberate divergence: the C++ decides nothing at this transition.
    ///
    /// It carries no payload because the fact it announces is already resolved:
    /// the core recomputes `SessionProfile::bidirectional` before routing it,
    /// so a path reads the answer off the session rather than deriving a second
    /// one from a payload.
    BidirectionalWithdrawn,
}

/// A consequence of an observed state edge that only the coordinator above the
/// trait can carry out.
///
/// The C++ does all three from inside an `if (initialize_state)` block in the
/// entered state's case, which is an edge detector
/// (`Charger.cpp:162`). Here the path observes the edge and the core discharges
/// it, because session identity, the transaction flag and the authorization all
/// live above `PowerPath`.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SessionDuty {
    /// Publish this session event. The core attaches the session identity, so
    /// no path holds one.
    Publish(SessionEvent),
    /// Open the billing record if none is open, and announce it.
    /// `Charger::start_transaction` (`Charger.cpp:1477-1518`), whose two call
    /// sites both guard it on `not flag_transaction_active`
    /// (`Charger.cpp:394` and `:523`) and which publishes the announcement
    /// itself, at `:1515`, rather than leaving it to the caller.
    StartTransaction,
    /// Close the billing record if one is open. `Charger.cpp:1063-1067`, whose
    /// guard on `flag_transaction_active` is what stops a transaction cancelled
    /// earlier from producing a second transactionFinished event.
    StopTransaction,
    /// The session is over. `Charger::stop_session` (`Charger.cpp:1391-1399`),
    /// reached from the `Finished` exit at `Charger.cpp:1081`.
    EndSession,
    /// Tell the vehicle the session is ending, `Charger.cpp:1013-1023`.
    ///
    /// Raised by the edge and answered by the core, because the entry block is
    /// mode independent in the C++ and the two gates it reads are not owned by
    /// a power path: `hlc_charging_active` is the trait's, and the pause
    /// branch's `hlc_d20_active and flag_paused_by_evse` are the session's. A
    /// path that raises this is saying only that it crossed the entry.
    AskVehicleToStop,
    /// Re-announce the ISO 15118 stack for the mode now being presented.
    ///
    /// `EvseManager::switch_DC_mode` and `switch_AC_mode` re-run a whole HLC
    /// setup on each flip. Here the path decides the flip and the core sends
    /// the announcement, because the identity, the debug flag and the
    /// advertised set all live on `hlc::HlcPort` and none of them is a charging
    /// decision. Only `ac_with_soc::AcWithSoc` raises it; see
    /// `hlc::setup::announce` for what each mode is owed.
    AnnounceMode(PresentedMode),
}

/// The session event an `AcState` edge is due, or none.
///
/// One row per `if (initialize_state)` block in `Charger::run_state_machine`
/// that signals a session event. The entered state alone decides, because the
/// C++ reads nothing but `current_state` there either; the edge is what makes
/// it fire once per entry rather than on every pass through the state.
pub fn session_event_for_edge(before: AcState, after: AcState) -> Option<SessionEvent> {
    if before == after {
        return None;
    }
    match after {
        // `Charger.cpp:675`.
        AcState::PrepareCharging => Some(SessionEvent::PrepareCharging),
        // `Charger.cpp:763`.
        AcState::Charging => Some(SessionEvent::ChargingStarted),
        // `Charger.cpp:877`.
        AcState::ChargingPausedEv => Some(SessionEvent::ChargingPausedEv),
        // `Charger.cpp:966-987` signals this one from the resident pass rather
        // than the entry, because it carries the reasons for the pause and
        // republishes when they change.
        //
        // The reasons are not carried. Ceiling: the event announces
        // the pause with no `ChargingPausedEVSEReasons` payload, and a change of
        // reason within one pause is not republished. Upgrade path: derive the
        // reason set the way `Charger.cpp:966-980` does, from the fault set, the
        // energy budget and the pause request, and carry it on the event.
        // Owner: RsEvseManager.
        AcState::ChargingPausedEvse => Some(SessionEvent::ChargingPausedEvse),
        // `Charger.cpp:583`.
        AcState::SwitchPhases => Some(SessionEvent::SwitchingPhases),
        // `Charger.cpp:1012`.
        AcState::StoppingCharging => Some(SessionEvent::StoppingCharging),

        // No row. The `EvseState::Reinit` entry writes a transcript line and
        // signals no session event, which is what makes the reinitialization
        // invisible to a consumer reading the session event sequence: the
        // session it interrupts is the same session on the other side.

        // No row. `Charger.cpp:205` announces the disabled state through the
        // enable and disable pair, which the wire attaches a deciding source to
        // and the core publishes through its own effect. `Charger.cpp:272`
        // publishes `AuthRequired` on the `WaitingForAuthentication` entry,
        // which the core publishes from the plug in it already owns.
        // `Charger.cpp:214` and `:1063` do lifecycle work with no event, which
        // is what the duties below carry. `Startup` has no C++ counterpart.
        AcState::Startup
        | AcState::Idle
        | AcState::WaitingForAuthentication
        | AcState::Reinit
        | AcState::Finished
        | AcState::Disabled => None,
    }
}

/// Everything one session progress edge is due, appended to `out`.
///
/// The state vocabulary is `iec::AcState`, which is `Charger::EvseState` minus
/// the control pilot detour states. That enum is mode independent: one
/// `shared_context.current_state` serves both charge modes and every session
/// event is signalled from an `if (initialize_state)` block inside it
/// (`Charger.cpp:162`). What is mode specific is which trigger moves it, not
/// what an entry is due, so this mapping lives above the trait and each
/// implementation supplies only its own transitions.
pub fn duties_for_edge(before: AcState, after: AcState, out: &mut Vec<SessionDuty>) {
    if before == after || before == AcState::Startup {
        // The port coming up is not the end of anything. The C++ has no startup
        // state: its `Idle` entry at `Charger.cpp:214` runs for the first time
        // against a session that was value initialized inactive, so the same
        // guard is there implicitly.
        return;
    }
    // The one edge that opens a billing record. It is an edge rather than a
    // signal because the C++ makes it one: `start_transaction` is called from
    // `case EvseState::WaitingForAuthentication` and nowhere else
    // (`Charger.cpp:395` and `:524`), and both call sites assign
    // `PrepareCharging` immediately after it, so the transition is the
    // transaction start. Every other route into `PrepareCharging` - the
    // switching break returning, the vehicle's own pause resuming, the EVSE
    // pause resuming - arrives from another state and opens nothing.
    //
    // Ahead of the entry's own event, because the C++ publishes from inside
    // `start_transaction` before the state moves, so `TransactionStarted`
    // precedes `PrepareCharging` on the wire.
    if before == AcState::WaitingForAuthentication && after == AcState::PrepareCharging {
        out.push(SessionDuty::StartTransaction);
    }
    out.extend(session_event_for_edge(before, after).map(SessionDuty::Publish));
    match after {
        // `Charger.cpp:1063-1067`, the `Finished` entry.
        AcState::Finished => out.push(SessionDuty::StopTransaction),

        // The resting states. `Charger.cpp:1080-1084` ends the session on the
        // way out of `Finished` into one of these. Not every route here passes
        // through `Finished` first: an AC unplug does, as the C++ does, but a
        // DC unplug and a disable on either path reach a resting state from a
        // live session directly. The close therefore travels with the edge into
        // the resting state rather than with the route that took it there,
        // which is what makes an ordinary unplug close its billing record.
        //
        // Arriving from `Finished` is the exception: that entry already closed
        // the record, and `Charger.cpp:232` only clears the flag on the `Idle`
        // entry rather than closing anything a second time.
        AcState::Idle | AcState::Disabled => {
            if before != AcState::Finished {
                out.push(SessionDuty::StopTransaction);
            }
            out.push(SessionDuty::EndSession);
        }

        // `Charger.cpp:1013-1023`, the second statement of the same
        // `if (initialize_state)` block whose first raised the event above. It
        // follows the publish for that reason: the C++ signals the state event
        // and asks the vehicle after it, and consumers read the pair as a
        // sequence.
        AcState::StoppingCharging => out.push(SessionDuty::AskVehicleToStop),

        // Nothing beyond the event above. Entering these leaves the session and
        // its billing record open, which is the point of every one of them, and
        // most sharply the reinitialization: `ac_with_soc` uses it to
        // reintroduce a vehicle to the same port in the other mode, so a
        // billing record closed here would split one charge into two.
        AcState::Startup
        | AcState::WaitingForAuthentication
        | AcState::PrepareCharging
        | AcState::Charging
        | AcState::ChargingPausedEv
        | AcState::ChargingPausedEvse
        | AcState::SwitchPhases
        | AcState::Reinit => {}
    }
}

/// Where entering `StoppingCharging` lands once the relays are observed open.
///
/// The `StoppingCharging` exit in `Charger::run_state_machine` re-reads the
/// shared context at that point: four flags decide whether the session is
/// over, and `flag_paused_by_evse` then separates a pause the EVSE asked for
/// from an ordinary halt. The first four are facts a path already holds; the
/// pause is not, because `Core` owns
/// `Session::paused_by_evse` and is its only writer. So the pause travels as
/// the destination stated by the route that began the stop.
///
/// Stating it rather than mirroring the session flag is what keeps it from
/// going stale, and the argument is what states it: each path has exactly one
/// function that enters `StoppingCharging`, `begin_stopping`, and it takes this
/// as a parameter. So every entry restates the destination and no stop can
/// begin without saying where it ends. A value left standing after the stop it
/// was set for is unreadable rather than stale, because the only reader guards
/// on being resident in that state.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum StoppingOutcome {
    /// The exit's `Finished` branch. The session may not restart.
    Finished,
    /// The exit's pause branch. The EVSE withdrew its offer with the
    /// transaction still open, so the session stays open behind it.
    PausedByEvse,
    /// The same pause branch, reached because the budget behind the charge went
    /// away rather than because anyone asked. It is the exit's first disjunct,
    /// `not power_available()` (`Charger.cpp:1088`), and it is separate from
    /// the pause above so that the budget coming back resumes this one and
    /// leaves an operator's pause standing.
    NoEnergy,
}

/// Where a session has got to, and what its edges are owed.
///
/// A path holds one of these instead of a bare state field, so there is no
/// route that moves the state without the observation: `enter` is the only
/// writer and it always runs the mapping above.
pub struct SessionProgress {
    state: AcState,
    duties: Vec<SessionDuty>,
    /// The states entered since the last drain, in order.
    ///
    /// The C++ signals every state entry (`Charger::signal_state`) and two
    /// consumers act on the signal itself rather than on where the port ended
    /// up. One pass here can cross several states, so the settled state alone
    /// would lose the ones passed through: an unplug during a charge crosses
    /// stopping and finished on its way back to idle, and the energy request
    /// owes a priority ask for the finished it crossed.
    entered: Vec<AcState>,
}

impl SessionProgress {
    pub fn new() -> Self {
        Self {
            state: AcState::Startup,
            duties: Vec::new(),
            entered: Vec::new(),
        }
    }

    pub fn state(&self) -> AcState {
        self.state
    }

    /// Move to `next`, recording what the edge is due. Entering the state
    /// already held is not an edge and is owed nothing, so a caller may state
    /// its destination without first checking where it is.
    pub fn enter(&mut self, next: AcState) {
        let before = self.state;
        self.state = next;
        if before != next {
            self.entered.push(next);
        }
        duties_for_edge(before, next, &mut self.duties);
    }

    /// Draining is what makes a duty impossible to discharge twice.
    pub fn take_duties(&mut self) -> Vec<SessionDuty> {
        std::mem::take(&mut self.duties)
    }

    /// Drained once per pass, for the same reason the duties are: a transition
    /// reported twice would ask the energy manager twice for one edge.
    pub fn take_entered(&mut self) -> Vec<AcState> {
        std::mem::take(&mut self.entered)
    }
}

impl Default for SessionProgress {
    fn default() -> Self {
        Self::new()
    }
}

/// Implemented by `ac::AcBasic`, `ac::AcHlc` and `dc::Dc`.
///
/// Every method returns effects rather than performing them, and receives `now`
/// rather than reading a clock, so an implementation is driven directly in tests
/// with no threads and no sleeps.
pub trait PowerPath: Send {
    fn name(&self) -> &'static str;

    /// The port comes up. `Charger::main_thread` starts the control pilot
    /// output before it publishes anything (`Charger.cpp:95-100`), and the IEC
    /// paths reach their first resting state here.
    ///
    /// The single writer drives this rather than emitting the boot effects
    /// itself, because a path that never learns it started never leaves its
    /// startup state and then refuses every plug in that follows.
    fn on_startup(&mut self) -> Vec<Effect>;

    fn on_session_start(&mut self, session: &Session, now: Instant) -> Vec<Effect>;

    fn on_authorized(&mut self, session: &Session, now: Instant) -> Vec<Effect>;

    /// One board support reading, with what the control pilot transition
    /// memory derived from it.
    ///
    /// `edges` rather than a previous level, because the derivation belongs to
    /// the one `CpTracker` above these paths: a path that kept its own copy
    /// could disagree with the SLAC layer about the same reading.
    fn on_bsp(
        &mut self,
        session: &Session,
        event: &BspEvent,
        edges: CpEdges,
        now: Instant,
    ) -> Vec<Effect>;

    fn on_limits_changed(&mut self, session: &Session, now: Instant) -> Vec<Effect>;

    fn on_stop(&mut self, session: &Session, reason: StopReason, now: Instant) -> Vec<Effect>;

    fn on_timer(&mut self, session: &Session, id: TimerId, now: Instant) -> Vec<Effect>;

    /// The states crossed since the last call, in order, drained.
    ///
    /// Separate from `state` because one pass can cross several: the energy
    /// request owes a priority ask for two of them, and the settled state alone
    /// cannot say whether either was crossed.
    fn take_entered_states(&mut self) -> Vec<AcState>;

    /// The state the port is in, as `Charger::get_current_state` reports it.
    ///
    /// Read by the energy flow request, which tells the energy manager what the
    /// port is doing and asks with priority on two of the transitions. It is on
    /// the trait rather than read off each path, because every path already
    /// keeps it and the request is mode independent.
    fn state(&self) -> AcState;

    /// The DC supply voltage target this path is asking for, which the
    /// enforced limits handler reads to turn a current allowance into a power
    /// one. Zero on every path with no supply to ask.
    ///
    /// **No default body, deliberately.** All three of these used to have one
    /// and each default was the AC basic answer, so a path that forgot to
    /// answer got that answer with nothing said. That is exactly the shape of
    /// the defect that made the ISO 15118 stop signalling inert on DC: the fact
    /// lived on `AcHlc`, the mode independent readers above the trait asked
    /// every path for it, and `Dc` inherited `false` from a default. The work
    /// was written, correct and unreachable, and no gate objected. A required
    /// method makes a new path a compile error until it says what it is.
    fn target_voltage_v(&self) -> f64;

    /// `shared_context.hlc_charging_active`, which is a `shared_context` fact
    /// but not a session one: its value is decided by the charge mode at the
    /// `Idle` entry (`Charger.cpp:217-224`) and raised on AC by
    /// `v2g_setup_finished`, so each implementation is the whole of its own
    /// answer and none of them needs to consult another.
    ///
    /// Three readers, all of them above the trait and all mode independent in
    /// the C++: the enforced limits handler, because it is what makes
    /// `Charger::switch_three_phases_while_charging` refuse a phase change
    /// (`Charger.cpp:1528-1530`); `Core::ask_vehicle_to_stop`
    /// (`Charger.cpp:1014`); and `Core::stop_error_for` (`:1359`). See each
    /// implementation for why it says what it says, and `target_voltage_v` for
    /// why none of them may leave it unsaid.
    fn hlc_charging_active(&self) -> bool;

    /// `EvseManager::fake_dc_enabled`, the second positional argument of
    /// `call_session_setup`.
    ///
    /// It is a fact about the power path rather than about the stack, which is
    /// why it is asked here: `EvseManager::init` seeds it from
    /// `config.ac_with_soc` and only `setup_fake_DC_mode` and `setup_AC_mode`
    /// write it afterwards. Three of the four paths answer false, and they say
    /// so; see `target_voltage_v` for why none of them may leave it unsaid.
    fn presents_fake_dc(&self) -> bool;

    /// `Charger::power_available` (`Charger.cpp:2124-2130`), whose two branches
    /// are the two charge modes: AC tests the stored maximum against the five
    /// point nine ampere floor, DC tests both enforced EVSE limits above zero.
    ///
    /// One reader above the trait, `Core::pause_reasons`, which is the
    /// `ChargingPausedEVSE` body deciding whether the charge is being held for
    /// want of energy. It is asked here rather than derived above, for the
    /// reason `target_voltage_v` gives: the C++ branches on the charge mode and
    /// a default here would silently hand one mode the other's answer.
    ///
    /// **Declared after the three above, and that position is load bearing.**
    /// `scripts/unconstructable.py`'s
    /// `a_fifth_power_path_that_answers_none_of_the_mode_facts` names those
    /// three in its expected diagnostic and matches it as a substring, so a
    /// required method declared among them reorders `rustc`'s list and that
    /// case reports itself no longer refused. Declared last, the expected text
    /// stays a prefix of the longer list. A fifth belongs here too.
    fn power_available(&self) -> bool;

    /// The current this path last signalled to the vehicle, which is
    /// `Charger::get_max_current_signalled_to_ev_internal`
    /// (`Charger.cpp:1925-1932`).
    ///
    /// Not the current limit: the figure the pilot carries can be a five
    /// percent offer, which signals no current at all, or nothing, and it lags
    /// the limit in the C++ by up to the five second PWM update interval of IEC
    /// 61851-1. Read by soft overcurrent detection and by nothing else, as its
    /// one C++ caller is `Charger::check_soft_over_current`.
    ///
    /// No default, because every answer is a safety threshold: a path that
    /// under-reports lets a vehicle overdraw undetected and one that
    /// over-reports trips on a compliant vehicle. Each implementation says what
    /// it says and why.
    fn signalled_current_a(&self) -> f64;

    /// Join the core's `EffectId` space, replacing whatever source of
    /// identities this path was built with.
    ///
    /// Called once by `Core::new`, because `Core` correlates completions ahead
    /// of the paths rather than beside them: it offers every `EffectDone` to its
    /// own await before this trait sees one. A path allocating from a private
    /// counter therefore shares a space with the core by accident, and the two
    /// answer each other's awaits.
    ///
    /// The default is a no-op, which is the whole answer for a path that awaits
    /// no completion of its own: `AcBasic` and `AcHlc` allocate no identity, so
    /// there is nothing for a shared space to protect. A path that starts
    /// awaiting something must override this, and `on_effect_done` returning
    /// anything is the sign that it has to.
    ///
    /// `EffectIds<ByPath>` has no constructor of its own: the only way to get
    /// one is `EffectIds::<ByCore>::delegate`, so whatever arrives here is a
    /// handle on the core's counter and cannot be a counter.
    fn adopt_effect_ids(&mut self, _ids: EffectIds<ByPath>) {}

    /// `id` is `None` when nothing awaited the effect, so a path must never
    /// treat an unidentified completion as the one it asked for.
    fn on_effect_done(
        &mut self,
        session: &Session,
        id: Option<EffectId>,
        outcome: &EffectOutcome,
        now: Instant,
    ) -> Vec<Effect>;

    /// The facts a power path can act on that are not one of the calls above:
    /// availability, pause and resume, the high level communication stages and
    /// the DC readings. The match must be exhaustive in every implementation.
    /// A variant an implementation does nothing for gets its own arm and a
    /// reason, so the absence is a stated fact rather than a fall-through.
    fn on_path_event(&mut self, session: &Session, event: PathEvent, now: Instant) -> Vec<Effect>;

    /// The duties the calls above raised, taken by the core once per pass.
    ///
    /// Draining is what makes a duty impossible to discharge twice: it is
    /// reported once, by the call that observed the edge, and the accumulator
    /// is empty afterwards.
    fn take_session_duties(&mut self) -> Vec<SessionDuty>;

    /// Safe state, issued on shutdown and on any fatal error. Must be
    /// idempotent, and must order energy removal before vehicle release.
    fn to_safe_state(&mut self) -> Vec<Effect>;
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The edge table, one row per `if (initialize_state)` block in
    /// `Charger::run_state_machine` that signals a session event.
    #[test]
    fn each_publishable_state_entry_derives_its_session_event() {
        for (entered, event) in [
            (AcState::PrepareCharging, SessionEvent::PrepareCharging),
            (AcState::Charging, SessionEvent::ChargingStarted),
            (AcState::ChargingPausedEv, SessionEvent::ChargingPausedEv),
            (
                AcState::ChargingPausedEvse,
                SessionEvent::ChargingPausedEvse,
            ),
            (AcState::StoppingCharging, SessionEvent::StoppingCharging),
        ] {
            assert_eq!(
                session_event_for_edge(AcState::Idle, entered),
                Some(event),
                "{entered:?}"
            );
        }
    }

    #[test]
    fn a_state_entry_with_no_row_derives_no_session_event() {
        for entered in [
            AcState::Startup,
            AcState::Idle,
            AcState::WaitingForAuthentication,
            AcState::Finished,
            AcState::Disabled,
        ] {
            assert_eq!(
                session_event_for_edge(AcState::Charging, entered),
                None,
                "{entered:?}"
            );
        }
    }

    /// The `initialize_state` property: an edge fires on entry and never while
    /// resident. Asserted over the whole table rather than over one witness.
    /// The duties half of the same property is
    /// `a_state_that_does_not_change_is_owed_nothing`.
    #[test]
    fn a_state_that_stays_the_same_is_not_an_entry() {
        for state in [
            AcState::Startup,
            AcState::Idle,
            AcState::WaitingForAuthentication,
            AcState::PrepareCharging,
            AcState::Charging,
            AcState::ChargingPausedEv,
            AcState::ChargingPausedEvse,
            AcState::StoppingCharging,
            AcState::Finished,
            AcState::Disabled,
        ] {
            assert_eq!(session_event_for_edge(state, state), None, "{state:?}");
        }
    }

    /// The `Finished` entry closes the record (`Charger.cpp:1063-1067`) and the
    /// exit into a resting state ends the session (`Charger.cpp:1081-1082`).
    /// Both together would close one record twice.
    #[test]
    fn a_resting_state_entered_from_finished_does_not_close_the_record_again() {
        let mut duties = Vec::new();
        duties_for_edge(AcState::Finished, AcState::Idle, &mut duties);
        assert_eq!(duties, vec![SessionDuty::EndSession]);

        let mut duties = Vec::new();
        duties_for_edge(AcState::Finished, AcState::Disabled, &mut duties);
        assert_eq!(duties, vec![SessionDuty::EndSession]);
    }

    /// A path that reaches a resting state without passing through `Finished`
    /// carries the close with the edge instead. That is what an unplug on the
    /// DC path is.
    #[test]
    fn a_resting_state_entered_from_a_live_session_closes_the_record() {
        let mut duties = Vec::new();
        duties_for_edge(AcState::Charging, AcState::Idle, &mut duties);
        assert_eq!(
            duties,
            vec![SessionDuty::StopTransaction, SessionDuty::EndSession]
        );
    }

    #[test]
    fn a_state_that_does_not_change_is_owed_nothing() {
        for state in [
            AcState::Startup,
            AcState::Idle,
            AcState::WaitingForAuthentication,
            AcState::PrepareCharging,
            AcState::Charging,
            AcState::ChargingPausedEv,
            AcState::ChargingPausedEvse,
            AcState::StoppingCharging,
            AcState::Finished,
            AcState::Disabled,
        ] {
            let mut duties = Vec::new();
            duties_for_edge(state, state, &mut duties);
            assert_eq!(duties, Vec::new(), "{state:?}");
        }
    }

    /// Every `AcState`, so the cross product below covers the enum rather than
    /// a sample of it.
    const EVERY_AC_STATE: &[AcState] = &[
        AcState::Startup,
        AcState::Idle,
        AcState::WaitingForAuthentication,
        AcState::PrepareCharging,
        AcState::Charging,
        AcState::ChargingPausedEv,
        AcState::ChargingPausedEvse,
        AcState::SwitchPhases,
        AcState::StoppingCharging,
        AcState::Reinit,
        AcState::Finished,
        AcState::Disabled,
    ];

    /// The billing record opens on one edge and on no other, stated over the
    /// whole cross product rather than over the one edge.
    ///
    /// The defect this replaces was an announcement made from a second place:
    /// the authorization signal published `TransactionStarted` whatever state
    /// the port was in, while this state machine already refused it. A duty
    /// raised on a second edge would be the same defect in a new shape, and
    /// only the cross product can see one.
    #[test]
    fn only_the_authentication_to_preparation_edge_opens_a_billing_record() {
        for before in EVERY_AC_STATE {
            for after in EVERY_AC_STATE {
                let mut duties = Vec::new();
                duties_for_edge(*before, *after, &mut duties);

                assert_eq!(
                    duties.contains(&SessionDuty::StartTransaction),
                    *before == AcState::WaitingForAuthentication
                        && *after == AcState::PrepareCharging,
                    "{before:?} -> {after:?} derived {duties:?}"
                );
            }
        }
    }

    /// The port coming up is not the end of anything.
    #[test]
    fn the_startup_edge_is_owed_nothing() {
        let mut duties = Vec::new();
        duties_for_edge(AcState::Startup, AcState::Idle, &mut duties);
        assert_eq!(duties, Vec::new());
    }

    #[test]
    fn progress_reports_each_edge_once() {
        let mut progress = SessionProgress::new();
        progress.enter(AcState::Idle);
        progress.enter(AcState::WaitingForAuthentication);
        progress.enter(AcState::PrepareCharging);
        assert_eq!(progress.state(), AcState::PrepareCharging);
        assert_eq!(
            progress.take_duties(),
            vec![
                SessionDuty::StartTransaction,
                SessionDuty::Publish(SessionEvent::PrepareCharging)
            ]
        );
        assert_eq!(progress.take_duties(), Vec::new());
    }

    /// Naming the state already held is not an edge, so a caller may state its
    /// destination without first checking where it is.
    #[test]
    fn progress_entering_the_state_it_holds_is_owed_nothing() {
        let mut progress = SessionProgress::new();
        progress.enter(AcState::Idle);
        progress.enter(AcState::WaitingForAuthentication);
        progress.take_duties();

        progress.enter(AcState::WaitingForAuthentication);

        assert_eq!(progress.take_duties(), Vec::new());
    }
}
