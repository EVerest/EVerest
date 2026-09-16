// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

//! The single writer.
//!
//! `Core::apply` performs no I/O, reads no clock, spawns nothing and locks
//! nothing. Time arrives as a parameter, so a forty second DC sequence runs
//! deterministically in microseconds under test.

pub mod auth;
pub mod config;
pub mod derate;
pub mod effect;
pub mod enable;
pub mod energy;
pub mod event;
pub mod faults;
pub mod hlc;
pub mod path;
pub mod persist;
pub mod powermeter_limits;
pub mod protocol;
pub mod session;
pub mod session_id;
pub mod session_log;
pub mod soft_oc;
pub mod token;

use std::time::{Duration, Instant};

use auth::{Auth, AuthContext, AuthPoll, AuthSignal, AuthorizationKind};
use effect::{
    ByCore, Effect, EffectId, EffectIds, EffectOutcome, ErrorReport, EvseError, HlcUpdate, Issued,
    SessionEventReport, SessionPayload, TimerId,
};
use enable::{EnableEntry, EnableTable};
use energy::enforce::PhaseSwitch;
use energy::{EnergyTree, Publish, PUBLISH_INTERVAL, TIMER_BUDGET_VALIDITY, TIMER_ENERGY_FLOW_REQUEST};
use event::{
    BspEvent, Command, CpEdges, CpEvent, CpTracker, ErrorEvent, ErrorSource, Event, HlcEvent,
    PowerSupplyCapabilities, Severity,
};
use faults::{Cause, FaultSignal, Faults};
use hlc::{
    AuthorizationHeld, AuthorizationResponse, DataLinkRequest, EvInfo, HlcPort, Route, Trigger,
    Verdict,
};
use path::iec::AcState;
use path::{PathEvent, PowerPath, SessionDuty};
use persist::SessionStore;
use powermeter_limits::CarSideMeter;
use protocol::SelectedProtocol;
use session::{
    EnableScope, EnergyTransferMode, PauseReason, Session, SessionEvent, SessionPhase,
    StartSessionReason, StopReason, StopTransactionReason,
};
use session_id::{EntropyExhausted, SessionIds};
use session_log::SessionLogEffect;
use soft_oc::{Detection, SoftOverCurrentAction, TIMER_SOFT_OVER_CURRENT};
use token::IdTag;

/// `evse_manager/Internal` (`errors/evse_manager.yaml`), raised when a session
/// start cannot be given an unpredictable identity.
///
/// The boundary's `RAISABLE_ERRORS` table in `main.rs` carries `Inoperative` and
/// `MREC9AuthorizationTimeout` only, so this raise is logged and dropped there
/// until the type is added to it. What blocks charging is the `Inoperative` the
/// fault set derives from it, and that is raised either way, so the safe state
/// and the bounded connector release do not wait on that table.
///
/// The error type constants belong in `faults` beside `INOPERATIVE`
/// and `MREC9_AUTHORIZATION_TIMEOUT`. This one sits at its single raise site
/// because `faults` was owned by another change while this landed; moving it
/// there is the upgrade and nothing else moves with it.
const INTERNAL: &str = "evse_manager/Internal";

/// The description carried with it. A session id is the transaction identity on
/// the bus, so the module has no id it may substitute.
const NO_SESSION_ID_DESCRIPTION: &str =
    "No unpredictable session id could be produced; the session start was refused.";

/// Carried when the thread that owns every deadline is gone. Named separately
/// from the session id case because the two share an error type but not a
/// remedy: that one refuses one session, this one ends the port's service.
const NO_TIMERS_DESCRIPTION: &str =
    "The timer thread stopped; no deadline can be honored and the port is out of service.";

/// What the ready sequence needs from configuration.
///
/// `EvseManager::ready_to_start_charging` is not called from `init` when
/// `external_ready_to_start_charging` is set; the module publishes
/// `waiting_for_external_ready` and waits for the external command instead
/// (`EvseManager.cpp:1491-1495`).
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct ReadyGate {
    pub awaits_external_signal: bool,
}

/// What configuration says about the billing meter.
///
/// One setting, carried in a struct rather than as a bare `bool` parameter, for
/// the reason `ReadyGate` is one: a call site that names the field cannot pass
/// the wrong flag. Deliberately not `Default`: the configured default is on, so
/// a construction that omitted the field would silently bill best effort on a
/// deployment that asked not to.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Metering {
    /// `fail_on_powermeter_errors`. When on, a meter that refuses to open the
    /// billing transaction takes the port out of service, because a customer
    /// who cannot be billed is not charged (`Charger.cpp:1427-1432`).
    pub fail_on_errors: bool,
}

/// Whether a request asks the energy manager to answer now.
///
/// A named pair rather than a `bool` at four call sites, because the C++ passes
/// a bare `bool` and the three false ones read as nothing at all.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum Urgency {
    /// `request_energy_from_energy_manager(true)`.
    Priority,
    /// The once a second ask.
    Periodic,
}

pub struct Core {
    path: Box<dyn PowerPath>,
    session: Session,
    /// Availability arbitration. It lives here rather than on a path because
    /// enablement is mode independent in the C++ too, and because the startup
    /// announcement and the `enable_disable` command are the same table.
    enable: EnableTable,
    /// Whether this EVSE holds permission to charge, and how it got it.
    auth: Auth,
    /// The active fault set and the `Inoperative` error it decides. Above the
    /// path, because error handling is mode independent in the C++ too.
    faults: Faults,
    /// Mints `Session::id` from a boundary supplied byte source, because `core`
    /// holds no random source of its own; see `session_id`.
    session_ids: SessionIds,
    /// Control pilot transition memory. One instance for the whole module: the
    /// plug in edge reaches the power paths as a `PowerPath::on_session_start`
    /// call, so no path keeps a derivation of its own to disagree with.
    cp: CpTracker,
    ready: ReadyGate,
    /// When `ready` was published, which is `mod->timepoint_ready_for_charging`
    /// (`EvseManager.cpp:1507`). `Some` is also the "already published" flag
    /// the C++ keeps separately as `charger_ready` (`:1499`): the two are the
    /// same fact, so one field cannot disagree with the other.
    ready_at: Option<Instant>,
    metering: Metering,
    /// Identities for the effects this core awaits itself, and the one space
    /// the power path draws from too.
    ///
    /// It is one space because this core correlates ahead of the path rather
    /// than beside it: the `Event::EffectDone` arm offers every completion to
    /// `answer_transaction_start` before `on_effect_done`. While the path held a
    /// private allocator the two both began at zero, and each answered the
    /// other's await.
    ///
    /// `ByCore` is the half of that the compiler now holds: this is the root of
    /// the space, the path gets the one delegate, and an identity the path
    /// allocated has a type this core's await slot will not take.
    effect_ids: EffectIds<ByCore>,
    /// The metering transaction start whose verdict has not arrived. Cleared by
    /// the verdict, so a second delivery of the same identity answers nothing.
    ///
    /// `Issued<ByCore>` rather than `EffectId`, so the only thing that can fill
    /// it is this core's own allocation. Neither the identity a completion
    /// carries back nor one the power path issued can be put here.
    awaiting_transaction_start: Option<Issued<ByCore>>,
    shutting_down: bool,
    /// The ISO 15118 port: the boot sequence, and the advertised energy
    /// transfer mode set with the two capability facts it derives from.
    ///
    /// `None` on a basic AC port, which is the one deployment with no stack
    /// wired. Absence is the whole of `hlc_enabled`: the port used to hold a
    /// disabled `HlcConfig` and thirty runtime guards read it, so every
    /// emission in that file was reachable-or-not by a flag rather than by a
    /// type. See `config::Deployment`, which is the only producer of this
    /// field and pairs it with the path in one `match`.
    hlc: Option<HlcPort>,
    /// The car side power meter's last reported floors.
    ///
    /// The car side meter sits on the DC side and is the billing meter when one
    /// is connected. What is kept here is not its readings but the minimum
    /// currents it says it can measure, which calibration law accuracy limits
    /// can put above the supply's own minimum. Every reader routes them either
    /// into the limits advertised to the vehicle or into the capability report
    /// the energy tree holds; no setpoint reads them. `EvseManager.cpp:245-249`
    /// subscribes to the same report, and `apply_powermeter_limits` is the C++
    /// merge.
    ///
    /// Most deployments have no such meter, which is why the requirement is
    /// `min_connections: 0` in this manifest as in the C++ one. Absent, and
    /// until the first report, `CarSideMeter::floors` is `None` and every merge
    /// is the identity, so nothing downstream needs to know.
    ///
    /// Above the high level communication port, because the C++ keeps them on
    /// `EvseManager` outside its `hlc_enabled` block and a basic AC port with a
    /// car side meter records them with no stack to tell.
    car_side_meter: CarSideMeter,
    /// What this node asks the energy manager for, and the identity it asks
    /// under.
    energy: EnergyTree,
    /// The session uuid held across a restart. Written when a transaction
    /// opens, removed when it closes, and read once at startup.
    persist: SessionStore,
    /// The state the transcript last named, so a transition line can name both
    /// ends of the edge. `Charger.cpp` keeps the same field for the same
    /// reason (`internal_context.last_state_detect_state_change`) and value
    /// initializes it to `Idle` (`:69`); here it starts where the paths start,
    /// because the port has a `Startup` state the C++ does not.
    logged_state: AcState,
    /// `initial_powermeter_value_received` (`EvseManager.cpp:1196-1200`), the
    /// predicate `EvseManager::ready` waits on.
    /// Set by the first reading to arrive, whenever that is: the C++
    /// subscribes before it waits, so a reading landing during `init` means
    /// the wait is already over when `ready` reaches it.
    initial_meter_seen: bool,
    /// Whether the startup sequence is parked on that reading.
    ///
    /// This is the C++'s blocked `ready` thread, as a fact rather than a
    /// thread: `true` from the boot that found no reading until whichever of
    /// the reading and the deadline releases it. `false` afterwards, so a
    /// second reading releases nothing and the sequence runs once.
    startup_held: bool,
    /// See `TIMER_INITIAL_METER_VALUE`.
    initial_meter_timeout: Duration,
    /// The pause reason set the last `ChargingPausedEvse` announcement named,
    /// so a change is announced and a repeat is not.
    /// `internal_context.last_charging_paused_evse_reasons` in the C++, which
    /// the `ChargingPausedEVSE` entry clears and its body compares against on
    /// every pass (`Charger.cpp:979`, `:1027-1029`). Empty is both the cleared
    /// value and "not paused", which is the C++ shape too: the body treats an
    /// empty set as a resume rather than as a set to announce.
    paused_reasons: Vec<PauseReason>,
    /// Soft overcurrent detection, or `None` on a path that has none.
    ///
    /// `None` is the DC answer, and it is the whole mode gate:
    /// `check_soft_over_current` is called from the AC branch of the charging
    /// state and from nowhere else (`Charger.cpp:852`, `:888`), so a DC port
    /// holds no detector rather than holding one behind a runtime test of the
    /// mode. A path with no detector cannot acquire a threshold by accident.
    soft_oc: Option<Detection>,
    /// Which protocol the port reports on `selected_protocol`.
    ///
    /// Here rather than on `Session`, because it is not per session: its value
    /// at boot is `Unknown` for every charge mode, the `ac_with_soc` flip
    /// writes it outside any session lifecycle event, and the C++ member it
    /// ports lives on `EvseManager` beside the other things that outlive a
    /// session. See `protocol`, which holds every writer and the predicate
    /// that decides when it reaches the wire.
    protocol: SelectedProtocol,
}

/// The billing meter's first reading, which the startup sequence waits for.
///
/// `EvseManager::ready` blocks on `powermeter_cv.wait_for` for
/// `initial_meter_value_timeout_ms` before it announces the resume, closes an
/// interrupted record or reports itself ready.
/// This port cannot block, so the deadline is a timer and the remainder of the
/// sequence is held until one of the two arrives.
///
/// In the 4xx block with the other deadlines `Core` owns itself. See
/// `every_timer_identity_in_the_module_is_unique`.
pub const TIMER_INITIAL_METER_VALUE: TimerId = TimerId(403);

/// Everything `Core` is built from besides its power path and its session.
///
/// A struct rather than a parameter list, so every collaborator is named at the
/// construction site. Two of these are error handling state and two are
/// identity state; passed positionally they are six values a caller can only
/// get right by counting.
pub struct CoreParts {
    pub ready: ReadyGate,
    pub auth: Auth,
    pub faults: Faults,
    pub session_ids: SessionIds,
    pub metering: Metering,
    /// The ISO 15118 port, or `None` on a deployment with no stack. Produced
    /// only by `config::resolve`, which chooses it and the power path together.
    pub hlc: Option<HlcPort>,
    pub energy: EnergyTree,
    /// The session uuid across a restart, and the record the previous run left.
    ///
    /// Carried in at construction rather than arriving as an event because
    /// that is where the C++ has it: `PersistentStore` is built in
    /// `EvseManager::init` (`EvseManager.cpp:128`), before anything can open a
    /// transaction, and the startup read is a plain call in the same
    /// initialization. A core built without one could not recover, so the
    /// recovery input is a construction argument and not an optional message.
    pub persist: SessionStore,
    /// Soft overcurrent detection, `None` on DC. See `Core::soft_oc`.
    pub soft_oc: Option<Detection>,
    /// How long the startup sequence waits for the billing meter's first
    /// reading, `initial_meter_value_timeout_ms`. Zero does not wait, which is
    /// what the setting's own description says it means.
    pub initial_meter_timeout: Duration,
}

impl Core {
    pub fn new(mut path: Box<dyn PowerPath>, session: Session, parts: CoreParts) -> Self {
        // Read before the path moves, so the transcript's first transition
        // names where the port actually was and not an assumed `Startup`.
        let path_state = path.state();

        // One id space for both correlators, established before any session
        // moves. A path left on its own allocator awaits identities this core
        // also mints, and whichever await is offered the completion first
        // claims it.
        let effect_ids = EffectIds::one_space();
        path.adopt_effect_ids(effect_ids.delegate());

        Self {
            path,
            session,
            enable: EnableTable::new(),
            auth: parts.auth,
            faults: parts.faults,
            session_ids: parts.session_ids,
            cp: CpTracker::new(),
            ready: parts.ready,
            ready_at: None,
            metering: parts.metering,
            effect_ids,
            awaiting_transaction_start: None,
            shutting_down: false,
            hlc: parts.hlc,
            car_side_meter: CarSideMeter::default(),
            energy: parts.energy,
            persist: parts.persist,
            logged_state: path_state,
            initial_meter_seen: false,
            startup_held: false,
            initial_meter_timeout: parts.initial_meter_timeout,
            paused_reasons: Vec::new(),
            soft_oc: parts.soft_oc,
            protocol: SelectedProtocol::default(),
        }
    }

    pub fn enable_table(&self) -> &EnableTable {
        &self.enable
    }

    pub fn session(&self) -> &Session {
        &self.session
    }

    pub fn auth(&self) -> &Auth {
        &self.auth
    }

    pub fn path_name(&self) -> &'static str {
        self.path.name()
    }

    /// The whole domain, as one function.
    pub fn apply(&mut self, event: Event, now: Instant) -> Vec<Effect> {
        if self.shutting_down {
            return Vec::new();
        }

        // Read at the pass boundary, for the reason
        // `discharge_session_duties` gives: the pass below can cross the
        // `StoppingCharging` entry that reads this flag and the `Idle` entry
        // that clears it, and the C++ order puts the read first.
        let hlc_active = self.path.hlc_charging_active();

        // The C++ re-reads `flag_authorized` on every pass through its state
        // machine rather than acting where the permission is lost. The look
        // therefore happens once per event and before the event is handled,
        // which is the pass boundary: whatever the previous pass left behind
        // decides what this one starts from.
        let mut effects = self.poll_authorization(now);
        effects.extend(match event {
            Event::Startup => self.on_startup(now),

            Event::Shutdown => {
                self.shutting_down = true;
                self.path.to_safe_state()
            }

            // Every deadline in the module went with that thread. The usual
            // answer to a fault of this class is safe state plus a bounded wait
            // before the connector is released, but that bound is a timer, so
            // here it can never arrive. Holding the vehicle would hold it until
            // the process is restarted, so the release is immediate and the
            // safe state runs first, which is the same order the bounded path
            // reaches by waiting. The port does not come back: without
            // deadlines it cannot honor a cable check, a pilot step or a
            // fallback window, so it stops rather than serving a session it
            // cannot time.
            Event::TimerThreadDied => {
                let mut effects =
                    self.raise_own_error(INTERNAL, NO_TIMERS_DESCRIPTION, Severity::High);
                effects.extend(self.path.to_safe_state());
                effects.push(Effect::UnlockConnector);
                self.shutting_down = true;
                effects
            }

            // The unplug clear runs after the path has seen the unplug, as it
            // does in the C++: `Charger::clear_errors_on_unplug` sits at the end
            // of the `Idle` entry (`Charger.cpp:233`), so the port is already
            // idle when a surviving fault re-asserts the safe state.
            Event::Bsp(ref bsp) => {
                // The reading is fed to the transition memory before anything
                // acts on it, because every edge it makes is a fact about the
                // pair (previous level, this reading) and nothing below can
                // recover it afterwards.
                let edges = match bsp {
                    BspEvent::Cp(cp) => self.cp.observe(*cp),
                    _ => CpEdges::default(),
                };
                let plugged_in = edges.plugged_in;
                // The SLAC layer is told first, which is where
                // `EvseManager.cpp:1094-1119` sits: ahead of the charger's
                // queue push and ahead of the stack block below.
                let mut effects = match self.hlc.as_mut() {
                    Some(hlc) => hlc.on_pilot(edges),
                    None => Vec::new(),
                };
                if plugged_in {
                    effects.extend(self.on_plug_in(now));
                }
                // A capability report is the one board support fact the
                // advertised set derives from, and the derivation is above the
                // path because it is not a charging decision.
                if let BspEvent::Capabilities(caps) = bsp {
                    if let Some(hlc) = self.hlc.as_mut() {
                        effects.extend(hlc.note_ac_capabilities((*caps).into()));
                    }
                    // The same report is what the energy request asks under,
                    // and what first says how many phases are live
                    // (`EvseManager.cpp:246-261`).
                    self.energy.note_capabilities(*caps);
                }
                if let BspEvent::PpAmpacity(ampacity_a) = bsp {
                    self.energy.note_pp_ampacity(*ampacity_a);
                }
                effects.extend(self.path.on_bsp(&self.session, bsp, edges, now));
                if matches!(bsp, BspEvent::Cp(cp) if cp.is_unplug()) {
                    effects.extend(self.clear_own_errors());
                    if let Some(hlc) = self.hlc.as_mut() {
                        hlc.on_unplug();
                    }
                }
                // The stack hears about the arrival and about every relay
                // movement, after the pilot event has reached the path. That is
                // the C++ order: `EvseManager.cpp:1118` pushes the event onto
                // the charger's queue and the forwarding block at `:1121-1142`
                // follows it. One block, gated on high level communication
                // alone, with no charge mode branch.
                if let Some(hlc) = self.hlc.as_mut() {
                    if plugged_in {
                        effects.extend(hlc.on_plug_in());
                    }
                    if let BspEvent::Cp(cp) = bsp {
                        match cp {
                            CpEvent::PowerOn => effects.extend(hlc.on_contactor(true)),
                            CpEvent::PowerOff => effects.extend(hlc.on_contactor(false)),
                            _ => {}
                        }
                    }
                }
                effects
            }

            Event::PowerSupplyCapabilities(caps) => self.on_supply_capabilities(&caps, now),

            // The energy tree's verdict, which reaches three places
            // (`energy_grid/energyImpl.cpp:519`, then `Charger::set_max_current`
            // at `Charger.cpp:1306-1325`).
            //
            // The vehicle is told last. In the C++ the overcurrent limit reaches
            // the board (`:1317`) before the signal that carries the figure to
            // the stack (`:1322`), and the path emits that limit here; nothing
            // reads the two as a sequence, and they travel on different
            // execution lanes at the boundary anyway.
            Event::EnforcedLimits(value) => {
                let context = energy::enforce::Context {
                    // All three are ISO 15118 facts, so a deployment with no
                    // stack answers each with the "nothing selected, nothing
                    // active" case rather than with a stored default. A basic
                    // AC port selects no ISO 15118-20 service, raises no SAE
                    // bidirectional flag, and has no `hack_allow_bpt_with_iso2`
                    // setting to read, because the setting lives on the
                    // configuration the stack is announced from.
                    selected_service: self
                        .hlc
                        .as_ref()
                        .and_then(|hlc| hlc.selected_service()),
                    allow_bpt_with_iso2: self
                        .hlc
                        .as_ref()
                        .is_some_and(|hlc| hlc.allow_bpt_with_iso2()),
                    sae_bidi_active: self
                        .hlc
                        .as_ref()
                        .is_some_and(|hlc| hlc.sae_bidi_active()),
                    target_voltage_v: self.path.target_voltage_v(),
                    hlc_charging_active: self.path.hlc_charging_active(),
                    charger_state: self.path.state(),
                    ready_since: self.ready_at,
                    now,
                };
                let Some(enforcement) = self.energy.enforce(*value, context) else {
                    return Vec::new();
                };
                // `Charger::set_max_current` refuses a grant whose validity has
                // already passed (`Charger.cpp:1384-1385`): it returns false,
                // the charger keeps the limit it had, and no
                // `signal_max_current` leaves the module. The phase count is a
                // different signal (`evse_managerImpl.cpp:58-63`, fed by
                // `energyImpl.cpp:530`) and is not gated on the validity, so it
                // is taken either way.
                let valid_for_s = enforcement.published.valid_for_s;
                if valid_for_s > 0 {
                    self.session.limits = enforcement.public_limits;
                } else {
                    self.session.limits.nr_of_phases_available =
                        enforcement.public_limits.nr_of_phases_available;
                }
                let mut effects = Vec::new();
                // `energyImpl.cpp:413` calls into the charger here, ahead of
                // everything else this handler publishes.
                match enforcement.switch_to_three_phases {
                    // Refused. `Charger.cpp:1528-1530` for a high level
                    // communication session, `energyImpl.cpp:424-429` for a
                    // board that cannot switch; both log and change nothing.
                    None => {}

                    // `Charger.cpp:1542`.
                    Some(PhaseSwitch::Direct(three_phases)) => {
                        effects.push(Effect::SwitchThreePhases(three_phases));
                    }

                    // `Charger.cpp:1532-1541`. The path owns the break, so the
                    // board call travels out of it rather than from here.
                    Some(PhaseSwitch::ThroughBreak(three_phases)) => effects
                        .extend(self.route_to_path(PathEvent::SwitchPhases { three_phases }, now)),
                }
                // Ahead of the enforced limits republish, which is the order
                // the C++ has: the countdown goes out at `energyImpl.cpp:491`
                // and `:497`, before `publish_enforced_limits` at `:513`. A
                // consumer reading the two together therefore never sees a
                // withheld limit before the countdown that explains it.
                if let Some(countdown) = enforcement.countdown {
                    effects.push(Effect::PublishRandomDelayCountdown(countdown));
                }
                effects.push(Effect::PublishEnforcedLimits(Box::new(
                    enforcement.published,
                )));
                // Published from `self.session.limits` rather than from the
                // enforcement, because a refused grant leaves the accepted
                // current standing behind the phase count that travelled with
                // it.
                effects.push(Effect::PublishLimits(self.session.limits));
                if valid_for_s > 0 {
                    effects.push(Effect::StartTimer {
                        id: TIMER_BUDGET_VALIDITY,
                        after: std::time::Duration::from_secs(valid_for_s as u64),
                    });
                    effects.extend(self.path.on_limits_changed(&self.session, now));
                    if let Some(hlc) = self.hlc.as_ref() {
                        effects
                            .extend(hlc.note_ac_current_limit(self.session.limits.max_current_a));
                    }
                }
                if let Some(dc) = enforcement.dc {
                    effects.push(Effect::HlcUpdate(HlcUpdate::DcMaximumLimits(dc.maximum)));
                    effects.push(Effect::HlcUpdate(HlcUpdate::DcMinimumLimits(dc.minimum)));
                    effects.extend(self.route_to_path(
                        PathEvent::DcEnforcedLimits {
                            maximum: dc.maximum,
                            minimum: dc.minimum,
                            exporting_to_grid: dc.exporting_to_grid,
                            reapply_target: dc.reapply_target,
                        },
                        now,
                    ));
                }
                effects
            }

            // Nothing is waiting on this one, so the verdict is dropped.
            Event::Command(ref command) => self.apply_command(command.clone(), now).0,

            // The caller is blocked on the verdict. Same `apply_command`, so
            // the awaited route cannot decide differently from the other one.
            Event::CommandAwaiting {
                ref command,
                reply,
            } => {
                let (mut effects, answer) = self.apply_command(command.clone(), now);
                // `main.rs` only awaits the five commands that return a
                // verdict, so a `None` is a boundary bug rather than a command
                // that legitimately answers nothing. Answering anyway keeps the
                // caller from waiting out its bound for nothing.
                let answer = answer.unwrap_or_else(|| {
                    log::error!("command {command:?} was awaited but returns no verdict");
                    false
                });
                effects.push(Effect::AnswerCommand { reply, answer });
                effects
            }

            // The publish timer belongs to no path: it rearms itself and
            // asks the energy manager again, which is the C++ detached thread
            // without the thread (`energyImpl.cpp:122-128`).
            Event::Timer { id, .. } if id == TIMER_ENERGY_FLOW_REQUEST => {
                self.request_energy(Urgency::Periodic)
            }

            // The soft overcurrent deadline. It belongs to no path: the
            // detector is above the trait because the raise it ends in has to
            // reach the fault set.
            Event::Timer { id, .. } if id == TIMER_BUDGET_VALIDITY => self.expire_budget(now),

            Event::Timer { id, .. } if id == TIMER_SOFT_OVER_CURRENT => {
                self.check_soft_over_current(now)
            }

            // `wait_for` returning on the timeout rather than on the
            // predicate. The sequence runs with whatever the meter has
            // reported, which is nothing, exactly as the C++ proceeds with
            // `initial_powermeter_value_received` still false.
            Event::Timer { id, .. } if id == TIMER_INITIAL_METER_VALUE => {
                if self.startup_held {
                    self.finish_startup(now)
                } else {
                    Vec::new()
                }
            }

            Event::Timer { id, .. } => self.path.on_timer(&self.session, id, now),

            Event::EffectDone { id, ref outcome } => {
                match self.answer_transaction_start(id, outcome) {
                    Some(effects) => effects,
                    None => self.path.on_effect_done(&self.session, id, outcome, now),
                }
            }

            // Error handling is mode independent and belongs above the path.
            // Raise and clear travel the same route, so neither can be
            // deliverable while the other is not.
            Event::Error(ref error) => self.note_error(error),

            // The DC supply's present output (`EvseManager.cpp:695-726`). Both
            // halves reach the vehicle; only the voltage reaches the path,
            // which is why `PathEvent::SupplyVoltage` carries one field.
            //
            // The vehicle is told first, matching the C++ order: `:716` sends
            // the present values and the cable check sequence reads
            // `powersupply_measurement` from its own thread.
            Event::SupplyVoltageCurrent {
                voltage_v,
                current_a,
            } => {
                self.energy.note_supply_voltage(voltage_v);
                // The measurement subscription is the second of the two the
                // C++ installs inside `if (hlc_enabled)` and
                // `if (charge_mode == "DC")`, so a port with no stack tells
                // the vehicle nothing and derates nothing. The energy tree's
                // note above and the path route below are not stack facts.
                let (mut effects, voltage_moved) = match self.hlc.as_mut() {
                    Some(hlc) => (
                        hlc.note_dc_present_values(voltage_v, current_a),
                        // External derating derives its current half from the
                        // present voltage and its power half likewise
                        // (`EvseManager.cpp:2689`), so a new measurement can
                        // move the derated ceiling. Refreshed only when it
                        // actually moved, which is never while no derate is
                        // set: this measurement arrives several times a second.
                        hlc.note_dc_present_voltage(voltage_v),
                    ),
                    None => (Vec::new(), false),
                };
                if voltage_moved {
                    self.refresh_derated_capabilities();
                }
                effects.extend(self.route_to_path(PathEvent::SupplyVoltage { voltage_v }, now));
                effects
            }

            Event::Isolation(reading) => self.route_to_path(PathEvent::Isolation(reading), now),
            Event::OverVoltageMeasurement { voltage_v } => {
                self.route_to_path(PathEvent::OverVoltageMeasurement { voltage_v }, now)
            }
            Event::IsolationSelfTest(passed) => {
                self.route_to_path(PathEvent::IsolationSelfTest(passed), now)
            }

            // The meter's power figure reaching the vehicle
            // (`EvseManager.cpp:1163-1169`), and nothing else. Nothing in this
            // module stores the reading. The C++ holds one field,
            // `latest_powermeter_data_billing` (`EvseManager.cpp:1175`), and
            // every read of it fills `meter_value` on an outgoing session event
            // (`evse/evse_managerImpl.cpp`, seven sites) or a ten second
            // telemetry record (`EvseManager.cpp:1369`). There is no per session
            // energy figure anywhere: no total taken at a session start, none
            // taken at its end, no difference between the two, and
            // `powermeter.yaml` gives `stop_transaction` a transaction id and
            // nothing else, so the billing record carries no energy either. The
            // boundary already republishes the reading it holds, which is the
            // same answer with one fewer hop. Holding a copy here would be state
            // nothing reads.
            //
            // The C++ handler has three consumers, of which one is
            // here and none is a store of the reading. The two missing ones are
            // the soft over current check fed by `set_current_drawn_by_vehicle`
            // (`:1153-1156`). Ceiling: the stack is told the live power but not
            // the full meter record `call_update_meter_info` carries
            // (`:1164`). Upgrade path: its own port with its own reader, and it
            // needs the whole wire type rather than the narrowing this event
            // carries, so it is not served by storing the reading here. Owner:
            // RsEvseManager.
            //
            // Soft overcurrent detection is the third consumer and is ported;
            // it takes the three phase currents (`:1157-1161`).
            Event::Meter(reading) => {
                // The record first and the power after, which is the C++ order
                // on consecutive lines (`EvseManager.cpp:1183` then `:1186`).
                let mut effects = self.hlc.as_ref().map_or_else(Vec::new, |hlc| {
                    let mut told = hlc.note_meter_record();
                    told.extend(hlc.note_ac_present_power(reading.power_w));
                    told
                });
                effects.extend(self.note_phase_currents(reading.phase_currents_a, now));
                // `subscribe_powermeter` feeds the plausibility comparison from
                // the meter's own DC figure, and only when it reports one.
                if let Some(voltage_v) = reading.dc_voltage_v {
                    effects.extend(
                        self.route_to_path(PathEvent::MeterVoltage { voltage_v }, now),
                    );
                }
                // Last, because the C++ notification is last: the subscriber
                // finishes with the reading before it wakes the startup
                // sequence, so a resume announced off this reading is
                // announced behind everything else the reading owed.
                effects.extend(self.note_initial_meter(now));
                effects
            }

            // The transcript and nothing else, which is the whole of
            // `EvseManager::log_v2g_message` (`EvseManager.cpp:1873-1889`).
            // The C++ gates the subscription itself on `config.session_logging`
            // (`:1048`) and then gates the handler on it again (`:1874`); the
            // everestrs subscription is static, so the single gate is the
            // logger, which discards a record it was not asked for and one that
            // arrives outside a session.
            Event::V2gMessage(ref message) => vec![Effect::SessionLog(SessionLogEffect::Record {
                origin: if message.from_vehicle() {
                    session_log::Origin::Car
                } else {
                    session_log::Origin::Evse
                },
                iso15118: true,
                msg: format!("V2G {}", message.id),
                payload: Some(Box::new(session_log::OwnedPayload {
                    xml: message.xml.clone(),
                    xml_hex: message.exi_hex.clone(),
                    xml_base64: message.exi_base64.clone(),
                    json: message.json.clone(),
                })),
            })],

            // `EvseManager::update_powermeter_capabilities`, whole: the store,
            // the session log line, and the push that merges the meter's
            // measurable floors into what the vehicle is offered
            // (`EvseManager::apply_powermeter_limits`).
            //
            // The C++ order, and both its gates. A report identical to the one
            // already held returns before the line, so `None` here writes
            // nothing at all. The line then leads the push, which is what an
            // operator reading the transcript against the wire trace expects.
            //
            // The energy tree is retold afterwards for the reason
            // `refresh_derated_capabilities` gives: it is the one reader that
            // keeps a copy of the report the enforced limits handler derives
            // the vehicle's limit set from, and a floor that never reached it
            // would narrow the capability message and leave the clamp wide.
            // The early return, the store and the transcript line are all
            // outside the C++ `hlc_enabled and charge_mode == "DC"` guard, so
            // they happen on every deployment and the memory they read is
            // `car_side_meter` rather than the port a basic AC deployment does
            // not have. Only the push below is the port's.
            Event::PowermeterCapabilities(ref caps) => {
                let Some(change) = self.car_side_meter.note(*caps) else {
                    return Vec::new();
                };
                let mut effects = vec![Effect::SessionLog(SessionLogEffect::Record {
                    origin: session_log::Origin::Evse,
                    iso15118: false,
                    msg: caps.transcript_line(),
                    payload: None,
                })];
                let floors = self.car_side_meter.floors();
                if let Some(hlc) = self.hlc.as_mut() {
                    effects.extend(hlc.note_floors(change, floors));
                }
                self.refresh_derated_capabilities();
                effects
            }

            Event::Hlc(ref hlc) => match hlc {
                // No effects, and no path involvement of its own: the fact is
                // read by two emissions the port owns. It is also one of the
                // three sources of the bidirectional resolution, which the
                // power path does read, so the resolution is refreshed here.
                HlcEvent::SelectedService(service) => {
                    if let Some(hlc) = self.hlc.as_mut() {
                        hlc.note_selected_service(*service);
                    }
                    // `Charger::set_hlc_d20_active` (`Charger.cpp:2135-2136`),
                    // the other statement of the same handler. A service
                    // selection is an ISO 15118-20 message, so its arrival is
                    // the whole of what the flag means; the C++ raises it
                    // without reading which service was chosen.
                    self.session.iso15118_20_active = true;
                    self.refresh_bidirectional();
                    Vec::new()
                }

                // `EvseManager.cpp:931-942`. The flag is raised and the
                // resolution and the V2H energy schedule follow it.
                HlcEvent::SaeBidiModeActive => {
                    if let Some(hlc) = self.hlc.as_mut() {
                        hlc.note_sae_bidi_active();
                    }
                    self.energy.note_sae_bidi_active();
                    self.refresh_bidirectional();
                    Vec::new()
                }

                // The autocharge identity taken from SLAC instead of from the
                // stack (`EvseManager.cpp:179-183`), which publishes a token
                // straight away rather than remembering one. The gate that
                // decides whether it does lives on the port.
                HlcEvent::VehicleMacAddress(mac_address) => self
                    .hlc
                    .as_mut()
                    .map_or_else(Vec::new, |hlc| hlc.note_vehicle_mac_address(mac_address)),

                HlcEvent::SessionSetup { evcc_id } => {
                    // The identity the autocharge token is built from, which
                    // the port derives once and holds until the next session
                    // setup replaces it, and the car manufacturer the same
                    // address names.
                    let mut effects = self
                        .hlc
                        .as_mut()
                        .map_or_else(Vec::new, |hlc| hlc.note_vehicle_identity(evcc_id));
                    effects.extend(self.route_to_path(PathEvent::HlcSessionSetup, now));
                    effects
                }
                // One assignment and no publish, which is the whole of the
                // C++ handler: the value reaches the wire on the next
                // announcement the protocol is published after.
                HlcEvent::SelectedProtocol(negotiated) => {
                    self.protocol.note_negotiated(negotiated.clone());
                    Vec::new()
                }
                HlcEvent::RequiresCableCheck => {
                    self.route_to_path(PathEvent::CableCheckRequired, now)
                }
                HlcEvent::PreChargeStarted => self.route_to_path(PathEvent::PreChargeStarted, now),
                HlcEvent::CurrentDemandStarted => {
                    self.route_to_path(PathEvent::CurrentDemandStarted, now)
                }
                // `subscribe_current_demand_finished` clears the SAE flag and
                // stops the charging phase after it, so the resolution is
                // refreshed before the path sees the event: the path reads the
                // fact off the session, and a stale one would hold the supply in
                // the import direction through the removal. The V2H schedule is
                // not released here; the C++ leaves
                // `external_local_energy_limits` standing.
                HlcEvent::CurrentDemandFinished => {
                    if let Some(hlc) = self.hlc.as_mut() {
                        hlc.note_current_demand_finished();
                    }
                    self.refresh_bidirectional();
                    self.route_to_path(PathEvent::CurrentDemandFinished, now)
                }
                HlcEvent::StopFromEv(_) => self.route_to_path(PathEvent::StopFromEv, now),

                HlcEvent::MatchingStarted(started) => {
                    // The same report writes `slac_unmatched`, which the unplug
                    // arm of `HlcPort::on_pilot` reads.
                    if let Some(hlc) = self.hlc.as_mut() {
                        hlc.note_matching_started(*started);
                    }
                    self.route_to_path(PathEvent::MatchingStarted(*started), now)
                }
                HlcEvent::SlacMatched(matched) => {
                    self.route_to_path(PathEvent::SlacMatched(*matched), now)
                }
                // Straight to the path, with nothing held here.
                // `EvseManager.cpp:1253-1256` calls the charger and keeps no
                // copy of the request, and the SLAC reset the C++ sends rides
                // inside the charger's own function
                // (`Charger.cpp:2140`), so only a request a state admitted
                // resets anything. Routing the reset from here instead would
                // send one from every state.
                HlcEvent::SlacErrorRoutine => {
                    self.route_to_path(PathEvent::SlacErrorRoutine, now)
                }
                HlcEvent::SetupFinished => self.route_to_path(PathEvent::SetupFinished, now),
                HlcEvent::AllowCloseContactor(allow) => {
                    self.route_to_path(PathEvent::AllowCloseContactor(*allow), now)
                }
                HlcEvent::OpenContactorDc => self.route_to_path(PathEvent::OpenContactorDc, now),

                // The three DC charge loop facts, each routed to the path
                // because the target, the clamp and the ramp all live there.
                HlcEvent::DcEvTarget {
                    voltage_v,
                    current_a,
                } => self.route_to_path(
                    PathEvent::DcEvTarget {
                        voltage_v: *voltage_v,
                        current_a: *current_a,
                    },
                    now,
                ),
                HlcEvent::DcDynamicChargeMode(request) => {
                    self.route_to_path(PathEvent::DcDynamicChargeMode(*request), now)
                }
                HlcEvent::DcEvMaximumLimits(maximum) => {
                    self.route_to_path(PathEvent::DcEvMaximumLimits(*maximum), now)
                }

                // The `ac_with_soc` flip trigger. It reaches the path like any
                // other vehicle fact: the C++ calls `switch_AC_mode` straight
                // from the subscription, and here the path that owns the mode
                // decides and raises the announcement as a duty.
                //
                // A DC port also accumulates the figure into the vehicle
                // record, ahead of the flip because the C++ registers that
                // `subscribe_dc_ev_status` first: the record one is in the DC
                // branch and the flip one is under `config.ac_with_soc`.
                HlcEvent::StateOfCharge { percent } => {
                    let mut effects = self
                        .hlc
                        .as_mut()
                        .map_or_else(Vec::new, |hlc| hlc.note_state_of_charge(*percent));
                    effects.extend(
                        self.route_to_path(PathEvent::StateOfCharge { percent: *percent }, now),
                    );
                    effects
                }

                // The stack is told directly and no path is involved, which is
                // what the C++ does at `EvseManager.cpp:1232-1238`.
                HlcEvent::DataLinkReady(ready) => self
                    .hlc
                    .as_ref()
                    .map_or_else(Vec::new, |hlc| hlc.on_data_link_ready(*ready)),

                // Both halves of a data link request, in the C++ order: the
                // charger is informed first (`EvseManager.cpp:375`, `:383`,
                // `:390`) and SLAC second (`:377`, `:384`, `:391`).
                HlcEvent::DataLinkError => self.on_data_link(DataLinkRequest::Error, now),
                HlcEvent::DataLinkPause => self.on_data_link(DataLinkRequest::Pause, now),
                HlcEvent::DataLinkTerminate => self.on_data_link(DataLinkRequest::Terminate, now),

                HlcEvent::SessionFailed(reason) => {
                    let uuid = self.session.id.clone();
                    self.hlc
                        .as_ref()
                        .map_or_else(Vec::new, |hlc| hlc.on_session_failed(uuid.clone(), *reason))
                }

                // The selected energy transfer mode is recorded
                // nowhere. Nothing below reads it, so narrowing it into
                // `PathEvent` would only move the drop one layer down.
                // Ceiling: a per session mode binding is not ported, so the
                // configured mode is the only one in effect. Upgrade path:
                // hold it on `Session`, which is where per session facts the
                // paths read already live. Owner: RsEvseManager.
                HlcEvent::ModeSelected { .. } => Vec::new(),

                // The two authorization requests. The permission the handlers
                // read is the authorization state machine's, which is why it is
                // passed in here rather than held by the port.
                HlcEvent::RequireAuthEim => {
                    let held = self.authorization_held();
                    self.hlc
                        .as_mut()
                        .map_or_else(Vec::new, |hlc| hlc.on_require_auth_eim(held))
                }
                HlcEvent::RequireAuthPlugAndCharge { token } => {
                    let held = self.authorization_held();
                    self.hlc.as_mut().map_or_else(Vec::new, |hlc| {
                        hlc.on_require_auth_plug_and_charge(held, token.clone())
                    })
                }
            },
        });

        // Every route into the path runs inside the match above, so one drain
        // per pass catches every edge the pass crossed. One drain feeding two
        // consumers, because draining twice would report each crossing to only
        // one of them.
        let crossed = self.path.take_entered_states();

        // Ahead of the duties, which is where the C++ writes it: the transition
        // line is the first thing a state machine pass emits
        // (`Charger.cpp:166`), before the entered state's body announces
        // anything. That order is what puts `Charger state: Finished->Idle`
        // above the `Session Finished` the `Idle` entry raises.
        effects.extend(self.log_state_transitions(&crossed));

        // The C++ discharges the same duties inside the entered state's
        // `if (initialize_state)` block, in the same tick.
        effects.extend(self.discharge_session_duties(hlc_active));

        // The `ChargingPausedEVSE` body's own work, at the pass boundary
        // because that is where the C++ does it: the announcement is raised
        // from the body of a loop that runs every 100 ms, so it sees whatever
        // the previous pass left standing rather than being owed by any one
        // route. Behind the duties, so the entry announcement has already
        // recorded the set this compares against and a pass that entered the
        // state does not announce it twice.
        effects.extend(self.reassess_pause_reasons());

        // Last, so the state the energy manager is told is the one the pass
        // settled on rather than one it passed through.
        effects.extend(self.request_energy_on_state_edge(&crossed));

        effects
    }

    /// A data link request, both halves.
    ///
    /// The path sees it first and the SLAC relay follows, which is the order
    /// each of the three C++ callbacks uses: `charger->dlink_*()` and then
    /// `r_slac[0]->call_dlink_*()`. The relay is unconditional there, so a path
    /// that can do nothing about the request still leaves the logical network.
    fn on_data_link(&mut self, request: DataLinkRequest, now: Instant) -> Vec<Effect> {
        let mut effects = self.route_to_path(PathEvent::DataLink(request), now);
        if let Some(hlc) = self.hlc.as_mut() {
            effects.extend(hlc.on_data_link(request));
        }
        // A terminate forgets the selected service (`EvseManager.cpp:388`),
        // which is one of the three sources. A pause keeps it, so the refresh
        // is a no-op there rather than a second branch.
        self.refresh_bidirectional();
        effects
    }

    /// Ask the energy manager for what this port needs, and keep the cadence
    /// running.
    ///
    /// The rearm travels with the publish rather than being armed once, so a
    /// missed or coalesced expiry cannot leave the port silent: every request
    /// schedules the next one.
    fn request_energy(&mut self, urgency: Urgency) -> Vec<Effect> {
        let request = self.energy.flow_request(Publish {
            charger_state: self.path.state(),
            bidirectional: self.session.profile.bidirectional,
            priority: urgency == Urgency::Priority,
        });
        vec![
            Effect::PublishEnergyFlowRequest(Box::new(request)),
            Effect::StartTimer {
                id: TIMER_ENERGY_FLOW_REQUEST,
                after: PUBLISH_INTERVAL,
            },
        ]
    }

    /// `energyImpl.cpp:130-137`: two transitions ask again straight away rather
    /// than waiting up to a second for the next periodic publish. Both are
    /// points where the budget is about to change hands, so a merged answer
    /// would arrive after the moment it was for.
    fn request_energy_on_state_edge(&mut self, crossed: &[AcState]) -> Vec<Effect> {
        if crossed
            .iter()
            .any(|state| matches!(state, AcState::WaitingForAuthentication | AcState::Finished))
        {
            self.request_energy(Urgency::Priority)
        } else {
            Vec::new()
        }
    }

    /// One transcript line per state the pass crossed, `Charger.cpp:166-168`.
    ///
    /// Named from the memory rather than from the crossings alone, because a
    /// pass can cross several states and the line names both ends of each edge.
    /// A crossing into the state already named is not an edge and is skipped,
    /// which is the `initialize_state` guard at `Charger.cpp:162`.
    ///
    /// The lines are produced whether or not a transcript is open. The logger
    /// discards a record that arrives outside a session, so the decision about
    /// what a session owes stays here and the decision about whether anything
    /// is listening stays there.
    fn log_state_transitions(&mut self, crossed: &[AcState]) -> Vec<Effect> {
        let mut effects = Vec::new();
        for &entered in crossed {
            let before = std::mem::replace(&mut self.logged_state, entered);
            if before == entered {
                continue;
            }
            effects.push(Effect::SessionLog(SessionLogEffect::evse(
                session_log::state_transition_message(before, entered),
            )));
        }
        effects
    }

    /// Carry out what the path's state edges raised.
    ///
    /// The path observes the edge and the core acts on it, because the session
    /// identity, the transaction flag and the authorization all live above
    /// `PowerPath`.
    /// `hlc_active` is `hlc_charging_active` as it stood before this pass drove
    /// the path, and deliberately not a fresh read.
    ///
    /// `Charger::run_state_machine` re-runs its switch until the state settles
    /// (`Charger.cpp:1092`), so one pass here is several C++ iterations. An
    /// unplug during a charge crosses `StoppingCharging`, `Finished` and `Idle`
    /// in one call, and the `Idle` entry is what clears the flag
    /// (`Charger.cpp:220`) while the `StoppingCharging` entry is what reads it
    /// (`:1014`). The C++ order puts the read first; a fresh read here would
    /// see the clear and ask the vehicle nothing at all on exactly the route
    /// that most needs it.
    fn discharge_session_duties(&mut self, hlc_active: bool) -> Vec<Effect> {
        let mut effects = Vec::new();
        for duty in self.path.take_session_duties() {
            match duty {
                SessionDuty::Publish(event) => {
                    // `Charger.cpp:803-804`: the `Charging` entry announces the
                    // start and forgets the last data link verdict in the same
                    // two statements, and this duty is that entry.
                    if event == SessionEvent::ChargingStarted {
                        if let Some(hlc) = self.hlc.as_mut() {
                            hlc.note_charging_started();
                        }
                    }
                    effects.extend(self.session_event(event));
                }
                SessionDuty::StartTransaction => effects.extend(self.start_transaction()),
                SessionDuty::StopTransaction => effects.extend(self.stop_transaction()),
                SessionDuty::EndSession => effects.extend(self.end_session()),
                SessionDuty::AskVehicleToStop => {
                    effects.extend(self.ask_vehicle_to_stop(hlc_active))
                }
                SessionDuty::AnnounceMode(mode) => {
                    // Only `AcWithSoc` raises this duty, and that path is
                    // refused without a stack wired (`config::resolve`), so a
                    // port with no port here never sees it.
                    if let Some(hlc) = self.hlc.as_ref() {
                        effects.extend(hlc.announce(mode));
                    }
                    // The AC half's whole announcement, which
                    // `setup_AC_mode`'s `else` is: it publishes nothing and
                    // writes this one field. Written here rather than inside
                    // `HlcPort::announce` because the field is not the stack's,
                    // and because the write happens on a port with no stack
                    // wired too - a deployment `config::resolve` refuses, so
                    // unreachable today, but the field would then be the only
                    // reason to reach for the port.
                    self.protocol.note_mode_announced(mode);
                }
            }
        }
        effects
    }

    /// The body of the `StoppingCharging` entry, `Charger.cpp:1013-1023`.
    ///
    /// ```cpp
    /// if (shared_context.hlc_charging_active) {
    ///     if (shared_context.hlc_d20_active and shared_context.flag_paused_by_evse) {
    ///         signal_hlc_pause_charging();
    ///     } else {
    ///         signal_hlc_stop_charging();
    ///     }
    /// } else {
    ///     cp_state_X1();
    /// }
    /// ```
    ///
    /// Mode independent, exactly as the C++ is: one state machine serves both
    /// charge modes and this block branches on neither. What differs by mode is
    /// the answer `hlc_charging_active` gives, which each power path supplies:
    /// `Dc` is always high level (`Charger.cpp:222-223`), `AcHlc` becomes so
    /// when the stack finishes setting the session up, and `AcBasic` never is.
    ///
    /// The `else` is not here. Withdrawing the pilot offer is a control pilot
    /// action with no DC meaning, so it stays on the path that owns the pilot:
    /// `AcHlc::emit` keeps the withdrawal it would otherwise filter out, and
    /// `AcBasic` never filters at all.
    fn ask_vehicle_to_stop(&mut self, hlc_active: bool) -> Vec<Effect> {
        if !hlc_active {
            return Vec::new();
        }
        let update = if self.session.iso15118_20_active && self.session.paused_by_evse {
            HlcUpdate::PauseCharging(true)
        } else {
            HlcUpdate::StopCharging(true)
        };
        vec![Effect::HlcUpdate(update)]
    }

    /// The one route into `PowerPath::on_path_event`, so the `Event` to
    /// `PathEvent` narrowing above is the only narrowing there is.
    fn route_to_path(&mut self, event: PathEvent, now: Instant) -> Vec<Effect> {
        self.path.on_path_event(&self.session, event, now)
    }

    /// `EvseManager.cpp:214-238` and `:545-546`. The report reaches the stack,
    /// re-derives the advertised set, and then installs the EVSE limit set and
    /// the cable check voltage input on the power path.
    ///
    /// The port is asked for the derived limits rather than being made to push
    /// them, so there is one derivation and the path holds a copy for the same
    /// reason `Charger` does: every clamp reads it from the state machine.
    ///
    /// **A deployment with no stack never hears this report at all.** Both C++
    /// subscriptions that carry it sit inside `if (hlc_enabled)` and inside
    /// `if (config.charge_mode == "DC")` (`:214-215` and `:528`-`:695`), so a
    /// port with a supply wired anyway hears neither. That was the outer half
    /// of `HlcPort::runs_dc_limits`, and it is the absence of the port now.
    fn on_supply_capabilities(
        &mut self,
        capabilities: &PowerSupplyCapabilities,
        now: Instant,
    ) -> Vec<Effect> {
        // ADR-0018. A withdrawal is a capability the supply no longer has
        // while the session still resolves to bidirectional. Read before the
        // report is applied, because the report is what changes the answer,
        // and asked of the session rather than of the previous report: a
        // supply that was never bidirectional withdraws nothing, and a session
        // that was never discharging has nothing to ramp down.
        let discharging = self.session.profile.bidirectional;
        let withdrawn = discharging && !capabilities.bidirectional;
        let floors = self.car_side_meter.floors();

        // The raw report is stored and the merged one is what leaves, which is
        // the split `get_powersupply_capabilities_for_hlc` makes: the stored
        // member stays raw so a relaxed derate restores it, while every reader
        // that is talking to the vehicle, the energy tree's copy among them,
        // takes the derated report with the car side meter's floors merged in.
        // ADR-0018 as revised. The refusal tracks the capability the supply
        // reports now, so a report that carries it again lifts the refusal and
        // the same session may discharge once more. Nothing is routed to the
        // path on the way back: the ramp up is the one the C++ runs on every
        // rise (`EvseManager.cpp:2681-2703`), and the path takes it on the
        // vehicle's next target because the ramp down left the ramp position
        // at zero. There is no resume to announce.
        if capabilities.bidirectional
            && self
                .hlc
                .as_mut()
                .is_some_and(|hlc| hlc.release_discharge_withdrawal())
        {
            self.refresh_bidirectional();
        }

        let Some(from_stack) = self.hlc.as_mut().map(|hlc| {
            (
                hlc.note_dc_capabilities(*capabilities, floors),
                hlc.for_hlc(*capabilities, floors),
                withdrawn && hlc.withdraw_discharge(),
                (hlc.dc_min_export_voltage_v(), hlc.dc_max_export_voltage_v()),
            )
        }) else {
            return Vec::new();
        };
        let (mut effects, for_hlc, discharge_withdrawn, (min_export_voltage_v, max_export_voltage_v)) =
            from_stack;

        self.energy.note_supply_capabilities(for_hlc);
        if discharge_withdrawn {
            self.refresh_bidirectional();
            effects.extend(self.route_to_path(PathEvent::BidirectionalWithdrawn, now));
        }
        effects.extend(self.route_to_path(
            PathEvent::DcExportVoltageRange {
                min_v: min_export_voltage_v,
                max_v: max_export_voltage_v,
            },
            now,
        ));
        effects
    }

    /// Ported from `EvseManager::ready_to_start_charging` and the call gate
    /// just above it (`EvseManager.cpp:1491-1516`).
    fn on_startup(&mut self, now: Instant) -> Vec<Effect> {
        // Everything the ISO 15118 stack is told once, which the C++ sends from
        // `init` (`EvseManager.cpp:971-995`) and therefore before anything the
        // charger does. It is ahead of the meter wait for the same reason.
        let fake_dc = self.path.presents_fake_dc();
        let floors = self.car_side_meter.floors();
        let mut effects = self
            .hlc
            .as_mut()
            .map_or_else(Vec::new, |hlc| hlc.boot(fake_dc, floors));
        // `energyImpl::ready` asks once with priority and then starts the
        // repeating publish (`energyImpl.cpp:116-128`). The first ask is
        // outside the `external_ready_to_start_charging` gate in the C++ too:
        // a port waiting for that signal still participates in the energy tree.
        //
        // Ahead of the meter wait as well, and that is a choice rather than a
        // copy: the wait is inside `EvseManager::ready` and this ask is inside
        // a different implementation's `ready`, so the C++ fixes no order
        // between the two and `ld-ev` decides it. Asking first keeps a port
        // whose meter never reports from being absent from the energy tree for
        // the whole of the timeout.
        effects.extend(self.request_energy(Urgency::Priority));
        // `EvseManager::ready`'s `powermeter_cv.wait_for` is here, and
        // everything below is what it waits for: the resume announcement, the record closure and the
        // ready report all read the meter or are read by a consumer that does.
        if self.initial_meter_timeout > Duration::ZERO && !self.initial_meter_seen {
            self.startup_held = true;
            effects.push(Effect::StartTimer {
                id: TIMER_INITIAL_METER_VALUE,
                after: self.initial_meter_timeout,
            });
            return effects;
        }
        effects.extend(self.finish_startup(now));
        effects
    }

    /// Everything `EvseManager::ready` does after the meter wait
    /// (`EvseManager.cpp:1502-1520`), and `ready_to_start_charging` behind it.
    ///
    /// Split out rather than left in `on_startup` because it has three
    /// callers: the boot that did not have to wait, the reading that ends the
    /// wait, and the deadline that ends it instead. `startup_held` is what
    /// keeps the three from running it twice.
    fn finish_startup(&mut self, now: Instant) -> Vec<Effect> {
        self.startup_held = false;
        // `EvseManager.cpp:1485` cleans up before the ready sequence at `:1491`,
        // and outside the `external_ready_to_start_charging` branch, so no
        // consumer sees the module up while a record left in the meter across
        // the restart is still open.
        let mut effects = self.recover_interrupted_transaction();
        // The second half of `Charger::cleanup_transactions_on_startup`
        // (`Charger.cpp:1505-1511`), outside the recovered-record branch there
        // and here: whatever the meter still holds under a name this module
        // cannot match is closed anyway, and no `transactionFinished` can be
        // announced for it.
        effects.push(Effect::CancelAllTransactions);
        effects.push(Effect::PublishWaitingForExternalReady(
            self.ready.awaits_external_signal,
        ));
        if !self.ready.awaits_external_signal {
            effects.extend(self.announce_ready(now));
        }
        effects
    }

    /// The first billing meter reading has arrived, which is what
    /// `subscribe_powermeter` notifies `powermeter_cv` about
    /// (`EvseManager.cpp:1196-1200`).
    ///
    /// Behind the reading's own work, as the C++ notification is: the
    /// subscriber records and republishes the reading and then wakes whoever
    /// is waiting.
    fn note_initial_meter(&mut self, now: Instant) -> Vec<Effect> {
        self.initial_meter_seen = true;
        if !self.startup_held {
            return Vec::new();
        }
        let mut effects = vec![Effect::CancelTimer {
            id: TIMER_INITIAL_METER_VALUE,
        }];
        effects.extend(self.finish_startup(now));
        effects
    }

    /// Close a transaction the previous run was interrupted with.
    ///
    /// The first half of `Charger::cleanup_transactions_on_startup`
    /// (`Charger.cpp:1480-1503`) together with the resumed announcement that
    /// precedes it (`EvseManager.cpp:1478-1481`). Both read the same record;
    /// the C++ reads the store twice with the clear in between, so this takes
    /// the recovered uuid once and both halves work from it.
    ///
    /// The order of the four is the C++ order and each step depends on it:
    ///
    /// 1. `SessionResumed`, so a CSMS learns this module knows about the
    ///    interrupted session before anything closes it and no separate
    ///    cleanup of its own is needed. It carries the recovered uuid.
    /// 2. The record is removed BEFORE the meter is asked to close
    ///    (`Charger.cpp:1483`, ahead of the loop at `:1486`). A recovery that
    ///    died on the meter call would otherwise find the same record on the
    ///    next boot and retry it forever.
    /// 3. The meter record is closed under the recovered name, which is the
    ///    only reason the name was persisted at all.
    /// 4. `TransactionFinished`, so the transaction ends for OCPP rather than
    ///    hanging until the CSMS times it out.
    ///
    /// An absent, empty or unreadable record reaches this as `None` and it does
    /// nothing, raising no error: `PersistentStore::get_session` collapses all
    /// three to `{}` (`PersistentStore.cpp:30-42`) and both C++ readers test
    /// only emptiness. A store that cannot be read must not stop the charger
    /// from starting.
    fn recover_interrupted_transaction(&mut self) -> Vec<Effect> {
        let Some(session_uuid) = self.persist.take_recovered() else {
            return Vec::new();
        };
        log::info!("cleaning up transaction with UUID {session_uuid} on start up");
        vec![
            // Built here rather than through `session_event`, which stamps the
            // current session's identity: there is no session at startup, and
            // this event's uuid is the recovered one. Its trigger list does not
            // name this event either, so nothing is skipped by not going
            // through it.
            Effect::PublishSessionEvent(SessionEventReport {
                uuid: session_uuid.clone(),
                event: SessionEvent::SessionResumed,
                started: None,
                payload: None,
            }),
            self.persist.clear(),
            Effect::StopTransaction {
                transaction_id: session_uuid,
            },
            // Through `session_event`, so the identity comes from the session
            // as it does for every other announcement. There is no session, so
            // the uuid is empty, which is what the C++ publishes here too:
            // `evse_managerImpl.cpp:276` fills it from
            // `charger->get_session_id()` and `shared_context.session_uuid` has
            // never been set this early.
            //
            // `PowerLoss`, which `Charger.cpp:1577` names here and nowhere
            // else. It is the one reason a consumer cannot derive: it is how a
            // CSMS learns the transaction ended because the charger lost power
            // rather than because a driver unplugged. Named literally rather
            // than through `Session::stop_named`, because there is no session
            // at startup to have recorded it.
            Effect::PublishSessionEvent(SessionEventReport {
                uuid: self.session.id.clone().unwrap_or_default(),
                event: SessionEvent::TransactionFinished,
                started: None,
                payload: Some(SessionPayload::TransactionFinished {
                    reason: StopTransactionReason::PowerLoss,
                    id_tag: None,
                }),
            }),
        ]
    }

    /// Announce the boot enable state, then `ready`, in that order.
    ///
    /// The order is the point. The C++ comment at `EvseManager.cpp:1509-1510`
    /// says other modules retrieve the enable state on startup, so it has to be
    /// observable before `ready` tells them the module is up.
    fn announce_ready(&mut self, now: Instant) -> Vec<Effect> {
        if self.ready_at.is_some() {
            // A second call warns and returns in the C++ too
            // (`EvseManager::ready_to_start_charging`'s `charger_ready`
            // guard).
            return Vec::new();
        }
        // `EvseManager.cpp:1507`, which the C++ stamps inside this guard as
        // well, so a refused second call does not move the instant the random
        // delay measures its startup window from.
        self.ready_at = Some(now);

        let (enabled, _) = self.enable.resolve();
        let event = self.enable.publish_initial_state(!enabled);
        let mut effects = vec![
            // The advertised energy transfer mode set, ahead of the enable
            // announcement because that is where `:1531` puts it and other
            // modules read the enable state on startup.
            Effect::PublishSupportedTransferModes(self.advertised_transfer_modes()),
        ];
        // `charger->run()` (`EvseManager.cpp:1534`), which is what starts the
        // state machine. **Here and not in `on_startup`**: it is inside this
        // function in the C++, so a port configured to wait for an external
        // signal has no state machine running until the signal arrives. Gating
        // the announcement alone left the path enabled, its control pilot
        // output on and a plug in able to actuate, on a port that had been told
        // to wait.
        //
        // The board enable is the path's own first output rather than a literal
        // here, so the path learns it started: one that does not never leaves
        // its startup state and refuses every plug in that follows.
        effects.extend(self.path.on_startup());
        effects.extend(self.announce_enable(event, self.enable.active_source()));
        effects.push(Effect::PublishReady(true));
        // `EvseManager.cpp:1542-1544`, the last statement of
        // `ready_to_start_charging`: a port that is ready with nothing heard
        // from its meter says so once. It is behind the ready report there, so
        // the operator reads it as a qualification of a port that did come up
        // rather than as a failure to come up.
        if !self.initial_meter_seen {
            log::warn!("no powermeter value received yet");
        }
        effects
    }

    /// Record one source's availability report and act on the decision.
    ///
    /// Ported from `Charger::enable_disable` at `Charger.cpp:1750-1774`. The
    /// announcement precedes the teardown there and here.
    fn apply_enable_disable(
        &mut self,
        entry: EnableEntry,
        scope: EnableScope,
        now: Instant,
    ) -> Vec<Effect> {
        let decision = self.enable.update(entry, scope, self.session.phase);

        let mut effects = Vec::new();
        if let Some(event) = decision.event {
            effects.extend(self.announce_enable(event, decision.active_source));
        }

        // `Charger::enable_disable` records the wire reason beside arming the
        // teardown, and the position is the whole of it: `Charger.cpp:1896-1897`
        // sits in the `else` of `if (is_enabled)` inside the state-changed
        // guard, so **every** state changing disable records it whatever phase
        // the port is in - not only the three that have a session to stop. That
        // is what puts `EVSEDisabled` on the `TransactionFinished` rather than
        // the `EVDisconnected` an unnamed stop defaults to.
        //
        // Unguarded on `transaction_active` there and unguarded here, which is
        // what makes the reset at the transaction start load bearing: a disable
        // on an idle port records a reason no transaction of its own will ever
        // use, and `Session::clear_stop` is what stops the next vehicle's
        // transaction ending under it.
        if decision.event == Some(SessionEvent::Disabled) {
            self.session
                .record_stop(StopTransactionReason::EvseDisabled, None);
        }

        match decision.next_phase {
            // A disable stops a live session rather than dropping power, with
            // the reason the C++ records at `Charger.cpp:1765`.
            Some(SessionPhase::Stopping) => {
                effects.extend(self.stop(StopReason::EvseDisabled, now))
            }
            Some(phase) => self.session.phase = phase,
            None => {}
        }

        // The availability change itself, handed to the path last so an
        // occupied port is routed through stopping before the port is taken
        // out of service. What travels is the arbitration answer rather than
        // the report that caused it: a losing report leaves the port as it was
        // and reaches nobody.
        //
        // The asymmetry is preserved: taking the port out of service is not
        // scope gated (`Charger.cpp:1756-1769`), while putting it back is,
        // because connector state is assigned only for a connector scoped
        // request (`:1722-1724`) and the restart is guarded on it (`:1726`), so
        // an EVSE scoped enable announces the change and leaves a disabled
        // connector stopped.
        let resolved = match decision.event {
            Some(SessionEvent::Enabled) if self.enable.connector_enabled() => {
                Some(PathEvent::Enable)
            }
            Some(SessionEvent::Disabled) => Some(PathEvent::Disable),
            _ => None,
        };
        if let Some(event) = resolved {
            if event == PathEvent::Disable {
                // An unavailable port reads its own control pilot level in the
                // C++ (`IECStateMachine.cpp:155-163`). Without this the level
                // the pilot was resting at when service ended survives, and the
                // next arrival looks like a level already resident.
                self.cp.note_disabled();
            }
            effects.extend(self.route_to_path(event, now));
        }

        effects
    }

    /// The advertised energy transfer mode set, published ahead of `ready`
    /// exactly where `EvseManager::ready_to_start_charging` publishes it
    /// (`EvseManager.cpp:1505`).
    ///
    /// The set is derived from connector type, live board support or power
    /// supply capability and the AC BPT setting; see `hlc`. It is empty for a
    /// deployment without high level communication, which is the set the C++
    /// leaves its monitor holding and publishes here regardless.
    pub fn advertised_transfer_modes(&self) -> Vec<EnergyTransferMode> {
        self.hlc
            .as_ref()
            .map_or_else(Vec::new, |hlc| hlc.advertised().to_vec())
    }

    /// Runs one command and answers the verdict its interface returns on the
    /// wire, or `None` for the commands that return nothing.
    ///
    /// The verdict is produced by the same call that makes the decision, which
    /// is what stops the two drifting apart. Five commands have one:
    /// `pause_charging`, `resume_charging` and `stop_transaction` answer
    /// whether a transaction was open (`Charger.cpp:1330-1345`, `:1367-1394`),
    /// `reserve` answers its accept verdict, and `enable_disable` answers the
    /// *arbitrated* state rather than the requested one (`:1697-1774`).
    /// `force_unlock` is not among them: the C++ answers it from whether a
    /// connector lock is wired (`evse/evse_managerImpl.cpp:491-500`), which is
    /// a boundary fact and is answered at the boundary.
    fn apply_command(&mut self, command: Command, now: Instant) -> (Vec<Effect>, Option<bool>) {
        let mut answer = None;
        let effects = match command {
            // `evse_managerImpl::handle_authorize_response`
            // (`evse/evse_managerImpl.cpp:424-455`). The bridge decides the
            // route, because both rules read what the vehicle is waiting for
            // and nothing else here knows that.
            Command::AuthorizeResponse {
                token,
                status,
                certificate,
                tariff,
                reservation_id,
            } => {
                let kind = AuthorizationKind::of(&token);
                let verdict = Verdict {
                    kind,
                    status,
                    certificate,
                };
                // Only the plug and charge wait is a stack fact, and nothing
                // can be waiting for one without a stack; see `authz::route`.
                let route = match self.hlc.as_ref() {
                    Some(hlc) => hlc.route_verdict(verdict),
                    None => crate::core::hlc::authz::route(false, verdict),
                };
                match route {
                    Route::Ignore => {
                        log::info!(
                            "an authorization other than plug and charge arrived while the \
                             vehicle waits for plug and charge, no effect"
                        );
                        Vec::new()
                    }

                    // A refusal never reaches the authorization state machine.
                    // `Charger::authorize` is called from the accepted branch
                    // alone (`:436`), so a refused authorization leaves a live
                    // session charging, and the comment at `:449-450` says why:
                    // a successful one may still arrive later. Answering the
                    // vehicle is all a refusal does here, and only a contract
                    // refusal carries an answer.
                    Route::Refused(response) => response
                        .map(|response| {
                            Effect::HlcUpdate(HlcUpdate::AuthorizationResponse(response))
                        })
                        .into_iter()
                        .collect(),

                    Route::Grant => {
                        let mut effects = self.grant_authorization(token, tariff, now);
                        // Last, exactly as `:461` is last. A grant can open a
                        // session, and that session has already announced
                        // itself by now, so `SessionStarted` names whatever
                        // reservation this module held *before* the verdict and
                        // never the one the verdict brought. That ordering is
                        // the whole reason an authorization first session
                        // spends its reservation on the start while the id a
                        // verdict carries reaches the transaction instead.
                        if let Some(id) = reservation_id {
                            // `mod->reserve(id, false)`: the same function
                            // with the announcement off, so the silent route
                            // cannot drift from the one that speaks.
                            effects.extend(self.reserve(id, false).0);
                        }
                        effects
                    }
                }
            }

            Command::WithdrawAuthorization => {
                let context = self.auth_context();
                let signals = self.auth.withdraw(self.path.state(), context);
                self.apply_auth_signals(&signals, now)
            }

            // The C++ drops the authorization along with the transaction
            // (`Charger.cpp:1373-1374`), which is what the per state poll below
            // then reads to complete the stopping route.
            Command::StopTransaction { reason, id_tag } => {
                // Read before the cancellation, which is where
                // `Charger::cancel_transaction` reads it: the whole of that
                // method sits inside the `flag_transaction_active` guard and it
                // returns false when the guard does not hold.
                answer = Some(self.session.transaction_active);
                self.cancel_transaction(reason, id_tag, now)
            }

            // Already relabelled and already accepted at the boundary, which
            // is where the verdict has to be decided because the command
            // answers its caller synchronously. What is left is the call.
            Command::UpdateAllowedTransferModes(modes) => {
                vec![Effect::HlcUpdate(HlcUpdate::TransferModes(modes))]
            }

            // The C++ setters write three atomics and stop
            // (`EvseManager.cpp:1770-1780`). Nothing is re-derived until the
            // next trigger point, so a change made mid session reaches the
            // vehicle at the next session start or finish and not before.
            Command::SetPlugAndChargeConfiguration(request) => {
                if let Some(hlc) = self.hlc.as_mut() {
                    hlc.configure_plug_and_charge(&request);
                }
                Vec::new()
            }

            // `EvseManager::set_external_derating` (`EvseManager.cpp:2701-2704`)
            // stores the request under a lock and returns, emitting nothing.
            // Emits nothing here either. The C++ re-derives on every read of
            // `get_powersupply_capabilities()`, and the one reader that keeps a
            // copy rather than re-reading is the energy tree, so the refresh is
            // a state update with no effects of its own.
            //
            // No capability report reaches the vehicle: the forward is gated on
            // the raw report, which a derate does not touch.
            // `EvseManager::set_external_derating` (`EvseManager.cpp:2826-2838`).
            // The stack is retold on a request that changed, and the one
            // reader that keeps a copy of the report is retold when the
            // derated report moved with it.
            Command::SetExternalDerating(requested) => {
                let floors = self.car_side_meter.floors();
                let (effects, moved) = self
                    .hlc
                    .as_mut()
                    .map_or_else(|| (Vec::new(), false), |hlc| {
                        hlc.note_external_derating(requested, floors)
                    });
                if moved {
                    self.refresh_derated_capabilities();
                }
                effects
            }

            // Already accepted at the boundary, which is where the `NoHlc`
            // answer has to be decided because the command answers its caller
            // synchronously. What is left is the store and, on an AC port, the
            // recompute of the advertised set
            // (`evse/evse_managerImpl.cpp:571-574`).
            Command::SetDerAvailable(available) => self
                .hlc
                .as_mut()
                .map_or_else(Vec::new, |hlc| hlc.set_der_available(available)),

            // `evse_managerImpl::handle_force_unlock`
            // (`evse/evse_managerImpl.cpp:515-524`) cancels the transaction as
            // an `UnlockCommand` and only then opens the lock. Both halves
            // matter and so does their order: the record has to close while
            // the connector is still held, or the operator's unlock is billed
            // inside the session it ended.
            //
            // The cancellation is what brings the port down, through the
            // stopping route the C++ takes on `flag_externally_cancelled`
            // rather than through the error shutdown this arm used to raise.
            Command::ForceUnlock => {
                // `handle_force_unlock` calls `cancel_transaction`, ignores what
                // it answers, and opens the lock regardless. The cancellation
                // guards itself, so an unlock that finds no record open changes
                // nothing about the session and still unlocks.
                let mut effects =
                    self.cancel_transaction(StopTransactionReason::UnlockCommand, None, now);
                effects.push(Effect::UnlockConnector);
                effects
            }

            // `evse_managerImpl::handle_reserve` (`:481-483`) is
            // `mod->reserve(id, true)` and nothing else, so every refusal and
            // the announcement rule are `Core::reserve`'s.
            Command::Reserve { reservation_id } => {
                let (effects, accepted) = self.reserve(reservation_id, true);
                answer = Some(accepted);
                effects
            }

            Command::CancelReservation => self.cancel_reservation(),

            Command::EnableDisable {
                source,
                state,
                priority,
                scope,
            } => {
                let effects = self.apply_enable_disable(
                    EnableEntry {
                        source,
                        state,
                        priority,
                    },
                    scope,
                    now,
                );
                // The arbitrated state, which is what `Charger::enable_disable`
                // returns (`:1774`) and not the state that was requested.
                // `resolve` is pure on the table, so reading it back after the
                // update yields the same `is_enabled` the update computed.
                answer = Some(self.enable.resolve().0);
                effects
            }

            // The deployment asked for the ready publish to wait for this
            // command (`EvseManager.cpp:1493-1495`). Reaching it without that
            // configuration is the second call the C++ warns about, and
            // `announce_ready` is idempotent for the same reason.
            Command::ExternalReadyToStartCharging => self.announce_ready(now),

            // `evse_managerImpl.cpp:474-480` hands both straight to
            // `Charger`, which acts on them through one flag its state machine
            // reads. Here the fact travels to the path, which is what holds the
            // state machine.
            //
            // The interface returns a bool and the C++ answers false when no
            // transaction is active (`Charger.cpp:1330-1345`). That refusal is
            // not expressible on this route: commands are posted onto the
            // single event queue and the caller has returned before the core
            // sees them, so `main.rs` answers true for accepted rather than for
            // acted upon. A path that cannot act on the request produces no
            // effects, which is the same outcome the C++ false reaches.
            //
            // The flag itself is the core's, not the path's: `Charger.cpp:1332`
            // and `:1340` write it outside the state machine and under the same
            // `flag_transaction_active` guard, and the stopping entry reads it
            // for either charge mode (`:1015`). A path that does nothing with
            // the request still leaves the flag standing, which is what the C++
            // does too.
            Command::PauseCharging => {
                answer = Some(self.session.transaction_active);
                if self.session.transaction_active {
                    self.session.paused_by_evse = true;
                }
                self.route_to_path(PathEvent::PauseRequested, now)
            }
            Command::ResumeCharging => {
                answer = Some(self.session.transaction_active);
                if self.session.transaction_active {
                    self.session.paused_by_evse = false;
                }
                // `Charger.cpp:1015-1024`. The C++ arm re-reads its three pause
                // reasons every pass and wakes SLAC on the pass that finds none
                // left; here the reducer settles the request in one call, so
                // the wake up is owed exactly when the state that guards it was
                // left. Reading the state on both sides of the route rather
                // than the request itself is what keeps a resume the reducer
                // refused from sending one.
                let was_paused_by_evse = self.path.state() == AcState::ChargingPausedEvse;
                let mut effects = self.route_to_path(PathEvent::ResumeRequested, now);
                if was_paused_by_evse && self.path.state() != AcState::ChargingPausedEvse {
                    if let Some(hlc) = self.hlc.as_ref() {
                        effects.extend(hlc.on_session_resume());
                    }
                }
                effects
            }

            // The four `uk_random_delay` handlers write module state and
            // return (`random_delay/uk_random_delayImpl.cpp:15-31`). No effect
            // follows any of them: the countdown is published from the
            // enforced limits handler, so what a consumer sees of an enable,
            // a cancel or a new maximum is the next enforced limit.
            Command::RandomDelayEnable => {
                self.energy.random_delay_mut().enable();
                Vec::new()
            }
            Command::RandomDelayDisable => {
                self.energy.random_delay_mut().disable();
                Vec::new()
            }
            Command::RandomDelayCancel => {
                self.energy.random_delay_mut().cancel();
                Vec::new()
            }
            Command::RandomDelaySetDuration(seconds) => {
                self.energy.random_delay_mut().set_duration_s(seconds);
                Vec::new()
            }
        };
        (effects, answer)
    }

    /// What `Auth` needs to know that it does not own.
    fn auth_context(&self) -> AuthContext {
        let (enabled, _) = self.enable.resolve();
        AuthContext {
            session_active: self.session.session_active,
            externally_cancelled: self.session.externally_cancelled,
            disable_requested: !enabled,
        }
    }

    /// What the authorization bridge reads off the authorization state machine
    /// and the session (`Charger::get_authorized_pnc`, `get_authorized_eim`
    /// and the `ready` half of `get_authorized_eim_ready_for_hlc`).
    fn authorization_held(&self) -> AuthorizationHeld {
        AuthorizationHeld {
            eim: self.auth.authorized_eim(),
            pnc: self.auth.authorized_plug_and_charge(),
            // The charger's state and not the session's phase, which is what
            // `get_authorized_eim_ready_for_hlc` reads (`Charger.cpp:1746-1748`).
            charging: matches!(
                self.path.state(),
                AcState::Charging | AcState::ChargingPausedEv | AcState::ChargingPausedEvse
            ),
        }
    }

    /// Refresh the session's view of the authorization from `Auth`, which is
    /// the only code that writes either field.
    ///
    /// `Auth` owns the identity and the permission; these two are the read
    /// model the power path, the metering start and the session event payloads
    /// see, the same way `limits` is. Refreshed from `Auth` in one place rather
    /// than assigned per signal, and the reason is an order:
    /// `Charger::authorize` assigns `shared_context.id_token` at `:1768`,
    /// before it calls `start_session` at `:1772`, so an authorization first
    /// session's `SessionStarted` names the token that opened it. Assigned in
    /// the `Authorized` arm - one signal after the `StartSession` that
    /// publishes the start - it named none, which a payload test caught.
    fn mirror_authorization(&mut self) {
        self.session.authorized_token = self.auth.token().cloned();
        self.session.authorized_plug_and_charge = self.auth.authorized_plug_and_charge();
        self.session.authorized_tariff = self.auth.tariff().clone();
    }

    /// Turn authorization signals into effects and session state.
    ///
    /// The order signals arrive in is the order the C++ acts in, so this walks
    /// them rather than reordering.
    fn apply_auth_signals(&mut self, signals: &[AuthSignal], now: Instant) -> Vec<Effect> {
        // `Auth` has already spoken by the time the signals it produced are
        // walked, so the read model is in place before the first of them can
        // publish anything. See `mirror_authorization`.
        self.mirror_authorization();
        let mut effects = Vec::new();
        for signal in signals {
            match signal {
                // The authorization was the first user interaction, so this is
                // the `authfirst` branch of `Charger::start_session`
                // (`Charger.cpp:1384`). A plug in start reaches
                // `start_session` with the other reason.
                AuthSignal::StartSession => {
                    match self.start_session(StartSessionReason::Authorized) {
                        Ok(started) => effects.extend(started),
                        // The signals that follow a start all belong to the
                        // session it opens, so none of them is applied: an
                        // `Authorized` acted on here would open a transaction
                        // with no session behind it.
                        Err(EntropyExhausted) => {
                            effects.extend(self.refuse_session_start());
                            return effects;
                        }
                    }
                }

                // `Charger::authorize`'s `signal_simple_event(Authorized)`
                // (`Charger.cpp:1772`) and the permission reaching the state
                // machine, which is all the C++ does here.
                //
                // The transaction is not opened from this signal. Its two C++
                // call sites are both inside
                // `case EvseState::WaitingForAuthentication`, a state entered
                // only from a plug in, so an authorization that arrives before
                // the vehicle grants permission and then waits for it. The path
                // below holds that state machine and `duties_for_edge` raises
                // `StartTransaction` on the one transition it makes, so the
                // announcement is owed by the transition rather than decided
                // twice.
                AuthSignal::Authorized => {
                    self.session.phase = SessionPhase::Authorized;
                    effects.extend(self.session_event(SessionEvent::Authorized));
                    effects.extend(self.path.on_authorized(&self.session, now));
                }

                AuthSignal::Deauthorized => {
                    effects.extend(self.session_event(SessionEvent::Deauthorized));
                }

                // `Charger::stop_session` ends the session outright
                // (`Charger.cpp:1391-1397`). It is only reached from the three
                // states with no power flowing, so there is nothing to ramp down
                // first.
                AuthSignal::StopSession => {
                    // The two fields are already lowered: `Auth::deauthorize`
                    // drops the permission and the identity before it pushes
                    // this signal, and the refresh above carried that.
                    effects.extend(self.stop(StopReason::DeAuthorized, now));
                    self.session.phase = SessionPhase::Finished;
                    self.session.session_active = false;
                    self.session.externally_cancelled = false;
                    self.auth.clear();
                    effects.extend(self.finish_session());
                }

                AuthSignal::AuthorizationTimeout => {
                    effects.extend(self.session_event(SessionEvent::PluginTimeout));
                }

                AuthSignal::HlcAuthorizationTimeout => {
                    effects.push(Effect::HlcUpdate(HlcUpdate::AuthorizationResponse(
                        AuthorizationResponse::TIMED_OUT,
                    )));
                }

                // `Charger::deauthorize_internal` raises this on `p_evse` and
                // then re-examines the fault set (`Charger.cpp:1670-1671`,
                // `ErrorHandling.cpp:337-343`). The gating on `raise_mrec9`
                // lives in `Auth`, so reaching this arm at all is the decision.
                AuthSignal::RaiseAuthorizationTimeout => {
                    effects.extend(self.raise_own_error(
                        faults::MREC9_AUTHORIZATION_TIMEOUT,
                        faults::MREC9_DESCRIPTION,
                        Severity::Medium,
                    ));
                }

                // Cancelling a consumed reservation is the reservation task's,
                // which owns `Command::Reserve` and the reservation timer.
                AuthSignal::ReservationConsumed => {}
            }
        }
        effects
    }

    /// The per state look at the authorization state, ported from
    /// `Charger.cpp:690`, `:781`, `:880`, `:942` and `:1042`.
    ///
    /// Called by whichever transition re-examines the session, which is why it
    /// is a read plus an action rather than a callback: the C++ re-reads
    /// `flag_authorized` on every pass through its state machine.
    pub fn poll_authorization(&mut self, now: Instant) -> Vec<Effect> {
        match self.auth.poll(self.session.phase) {
            AuthPoll::Continue => Vec::new(),
            AuthPoll::Stop => self.stop(StopReason::DeAuthorized, now),
            // `Charger.cpp:1043` only changes state. The session itself ends
            // when the cable comes out, not here, which is why the cancellation
            // flag survives this transition.
            AuthPoll::Finish => {
                self.session.phase = SessionPhase::Finished;
                Vec::new()
            }
        }
    }

    /// An error reported by one of this module's peers. It is already raised on
    /// their interface, so only the fault set is fed.
    fn note_error(&mut self, error: &ErrorEvent) -> Vec<Effect> {
        let signals = self.faults.apply_error_event(error);
        self.apply_fault_signals(signals)
    }

    /// An error this module raises on its own `evse_manager` interface. Every
    /// `raise_*` in `ErrorHandling.cpp` raises and then re-examines the fault
    /// set, so both halves happen here rather than at the call site.
    ///
    /// None of these raises carries a sub type, so the field is empty rather
    /// than a parameter nobody supplies.
    ///
    /// The text goes in `message`, which is where `create_error`'s third
    /// parameter puts it at every one of these C++ raise sites
    /// (`raise_internal_error`, `raise_authorization_timeout_error`,
    /// `raise_powermeter_transaction_start_failed_error`). That is the field
    /// OCPP reads: an error that maps to no OCPP code renders `message` as the
    /// `vendorId` and never looks at `description` (`get_error_info` in
    /// `modules/EVSE/OCPP/OCPP.cpp`). `description` is filled with the same text
    /// where the C++ leaves the framework's placeholder; nothing reads it, and
    /// naming the error twice loses nothing.
    fn raise_own_error(
        &mut self,
        error_type: &str,
        description: &str,
        severity: Severity,
    ) -> Vec<Effect> {
        let mut effects = vec![Effect::RaiseError(ErrorReport {
            error_type: error_type.to_owned(),
            sub_type: String::new(),
            severity,
            vendor_id: String::new(),
            description: description.to_owned(),
            message: description.to_owned(),
        })];
        let signals = self.faults.raise(Cause {
            source: ErrorSource::Evse,
            error_type: error_type.to_owned(),
            sub_type: String::new(),
            description: description.to_owned(),
            vendor_id: String::new(),
            severity,
        });
        effects.extend(self.apply_fault_signals(signals));
        effects
    }

    /// The verdict on the metering transaction start, if this completion is the
    /// one the core is awaiting.
    ///
    /// `None` means the completion belongs to a power path, which is the only
    /// other correlator. Answering here rather than in the path is what keeps
    /// the decision to stop in the core: `Charger::start_transaction` refuses to
    /// proceed on a refused start (`Charger.cpp:1427-1432`), and the raise is
    /// what carries that refusal into the fault set, which answers a blocking
    /// cause with `Inoperative` and the error shutdown class.
    ///
    /// The identity is taken rather than read, so one request is answered once.
    /// A start that is refused with `fail_on_powermeter_errors` off is logged
    /// and charged through, which is what the C++ does at `Charger.cpp:1428`.
    fn answer_transaction_start(
        &mut self,
        id: Option<EffectId>,
        outcome: &EffectOutcome,
    ) -> Option<Vec<Effect>> {
        let awaited = self.awaiting_transaction_start?;
        if !awaited.answers(id) {
            return None;
        }
        self.awaiting_transaction_start = None;

        let EffectOutcome::Failed(reason) = outcome else {
            return Some(self.persist_session());
        };
        log::error!("failed to start a transaction on the power meter: {reason}");
        if !self.metering.fail_on_errors {
            return Some(self.persist_session());
        }
        let description = format!("Failed to start transaction on the power meter: {reason}");
        Some(self.raise_own_error(
            faults::POWERMETER_TRANSACTION_START_FAILED,
            &description,
            Severity::Medium,
        ))
    }

    /// Write the session uuid where the next boot will find it.
    ///
    /// `Charger.cpp:1439`, and the position inside `start_transaction` is the
    /// whole decision. The store is the last statement of that function, so it
    /// is reached only once the metering start has been accepted or its refusal
    /// tolerated: the `return false` at `Charger.cpp:1431` leaves NO record.
    /// Both non-raising exits of the verdict above are therefore stores, and
    /// the raising one is not. Stored a moment earlier, next to the request, a
    /// start refused with `fail_on_powermeter_errors` on would leave a record
    /// behind for a transaction that never opened, and the next boot would
    /// announce a `PowerLoss` for it.
    ///
    /// Not `start_session`: a session exists from the plug in, long before any
    /// transaction, and a record written there would recover a session that was
    /// never billed.
    fn persist_session(&mut self) -> Vec<Effect> {
        // The record names the transaction, so a session with no identity has
        // nothing to write. The same shape is already refused an
        // `Effect::StartTransaction` above, so this is unreachable through it;
        // it is a broken invariant rather than a reason to store an empty uuid,
        // which is what the store's readers mean by "no record".
        match self.session.id.clone() {
            Some(session_uuid) => vec![self.persist.store(session_uuid)],
            None => {
                log::error!("an open transaction holds no identity, nothing persisted for restart");
                Vec::new()
            }
        }
    }

    /// One meter record's phase currents, stored and then checked.
    ///
    /// Two steps, as the C++ has them: `set_current_drawn_by_vehicle`
    /// (`Charger.cpp:1934-1940`) stores from the meter callback in whatever
    /// state the port is in, and only the two charging states read the store
    /// back. A record that carries no phase set stores nothing and leaves the
    /// previous three values standing, which is the `and .L1 and .L2 and .L3`
    /// gate at `EvseManager.cpp:1157`.
    ///
    /// The check still runs on a record with no phase set, because the C++ tick
    /// runs it on every pass regardless of what the meter last said.
    fn note_phase_currents(
        &mut self,
        phase_currents_a: Option<soft_oc::PhaseCurrents>,
        now: Instant,
    ) -> Vec<Effect> {
        let Some(detection) = self.soft_oc.as_mut() else {
            return Vec::new();
        };
        if let Some(drawn) = phase_currents_a {
            detection.note_phase_currents(drawn);
        }
        self.check_soft_over_current(now)
    }

    /// `Charger::check_soft_over_current` (`Charger.cpp:1942-1976`), gated on
    /// the state its two call sites sit in.
    ///
    /// The gate is re-read here rather than latched, so a deadline that expires
    /// while the port is outside those two states discharges nothing, exactly
    /// as the C++ simply does not call the function there. The crossing itself
    /// survives that, because the C++ keeps `over_current` and its timestamp
    /// across the excursion: a session that crosses, pauses on the EVSE side
    /// and resumes still over the limit trips on the first pass after it
    /// resumes.
    ///
    /// One wake-up is armed per crossing, and a deadline that expires
    /// outside the two states is not re-armed. Ceiling: a crossing that begins,
    /// leaves the two states, and returns to them with the meter permanently
    /// silent has no event left to trip it, where the C++ 100 ms tick would.
    /// Upgrade path: re-arm the wake-up when the deadline expires with the
    /// crossing still standing. Owner: RsEvseManager.
    fn check_soft_over_current(&mut self, now: Instant) -> Vec<Effect> {
        if !soft_oc::state_runs_check(self.path.state()) {
            return Vec::new();
        }
        let signalled_a = self.path.signalled_current_a();
        let Some(detection) = self.soft_oc.as_mut() else {
            return Vec::new();
        };
        let timeout = detection.timeout();
        // The borrow ends here: the trigger arm below reaches the fault set
        // through `raise_own_error`, which takes the whole core.
        let Some(action) = detection.evaluate(signalled_a, now) else {
            return Vec::new();
        };
        match action {
            SoftOverCurrentAction::CrossingStarted { message } => vec![
                Effect::SessionLog(SessionLogEffect::evse(message)),
                Effect::StartTimer {
                    id: TIMER_SOFT_OVER_CURRENT,
                    after: timeout,
                },
            ],

            // Nothing is logged: `Charger.cpp:1962` drops the latch silently.
            SoftOverCurrentAction::CrossingEnded => vec![Effect::CancelTimer {
                id: TIMER_SOFT_OVER_CURRENT,
            }],

            // The log first and then the raise, the order
            // `Charger.cpp:1973-1975` has, and the same string in both.
            SoftOverCurrentAction::Triggered { message } => {
                let mut effects = vec![Effect::SessionLog(SessionLogEffect::evse(message.clone()))];
                effects.extend(self.raise_own_error(
                    faults::MREC4_OVER_CURRENT_FAILURE,
                    &message,
                    Severity::High,
                ));
                effects
            }
        }
    }

    /// Clear the errors this module raised itself, which the unplug ends along
    /// with the session. Ported from `Charger::clear_errors_on_unplug`
    /// (`Charger.cpp:2285-2296`), and mode independent there and here.
    ///
    /// Which errors those are is `Faults`' answer, not a list kept here, so a
    /// raise added to `raise_own_error` later is cleared without this function
    /// changing.
    fn clear_own_errors(&mut self) -> Vec<Effect> {
        let (cleared, signals) = self.faults.clear_own();
        // The per error clears precede the fault set's own signals, the order
        // `ErrorHandling.cpp:345-351` clears in: the interface first, then
        // `process_error`.
        let mut effects: Vec<Effect> = cleared
            .into_iter()
            .map(|cause| {
                Effect::ClearError(ErrorReport {
                    error_type: cause.error_type,
                    sub_type: cause.sub_type,
                    severity: cause.severity,
                    // A clear names the error and nothing else; the
                    // description, message and vendor id belong to the raise.
                    vendor_id: String::new(),
                    description: String::new(),
                    message: String::new(),
                })
            })
            .collect();
        effects.extend(self.apply_fault_signals(signals));
        effects
    }

    /// The only route by which an error leaves the core. Both entries above
    /// funnel through it, so a raise and its clear are built from one place and
    /// neither can grow a field the other lacks.
    fn apply_fault_signals(&mut self, signals: Vec<FaultSignal>) -> Vec<Effect> {
        let inoperative = |severity, vendor_id, description, message| ErrorReport {
            error_type: faults::INOPERATIVE.to_owned(),
            sub_type: String::new(),
            severity,
            vendor_id,
            description,
            message,
        };

        let mut effects = Vec::new();
        for signal in signals {
            match signal {
                // The severity of the `Inoperative` error itself is fixed at the
                // raise (`ErrorHandling.cpp:289-290`); the causes decide the
                // shutdown class below, not this field.
                FaultSignal::RaiseInoperative {
                    description,
                    message,
                    vendor_id,
                } => effects.push(Effect::RaiseError(inoperative(
                    Severity::High,
                    vendor_id,
                    description,
                    message,
                ))),

                FaultSignal::ClearInoperative => effects.push(Effect::ClearError(inoperative(
                    Severity::High,
                    String::new(),
                    String::new(),
                    String::new(),
                ))),

                // `Charger::error_thread` turns these four into a shutdown
                // type and nothing else (`Charger.cpp:130-142`). The emergency
                // and error classes reach the same safe state here because the
                // port holds no `shutdown_type` yet, and the two cleared
                // signals only lift it, which is the restart that the charger
                // state machine owns.
                //
                // The charger is not the only consumer, which is the trap:
                // `error_handling->signal_error` has three subscribers, and
                // reading only `Charger.cpp:78` costs the reservation
                // cancellation below.
                FaultSignal::ForceErrorShutdown | FaultSignal::ForceEmergencyShutdown => {
                    // `Charger.cpp:319-322`. Only the `WaitingForAuthentication`
                    // arm resets SLAC on its way to `Finished`; every other
                    // state leaves the link for the unplug to reset, so the
                    // state is read before the safe state moves it.
                    let waiting_for_auth = self.path.state() == AcState::WaitingForAuthentication;
                    effects.extend(self.path.to_safe_state());
                    if waiting_for_auth {
                        if let Some(hlc) = self.hlc.as_ref() {
                            effects.extend(hlc.on_slac_reset());
                        }
                    }
                    // Last, which is where both C++ shutdowns put it: after
                    // `bsp->allow_power_on(false)`, so the vehicle learns why
                    // only once the port is already safe.
                    if let Some(hlc) = self.hlc.as_ref() {
                        effects.extend(hlc.on_fault_shutdown());
                    }
                    // The second consumer, `EvseManager.cpp:1288-1293`: a
                    // fatal shutdown cancels an active reservation and
                    // announces `ReservationEnd`. It is not the charger's, and
                    // it is the half that leaves the module.
                    //
                    // The C++ fixes no order between the two. This one runs on
                    // the signalling thread while the shutdown above is still
                    // queued for `Charger::error_thread`. Nor does the port:
                    // the announcement is a publish and the safe state an
                    // actuation, and `Effect::context` puts those on separate
                    // lanes.
                    effects.extend(self.cancel_reservation());
                }

                FaultSignal::AllErrorsPreventingChargingCleared | FaultSignal::AllErrorsCleared => {
                }
            }
        }
        effects
    }

    /// What the transcript owes a session event, which is nothing for seven of
    /// the nine.
    ///
    /// The two that matter are the two the C++ brackets its log with. A start
    /// opens a fresh transcript and names the reason; a finish names the end and
    /// closes it. `session_uuid` and not a derived suffix travels in the start:
    /// the `logfile_suffix` rule is configuration, and all four session log
    /// configuration keys are read at the one place that builds the logger.
    fn session_log_bracket(&self, event: SessionEvent) -> Vec<Effect> {
        match event {
            SessionEvent::SessionStarted => vec![
                Effect::SessionLog(SessionLogEffect::Start {
                    session_uuid: self.session.id.clone().unwrap_or_default(),
                }),
                Effect::SessionLog(SessionLogEffect::evse(format!(
                    "Session Started: {}",
                    session_log::start_reason_name(self.session.last_start_reason)
                ))),
            ],
            SessionEvent::SessionFinished => vec![
                Effect::SessionLog(SessionLogEffect::evse("Session Finished")),
                Effect::SessionLog(SessionLogEffect::Stop),
            ],
            SessionEvent::Authorized
            | SessionEvent::Deauthorized
            | SessionEvent::Enabled
            | SessionEvent::Disabled
            // Never arrives here. `recover_interrupted_transaction` builds its
            // report directly, because the uuid is the recovered one and not
            // the session's, so it does not pass through `session_event`.
            | SessionEvent::SessionResumed
            | SessionEvent::AuthRequired
            | SessionEvent::TransactionStarted
            | SessionEvent::ChargingFinished
            | SessionEvent::TransactionFinished
            | SessionEvent::PrepareCharging
            | SessionEvent::ChargingStarted
            | SessionEvent::ChargingPausedEv
            | SessionEvent::ChargingPausedEvse
            | SessionEvent::SwitchingPhases
            | SessionEvent::StoppingCharging
            | SessionEvent::ReservationStart
            | SessionEvent::ReservationEnd
            | SessionEvent::PluginTimeout => Vec::new(),
        }
    }

    /// What this event names beyond itself, and the reservation consumption
    /// that goes with two of the three.
    ///
    /// Three of the twenty session events carry a payload and the rest carry
    /// none, which is why this answers an `Option` rather than each event
    /// filling fields: an event cannot carry the wrong payload, or two.
    ///
    /// The order of the two consuming arms is the whole reason this is one
    /// function. `evse/evse_managerImpl.cpp` reads `is_reserved()` in both the
    /// session started and the transaction started connection, and the first of
    /// them cancels the reservation when the start reason is `Authorized`. So
    /// on an authorization first session the reservation reaches
    /// `SessionStarted` and the `TransactionStarted` behind it names none,
    /// while on a plug in first session `SessionStarted` names it without
    /// consuming and the transaction gets it. Split across the two callers,
    /// that ordering would be a convention; here it is the shape of one match.
    ///
    /// Neither consumption announces `ReservationEnd`. The C++ passes
    /// `signal_event = false` at both sites, and its comment at the transaction
    /// start says what that buys: "this allows OCPP1.6 to not move back to
    /// available".
    ///
    /// Three wire fields are absent from every arm because they are absent from
    /// this port, not because a consumer would not read them:
    /// `TransactionStarted.signed_meter_value` and both signed values on
    /// `TransactionFinished`. All three come from the billing meter's answer to
    /// `start_transaction` and `stop_transaction`, which `Effect::StartTransaction`
    /// and `Effect::StopTransaction` reduce to a status at the boundary; and the
    /// start one is not merely dropped but unobtainable in this order, since
    /// this module announces the transaction before it opens the metering
    /// record while `Charger::start_transaction` does the reverse. See
    /// `docs/architecture.md`.
    fn session_payload(&mut self, event: SessionEvent) -> Option<SessionPayload> {
        match event {
            SessionEvent::SessionStarted => {
                let authorization_first =
                    self.session.last_start_reason == StartSessionReason::Authorized;
                Some(SessionPayload::Started {
                    reservation_id: if authorization_first {
                        self.session.take_reservation()
                    } else {
                        self.session.reservation_id
                    },
                    // `Charger::start_session` hands the signal a token only on
                    // the `authfirst` branch, so a plug in start names none
                    // even when one is already held.
                    id_tag: if authorization_first {
                        self.session.authorized_token.clone()
                    } else {
                        None
                    },
                })
            }

            SessionEvent::TransactionStarted => {
                // Required by the wire type and by every consumer, so an
                // announcement that cannot name the identity is a defect
                // upstream of here rather than a payload to omit: the only
                // route to this event is `start_transaction`, which the power
                // path reaches only from a state its own authorization loop
                // enters with the permission already mirrored.
                let Some(id_tag) = self.session.authorized_token.clone() else {
                    log::error!(
                        "a transaction started with no authorized identity, no payload published"
                    );
                    return None;
                };
                Some(SessionPayload::TransactionStarted {
                    reservation_id: self.session.take_reservation(),
                    id_tag,
                })
            }

            SessionEvent::TransactionFinished => {
                let (reason, id_tag) = self.session.stop_named();
                Some(SessionPayload::TransactionFinished { reason, id_tag })
            }

            // The set, and the memory of it the next assessment compares
            // against. Recorded here rather than at each announcement site
            // because this is the one place every `ChargingPausedEvse`
            // announcement passes through, so the memory cannot be left
            // behind by a route that forgot to write it.
            //
            // An empty set never reaches the wire as an empty list: the entry
            // into this state is what raises the duty and something is always
            // holding the charge when it does. `reassess_pause_reasons` is the
            // other producer and it refuses an empty set outright.
            SessionEvent::ChargingPausedEvse => {
                let reasons = self.pause_reasons();
                self.paused_reasons = reasons.clone();
                Some(SessionPayload::ChargingPausedEvse { reasons })
            }

            SessionEvent::Authorized
            | SessionEvent::Deauthorized
            | SessionEvent::Enabled
            | SessionEvent::Disabled
            | SessionEvent::SessionResumed
            | SessionEvent::AuthRequired
            | SessionEvent::PrepareCharging
            | SessionEvent::ChargingStarted
            | SessionEvent::ChargingPausedEv
            | SessionEvent::SwitchingPhases
            | SessionEvent::StoppingCharging
            // `signal_simple_event` carries the event and nothing else, so the
            // subscriber's payload arms never run for it
            // (`evse/evse_managerImpl.cpp:332-361`).
            | SessionEvent::ChargingFinished
            | SessionEvent::SessionFinished
            | SessionEvent::ReservationStart
            | SessionEvent::ReservationEnd
            | SessionEvent::PluginTimeout => None,
        }
    }

    /// The reason set the `ChargingPausedEVSE` body builds on every pass,
    /// `Charger.cpp:1005-1017`.
    ///
    /// Two of the three the C++ can name, in its push order. `PauseReason`
    /// says why `Error` is unreachable here rather than unported.
    ///
    /// Not gated on the state: the two callers are, and both for their own
    /// reason. `session_payload` is reached only by an announcement of this
    /// very event, and `reassess_pause_reasons` tests the state itself because
    /// the facts it watches change from outside the state too.
    fn pause_reasons(&self) -> Vec<PauseReason> {
        let mut reasons = Vec::new();
        if !self.path.power_available() {
            reasons.push(PauseReason::NoEnergy);
        }
        if self.session.paused_by_evse {
            reasons.push(PauseReason::UserPause);
        }
        reasons
    }

    /// Announce the pause again if the set holding the charge has changed,
    /// which is the whole of `Charger.cpp:1019-1030`.
    ///
    /// Called from the pass boundary in `apply` rather than from the routes
    /// that can change the two facts, because the C++ reaches it on every pass
    /// of a loop that runs every 100 ms. Naming the routes instead is a
    /// subset that has to be kept in step with them: the pause and resume
    /// commands and the two budget routes are today's, and a fifth would
    /// arrive with nothing objecting. Gated on the state and on the set having
    /// changed, so a pass that has nothing to say says nothing.
    ///
    /// The empty set is not acted on. `Charger.cpp:1019-1024`
    /// treats one as a resume, wakes SLAC and moves to `PrepareCharging`; here
    /// the reducer resumes on the request that empties the set rather than on
    /// a pass that notices it is empty, so a port left in this state with
    /// nothing holding it stays there. Ceiling: a session paused for want of
    /// energy alone does not resume by itself when a budget arrives, and waits
    /// for a resume request. Upgrade path: the reducer gains the resume this
    /// assessment can ask for. Owner: RsEvseManager.
    fn reassess_pause_reasons(&mut self) -> Vec<Effect> {
        if self.path.state() != AcState::ChargingPausedEvse {
            return Vec::new();
        }
        let reasons = self.pause_reasons();
        if reasons.is_empty() || reasons == self.paused_reasons {
            return Vec::new();
        }
        self.session_event(SessionEvent::ChargingPausedEvse)
    }

    /// The one construction site for `Effect::PublishSessionEvent`.
    ///
    /// Producers name the event and nothing else. The identity is read from the
    /// session here rather than passed by each of the nine callers, so a
    /// producer cannot emit a session event that does not name its session.
    /// Announce one session event, and retell the stack what the vehicle may
    /// pay with where that event is one of the three that trigger it.
    ///
    /// The C++ reaches the retelling through two subscribers to its own
    /// signals (`EvseManager.cpp:1269` and `:1310`), so the announcement is on
    /// the wire before the stack is spoken to. That order is preserved here by
    /// the order of the returned effects, and it is the reason the retelling
    /// lives at this one site rather than at the nine callers: a tenth caller
    /// cannot announce `SessionFinished` without the stack hearing about it.
    /// The `selected_protocol` republication that follows an announcement, or
    /// nothing for an event the C++ does not publish it after.
    ///
    /// One producer of `Effect::PublishSelectedProtocol`, called from the two
    /// places that announce a session event: `session_event` and
    /// `announce_enable`. The C++ has one publish site because the twelve
    /// events it fires for all travel on one signal; this port publishes those
    /// twelve through two effects, so the pairing lives in one function that
    /// both of them call rather than in each of them.
    fn republish_selected_protocol(&self, event: SessionEvent) -> Vec<Effect> {
        if !protocol::published_after(event) {
            return Vec::new();
        }
        vec![Effect::PublishSelectedProtocol(self.protocol.wire())]
    }

    /// One availability announcement and the protocol republication behind it.
    ///
    /// The only producer of `Effect::PublishEnableEvent`. Both C++ `Enabled`
    /// and `Disabled` are raised through `Charger::signal_simple_event`
    /// (`Charger.cpp:214`, `:1823`, `:1826`, `:1888`), so the same lambda that
    /// publishes them publishes the protocol; keeping the pair in one function
    /// is what stops the two call sites from disagreeing about that.
    fn announce_enable(&self, event: SessionEvent, source: EnableEntry) -> Vec<Effect> {
        let mut effects = vec![Effect::PublishEnableEvent { event, source }];
        effects.extend(self.republish_selected_protocol(event));
        effects
    }

    fn session_event(&mut self, event: SessionEvent) -> Vec<Effect> {
        // The transcript brackets the announcement. The C++ opens the
        // transcript, writes the started line and only then publishes
        // (`evse/evse_managerImpl.cpp:169-188`), and on the way out writes the
        // finished line before it closes (`:328-329`). Both orders are
        // preserved by putting these ahead of the publish, which is what lets
        // the boundary read the opened directory back into
        // `session_started.logging_path`: the two effects share one serial
        // lane, so the start has run by the time the publish does.
        let mut effects = self.session_log_bracket(event);
        // The three lifecycle writes of `selected_protocol`, ahead of the
        // announcement as all three C++ lambdas have them. Only
        // `SessionFinished` is an event the value is published after, so this
        // is the one of the three whose write is visible in the same
        // announcement that made it.
        self.protocol.on_session_event(event);
        let payload = self.session_payload(event);
        // The vehicle record is reset and republished empty at both ends of a
        // session, so one vehicle's identity cannot outlive its session on the
        // wire. The C++ does it in the lambdas it connects to
        // `signal_session_started_event` and `signal_simple_event`, in both
        // cases above their own `hlc_enabled` guard, so a deployment without a
        // stack still publishes the empty record; there is simply never
        // anything in it.
        if matches!(
            event,
            SessionEvent::SessionStarted | SessionEvent::SessionFinished
        ) {
            if let Some(hlc) = self.hlc.as_mut() {
                hlc.forget_vehicle_record();
            }
            effects.push(Effect::PublishEvInfo(EvInfo::default()));
        }
        effects.push(Effect::PublishSessionEvent(SessionEventReport {
            uuid: match event {
                // A reservation is not part of a session
                // (`evse/evse_managerImpl.cpp:126`).
                SessionEvent::ReservationStart | SessionEvent::ReservationEnd => String::new(),
                _ => self.session.id.clone().unwrap_or_default(),
            },
            started: match event {
                SessionEvent::SessionStarted => Some(self.session.last_start_reason),
                _ => None,
            },
            payload,
            event,
        }));
        effects.extend(self.republish_selected_protocol(event));
        // The three trigger points, and no others. The C++ lambda early
        // returns on every session event but these two
        // (`EvseManager.cpp:1280-1282`), and the start has its own signal.
        let trigger = match event {
            SessionEvent::Authorized => Some(Trigger::Authorized),
            SessionEvent::SessionFinished => Some(Trigger::SessionFinished),
            SessionEvent::SessionStarted => {
                Some(Trigger::SessionStarted(self.session.last_start_reason))
            }
            _ => None,
        };
        if let Some(trigger) = trigger {
            let fake_dc = self.path.presents_fake_dc();
            if let Some(hlc) = self.hlc.as_ref() {
                effects.extend(hlc.session_setup(trigger, fake_dc));
            }
        }
        effects
    }

    /// `Charger::start_session` (`Charger.cpp:1376-1390`). The only site that
    /// mints an identity and the only one that publishes `SessionStarted`, so a
    /// start can neither reuse the previous session's uuid nor omit its reason.
    /// The identity is minted before any session state moves, so a start that
    /// cannot be given one leaves nothing half opened behind it.
    /// Resolves the bidirectional fact once and writes it where the power path
    /// reads it.
    ///
    /// ADR-0018: the C++ recomputes a disjunction at each read site and the
    /// sites disagree about which terms it has, so the answer at any moment is
    /// a property of read ordering. There is one resolution here and one field,
    /// and every source change calls this.
    fn refresh_bidirectional(&mut self) {
        // False without a stack, and that is the resolution rather than a
        // default: every one of the three sources ADR-0018 names is an
        // ISO 15118 fact, so a basic AC port cannot discharge and cannot be
        // asked to.
        self.session.profile.bidirectional =
            self.hlc.as_ref().is_some_and(|hlc| hlc.bidirectional());
    }

    /// The budget a grant carried has outlived its validity.
    ///
    /// `Charger::power_available` (`Charger.cpp:2116-2122`) drops the stored
    /// maximum to zero and warns once the deadline has passed, and the drop
    /// goes out on `signal_max_current` like any other. So the fallback is
    /// published, the path is told, and the vehicle's announcement follows -
    /// the same three consequences a grant of zero would have had.
    ///
    /// The C++ guard is `if (max_current > 0.)`: an expiry that finds nothing
    /// to drop signals nothing. It reads the stored magnitude, so a discharge
    /// allowance is something to drop.
    fn expire_budget(&mut self, now: Instant) -> Vec<Effect> {
        if self.session.limits.max_current_a == 0.0 {
            return Vec::new();
        }
        self.session.limits.max_current_a = 0.0;
        let mut effects = vec![Effect::PublishLimits(self.session.limits)];
        effects.extend(self.path.on_limits_changed(&self.session, now));
        if let Some(hlc) = self.hlc.as_ref() {
            effects.extend(hlc.note_ac_current_limit(0.0));
        }
        effects
    }

    /// Retells the one reader that keeps a copy of the derated capability
    /// report, after something moved it with no new report to carry it.
    ///
    /// The C++ holds no copies: `energyImpl` and the charger clamp both call
    /// `get_powersupply_capabilities()` when they need it, so a stored derate
    /// reaches them at their next read with nothing pushed at all. The energy
    /// tree is the one reader here that does keep a copy, so it is the one
    /// place that has to be retold.
    ///
    /// The power path is deliberately not retold. It learns the narrowed
    /// ceiling on the next enforced limits pass, which derives the EVSE
    /// maximum set from this very report (`energy::enforce`, reading
    /// `energyImpl.cpp:545`) and is the sole producer of the path's limit set.
    /// Pushing a set from here would have to name the supply's ceiling without
    /// the energy allowance applied, widening the DC clamp back to the supply
    /// maximum until the next pass. Deferring to the energy tick is also what
    /// the C++ does, so this is one fewer divergence rather than a concession;
    /// see `docs/architecture.md`.
    ///
    /// `bidirectional` is not a reader either: derating cannot change it, so
    /// the session profile cannot move here and is deliberately not refreshed.
    fn refresh_derated_capabilities(&mut self) {
        let floors = self.car_side_meter.floors();
        let Some(capabilities) = self
            .hlc
            .as_ref()
            .and_then(|hlc| hlc.derated_supply_capabilities(floors))
        else {
            return;
        };
        self.energy.note_supply_capabilities(capabilities);
    }

    /// `Charger::authorize` and the `charger_was_authorized` beside it
    /// (`evse/evse_managerImpl.cpp:436-437`), which is the whole of what an
    /// accepted verdict does to this module's permission state.
    ///
    /// Shared with the free charging grant, which the C++ shares too: its
    /// plug in handler calls the same `charger->authorize` rather than routing
    /// a verdict through `handle_authorize_response` (`:153-160`).
    fn grant_authorization(
        &mut self,
        token: IdTag,
        tariff: crate::core::auth::TariffMessages,
        now: Instant,
    ) -> Vec<Effect> {
        let context = self.auth_context();
        let signals = self.auth.authorize(token, tariff, context);
        let mut effects = self.apply_auth_signals(&signals, now);
        // `charger_was_authorized` runs after the permission has been recorded
        // (`:437`), which is what the gate then reads.
        let held = self.authorization_held();
        if let Some(hlc) = self.hlc.as_mut() {
            effects.extend(hlc.on_authorization_settled(held));
        }
        effects
    }

    fn start_session(
        &mut self,
        reason: StartSessionReason,
    ) -> Result<Vec<Effect>, EntropyExhausted> {
        let id = self.session_ids.mint()?;
        self.session.session_active = true;
        self.session.externally_cancelled = false;
        self.session.last_start_reason = reason;
        self.session.id = Some(id);
        Ok(self.session_event(SessionEvent::SessionStarted))
    }

    /// A session start that could not be given an unpredictable identity.
    ///
    /// The identity is also the transaction identity on the bus, so there is no
    /// substitute to offer: not an invented id, not a counter, and least of all
    /// the previous session's. The start is refused and the reason is raised on
    /// this module's own interface, which is the route every other refusal in
    /// this file takes. The fault set answers that raise with the error shutdown
    /// class, so the port is driven to safe state and the bounded connector
    /// release is armed, and the vehicle is not left latched by the refusal. An
    /// unplug clears the raise along with every other error this module owns.
    ///
    /// The permission to charge goes with it. `Auth::authorize` records the
    /// permission before the signals it returns are applied, so a refusal that
    /// left it standing would be read by a later pass as authority to charge
    /// into a session that was never opened. The session's own copy of the token
    /// needs no clearing: it is written by the `Authorized` signal, which a
    /// refused start never reaches.
    fn refuse_session_start(&mut self) -> Vec<Effect> {
        self.auth.clear();
        // A permission the port cannot act on must not survive the refusal: a
        // later pass would read the read model and charge into a session that
        // was never opened. `a_refused_start_holds_no_authorization` pins it,
        // and it caught this the moment the read model stopped being written by
        // the `Authorized` arm the refusal returns before.
        self.mirror_authorization();
        // The error class, not the emergency one. Nothing is energized at a
        // start, and this is the class the other start time refusal
        // (`MREC9AuthorizationTimeout`) is raised with.
        self.raise_own_error(INTERNAL, NO_SESSION_ID_DESCRIPTION, Severity::Medium)
    }

    /// A vehicle arrived. The `WaitingForAuthentication` initialize pass of
    /// `Charger::run_state_machine` (`Charger.cpp:262-272`).
    ///
    /// The path moves first, as it does there: the state entry withdraws power
    /// permission (`Charger.cpp:264`) before the session is announced. The
    /// start is guarded on the session not already being active, because an
    /// authorization that came first has already opened one (`Charger.cpp:268`),
    /// while the authorization request is not guarded at all.
    fn on_plug_in(&mut self, now: Instant) -> Vec<Effect> {
        // The identity is minted before the port moves, so a start that cannot
        // be given one refuses with the connector untouched rather than after
        // it has latched. The announcement still follows the path, which is the
        // order the C++ publishes in.
        // Whether this arrival is what opened the session, which is the
        // `EVConnected` reason the free charging grant below is gated on.
        let opened_here = !self.session.session_active;
        let started = if opened_here {
            match self.start_session(StartSessionReason::EvConnected) {
                Ok(started) => started,
                Err(EntropyExhausted) => return self.refuse_session_start(),
            }
        } else {
            Vec::new()
        };
        self.session.phase = SessionPhase::WaitingForAuthorization;
        let mut effects = self.path.on_session_start(&self.session, now);
        effects.extend(started);
        effects.extend(self.session_event(SessionEvent::AuthRequired));
        // `evse/evse_managerImpl.cpp:149-160`. A free charging deployment
        // authorizes itself here, on the session started signal and under the
        // `EVConnected` reason alone: an authorization first session already
        // holds the identity the vehicle presented, and granting a second one
        // would replace it - which is what a metering transaction would then be
        // billed under. The vehicle is still told an authorization is required
        // first, which is the order the C++ has: the grant runs on a thread of
        // its own out of the same handler, after the event.
        if let Some(token) = self.auth.free_service_token().filter(|_| opened_here) {
            effects.extend(self.grant_authorization(token, crate::core::auth::TariffMessages::default(), now));
        }
        effects
    }

    /// The identity release half of `Charger::stop_session`
    /// (`Charger.cpp:1391-1399`), which clears the uuid after the event that
    /// still carries it.
    fn finish_session(&mut self) -> Vec<Effect> {
        let effects = self.session_event(SessionEvent::SessionFinished);
        self.session.id = None;
        effects
    }

    /// `EvseManager::reserve` (`EvseManager.cpp:1720-1774`), both of its call
    /// sites, and the `signal_reservation_event` parameter that tells them
    /// apart.
    ///
    /// One function because the C++ has one: `handle_reserve` passes true
    /// (`evse/evse_managerImpl.cpp:481-483`) and
    /// `handle_authorize_response` passes false (`:453-461`), because the
    /// second is learning the id of a reservation that already exists rather
    /// than making one. Two copies here meant the command route had none of
    /// the refusals and announced unconditionally.
    ///
    /// **Three refusals, and the idle one carries the other two.** A port out
    /// of service and a port with a fatal error are each refused before it
    /// (`:1724-1740`), and `Disabled` is neither `Idle` nor `Startup`, so the
    /// first is subsumed. The second has no port equivalent - this module holds no
    /// `shutdown_type` - and a fatal shutdown leaves the reducer outside
    /// `Idle` regardless, so the idle refusal stands in for it. On the verdict
    /// route the idle test is the one that shows on the wire: a plug in first
    /// session is already past `Idle` when the verdict lands, so the C++ drops
    /// the id and the transaction carries none.
    ///
    /// **The overwrite rule** (`:1744-1756`): an id already held wins unless
    /// the request names that same id. A held `-1` is not such an id. It is
    /// the C++ sentinel for "reserved, but for no reservation this module can
    /// name", which `reserve` sets by declining to store a negative id
    /// (`:1754-1756`) and its accept condition then treats as empty (`:1752`).
    /// It reaches this module for real:
    /// `AuthHandler::check_evse_reserved_and_send_updates` calls
    /// `reserve(evse, -1)` once the global reservations match the available
    /// EVSEs.
    ///
    /// **And the announcement is not every accept.** `overwrite_reservation`
    /// is computed before the store and the sentinel test after it
    /// (`:1748`, `:1759`), so a reservation re-made under its own nameable id
    /// says nothing while every other accept announces. That asymmetry is
    /// what keeps a CSMS from being told a reservation started twice.
    /// Answers the effects beside the accept/refuse verdict `handle_reserve`
    /// returns on the wire.
    ///
    /// The verdict is returned rather than recomputed from the session
    /// afterwards, because the refusals are not distinguishable once they have
    /// happened: a request refused for a non-idle port and one accepted under
    /// its own already-held id both leave `reservation_id` as they found it.
    fn reserve(&mut self, id: i64, announce: bool) -> (Vec<Effect>, bool) {
        // `Startup` is admitted beside `Idle` because it is the port before
        // its first pass and the C++ calls that `Idle`: the `Charger`
        // constructor assigns it (`Charger.cpp:54`), so a reservation arriving
        // between the module becoming ready and the board reporting itself is
        // accepted there. Refusing it here would be a refusal the C++ has no
        // state to make.
        if !matches!(self.path.state(), AcState::Idle | AcState::Startup) {
            return (Vec::new(), false);
        }
        let held = self.session.reservation_id;
        if held.is_some_and(|held| held >= 0 && held != id) {
            return (Vec::new(), false);
        }
        // `:1755-1757`. A negative id reserves the connector and names no
        // reservation, so it leaves the sentinel standing rather than storing
        // itself; the refusal above means nothing nameable is standing.
        self.session.reservation_id = Some(if id >= 0 { id } else { -1 });
        let overwrite = held == Some(id);
        // Accepted either way. The C++ returns true from inside the accept
        // branch whether or not it signalled (`EvseManager.cpp:1758-1771`), so
        // a silent overwrite is a success and not a refusal.
        if !announce || (overwrite && self.session.reservation_id != Some(-1)) {
            return (Vec::new(), true);
        }
        (self.session_event(SessionEvent::ReservationStart), true)
    }

    /// `EvseManager::cancel_reservation(true)` (`EvseManager.cpp:1776-1789`):
    /// drop the reservation and announce that it ended, and do neither when
    /// none is held.
    ///
    /// The guard is the C++ `if (reserved)`, and it is what keeps a fatal
    /// shutdown on an unreserved connector from announcing the end of a
    /// reservation that never began.
    fn cancel_reservation(&mut self) -> Vec<Effect> {
        if self.session.take_reservation().is_none() {
            return Vec::new();
        }
        self.session_event(SessionEvent::ReservationEnd)
    }

    /// `Charger::cancel_transaction` (`Charger.cpp:1423-1449`) and the stop it
    /// sets the state machine up to take.
    ///
    /// Two callers, as in the C++: `handle_stop_transaction` forwards its
    /// request here and `handle_force_unlock` calls the same function with
    /// `UnlockCommand` before it opens the lock.
    fn cancel_transaction(
        &mut self,
        reason: StopTransactionReason,
        id_tag: Option<IdTag>,
        now: Instant,
    ) -> Vec<Effect> {
        // `flag_transaction_active` wraps the WHOLE of
        // `Charger::cancel_transaction`, not just the pair it records. A stop
        // request that finds no transaction open therefore changes nothing at
        // all: it does not name a reason, does not cancel, does not revoke the
        // authorization, and reports no success. An authorization first session
        // waiting for a plug in survives a stop that arrives before it.
        if !self.session.transaction_active {
            return Vec::new();
        }
        self.session.record_stop(reason, id_tag);
        self.session.externally_cancelled = true;
        self.auth.revoke();
        self.mirror_authorization();
        self.stop(reason.narrow(), now)
    }

    fn stop(&mut self, reason: StopReason, now: Instant) -> Vec<Effect> {
        self.session.phase = SessionPhase::Stopping;
        // Before the path moves, for the reason `discharge_session_duties`
        // gives.
        let hlc_active = self.path.hlc_charging_active();
        // `Charger::cancel_transaction` (`Charger.cpp:1355-1363`) names the
        // reason to the vehicle before the state machine reaches the stopping
        // entry that asks it to end the session, so the error leads here too.
        let mut effects: Vec<Effect> = self
            .stop_error_for(reason, hlc_active)
            .map(|error| Effect::HlcUpdate(HlcUpdate::SendError(error)))
            .into_iter()
            .collect();
        effects.extend(self.path.on_stop(&self.session, reason, now));
        // The transition line leads, for the same reason it does at the end of
        // an ordinary pass: the C++ writes it before the entered state
        // announces anything.
        let crossed = self.path.take_entered_states();
        effects.extend(self.log_state_transitions(&crossed));
        // The stopping announcement belongs to the state the path just
        // entered, and the C++ signals it from that entry (`Charger.cpp:1012`)
        // before `Finished` closes the record (`Charger.cpp:1067`). Draining
        // here rather than leaving it to the end of the pass is what keeps the
        // two in that order on the wire, which consumers read as a sequence.
        effects.extend(self.discharge_session_duties(hlc_active));
        effects.extend(self.stop_transaction());
        effects
    }

    /// What an external stop names to the vehicle, `Charger.cpp:1355-1363`.
    ///
    /// Two of the eight reasons name an error and the rest name none, which is
    /// the C++ shape: it tests for exactly `EmergencyStop` and `PowerLoss`.
    /// Both gates are the C++ ones and neither is a power path's:
    /// `flag_transaction_active` wraps the whole of `cancel_transaction`
    /// (`:1357`), and `hlc_charging_active` wraps the two sends inside it
    /// (`:1359`). The routes that reach here with the other six reasons name
    /// nothing on either side.
    fn stop_error_for(&self, reason: StopReason, hlc_active: bool) -> Option<EvseError> {
        if !self.session.transaction_active || !hlc_active {
            return None;
        }
        match reason {
            StopReason::EmergencyStop => Some(EvseError::EmergencyShutdown),
            StopReason::PowerLoss => Some(EvseError::UtilityInterruptEvent),
            _ => None,
        }
    }

    /// `Charger::start_transaction` (`Charger.cpp:1477-1518`), with the guard
    /// both its call sites apply (`Charger.cpp:394` and `:523`): a
    /// re-validation inside a live session is not a second billing record, and
    /// neither is the reinitialization, which re-enters the transition that
    /// raises this duty precisely to keep the record open.
    ///
    /// The identity is the session's own. `start_session` mints it before any
    /// session state moves and a start that cannot be given one is refused, so
    /// a live session always holds one; a missing one is a broken invariant and
    /// not a reason to open a nameless transaction.
    fn start_transaction(&mut self) -> Vec<Effect> {
        if self.session.transaction_active {
            return Vec::new();
        }
        self.session.transaction_active = true;
        let mut effects = Vec::new();
        match self.session.id.clone() {
            Some(transaction_id) => {
                // `Charger::start_transaction`'s first two statements
                // (`Charger.cpp:1478-1479`). A reason recorded before this
                // transaction existed - a disable on an idle port records one -
                // is not this transaction's to end under.
                self.session.clear_stop();
                let id = self.effect_ids.allocate();
                self.awaiting_transaction_start = Some(id);
                effects.push(Effect::StartTransaction {
                    id: id.effect_id(),
                    transaction_id,
                    id_token: self.session.authorized_token.clone(),
                    // `Charger.cpp:1492-1495`: the first message, and nothing
                    // where the verdict carried none.
                    tariff_text: self.session.authorized_tariff.text().map(str::to_owned),
                });
            }
            None => log::error!(
                "an authorized session holds no identity, no metering transaction opened"
            ),
        }
        // After the meter is asked and not before, which is the whole of
        // `Charger::start_transaction`'s order (`Charger.cpp:1497-1513`): the
        // announcement reads the meter's answer back for
        // `transaction_started.signed_meter_value`
        // (`evse/evse_managerImpl.cpp:207`), so a payload built ahead of the
        // request can only carry nothing. Both effects are
        // `ExecContext::Publish`, so this is execution order and not merely
        // dispatch order; `Effect::context` says why that lane.
        //
        // Still unconditional on the meter's verdict, which the C++'s is not.
        // `Charger::start_transaction` returns false from inside its meter loop
        // when a billing meter answers `UNEXPECTED_ERROR` and
        // `fail_on_powermeter_errors` is set, so it reaches neither
        // `store_session` nor `signal_transaction_started_event`: nothing is
        // persisted and nothing is announced. This port announces here and
        // answers the verdict afterwards in `answer_transaction_start`, which
        // is where the error is raised and the port taken out of service.
        //
        // Said plainly: on a port with `fail_on_powermeter_errors` set, a
        // refused meter means the CSMS is told a transaction started that the
        // C++ would never have opened, and the port then goes inoperative
        // behind that announcement. The divergence stands because neither way
        // out is available. Announcing after the verdict inverts the order 73
        // existing assertions were written against, which makes every one of
        // them a judgement call rather than an edit. Deferring the
        // announcement until the verdict arrives invents a requested but not
        // open transaction the C++ cannot have, because its meter call blocks
        // inside `start_transaction`, and that state would then owe its own
        // answers about an unplug, a stop and a shutdown reaching it. The
        // divergence is older than this ordering and unchanged by it; the
        // divergence list in `docs/architecture.md` carries it too.
        effects.extend(self.session_event(SessionEvent::TransactionStarted));
        effects
    }

    /// `Charger::stop_transaction`, with the guard its only state machine
    /// caller applies (`Charger.cpp:1063-1066`): a transaction cancelled
    /// earlier must not produce a second transactionFinished event.
    /// `transaction_active` is that guard, and it is the only one, so every
    /// route that closes a record is protected by it rather than by knowing
    /// which other route may have run first.
    fn stop_transaction(&mut self) -> Vec<Effect> {
        if !self.session.transaction_active {
            return Vec::new();
        }
        self.session.transaction_active = false;
        // A start verdict still outstanding belongs to the transaction this
        // just closed, so it is dropped with it. `end_session` does the same
        // for the session; this is the narrower window, and closing it is what
        // keeps the record honest.
        //
        // Reachable: `AcState::Finished` raises `StopTransaction` without
        // `EndSession` (`path/mod.rs:207`) and then waits for the unplug, so a
        // metering start still in flight can be answered while the session is
        // alive and the transaction is not. Left standing, that verdict would
        // persist a record for a transaction that had already finished cleanly,
        // and nothing clears it afterwards: the next boot would announce a
        // `PowerLoss` for a session that ended normally. It would also raise
        // `POWERMETER_TRANSACTION_START_FAILED` on a port with nothing running.
        //
        // The C++ cannot reach either, because its `store_session` sits inside
        // a synchronous `start_transaction` (`Charger.cpp:1439`) that no stop
        // can interleave with, so the record exists only while the transaction
        // does. This restores that invariant.
        self.awaiting_transaction_start = None;
        let mut effects = Vec::new();
        // The stop names the transaction the start opened. An empty
        // `transaction_id` means "cancel every ongoing transaction" on the
        // powermeter interface, so a missing identity closes nothing rather
        // than closing everything.
        match self.session.id.clone() {
            Some(transaction_id) => effects.push(Effect::StopTransaction { transaction_id }),
            None => {
                log::error!("a session with an open transaction holds no identity, nothing closed")
            }
        }
        // `Charger.cpp:1471`: after the meter has been asked to close the
        // record and before the two events that announce it. A transaction
        // closed here is not one to recover, so the record goes with it.
        //
        // This is the only site that clears, because `transaction_active` just
        // above is the only write that ends a transaction. Every route that
        // closes a record runs through this function, so no route can end one
        // and leave its record behind. `end_session` deliberately does not
        // clear: `Charger::stop_session` (`Charger.cpp:1392-1400`) does not
        // touch the store, and a session that never opened a transaction never
        // had a record.
        effects.push(self.persist.clear());
        // `Charger.cpp:1548-1550`: two announcements in that order, from two
        // different signals. The first says the charge is over and the second
        // says the billing record is closed, and a consumer reads the pair as
        // a sequence. Three API modules reach their finished state off the
        // first and not off the second (`modules/API/API/API.cpp:139`,
        // `modules/API/EVerestAPI/evse_manager_consumer_API/session_info.cpp:63`,
        // `modules/API/RpcApi/data/SessionInfo.cpp:83`), so a port that
        // published only the second left every one of them in whichever state
        // the charge had last put it in.
        effects.extend(self.session_event(SessionEvent::ChargingFinished));
        effects.extend(self.session_event(SessionEvent::TransactionFinished));
        effects
    }

    /// `Charger::stop_session` (`Charger.cpp:1391-1399`), reached from the
    /// `Finished` exit at `Charger.cpp:1081`.
    ///
    /// Guarded on the session being active, which is what makes the duty safe
    /// to raise from every edge into a resting state: a session already ended
    /// by the deauthorization route is not ended again by the unplug that
    /// follows it.
    fn end_session(&mut self) -> Vec<Effect> {
        if !self.session.session_active {
            return Vec::new();
        }
        self.session.session_active = false;
        self.session.externally_cancelled = false;
        self.session.paused_by_evse = false;
        // `Charger.cpp:229` clears this one on the `Idle` entry rather than in
        // `stop_session`, which is one state later on the same route out of
        // `Finished`. Nothing reads it in between, and clearing it here keeps
        // every fact whose life is one session ending in one place.
        self.session.iso15118_20_active = false;
        self.session.phase = SessionPhase::Idle;
        // ADR-0018. The discharge refusal is released here as well as on a
        // capability that returns, because a supply can also stop reporting
        // altogether: a refusal carried into the next session on a supply that
        // never reports again would need an unplug to clear. The edge the
        // release reports is not read, because the refresh below is
        // unconditional on this route.
        if let Some(hlc) = self.hlc.as_mut() {
            hlc.release_discharge_withdrawal();
        }
        self.refresh_bidirectional();
        self.auth.clear();
        // A start verdict outstanding when the session ends belongs to a session
        // that no longer exists. Left standing it would raise on an idle port,
        // and the unplug that clears own errors has already run, so the raise
        // would latch until the next one. The C++ cannot reach this shape
        // because its `start_transaction` is synchronous inside the state
        // machine; here the verdict returns as an event and can outlive the
        // session that asked for it.
        self.awaiting_transaction_start = None;
        // The crossing belongs to the session that was drawing. The C++ clears
        // the error itself on the unplug (`Charger::clear_errors_on_unplug`);
        // the latch behind it has to go too, or the next vehicle inherits a
        // window that has already expired and is stopped on its first sample.
        if let Some(detection) = self.soft_oc.as_mut() {
            detection.end_session();
        }
        // `auth.clear()` above lowered the permission and the identity; this
        // carries that into the read model, which is the only writer of it.
        self.mirror_authorization();
        self.finish_session()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::core::auth::{
        AuthorizationKind, AuthorizationStatus, CertificateStatus, TariffMessages,
    };
    use crate::core::token::{
        id_tag_for_tests, typed_id_tag_for_tests, IdTag, IdTokenType, EVERY_TOKEN_TYPE,
    };
    use crate::core::config::{ChargeMode, SwitchCpState};
    use crate::core::effect::EvseError;
    use crate::core::enable::{EnableSource, EnableState};
    use crate::core::event::{
        CpEvent, HardwareCapabilities, HlcEvent, IsolationReading, MeterReading,
        PowerSupplyCapabilities, ReplyToken,
    };
    use crate::core::hlc::{
        ConnectorKind, DynamicModeRequest, EvMaximumLimits, MinimumLimits, PaymentOption,
        PhysicalValues, PlugAndChargeConfiguration, Power, SelectedService, SessionSetup,
    };
    use crate::core::path::ac::AcBasic;
    use crate::core::path::dc::{Dc, DcConfig};
    use crate::core::path::iec::IecConfig;
    use crate::core::session::{Limits, PwmStart};

    /// A deterministic byte source. Injecting it is what lets a session
    /// sequence be asserted while production draws from the operating system.
    fn test_session_ids() -> SessionIds {
        SessionIds::new(
            crate::core::config::SessionIdType::Uuid,
            crate::core::session_id::SeededBytes::new([0x5eed, 0xf00d]),
        )
    }

    /// The module id every test persists under, so a test that asserts on the
    /// key asserts on the same one production builds.
    const TEST_MODULE_ID: &str = "evse_manager";

    /// A store holding `recovered` from the previous run. `None` is the boot
    /// with nothing left behind, which is every test but the recovery ones.
    fn test_persist(recovered: Option<&str>) -> SessionStore {
        SessionStore::new(TEST_MODULE_ID, recovered.map(str::to_owned))
    }

    fn test_session_key() -> String {
        SessionStore::key_for(TEST_MODULE_ID)
    }

    /// Soft overcurrent detection over the manifest defaults, which is what an
    /// AC deployment that configures none of the three keys gets.
    fn test_soft_oc() -> Option<Detection> {
        Some(Detection::new(soft_oc::SoftOverCurrentConfig {
            tolerance_percent: 10.0,
            measurement_noise_a: 0.5,
            timeout: std::time::Duration::from_millis(7000),
        }))
    }

    /// The high level communication port a test core carries, or `None`.
    ///
    /// Most cores in this file run `AcBasic`, which is exactly the deployment
    /// the C++ leaves with an empty advertised set and no stack wired, so they
    /// take `None` and the ready sequence publishes the empty set for them.
    ///
    /// `enabled` is not a parameter any more: it decided whether the port held
    /// a disabled configuration, and there is no such configuration. It decides
    /// whether there is a port at all, which is what the `Option` says.
    fn test_hlc(charge_mode: ChargeMode, enabled: bool) -> Option<HlcPort> {
        test_hlc_with(charge_mode, enabled, &[])
    }

    fn test_hlc_with(
        charge_mode: ChargeMode,
        enabled: bool,
        pairs: &[(&str, serde_json::Value)],
    ) -> Option<HlcPort> {
        if !enabled {
            return None;
        }
        let raw: crate::core::config::RawConfig = [
            (
                "charge_mode".to_string(),
                serde_json::json!(match charge_mode {
                    ChargeMode::Ac => "AC",
                    ChargeMode::Dc => "DC",
                }),
            ),
            ("connector_id".to_string(), serde_json::json!(1)),
            // On AC a configuration exists only where this or `ac_with_soc` is
            // set. Overridable by `pairs`, which is collected after it.
            ("ac_hlc_enabled".to_string(), serde_json::json!(true)),
        ]
        .into_iter()
        .chain(
            pairs
                .iter()
                .map(|(key, value)| (key.to_string(), value.clone())),
        )
        .collect();
        let settings = crate::core::config::Settings::from_raw(
            &raw,
            &crate::core::config::Mapping {
                evse: 1,
                connectors: vec![1],
            },
        )
        .unwrap();
        // The stack and SLAC connected, which is what `hlc_enabled` reads off
        // the wiring and all this fixture needs to say about it.
        let wiring = crate::core::config::Wiring {
            hlc: true,
            slac: true,
            ..crate::core::config::Wiring::default()
        };
        Some(HlcPort::new(
            std::sync::Arc::new(
                crate::core::hlc::HlcConfig::for_deployment(&settings, &wiring)
                    .expect("a wired deployment has a configuration"),
            ),
            charge_mode,
        ))
    }

    /// The energy tree every test core carries. The identity is the module id
    /// a configured `EvseManager` would have.
    fn test_energy(charge_mode: ChargeMode) -> EnergyTree {
        EnergyTree::new(
            crate::core::energy::NodeUuid::from_module_id("evse_manager"),
            crate::core::energy::EnergyConfig {
                charge_mode,
                ac_nominal_voltage_v: 230.0,
                sae_v2h: false,
                request_zero_power_in_idle: true,
            },
            crate::core::energy::random_delay::boot_defaults(),
        )
    }

    fn core_with(ready: ReadyGate) -> Core {
        let path = Box::new(AcBasic::new(IecConfig {
            initial_current_limit_a: 16.0,
            has_ventilation: true,
            lock_connector_in_state_b: true,
            switch_phases_cp_state: SwitchCpState::X1,
            switch_phases_delay: std::time::Duration::from_secs(10),
            reinit_method: crate::core::config::ReinitMethod::CpStateF,
            reinit_duration: std::time::Duration::from_millis(3000),
            hlc_no_energy_timeout: std::time::Duration::from_secs(5),
            type2_socket: false,
        }));
        Core::new(
            path,
            Session::new(
                PwmStart::Nominal,
                Limits {
                    max_current_a: 16.0,
                    nr_of_phases_available: 3,
                },
            ),
            CoreParts {
                ready,
                auth: Auth::new(true),
                faults: Faults::new(true),
                session_ids: test_session_ids(),
                metering: Metering {
                    fail_on_errors: false,
                },
                // Every core fixture boots without waiting, so a test that
                // means to drive the wait arms it by hand; see
                // `the_startup_sequence_waits_for_the_first_meter_reading`.
                initial_meter_timeout: Duration::ZERO,
                hlc: test_hlc(ChargeMode::Ac, false),
                energy: test_energy(ChargeMode::Ac),
                persist: test_persist(None),
                soft_oc: test_soft_oc(),
            },
        )
    }

    fn core_with_faults(faults: Faults) -> Core {
        Core::new(
            Box::new(AcBasic::new(IecConfig {
                initial_current_limit_a: 16.0,
                has_ventilation: true,
                lock_connector_in_state_b: true,
                switch_phases_cp_state: SwitchCpState::X1,
                switch_phases_delay: std::time::Duration::from_secs(10),
                reinit_method: crate::core::config::ReinitMethod::CpStateF,
                reinit_duration: std::time::Duration::from_millis(3000),
                hlc_no_energy_timeout: std::time::Duration::from_secs(5),
                type2_socket: false,
            })),
            Session::new(
                PwmStart::Nominal,
                Limits {
                    max_current_a: 16.0,
                    nr_of_phases_available: 3,
                },
            ),
            CoreParts {
                ready: ReadyGate::default(),
                auth: Auth::new(true),
                faults,
                session_ids: test_session_ids(),
                metering: Metering {
                    fail_on_errors: false,
                },
                // Every core fixture boots without waiting, so a test that
                // means to drive the wait arms it by hand; see
                // `the_startup_sequence_waits_for_the_first_meter_reading`.
                initial_meter_timeout: Duration::ZERO,
                hlc: test_hlc(ChargeMode::Ac, false),
                energy: test_energy(ChargeMode::Ac),
                persist: test_persist(None),
                soft_oc: test_soft_oc(),
            },
        )
    }

    /// A core whose session is already running, which is what lets a withdraw
    /// reach the authorization timeout branch at all.
    fn core_in_session() -> Core {
        core_in_session_with(Auth::new(true), Faults::new(true))
    }

    fn core_in_session_with(auth: Auth, faults: Faults) -> Core {
        let mut core = core_with_faults(faults);
        core.auth = auth;
        core.session.session_active = true;
        core
    }

    fn core() -> Core {
        core_with(ReadyGate::default())
    }

    /// A core whose power path is the DC one.
    ///
    /// Every other core in this file holds an AC path and the recorder path
    /// holds no session progress at all, so without this the chain from a duty
    /// a `Dc` edge raises to the effect the core makes of it is driven nowhere.
    fn dc_core() -> Core {
        dc_core_with_hlc(test_hlc(ChargeMode::Dc, true))
    }

    /// A DC port taking the autocharge identity from SLAC, which is the one
    /// configuration in which the MAC address arm does anything.
    fn dc_core_with_autocharge_from_slac() -> Core {
        dc_core_with_hlc(test_hlc_with(
            ChargeMode::Dc,
            true,
            &[
                ("enable_autocharge", serde_json::json!(true)),
                (
                    "autocharge_use_slac_instead_of_hlc",
                    serde_json::json!(true),
                ),
            ],
        ))
    }

    fn dc_core_with_hlc(hlc: Option<HlcPort>) -> Core {
        // Both optional monitors wired, which is what a full DC deployment
        // gets. They arrive as the collaborators themselves, so this fixture
        // cannot claim one and hold nothing to talk to.
        let wiring = crate::core::config::Wiring {
            imd: true,
            over_voltage_monitor: true,
            ..crate::core::config::Wiring::default()
        };
        Core::new(
            Box::new(Dc::new(
                DcConfig {
                    isolation_voltage_v: 500.0,
                    relays_open_voltage_v: 500.0,
                    relays_closed_timeout: std::time::Duration::from_secs(5),
                    imd_measurements: 3,
                    ramp_ampere_per_second: 20.0,
                    cable_check_current_limit_a: 2.0,
                    connector: ConnectorKind::Other,
                    internal_over_voltage_duration: std::time::Duration::from_millis(0),
            plausibility_max_spread_v: 50.0,
            plausibility_fault_duration: std::time::Duration::from_millis(0),
            no_energy_timeout: std::time::Duration::from_secs(5),
                },
                crate::core::path::dc::IsolationMonitor::for_wiring(
                    &wiring,
                    crate::core::path::dc::CableCheckOptions::default(),
                ),
                crate::core::path::dc::OverVoltageMonitor::for_wiring(&wiring),
            )),
            Session::new(
                PwmStart::Nominal,
                Limits {
                    max_current_a: 16.0,
                    nr_of_phases_available: 3,
                },
            ),
            CoreParts {
                ready: ReadyGate::default(),
                auth: Auth::new(true),
                faults: Faults::new(true),
                session_ids: test_session_ids(),
                metering: Metering {
                    fail_on_errors: false,
                },
                // Every core fixture boots without waiting, so a test that
                // means to drive the wait arms it by hand; see
                // `the_startup_sequence_waits_for_the_first_meter_reading`.
                initial_meter_timeout: Duration::ZERO,
                hlc,
                energy: test_energy(ChargeMode::Dc),
                persist: test_persist(None),
                // No detector on DC, as production has none.
                soft_oc: None,
            },
        )
    }

    fn enable(source: EnableSource, priority: i64, scope: EnableScope) -> Event {
        reported(source, EnableState::Enable, priority, scope)
    }

    fn disable(source: EnableSource, priority: i64, scope: EnableScope) -> Event {
        reported(source, EnableState::Disable, priority, scope)
    }

    /// A source reporting any of the three states, which is the one wire call.
    fn reported(
        source: EnableSource,
        state: EnableState,
        priority: i64,
        scope: EnableScope,
    ) -> Event {
        Event::Command(Command::EnableDisable {
            source,
            state,
            priority,
            scope,
        })
    }

    /// The transaction effects carry their identity, so containment is a
    /// predicate rather than an equality. These say exactly what the unit
    /// variants used to say and no less.
    fn starts_transaction(effect: &Effect) -> bool {
        matches!(effect, Effect::StartTransaction { .. })
    }

    fn stops_transaction(effect: &Effect) -> bool {
        matches!(effect, Effect::StopTransaction { .. })
    }

    /// Position of the first effect satisfying `pred`, so orderings are asserted
    /// by index rather than by containment.
    fn index_of(effects: &[Effect], pred: impl Fn(&Effect) -> bool) -> usize {
        effects
            .iter()
            .position(pred)
            .unwrap_or_else(|| panic!("effect not emitted, got {effects:?}"))
    }

    fn now() -> Instant {
        Instant::now()
    }

    /// A power path that records what it was handed across the trait surface.
    /// A stop reason and a forwarded path event are both otherwise
    /// unobservable: no effect carries either, so the trait call is the only
    /// place they can be seen.
    #[derive(Default)]
    struct PathRecorder {
        reasons: std::sync::Arc<std::sync::Mutex<Vec<StopReason>>>,
        events: std::sync::Arc<std::sync::Mutex<Vec<PathEvent>>>,
        /// The trait calls in the order they arrived. Two recorders side by
        /// side say what happened but not in which order, and the order is the
        /// whole point of routing a live session through stopping.
        calls: std::sync::Arc<std::sync::Mutex<Vec<&'static str>>>,
        /// The delegate `Core::new` handed this path, kept so a test can
        /// allocate a path side identity: a real one, from the port's one
        /// space, that the core's own await was never given.
        ids: PathIdentities,
        /// Whether an authorization has asked the core to open a billing
        /// record. The three real paths raise that duty by crossing
        /// `WaitingForAuthentication` to `PrepareCharging`; the recorder runs
        /// no state machine, so it raises it from the authorization directly.
        /// Without it no test over this path can open a record at all, and
        /// several are about what the record's own effect carries.
        opened_a_record: bool,
    }

    impl PowerPath for PathRecorder {
        fn name(&self) -> &'static str {
            "path_recorder"
        }
        fn adopt_effect_ids(&mut self, ids: EffectIds<effect::ByPath>) {
            *self.ids.lock().unwrap() = Some(ids);
        }
        /// The recorder runs no state machine, so it reports the state a port
        /// with no vehicle is in and crosses nothing.
        fn state(&self) -> AcState {
            AcState::Idle
        }
        fn take_entered_states(&mut self) -> Vec<AcState> {
            Vec::new()
        }
        /// The recorder offers no pilot duty, so it signals no current. It
        /// rests in `Idle`, where the soft overcurrent check does not run, so
        /// nothing measures against this.
        fn signalled_current_a(&self) -> f64 {
            0.0
        }

        /// The recorder is the fourth power path and it answers every one of
        /// the trait's mode facts explicitly, like the other three. It drives
        /// no supply, holds no ISO 15118 session, presents no fake DC and
        /// holds no budget, so each answer is the empty one; what matters is
        /// that they are written here rather than inherited from a default
        /// nobody read.
        fn target_voltage_v(&self) -> f64 {
            0.0
        }
        fn hlc_charging_active(&self) -> bool {
            false
        }
        fn presents_fake_dc(&self) -> bool {
            false
        }
        /// It rests in `Idle` and never reaches a paused state, so the one
        /// reader of this never asks it.
        fn power_available(&self) -> bool {
            false
        }
        fn on_startup(&mut self) -> Vec<Effect> {
            self.calls.lock().unwrap().push("on_startup");
            Vec::new()
        }
        fn on_session_start(&mut self, _: &Session, _: Instant) -> Vec<Effect> {
            self.calls.lock().unwrap().push("on_session_start");
            Vec::new()
        }
        fn on_authorized(&mut self, _: &Session, _: Instant) -> Vec<Effect> {
            self.opened_a_record = true;
            Vec::new()
        }
        fn on_bsp(
            &mut self,
            _: &Session,
            _: &crate::core::event::BspEvent,
            _: crate::core::event::CpEdges,
            _: Instant,
        ) -> Vec<Effect> {
            self.calls.lock().unwrap().push("on_bsp");
            Vec::new()
        }
        fn on_limits_changed(&mut self, _: &Session, _: Instant) -> Vec<Effect> {
            Vec::new()
        }
        fn on_stop(&mut self, _: &Session, reason: StopReason, _: Instant) -> Vec<Effect> {
            self.reasons.lock().unwrap().push(reason);
            self.calls.lock().unwrap().push("stop");
            Vec::new()
        }
        fn on_timer(
            &mut self,
            _: &Session,
            _: crate::core::effect::TimerId,
            _: Instant,
        ) -> Vec<Effect> {
            Vec::new()
        }
        fn on_effect_done(
            &mut self,
            _: &Session,
            _: Option<crate::core::effect::EffectId>,
            _: &crate::core::effect::EffectOutcome,
            _: Instant,
        ) -> Vec<Effect> {
            self.calls.lock().unwrap().push("on_effect_done");
            Vec::new()
        }
        fn on_path_event(&mut self, _: &Session, event: PathEvent, _: Instant) -> Vec<Effect> {
            self.events.lock().unwrap().push(event);
            self.calls.lock().unwrap().push("path event");
            Vec::new()
        }
        /// The recording path observes no state, so it raises no duty. A test
        /// that needs one drives a real AC path.
        fn take_session_duties(&mut self) -> Vec<SessionDuty> {
            if std::mem::take(&mut self.opened_a_record) {
                return vec![SessionDuty::StartTransaction];
            }
            Vec::new()
        }
        fn to_safe_state(&mut self) -> Vec<Effect> {
            self.calls.lock().unwrap().push("to_safe_state");
            Vec::new()
        }
    }

    /// A power path's handle on the core's one space, shared out of the
    /// recorder so a test can draw from it.
    type PathIdentities = std::sync::Arc<std::sync::Mutex<Option<EffectIds<effect::ByPath>>>>;

    /// One identity allocated by the power path.
    ///
    /// The other half of the compile time rule, read at run time: this is a
    /// live identity out of the port's single counter, and the core must forward
    /// its completion rather than answer it, because the core's own await was
    /// never handed this.
    fn path_identity(ids: &PathIdentities) -> EffectId {
        ids.lock()
            .unwrap()
            .as_mut()
            .expect("Core::new delegates the space before any session moves")
            .allocate()
            .effect_id()
    }

    type RecordedReasons = std::sync::Arc<std::sync::Mutex<Vec<StopReason>>>;
    type RecordedEvents = std::sync::Arc<std::sync::Mutex<Vec<PathEvent>>>;
    type RecordedCalls = std::sync::Arc<std::sync::Mutex<Vec<&'static str>>>;

    /// A core over a recording path, sharing the recorder's handles.
    fn core_recording_path() -> (Core, RecordedEvents, RecordedCalls) {
        let (core, _, events, calls) = core_recording_everything();
        (core, events, calls)
    }

    /// The same core, with the power path's handle on the space as well, for
    /// the tests that need an identity the core did not issue to itself.
    fn core_recording_path_identities() -> (Core, PathIdentities, RecordedCalls) {
        let (core, _, ids, _, calls) = core_recording_all();
        (core, ids, calls)
    }

    fn core_recording_everything() -> (Core, RecordedReasons, RecordedEvents, RecordedCalls) {
        let (core, reasons, _, events, calls) = core_recording_all();
        (core, reasons, events, calls)
    }

    /// Every handle the recorder shares. The two wrappers above project from
    /// this, so there is one place a core over a recording path is built.
    fn core_recording_all() -> (
        Core,
        RecordedReasons,
        PathIdentities,
        RecordedEvents,
        RecordedCalls,
    ) {
        let path = Box::new(PathRecorder::default());
        let reasons = std::sync::Arc::clone(&path.reasons);
        let events = std::sync::Arc::clone(&path.events);
        let calls = std::sync::Arc::clone(&path.calls);
        let ids = std::sync::Arc::clone(&path.ids);
        let core = Core::new(
            path,
            Session::new(
                PwmStart::Nominal,
                Limits {
                    max_current_a: 16.0,
                    nr_of_phases_available: 3,
                },
            ),
            CoreParts {
                ready: ReadyGate::default(),
                auth: Auth::new(true),
                faults: Faults::new(true),
                session_ids: test_session_ids(),
                metering: Metering {
                    fail_on_errors: false,
                },
                // Every core fixture boots without waiting, so a test that
                // means to drive the wait arms it by hand; see
                // `the_startup_sequence_waits_for_the_first_meter_reading`.
                initial_meter_timeout: Duration::ZERO,
                hlc: test_hlc(ChargeMode::Ac, false),
                energy: test_energy(ChargeMode::Ac),
                persist: test_persist(None),
                soft_oc: test_soft_oc(),
            },
        );
        (core, reasons, ids, events, calls)
    }

    /// A recording path on a DC port, so the DC facts are gated on and the
    /// path events they produce are observable.
    fn dc_core_recording_path() -> (Core, RecordedEvents, RecordedCalls) {
        let path = Box::new(PathRecorder::default());
        let events = std::sync::Arc::clone(&path.events);
        let calls = std::sync::Arc::clone(&path.calls);
        let core = Core::new(
            path,
            Session::new(
                PwmStart::Nominal,
                Limits {
                    max_current_a: 16.0,
                    nr_of_phases_available: 3,
                },
            ),
            CoreParts {
                ready: ReadyGate::default(),
                auth: Auth::new(true),
                faults: Faults::new(true),
                session_ids: test_session_ids(),
                metering: Metering {
                    fail_on_errors: false,
                },
                // Every core fixture boots without waiting, so a test that
                // means to drive the wait arms it by hand; see
                // `the_startup_sequence_waits_for_the_first_meter_reading`.
                initial_meter_timeout: Duration::ZERO,
                hlc: test_hlc(ChargeMode::Dc, true),
                energy: test_energy(ChargeMode::Dc),
                persist: test_persist(None),
                // No detector on DC, as production has none.
                soft_oc: None,
            },
        );
        (core, events, calls)
    }

    /// A core whose stop reasons are recorded, sharing the recorder's handle.
    fn core_recording_reasons() -> (Core, RecordedReasons) {
        let (core, reasons, _, _) = core_recording_everything();
        (core, reasons)
    }

    fn authorize(accepted: bool, kind: AuthorizationKind) -> Event {
        Event::Command(Command::AuthorizeResponse {
            // The kind is read off the token now, so the fixture carries it
            // there rather than beside it.
            token: id_tag_for_tests("tok", kind == AuthorizationKind::PlugAndCharge),
            status: if accepted {
                AuthorizationStatus::Accepted
            } else {
                AuthorizationStatus::Blocked
            },
            certificate: None,
            tariff: TariffMessages::default(),
            reservation_id: None,
        })
    }

    /// An accepted verdict naming a credential kind and a tariff, which is what
    /// the metering transaction is opened under.
    ///
    /// Separate from `authorize` above rather than replacing it, because the
    /// hundred-odd call sites of that one are asking "an authorization
    /// arrived", not "an authorization of this kind carrying these terms".
    fn authorize_billing(token_type: IdTokenType, tariff: &[&str]) -> Event {
        Event::Command(Command::AuthorizeResponse {
            token: typed_id_tag_for_tests("tok", token_type, false),
            status: AuthorizationStatus::Accepted,
            certificate: None,
            tariff: TariffMessages::new(tariff.iter().map(|text| (*text).to_owned()).collect()),
            reservation_id: None,
        })
    }

    /// The metering transaction one authorization opened, as the boundary would
    /// see it.
    fn metering_start(effects: &[Effect]) -> (Option<IdTag>, Option<String>) {
        effects
            .iter()
            .find_map(|effect| match effect {
                Effect::StartTransaction {
                    id_token,
                    tariff_text,
                    ..
                } => Some((id_token.clone(), tariff_text.clone())),
                _ => None,
            })
            .expect("an accepted authorization starts the metering transaction")
    }

    #[test]
    fn apply_is_deterministic_for_a_fixed_time_input() {
        // The same event at the same instant against equal state yields equal
        // effects. This is what makes long timed sequences testable.
        let instant = now();
        let a = core().apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), instant);
        let b = core().apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), instant);
        assert_eq!(a, b);
    }

    #[test]
    fn a_raised_error_drives_the_path_to_safe_state() {
        let mut core = core();
        let effects = core.apply(
            Event::Error(ErrorEvent {
                source: ErrorSource::Bsp,
                error_type: "evse_board_support/MREC8EmergencyStop".into(),
                sub_type: String::new(),
                vendor_id: String::new(),
                severity: Severity::Medium,
                raised: true,
            }),
            now(),
        );
        assert!(effects.contains(&Effect::AllowPowerOn(false)));
    }

    /// `Charger::emergency_shutdown` (`Charger.cpp:2268`) and
    /// `Charger::error_shutdown` (`:2283`) each close with
    /// `signal_hlc_error(Error_EmergencyShutdown)`, unconditionally and for
    /// either charge mode. The only gate is `r_hlc.empty()`, which the boundary
    /// answers for.
    ///
    /// Both classes are exercised, because the two C++ functions issue the same
    /// call and a fix that reached only one of them would look right.
    #[test]
    fn a_fault_shutdown_tells_the_vehicle_why() {
        for (severity, class) in [(Severity::High, "emergency"), (Severity::Medium, "error")] {
            let mut core = ac_hlc_core();
            let effects = core.apply(
                Event::Error(ErrorEvent {
                    source: ErrorSource::Bsp,
                    error_type: "evse_board_support/MREC8EmergencyStop".into(),
                    sub_type: String::new(),
                    vendor_id: String::new(),
                    severity,
                    raised: true,
                }),
                now(),
            );

            let told = effects
                .iter()
                .position(|effect| {
                    *effect == Effect::HlcUpdate(HlcUpdate::SendError(EvseError::EmergencyShutdown))
                })
                .unwrap_or_else(|| panic!("{class} shutdown must tell the vehicle, {effects:?}"));
            let opened = effects
                .iter()
                .position(|effect| *effect == Effect::AllowPowerOn(false))
                .expect("the contactors still open");
            // `signal_hlc_error` is the last statement of both functions, after
            // `bsp->allow_power_on(false)`.
            assert!(opened < told, "{class}: got {effects:?}");
        }
    }

    /// The same fault on a port with no high level communication has nobody to
    /// tell, which is the `config.enabled` gate every other `HlcPort` producer
    /// reads.
    #[test]
    fn a_fault_shutdown_on_a_port_without_high_level_communication_tells_nobody() {
        let mut core = core();
        let effects = core.apply(
            Event::Error(ErrorEvent {
                source: ErrorSource::Bsp,
                error_type: "evse_board_support/MREC8EmergencyStop".into(),
                sub_type: String::new(),
                vendor_id: String::new(),
                severity: Severity::High,
                raised: true,
            }),
            now(),
        );
        assert!(
            !effects
                .iter()
                .any(|effect| matches!(effect, Effect::HlcUpdate(HlcUpdate::SendError(_)))),
            "got {effects:?}"
        );
    }

    #[test]
    fn a_cleared_error_travels_the_same_route_as_a_raise() {
        // Both are Event::Error. There is no lane on which one is admitted and
        // the other dropped, which is the regression this shape prevents.
        let mut core = core();
        let error = |raised| {
            Event::Error(ErrorEvent {
                source: ErrorSource::Bsp,
                error_type: "evse_board_support/MREC8EmergencyStop".into(),
                sub_type: String::new(),
                vendor_id: String::new(),
                severity: Severity::High,
                raised,
            })
        };

        let raised = core.apply(error(true), now());
        assert!(raised.contains(&Effect::AllowPowerOn(false)));

        let cleared = core.apply(error(false), now());
        assert!(
            !cleared
                .iter()
                .any(|effect| effect.context() == Some(effect::ExecContext::Safety)),
            "a clear does not itself actuate, got {cleared:?}"
        );
        assert!(
            cleared
                .iter()
                .any(|effect| matches!(effect, Effect::ClearError(report)
                    if report.error_type == faults::INOPERATIVE)),
            "the last blocking cause going away clears Inoperative, got {cleared:?}"
        );
    }

    #[test]
    fn a_dead_timer_thread_de_energizes_and_releases_the_vehicle_at_once() {
        let mut core = core();
        let effects = core.apply(Event::TimerThreadDied, now());

        // The last withdrawal, not the first. The error raise emits one of its
        // own before the safe state does, so anchoring on the first would hold
        // wherever the release sat and would assert nothing.
        let power = effects
            .iter()
            .rposition(|effect| *effect == Effect::AllowPowerOn(false))
            .expect("power permission is withdrawn");
        let release = effects
            .iter()
            .position(|effect| *effect == Effect::UnlockConnector)
            .expect("the vehicle is released rather than held for a deadline that cannot arrive");

        assert!(
            power < release,
            "the cable is de-energized before the vehicle is let go"
        );
    }

    #[test]
    fn a_dead_timer_thread_names_the_cause_on_the_interface() {
        let mut core = core();
        let effects = core.apply(Event::TimerThreadDied, now());
        let report = effects
            .iter()
            .find_map(|effect| match effect {
                Effect::RaiseError(report) if report.error_type == INTERNAL => Some(report),
                _ => None,
            })
            .expect("an operator learns why the port went out of service");

        // The emergency class, unlike the session id refusal which shares this
        // error type. That one declines one session and the port keeps serving;
        // this one ends its service, so it is not the ordinary error class.
        assert_eq!(report.severity, Severity::High);
    }

    #[test]
    fn shutdown_reaches_safe_state_and_then_ignores_further_events() {
        let mut core = core();
        let effects = core.apply(Event::Shutdown, now());
        assert!(effects.contains(&Effect::AllowPowerOn(false)));

        let after = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::C)), now());
        assert!(after.is_empty());
    }

    #[test]
    fn stopping_an_active_transaction_emits_exactly_one_stop() {
        let mut core = core_awaiting_authorization();
        core.apply(
            Event::Command(Command::AuthorizeResponse {
                token: id_tag_for_tests("tok", false),
                status: AuthorizationStatus::Accepted,
                certificate: None,
                tariff: TariffMessages::default(),
                reservation_id: None,
            }),
            now(),
        );

        let first = core.apply(
            Event::Command(Command::StopTransaction {
                reason: StopTransactionReason::Local,
                id_tag: None,
            }),
            now(),
        );
        assert!(first.iter().any(stops_transaction));

        let second = core.apply(
            Event::Command(Command::StopTransaction {
                reason: StopTransactionReason::Local,
                id_tag: None,
            }),
            now(),
        );
        assert!(
            !second.iter().any(stops_transaction),
            "stop must not be emitted twice"
        );
    }

    #[test]
    fn a_rejected_authorization_starts_no_transaction() {
        let mut core = core();
        let effects = core.apply(
            Event::Command(Command::AuthorizeResponse {
                token: id_tag_for_tests("tok", false),
                status: AuthorizationStatus::Blocked,
                certificate: None,
                tariff: TariffMessages::default(),
                reservation_id: None,
            }),
            now(),
        );
        assert!(effects.is_empty());
        assert!(!core.session().transaction_active);
    }

    #[test]
    fn startup_announces_the_enable_state_before_it_publishes_ready() {
        // The C++ comment at `EvseManager.cpp:1509-1510` gives the reason:
        // other modules read the enable state on startup, so it has to be
        // observable before `ready` tells them the module is up.
        let mut core = core();

        let effects = core.apply(Event::Startup, now());

        let announced = index_of(&effects, |effect| {
            matches!(
                effect,
                Effect::PublishEnableEvent {
                    event: SessionEvent::Enabled,
                    ..
                }
            )
        });
        let ready = index_of(&effects, |effect| {
            matches!(effect, Effect::PublishReady(true))
        });
        assert!(
            announced < ready,
            "the enable state must precede ready, got {effects:?}"
        );
    }

    #[test]
    fn startup_announces_disabled_when_a_disable_already_won() {
        // A disable that arrives before the framework calls on_ready is already
        // in the table, so the boot announcement reports it. In the C++ the
        // same request has driven the state machine to Disabled by then
        // (`Charger.cpp:1769` then `:1691`).
        let mut core = core();
        core.apply(
            disable(EnableSource::Csms, 100, EnableScope::Connector),
            now(),
        );

        let effects = core.apply(Event::Startup, now());

        assert!(effects.iter().any(|effect| matches!(
            effect,
            Effect::PublishEnableEvent {
                event: SessionEvent::Disabled,
                ..
            }
        )));
    }

    #[test]
    fn startup_enables_the_board_support_output_with_the_ready_sequence() {
        // `Charger::main_thread` calls `bsp->enable(true)` first and publishes
        // its initial values after (`Charger.cpp:98-103`), and without it the
        // port never drives the control pilot at all. But that thread is
        // started by `charger->run()`, which the ready sequence calls
        // (`EvseManager.cpp:1534`), so the enable belongs **after** the
        // publications `init` makes and before the enable state this module
        // announces from the same sequence.
        let mut core = core();

        let effects = core.apply(Event::Startup, now());

        let enabled = effects
            .iter()
            .position(|effect| *effect == Effect::BspEnable(true))
            .expect("the board is enabled");
        let waiting = effects
            .iter()
            .position(|effect| matches!(effect, Effect::PublishWaitingForExternalReady(_)))
            .expect("the init publication");
        let announced = effects
            .iter()
            .position(|effect| matches!(effect, Effect::PublishEnableEvent { .. }))
            .expect("the enable announcement");
        assert!(waiting < enabled && enabled < announced, "got {effects:?}");
    }

    /// A port told to wait for an external signal has no state machine running
    /// until it arrives, because `charger->run()` is inside the gate.
    ///
    /// This test asserted the opposite: that the board output came up anyway,
    /// on the argument that the C++ enable is unconditional. It is
    /// unconditional within the thread, and the thread is what the gate holds
    /// back -- so the port was driving its control pilot, and able to actuate a
    /// plug in, while waiting for permission to start.
    #[test]
    fn a_port_waiting_for_a_signal_does_not_enable_the_board_until_it_arrives() {
        let mut core = core_with(ReadyGate {
            awaits_external_signal: true,
        });

        let waiting = core.apply(Event::Startup, now());

        assert!(
            !waiting.contains(&Effect::BspEnable(true)),
            "got {waiting:?}"
        );

        let signalled = core.apply(
            Event::Command(Command::ExternalReadyToStartCharging),
            now(),
        );

        assert!(
            signalled.contains(&Effect::BspEnable(true)),
            "the signal starts it: {signalled:?}"
        );
    }

    /// And the vehicle cannot make it start either. A plug in arriving while
    /// the port waits actuates nothing, which is the whole of what the gate is
    /// for: there is no state machine to hand it to.
    #[test]
    fn a_plug_in_on_a_port_waiting_for_a_signal_actuates_nothing() {
        let mut core = core_with(ReadyGate {
            awaits_external_signal: true,
        });
        core.apply(Event::Startup, now());

        let plugged_in = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());

        assert!(
            !plugged_in.iter().any(|effect| matches!(
                effect,
                Effect::PwmOn(_) | Effect::AllowPowerOn(true) | Effect::SetCpState(_)
            )),
            "got {plugged_in:?}"
        );
    }

    #[test]
    fn startup_reports_that_it_is_not_waiting_for_an_external_signal() {
        let mut core = core();

        let effects = core.apply(Event::Startup, now());

        assert!(effects.contains(&Effect::PublishWaitingForExternalReady(false)));
    }

    #[test]
    fn startup_holds_ready_back_when_an_external_signal_is_configured() {
        let mut core = core_with(ReadyGate {
            awaits_external_signal: true,
        });

        let effects = core.apply(Event::Startup, now());

        assert!(effects.contains(&Effect::PublishWaitingForExternalReady(true)));
        assert!(
            !effects.contains(&Effect::PublishReady(true)),
            "ready waits for the external command, got {effects:?}"
        );
    }

    #[test]
    fn the_external_ready_command_completes_the_held_back_ready_sequence() {
        let mut core = core_with(ReadyGate {
            awaits_external_signal: true,
        });
        core.apply(Event::Startup, now());

        let effects = core.apply(Event::Command(Command::ExternalReadyToStartCharging), now());

        let announced = index_of(&effects, |effect| {
            matches!(effect, Effect::PublishEnableEvent { .. })
        });
        let ready = index_of(&effects, |effect| {
            matches!(effect, Effect::PublishReady(true))
        });
        assert!(announced < ready);
    }

    #[test]
    fn the_ready_sequence_runs_at_most_once() {
        // `ready_to_start_charging` warns and returns on a second call, on
        // its `charger_ready` guard.
        let mut core = core();
        core.apply(Event::Startup, now());

        let again = core.apply(Event::Command(Command::ExternalReadyToStartCharging), now());

        assert!(again.is_empty(), "got {again:?}");
    }

    /// The startup wait `initial_meter_value_timeout_ms` buys, at
    /// `EvseManager::ready`'s `powermeter_cv.wait_for`, and what it holds
    /// back.
    ///
    /// The setting was parsed and never read, so a restart announced the
    /// resume, closed the interrupted record and reported itself ready before
    /// the meter had said anything. The `TransactionFinished` that closes a
    /// record recovered from a power loss is built from the boundary's latest
    /// reading, so with nothing reported it named zero energy: the CSMS was
    /// told the interrupted session drew nothing at all.
    mod the_initial_meter_wait {
        use super::*;

        fn a_reading() -> Event {
            Event::Meter(MeterReading {
                energy_wh_import: 4_200.0,
                power_w: None,
                voltage_v: 230.0,
                current_a: 0.0,
                phase_currents_a: None,
                dc_voltage_v: None,
            })
        }

        /// The default, five seconds, on a port with a record to recover.
        fn core_waiting(recovered: Option<&str>) -> Core {
            let mut core = core();
            core.persist = test_persist(recovered);
            core.initial_meter_timeout = Duration::from_secs(5);
            core
        }

        /// The session events the pass published. The availability
        /// announcement travels on `Effect::PublishEnableEvent` instead, which
        /// is why it is absent from every list below; `reports_ready` is the
        /// end of the sequence these tests read.
        fn announced(effects: &[Effect]) -> Vec<SessionEvent> {
            effects
                .iter()
                .filter_map(|effect| match effect {
                    Effect::PublishSessionEvent(report) => Some(report.event),
                    _ => None,
                })
                .collect()
        }

        fn reports_ready(effects: &[Effect]) -> bool {
            effects.contains(&Effect::PublishReady(true))
        }

        #[test]
        fn the_boot_holds_the_recovery_and_the_ready_report() {
            let mut core = core_waiting(Some("left-over-uuid"));

            let booted = core.apply(Event::Startup, now());

            assert!(
                booted.contains(&Effect::StartTimer {
                    id: TIMER_INITIAL_METER_VALUE,
                    after: Duration::from_secs(5)
                }),
                "got {booted:?}"
            );
            assert!(announced(&booted).is_empty(), "got {booted:?}");
            assert!(!reports_ready(&booted), "got {booted:?}");
            assert!(
                !booted.iter().any(|effect| matches!(
                    effect,
                    Effect::StopTransaction { .. } | Effect::CancelAllTransactions
                )),
                "nothing closed the record yet: {booted:?}"
            );
        }

        /// The reading is what the C++ notification releases, and the timer it
        /// no longer needs goes with it.
        #[test]
        fn the_first_reading_releases_the_rest_of_the_sequence() {
            let mut core = core_waiting(Some("left-over-uuid"));
            core.apply(Event::Startup, now());

            let released = core.apply(a_reading(), now());

            assert!(
                released.contains(&Effect::CancelTimer {
                    id: TIMER_INITIAL_METER_VALUE
                }),
                "got {released:?}"
            );
            assert_eq!(
                announced(&released),
                vec![
                    SessionEvent::SessionResumed,
                    SessionEvent::TransactionFinished,
                ],
                "got {released:?}"
            );
            assert!(reports_ready(&released), "got {released:?}");
        }

        /// And the deadline releases it with nothing reported, which is
        /// `wait_for` returning on the timeout rather than on the predicate.
        /// A meter that never reports must not leave the port unable to charge.
        #[test]
        fn the_deadline_releases_it_with_no_reading_at_all() {
            let mut core = core_waiting(Some("left-over-uuid"));
            core.apply(Event::Startup, now());

            let released = core.apply(
                Event::Timer {
                    id: TIMER_INITIAL_METER_VALUE,
                    generation: 0,
                },
                now(),
            );

            assert_eq!(
                announced(&released),
                vec![
                    SessionEvent::SessionResumed,
                    SessionEvent::TransactionFinished,
                ],
                "got {released:?}"
            );
            assert!(reports_ready(&released), "got {released:?}");
        }

        /// Whichever arrives first releases it, and the other releases nothing:
        /// the C++ predicate is checked once and the thread runs once.
        #[test]
        fn the_sequence_runs_once_whichever_releases_it() {
            for second in [a_reading, || Event::Timer {
                id: TIMER_INITIAL_METER_VALUE,
                generation: 0,
            }] {
                let mut core = core_waiting(Some("left-over-uuid"));
                core.apply(Event::Startup, now());
                let first = core.apply(a_reading(), now());
                assert!(reports_ready(&first), "the control: {first:?}");

                let again = core.apply(second(), now());

                // `CancelAllTransactions` and the waiting report are the two
                // steps of the sequence that are not idempotent, so they are
                // what a second run shows. The announcements are idempotent by
                // accident: the record has been cleared and the ready report
                // refuses a second time, so a check reading only those passes
                // whether the sequence ran again or not.
                assert!(
                    !again.iter().any(|effect| matches!(
                        effect,
                        Effect::CancelAllTransactions
                            | Effect::PublishWaitingForExternalReady(_)
                    )),
                    "the sequence ran again: {again:?}"
                );
                assert!(announced(&again).is_empty(), "got {again:?}");
                assert!(!reports_ready(&again), "got {again:?}");
            }
        }

        /// A reading that arrived before the boot means there is nothing to
        /// wait for, which is the C++ subscribing before it waits: the
        /// predicate is already true when `wait_for` reaches it.
        #[test]
        fn a_reading_before_the_boot_leaves_nothing_to_wait_for() {
            let mut core = core_waiting(Some("left-over-uuid"));
            core.apply(a_reading(), now());

            let booted = core.apply(Event::Startup, now());

            assert!(
                !booted.iter().any(|effect| matches!(
                    effect,
                    Effect::StartTimer {
                        id: TIMER_INITIAL_METER_VALUE,
                        ..
                    }
                )),
                "got {booted:?}"
            );
            assert!(reports_ready(&booted), "got {booted:?}");
        }

        /// A configured zero does not wait, which the setting's own
        /// description says outright. Every other core fixture here is built
        /// that way, so this is the one that names it.
        #[test]
        fn a_configured_zero_does_not_wait() {
            let mut core = core_waiting(Some("left-over-uuid"));
            core.initial_meter_timeout = Duration::ZERO;

            let booted = core.apply(Event::Startup, now());

            assert!(
                !booted.iter().any(|effect| matches!(
                    effect,
                    Effect::StartTimer {
                        id: TIMER_INITIAL_METER_VALUE,
                        ..
                    }
                )),
                "got {booted:?}"
            );
            assert!(reports_ready(&booted), "got {booted:?}");
        }

        /// The energy tree is asked before the wait rather than behind it, and
        /// that is the one deliberate departure from the C++ order: the ask
        /// lives in a different implementation's `ready`, so nothing orders it
        /// against `EvseManager::ready`'s wait. A port whose meter never
        /// reports must not be missing from the tree for the whole timeout.
        #[test]
        fn the_energy_tree_is_asked_before_the_wait() {
            let mut core = core_waiting(None);

            let booted = core.apply(Event::Startup, now());

            assert!(
                booted
                    .iter()
                    .any(|effect| matches!(effect, Effect::PublishEnergyFlowRequest(_))),
                "got {booted:?}"
            );
        }
    }

    /// Every deadline in the module, and no two of them share an identity.
    ///
    /// One space, not one per owner, because `Core::apply` matches its own two
    /// identities before it offers a deadline to the power path: a path timer
    /// that collided with one of those would never reach the path at all, and
    /// nothing downstream would notice. The `ac_with_soc` deadline shipped that
    /// way in its first revision, against `TIMER_ENERGY_FLOW_REQUEST`, and only
    /// a test that drove the whole core saw it.
    ///
    /// The `TStep` group is deliberately absent from the count of distinct
    /// owners rather than from this list: its three reducer timers share
    /// `ac::TIMER_T_STEP` on purpose, which is a sharing decided in one place
    /// and asserted in `path::ac`.
    #[test]
    fn every_timer_identity_in_the_module_is_unique() {
        let identities = [
            ("energy flow request", crate::core::energy::TIMER_ENERGY_FLOW_REQUEST),
            ("soft over current", crate::core::soft_oc::TIMER_SOFT_OVER_CURRENT),
            ("five percent fallback", crate::core::path::ac::TIMER_FIVE_PERCENT_FALLBACK),
            ("wait for energy", crate::core::path::ac::TIMER_WAIT_FOR_ENERGY),
            ("stopping charging", crate::core::path::ac::TIMER_STOPPING_CHARGING),
            ("C1", crate::core::path::ac::TIMER_C1),
            ("state F unlock", crate::core::path::ac::TIMER_CP_STATE_F_UNLOCK),
            ("pilot detour", crate::core::path::ac::TIMER_T_STEP),
            ("switch phases", crate::core::path::ac::TIMER_SWITCH_PHASES),
            ("reinit hold", crate::core::path::ac::TIMER_REINIT),
            ("fake DC refresh", crate::core::path::ac_with_soc::TIMER_DC_REFRESH),
            ("cable check", crate::core::path::dc::TIMER_CABLE_CHECK),
            ("contactor confirm", crate::core::path::dc::TIMER_CONTACTOR_CONFIRM),
            ("enforce limits", crate::core::path::dc::TIMER_ENFORCE_LIMITS),
            ("over voltage error", crate::core::path::dc::TIMER_OVER_VOLTAGE_ERROR),
            ("voltage plausibility", crate::core::path::dc::TIMER_PLAUSIBILITY),
            ("DC no energy", crate::core::path::dc::TIMER_NO_ENERGY),
            ("high level no energy", crate::core::path::ac::TIMER_HLC_NO_ENERGY),
            ("budget validity", crate::core::energy::TIMER_BUDGET_VALIDITY),
            ("initial meter value", TIMER_INITIAL_METER_VALUE),
        ];

        for (i, (name, id)) in identities.iter().enumerate() {
            for (other_name, other_id) in &identities[i + 1..] {
                assert_ne!(id, other_id, "`{name}` and `{other_name}` share {id:?}");
            }
        }
    }

    /// The `ac_with_soc` mode seen from the coordinator: the mode reaches the
    /// stack and the session setup, and the flip's announcement is discharged
    /// as a duty.
    mod ac_with_soc {
        use super::*;
        use crate::core::path::ac_with_soc::AcWithSoc;
        use serde_json::json;

        fn core() -> Core {
            Core::new(
                Box::new(AcWithSoc::new(IecConfig {
                    initial_current_limit_a: 16.0,
                    has_ventilation: true,
                    lock_connector_in_state_b: true,
                    switch_phases_cp_state: SwitchCpState::X1,
                    switch_phases_delay: std::time::Duration::from_secs(10),
                    reinit_method: crate::core::config::ReinitMethod::CpStateF,
                    reinit_duration: std::time::Duration::from_millis(3000),
                    hlc_no_energy_timeout: std::time::Duration::from_secs(5),
                    type2_socket: false,
                })),
                Session::new(
                    PwmStart::Nominal,
                    Limits {
                        max_current_a: 16.0,
                        nr_of_phases_available: 3,
                    },
                ),
                CoreParts {
                    ready: ReadyGate::default(),
                    auth: Auth::new(true),
                    faults: Faults::new(true),
                    session_ids: test_session_ids(),
                    metering: Metering {
                        fail_on_errors: false,
                    },
                    hlc: test_hlc_with(ChargeMode::Ac, true, &[("ac_with_soc", json!(true))]),
                    energy: test_energy(ChargeMode::Ac),
                    persist: test_persist(None),
                    soft_oc: test_soft_oc(),
                    initial_meter_timeout: Duration::ZERO,
                },
            )
        }

        fn setups(effects: &[Effect]) -> Vec<bool> {
            effects
                .iter()
                .filter_map(|effect| match effect {
                    Effect::HlcUpdate(HlcUpdate::SessionSetup(setup)) => Some(setup.fake_dc),
                    _ => None,
                })
                .collect()
        }

        fn transfer_modes(effects: &[Effect]) -> Vec<Vec<EnergyTransferMode>> {
            effects
                .iter()
                .filter_map(|effect| match effect {
                    Effect::HlcUpdate(HlcUpdate::TransferModes(modes)) => Some(modes.clone()),
                    _ => None,
                })
                .collect()
        }

        /// `EvseManager` announces the derived set in `init` and then overwrites
        /// it with the fake DC pair in `ready`, which is the order the boot
        /// sequence and the path's own startup reach here.
        #[test]
        fn the_boot_announces_the_derived_set_and_then_the_fake_dc_one() {
            let effects = core().apply(Event::Startup, now());

            assert_eq!(
                transfer_modes(&effects),
                vec![
                    // The derived AC set, from the safe default capabilities a
                    // board that has not reported yet leaves standing.
                    vec![EnergyTransferMode::AcSinglePhase],
                    vec![
                        EnergyTransferMode::DcExtended,
                        EnergyTransferMode::DcCore
                    ],
                ],
                "got {effects:?}"
            );
        }

        /// The argument the port used to pass as a constant `false`.
        #[test]
        fn the_boot_session_setup_names_the_fake_dc_mode() {
            let effects = core().apply(Event::Startup, now());
            assert_eq!(setups(&effects), vec![true], "got {effects:?}");
        }

        /// And once the flip has happened, every later trigger point says so.
        #[test]
        fn a_session_setup_after_the_flip_names_the_ac_mode() {
            let mut core = core();
            core.apply(Event::Startup, now());
            core.apply(
                Event::Hlc(HlcEvent::StateOfCharge { percent: 50.0 }),
                now(),
            );

            let effects = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());

            assert_eq!(
                setups(&effects),
                vec![false],
                "the plug in re-derives the setup, got {effects:?}"
            );
        }

        /// The duty the path raises is discharged through the port that owns the
        /// identity, so the announcement reaches the stack rather than staying
        /// a decision nobody carried out.
        #[test]
        fn the_flip_announcement_reaches_the_stack() {
            let mut core = core();
            core.apply(Event::Startup, now());

            let effects = core.apply(
                Event::Hlc(HlcEvent::StateOfCharge { percent: 50.0 }),
                now(),
            );

            // The AC side of a flip announces nothing, which is why the mode is
            // read back off the path instead of off the wire here.
            assert!(!core.path.presents_fake_dc());
            assert_eq!(transfer_modes(&effects), Vec::<Vec<_>>::new());

            // The deadline brings it back, and that direction does announce.
            let back = core.apply(
                Event::Timer {
                    id: crate::core::path::ac_with_soc::TIMER_DC_REFRESH,
                    generation: 0,
                },
                now(),
            );
            assert_eq!(
                transfer_modes(&back),
                vec![vec![
                    EnergyTransferMode::DcExtended,
                    EnergyTransferMode::DcCore
                ]],
                "got {back:?}"
            );
        }

        /// The SLAC report is two facts, and the narrower one has to survive the
        /// trip through the core: without it the reinitialization would break the
        /// pilot under a matched link.
        #[test]
        fn the_matched_half_of_the_slac_report_reaches_the_path() {
            let mut core = core();
            core.apply(Event::Startup, now());
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
            core.apply(Event::Hlc(HlcEvent::SlacMatched(true)), now());

            let effects = core.apply(
                Event::Hlc(HlcEvent::StateOfCharge { percent: 50.0 }),
                now(),
            );

            assert!(
                effects.contains(&Effect::HlcUpdate(HlcUpdate::StopCharging(true))),
                "the vehicle is asked to end the session first, got {effects:?}"
            );
            assert_ne!(core.path.state(), AcState::Reinit);

            let released = core.apply(Event::Hlc(HlcEvent::SlacMatched(false)), now());
            assert_eq!(core.path.state(), AcState::Reinit, "got {released:?}");
        }
    }

    /// A core over the AC path that speaks ISO 15118, which is the deployment
    /// whose advertised set is derived from board support capabilities.
    fn ac_hlc_core() -> Core {
        ac_hlc_core_with(&[])
    }

    fn ac_hlc_core_with(pairs: &[(&str, serde_json::Value)]) -> Core {
        Core::new(
            Box::new(crate::core::path::ac::AcHlc::new(
                IecConfig {
                    initial_current_limit_a: 16.0,
                    has_ventilation: true,
                    lock_connector_in_state_b: true,
                    switch_phases_cp_state: SwitchCpState::X1,
                    switch_phases_delay: std::time::Duration::from_secs(10),
                    reinit_method: crate::core::config::ReinitMethod::CpStateF,
                    reinit_duration: std::time::Duration::from_millis(3000),
                    hlc_no_energy_timeout: std::time::Duration::from_secs(5),
                    type2_socket: false,
                },
                PwmStart::Nominal,
            )),
            Session::new(
                PwmStart::Nominal,
                Limits {
                    max_current_a: 16.0,
                    nr_of_phases_available: 3,
                },
            ),
            CoreParts {
                ready: ReadyGate::default(),
                auth: Auth::new(true),
                faults: Faults::new(true),
                session_ids: test_session_ids(),
                metering: Metering {
                    fail_on_errors: false,
                },
                // Every core fixture boots without waiting, so a test that
                // means to drive the wait arms it by hand; see
                // `the_startup_sequence_waits_for_the_first_meter_reading`.
                initial_meter_timeout: Duration::ZERO,
                hlc: test_hlc_with(ChargeMode::Ac, true, pairs),
                energy: test_energy(ChargeMode::Ac),
                persist: test_persist(None),
                soft_oc: test_soft_oc(),
            },
        )
    }

    /// The authorization bridge, driven through the core.
    ///
    /// `authz` owns the decisions and tests them directly; what is asserted
    /// here is the wiring, which is where a port of this shape goes wrong: the
    /// waiting flags reset at the wrong edge, the permission read off the wrong
    /// state, or a verdict that the bridge dropped still reaching the
    /// authorization state machine.
    mod the_authorization_bridge {
        use super::*;
        use crate::core::hlc::{AuthorizationResponse, OpaqueToken, ProvidedToken};

        fn a_contract_token() -> OpaqueToken {
            OpaqueToken::new(serde_json::json!({
                "id_token": {"value": "CONTRACT", "type": "eMAID"},
                "authorization_type": "PlugAndCharge",
                "connectors": [7],
            }))
        }

        fn require_plug_and_charge() -> Event {
            Event::Hlc(HlcEvent::RequireAuthPlugAndCharge {
                token: a_contract_token(),
            })
        }

        fn answers(effects: &[Effect]) -> Vec<AuthorizationResponse> {
            effects
                .iter()
                .filter_map(|effect| match effect {
                    Effect::HlcUpdate(HlcUpdate::AuthorizationResponse(response)) => {
                        Some(*response)
                    }
                    _ => None,
                })
                .collect()
        }

        fn offered(effects: &[Effect]) -> Vec<ProvidedToken> {
            effects
                .iter()
                .filter_map(|effect| match effect {
                    Effect::PublishProvidedToken(token) => Some(token.clone()),
                    _ => None,
                })
                .collect()
        }

        #[test]
        fn a_pending_request_is_answered_when_the_permission_arrives() {
            let mut core = ac_hlc_core();
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
            core.apply(Event::Hlc(HlcEvent::RequireAuthEim), now());

            let effects = core.apply(authorize(true, AuthorizationKind::Eim), now());

            assert_eq!(
                answers(&effects),
                vec![AuthorizationResponse::GRANTED_EIM],
                "{effects:?}"
            );
        }

        #[test]
        fn a_contract_permission_answers_the_contract_request_that_asked_for_it() {
            let mut core = ac_hlc_core();
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
            core.apply(require_plug_and_charge(), now());

            let effects = core.apply(authorize(true, AuthorizationKind::PlugAndCharge), now());

            assert_eq!(
                answers(&effects),
                vec![AuthorizationResponse::GRANTED_PLUG_AND_CHARGE],
                "{effects:?}"
            );
        }

        #[test]
        fn a_contract_request_offers_the_token_to_the_authorization_module() {
            let mut core = ac_hlc_core();
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());

            let effects = core.apply(require_plug_and_charge(), now());

            assert_eq!(
                offered(&effects),
                vec![ProvidedToken::PlugAndCharge {
                    token: a_contract_token(),
                    connectors: vec![1],
                }],
                "{effects:?}"
            );
        }

        #[test]
        fn the_permission_the_gate_reads_is_the_one_the_authorization_state_machine_holds() {
            // A contract request answered by an ordinary permission is not an
            // answer, so the kind has to come off `Auth` rather than off the
            // verdict that arrived.
            let mut core = ac_hlc_core();
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
            core.apply(Event::Hlc(HlcEvent::RequireAuthEim), now());

            let effects = core.apply(authorize(true, AuthorizationKind::PlugAndCharge), now());

            assert!(
                answers(&effects).is_empty(),
                "the vehicle asked for an ordinary authorization, got {effects:?}"
            );
        }

        #[test]
        fn a_verdict_the_bridge_drops_records_no_tariff() {
            // `Route::Ignore` returns before `Auth::authorize`, so an external
            // verdict arriving while the vehicle waits for its contract must
            // leave the terms as well as the permission untouched. A tariff
            // recorded here would be billed to whatever contract is admitted
            // next.
            let mut core = ac_hlc_core();
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
            core.apply(require_plug_and_charge(), now());

            let effects = core.apply(
                Event::Command(Command::AuthorizeResponse {
                    token: id_tag_for_tests("card", false),
                    status: AuthorizationStatus::Accepted,
                    certificate: None,
                    tariff: TariffMessages::new(vec!["EUR 0.30/kWh".to_owned()]),
                    reservation_id: None,
                }),
                now(),
            );

            assert!(effects.is_empty(), "the verdict is dropped, got {effects:?}");
            assert_eq!(core.auth().tariff().text(), None);
            assert_eq!(core.session().authorized_tariff.text(), None);
        }

        #[test]
        fn a_contract_grant_bills_under_the_tariff_it_carried() {
            // The other route into `Auth::authorize`: a verdict the vehicle was
            // waiting for. Both routes have to carry the terms, and only this
            // one goes through the stack's own wait.
            let mut core = ac_hlc_core();
            // The port has to be up before a plug in reaches it, the vehicle
            // has to be there before a record can be opened, and the link has
            // to be matched or the pilot detours through state F first
            // (`Charger.cpp:404-420`) and the record waits for the detour.
            core.apply(Event::Startup, now());
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
            core.apply(Event::Hlc(HlcEvent::MatchingStarted(true)), now());
            core.apply(Event::Hlc(HlcEvent::SlacMatched(true)), now());
            core.apply(Event::Hlc(HlcEvent::SetupFinished), now());
            core.apply(require_plug_and_charge(), now());

            let effects = core.apply(
                Event::Command(Command::AuthorizeResponse {
                    token: typed_id_tag_for_tests("CONTRACT", IdTokenType::EMaid, true),
                    status: AuthorizationStatus::Accepted,
                    certificate: None,
                    tariff: TariffMessages::new(vec!["EUR 0.55/kWh".to_owned()]),
                    reservation_id: None,
                }),
                now(),
            );

            let (id_token, tariff_text) = metering_start(&effects);
            assert_eq!(
                id_token.map(|tag| tag.token_type()),
                Some(IdTokenType::EMaid)
            );
            assert_eq!(tariff_text.as_deref(), Some("EUR 0.55/kWh"));
        }

        #[test]
        fn a_vehicle_arriving_clears_what_the_previous_vehicle_was_waiting_for() {
            // The primary reset, and unconditional (`EvseManager.cpp:1123-1130`).
            let mut core = ac_hlc_core();
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
            core.apply(require_plug_and_charge(), now());
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::A)), now());

            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
            let effects = core.apply(authorize(true, AuthorizationKind::PlugAndCharge), now());

            assert!(
                answers(&effects).is_empty(),
                "the new session is nobody's answer, got {effects:?}"
            );
        }

        #[test]
        fn a_vehicle_arriving_clears_an_outstanding_ordinary_request_too() {
            // Every other reset test here arms a contract request, so a reset
            // that took only the contract flag down would satisfy all of them.
            let mut core = ac_hlc_core();
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
            core.apply(Event::Hlc(HlcEvent::RequireAuthEim), now());
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::A)), now());

            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
            let effects = core.apply(authorize(true, AuthorizationKind::Eim), now());

            assert!(
                answers(&effects).is_empty(),
                "the new session is nobody's answer, got {effects:?}"
            );
        }

        fn debugging_core() -> Core {
            ac_hlc_core_with(&[("dbg_hlc_auth_after_tstep", serde_json::json!(true))])
        }

        #[test]
        fn the_debug_arm_reads_the_state_the_charger_is_actually_in() {
            // `charging` is read by the debug arm alone, so without a core test
            // that turns the setting on, nothing proves the fact is wired to
            // the charger's state rather than to a constant. The port is not
            // charging here, so a request finding an authorization already held
            // is answered only if the fact is misreported.
            let mut core = debugging_core();
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
            core.apply(Event::Hlc(HlcEvent::RequireAuthEim), now());
            core.apply(authorize(true, AuthorizationKind::Eim), now());
            assert!(core.auth().authorized_eim());
            assert_ne!(core.path.state(), AcState::Charging);

            let effects = core.apply(Event::Hlc(HlcEvent::RequireAuthEim), now());

            assert!(
                answers(&effects).is_empty(),
                "the authorization is held but not yet in use, got {effects:?}"
            );
        }

        /// A state the C++ accepts, paired with the drive that reaches it.
        type Drive<'a> = (AcState, &'a dyn Fn(&mut Core));

        /// And the case the defect made unreachable. The fact was read off
        /// `SessionPhase::Charging`, a phase production never assigns, so the
        /// answer was always false: a charging session with an external
        /// authorization in hand was never told it was authorized, and the
        /// vehicle waited on a request nothing would answer.
        ///
        /// `get_authorized_eim_ready_for_hlc` reads the charger's state, and
        /// all three states it accepts are states this port has.
        #[test]
        fn a_charging_session_answers_the_debug_arm_at_once() {
            // Each of the three states the C++ accepts, reached the way a
            // session reaches it: the charge itself, the vehicle opening S2,
            // and the EVSE withdrawing its offer.
            let drives: [Drive; 3] = [
                (AcState::Charging, &|_core| {}),
                (AcState::ChargingPausedEv, &|core: &mut Core| {
                    core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
                }),
                (AcState::ChargingPausedEvse, &|core: &mut Core| {
                    core.apply(Event::Command(Command::PauseCharging), now());
                    core.apply(Event::Bsp(BspEvent::Cp(CpEvent::PowerOff)), now());
                }),
            ];
            for (state, reach) in drives {
                let mut core = debugging_core();
                core.apply(Event::Startup, now());
                core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
                core.apply(Event::Hlc(HlcEvent::RequireAuthEim), now());
                core.apply(authorize(true, AuthorizationKind::Eim), now());
                core.apply(Event::Bsp(BspEvent::Cp(CpEvent::C)), now());
                core.apply(Event::Bsp(BspEvent::Cp(CpEvent::PowerOn)), now());
                assert_eq!(core.path.state(), AcState::Charging, "the control");
                reach(&mut core);
                assert_eq!(core.path.state(), state, "the drive reaches {state:?}");

                let effects = core.apply(Event::Hlc(HlcEvent::RequireAuthEim), now());

                assert!(
                    !answers(&effects).is_empty(),
                    "{state:?} is ready for high level communication: {effects:?}"
                );
            }
        }

        #[test]
        fn a_vehicle_leaving_clears_what_it_was_waiting_for() {
            let mut core = ac_hlc_core();
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
            core.apply(require_plug_and_charge(), now());

            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::A)), now());
            let effects = core.apply(authorize(true, AuthorizationKind::PlugAndCharge), now());

            assert!(
                answers(&effects).is_empty(),
                "the vehicle that asked is gone, got {effects:?}"
            );
        }

        #[test]
        fn an_ordinary_permission_arriving_while_a_contract_is_awaited_changes_nothing_at_all() {
            let mut core = ac_hlc_core();
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
            core.apply(require_plug_and_charge(), now());

            let effects = core.apply(authorize(true, AuthorizationKind::Eim), now());

            assert!(effects.is_empty(), "{effects:?}");
            assert!(
                !core.auth().authorized(),
                "the dropped verdict must not reach the authorization state machine"
            );
        }

        #[test]
        fn a_refused_contract_is_reported_to_the_vehicle() {
            let mut core = ac_hlc_core();
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
            core.apply(require_plug_and_charge(), now());

            let effects = core.apply(
                Event::Command(Command::AuthorizeResponse {
                    token: id_tag_for_tests("tok", true),
                    status: AuthorizationStatus::Blocked,
                    certificate: Some(CertificateStatus::CertificateRevoked),
                    tariff: TariffMessages::default(),
                    reservation_id: None,
                }),
                now(),
            );

            assert_eq!(
                answers(&effects),
                vec![AuthorizationResponse {
                    status: AuthorizationStatus::Blocked,
                    certificate: CertificateStatus::CertificateRevoked,
                }],
                "{effects:?}"
            );
        }

        #[test]
        fn a_contract_refused_and_then_granted_still_reaches_the_vehicle() {
            // A refusal answers the vehicle but settles nothing: the C++ clears
            // what is being waited for in `charger_was_authorized`
            // (`EvseManager.cpp:1891-1908`), which a refusal never reaches. A
            // refusal that cleared the flag would leave a later grant
            // unannounced and the vehicle waiting on an answer that never comes.
            let mut core = ac_hlc_core();
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
            core.apply(require_plug_and_charge(), now());
            core.apply(
                Event::Command(Command::AuthorizeResponse {
                    token: id_tag_for_tests("tok", true),
                    status: AuthorizationStatus::Blocked,
                    certificate: Some(CertificateStatus::CertificateRevoked),
                    tariff: TariffMessages::default(),
                    reservation_id: None,
                }),
                now(),
            );

            let effects = core.apply(authorize(true, AuthorizationKind::PlugAndCharge), now());

            assert_eq!(
                answers(&effects),
                vec![AuthorizationResponse::GRANTED_PLUG_AND_CHARGE],
                "{effects:?}"
            );
        }

        #[test]
        fn an_ordinary_refusal_leaves_the_vehicle_waiting_for_the_grant_that_follows() {
            // The reason the C++ gives for telling the vehicle nothing about an
            // external identification refusal (`evse/evse_managerImpl.cpp:449-450`):
            // a successful authorization may still arrive. It has to still be
            // announced when it does.
            let mut core = ac_hlc_core();
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
            core.apply(Event::Hlc(HlcEvent::RequireAuthEim), now());
            let refused = core.apply(authorize(false, AuthorizationKind::Eim), now());
            assert!(answers(&refused).is_empty(), "{refused:?}");

            let effects = core.apply(authorize(true, AuthorizationKind::Eim), now());

            assert_eq!(
                answers(&effects),
                vec![AuthorizationResponse::GRANTED_EIM],
                "{effects:?}"
            );
        }

        #[test]
        fn an_ordinary_refusal_is_not_reported_to_the_vehicle() {
            let mut core = ac_hlc_core();
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
            core.apply(Event::Hlc(HlcEvent::RequireAuthEim), now());

            let effects = core.apply(authorize(false, AuthorizationKind::Eim), now());

            assert!(answers(&effects).is_empty(), "{effects:?}");
        }

        #[test]
        fn autocharge_offers_the_identity_the_stack_reported() {
            let mut core = ac_hlc_core_with(&[("enable_autocharge", serde_json::json!(true))]);
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
            core.apply(
                Event::Hlc(HlcEvent::SessionSetup {
                    evcc_id: "AA:BB:CC:DD:EE:FF".into(),
                }),
                now(),
            );

            let effects = core.apply(Event::Hlc(HlcEvent::RequireAuthEim), now());

            assert_eq!(
                offered(&effects),
                vec![ProvidedToken::Autocharge {
                    id_token: "VID:AABBCCDDEEFF".into(),
                    connectors: vec![1],
                }],
                "{effects:?}"
            );
        }

        #[test]
        fn autocharge_taking_its_identity_from_slac_offers_nothing_here() {
            // `EvseManager.cpp:1016` installs the `evcc_id` handler only when
            // the setting is off, so with it on the stack's identity is not
            // recorded at all and the SLAC arm owns the token. That arm is
            // unported, so nothing is offered.
            let mut core = ac_hlc_core_with(&[
                ("enable_autocharge", serde_json::json!(true)),
                (
                    "autocharge_use_slac_instead_of_hlc",
                    serde_json::json!(true),
                ),
            ]);
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
            core.apply(
                Event::Hlc(HlcEvent::SessionSetup {
                    evcc_id: "AA:BB:CC:DD:EE:FF".into(),
                }),
                now(),
            );

            let effects = core.apply(Event::Hlc(HlcEvent::RequireAuthEim), now());

            assert!(offered(&effects).is_empty(), "{effects:?}");
        }

        #[test]
        fn a_port_without_high_level_communication_answers_no_request_at_all() {
            // The events cannot arrive with no stack wired, and the gate says so
            // rather than relying on that.
            let mut core = core();
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());

            let asked = core.apply(Event::Hlc(HlcEvent::RequireAuthEim), now());
            let effects = core.apply(authorize(true, AuthorizationKind::Eim), now());

            assert!(asked.is_empty(), "{asked:?}");
            assert!(answers(&effects).is_empty(), "{effects:?}");
        }
    }

    /// The narrowing from `HlcEvent` to what the core does with it.
    ///
    /// Every arm of the `Event::Hlc` match, driven through the core. The
    /// routing arms are one line each and read as obviously right, which is
    /// exactly the shape a wrong one has: an inverted boolean or a neighbouring
    /// variant compiles and reads plausibly.
    mod what_the_stack_tells_the_core {
        use super::*;
        use crate::core::effect::SlacUpdate;
        use crate::core::event::HlcSessionFailure;

        /// Each stack fact and the path event it must arrive as. The payload is
        /// part of the row, so a permission that arrives inverted or a pause
        /// that arrives as a terminate is a failure rather than a passing test
        /// about the variant's name.
        #[test]
        fn every_stack_fact_reaches_the_path_as_the_event_the_table_names() {
            let rows: Vec<(HlcEvent, PathEvent)> = vec![
                (
                    HlcEvent::SessionSetup {
                        evcc_id: "AA:BB:CC:DD:EE:FF".into(),
                    },
                    PathEvent::HlcSessionSetup,
                ),
                (HlcEvent::RequiresCableCheck, PathEvent::CableCheckRequired),
                (HlcEvent::PreChargeStarted, PathEvent::PreChargeStarted),
                (
                    HlcEvent::CurrentDemandStarted,
                    PathEvent::CurrentDemandStarted,
                ),
                (
                    HlcEvent::CurrentDemandFinished,
                    PathEvent::CurrentDemandFinished,
                ),
                (
                    HlcEvent::StopFromEv(StopReason::Remote),
                    PathEvent::StopFromEv,
                ),
                (
                    HlcEvent::MatchingStarted(true),
                    PathEvent::MatchingStarted(true),
                ),
                (
                    HlcEvent::MatchingStarted(false),
                    PathEvent::MatchingStarted(false),
                ),
                (HlcEvent::SetupFinished, PathEvent::SetupFinished),
                (
                    HlcEvent::SlacErrorRoutine,
                    PathEvent::SlacErrorRoutine,
                ),
                (
                    HlcEvent::AllowCloseContactor(true),
                    PathEvent::AllowCloseContactor(true),
                ),
                (
                    HlcEvent::AllowCloseContactor(false),
                    PathEvent::AllowCloseContactor(false),
                ),
                (HlcEvent::OpenContactorDc, PathEvent::OpenContactorDc),
                (
                    HlcEvent::DataLinkError,
                    PathEvent::DataLink(DataLinkRequest::Error),
                ),
                (
                    HlcEvent::DataLinkPause,
                    PathEvent::DataLink(DataLinkRequest::Pause),
                ),
                (
                    HlcEvent::DataLinkTerminate,
                    PathEvent::DataLink(DataLinkRequest::Terminate),
                ),
                // The two halves of the DC target are adjacent and both `f64`,
                // which is the shape that transposes silently, so the row
                // carries values that cannot be read either way round.
                (
                    HlcEvent::DcEvTarget {
                        voltage_v: 412.5,
                        current_a: 63.25,
                    },
                    PathEvent::DcEvTarget {
                        voltage_v: 412.5,
                        current_a: 63.25,
                    },
                ),
                (
                    HlcEvent::DcDynamicChargeMode(DynamicModeRequest {
                        max_charge_power_w: 40_000.0,
                        min_charge_power_w: 1_000.0,
                        max_charge_current_a: 250.0,
                        max_voltage_v: 800.0,
                        min_voltage_v: 300.0,
                        max_discharge_power_w: Some(-30_000.0),
                        min_discharge_power_w: Some(-900.0),
                        max_discharge_current_a: Some(-75.0),
                    }),
                    PathEvent::DcDynamicChargeMode(DynamicModeRequest {
                        max_charge_power_w: 40_000.0,
                        min_charge_power_w: 1_000.0,
                        max_charge_current_a: 250.0,
                        max_voltage_v: 800.0,
                        min_voltage_v: 300.0,
                        max_discharge_power_w: Some(-30_000.0),
                        min_discharge_power_w: Some(-900.0),
                        max_discharge_current_a: Some(-75.0),
                    }),
                ),
                (
                    HlcEvent::DcEvMaximumLimits(EvMaximumLimits {
                        maximum_current_a: Some(275.0),
                        maximum_voltage_v: Some(920.0),
                    }),
                    PathEvent::DcEvMaximumLimits(EvMaximumLimits {
                        maximum_current_a: Some(275.0),
                        maximum_voltage_v: Some(920.0),
                    }),
                ),
            ];

            for (fact, expected) in rows {
                let (mut core, events, _) = core_recording_path();
                core.apply(Event::Hlc(fact.clone()), now());
                let events_now = events.lock().unwrap().clone();
                assert_eq!(
                    events_now,
                    vec![expected],
                    "{fact:?} must reach the path as {expected:?}"
                );
            }
        }

        /// The three facts the core answers without involving a path, so the
        /// table above must not see them. `DataLinkReady` goes straight to the
        /// stack (`EvseManager.cpp:1232-1238`), the session failure is
        /// republished, and the selected mode is recorded nowhere.
        #[test]
        fn the_facts_no_path_sees_reach_no_path() {
            for fact in [
                HlcEvent::DataLinkReady(true),
                HlcEvent::SessionFailed(HlcSessionFailure::UnexpectedSessionEnd),
                HlcEvent::ModeSelected {
                    transfer: "AC_three_phase_core".into(),
                },
            ] {
                let (mut core, events, _) = core_recording_path();
                core.apply(Event::Hlc(fact.clone()), now());
                let events_now = events.lock().unwrap().clone();
                assert!(
                    events_now.is_empty(),
                    "{fact:?} reached a path: {events_now:?}"
                );
            }
        }

        /// `EvseManager.cpp:372-392`: each of the three callbacks informs the
        /// charger first (`:375`, `:383`, `:390`) and SLAC second (`:377`,
        /// `:384`, `:391`), and each sends its own relay. Both halves and their
        /// order, in one test, because a swap produces the same set.
        #[test]
        fn a_data_link_request_reaches_the_charger_before_slac_and_relays_itself() {
            for (fact, relay) in [
                (HlcEvent::DataLinkError, SlacUpdate::DlinkError),
                (HlcEvent::DataLinkPause, SlacUpdate::DlinkPause),
                (HlcEvent::DataLinkTerminate, SlacUpdate::DlinkTerminate),
            ] {
                let mut core = ac_hlc_core();

                let effects = core.apply(Event::Hlc(fact.clone()), now());

                let told_slac = effects
                    .iter()
                    .position(|effect| *effect == Effect::SlacUpdate(relay))
                    .unwrap_or_else(|| panic!("{fact:?} must relay {relay:?}, got {effects:?}"));
                // The pause and the terminate each drop the pilot
                // (`Charger.cpp:2058`, `:2066`), which is the charger half.
                if fact != HlcEvent::DataLinkError {
                    let told_charger = effects
                        .iter()
                        .position(|effect| {
                            *effect == Effect::SetCpState(crate::core::effect::CpState::X1)
                        })
                        .unwrap_or_else(|| panic!("{fact:?} got {effects:?}"));
                    assert!(told_charger < told_slac, "{fact:?} got {effects:?}");
                }
            }
        }

        /// `EvseManager.cpp:365-370` republishes the failure on this module's
        /// own interface. The identity is read from the live session, so a
        /// republish that lost it would name no session at all.
        #[test]
        fn a_session_failure_is_republished_with_the_session_it_belongs_to() {
            let mut core = ac_hlc_core();
            core.apply(Event::Startup, now());
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
            let uuid = core.session.id.clone();
            assert!(uuid.is_some(), "the plug in must have opened a session");

            let effects = core.apply(
                Event::Hlc(HlcEvent::SessionFailed(
                    HlcSessionFailure::FailedTlsHandshake,
                )),
                now(),
            );

            assert!(
                effects.contains(&Effect::PublishHlcSessionFailed {
                    uuid,
                    reason: HlcSessionFailure::FailedTlsHandshake,
                }),
                "got {effects:?}"
            );
        }

        /// The data link readiness is relayed unchanged, both ways.
        #[test]
        fn the_data_link_readiness_reaches_the_stack_as_itself() {
            for ready in [true, false] {
                let mut core = ac_hlc_core();
                let effects = core.apply(Event::Hlc(HlcEvent::DataLinkReady(ready)), now());
                assert!(
                    effects.contains(&Effect::HlcUpdate(HlcUpdate::DlinkReady(ready))),
                    "got {effects:?}"
                );
            }
        }

        /// The three `HlcPort` producers that answer the stack directly all
        /// read `config.enabled`, and a port with no stack behind it must not
        /// send to one. `core()` is that port: `test_hlc(ChargeMode::Ac,
        /// false)`.
        #[test]
        fn a_port_without_a_stack_answers_it_nothing() {
            for fact in [
                HlcEvent::DataLinkReady(true),
                HlcEvent::SessionFailed(HlcSessionFailure::UnexpectedSessionEnd),
                HlcEvent::DataLinkError,
                HlcEvent::DataLinkPause,
                HlcEvent::DataLinkTerminate,
            ] {
                let mut core = core();
                let effects = core.apply(Event::Hlc(fact.clone()), now());
                assert!(
                    !effects.iter().any(|effect| matches!(
                        effect,
                        Effect::HlcUpdate(_)
                            | Effect::SlacUpdate(_)
                            | Effect::PublishHlcSessionFailed { .. }
                    )),
                    "{fact:?} got {effects:?}"
                );
            }
        }
    }

    /// Everything the vehicle is told about payment and contracts.
    ///
    /// The C++ subscribes to its own session event signal and to the session
    /// start signal, and re-derives the payment options in each subscriber
    /// (`EvseManager.cpp:1269-1309`, `:1310-1344`). `Core::session_event` is
    /// the single site that publishes a session event, so hooking the
    /// re-derivation there reaches exactly the same three events: the two the
    /// C++ lambda does not early return on, plus the start.
    mod what_the_vehicle_is_told_about_payment {
        use super::*;

        fn setups(effects: &[Effect]) -> Vec<SessionSetup> {
            effects
                .iter()
                .filter_map(|effect| match effect {
                    Effect::HlcUpdate(HlcUpdate::SessionSetup(setup)) => Some(setup.clone()),
                    _ => None,
                })
                .collect()
        }

        fn plugged_in_core() -> (Core, Vec<Effect>) {
            let mut core = ac_hlc_core();
            core.apply(Event::Startup, now());
            let effects = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
            (core, effects)
        }

        #[test]
        fn a_session_start_re_derives_the_payment_options() {
            let (_, effects) = plugged_in_core();
            assert_eq!(
                setups(&effects),
                vec![SessionSetup {
                    payment_options: vec![PaymentOption::ExternalPayment, PaymentOption::Contract],
                    supported_certificate_service: true,
                    central_contract_validation_allowed: false,
                    fake_dc: false,
                }]
            );
        }

        /// The C++ sends it from a subscriber to the published event, so the
        /// consumer sees the announcement before the stack is retold anything.
        #[test]
        fn the_session_start_is_published_before_the_stack_is_retold() {
            let (_, effects) = plugged_in_core();
            let published = index_of(&effects, |effect| {
                matches!(
                    effect,
                    Effect::PublishSessionEvent(report)
                        if report.event == SessionEvent::SessionStarted
                )
            });
            let retold = index_of(&effects, |effect| {
                matches!(effect, Effect::HlcUpdate(HlcUpdate::SessionSetup(_)))
            });
            assert!(published < retold, "got {effects:?}");
        }

        #[test]
        fn an_authorization_re_derives_the_payment_options_without_the_contract() {
            let (mut core, _) = plugged_in_core();
            let effects = core.apply(authorize(true, AuthorizationKind::Eim), now());
            assert_eq!(
                setups(&effects),
                vec![SessionSetup {
                    payment_options: vec![PaymentOption::ExternalPayment],
                    supported_certificate_service: false,
                    central_contract_validation_allowed: false,
                    fake_dc: false,
                }]
            );
        }

        /// A finished session is the one trigger point that re-offers the
        /// contract, which is what lets the next vehicle use it.
        #[test]
        fn a_finished_session_re_offers_the_contract() {
            let (mut core, _) = plugged_in_core();
            core.apply(authorize(true, AuthorizationKind::Eim), now());

            let effects = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::A)), now());

            let offered: Vec<Vec<PaymentOption>> = setups(&effects)
                .into_iter()
                .map(|setup| setup.payment_options)
                .collect();
            assert!(
                offered.contains(&vec![
                    PaymentOption::ExternalPayment,
                    PaymentOption::Contract
                ]),
                "got {offered:?}"
            );
        }

        /// Every other session event leaves the stack alone, which is the early
        /// return at `EvseManager.cpp:1280-1282`.
        #[test]
        fn the_other_session_events_tell_the_stack_nothing() {
            let mut core = ac_hlc_core();
            core.apply(Event::Startup, now());

            let effects = core.apply(
                Event::Command(Command::Reserve { reservation_id: 7 }),
                now(),
            );

            assert!(setups(&effects).is_empty(), "got {effects:?}");
        }

        #[test]
        fn a_deployment_without_high_level_communication_is_never_retold() {
            let mut core = core();
            core.apply(Event::Startup, now());
            let mut effects = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
            effects.extend(core.apply(authorize(true, AuthorizationKind::Eim), now()));
            effects.extend(core.apply(Event::Bsp(BspEvent::Cp(CpEvent::A)), now()));
            assert!(setups(&effects).is_empty(), "got {effects:?}");
        }

        /// The command answers nothing on its own and reaches the vehicle at
        /// the next trigger point (`evse/evse_managerImpl.cpp:511-524` writes
        /// only the atomics).
        #[test]
        fn the_plug_and_charge_command_reaches_the_next_session_start() {
            let mut core = ac_hlc_core();
            core.apply(Event::Startup, now());

            let answered = core.apply(
                Event::Command(Command::SetPlugAndChargeConfiguration(
                    PlugAndChargeConfiguration {
                        enabled: Some(false),
                        ..PlugAndChargeConfiguration::default()
                    },
                )),
                now(),
            );
            assert!(answered.is_empty(), "got {answered:?}");

            let effects = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
            assert_eq!(
                setups(&effects),
                vec![SessionSetup {
                    payment_options: vec![PaymentOption::ExternalPayment],
                    supported_certificate_service: false,
                    central_contract_validation_allowed: false,
                    fake_dc: false,
                }]
            );
        }

        /// Each field of the command travels on its own, which matters because
        /// the two contract flags are adjacent booleans.
        #[test]
        fn each_command_field_reaches_its_own_flag() {
            let told = |request: PlugAndChargeConfiguration| {
                let mut core = ac_hlc_core();
                core.apply(Event::Startup, now());
                core.apply(
                    Event::Command(Command::SetPlugAndChargeConfiguration(request)),
                    now(),
                );
                let effects = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
                let setup = setups(&effects).first().expect("a session setup").clone();
                (
                    setup.supported_certificate_service,
                    setup.central_contract_validation_allowed,
                )
            };

            assert_eq!(
                told(PlugAndChargeConfiguration {
                    central_validation_allowed: Some(true),
                    ..PlugAndChargeConfiguration::default()
                }),
                (true, true)
            );
            assert_eq!(
                told(PlugAndChargeConfiguration {
                    certificate_installation_enabled: Some(false),
                    ..PlugAndChargeConfiguration::default()
                }),
                (false, false)
            );
        }
    }

    /// Everything the SLAC layer is told about matching.
    ///
    /// `EvseManager.cpp:1094-1119` on the pilot, `Charger.cpp:1020-1022` on a
    /// resume out of the EVSE's own pause, and `Charger.cpp:319-322` on the
    /// fatal error exit from `WaitingForAuthentication`.
    /// `Charger::set_max_current` stores a deadline with the budget and
    /// `power_available()` falls back to zero once it has passed. Neither
    /// existed here: `valid_for_s` was carried so it could be republished
    /// unchanged and nothing read it, so an energy manager that went quiet left
    /// its last grant standing for ever.
    mod the_budget_expires {
        use super::*;

        /// The identity is the one the module's own flow request carries, which
        /// is what makes the answer this node's rather than another's.
        fn granted(ampere: f64, valid_for_s: i64) -> Event {
            Event::EnforcedLimits(Box::new(energy::enforce::EnforcedLimits {
                uuid: "evse_manager".to_owned(),
                valid_for_s,
                schedule: Vec::new(),
                limits_root_side: energy::enforce::LimitsRes {
                    ac_max_current_a: Some(energy::flow_request::NumberWithSource::new(
                        ampere, "test",
                    )),
                    total_power_w: None,
                    ac_max_phase_count: None,
                },
            }))
        }

        fn published_limits(effects: &[Effect]) -> Vec<Limits> {
            effects
                .iter()
                .filter_map(|effect| match effect {
                    Effect::PublishLimits(limits) => Some(*limits),
                    _ => None,
                })
                .collect()
        }

        /// The board's phase count has to be known or the enforcement is
        /// discarded before it reaches the budget at all.
        fn core_with_energy() -> Core {
            let mut core = core_up();
            core.apply(
                Event::Bsp(BspEvent::Capabilities(HardwareCapabilities {
                    max_phase_count_import: 1,
                    min_phase_count_import: 1,
                    ..HardwareCapabilities::default()
                })),
                now(),
            );
            core
        }

        #[test]
        fn a_grant_arms_the_deadline_its_own_validity_names() {
            let mut core = core_with_energy();

            let effects = core.apply(granted(16.0, 45), now());

            assert!(
                effects.contains(&Effect::StartTimer {
                    id: TIMER_BUDGET_VALIDITY,
                    after: std::time::Duration::from_secs(45),
                }),
                "{effects:?}"
            );
            assert_eq!(core.session().limits.max_current_a, 16.0);
        }

        #[test]
        fn the_deadline_passing_falls_the_budget_back_to_zero() {
            let mut core = core_with_energy();
            core.apply(granted(16.0, 45), now());

            let effects = core.apply(
                Event::Timer {
                    id: TIMER_BUDGET_VALIDITY,
                    generation: 1,
                },
                now(),
            );

            assert_eq!(core.session().limits.max_current_a, 0.0);
            assert_eq!(
                published_limits(&effects)
                    .into_iter()
                    .map(|limits| limits.max_current_a)
                    .collect::<Vec<f64>>(),
                vec![0.0],
                "the fallback goes out as a limit like any other: {effects:?}"
            );
            assert!(
                effects.contains(&Effect::SetOvercurrentLimit(0.0)),
                "and the board is given the fallback: {effects:?}"
            );
        }

        /// `if (shared_context.max_current > 0.)`. An expiry that finds nothing
        /// to drop signals nothing, so a quiet energy manager does not publish
        /// a zero every time a deadline comes round.
        #[test]
        fn an_expiry_with_nothing_left_to_drop_says_nothing() {
            let mut core = core_with_energy();
            core.apply(granted(16.0, 45), now());
            let expiry = || Event::Timer {
                id: TIMER_BUDGET_VALIDITY,
                generation: 1,
            };
            core.apply(expiry(), now());

            let again = core.apply(expiry(), now());

            assert!(again.is_empty(), "{again:?}");
        }

        /// `Charger.cpp:1384-1385`. A grant that is already invalid is refused
        /// outright: the charger keeps the limit it had and emits no signal.
        /// The republish and the phase count still travel, because they are
        /// the handler's own rather than the charger's.
        #[test]
        fn a_grant_that_is_already_invalid_is_refused() {
            let mut core = core_with_energy();
            core.apply(granted(16.0, 45), now());

            let effects = core.apply(granted(32.0, 0), now());

            assert_eq!(
                core.session().limits.max_current_a, 16.0,
                "the accepted limit stands: {effects:?}"
            );
            assert!(
                !effects.iter().any(|effect| matches!(
                    effect,
                    Effect::StartTimer {
                        id: TIMER_BUDGET_VALIDITY,
                        ..
                    }
                )),
                "and no deadline is armed for it: {effects:?}"
            );
            assert_eq!(
                published_limits(&effects)
                    .into_iter()
                    .map(|limits| limits.max_current_a)
                    .collect::<Vec<f64>>(),
                vec![16.0],
                "the republish carries the standing limit: {effects:?}"
            );
            assert!(
                !effects
                    .iter()
                    .any(|effect| matches!(effect, Effect::SetOvercurrentLimit(_))),
                "and the board is not given a limit the charger refused: {effects:?}"
            );
        }
    }

    /// `disable_authentication`: the deployment charges for free, so it
    /// authorizes every vehicle itself.
    ///
    /// The setting was parsed and had no reader at all, so a free charging
    /// deployment waited for an authorization nobody was going to send.
    mod free_charging {
        use super::*;

        fn free_core() -> Core {
            let mut core = core();
            core.auth = Auth::new(true).free_charging();
            core.apply(Event::Startup, now());
            core
        }

        #[test]
        fn a_plug_in_authorizes_itself_on_a_free_charging_port() {
            let mut core = free_core();

            let effects = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());

            assert!(core.auth.authorized(), "{effects:?}");
            let announced = published_events(&effects);
            assert!(
                announced.contains(&SessionEvent::Authorized),
                "and the grant is announced: {announced:?}"
            );
        }

        /// The identity is the C++'s: the metering transaction is opened under
        /// it, so its value reaches a billing record and is not free to change.
        #[test]
        fn the_grant_carries_the_free_service_identity() {
            let mut core = free_core();

            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());

            let token = core.auth.token().cloned().expect("an identity");
            assert_eq!(token.value(), "FREESERVICE");
            assert_eq!(token.token_type(), IdTokenType::Local);
            assert!(!token.is_plug_and_charge());
            assert_eq!(token.payload()["authorization_type"], "RFID");
            assert_eq!(token.payload()["prevalidated"], true);
        }

        /// The vehicle is still told an authorization is required first, which
        /// is the C++ order: the grant runs out of the session started handler,
        /// after the event that handler is publishing.
        #[test]
        fn the_port_still_asks_for_an_authorization_before_granting_one() {
            let mut core = free_core();

            let effects = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());

            let announced = published_events(&effects);
            let required = announced
                .iter()
                .position(|event| *event == SessionEvent::AuthRequired);
            let granted = announced
                .iter()
                .position(|event| *event == SessionEvent::Authorized);
            assert!(
                required < granted,
                "the ask comes first: {announced:?}"
            );
        }

        /// A port that does not charge for free grants nothing, which is the
        /// same drive with the setting off.
        #[test]
        fn a_port_that_charges_for_its_energy_grants_nothing() {
            let mut core = core();
            core.apply(Event::Startup, now());

            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());

            assert!(!core.auth.authorized());
        }

        /// Only a plug in first session, which is the `EVConnected` reason the
        /// C++ tests. An authorization first session already has an identity
        /// and must not be given a second one.
        #[test]
        fn an_authorization_first_session_is_not_given_a_second_identity() {
            let mut core = free_core();
            core.apply(authorize(true, AuthorizationKind::Eim), now());
            let first = core.auth.token().cloned().expect("an identity");
            assert_eq!(first.value(), "tok", "the vehicle's own identity");

            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());

            assert_eq!(
                core.auth.token().map(IdTag::value),
                Some("tok"),
                "the vehicle's own identity stands"
            );
        }
    }

    mod what_slac_is_told_about_matching {
        use super::*;
        use crate::core::effect::SlacUpdate;

        fn slac_updates(effects: &[Effect]) -> Vec<SlacUpdate> {
            effects
                .iter()
                .filter_map(|effect| match effect {
                    Effect::SlacUpdate(update) => Some(*update),
                    _ => None,
                })
                .collect()
        }

        /// The event a data link request arrives as.
        fn data_link_event(request: DataLinkRequest) -> Event {
            Event::Hlc(match request {
                DataLinkRequest::Error => HlcEvent::DataLinkError,
                DataLinkRequest::Pause => HlcEvent::DataLinkPause,
                DataLinkRequest::Terminate => HlcEvent::DataLinkTerminate,
            })
        }

        /// A live AC high level communication charge that the EVSE has paused
        /// and whose relays are reported open, so it is resident in
        /// `Charger::EvseState::ChargingPausedEVSE`.
        fn paused_by_evse() -> Core {
            let mut core = ac_hlc_core();
            core.apply(Event::Startup, now());
            core.apply(plug_in(), now());
            core.apply(Event::Hlc(HlcEvent::SetupFinished), now());
            core.apply(authorize(true, AuthorizationKind::Eim), now());
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::C)), now());
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::PowerOn)), now());
            core.apply(Event::Command(Command::PauseCharging), now());
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::PowerOff)), now());
            assert_eq!(core.path.state(), AcState::ChargingPausedEvse);
            core
        }

        /// `EvseManager.cpp:1105-1110`. The arrival is what starts matching,
        /// and the C++ comment there is that starting it late costs a vehicle
        /// its retries.
        #[test]
        fn an_arrival_starts_matching() {
            let mut core = ac_hlc_core();
            let effects = core.apply(plug_in(), now());
            assert_eq!(slac_updates(&effects), vec![SlacUpdate::EnterBcd]);
        }

        /// The same reading a second time is the same vehicle, so nothing is
        /// said: `CpTracker` answers the arrival as an edge.
        #[test]
        fn a_repeated_reading_starts_nothing() {
            let mut core = ac_hlc_core();
            core.apply(plug_in(), now());
            let effects = core.apply(plug_in(), now());
            assert_eq!(slac_updates(&effects), Vec::new());
        }

        /// `IECStateMachine.cpp:215-218` pushes `EFtoBCD` for state B out of E
        /// or F, and `EvseManager.cpp:1095-1097` starts matching again on it.
        #[test]
        fn state_b_out_of_e_or_f_restarts_matching() {
            for interruption in [CpEvent::E, CpEvent::F] {
                let mut core = ac_hlc_core();
                core.apply(plug_in(), now());
                core.apply(Event::Bsp(BspEvent::Cp(interruption)), now());
                let effects = core.apply(plug_in(), now());
                assert_eq!(
                    slac_updates(&effects),
                    vec![SlacUpdate::EnterBcd],
                    "state B out of {interruption:?}"
                );
            }
        }

        /// `IECStateMachine.cpp:305-308` and `:322-325`. Both fault levels end
        /// the logical network.
        #[test]
        fn leaving_bcd_for_e_or_f_leaves_the_network() {
            for fault in [CpEvent::E, CpEvent::F] {
                let mut core = ac_hlc_core();
                core.apply(plug_in(), now());
                let effects = core.apply(Event::Bsp(BspEvent::Cp(fault)), now());
                assert_eq!(
                    slac_updates(&effects),
                    vec![SlacUpdate::LeaveBcd],
                    "state {fault:?} out of B"
                );
            }
        }

        /// `EvseManager.cpp:1111-1116`. The reset rides on the flag the SLAC
        /// state report writes, and only a link that was still matched gets
        /// one.
        #[test]
        fn an_unplug_resets_slac_only_where_matching_had_begun() {
            for (started, expected) in [
                (
                    true,
                    vec![SlacUpdate::LeaveBcd, SlacUpdate::Reset],
                ),
                (false, vec![SlacUpdate::LeaveBcd]),
            ] {
                let mut core = ac_hlc_core();
                core.apply(plug_in(), now());
                core.apply(Event::Hlc(HlcEvent::MatchingStarted(started)), now());
                let effects = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::A)), now());
                assert_eq!(
                    slac_updates(&effects),
                    expected,
                    "matching started: {started}"
                );
            }
        }

        /// `IECStateMachine.cpp:183-185` pushes no departure for a port that
        /// had nothing plugged in, so neither does this.
        #[test]
        fn state_a_without_a_vehicle_says_nothing() {
            let mut core = ac_hlc_core();
            let effects = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::A)), now());
            assert_eq!(slac_updates(&effects), Vec::new());
        }

        /// `Charger.cpp:1020-1022`. The wake up is owed on every verdict but a
        /// pause, because a paused link is the one that is still matched.
        #[test]
        fn a_resume_wakes_slac_unless_the_vehicle_only_paused_the_link() {
            for (request, expected) in [
                (None, vec![SlacUpdate::EnterBcd]),
                (Some(DataLinkRequest::Terminate), vec![SlacUpdate::EnterBcd]),
                (Some(DataLinkRequest::Pause), Vec::new()),
            ] {
                let mut core = paused_by_evse();
                if let Some(request) = request {
                    core.apply(data_link_event(request), now());
                }
                let effects = core.apply(Event::Command(Command::ResumeCharging), now());
                assert_eq!(
                    slac_updates(&effects),
                    expected,
                    "last data link request: {request:?}"
                );
            }
        }

        /// The same wake up on a DC session, which could not happen at all
        /// until the DC path's resume moved the state: `Core` owes it on
        /// whichever path actually leaves `ChargingPausedEvse`, so a path that
        /// stayed put got no wake up and no restart. Both halves are asserted
        /// here, because the wake up alone would pass on a session that never
        /// resumed.
        #[test]
        fn a_dc_resume_leaves_the_paused_state_and_wakes_slac() {
            let mut core = dc_core();
            core.apply(Event::Startup, now());
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
            core.apply(authorize(true, AuthorizationKind::Eim), now());
            core.apply(Event::Hlc(HlcEvent::CurrentDemandStarted), now());
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::PowerOn)), now());
            assert_eq!(core.path.state(), AcState::Charging, "the control");

            core.apply(Event::Command(Command::PauseCharging), now());
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::PowerOff)), now());
            assert_eq!(core.path.state(), AcState::ChargingPausedEvse);

            let effects = core.apply(Event::Command(Command::ResumeCharging), now());

            assert_eq!(
                core.path.state(),
                AcState::PrepareCharging,
                "the session restarts: {effects:?}"
            );
            assert_eq!(slac_updates(&effects), vec![SlacUpdate::EnterBcd]);
        }

        /// `Charger.cpp:803-804`: the `Charging` entry forgets the verdict, so
        /// a second pause in one session is judged on its own.
        #[test]
        fn a_charge_beginning_forgets_the_previous_verdict() {
            let mut core = paused_by_evse();
            core.apply(data_link_event(DataLinkRequest::Pause), now());
            core.apply(Event::Command(Command::ResumeCharging), now());
            // Back to a live charge, then paused again with no new verdict.
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::C)), now());
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::PowerOn)), now());
            core.apply(Event::Command(Command::PauseCharging), now());
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::PowerOff)), now());
            let effects = core.apply(Event::Command(Command::ResumeCharging), now());
            assert_eq!(
                slac_updates(&effects),
                vec![SlacUpdate::EnterBcd],
                "the pause verdict outlived the charge it belonged to"
            );
        }

        /// `Charger.cpp:319-322`. The fatal error exit from
        /// `WaitingForAuthentication` resets SLAC on its way to `Finished`, and
        /// only that arm does: every other state leaves the link for the unplug
        /// to reset, which is why the state is read before the safe state moves
        /// it.
        #[test]
        fn only_a_fault_while_waiting_for_authentication_resets_slac() {
            let fault = || {
                Event::Error(ErrorEvent {
                    source: ErrorSource::Bsp,
                    error_type: "evse_board_support/MREC8EmergencyStop".into(),
                    sub_type: String::new(),
                    vendor_id: String::new(),
                    severity: Severity::High,
                    raised: true,
                })
            };

            let mut waiting = ac_hlc_core();
            waiting.apply(Event::Startup, now());
            waiting.apply(plug_in(), now());
            assert_eq!(waiting.path.state(), AcState::WaitingForAuthentication);
            assert_eq!(
                slac_updates(&waiting.apply(fault(), now())),
                vec![SlacUpdate::Reset]
            );

            let mut charging = paused_by_evse();
            assert_ne!(charging.path.state(), AcState::WaitingForAuthentication);
            assert_eq!(slac_updates(&charging.apply(fault(), now())), Vec::new());
        }

        /// A resume the reducer refuses is not a resume, so it owes nothing.
        #[test]
        fn a_resume_from_no_pause_wakes_nothing() {
            let mut core = ac_hlc_core();
            core.apply(plug_in(), now());
            let effects = core.apply(Event::Command(Command::ResumeCharging), now());
            assert_eq!(slac_updates(&effects), Vec::new());
        }
    }

    /// Everything the vehicle is told about the relays.
    ///
    /// `EvseManager.cpp:1121-1142` forwards three commands on a plug in and one
    /// on each relay movement, all inside one `if (hlc_enabled)` block with no
    /// charge mode branch.
    mod what_the_stack_is_told_about_the_relays {
        use super::*;

        /// The relay block alone.
        ///
        /// A plug in also opens a session, and the session start retells the
        /// stack what the vehicle may pay with (`EvseManager.cpp:1343`). That
        /// one is not part of this block and travels on its own trigger, so it
        /// is filtered out here rather than being pinned into an order it does
        /// not have: the C++ reaches it from the charger thread, off the queue
        /// the pilot event was pushed onto, while the relay block runs inline
        /// in the subscriber, so the two have no fixed order there either.
        fn hlc_updates(effects: &[Effect]) -> Vec<HlcUpdate> {
            effects
                .iter()
                .filter_map(|effect| match effect {
                    Effect::HlcUpdate(HlcUpdate::SessionSetup(_)) => None,
                    Effect::HlcUpdate(update) => Some(update.clone()),
                    _ => None,
                })
                .collect()
        }

        /// The same filter, over positions rather than values.
        fn is_relay_block(effect: &Effect) -> bool {
            matches!(effect, Effect::HlcUpdate(update)
                if !matches!(update, HlcUpdate::SessionSetup(_)))
        }

        #[test]
        fn a_plug_in_clears_what_the_previous_session_left_standing() {
            let mut core = ac_hlc_core();
            core.apply(Event::Startup, now());

            let effects = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());

            assert_eq!(
                hlc_updates(&effects),
                vec![
                    HlcUpdate::ResetError,
                    HlcUpdate::ContactorClosed(false),
                    HlcUpdate::StopCharging(false),
                ],
                "`EvseManager.cpp:1124-1127`, in that order"
            );
        }

        #[test]
        fn the_relays_closing_and_opening_are_both_reported() {
            let mut core = ac_hlc_core();
            core.apply(Event::Startup, now());
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());

            let closed = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::PowerOn)), now());
            let opened = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::PowerOff)), now());

            assert_eq!(
                hlc_updates(&closed),
                vec![HlcUpdate::ContactorClosed(true)],
                "`EvseManager.cpp:1133-1136`"
            );
            assert_eq!(
                hlc_updates(&opened),
                vec![HlcUpdate::ContactorClosed(false)],
                "`EvseManager.cpp:1138-1143`"
            );
        }

        #[test]
        fn the_pilot_event_reaches_the_power_path_before_the_stack_hears_it() {
            // `EvseManager.cpp:1118` pushes the event onto the charger's queue
            // and only then forwards to the stack at `:1121-1142`, so a relay
            // report can never precede the path's own reaction to the same
            // edge. Asserted on the plug in, which is the one edge where the
            // path answers with effects of its own: on a bare relay movement
            // it answers with none, so that edge cannot tell the two orders
            // apart.
            let mut core = ac_hlc_core();
            core.apply(Event::Startup, now());

            let effects = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
            // The energy ask this arrival also triggers is not part of the
            // order under test. The C++ sends it from a detached thread
            // (`energyImpl.cpp:134`), so it is ordered against neither the
            // charger nor the stack, and it is emitted at the end of the pass
            // here. Nor is the transcript: it records what happened and
            // actuates nothing, and its state transition line is written at
            // the end of the pass for the same reason.
            let effects: Vec<Effect> = effects
                .into_iter()
                .filter(|effect| {
                    !matches!(
                        effect,
                        Effect::PublishEnergyFlowRequest(_)
                            | Effect::SessionLog(_)
                            | Effect::StartTimer {
                                id: crate::core::energy::TIMER_ENERGY_FLOW_REQUEST,
                                ..
                            }
                    )
                })
                .collect();

            let first_told = effects
                .iter()
                .position(is_relay_block)
                .expect("the stack must hear the arrival");
            let last_from_the_path = effects
                .iter()
                .rposition(|effect| !is_relay_block(effect))
                .expect("the path must answer the arrival");
            assert!(
                last_from_the_path < first_told,
                "the whole forwarding block follows the path, got {effects:?}"
            );
        }

        #[test]
        fn a_port_without_high_level_communication_tells_nobody() {
            let mut core = core();
            core.apply(Event::Startup, now());

            let plugged = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
            let closed = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::PowerOn)), now());

            assert!(hlc_updates(&plugged).is_empty(), "got {plugged:?}");
            assert!(hlc_updates(&closed).is_empty(), "got {closed:?}");
        }
    }

    fn board_capabilities(min_import: i64, max_import: i64) -> Event {
        Event::Bsp(BspEvent::Capabilities(HardwareCapabilities {
            min_phase_count_import: min_import,
            max_phase_count_import: max_import,
            ..HardwareCapabilities::default()
        }))
    }

    #[test]
    fn the_boot_sequence_reaches_the_stack_at_startup_ahead_of_ready() {
        // `EvseManager.cpp:362` and `:971-995` both run inside the same
        // `if (hlc_enabled)` block, so everything the stack is told precedes
        // the ready announcement at `:1491`.
        let mut core = dc_core();

        let effects = core.apply(Event::Startup, now());

        let kinds: Vec<&Effect> = effects
            .iter()
            .filter(|effect| {
                matches!(
                    effect,
                    Effect::HlcUpdate(_) | Effect::PublishSupportedTransferModes(_)
                )
            })
            .collect();
        assert_eq!(
            kinds,
            vec![
                &Effect::HlcUpdate(HlcUpdate::SessionSetup(SessionSetup {
                    payment_options: vec![PaymentOption::ExternalPayment, PaymentOption::Contract],
                    supported_certificate_service: true,
                    central_contract_validation_allowed: false,
                    fake_dc: false,
                })),
                // `:561-564`, the DC limit block's direct push, which carries
                // the seed report because nothing has been sent yet;
                // `hlc::dc_limits::boot` owns that fact.
                &Effect::HlcUpdate(HlcUpdate::PowerSupplyCapabilities(Box::new(
                    crate::core::event::PowerSupplyCapabilities::sane_default()
                ))),
                &Effect::HlcUpdate(HlcUpdate::ChargingParameters(PhysicalValues {
                    ac_nominal_voltage_v: None,
                    dc_current_regulation_tolerance_a: Some(0.5),
                    dc_peak_current_ripple_a: Some(0.5),
                    dc_energy_to_be_delivered_wh: Some(10_000.0),
                })),
                &Effect::HlcUpdate(HlcUpdate::DcMinimumLimits(MinimumLimits::default())),
                &Effect::HlcUpdate(HlcUpdate::DcPresentValues {
                    voltage_v: 0.0,
                    current_a: 0.0,
                }),
                &Effect::HlcUpdate(HlcUpdate::ReceiptRequired(false)),
                &Effect::HlcUpdate(HlcUpdate::Setup {
                    evse_id: "DE*PNX*E1234567*1".to_string(),
                    evse_id_din: "49A80737A45678".to_string(),
                    sae_mode: crate::core::hlc::SaeBidiMode::None,
                    debug_mode: false,
                }),
                &Effect::PublishSupportedTransferModes(vec![EnergyTransferMode::DcExtended]),
                &Effect::HlcUpdate(HlcUpdate::TransferModes(vec![
                    EnergyTransferMode::DcExtended
                ])),
                &Effect::HlcUpdate(HlcUpdate::ResetError),
                // The ready sequence publish, which announces the same set on
                // this module's own interface and tells the stack nothing.
                &Effect::PublishSupportedTransferModes(vec![EnergyTransferMode::DcExtended]),
            ]
        );

        let reset = index_of(&effects, |effect| {
            effect == &Effect::HlcUpdate(HlcUpdate::ResetError)
        });
        let ready = index_of(&effects, |effect| {
            matches!(effect, Effect::PublishReady(true))
        });
        assert!(reset < ready);
    }

    #[test]
    fn the_advertised_set_is_published_ahead_of_the_enable_announcement() {
        // `EvseManager.cpp:1505` publishes it, `:1512` announces the enable
        // state, and other modules read the enable state on startup, so the
        // order is part of what a consumer sees.
        let mut core = dc_core();

        let effects = core.apply(Event::Startup, now());

        let advertised = effects
            .iter()
            .rposition(|effect| matches!(effect, Effect::PublishSupportedTransferModes(_)))
            .expect("advertised set publish");
        let announced = index_of(&effects, |effect| {
            matches!(effect, Effect::PublishEnableEvent { .. })
        });
        assert!(advertised < announced, "got {effects:?}");
    }

    /// The over voltage push cannot happen on an AC port, and not because a
    /// setter declines it: the derivation and its two inputs live on `Dc`, and
    /// `Ac` drops `PathEvent::DcEvMaximumLimits` outright
    /// (`src/core/path/ac.rs:1385`). Driven rather than argued, because the
    /// sibling shape this program has already recorded once, a setter that
    /// accepts input on a port where it can never do anything, is invisible
    /// from the setter's own side.
    #[test]
    fn an_ac_port_never_pushes_over_voltage_thresholds() {
        let mut core = core();
        core.apply(Event::Startup, now());

        let effects = core.apply(
            Event::Hlc(HlcEvent::DcEvMaximumLimits(EvMaximumLimits {
                maximum_current_a: Some(275.0),
                maximum_voltage_v: Some(920.0),
            })),
            now(),
        );

        assert!(
            !effects
                .iter()
                .any(|effect| matches!(effect, Effect::OverVoltageLimits { .. })),
            "an AC port has no over voltage monitor to configure: {effects:?}"
        );
    }

    /// The reachability question, driven end to end: an `HlcEvent` off the
    /// stack has to come out of `Core::apply` as a configured monitor, through
    /// the real DC path and not a recorder. Without this the derivation could
    /// be correct and still never be told to anyone.
    ///
    /// `EvseManager.cpp:851-870`. The monitor is a protection and an
    /// unconfigured one is silently inert: `OverVoltageMonitor::update_voltage`
    /// returns at `limits_valid_` (`OverVoltageMonitor.cpp:53`) and both limits
    /// default to infinity, so a monitor that is started but never told what to
    /// watch for reports nothing, ever, with no error and no log.
    #[test]
    fn the_vehicle_maximum_reaches_the_over_voltage_monitor_as_two_thresholds() {
        let mut core = dc_core();
        core.apply(Event::Startup, now());
        // The supply half of the negotiation. Without it the path still holds
        // its pre report default and the step would be the bottom one.
        core.apply(
            Event::PowerSupplyCapabilities(Box::new(PowerSupplyCapabilities {
                max_export_voltage_v: 950.0,
                ..PowerSupplyCapabilities::sane_default()
            })),
            now(),
        );

        let effects = core.apply(
            Event::Hlc(HlcEvent::DcEvMaximumLimits(EvMaximumLimits {
                maximum_current_a: Some(275.0),
                maximum_voltage_v: Some(920.0),
            })),
            now(),
        );

        let limits = effects
            .iter()
            .find_map(|effect| match effect {
                Effect::OverVoltageLimits(thresholds) => {
                    Some((thresholds.emergency_v(), thresholds.error_v()))
                }
                _ => None,
            })
            .expect("the monitor is configured");
        assert_eq!(
            limits,
            (1100.0, 920.0),
            "emergency is the IEC step above the negotiated maximum and error \
             is the vehicle's own rating: {effects:?}"
        );
    }

    #[test]
    fn a_supply_capability_report_republishes_the_advertised_dc_set() {
        let mut core = dc_core();
        core.apply(Event::Startup, now());
        // The report also carries the DC limit emissions, which
        // `hlc::dc_limits` owns; this is about the advertised set.

        let effects = core.apply(
            Event::PowerSupplyCapabilities(Box::new(PowerSupplyCapabilities {
                bidirectional: true,
                ..PowerSupplyCapabilities::sane_default()
            })),
            now(),
        );

        assert_eq!(
            effects,
            vec![
                // This module's own interface first. Every effect below shares
                // the one ordered lane, and OCPP latches this variable when it
                // builds the device model; `HlcPort::note_dc_capabilities` has
                // the whole reason.
                Effect::PublishSupportedTransferModes(vec![
                    EnergyTransferMode::DcExtended,
                    EnergyTransferMode::DcBpt
                ]),
                Effect::HlcUpdate(HlcUpdate::TransferModes(vec![
                    EnergyTransferMode::DcExtended,
                    EnergyTransferMode::DcBpt
                ])),
                // The report changed, so it reaches the stack this time.
                Effect::HlcUpdate(HlcUpdate::PowerSupplyCapabilities(Box::new(
                    PowerSupplyCapabilities {
                        bidirectional: true,
                        ..PowerSupplyCapabilities::sane_default()
                    }
                ))),
                Effect::HlcUpdate(HlcUpdate::ChargingParameters(PhysicalValues {
                    ac_nominal_voltage_v: None,
                    dc_current_regulation_tolerance_a: Some(0.5),
                    dc_peak_current_ripple_a: Some(0.5),
                    dc_energy_to_be_delivered_wh: Some(10_000.0),
                })),
                Effect::HlcUpdate(HlcUpdate::DcMinimumLimits(MinimumLimits::default())),
            ]
        );
    }

    /// `EvseManager.cpp:695-726`. The vehicle is told the supply's output, and
    /// the same reading also reaches the path, which reads only the voltage.
    #[test]
    fn a_supply_reading_reaches_the_vehicle_and_then_the_path() {
        let (mut core, events, _calls) = dc_core_recording_path();

        let effects = core.apply(
            Event::SupplyVoltageCurrent {
                voltage_v: 412.5,
                current_a: 63.25,
            },
            now(),
        );

        assert_eq!(
            effects,
            vec![Effect::HlcUpdate(HlcUpdate::DcPresentValues {
                voltage_v: 412.5,
                current_a: 63.25,
            })]
        );
        let events_now = events.lock().unwrap().clone();
        assert!(
            events_now
                .contains(&PathEvent::SupplyVoltage { voltage_v: 412.5 }),
            "{:?}",
            events_now
        );
    }

    /// The capability report installs the cable check voltage input, but the
    /// EVSE limit set now waits for the energy manager's allowance.
    #[test]
    fn a_supply_capability_report_defers_the_limit_set_to_energy_management() {
        let (mut core, events, _calls) = dc_core_recording_path();

        core.apply(
            Event::PowerSupplyCapabilities(Box::new(PowerSupplyCapabilities {
                bidirectional: true,
                max_export_voltage_v: 950.0,
                min_export_voltage_v: 150.0,
                max_export_current_a: 400.0,
                min_export_current_a: 2.0,
                max_export_power_w: 300_000.0,
                max_import_current_a: Some(300.0),
                max_import_power_w: Some(250_000.0),
                min_import_current_a: Some(4.0),
                min_import_voltage_v: Some(200.0),
                ..PowerSupplyCapabilities::sane_default()
            })),
            now(),
        );

        let seen = events.lock().unwrap().clone();
        assert!(
            !seen
                .iter()
                .any(|event| matches!(event, PathEvent::DcEnforcedLimits { .. })),
            "{seen:?}"
        );
        assert!(
            seen.contains(&PathEvent::DcExportVoltageRange { min_v: 150.0, max_v: 950.0 }),
            "{seen:?}"
        );
    }

    #[test]
    fn a_board_capability_report_republishes_the_advertised_ac_set() {
        let mut core = ac_hlc_core();
        core.apply(Event::Startup, now());

        let effects = core.apply(board_capabilities(1, 3), now());

        assert!(
            effects.contains(&Effect::PublishSupportedTransferModes(vec![
                EnergyTransferMode::AcSinglePhase,
                EnergyTransferMode::AcTwoPhase,
                EnergyTransferMode::AcThreePhase,
            ])),
            "got {effects:?}"
        );
        assert!(
            effects.contains(&Effect::HlcUpdate(HlcUpdate::TransferModes(vec![
                EnergyTransferMode::AcSinglePhase,
                EnergyTransferMode::AcTwoPhase,
                EnergyTransferMode::AcThreePhase,
            ])))
        );
    }

    #[test]
    fn a_capability_report_arriving_mid_session_still_republishes_the_set() {
        // Derating is the reason the C++ subscribes rather than reading once,
        // and nothing about the recomputation waits for the port to be idle.
        let mut core = ac_hlc_core();
        core.apply(Event::Startup, now());
        core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
        assert!(core.session().session_active);

        let effects = core.apply(board_capabilities(3, 3), now());

        assert!(
            effects.contains(&Effect::PublishSupportedTransferModes(vec![
                EnergyTransferMode::AcThreePhase
            ])),
            "got {effects:?}"
        );
    }

    #[test]
    fn a_board_capability_report_reaches_no_stack_on_a_port_without_one() {
        let mut core = core();
        core.apply(Event::Startup, now());

        let effects = core.apply(board_capabilities(1, 3), now());

        assert!(effects.is_empty(), "got {effects:?}");
    }

    /// The bidirectional fact end to end: three sources reaching one field,
    /// and the ADR-0018 withdrawal that vetoes them.
    mod bidirectional_resolution {
        use super::*;

        fn a_bidirectional_supply() -> Event {
            Event::PowerSupplyCapabilities(Box::new(PowerSupplyCapabilities {
                bidirectional: true,
                ..PowerSupplyCapabilities::sane_default()
            }))
        }

        fn a_unidirectional_supply() -> Event {
            Event::PowerSupplyCapabilities(Box::new(PowerSupplyCapabilities {
                bidirectional: false,
                ..PowerSupplyCapabilities::sane_default()
            }))
        }

        #[test]
        fn a_selected_bidirectional_service_resolves_the_session_fact() {
            let mut core = dc_core();
            core.apply(Event::Startup, now());
            assert!(!core.session().profile.bidirectional, "the control");

            core.apply(
                Event::Hlc(HlcEvent::SelectedService(SelectedService::DcBpt)),
                now(),
            );

            assert!(core.session().profile.bidirectional);
        }

        #[test]
        fn a_selected_unidirectional_service_leaves_the_fact_alone() {
            let mut core = dc_core();
            core.apply(Event::Startup, now());

            core.apply(
                Event::Hlc(HlcEvent::SelectedService(SelectedService::Dc)),
                now(),
            );

            assert!(!core.session().profile.bidirectional);
        }

        /// `EvseManager.cpp:931-942`. The SAE flag is a source on its own, so
        /// a session that selected nothing is still bidirectional under it.
        #[test]
        fn the_sae_bidirectional_flag_resolves_the_fact_on_its_own() {
            let mut core = dc_core();
            core.apply(Event::Startup, now());

            core.apply(Event::Hlc(HlcEvent::SaeBidiModeActive), now());

            assert!(core.session().profile.bidirectional);
        }

        /// `EvseManager.cpp:594`, the one writer of `false` on that flag.
        #[test]
        fn a_finished_current_demand_clears_the_sae_flag() {
            let mut core = dc_core();
            core.apply(Event::Startup, now());
            core.apply(Event::Hlc(HlcEvent::SaeBidiModeActive), now());

            core.apply(Event::Hlc(HlcEvent::CurrentDemandFinished), now());

            assert!(!core.session().profile.bidirectional);
        }

        /// `EvseManager.cpp:388`. A terminate forgets the selection, so the
        /// session fact falls with it; a pause keeps both.
        #[test]
        fn a_terminated_data_link_forgets_the_selection_and_a_pause_keeps_it() {
            let mut core = dc_core();
            core.apply(Event::Startup, now());
            core.apply(
                Event::Hlc(HlcEvent::SelectedService(SelectedService::DcBpt)),
                now(),
            );

            core.apply(Event::Hlc(HlcEvent::DataLinkPause), now());
            assert!(core.session().profile.bidirectional, "a pause can resume");

            core.apply(Event::Hlc(HlcEvent::DataLinkTerminate), now());
            assert!(!core.session().profile.bidirectional);
        }

        /// ADR-0018, divergence 4. The supply withdraws its capability while a
        /// discharge is live: the fact falls and the path is told to ramp the
        /// discharge down.
        #[test]
        fn a_withdrawn_capability_clears_the_fact_and_tells_the_path() {
            let (mut core, events, _calls) = dc_core_recording_path();
            core.apply(Event::Startup, now());
            core.apply(a_bidirectional_supply(), now());
            core.apply(
                Event::Hlc(HlcEvent::SelectedService(SelectedService::DcBpt)),
                now(),
            );
            assert!(core.session().profile.bidirectional, "the control");
            events.lock().unwrap().clear();

            core.apply(a_unidirectional_supply(), now());

            assert!(!core.session().profile.bidirectional);
            let events_now = events.lock().unwrap().clone();
            assert!(
                events_now
                    .contains(&PathEvent::BidirectionalWithdrawn),
                "{:?}",
                events_now
            );
        }

        /// The refusal half, as ADR-0018 now stands. While the supply reports
        /// no capability nothing lifts the veto, not a fresh source and not a
        /// fresh selection; the capability returning is the one thing that
        /// does, and it does so within the same session.
        #[test]
        fn only_the_capability_returning_restores_the_fact_within_the_session() {
            let mut core = dc_core();
            core.apply(Event::Startup, now());
            core.apply(a_bidirectional_supply(), now());
            core.apply(
                Event::Hlc(HlcEvent::SelectedService(SelectedService::DcBpt)),
                now(),
            );
            core.apply(a_unidirectional_supply(), now());
            assert!(!core.session().profile.bidirectional, "the control");

            core.apply(Event::Hlc(HlcEvent::SaeBidiModeActive), now());
            assert!(!core.session().profile.bidirectional, "a fresh source");

            core.apply(
                Event::Hlc(HlcEvent::SelectedService(SelectedService::DcBpt)),
                now(),
            );
            assert!(!core.session().profile.bidirectional, "a fresh selection");

            core.apply(a_bidirectional_supply(), now());
            assert!(
                core.session().profile.bidirectional,
                "the supply recovered, so the session may discharge again"
            );
        }

        /// The withdrawal that follows a recovery is its own edge, so the ramp
        /// down is asked for again rather than once per session.
        #[test]
        fn a_second_withdrawal_after_a_recovery_ramps_the_discharge_down_again() {
            let (mut core, events, _calls) = dc_core_recording_path();
            core.apply(Event::Startup, now());
            core.apply(a_bidirectional_supply(), now());
            core.apply(
                Event::Hlc(HlcEvent::SelectedService(SelectedService::DcBpt)),
                now(),
            );
            core.apply(a_unidirectional_supply(), now());
            core.apply(a_bidirectional_supply(), now());
            assert!(core.session().profile.bidirectional, "the control");
            events.lock().unwrap().clear();

            core.apply(a_unidirectional_supply(), now());

            assert!(!core.session().profile.bidirectional);
            let events_now = events.lock().unwrap().clone();
            assert!(
                events_now.contains(&PathEvent::BidirectionalWithdrawn),
                "{events_now:?}"
            );
        }

        /// ADR-0018 releases the refusal when the session ends as well as on a
        /// capability that returns, so a supply that withdrew and then went
        /// quiet does not hold the next session to it.
        ///
        /// The next session is opened with **no new capability report**, which
        /// is what makes this a test of the session end release rather than of
        /// the recovery one: a report carrying the capability would lift the
        /// refusal by itself and the test would pass with the session end
        /// release deleted.
        ///
        /// A mutation sweep found the release surviving with no test on it,
        /// which is what this closes: without it a single derating would take
        /// a port out of bidirectional service until the process restarted.
        #[test]
        fn the_refusal_does_not_outlive_the_session_it_happened_in() {
            let mut core = dc_core();
            core.apply(Event::Startup, now());
            core.apply(a_bidirectional_supply(), now());
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
            assert!(core.session().session_active, "a vehicle opens a session");
            core.apply(
                Event::Hlc(HlcEvent::SelectedService(SelectedService::DcBpt)),
                now(),
            );
            core.apply(a_unidirectional_supply(), now());
            assert!(!core.session().profile.bidirectional, "the control");

            // The vehicle leaves, which is the route every resting state takes
            // into `end_session`.
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::A)), now());
            assert!(
                !core.session().session_active,
                "the unplug must have ended the session for this to test anything"
            );

            // A fresh session on the same supply, which has reported nothing
            // since it withdrew.
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
            core.apply(
                Event::Hlc(HlcEvent::SelectedService(SelectedService::DcBpt)),
                now(),
            );

            assert!(
                core.session().profile.bidirectional,
                "the next session is not held to the last one's withdrawal"
            );
        }

        /// A supply that was never bidirectional withdraws nothing, so a
        /// unidirectional report on an ordinary session must not latch the
        /// veto and must not disturb the path.
        #[test]
        fn a_unidirectional_supply_on_an_ordinary_session_withdraws_nothing() {
            let (mut core, events, _calls) = dc_core_recording_path();
            core.apply(Event::Startup, now());
            events.lock().unwrap().clear();

            core.apply(a_unidirectional_supply(), now());

            let events_now = events.lock().unwrap().clone();
            assert!(
                !events_now
                    .contains(&PathEvent::BidirectionalWithdrawn),
                "{:?}",
                events_now
            );

            // And the veto is not latched, so a later source still resolves.
            core.apply(Event::Hlc(HlcEvent::SaeBidiModeActive), now());
            assert!(core.session().profile.bidirectional);
        }

        /// A repeated unidirectional report announces the withdrawal once.
        #[test]
        fn a_repeated_unidirectional_report_announces_the_withdrawal_once() {
            let (mut core, events, _calls) = dc_core_recording_path();
            core.apply(Event::Startup, now());
            core.apply(a_bidirectional_supply(), now());
            core.apply(Event::Hlc(HlcEvent::SaeBidiModeActive), now());
            core.apply(a_unidirectional_supply(), now());
            events.lock().unwrap().clear();

            core.apply(a_unidirectional_supply(), now());

            let events_now = events.lock().unwrap().clone();
            assert!(
                !events_now
                    .contains(&PathEvent::BidirectionalWithdrawn),
                "{:?}",
                events_now
            );
        }
    }

    /// `set_der_available` on an AC port widens the advertised set with
    /// `AC_DER_IEC` and republishes it (`evse/evse_managerImpl.cpp:571-574`).
    #[test]
    fn a_der_declaration_widens_what_an_ac_port_advertises() {
        let mut core = ac_hlc_core();
        core.apply(Event::Startup, now());
        // The export capability is the other half of the gate
        // (`energy_transfer_modes.cpp:12-37`), so the board has to report one
        // before the declaration can widen anything.
        core.apply(
            Event::Bsp(BspEvent::Capabilities(HardwareCapabilities {
                min_phase_count_import: 1,
                max_phase_count_import: 3,
                max_current_a_export: 16.0,
                max_phase_count_export: 1,
                ..HardwareCapabilities::default()
            })),
            now(),
        );
        assert!(
            !core
                .advertised_transfer_modes()
                .contains(&EnergyTransferMode::AcDerIec),
            "the control"
        );

        let effects = core.apply(Event::Command(Command::SetDerAvailable(true)), now());

        assert!(core
            .advertised_transfer_modes()
            .contains(&EnergyTransferMode::AcDerIec));
        assert_eq!(
            effects,
            vec![
                Effect::PublishSupportedTransferModes(core.advertised_transfer_modes()),
                Effect::HlcUpdate(HlcUpdate::TransferModes(core.advertised_transfer_modes())),
            ]
        );
    }

    #[test]
    fn an_accepted_transfer_mode_narrowing_reaches_the_stack() {
        let mut core = dc_core();
        core.apply(Event::Startup, now());

        let effects = core.apply(
            Event::Command(Command::UpdateAllowedTransferModes(vec![
                EnergyTransferMode::Mcs,
            ])),
            now(),
        );

        assert_eq!(
            effects,
            vec![Effect::HlcUpdate(HlcUpdate::TransferModes(vec![
                EnergyTransferMode::Mcs
            ]))]
        );
    }

    #[test]
    fn a_narrowing_does_not_change_what_the_module_advertises() {
        // The C++ command reaches the stack and never touches the monitor
        // (`evse/evse_managerImpl.cpp:562-564`).
        let mut core = dc_core();
        core.apply(Event::Startup, now());

        core.apply(
            Event::Command(Command::UpdateAllowedTransferModes(vec![
                EnergyTransferMode::Mcs,
            ])),
            now(),
        );

        assert_eq!(
            core.advertised_transfer_modes(),
            vec![EnergyTransferMode::DcExtended]
        );
    }

    #[test]
    fn the_advertised_transfer_mode_set_is_empty_until_it_is_derived() {
        assert!(core().advertised_transfer_modes().is_empty());
    }

    #[test]
    fn a_disable_command_announces_disabled_carrying_its_source() {
        let mut core = core();

        let effects = core.apply(
            disable(EnableSource::ServiceTechnician, 5, EnableScope::Connector),
            now(),
        );

        assert!(effects.iter().any(|effect| matches!(
            effect,
            Effect::PublishEnableEvent {
                event: SessionEvent::Disabled,
                source: EnableEntry {
                    source: EnableSource::ServiceTechnician,
                    state: EnableState::Disable,
                    priority: 5,
                },
            }
        )));
        assert!(!core.enable_table().connector_enabled());
    }

    #[test]
    fn a_connector_scoped_enable_restores_the_connector_and_returns_to_idle() {
        // The session is driven out of Idle first, otherwise the phase
        // assertion below holds for a core that never touches the phase at all.
        let mut core = core();
        core.apply(
            Event::Command(Command::AuthorizeResponse {
                token: id_tag_for_tests("tok", false),
                status: AuthorizationStatus::Accepted,
                certificate: None,
                tariff: TariffMessages::default(),
                reservation_id: None,
            }),
            now(),
        );
        core.apply(
            disable(EnableSource::Csms, 5, EnableScope::Connector),
            now(),
        );
        assert_eq!(core.session().phase, SessionPhase::Stopping);

        let effects = core.apply(enable(EnableSource::Csms, 5, EnableScope::Connector), now());

        assert!(effects.iter().any(|effect| matches!(
            effect,
            Effect::PublishEnableEvent {
                event: SessionEvent::Enabled,
                ..
            }
        )));
        assert!(core.enable_table().connector_enabled());
        assert_eq!(core.session().phase, SessionPhase::Idle);
    }

    #[test]
    fn an_evse_scoped_enable_does_not_return_a_disabled_connector_to_idle() {
        // Re-entry to idle is gated on connector enabled state
        // (`Charger.cpp:1726`), which an EVSE scoped enable cannot restore.
        let mut core = core();
        core.apply(
            Event::Command(Command::AuthorizeResponse {
                token: id_tag_for_tests("tok", false),
                status: AuthorizationStatus::Accepted,
                certificate: None,
                tariff: TariffMessages::default(),
                reservation_id: None,
            }),
            now(),
        );
        core.apply(
            disable(EnableSource::Csms, 5, EnableScope::Connector),
            now(),
        );

        core.apply(enable(EnableSource::Csms, 5, EnableScope::Evse), now());

        assert_eq!(core.session().phase, SessionPhase::Stopping);
    }

    #[test]
    fn an_evse_scoped_enable_cannot_restore_a_disabled_connector() {
        // The asymmetry at `Charger.cpp:1720-1726`: connector state is assigned
        // only for a connector scoped request, and re-entry to idle is gated on
        // connector state.
        let mut core = core();
        core.apply(
            disable(EnableSource::Csms, 5, EnableScope::Connector),
            now(),
        );

        core.apply(enable(EnableSource::Csms, 5, EnableScope::Evse), now());

        assert!(
            !core.enable_table().connector_enabled(),
            "an EVSE scoped enable leaves connector state alone"
        );
    }

    #[test]
    fn a_repeated_report_from_the_same_source_announces_nothing() {
        let mut core = core();
        core.apply(
            disable(EnableSource::Csms, 5, EnableScope::Connector),
            now(),
        );

        let again = core.apply(
            disable(EnableSource::Csms, 5, EnableScope::Connector),
            now(),
        );

        assert!(
            !again
                .iter()
                .any(|effect| matches!(effect, Effect::PublishEnableEvent { .. })),
            "no state change, no announcement, got {again:?}"
        );
    }

    /// A source that withdraws leaves the decision to the others.
    ///
    /// `Unassigned` is not a vote: `enable_disable_source_table_update` records
    /// the row and the walk skips it (`Charger.cpp:1776-1853`), so the answer
    /// is whatever the remaining sources say. The boundary had to turn the
    /// third state into one of two commands and chose `Enable`, so a source
    /// withdrawing kept its priority and **won**: a highest authority source
    /// letting go forced the port enabled over a disable that should have
    /// stood.
    #[test]
    fn a_source_that_withdraws_stops_deciding_rather_than_voting_to_enable() {
        let mut core = core_up();
        // A low authority disable, and a higher authority source that has said
        // nothing yet.
        core.apply(disable(EnableSource::LocalApi, 42, EnableScope::Evse), now());
        assert!(!core.enable.resolve().0, "the control: disabled");

        // The high authority source withdraws.
        let withdrawn = core.apply(
            reported(
                EnableSource::LocalKeyLock,
                EnableState::Unassigned,
                0,
                EnableScope::Evse,
            ),
            now(),
        );

        assert!(
            !core.enable.resolve().0,
            "the disable still stands: {withdrawn:?}"
        );
        assert!(
            !withdrawn
                .iter()
                .any(|effect| matches!(effect, Effect::PublishEnableEvent { .. })),
            "and nothing was announced, because nothing changed: {withdrawn:?}"
        );
    }

    /// The other half: a source that withdraws its own disable does change the
    /// answer, because the row it leaves behind is skipped.
    #[test]
    fn withdrawing_a_disable_re_enables_the_port() {
        let mut core = core_up();
        core.apply(disable(EnableSource::Csms, 1, EnableScope::Evse), now());
        assert!(!core.enable.resolve().0, "the control");

        core.apply(
            reported(EnableSource::Csms, EnableState::Unassigned, 1, EnableScope::Evse),
            now(),
        );

        assert!(core.enable.resolve().0);
    }

    #[test]
    fn a_winning_disable_routes_an_active_session_through_stopping() {
        let mut core = core_awaiting_authorization();
        core.apply(
            Event::Command(Command::AuthorizeResponse {
                token: id_tag_for_tests("tok", false),
                status: AuthorizationStatus::Accepted,
                certificate: None,
                tariff: TariffMessages::default(),
                reservation_id: None,
            }),
            now(),
        );

        let effects = core.apply(
            disable(EnableSource::Csms, 5, EnableScope::Connector),
            now(),
        );

        // The route, not where it settled: the pass crosses stopping and
        // carries on to the resting state the disable asked for, so the
        // announcement is the evidence and the settled phase is not.
        assert!(
            published_events(&effects).contains(&SessionEvent::StoppingCharging),
            "got {effects:?}"
        );
        assert!(effects.iter().any(stops_transaction));
        let announced = index_of(&effects, |effect| {
            matches!(effect, Effect::PublishEnableEvent { .. })
        });
        let stopped = index_of(&effects, stops_transaction);
        assert!(
            announced < stopped,
            "the C++ signals the event before arming the teardown \
             (`Charger.cpp:1758-1769`), got {effects:?}"
        );
    }

    #[test]
    fn a_winning_disable_reaches_the_power_path() {
        // Without this the reducer's disable arm is dead in a live deployment:
        // the phase is assigned, the event is published, and the port is never
        // stopped at the board.
        let (mut core, events, _calls) = core_recording_path();

        core.apply(
            disable(EnableSource::Csms, 5, EnableScope::Connector),
            now(),
        );

        let events_now = events.lock().unwrap().clone();
        assert!(
            events_now.contains(&PathEvent::Disable),
            "a winning disable must reach the path, got {:?}",
            events_now
        );
    }

    #[test]
    fn a_winning_enable_reaches_the_power_path() {
        let (mut core, events, _calls) = core_recording_path();
        core.apply(
            disable(EnableSource::Csms, 5, EnableScope::Connector),
            now(),
        );

        core.apply(enable(EnableSource::Csms, 5, EnableScope::Connector), now());

        let events_now = events.lock().unwrap().clone();
        assert!(
            events_now.contains(&PathEvent::Enable),
            "a winning enable must reach the path, got {:?}",
            events_now
        );
    }

    #[test]
    fn a_pause_and_a_resume_command_both_reach_the_power_path() {
        // `evse_managerImpl.cpp:474-480` hands both straight to `Charger`.
        // Dropped here, `AcState::ChargingPausedEvse` is unreachable in a real
        // deployment however complete the reducer is.
        let (mut core, events, _calls) = core_recording_path();

        core.apply(Event::Command(Command::PauseCharging), now());
        core.apply(Event::Command(Command::ResumeCharging), now());

        let seen = events.lock().unwrap().clone();
        assert!(
            seen.contains(&PathEvent::PauseRequested),
            "a pause must reach the path, got {seen:?}"
        );
        assert!(
            seen.contains(&PathEvent::ResumeRequested),
            "a resume must reach the path, got {seen:?}"
        );
    }

    #[test]
    fn the_cable_check_request_reaches_the_power_path() {
        // `EvseManager.cpp:563-567`. It arrives from `main.rs` and previously
        // fell through the path's catch-all, which left the whole DC cable
        // check sequence without a production caller.
        let (mut core, events, _calls) = core_recording_path();

        core.apply(Event::Hlc(HlcEvent::RequiresCableCheck), now());

        let seen = events.lock().unwrap().clone();
        assert!(
            seen.contains(&PathEvent::CableCheckRequired),
            "the cable check request must reach the path, got {seen:?}"
        );
    }

    #[test]
    fn every_event_a_path_acts_on_narrows_to_its_own_path_event() {
        // The narrowing is the one place `Event` becomes `PathEvent`, so a
        // stage routed to the wrong variant is a silent misroute that no
        // implementation can catch: it sees a well formed input either way.
        let (mut core, events, _calls) = core_recording_path();

        let routed = [
            (
                Event::Hlc(HlcEvent::SessionSetup {
                    evcc_id: "de:pionix:0001".into(),
                }),
                PathEvent::HlcSessionSetup,
            ),
            (
                Event::Hlc(HlcEvent::RequiresCableCheck),
                PathEvent::CableCheckRequired,
            ),
            (
                Event::Hlc(HlcEvent::PreChargeStarted),
                PathEvent::PreChargeStarted,
            ),
            (
                Event::Hlc(HlcEvent::CurrentDemandStarted),
                PathEvent::CurrentDemandStarted,
            ),
            (
                Event::Hlc(HlcEvent::CurrentDemandFinished),
                PathEvent::CurrentDemandFinished,
            ),
            (
                Event::Hlc(HlcEvent::StopFromEv(StopReason::Local)),
                PathEvent::StopFromEv,
            ),
            (
                Event::SupplyVoltageCurrent {
                    voltage_v: 412.5,
                    current_a: 7.0,
                },
                PathEvent::SupplyVoltage { voltage_v: 412.5 },
            ),
            (
                Event::Isolation(IsolationReading { resistance_ohm: 1234.0, ..Default::default() }),
                PathEvent::Isolation(IsolationReading { resistance_ohm: 1234.0, ..Default::default() }),
            ),
            (
                Event::Command(Command::PauseCharging),
                PathEvent::PauseRequested,
            ),
            (
                Event::Command(Command::ResumeCharging),
                PathEvent::ResumeRequested,
            ),
        ];

        for (event, expected) in routed {
            events.lock().unwrap().clear();
            core.apply(event.clone(), now());
            let events_now = events.lock().unwrap().clone();
            assert_eq!(
                events_now.as_slice(),
                [expected],
                "{event:?} narrowed wrongly"
            );
        }
    }

    #[test]
    fn an_event_no_path_can_act_on_never_reaches_one() {
        // The other half of the narrowing. A meter reading and a selected
        // energy transfer mode are both recorded nowhere, which the arms in
        // `apply` say out loud; this holds them to it rather than letting a
        // future arm quietly route one into a path that has no use for it.
        let (mut core, events, _calls) = core_recording_path();

        core.apply(
            Event::Meter(MeterReading {
                energy_wh_import: 1.0,
                power_w: Some(Power {
                    total_w: 2.0,
                    ..Power::default()
                }),
                voltage_v: 3.0,
                current_a: 4.0,
                dc_voltage_v: None,
                phase_currents_a: None,
            }),
            now(),
        );
        core.apply(
            Event::Hlc(HlcEvent::ModeSelected {
                transfer: "AC_single_phase_core".into(),
            }),
            now(),
        );

        let events_now = events.lock().unwrap().clone();
        assert!(
            events_now.is_empty(),
            "got {:?}",
            events_now
        );
    }

    /// The three inbound AC parameter facts, driven through `Core`.
    ///
    /// `ac_params` decides what each becomes and `HlcPort` decides whether it
    /// goes out at all; what is asserted here is the wiring, which is where a
    /// port of this shape goes wrong: a fact routed to the path instead of the
    /// port, or a fact whose route was never added and which therefore vanishes
    /// with no arm saying so.
    /// The enforced limits arm, at the level where a route can be missing
    /// rather than wrong. The gate is silent on a mismatch by design
    /// (`energyImpl.cpp:386`), so "nothing happened" is a legitimate outcome
    /// here and the only way to tell it from an unwired arm is to assert both
    /// sides of the same gate.
    mod energy_enforcement {
        use super::*;

        fn capable_dc_core() -> (Core, RecordedEvents) {
            let (mut core, events, _calls) = dc_core_recording_path();
            core.apply(
                Event::Bsp(BspEvent::Capabilities(HardwareCapabilities {
                    max_phase_count_import: 3,
                    min_phase_count_import: 1,
                    ..HardwareCapabilities::default()
                })),
                now(),
            );
            core.apply(
                Event::PowerSupplyCapabilities(Box::new(PowerSupplyCapabilities {
                    bidirectional: true,
                    max_export_voltage_v: 950.0,
                    min_export_voltage_v: 150.0,
                    max_export_current_a: 400.0,
                    min_export_current_a: 2.0,
                    max_export_power_w: 300_000.0,
                    max_import_current_a: Some(300.0),
                    min_import_current_a: Some(4.0),
                    max_import_power_w: Some(250_000.0),
                    min_import_voltage_v: Some(200.0),
                    ..PowerSupplyCapabilities::sane_default()
                })),
                now(),
            );
            core.apply(
                Event::SupplyVoltageCurrent {
                    voltage_v: 400.0,
                    current_a: 0.0,
                },
                now(),
            );
            events.lock().unwrap().clear();
            (core, events)
        }

        fn enforced(uuid: &str, ampere: f64, watt: f64) -> Event {
            Event::EnforcedLimits(Box::new(energy::enforce::EnforcedLimits {
                uuid: uuid.to_owned(),
                valid_for_s: 60,
                schedule: Vec::new(),
                limits_root_side: energy::enforce::LimitsRes {
                    ac_max_current_a: Some(energy::flow_request::NumberWithSource::new(
                        ampere, "test",
                    )),
                    total_power_w: Some(energy::flow_request::NumberWithSource::new(watt, "test")),
                    ac_max_phase_count: None,
                },
            }))
        }

        /// An answer addressed to another node does nothing at all, not even a
        /// republish. This is the half a port gets wrong in the invisible
        /// direction: give the flow request a fresh identity per publish and
        /// every enforced limit lands here and is dropped in silence.
        #[test]
        fn an_answer_for_another_node_produces_no_effects_whatsoever() {
            let (mut core, events) = capable_dc_core();
            let effects = core.apply(enforced("some_other_node", 200.0, 20_000.0), now());
            assert!(effects.is_empty(), "{effects:?}");
            let events_now = events.lock().unwrap().clone();
            assert!(
                events_now.is_empty(),
                "the path heard about it: {events_now:?}"
            );
        }

        /// The same answer addressed to this node reaches all four of its
        /// destinations: the republish for this node's own subscribers, the
        /// board limit, the vehicle's limit set, and the path.
        #[test]
        fn an_answer_for_this_node_reaches_every_destination() {
            let (mut core, events) = capable_dc_core();
            let effects = core.apply(enforced("evse_manager", 200.0, 20_000.0), now());

            let republished = effects.iter().find_map(|effect| match effect {
                Effect::PublishEnforcedLimits(value) => Some(value),
                _ => None,
            });
            let republished = republished.expect("the enforced limits are republished");
            assert_eq!(republished.uuid, "evse_manager");
            assert_eq!(republished.valid_for_s, 60);

            assert!(
                effects
                    .iter()
                    .any(|effect| matches!(effect, Effect::PublishLimits(_))),
                "{effects:?}"
            );
            assert!(
                effects.iter().any(|effect| matches!(
                    effect,
                    Effect::HlcUpdate(HlcUpdate::DcMaximumLimits(_))
                )),
                "{effects:?}"
            );
            assert!(
                effects.iter().any(|effect| matches!(
                    effect,
                    Effect::HlcUpdate(HlcUpdate::DcMinimumLimits(_))
                )),
                "{effects:?}"
            );

            let seen = events.lock().unwrap().clone();
            assert!(
                seen.iter()
                    .any(|event| matches!(event, PathEvent::DcEnforcedLimits { .. })),
                "{seen:?}"
            );
        }

        /// The maximum limit set the vehicle is told carries the energy
        /// allowance, not the supply's ceiling. The allowance here is 20 kW
        /// against a supply that can do 300 kW, so a port still deriving the
        /// set from the capability report alone names 300 kW.
        ///
        /// The power is asserted rather than the current because the current
        /// derivation needs a target voltage, and the recorder standing in for
        /// the path reports none; the three current derivations are driven in
        /// `core::energy::enforce`, which can supply one.
        #[test]
        fn the_vehicles_limit_set_carries_the_energy_allowance() {
            let (mut core, _events) = capable_dc_core();
            let effects = core.apply(enforced("evse_manager", 200.0, 20_000.0), now());

            let maximum = effects
                .iter()
                .find_map(|effect| match effect {
                    Effect::HlcUpdate(HlcUpdate::DcMaximumLimits(limits)) => Some(*limits),
                    _ => None,
                })
                .expect("a maximum limit set");
            assert_eq!(maximum.maximum_power_w, 20_000.0);
            assert_eq!(maximum.maximum_voltage_v, 950.0);
        }

        /// The second identical answer is suppressed by the change gate, so the
        /// vehicle is not told the same limits twice, while the republish and
        /// the board limit still go out: the gate covers the DC block alone
        /// (`energyImpl.cpp:556`) and not the handler.
        #[test]
        fn an_unchanged_answer_still_republishes_but_tells_the_vehicle_nothing() {
            let (mut core, _events) = capable_dc_core();
            core.apply(enforced("evse_manager", 200.0, 20_000.0), now());
            let effects = core.apply(enforced("evse_manager", 200.0, 20_000.0), now());

            assert!(
                effects
                    .iter()
                    .any(|effect| matches!(effect, Effect::PublishEnforcedLimits(_))),
                "{effects:?}"
            );
            assert!(
                !effects.iter().any(|effect| matches!(
                    effect,
                    Effect::HlcUpdate(HlcUpdate::DcMaximumLimits(_))
                        | Effect::HlcUpdate(HlcUpdate::DcMinimumLimits(_))
                )),
                "{effects:?}"
            );
        }
    }

    /// The UK smart charging random delay, at the level `Core` owns: whether
    /// the four commands reach it, whether the countdown reaches the bus, and
    /// whether the readiness instant the startup branch measures against is
    /// actually threaded. The decision itself is tested in
    /// `core::energy::random_delay`.
    mod uk_random_delay {
        use super::*;

        fn enforced_limit(ampere: f64) -> Event {
            Event::EnforcedLimits(Box::new(energy::enforce::EnforcedLimits {
                uuid: "evse_manager".to_owned(),
                valid_for_s: 60,
                schedule: Vec::new(),
                limits_root_side: energy::enforce::LimitsRes {
                    ac_max_current_a: Some(energy::flow_request::NumberWithSource::new(
                        ampere, "test",
                    )),
                    ..energy::enforce::LimitsRes::default()
                },
            }))
        }

        /// Plugged in and waiting for authentication, which is one of the three
        /// states a delay is allowed in, with the energy tree told how many
        /// phases the board has so an enforced limit is not discarded.
        ///
        /// Startup lands at `at`, so a caller can place its enforced limits
        /// inside or outside the five second startup window deliberately.
        /// Every test here has to choose, because a limit applied inside it
        /// triggers a delay whether or not the limit moved.
        fn waiting_core(at: Instant) -> Core {
            let mut core = ac_hlc_core();
            core.apply(Event::Startup, at);
            core.apply(
                Event::Bsp(BspEvent::Capabilities(HardwareCapabilities {
                    max_phase_count_import: 1,
                    min_phase_count_import: 1,
                    ..HardwareCapabilities::default()
                })),
                at,
            );
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), at);
            core
        }

        /// Well past the startup window, so only a limit change can trigger.
        fn settled(from: Instant) -> Instant {
            from + std::time::Duration::from_secs(10)
        }

        fn countdown(effects: &[Effect]) -> Option<energy::random_delay::CountDown> {
            effects.iter().find_map(|effect| match effect {
                Effect::PublishRandomDelayCountdown(countdown) => Some(*countdown),
                _ => None,
            })
        }

        fn position(effects: &[Effect], of: fn(&Effect) -> bool) -> Option<usize> {
            effects.iter().position(of)
        }

        /// The configured default is off, so the port a deployment gets
        /// without asking publishes nothing on `random_delay` at all.
        #[test]
        fn a_port_with_the_feature_off_publishes_no_countdown() {
            let t0 = now();
            let mut core = waiting_core(t0);
            let effects = core.apply(enforced_limit(16.0), settled(t0));
            assert_eq!(countdown(&effects), None, "got {effects:?}");
            assert!(effects.contains(&Effect::SetOvercurrentLimit(16.0)));
        }

        /// `energyImpl.cpp:491` publishes the countdown before
        /// `publish_enforced_limits` at `:513`. A consumer reading the two
        /// together never sees a withheld limit ahead of the countdown that
        /// explains it.
        #[test]
        fn the_countdown_goes_out_before_the_enforced_limits_it_explains() {
            let t0 = now();
            let mut core = waiting_core(t0);
            core.energy.random_delay_mut().enable();
            let effects = core.apply(enforced_limit(16.0), settled(t0));
            let countdown = position(&effects, |effect| {
                matches!(effect, Effect::PublishRandomDelayCountdown(_))
            })
            .expect("no countdown in {effects:?}");
            let limits = position(&effects, |effect| {
                matches!(effect, Effect::PublishEnforcedLimits(_))
            })
            .expect("no enforced limits");
            assert!(countdown < limits, "got {effects:?}");
        }

        /// Each of the four commands, observed through the next enforced
        /// limit, because that is the only thing any of them changes.
        #[test]
        fn the_four_commands_reach_the_delay() {
            let t0 = now();
            let at = settled(t0);
            // `enable` turns it on: the step up from zero is now withheld.
            let mut core = waiting_core(t0);
            core.apply(Event::Command(Command::RandomDelayEnable), at);
            let effects = core.apply(enforced_limit(16.0), at);
            assert!(countdown(&effects).expect("enabled").countdown_s > 0);
            assert!(
                effects.contains(&Effect::SetOvercurrentLimit(0.0)),
                "{effects:?}"
            );

            // `cancel` releases it, and the same request is not redrawn.
            core.apply(Event::Command(Command::RandomDelayCancel), at);
            let effects = core.apply(enforced_limit(16.0), at);
            assert_eq!(countdown(&effects).expect("still enabled").countdown_s, 0);
            assert!(
                effects.contains(&Effect::SetOvercurrentLimit(16.0)),
                "{effects:?}"
            );

            // `disable` stops the publish as well as the delay.
            core.apply(Event::Command(Command::RandomDelayDisable), at);
            let effects = core.apply(enforced_limit(20.0), at);
            assert_eq!(countdown(&effects), None, "{effects:?}");

            // `set_duration_s(0)` is accepted off the bus and bounds the next
            // delay to nothing rather than dividing by zero.
            core.apply(Event::Command(Command::RandomDelayEnable), at);
            core.apply(Event::Command(Command::RandomDelaySetDuration(0)), at);
            let effects = core.apply(enforced_limit(6.0), at);
            assert_eq!(countdown(&effects).expect("enabled").countdown_s, 0);
        }

        /// A cancel inside the startup window is re-armed at once, because the
        /// startup branch does not look at whether the limit moved. It follows
        /// from `energyImpl.cpp:373-380` and it is the one case where "the
        /// effect is the same as if the time expired just now"
        /// (`interfaces/uk_random_delay.yaml`) does not hold: the time
        /// expiring would not have drawn a second interval.
        #[test]
        fn a_cancel_inside_the_startup_window_is_redrawn_at_once() {
            let t0 = now();
            let mut core = waiting_core(t0);
            let inside = t0 + std::time::Duration::from_secs(1);
            core.apply(Event::Command(Command::RandomDelayEnable), inside);
            let effects = core.apply(enforced_limit(16.0), inside);
            assert!(countdown(&effects).expect("enabled").countdown_s > 0);

            core.apply(Event::Command(Command::RandomDelayCancel), inside);
            let effects = core.apply(enforced_limit(16.0), inside);
            assert!(
                countdown(&effects).expect("enabled").countdown_s > 0,
                "the startup detector released the port: {effects:?}"
            );
        }

        /// `mod->timepoint_ready_for_charging` reaches the decision. Enabling
        /// the feature after a limit is already in force leaves no limit
        /// change pending, so the startup branch is the only one that can
        /// fire, and what it holds is zero rather than the limit in force.
        #[test]
        fn a_vehicle_attached_just_after_startup_is_held_at_zero() {
            let t0 = now();
            let mut core = waiting_core(t0);
            core.apply(enforced_limit(16.0), t0);
            core.energy.random_delay_mut().enable();

            let effects = core.apply(enforced_limit(16.0), t0 + std::time::Duration::from_secs(1));
            let countdown = countdown(&effects).expect("the feature is on");
            assert!(countdown.countdown_s > 0, "no startup delay: {effects:?}");
            assert_eq!(countdown.current_limit_during_delay_a, 0.0);
            assert_eq!(countdown.current_limit_after_delay_a, 16.0);
            assert!(
                effects.contains(&Effect::SetOvercurrentLimit(0.0)),
                "the board was given the remembered current: {effects:?}"
            );
        }

        /// The same core with no readiness announced cannot reach the startup
        /// branch, which is what says the instant is read rather than assumed.
        #[test]
        fn a_port_that_never_announced_readiness_has_no_startup_window() {
            let mut core = ac_hlc_core();
            core.apply(
                Event::Bsp(BspEvent::Capabilities(HardwareCapabilities {
                    max_phase_count_import: 1,
                    min_phase_count_import: 1,
                    ..HardwareCapabilities::default()
                })),
                now(),
            );
            core.apply(enforced_limit(16.0), now());
            core.energy.random_delay_mut().enable();
            let effects = core.apply(enforced_limit(16.0), now());
            assert_eq!(
                countdown(&effects).expect("the feature is on").countdown_s,
                0,
                "got {effects:?}"
            );
            assert!(
                effects.contains(&Effect::SetOvercurrentLimit(16.0)),
                "{effects:?}"
            );
        }
    }

    mod ac_parameters {
        use super::*;

        fn initialize_energy(core: &mut Core, phases: i64) {
            core.apply(
                Event::Bsp(BspEvent::Capabilities(HardwareCapabilities {
                    max_phase_count_import: phases,
                    min_phase_count_import: 1,
                    ..HardwareCapabilities::default()
                })),
                now(),
            );
        }

        fn enforced_limit(ampere: f64) -> Event {
            Event::EnforcedLimits(Box::new(energy::enforce::EnforcedLimits {
                uuid: "evse_manager".to_owned(),
                valid_for_s: 60,
                schedule: Vec::new(),
                limits_root_side: energy::enforce::LimitsRes {
                    ac_max_current_a: Some(energy::flow_request::NumberWithSource::new(
                        ampere, "test",
                    )),
                    ..energy::enforce::LimitsRes::default()
                },
            }))
        }

        fn ac_limit_commands(effects: &[Effect]) -> Vec<HlcUpdate> {
            effects
                .iter()
                .filter_map(|effect| match effect {
                    Effect::HlcUpdate(
                        update @ (HlcUpdate::AcMaxCurrent(_)
                        | HlcUpdate::AcTargetPower(_)
                        | HlcUpdate::AcPresentPower(_)),
                    ) => Some(update.clone()),
                    _ => None,
                })
                .collect()
        }

        /// `energy_grid/energyImpl.cpp:519` into `Charger::set_max_current`,
        /// whose `signal_max_current` reaches the stack (`Charger.cpp:1322`).
        #[test]
        fn an_enforced_limit_reaches_the_vehicle_as_well_as_the_board() {
            let mut core = ac_hlc_core();
            initialize_energy(&mut core, 1);
            let effects = core.apply(enforced_limit(20.0), now());
            assert_eq!(
                ac_limit_commands(&effects),
                vec![HlcUpdate::AcMaxCurrent(20.0)]
            );
            assert!(
                effects.contains(&Effect::SetOvercurrentLimit(20.0)),
                "the board still hears it: {effects:?}"
            );
        }

        /// The same limit change, told the other way once the vehicle has
        /// selected an ISO 15118-20 AC service. This is the branch checkbox six
        /// asks for, end to end through `Core`.
        #[test]
        fn the_same_enforced_limit_becomes_a_target_power_for_an_iso20_ac_session() {
            let mut core = ac_hlc_core_with(&[("ac_nominal_voltage", serde_json::json!(230.0))]);
            initialize_energy(&mut core, 1);

            let iso2 = core.apply(enforced_limit(20.0), now());
            core.apply(
                Event::Hlc(HlcEvent::SelectedService(SelectedService::Ac)),
                now(),
            );
            let iso20 = core.apply(enforced_limit(20.0), now());

            assert_eq!(
                ac_limit_commands(&iso2),
                vec![HlcUpdate::AcMaxCurrent(20.0)]
            );
            // The board report has not arrived, so the phase count is the safe
            // default one (`EvseManager.cpp:154`): 20 A at 230 V on one phase.
            assert_eq!(
                ac_limit_commands(&iso20),
                vec![HlcUpdate::AcTargetPower(Power {
                    total_w: 4_600.0,
                    ..Power::default()
                })]
            );
            assert_ne!(
                ac_limit_commands(&iso2),
                ac_limit_commands(&iso20),
                "one limit change, two sessions, two different commands"
            );
        }

        /// A board support report announces the envelope through the port
        /// (`EvseManager.cpp:287`), and it also widens the phase count the
        /// target power converts at.
        #[test]
        fn a_board_report_announces_the_envelope_and_widens_the_target_power() {
            let mut core = ac_hlc_core_with(&[("ac_nominal_voltage", serde_json::json!(230.0))]);
            core.apply(
                Event::Hlc(HlcEvent::SelectedService(SelectedService::Ac)),
                now(),
            );
            let effects = core.apply(
                Event::Bsp(BspEvent::Capabilities(HardwareCapabilities {
                    max_current_a_import: 32.0,
                    min_current_a_import: 6.0,
                    max_phase_count_import: 3,
                    min_phase_count_import: 1,
                    max_current_a_export: 0.0,
                    min_current_a_export: 0.0,
                    max_phase_count_export: 0,
                    min_phase_count_export: 0,
                    supports_changing_phases_during_charging: false,
                    supports_cp_state_e: false,
                })),
                now(),
            );
            assert_eq!(
                effects
                    .iter()
                    .filter(|effect| matches!(
                        effect,
                        Effect::HlcUpdate(
                            HlcUpdate::AcMaximumLimits(_)
                                | HlcUpdate::AcMinimumLimits(_)
                                | HlcUpdate::AcParameters(_)
                        )
                    ))
                    .count(),
                3,
                "{effects:?}"
            );

            let effects = core.apply(enforced_limit(20.0), now());
            assert_eq!(
                ac_limit_commands(&effects),
                vec![HlcUpdate::AcTargetPower(Power {
                    total_w: 13_800.0,
                    ..Power::default()
                })],
                "three phases now, so three times the watts"
            );
        }

        /// The combination where silence is the answer, driven end to end
        /// because silence is exactly what a missing route also looks like.
        ///
        /// A DER session selects `AC_DER_IEC`, which the C++ names in neither
        /// arm (`EvseManager.cpp:1244`, `:1249-1250`), so its limit changes
        /// reach the vehicle not at all. That is a gap in the C++ rather than a
        /// decision here, and it is preserved; what this pins is that the
        /// silence is the branch's and not a route that was never wired, by
        /// asserting the board still hears the same change.
        #[test]
        fn a_der_session_hears_no_limit_while_the_board_still_does() {
            let mut core = ac_hlc_core();
            initialize_energy(&mut core, 1);
            core.apply(
                Event::Hlc(HlcEvent::SelectedService(SelectedService::AcDerIec)),
                now(),
            );
            let effects = core.apply(enforced_limit(20.0), now());

            assert!(
                ac_limit_commands(&effects).is_empty(),
                "the C++ names neither arm for a DER service: {effects:?}"
            );
            assert!(
                effects.contains(&Effect::SetOvercurrentLimit(20.0)),
                "the change still reached the board, so the route exists: {effects:?}"
            );
        }

        /// `EvseManager.cpp:1167-1168`. The meter reading reaches the vehicle
        /// once a service has been selected and not before.
        #[test]
        fn a_meter_reading_reaches_the_vehicle_only_after_a_service_was_selected() {
            let mut core = ac_hlc_core();
            let reading = MeterReading {
                energy_wh_import: 100.0,
                power_w: Some(Power {
                    total_w: 3_300.0,
                    l1_w: Some(1_100.0),
                    l2_w: Some(1_100.0),
                    l3_w: Some(1_100.0),
                }),
                voltage_v: 230.0,
                current_a: 14.0,
                phase_currents_a: None,
                            dc_voltage_v: None,
            };

            // Before the selection the reading carries the meter's record and
            // no power figure: the record is sent to any port with a stack and
            // the power only for a selected ISO 15118-20 service, which is the
            // weaker guard the C++ has one line above the stronger one.
            assert_eq!(
                ac_limit_commands(&core.apply(Event::Meter(reading), now())),
                Vec::new()
            );
            core.apply(
                Event::Hlc(HlcEvent::SelectedService(SelectedService::Ac)),
                now(),
            );
            assert_eq!(
                ac_limit_commands(&core.apply(Event::Meter(reading), now())),
                vec![HlcUpdate::AcPresentPower(reading.power_w.unwrap())]
            );
        }

        /// The `p.power_W` half of the same conjunction. A meter that reports no
        /// power figure leaves the vehicle's last one standing, which is a
        /// different thing from replacing it with zero watts.
        ///
        /// The reading still carries the meter's record, which is sent on every
        /// reading and asks nothing of the power figure.
        #[test]
        fn a_meter_reading_without_a_power_figure_announces_no_power() {
            let mut core = ac_hlc_core();
            core.apply(
                Event::Hlc(HlcEvent::SelectedService(SelectedService::Ac)),
                now(),
            );
            let effects = core.apply(
                Event::Meter(MeterReading {
                    energy_wh_import: 100.0,
                    power_w: None,
                    voltage_v: 230.0,
                    current_a: 14.0,
                    phase_currents_a: None,
                    dc_voltage_v: None,
                }),
                now(),
            );

            assert_eq!(ac_limit_commands(&effects), Vec::new());
            assert_eq!(
                effects,
                vec![Effect::HlcUpdate(HlcUpdate::MeterInfo)],
                "the record and nothing else"
            );
        }

        /// A selected service reaches the port and produces nothing itself, and
        /// it reaches no path: nothing on a power path reads it.
        #[test]
        fn a_selected_service_produces_nothing_and_reaches_no_path() {
            let (mut core, events, _calls) = core_recording_path();
            assert!(core
                .apply(
                    Event::Hlc(HlcEvent::SelectedService(SelectedService::AcBpt)),
                    now(),
                )
                .is_empty());
            let events_now = events.lock().unwrap().clone();
            assert!(
                events_now.is_empty(),
                "got {:?}",
                events_now
            );
        }
    }

    #[test]
    fn a_losing_report_does_not_reach_the_path() {
        // Arbitration answers with the port's availability. A report that does
        // not change that answer changes nothing about the port.
        let (mut core, events, _calls) = core_recording_path();
        core.apply(
            disable(EnableSource::Csms, 1, EnableScope::Connector),
            now(),
        );
        events.lock().unwrap().clear();

        core.apply(
            enable(EnableSource::LocalApi, 9, EnableScope::Connector),
            now(),
        );

        let events_now = events.lock().unwrap().clone();
        assert!(
            events_now.is_empty(),
            "a losing report must not reach the path, got {:?}",
            events_now
        );
    }

    #[test]
    fn a_repeated_report_from_the_same_source_does_not_reach_the_path() {
        let (mut core, events, _calls) = core_recording_path();
        core.apply(
            disable(EnableSource::Csms, 5, EnableScope::Connector),
            now(),
        );
        events.lock().unwrap().clear();

        core.apply(
            disable(EnableSource::Csms, 5, EnableScope::Connector),
            now(),
        );

        let events_now = events.lock().unwrap().clone();
        assert!(
            events_now.is_empty(),
            "no state change, nothing to act on, got {:?}",
            events_now
        );
    }

    #[test]
    fn a_disable_arriving_on_a_live_session_stops_it_before_it_reaches_the_path() {
        // The C++ routes an occupied port through StoppingCharging rather than
        // dropping power where it stands (`Charger.cpp:1756-1769`), so the stop
        // has to be handed to the path ahead of the availability change.
        let (mut core, reasons, events, calls) = core_recording_everything();
        core.apply(authorize(true, AuthorizationKind::Eim), now());

        core.apply(
            disable(EnableSource::Csms, 5, EnableScope::Connector),
            now(),
        );

        assert_eq!(core.session().phase, SessionPhase::Stopping);
        let reasons_now = reasons.lock().unwrap().clone();
        assert_eq!(reasons_now, vec![StopReason::EvseDisabled]);
        let events_now = events.lock().unwrap().clone();
        assert!(
            matches!(events_now.as_slice(), [PathEvent::Disable]),
            "got {:?}",
            events_now
        );
        let calls_now = calls.lock().unwrap().clone();
        assert_eq!(
            calls_now,
            vec!["stop", "path event"],
            "the availability change must arrive after the stop, otherwise the \
             port is out of service before the session has been ended"
        );
    }

    #[test]
    fn an_evse_scoped_disable_reaches_the_path_although_it_leaves_connector_state_alone() {
        // The asymmetry from the other side: taking the port out of service is
        // not scope gated (`Charger.cpp:1756-1769`), so an EVSE scoped disable
        // stops the port even though connector state is untouched.
        let (mut core, events, _calls) = core_recording_path();

        core.apply(disable(EnableSource::Csms, 5, EnableScope::Evse), now());

        assert!(
            core.enable_table().connector_enabled(),
            "an EVSE scoped request leaves connector state alone"
        );
        let events_now = events.lock().unwrap().clone();
        assert!(
            matches!(events_now.as_slice(), [PathEvent::Disable]),
            "got {:?}",
            events_now
        );
    }

    #[test]
    fn a_disable_arriving_on_a_session_already_stopping_still_reaches_the_path() {
        // There is no phase left to assign here, and the port still has to be
        // taken out of service: the C++ arms its flag and the stopping state
        // carries the port into Disabled on its own.
        let (mut core, events, _calls) = core_recording_path();
        core.apply(authorize(true, AuthorizationKind::Eim), now());
        core.apply(
            Event::Command(Command::StopTransaction {
                reason: StopTransactionReason::Local,
                id_tag: None,
            }),
            now(),
        );
        assert_eq!(core.session().phase, SessionPhase::Stopping);
        events.lock().unwrap().clear();

        core.apply(
            disable(EnableSource::Csms, 5, EnableScope::Connector),
            now(),
        );

        let events_now = events.lock().unwrap().clone();
        assert!(
            matches!(events_now.as_slice(), [PathEvent::Disable]),
            "got {:?}",
            events_now
        );
    }

    #[test]
    fn the_report_handed_to_the_path_carries_the_source_that_won_the_arbitration() {
        // The reporter is not always the winner: a source that lowers its own
        // priority out of the way hands the decision to another entry. What the
        // path is told has to match what was announced.
        let (mut core, events, _calls) = core_recording_path();
        core.apply(
            enable(EnableSource::LocalApi, 3, EnableScope::Connector),
            now(),
        );
        core.apply(
            disable(EnableSource::Csms, 1, EnableScope::Connector),
            now(),
        );
        events.lock().unwrap().clear();

        let effects = core.apply(enable(EnableSource::Csms, 9, EnableScope::Connector), now());

        let announced = effects
            .iter()
            .find_map(|effect| match effect {
                Effect::PublishEnableEvent { source, .. } => Some(*source),
                _ => None,
            })
            .expect("the enable must be announced");
        assert_eq!(announced.source, EnableSource::LocalApi);
        let events_now = events.lock().unwrap().clone();
        assert!(
            matches!(events_now.as_slice(), [PathEvent::Enable]),
            "got {:?}",
            events_now
        );
    }

    #[test]
    fn an_evse_scoped_enable_does_not_restart_a_board_whose_connector_stays_disabled() {
        // `Charger.cpp:1726-1731` guards the board restart on connector state,
        // so an EVSE scoped enable announces the change without putting the
        // port back into service. The same asymmetry keeps the phase where it
        // was.
        let (mut core, events, _calls) = core_recording_path();
        core.apply(
            disable(EnableSource::Csms, 5, EnableScope::Connector),
            now(),
        );
        events.lock().unwrap().clear();

        let effects = core.apply(enable(EnableSource::Csms, 5, EnableScope::Evse), now());

        assert!(
            effects
                .iter()
                .any(|effect| matches!(effect, Effect::PublishEnableEvent { .. })),
            "the change is still announced, got {effects:?}"
        );
        let events_now = events.lock().unwrap().clone();
        assert!(
            events_now.is_empty(),
            "a disabled connector keeps the board stopped, got {:?}",
            events_now
        );
    }

    #[test]
    fn an_accepted_authorization_announces_authorized_before_the_transaction_starts() {
        let mut core = core_awaiting_authorization();

        let effects = core.apply(authorize(true, AuthorizationKind::Eim), now());

        let announced = index_of(&effects, |effect| {
            published(effect, SessionEvent::Authorized)
        });
        let started = index_of(&effects, starts_transaction);
        assert!(
            announced < started,
            "`Charger.cpp:1643` signals before the flag is set, got {effects:?}"
        );
    }

    #[test]
    fn the_metering_transaction_carries_the_session_identity() {
        // `Charger.cpp:1409` fills `TransactionReq::transaction_id` from
        // `shared_context.session_uuid` and `Charger.cpp:1455` stops on the same
        // string. The session identity is the transaction identity, so the
        // effect carries it rather than the boundary minting a second one.
        let mut core = core_awaiting_authorization();

        let effects = core.apply(authorize(true, AuthorizationKind::Eim), now());
        let uuid = core
            .session()
            .id
            .clone()
            .expect("a started session holds its identity");

        let started = effects
            .iter()
            .find_map(|effect| match effect {
                Effect::StartTransaction { transaction_id, .. } => Some(transaction_id.clone()),
                _ => None,
            })
            .expect("an accepted authorization starts the metering transaction");
        assert_eq!(started, uuid);

        let effects = core.apply(
            Event::Command(Command::StopTransaction {
                reason: StopTransactionReason::Remote,
                id_tag: None,
            }),
            now(),
        );
        let stopped = effects
            .iter()
            .find_map(|effect| match effect {
                Effect::StopTransaction { transaction_id } => Some(transaction_id.clone()),
                _ => None,
            })
            .expect("a stop closes the metering transaction");
        assert_eq!(
            stopped, uuid,
            "the stop must name the transaction the start opened"
        );
    }

    #[test]
    fn one_transaction_spans_a_pause_and_closes_under_the_id_it_opened() {
        // A pause is not the end of the billing record. Nothing between the
        // start and the stop may reopen or close it, and the stop names the id
        // the start opened.
        let mut core = core_awaiting_authorization();
        core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());

        let opened = core.apply(authorize(true, AuthorizationKind::Eim), now());
        let opened_id = opened
            .iter()
            .find_map(|effect| match effect {
                Effect::StartTransaction { transaction_id, .. } => Some(transaction_id.clone()),
                _ => None,
            })
            .expect("the authorization opens the billing record");

        let mut between = Vec::new();
        for cp in [CpEvent::C, CpEvent::B, CpEvent::C] {
            between.extend(core.apply(Event::Bsp(BspEvent::Cp(cp)), now()));
        }
        assert!(
            !between.iter().any(starts_transaction) && !between.iter().any(stops_transaction),
            "a pause and a resume touch no billing record, got {between:?}"
        );

        let closed = core.apply(
            Event::Command(Command::StopTransaction {
                reason: StopTransactionReason::Remote,
                id_tag: None,
            }),
            now(),
        );
        let closed_ids: Vec<_> = closed
            .iter()
            .filter_map(|effect| match effect {
                Effect::StopTransaction { transaction_id } => Some(transaction_id.clone()),
                _ => None,
            })
            .collect();
        assert_eq!(closed_ids, vec![opened_id]);
    }

    #[test]
    fn a_second_authorization_does_not_open_a_second_metering_transaction() {
        // Both C++ call sites guard with `if (not
        // shared_context.flag_transaction_active)` (`Charger.cpp:384` and
        // `:513`), so a re-validation inside a live session bills once. Without
        // the guard the meter is asked to open a transaction under an id it has
        // already opened.
        let mut core = core_awaiting_authorization();
        let first = core.apply(authorize(true, AuthorizationKind::Eim), now());
        assert_eq!(first.iter().filter(|e| starts_transaction(e)).count(), 1);

        let second = core.apply(authorize(true, AuthorizationKind::Eim), now());

        assert!(
            !second.iter().any(starts_transaction),
            "a live transaction is not reopened, got {second:?}"
        );
    }

    #[test]
    fn the_metering_transaction_carries_the_token_that_authorized_it() {
        // `Charger::start_transaction` fills the OCMF identification from the
        // id token, so the billing record names who charged. The whole record
        // travels, because the request is filled from two of its fields.
        let mut core = core_awaiting_authorization();

        let effects = core.apply(authorize(true, AuthorizationKind::PlugAndCharge), now());

        let (id_token, _) = metering_start(&effects);
        let tag = id_token.expect("the transaction names the identity");
        assert_eq!(tag.value(), "tok");
        assert!(tag.is_plug_and_charge());
    }

    #[test]
    fn the_metering_transaction_is_billed_under_the_credential_presented() {
        // The defect this closes: every credential kind reaches the meter as
        // itself. A card billed as a contract, or as "type not specified", is a
        // signed metrology record naming the wrong kind of user.
        for token_type in EVERY_TOKEN_TYPE {
            let mut core = core_awaiting_authorization();

            let effects = core.apply(authorize_billing(token_type, &[]), now());

            let (id_token, _) = metering_start(&effects);
            assert_eq!(
                id_token.map(|tag| tag.token_type()),
                Some(token_type),
                "{token_type:?}"
            );
        }
    }

    #[test]
    fn a_contract_and_a_card_are_billed_as_different_credentials() {
        // The pair the assertion in the pricing suite compares. Before the
        // token type travelled, both of these answered from one bool and a
        // card came out indistinguishable from an unstated kind.
        let mut contract = core_awaiting_authorization();
        let mut card = core_awaiting_authorization();

        let contract_effects = contract.apply(authorize_billing(IdTokenType::EMaid, &[]), now());
        let card_effects = card.apply(authorize_billing(IdTokenType::Iso14443, &[]), now());

        let contract_type = metering_start(&contract_effects).0.map(|t| t.token_type());
        let card_type = metering_start(&card_effects).0.map(|t| t.token_type());
        assert_eq!(contract_type, Some(IdTokenType::EMaid));
        assert_eq!(card_type, Some(IdTokenType::Iso14443));
        assert_ne!(contract_type, card_type);
    }

    #[test]
    fn the_metering_transaction_is_opened_under_the_tariff_the_verdict_named() {
        // `Charger.cpp:1492-1495`. The tariff has no other source in this
        // module: it arrives on the authorization verdict and nowhere else.
        let mut core = core_awaiting_authorization();

        let effects = core.apply(
            authorize_billing(IdTokenType::Iso14443, &["GBP 0.12/kWh, no idle fee"]),
            now(),
        );

        assert_eq!(
            metering_start(&effects).1.as_deref(),
            Some("GBP 0.12/kWh, no idle fee")
        );
    }

    #[test]
    fn several_tariff_messages_open_the_transaction_under_the_first() {
        let mut core = core_awaiting_authorization();

        let effects = core.apply(
            authorize_billing(
                IdTokenType::Iso14443,
                &["EUR 0.30/kWh", "0,30 EUR/kWh", "0.30 EUR pro kWh"],
            ),
            now(),
        );

        assert_eq!(metering_start(&effects).1.as_deref(), Some("EUR 0.30/kWh"));
    }

    #[test]
    fn an_empty_tariff_message_is_a_tariff_and_not_an_absence() {
        // `MessageContent::content` has no minimum length and `tariff_text` on
        // `powermeter.yaml` has `minLength: 0`, so an empty message is a legal
        // answer that means something different from no message at all.
        // `Charger::start_transaction`'s guard is `tariff_messages.empty()`,
        // not a test on the content, so the empty string is relayed - and a
        // reader here that treated it as absence would quietly re-diverge.
        let mut core = core_awaiting_authorization();

        let effects = core.apply(authorize_billing(IdTokenType::Iso14443, &[""]), now());

        assert_eq!(
            metering_start(&effects).1.as_deref(),
            Some(""),
            "an empty tariff text is not the same answer as an unset field"
        );
    }

    #[test]
    fn a_verdict_with_no_tariff_opens_the_transaction_naming_none() {
        // Nothing is defaulted into the field. `Charger::start_transaction`
        // leaves `tariff_text` unset when the verdict carried no messages, and
        // a value invented here is a price the meter would sign that nobody
        // quoted.
        let mut core = core_awaiting_authorization();

        let effects = core.apply(authorize_billing(IdTokenType::Iso14443, &[]), now());

        assert_eq!(metering_start(&effects).1, None);
    }

    #[test]
    fn a_second_authorization_without_a_tariff_does_not_reuse_the_first_ones() {
        // The tariff is state now, so the read model the metering start bills
        // from has to move with every verdict. Driven through the mirror rather
        // than through a second `StartTransaction`, because a live transaction
        // is deliberately not reopened.
        let mut core = core_awaiting_authorization();
        let first = core.apply(
            authorize_billing(IdTokenType::Iso14443, &["EUR 0.30/kWh"]),
            now(),
        );
        assert_eq!(metering_start(&first).1.as_deref(), Some("EUR 0.30/kWh"));

        core.apply(authorize_billing(IdTokenType::Iso14443, &[]), now());

        assert_eq!(
            core.session().authorized_tariff.text(),
            None,
            "the terms the next metering start would bill under"
        );
    }

    #[test]
    fn the_session_read_model_reports_the_tariff_the_authorization_carried() {
        // `mirror_authorization` is the one writer of all three authorization
        // fields on the session, so the tariff cannot be left behind while the
        // token moves.
        let mut core = core();

        core.apply(
            authorize_billing(IdTokenType::Iso15693, &["EUR 0.42/kWh"]),
            now(),
        );

        assert_eq!(
            core.session().authorized_tariff.text(),
            Some("EUR 0.42/kWh")
        );
        assert_eq!(core.auth().tariff().text(), Some("EUR 0.42/kWh"));
        assert_eq!(
            core.session()
                .authorized_token
                .as_ref()
                .map(IdTag::token_type),
            Some(IdTokenType::Iso15693)
        );
    }

    #[test]
    fn a_refused_authorization_opens_no_transaction_and_records_no_tariff() {
        let mut core = core();

        let effects = core.apply(
            Event::Command(Command::AuthorizeResponse {
                token: id_tag_for_tests("tok", false),
                status: AuthorizationStatus::Blocked,
                certificate: None,
                tariff: TariffMessages::new(vec!["EUR 0.30/kWh".to_owned()]),
                reservation_id: None,
            }),
            now(),
        );

        assert!(
            !effects.iter().any(starts_transaction),
            "a refused verdict bills nothing, got {effects:?}"
        );
        assert_eq!(core.session().authorized_tariff.text(), None);
        assert_eq!(core.auth().tariff().text(), None);
    }

    #[test]
    fn an_authorization_over_iso_reaches_the_power_path_as_plug_and_charge() {
        let mut core = core();

        core.apply(authorize(true, AuthorizationKind::PlugAndCharge), now());

        assert!(core.session().authorized_plug_and_charge);
        assert!(core.auth().authorized_plug_and_charge());
    }

    #[test]
    fn an_external_token_does_not_reach_the_power_path_as_plug_and_charge() {
        let mut core = core();

        core.apply(authorize(true, AuthorizationKind::Eim), now());

        assert!(!core.session().authorized_plug_and_charge);
    }

    #[test]
    fn a_disabled_evse_discards_a_delayed_authorization() {
        // `Charger.cpp:1629-1636`. Without the guard the delayed verdict starts a
        // transaction on an EVSE that is out of service.
        let mut core = core();
        core.apply(
            disable(EnableSource::Csms, 5, EnableScope::Connector),
            now(),
        );

        let effects = core.apply(authorize(true, AuthorizationKind::Eim), now());

        assert!(effects.is_empty(), "{effects:?}");
        assert!(!core.session().transaction_active);
    }

    #[test]
    fn a_cancelled_session_discards_a_delayed_authorization() {
        let mut core = core();
        core.apply(authorize(true, AuthorizationKind::Eim), now());
        core.apply(
            Event::Command(Command::StopTransaction {
                reason: StopTransactionReason::Remote,
                id_tag: None,
            }),
            now(),
        );

        let effects = core.apply(authorize(true, AuthorizationKind::Eim), now());

        assert!(
            !effects.iter().any(starts_transaction),
            "a delayed authorization must not restart a cancelled session, got {effects:?}"
        );
    }

    #[test]
    fn a_winning_disable_takes_an_idle_evse_out_of_service() {
        let mut core = core();

        core.apply(
            disable(EnableSource::Csms, 5, EnableScope::Connector),
            now(),
        );

        assert_eq!(core.session().phase, SessionPhase::Disabled);
    }

    #[test]
    fn an_externally_cancelled_transaction_revokes_the_authorization() {
        // `Charger::cancel_transaction` drops the authorization along with the
        // transaction, which is what the per state poll then reads. It is
        // inside the `flag_transaction_active` guard, so a transaction has to
        // be open for any of it to happen.
        let mut core = core();
        core.apply(Event::Startup, now());
        core.apply(plug_in(), now());
        core.apply(authorize(true, AuthorizationKind::Eim), now());
        assert!(core.auth().authorized());
        assert!(core.session().transaction_active, "a record is open");

        core.apply(
            Event::Command(Command::StopTransaction {
                reason: StopTransactionReason::Remote,
                id_tag: None,
            }),
            now(),
        );

        assert!(!core.auth().authorized());
    }

    #[test]
    fn a_stopping_session_without_an_authorization_finishes_on_the_next_pass() {
        // The poll at `Charger.cpp:1042-1043`: the stopping route cannot recover
        // from a lost authorization, so it completes. The stop has to find a
        // transaction open to take the authorization down at all.
        let mut core = core();
        core.apply(Event::Startup, now());
        core.apply(plug_in(), now());
        core.apply(authorize(true, AuthorizationKind::Eim), now());
        core.apply(
            Event::Command(Command::StopTransaction {
                reason: StopTransactionReason::Remote,
                id_tag: None,
            }),
            now(),
        );
        assert_eq!(core.session().phase, SessionPhase::Stopping);

        // Any pass runs the poll. The cable is deliberately left in: the C++
        // only changes state here and the session itself ends when the cable
        // comes out, so unplugging would end it for the other reason and prove
        // nothing about the poll.
        core.apply(
            Event::SupplyVoltageCurrent {
                voltage_v: 0.0,
                current_a: 0.0,
            },
            now(),
        );

        assert_eq!(core.session().phase, SessionPhase::Finished);
    }

    #[test]
    fn a_disable_stops_a_live_session_with_its_own_reason() {
        // `Charger.cpp:1765` records EVSEDisabled, which used to collapse onto
        // Remote for want of a variant.
        let (mut core, reasons) = core_recording_reasons();
        core.apply(authorize(true, AuthorizationKind::Eim), now());

        core.apply(
            disable(EnableSource::Csms, 5, EnableScope::Connector),
            now(),
        );

        let reasons_now = reasons.lock().unwrap().clone();
        assert_eq!(reasons_now, vec![StopReason::EvseDisabled]);
    }

    #[test]
    fn a_withdrawn_authorization_stops_with_the_deauthorized_reason() {
        let (mut core, reasons) = core_recording_reasons();
        core.apply(authorize(true, AuthorizationKind::Eim), now());
        core.session.phase = SessionPhase::WaitingForAuthorization;

        core.apply(Event::Command(Command::WithdrawAuthorization), now());

        let reasons_now = reasons.lock().unwrap().clone();
        assert_eq!(reasons_now, vec![StopReason::DeAuthorized]);
    }

    #[test]
    fn a_withdrawn_authorization_announces_deauthorized() {
        // The withdraw command used to be dropped on the floor.
        let mut core = core();
        core.apply(authorize(true, AuthorizationKind::Eim), now());

        let effects = core.apply(Event::Command(Command::WithdrawAuthorization), now());

        assert!(
            effects
                .iter()
                .any(|effect| published(effect, SessionEvent::Deauthorized)),
            "{effects:?}"
        );
    }

    #[test]
    fn a_withdraw_mid_charge_keeps_the_authorization() {
        // The state guard at `Charger.cpp:1665`. The per state poll is what
        // stops a charging session, not the withdraw itself.
        //
        // Driven to a real charge rather than by assigning the state it is
        // asserted against: the guard reads the charger state, so a fixture
        // that writes one would be testing the assignment.
        let mut core = charging_after_an_external_authorization();

        core.apply(Event::Command(Command::WithdrawAuthorization), now());

        assert!(core.auth().authorized());
        assert_eq!(core.path.state(), AcState::Charging);
    }

    /// Plug in, authorize, draw power. The refusal tests below all need a
    /// session that is actually charging, because a refusal that stops one is
    /// only observable against a session there is something to stop.
    fn charging_after_an_external_authorization() -> Core {
        let mut core = core_up();
        core.apply(plug_in(), now());
        core.apply(authorize(true, AuthorizationKind::Eim), now());
        let drawing = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::C)), now());
        assert!(
            published_events(&drawing).contains(&SessionEvent::ChargingStarted),
            "the vehicle must be drawing power before the refusal arrives, got {drawing:?}"
        );
        core
    }

    fn answered_the_vehicle(effects: &[Effect]) -> Vec<AuthorizationResponse> {
        effects
            .iter()
            .filter_map(|effect| match effect {
                Effect::HlcUpdate(HlcUpdate::AuthorizationResponse(response)) => Some(*response),
                _ => None,
            })
            .collect()
    }

    #[test]
    fn a_refused_authorization_leaves_a_live_charge_running() {
        // `evse/evse_managerImpl.cpp:436` calls `Charger::authorize` from the
        // accepted branch alone, so a refusal is inert with respect to the
        // charger. Handing it to the authorization state machine instead stops
        // the session under a vehicle that is already drawing power.
        let mut core = charging_after_an_external_authorization();
        let phase = core.session().phase;

        let effects = core.apply(authorize(false, AuthorizationKind::Eim), now());

        assert!(
            effects.is_empty(),
            "a refusal reaches neither the charger nor the vehicle, got {effects:?}"
        );
        assert!(
            core.auth().authorized_eim(),
            "the permission the charge runs on survives the refusal"
        );
        assert_eq!(core.session().phase, phase);
        assert!(core.session().session_active);
    }

    #[test]
    fn a_refused_contract_leaves_a_live_charge_running_too() {
        // The contract refusal is forwarded to the vehicle (`:448-454`) and
        // that is the whole of it: the same accepted-branch-only call site
        // governs both kinds, so the answer is the only effect it may have.
        let mut core = charging_after_an_external_authorization();
        let phase = core.session().phase;

        let effects = core.apply(
            Event::Command(Command::AuthorizeResponse {
                token: id_tag_for_tests("tok", true),
                status: AuthorizationStatus::Blocked,
                certificate: Some(CertificateStatus::CertificateRevoked),
                tariff: TariffMessages::default(),
                reservation_id: None,
            }),
            now(),
        );

        assert_eq!(
            answered_the_vehicle(&effects),
            vec![AuthorizationResponse {
                status: AuthorizationStatus::Blocked,
                certificate: CertificateStatus::CertificateRevoked,
            }],
            "{effects:?}"
        );
        assert_eq!(
            effects.len(),
            1,
            "answering the vehicle is all a refusal does, got {effects:?}"
        );
        assert!(core.auth().authorized_eim());
        assert_eq!(core.session().phase, phase);
        assert!(core.session().session_active);
    }

    #[test]
    fn a_refusal_before_a_grant_does_not_stop_the_grant_being_honored() {
        // The reason the C++ gives for ignoring an external identification
        // refusal (`evse/evse_managerImpl.cpp:449-450`): a successful
        // authorization may still arrive later. A refusal that had taken the
        // session down would leave the grant nothing to authorize.
        let mut core = core_up();
        core.apply(plug_in(), now());

        let refused = core.apply(authorize(false, AuthorizationKind::Eim), now());
        assert!(refused.is_empty(), "{refused:?}");

        let effects = core.apply(authorize(true, AuthorizationKind::Eim), now());

        assert!(core.auth().authorized_eim());
        assert!(
            published_events(&effects).contains(&SessionEvent::Authorized),
            "{effects:?}"
        );
        assert!(
            effects.iter().any(starts_transaction),
            "the billing record the refusal never opened opens here, got {effects:?}"
        );
    }

    #[test]
    fn a_refusal_arriving_before_any_session_starts_none() {
        // The removed branch returned early outside a session, so a refusal
        // that starts one is a way the grant path could be reached by mistake.
        let mut core = core_up();

        let effects = core.apply(authorize(false, AuthorizationKind::Eim), now());

        assert!(effects.is_empty(), "{effects:?}");
        assert!(!core.session().session_active);
        assert!(!core.auth().authorized());
    }

    #[test]
    fn a_losing_disable_neither_announces_nor_stops() {
        let mut core = core();
        core.apply(enable(EnableSource::Csms, 1, EnableScope::Connector), now());

        let effects = core.apply(
            disable(EnableSource::MobileApp, 900, EnableScope::Connector),
            now(),
        );

        assert!(
            !effects
                .iter()
                .any(|effect| matches!(effect, Effect::PublishEnableEvent { .. })),
            "a lower authority disable does not win, got {effects:?}"
        );
        assert!(core.enable_table().connector_enabled());
    }

    fn blocking_error(source: ErrorSource, raised: bool) -> Event {
        Event::Error(ErrorEvent {
            source,
            error_type: "generic/CommunicationFault".into(),
            sub_type: String::new(),
            vendor_id: "acme".into(),
            severity: Severity::Medium,
            raised,
        })
    }

    fn raised_report(effects: &[Effect]) -> Option<&ErrorReport> {
        effects.iter().find_map(|effect| match effect {
            Effect::RaiseError(report) => Some(report),
            _ => None,
        })
    }

    #[test]
    fn every_error_source_reaches_the_error_lane_through_the_core() {
        // The three sources that had no inbound route at all until the fault set
        // was wired into the core.
        for source in [ErrorSource::Evse, ErrorSource::Slac, ErrorSource::Hlc] {
            let mut core = core();
            let effects = core.apply(blocking_error(source, true), now());

            let report = raised_report(&effects)
                .unwrap_or_else(|| panic!("{source:?} raised nothing, got {effects:?}"));
            assert_eq!(report.error_type, faults::INOPERATIVE, "{source:?}");
            assert_eq!(report.severity, Severity::High, "{source:?}");
            assert_eq!(report.description, "CommunicationFault", "{source:?}");

            let cleared = core.apply(blocking_error(source, false), now());
            assert!(
                cleared
                    .iter()
                    .any(|effect| matches!(effect, Effect::ClearError(report)
                        if report.error_type == faults::INOPERATIVE)),
                "{source:?} never cleared, got {cleared:?}"
            );
        }
    }

    #[test]
    fn a_blocking_error_prevents_charging_and_a_non_blocking_one_does_not() {
        // Pinned through `Core` rather than through `Faults`, so it is the wiring
        // that is under test. `VendorWarning` is the board support driver's own
        // non blocking entry.
        let non_blocking = |raised| {
            Event::Error(ErrorEvent {
                source: ErrorSource::Bsp,
                error_type: "evse_board_support/VendorWarning".into(),
                sub_type: String::new(),
                vendor_id: String::new(),
                severity: Severity::Medium,
                raised,
            })
        };

        let mut tolerated = core();
        let effects = tolerated.apply(non_blocking(true), now());
        assert!(effects.is_empty(), "{effects:?}");

        let mut blocked = core();
        let effects = blocked.apply(blocking_error(ErrorSource::Bsp, true), now());
        assert!(raised_report(&effects).is_some(), "{effects:?}");
        assert!(
            effects.contains(&Effect::AllowPowerOn(false)),
            "the port is taken out of service, got {effects:?}"
        );
    }

    #[test]
    fn a_high_severity_cause_still_takes_the_port_out_of_service() {
        let mut core = core();
        let effects = core.apply(
            Event::Error(ErrorEvent {
                source: ErrorSource::Bsp,
                error_type: "evse_board_support/MREC8EmergencyStop".into(),
                sub_type: String::new(),
                vendor_id: String::new(),
                severity: Severity::High,
                raised: true,
            }),
            now(),
        );
        assert!(
            effects.contains(&Effect::AllowPowerOn(false)),
            "{effects:?}"
        );
    }

    #[test]
    fn a_second_report_of_the_same_cause_raises_nothing_further() {
        let mut core = core();
        core.apply(blocking_error(ErrorSource::Bsp, true), now());

        let repeat = core.apply(blocking_error(ErrorSource::Bsp, true), now());

        assert!(repeat.is_empty(), "{repeat:?}");
    }

    #[test]
    fn the_inoperative_vendor_id_setting_reaches_the_fault_set() {
        let mut off = core_with_faults(Faults::new(false));
        let effects = off.apply(blocking_error(ErrorSource::Bsp, true), now());
        assert_eq!(
            raised_report(&effects).map(|report| report.vendor_id.as_str()),
            Some(faults::DEFAULT_VENDOR_ID),
            "{effects:?}"
        );

        let mut on = core_with_faults(Faults::new(true));
        let effects = on.apply(blocking_error(ErrorSource::Bsp, true), now());
        assert_eq!(
            raised_report(&effects).map(|report| report.vendor_id.as_str()),
            Some("acme"),
            "{effects:?}"
        );
    }

    #[test]
    fn an_authorization_timeout_raises_mrec9_and_then_inoperative() {
        let mut core = core_in_session();

        let effects = core.apply(Event::Command(Command::WithdrawAuthorization), now());

        let types: Vec<&str> = effects
            .iter()
            .filter_map(|effect| match effect {
                Effect::RaiseError(report) => Some(report.error_type.as_str()),
                _ => None,
            })
            .collect();
        assert_eq!(
            types,
            vec![
                "evse_manager/MREC9AuthorizationTimeout",
                faults::INOPERATIVE
            ],
            "the error is raised before the fault set is re-examined, got {effects:?}"
        );
        let mrec9 = raised_report(&effects).expect("nothing was raised");
        assert_eq!(mrec9.severity, Severity::Medium, "{mrec9:?}");
        assert_eq!(mrec9.description, faults::MREC9_DESCRIPTION, "{mrec9:?}");
        assert!(mrec9.sub_type.is_empty(), "{mrec9:?}");

        assert!(
            effects
                .iter()
                .any(|effect| published(effect, SessionEvent::PluginTimeout)),
            "{effects:?}"
        );
        assert!(
            effects.contains(&Effect::HlcUpdate(HlcUpdate::AuthorizationResponse(
                AuthorizationResponse::TIMED_OUT
            ))),
            "{effects:?}"
        );
    }

    #[test]
    fn an_error_this_module_raises_is_attributed_to_its_own_interface() {
        // The source decides the primary cause and which ignore table applies.
        // `Evse` sorts ahead of every peer, so an error raised here leads the
        // cause list whichever of the two arrived first.
        //
        // The port's own timeout leads here because that is the reachable
        // order: `Charger::deauthorize_internal`'s guard reads `current_state`
        // (`Charger.cpp:1665`), and a blocking peer error has already routed
        // the port out of every state that guard allows, so a withdraw behind
        // one raises nothing to attribute.
        let mut core = core_in_session();
        core.apply(Event::Command(Command::WithdrawAuthorization), now());

        let effects = core.apply(blocking_error(ErrorSource::Bsp, true), now());

        let description = effects
            .iter()
            .rev()
            .find_map(|effect| match effect {
                Effect::RaiseError(report) if report.error_type == faults::INOPERATIVE => {
                    Some(report.description.as_str())
                }
                _ => None,
            })
            .unwrap_or_else(|| panic!("Inoperative was not re-raised, got {effects:?}"));
        assert_eq!(
            description, "MREC9AuthorizationTimeout, CommunicationFault",
            "{effects:?}"
        );
    }

    #[test]
    fn an_authorization_timeout_raises_no_error_when_mrec9_is_off() {
        let mut core = core_in_session_with(Auth::new(false), Faults::new(false));

        let effects = core.apply(Event::Command(Command::WithdrawAuthorization), now());

        assert!(
            raised_report(&effects).is_none(),
            "the error is gated on raise_mrec9, got {effects:?}"
        );
        assert!(
            effects
                .iter()
                .any(|effect| published(effect, SessionEvent::PluginTimeout)),
            "the session event is not gated, got {effects:?}"
        );
    }

    fn unplug() -> Event {
        Event::Bsp(BspEvent::Cp(CpEvent::A))
    }

    fn cleared_types(effects: &[Effect]) -> Vec<&str> {
        effects
            .iter()
            .filter_map(|effect| match effect {
                Effect::ClearError(report) => Some(report.error_type.as_str()),
                _ => None,
            })
            .collect()
    }

    #[test]
    fn an_unplug_clears_the_error_this_module_raised_and_leaves_the_evse_operative() {
        // The raise reached the fault set, so `Inoperative` went up with it. With
        // no clear route the port stays out of service for every later session.
        let mut core = core_in_session();
        core.apply(Event::Command(Command::WithdrawAuthorization), now());

        let effects = core.apply(unplug(), now());

        assert_eq!(
            cleared_types(&effects),
            vec![faults::MREC9_AUTHORIZATION_TIMEOUT, faults::INOPERATIVE],
            "the raise is cleared before the fault set is re-examined, got {effects:?}"
        );

        // The clear names the error the raise named, so the two cannot drift
        // apart into a clear the framework does not match to anything.
        let cleared = effects
            .iter()
            .find_map(|effect| match effect {
                Effect::ClearError(report)
                    if report.error_type == faults::MREC9_AUTHORIZATION_TIMEOUT =>
                {
                    Some(report)
                }
                _ => None,
            })
            .expect("MREC9 was not cleared");
        assert!(cleared.sub_type.is_empty(), "{cleared:?}");
        assert_eq!(cleared.severity, Severity::Medium, "{cleared:?}");

        // A fresh session is the proof the fault set is empty rather than merely
        // reported clear: the same raise has to be a change again.
        core.session.session_active = true;
        core.auth = Auth::new(true);
        let next = core.apply(Event::Command(Command::WithdrawAuthorization), now());
        assert_eq!(
            raised_report(&next).map(|report| report.error_type.as_str()),
            Some(faults::MREC9_AUTHORIZATION_TIMEOUT),
            "the second session re-raises, so the first was truly cleared, got {next:?}"
        );
    }

    #[test]
    fn a_control_pilot_reading_that_is_not_an_unplug_clears_nothing() {
        // The cable is still in and the session is still running, so the raise
        // still stands. Clearing here would drop MREC9 while the vehicle waits
        // on an authorization that never came.
        let mut core = core_in_session();
        core.apply(Event::Command(Command::WithdrawAuthorization), now());

        for cp in [CpEvent::B, CpEvent::C, CpEvent::E, CpEvent::PowerOn] {
            let effects = core.apply(Event::Bsp(BspEvent::Cp(cp)), now());
            assert!(
                cleared_types(&effects).is_empty(),
                "{cp:?} is not an unplug, got {effects:?}"
            );
        }
    }

    #[test]
    fn an_unplug_does_not_clear_a_peer_raised_error() {
        // The peer still holds the fault. Clearing it here would report an
        // isolation monitor fault gone while the monitor still has it.
        let mut core = core_in_session();
        core.apply(blocking_error(ErrorSource::IsolationMonitor, true), now());

        let effects = core.apply(unplug(), now());

        assert!(
            cleared_types(&effects).is_empty(),
            "neither the peer error nor Inoperative may be cleared, got {effects:?}"
        );
    }

    #[test]
    fn an_unplug_clears_only_this_modules_share_of_a_mixed_fault_set() {
        // The timeout first, for the reason the attribution test above gives:
        // a withdraw behind a blocking peer error is refused by the state
        // guard and raises no timeout to mix in.
        let mut core = core_in_session();
        core.apply(Event::Command(Command::WithdrawAuthorization), now());
        core.apply(blocking_error(ErrorSource::IsolationMonitor, true), now());

        let effects = core.apply(unplug(), now());

        // Inoperative is cleared and re-raised because its cause set shrank, not
        // because it went away. The peer cause is still behind it.
        assert_eq!(
            cleared_types(&effects),
            vec![faults::MREC9_AUTHORIZATION_TIMEOUT, faults::INOPERATIVE],
            "{effects:?}"
        );
        let reraised = effects
            .iter()
            .find_map(|effect| match effect {
                Effect::RaiseError(report) if report.error_type == faults::INOPERATIVE => {
                    Some(report.description.as_str())
                }
                _ => None,
            })
            .unwrap_or_else(|| panic!("Inoperative was not re-raised, got {effects:?}"));
        assert_eq!(reraised, "CommunicationFault", "{effects:?}");
    }

    #[test]
    fn a_fault_outliving_the_unplug_reasserts_the_safe_state_after_the_path_goes_idle() {
        // `Charger.cpp:2296` resets the edge its fatal error actuation fires on.
        // The unplug drives the port back to idle, which undoes that actuation,
        // so the re-assert has to come after the path has seen the unplug.
        let (mut core, _events, calls) = core_recording_path();
        core.apply(blocking_error(ErrorSource::IsolationMonitor, true), now());
        calls.lock().unwrap().clear();

        core.apply(unplug(), now());

        let calls_now = calls.lock().unwrap().clone();
        assert_eq!(
            calls_now.as_slice(),
            ["on_bsp", "to_safe_state"],
            "the safe state is re-asserted, and only after the unplug reached the path"
        );
    }

    #[test]
    fn an_unplug_with_no_fault_active_asserts_nothing() {
        let (mut core, _events, calls) = core_recording_path();
        calls.lock().unwrap().clear();

        let effects = core.apply(unplug(), now());

        let calls_now = calls.lock().unwrap().clone();
        assert_eq!(
            calls_now.as_slice(),
            ["on_bsp"],
            "a clean unplug must not drive the port to safe state"
        );
        assert!(cleared_types(&effects).is_empty(), "{effects:?}");
    }

    #[test]
    fn a_disconnected_control_pilot_clears_the_same_errors_as_a_state_a_unplug() {
        let mut core = core_in_session();
        core.apply(Event::Command(Command::WithdrawAuthorization), now());

        let effects = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::Disconnected)), now());

        assert_eq!(
            cleared_types(&effects),
            vec![faults::MREC9_AUTHORIZATION_TIMEOUT, faults::INOPERATIVE],
            "{effects:?}"
        );
    }

    #[test]
    fn the_unplug_clear_is_not_in_the_safety_lane() {
        let mut core = core_in_session();
        core.apply(Event::Command(Command::WithdrawAuthorization), now());

        let effects = core.apply(unplug(), now());

        for effect in &effects {
            if matches!(effect, Effect::ClearError(_)) {
                assert_ne!(
                    effect.context(),
                    Some(effect::ExecContext::Safety),
                    "a clear does not itself actuate, got {effect:?}"
                );
            }
        }
    }

    /// Every value of `SessionEvent`, written out rather than derived, so
    /// adding one fails the lists that walk it rather than passing silently.
    const EVERY_SESSION_EVENT: &[SessionEvent] = &[
        SessionEvent::Authorized,
        SessionEvent::Deauthorized,
        SessionEvent::Enabled,
        SessionEvent::Disabled,
        SessionEvent::SessionStarted,
        SessionEvent::SessionResumed,
        SessionEvent::AuthRequired,
        SessionEvent::TransactionStarted,
        SessionEvent::PrepareCharging,
        SessionEvent::ChargingStarted,
        SessionEvent::ChargingPausedEv,
        SessionEvent::ChargingPausedEvse,
        SessionEvent::SwitchingPhases,
        SessionEvent::StoppingCharging,
        SessionEvent::ChargingFinished,
        SessionEvent::TransactionFinished,
        SessionEvent::SessionFinished,
        SessionEvent::ReservationStart,
        SessionEvent::ReservationEnd,
        SessionEvent::PluginTimeout,
    ];

    /// Every value of `StopTransactionReason`, for the same reason.
    const EVERY_STOP_REASON: &[StopTransactionReason] = &[
        StopTransactionReason::EmergencyStop,
        StopTransactionReason::EvDisconnected,
        StopTransactionReason::HardReset,
        StopTransactionReason::Local,
        StopTransactionReason::Other,
        StopTransactionReason::PowerLoss,
        StopTransactionReason::Reboot,
        StopTransactionReason::Remote,
        StopTransactionReason::SoftReset,
        StopTransactionReason::UnlockCommand,
        StopTransactionReason::DeAuthorized,
        StopTransactionReason::EnergyLimitReached,
        StopTransactionReason::GroundFault,
        StopTransactionReason::LocalOutOfCredit,
        StopTransactionReason::MasterPass,
        StopTransactionReason::OvercurrentFault,
        StopTransactionReason::PowerQuality,
        StopTransactionReason::SocLimitReached,
        StopTransactionReason::StoppedByEv,
        StopTransactionReason::TimeLimitReached,
        StopTransactionReason::Timeout,
        StopTransactionReason::ReqEnergyTransferRejected,
        StopTransactionReason::EvseDisabled,
    ];

    /// True when this effect is the named session event, whatever identity it
    /// carries.
    fn published(effect: &Effect, event: SessionEvent) -> bool {
        matches!(effect, Effect::PublishSessionEvent(report) if report.event == event)
    }

    /// Every `PublishSessionEvent` in the order it was emitted.
    fn reports(effects: &[Effect]) -> Vec<SessionEventReport> {
        effects
            .iter()
            .filter_map(|effect| match effect {
                Effect::PublishSessionEvent(report) => Some(report.clone()),
                _ => None,
            })
            .collect()
    }

    fn report_for(effects: &[Effect], event: SessionEvent) -> SessionEventReport {
        reports(effects)
            .into_iter()
            .find(|report| report.event == event)
            .unwrap_or_else(|| panic!("no {event:?} was published"))
    }

    /// Every `Effect::SessionLog` in the order it was emitted, rendered as the
    /// pair a transcript reader sees.
    fn transcript(effects: &[Effect]) -> Vec<String> {
        effects
            .iter()
            .filter_map(|effect| match effect {
                Effect::SessionLog(SessionLogEffect::Start { session_uuid }) => {
                    Some(format!("START {session_uuid}"))
                }
                Effect::SessionLog(SessionLogEffect::Stop) => Some("STOP".to_string()),
                Effect::SessionLog(SessionLogEffect::Record { origin, msg, .. }) => {
                    Some(format!("{} {msg}", origin.as_str()))
                }
                _ => None,
            })
            .collect()
    }

    fn position_of(effects: &[Effect], predicate: impl Fn(&Effect) -> bool) -> usize {
        effects
            .iter()
            .position(predicate)
            .unwrap_or_else(|| panic!("effect not emitted, got {effects:?}"))
    }

    #[test]
    fn the_transcript_opens_before_the_start_reaches_the_wire() {
        // `evse/evse_managerImpl.cpp:169-188` calls `startSession` first, then
        // writes the started line, then publishes. The order is load bearing:
        // it is what lets the publish carry the directory the start opened as
        // `session_started.logging_path`, since the two share one serial lane.
        let mut core = core();
        let effects = core
            .start_session(StartSessionReason::Authorized)
            .expect("a healthy source mints");

        let opened = position_of(&effects, |effect| {
            matches!(effect, Effect::SessionLog(SessionLogEffect::Start { .. }))
        });
        let announced = position_of(&effects, |effect| {
            published(effect, SessionEvent::SessionStarted)
        });
        assert!(
            opened < announced,
            "the transcript opens before the announcement, got {effects:?}"
        );
        assert_eq!(
            transcript(&effects),
            vec![
                format!("START {}", core.session().id.clone().unwrap()),
                "EVSE Session Started: Authorized".to_string(),
            ],
            "the start names the session and the reason the wire names"
        );
    }

    #[test]
    fn the_transcript_names_the_other_start_reason_too() {
        let mut core = core();
        let effects = core
            .start_session(StartSessionReason::EvConnected)
            .expect("a healthy source mints");
        assert!(
            transcript(&effects).contains(&"EVSE Session Started: EVConnected".to_string()),
            "got {:?}",
            transcript(&effects)
        );
    }

    #[test]
    fn the_finished_line_is_written_before_the_transcript_closes() {
        // `evse/evse_managerImpl.cpp:328-329`: the line, then `stopSession`,
        // which is what puts `Session Finished` inside the file rather than
        // after its last record.
        let mut core = core();
        core.start_session(StartSessionReason::Authorized)
            .expect("a healthy source mints");

        let effects = core.finish_session();

        assert_eq!(
            transcript(&effects),
            vec!["EVSE Session Finished".to_string(), "STOP".to_string()]
        );
        let written = position_of(&effects, |effect| {
            matches!(effect, Effect::SessionLog(SessionLogEffect::Record { .. }))
        });
        let closed = position_of(&effects, |effect| {
            matches!(effect, Effect::SessionLog(SessionLogEffect::Stop))
        });
        assert!(written < closed, "got {effects:?}");
    }

    /// Only the two the C++ brackets its log with reach the transcript from
    /// here. The rest are announcements, and the C++ writes no session log line
    /// for any of them at this site.
    ///
    /// Driven over every session event rather than over a list of nine, which
    /// is what it used to be: a new event arriving with a line nobody chose for
    /// it would close the transcript early, and a sample cannot see that.
    #[test]
    fn only_the_two_bracketing_events_owe_the_transcript_anything() {
        let core = core();

        let bracketed: Vec<SessionEvent> = EVERY_SESSION_EVENT
            .iter()
            .copied()
            .filter(|event| !core.session_log_bracket(*event).is_empty())
            .collect();

        assert_eq!(
            bracketed,
            vec![SessionEvent::SessionStarted, SessionEvent::SessionFinished]
        );
    }

    #[test]
    fn a_plug_in_writes_the_transition_the_cpp_writes_and_writes_it_after_the_start() {
        // `Charger.cpp:166` names both ends of the edge with the C++ spellings,
        // and the line lands after the transcript is open, exactly as the C++
        // second pass of `run_state_machine` does: the `Idle` entry starts the
        // session and the pass that enters `Wait for Auth` logs the edge.
        let mut core = core_up();

        let effects = core.apply(plug_in(), now());

        let lines = transcript(&effects);
        let started = lines
            .iter()
            .position(|line| line.starts_with("START "))
            .unwrap_or_else(|| panic!("the transcript must open, got {lines:?}"));
        let crossed = lines
            .iter()
            .position(|line| line == "EVSE Charger state: Idle->Wait for Auth")
            .unwrap_or_else(|| panic!("the edge must be named, got {lines:?}"));
        assert!(started < crossed, "got {lines:?}");
    }

    #[test]
    fn the_transition_line_names_the_state_the_transcript_last_saw() {
        // The memory is what makes the line a pair rather than a destination,
        // so a second pass names the first pass's destination as its origin.
        let mut core = core_up();
        core.apply(plug_in(), now());

        let effects = core.apply(unplug(), now());

        let lines = transcript(&effects);
        assert!(
            lines
                .iter()
                .any(|line| line.starts_with("EVSE Charger state: Wait for Auth->")),
            "the edge leaves the state the previous pass entered, got {lines:?}"
        );
    }

    #[test]
    fn crossing_the_state_already_named_writes_no_line() {
        // `Charger.cpp:162`'s `initialize_state` guard. A path that reports the
        // state it is already in is not an edge.
        let mut core = core_up();
        core.apply(plug_in(), now());
        let settled = core.logged_state;

        assert!(
            core.log_state_transitions(&[settled]).is_empty(),
            "a repeat is not an edge"
        );
    }

    #[test]
    fn a_pass_crossing_several_states_writes_a_line_for_each_in_order() {
        let mut core = core_up();

        let effects = core.log_state_transitions(&[
            AcState::WaitingForAuthentication,
            AcState::Charging,
            AcState::Finished,
        ]);

        assert_eq!(
            transcript(&effects),
            vec![
                "EVSE Charger state: Idle->Wait for Auth".to_string(),
                "EVSE Charger state: Wait for Auth->Charging".to_string(),
                "EVSE Charger state: Charging->Finished".to_string(),
            ]
        );
    }

    #[test]
    fn an_iso_request_is_recorded_on_the_car_side_and_a_response_on_the_evse_side() {
        // `EvseManager.cpp:1884-1888`. The EV sends requests and the SECC
        // answers them, so a `Req` is a car message however the comment above
        // that line reads.
        let mut core = core();
        let request = core.apply(
            Event::V2gMessage(Box::new(crate::core::event::V2gMessage {
                id: "SessionSetupReq".to_string(),
                ..Default::default()
            })),
            now(),
        );
        assert_eq!(transcript(&request), vec!["CAR V2G SessionSetupReq"]);

        let response = core.apply(
            Event::V2gMessage(Box::new(crate::core::event::V2gMessage {
                id: "SessionSetupRes".to_string(),
                ..Default::default()
            })),
            now(),
        );
        assert_eq!(transcript(&response), vec!["EVSE V2G SessionSetupRes"]);
    }

    #[test]
    fn an_iso_message_carries_all_four_payload_representations() {
        let mut core = core();
        let effects = core.apply(
            Event::V2gMessage(Box::new(crate::core::event::V2gMessage {
                id: "PreChargeReq".to_string(),
                xml: "<PreChargeReq/>".to_string(),
                json: "{\"PreChargeReq\":{}}".to_string(),
                exi_hex: "809a".to_string(),
                exi_base64: "gJo=".to_string(),
            })),
            now(),
        );

        let Some(Effect::SessionLog(SessionLogEffect::Record {
            origin,
            iso15118,
            msg,
            payload,
        })) = effects.first()
        else {
            panic!("an ISO message is one record, got {effects:?}");
        };
        assert_eq!(*origin, session_log::Origin::Car);
        assert!(*iso15118, "an ISO message is logged as one");
        assert_eq!(msg, "V2G PreChargeReq");
        let payload = payload.as_deref().expect("the payload travels");
        assert_eq!(payload.xml, "<PreChargeReq/>");
        assert_eq!(payload.json, "{\"PreChargeReq\":{}}");
        assert_eq!(payload.xml_hex, "809a");
        assert_eq!(payload.xml_base64, "gJo=");
    }

    #[test]
    fn an_iso_message_reaches_no_state_machine() {
        // The C++ handler writes one record and returns; nothing in `Charger`
        // or the stack port sees it. So the transcript record is the whole of
        // its effect, and it changes no state.
        let mut core = core_up();
        core.apply(plug_in(), now());
        let before = core.session().phase;

        let effects = core.apply(
            Event::V2gMessage(Box::new(crate::core::event::V2gMessage {
                id: "AuthorizationReq".to_string(),
                ..Default::default()
            })),
            now(),
        );

        assert_eq!(effects.len(), 1, "one record and nothing else: {effects:?}");
        assert_eq!(core.session().phase, before);
    }

    /// The car side power meter's measurable floors as they reach the
    /// transcript, and what an AC port does with them.
    ///
    /// These tests pinned a GAP until the merge was ported. Their previous
    /// shape is worth keeping in view, because the record of what changed is
    /// the point of a declared divergence:
    ///
    /// - The two transcript tests asserted a line carrying
    ///   `NOT applied to any limit`. That clause existed so an operator
    ///   comparing this transcript with the C++'s could tell the ported half
    ///   from the unported one. It is gone, and the line is the C++ line.
    /// - `the_record_is_the_whole_effect_of_the_report` and
    ///   `a_floor_above_the_standing_limit_still_changes_nothing` asserted that
    ///   exactly one effect came out. Both are still here, still asserting one
    ///   effect, and **neither ever pinned the drop**: both drive `core_up`,
    ///   which holds an AC path and an AC charge mode, and the C++ push is
    ///   gated on `hlc_enabled and config.charge_mode == "DC"`. So on the port
    ///   they drive, one effect is what the C++ produces too, before and after
    ///   the merge. They are renamed to say what they do pin, which is the
    ///   charge mode gate, and the DC behaviour they were meant to cover is
    ///   driven in `what_a_car_side_meter_floor_narrows`.
    mod powermeter_capabilities {
        use super::*;
        use crate::core::event::PowermeterCapabilities;

        fn report(import: Option<f64>, export: Option<f64>) -> Event {
            Event::PowermeterCapabilities(PowermeterCapabilities {
                min_import_current_a: import,
                min_export_current_a: export,
            })
        }

        #[test]
        fn both_floors_reach_the_transcript() {
            // `EvseManager::update_powermeter_capabilities` writes this line
            // and this port now writes the same one, so the two transcripts
            // can be compared character for character.
            let mut core = core();

            let effects = core.apply(report(Some(2.0), Some(3.5)), now());

            assert_eq!(
                transcript(&effects),
                vec![
                    "EVSE Received power meter capabilities: \
                     min_import_current_A (charging): 2, min_export_current_A \
                     (discharging): 3.5"
                        .to_string()
                ]
            );
        }

        #[test]
        fn an_absent_floor_is_written_as_not_available() {
            // The C++ prints `N/A` for an absent optional rather than dropping
            // the field, so a reader can tell "no floor" from "zero amps".
            let mut core = core();

            let effects = core.apply(report(None, None), now());

            assert_eq!(
                transcript(&effects),
                vec![
                    "EVSE Received power meter capabilities: \
                     min_import_current_A (charging): N/A, min_export_current_A \
                     (discharging): N/A"
                        .to_string()
                ]
            );
        }

        /// A second report carrying the same figures writes nothing at all.
        ///
        /// `update_powermeter_capabilities` compares against the stored report
        /// and returns before its session log line, so a meter that republishes
        /// its capabilities on a timer does not fill the transcript. The port
        /// wrote a line per report while the merge was unported, because it
        /// stored nothing to compare against.
        #[test]
        fn a_report_the_port_already_holds_writes_nothing() {
            let mut core = core();
            assert_eq!(core.apply(report(Some(2.0), None), now()).len(), 1);

            let again = core.apply(report(Some(2.0), None), now());

            assert!(again.is_empty(), "got {again:?}");
            // And a figure that moved is a new report again.
            assert_eq!(core.apply(report(Some(2.5), None), now()).len(), 1);
        }

        /// An AC port records the floors and tells the vehicle nothing.
        ///
        /// The C++ gate, `if (hlc_enabled and config.charge_mode == "DC")`
        /// around the push. The store is not gated, so the floor is held and
        /// would apply if this port ever presented DC.
        #[test]
        fn an_ac_port_records_the_report_and_offers_the_vehicle_nothing() {
            let mut core = core_up();
            core.apply(plug_in(), now());
            let before = core.session().phase;

            let effects = core.apply(report(Some(6.0), Some(6.0)), now());

            assert_eq!(
                effects.len(),
                1,
                "the report produces one transcript record and nothing else: {effects:?}"
            );
            assert!(
                matches!(
                    effects.first(),
                    Some(Effect::SessionLog(SessionLogEffect::Record { .. }))
                ),
                "the one effect is the transcript record: {effects:?}"
            );
            assert_eq!(core.session().phase, before, "no state machine reads it");
        }

        /// The cell that carries the charge mode gate, which is the one thing
        /// left in `HlcPort::note_floors` now that the store, the comparison
        /// and the transcript line have moved up to `Core::car_side_meter`.
        ///
        /// The two tests above drive `core_up()`, a basic AC port. That port
        /// has no high level communication port at all, so what they pin is the
        /// absence and not the gate: dropping `runs_dc_limits` from
        /// `note_floors` would leave both of them green while an AC port with a
        /// stack began pushing DC capability messages at its vehicle. This is
        /// the same deployment with the stack wired.
        #[test]
        fn an_ac_port_with_a_stack_records_the_report_and_still_tells_the_vehicle_nothing() {
            let mut core = ac_hlc_core();
            core.apply(Event::Startup, now());
            core.apply(plug_in(), now());

            let effects = core.apply(report(Some(6.0), Some(6.0)), now());

            assert_eq!(
                transcript(&effects).len(),
                1,
                "the report is recorded once: {effects:?}"
            );
            for effect in &effects {
                assert!(
                    !matches!(effect, Effect::HlcUpdate(_)),
                    "an AC port told its vehicle about a DC capability: {effect:?}"
                );
            }
        }

        /// And an implausible floor on an AC port is still only recorded, so
        /// the gate is not a function of how large the figure is.
        #[test]
        fn an_ac_port_offers_nothing_however_high_the_floor() {
            let mut core = core_up();
            core.apply(plug_in(), now());

            let effects = core.apply(report(Some(1000.0), Some(1000.0)), now());

            assert_eq!(
                effects.len(),
                1,
                "an implausible floor is still only recorded: {effects:?}"
            );
        }
    }

    /// What a car side power meter's measurable floor narrows, driven through
    /// the core.
    ///
    /// `core::powermeter_limits` owns the arithmetic and `hlc::dc_limits` owns
    /// the seam. What is asserted here is the behaviour neither can see: that a
    /// meter which cannot measure below some current raises the minimum the
    /// vehicle is offered, in the message the stack turns into
    /// `EVSEMinimumCurrentLimit` and in the limit set the power path clamps
    /// against.
    ///
    /// Every test here reads a **minimum**, because that is the only thing the
    /// merge can move, and reads it after a floor that is either above or below
    /// the standing one, because a floor is not a clamp in both directions and
    /// a test on one side alone cannot tell the two apart.
    mod what_a_car_side_meter_floor_narrows {
        use super::*;
        use crate::core::event::PowermeterCapabilities;
        use crate::core::hlc::dc_limits::MinimumLimits;

        /// A bidirectional supply that offers four amperes in each direction as
        /// its minimum, so a meter floor above four is visible and a floor
        /// below four is visibly ignored. The voltage minima are named too,
        /// because the minimum power the stack is told is the product.
        fn supply() -> PowerSupplyCapabilities {
            PowerSupplyCapabilities {
                bidirectional: true,
                min_export_voltage_v: 150.0,
                max_export_current_a: 400.0,
                min_export_current_a: 4.0,
                max_export_power_w: 150_000.0,
                min_import_voltage_v: Some(150.0),
                max_import_current_a: Some(400.0),
                min_import_current_a: Some(4.0),
                max_import_power_w: Some(100_000.0),
                ..PowerSupplyCapabilities::sane_default()
            }
        }

        /// Filled by name: both fields are `Option<f64>`, the two directions
        /// cross on the way to the supply's limits, and a positional literal
        /// would compile with them swapped.
        fn meter(import: Option<f64>, export: Option<f64>) -> Event {
            Event::PowermeterCapabilities(PowermeterCapabilities {
                min_import_current_a: import,
                min_export_current_a: export,
            })
        }

        fn board_support() -> Event {
            Event::Bsp(BspEvent::Capabilities(HardwareCapabilities {
                max_phase_count_import: 3,
                min_phase_count_import: 1,
                ..HardwareCapabilities::default()
            }))
        }

        /// An enforced limits answer carrying a site allowance far above
        /// anything the supply can deliver, so the figures under test are the
        /// supply's own and not the site's share.
        fn enforced() -> Event {
            Event::EnforcedLimits(Box::new(crate::core::energy::enforce::EnforcedLimits {
                uuid: "evse_manager".to_owned(),
                valid_for_s: 60,
                schedule: Vec::new(),
                limits_root_side: crate::core::energy::enforce::LimitsRes {
                    ac_max_current_a: Some(
                        crate::core::energy::flow_request::NumberWithSource::new(32.0, "test"),
                    ),
                    total_power_w: Some(crate::core::energy::flow_request::NumberWithSource::new(
                        300_000.0, "test",
                    )),
                    ac_max_phase_count: None,
                },
            }))
        }

        fn flow_request_tick(generation: u64) -> Event {
            Event::Timer {
                id: crate::core::energy::TIMER_ENERGY_FLOW_REQUEST,
                generation,
            }
        }

        fn reporting_core() -> Core {
            let mut core = dc_core();
            core.apply(Event::Startup, now());
            core.apply(Event::PowerSupplyCapabilities(Box::new(supply())), now());
            core
        }

        fn recording_core() -> (Core, RecordedEvents) {
            let (mut core, events, _calls) = dc_core_recording_path();
            core.apply(Event::Startup, now());
            core.apply(board_support(), now());
            core.apply(Event::PowerSupplyCapabilities(Box::new(supply())), now());
            (core, events)
        }

        /// The minimum limit set last sent to the ISO 15118 stack, which is
        /// what the vehicle is offered as its lowest usable current.
        fn offered_minimum(effects: &[Effect]) -> Option<MinimumLimits> {
            effects.iter().rev().find_map(|effect| match effect {
                Effect::HlcUpdate(HlcUpdate::DcMinimumLimits(limits)) => Some(*limits),
                _ => None,
            })
        }

        /// The capability report last forwarded to the stack, if any.
        fn forwarded(effects: &[Effect]) -> Option<PowerSupplyCapabilities> {
            effects.iter().rev().find_map(|effect| match effect {
                Effect::HlcUpdate(HlcUpdate::PowerSupplyCapabilities(caps)) => Some(**caps),
                _ => None,
            })
        }

        /// The minimum limit set the power path was last told to clamp the
        /// vehicle's request against, which the enforced limits handler
        /// produces from the report the energy tree holds. Every reader here
        /// drives a pass first: the pass is part of the property, because a
        /// floor that never reached the energy tree would narrow the wire
        /// message and leave the clamp wide.
        fn clamped_minimum(events: &RecordedEvents) -> Option<MinimumLimits> {
            events
                .lock()
                .unwrap()
                .iter()
                .rev()
                .find_map(|event| match event {
                    PathEvent::DcEnforcedLimits { minimum, .. } => Some(*minimum),
                    _ => None,
                })
        }

        /// THE DONE WHEN. A meter that cannot measure below six amperes in the
        /// charging direction raises the minimum current the vehicle is
        /// offered from the supply's four to six, so the offer NARROWS: the
        /// four and five ampere operating points are withdrawn.
        #[test]
        fn a_meter_floor_above_the_supply_minimum_narrows_the_offer() {
            let mut core = reporting_core();

            let effects = core.apply(meter(Some(6.0), None), now());

            assert_eq!(
                offered_minimum(&effects),
                Some(MinimumLimits {
                    minimum_current_a: 6.0,
                    minimum_voltage_v: 150.0,
                    // The product of the two, so the power floor rises with
                    // the current floor rather than staying at the supply's.
                    minimum_power_w: 900.0,
                    minimum_discharge_current_a: None,
                    minimum_discharge_power_w: None,
                }),
                "got {effects:?}"
            );
        }

        /// The direction, proved rather than reasoned about. A meter that can
        /// measure down to one ampere does NOT widen the offer to one ampere:
        /// the supply's four stands. A clamp in both directions would answer
        /// one here, and the same fixture answers six above.
        #[test]
        fn a_meter_floor_below_the_supply_minimum_widens_nothing() {
            let mut core = reporting_core();

            let effects = core.apply(meter(Some(1.0), None), now());

            assert_eq!(
                offered_minimum(&effects).map(|limits| limits.minimum_current_a),
                Some(4.0),
                "got {effects:?}"
            );
        }

        /// And a floor that restricts nothing forwards no capability report,
        /// because the report the stack is told did not move. The two derived
        /// emissions still go out, which is the C++ shape: only the forward
        /// carries a change gate.
        #[test]
        fn a_meter_that_restricts_nothing_forwards_no_capability_report() {
            let mut core = reporting_core();

            let effects = core.apply(meter(Some(1.0), Some(1.0)), now());

            assert_eq!(forwarded(&effects), None, "got {effects:?}");
            assert!(offered_minimum(&effects).is_some(), "got {effects:?}");
        }

        /// A restricting floor does forward one, carrying the raised minimum,
        /// so the stack's copy of the supply agrees with the minimum limit
        /// message it arrives beside.
        #[test]
        fn a_restricting_floor_forwards_the_raised_report() {
            let mut core = reporting_core();

            let effects = core.apply(meter(Some(6.0), Some(7.0)), now());

            let sent = forwarded(&effects).expect("a capability report: {effects:?}");
            assert_eq!(sent.min_export_current_a, 6.0);
            assert_eq!(sent.min_import_current_a, Some(7.0));
            assert_eq!(
                offered_minimum(&effects).map(|limits| limits.minimum_current_a),
                Some(6.0),
            );
        }

        /// The discharging direction, which the capability report's minimum
        /// message cannot carry: that message is the three field export set.
        /// It reaches the vehicle through the enforced limits handler's five
        /// field set instead, so the floor has to have reached the energy
        /// tree's copy of the report.
        #[test]
        fn the_discharging_floor_narrows_the_discharge_clamp() {
            let (mut core, events) = recording_core();
            core.apply(enforced(), now());
            assert_eq!(
                clamped_minimum(&events).and_then(|limits| limits.minimum_discharge_current_a),
                Some(4.0),
                "the supply's own discharge minimum first"
            );

            core.apply(meter(None, Some(9.0)), now());
            core.apply(enforced(), now());

            let minimum = clamped_minimum(&events).expect("a limit set");
            assert_eq!(minimum.minimum_discharge_current_a, Some(9.0));
            assert_eq!(
                minimum.minimum_discharge_power_w,
                Some(1350.0),
                "the product of the raised current and the import voltage minimum"
            );
            // And the charging direction is untouched by an export floor.
            assert_eq!(minimum.minimum_current_a, 4.0);
        }

        /// The charging direction end to end through the same path, so the
        /// floor is not only on the wire message but also in what the port
        /// clamps the vehicle's request against.
        #[test]
        fn the_charging_floor_narrows_the_charging_clamp() {
            let (mut core, events) = recording_core();
            core.apply(enforced(), now());
            assert_eq!(
                clamped_minimum(&events).map(|limits| limits.minimum_current_a),
                Some(4.0)
            );

            core.apply(meter(Some(6.0), None), now());
            core.apply(enforced(), now());

            assert_eq!(
                clamped_minimum(&events).map(|limits| limits.minimum_current_a),
                Some(6.0)
            );
        }

        /// A floor that arrives **before** the supply has reported anything is
        /// still applied when the report lands, because it is stored beside the
        /// report rather than folded into it. The reverse order is what every
        /// other test here drives.
        #[test]
        fn a_floor_that_arrives_before_the_first_report_narrows_it_when_it_lands() {
            let mut core = dc_core();
            core.apply(Event::Startup, now());

            core.apply(meter(Some(6.0), None), now());
            let effects = core.apply(Event::PowerSupplyCapabilities(Box::new(supply())), now());

            assert_eq!(
                offered_minimum(&effects).map(|limits| limits.minimum_current_a),
                Some(6.0),
                "got {effects:?}"
            );
            assert_eq!(
                forwarded(&effects).map(|caps| caps.min_export_current_a),
                Some(6.0),
                "and the forward carries it too: {effects:?}"
            );
        }

        /// The same ordering as the test above, read on the clamp instead of on
        /// the wire message.
        ///
        /// It is a separate test because it is the only cell that drives the
        /// **arrival** seam: with the floor already standing, the report
        /// reaching the energy tree is the one write of the merged value, and
        /// nothing retells it afterwards. Handing that site the unmerged report
        /// passes every other test here, because a meter report arriving second
        /// retells the tree on its way through.
        #[test]
        fn a_floor_that_arrived_first_reaches_the_clamp_when_the_report_lands() {
            let (mut core, events, _calls) = dc_core_recording_path();
            core.apply(Event::Startup, now());
            core.apply(board_support(), now());

            core.apply(meter(Some(6.0), Some(9.0)), now());
            core.apply(Event::PowerSupplyCapabilities(Box::new(supply())), now());
            core.apply(enforced(), now());

            let minimum = clamped_minimum(&events).expect("a limit set");
            assert_eq!(minimum.minimum_current_a, 6.0);
            assert_eq!(minimum.minimum_discharge_current_a, Some(9.0));
        }

        /// A port with no car side meter at all is untouched: the merge is the
        /// identity, so the supply's own minimum is what the vehicle is
        /// offered. The baseline every test above moves away from, asserted so
        /// that a merge which silently raised a floor of its own would fail.
        #[test]
        fn a_port_with_no_meter_offers_the_supplys_own_minimum() {
            let mut core = dc_core();
            core.apply(Event::Startup, now());

            let effects = core.apply(Event::PowerSupplyCapabilities(Box::new(supply())), now());

            assert_eq!(
                offered_minimum(&effects).map(|limits| limits.minimum_current_a),
                Some(4.0),
                "got {effects:?}"
            );
            assert_eq!(
                forwarded(&effects).map(|caps| caps.min_import_current_a),
                Some(Some(4.0)),
                "got {effects:?}"
            );
        }

        /// A meter that withdraws a floor it previously named releases the
        /// offer back to the supply's own minimum.
        ///
        /// The floors are stored beside the report rather than folded into it,
        /// which is what makes this possible at all: a port that had merged
        /// the figure into the stored report would have no way back. The same
        /// property `core::derate` relies on for a relaxed derate.
        #[test]
        fn a_withdrawn_floor_releases_the_offer_again() {
            let mut core = reporting_core();
            let raised = core.apply(meter(Some(6.0), None), now());
            assert_eq!(
                offered_minimum(&raised).map(|limits| limits.minimum_current_a),
                Some(6.0)
            );

            let released = core.apply(meter(None, None), now());

            assert_eq!(
                offered_minimum(&released).map(|limits| limits.minimum_current_a),
                Some(4.0),
                "got {released:?}"
            );
        }

        /// One report carrying a floor that restricts and a floor that does
        /// not, so the two halves are driven together and against each other.
        /// `core::powermeter_limits` asserts their independence on the pure
        /// function; this is the same fact where the port acts on it.
        #[test]
        fn a_report_that_restricts_one_direction_only_moves_that_one() {
            let (mut core, events) = recording_core();

            core.apply(meter(Some(6.0), Some(1.0)), now());
            core.apply(enforced(), now());

            let minimum = clamped_minimum(&events).expect("a limit set");
            assert_eq!(minimum.minimum_current_a, 6.0, "the charging floor bites");
            assert_eq!(
                minimum.minimum_discharge_current_a,
                Some(4.0),
                "and the discharging one does not"
            );
        }

        /// A meter republishing the report the port already holds tells the
        /// vehicle nothing, which is the C++ early return. Without the dedup a
        /// meter on a timer would re-send the minimum limits several times a
        /// minute for no change.
        #[test]
        fn a_repeated_report_tells_the_vehicle_nothing() {
            let mut core = reporting_core();
            assert!(!core.apply(meter(Some(6.0), None), now()).is_empty());

            let again = core.apply(meter(Some(6.0), None), now());

            assert!(again.is_empty(), "got {again:?}");
        }

        /// A floor above what the supply can deliver at all collapses the band
        /// to the ceiling rather than inverting it, and the ceiling it is
        /// clamped against on the path is the **derated** one. The cross
        /// product cell where both narrowings are live at once.
        #[test]
        fn a_floor_above_a_derated_ceiling_is_clamped_at_that_ceiling() {
            let (mut core, events) = recording_core();
            core.apply(
                Event::Command(Command::SetExternalDerating(
                    crate::core::derate::ExternalDerating {
                        max_export_current_a: Some(20.0),
                        ..crate::core::derate::ExternalDerating::default()
                    },
                )),
                now(),
            );

            core.apply(meter(Some(50.0), None), now());
            core.apply(enforced(), now());

            let minimum = clamped_minimum(&events).expect("a limit set");
            assert_eq!(
                minimum.minimum_current_a, 20.0,
                "the floor is clamped down to the derated maximum, not left above it"
            );
        }

        /// And the same floor is clamped the same way on the wire message,
        /// because the report this port announces is the derated one:
        /// `push_powersupply_capabilities_to_hlc` fills the minimum limits from
        /// `apply_powermeter_limits(apply_external_derating(raw))`
        /// (`EvseManager.cpp:2798` into `:2812-2817`).
        ///
        /// This asserted the opposite, that the announced report carried no
        /// derating, and called it `core::derate`'s divergence rather than the
        /// merge's. It was one divergence rather than two: the port forwarded
        /// an underated report, so its minima were merged against underated
        /// maxima, and the vehicle was offered a floor above the ceiling the
        /// supply would actually hold.
        #[test]
        fn the_announced_floor_is_clamped_to_the_derated_maximum_too() {
            let mut core = reporting_core();
            core.apply(
                Event::Command(Command::SetExternalDerating(
                    crate::core::derate::ExternalDerating {
                        max_export_current_a: Some(20.0),
                        ..crate::core::derate::ExternalDerating::default()
                    },
                )),
                now(),
            );

            let effects = core.apply(meter(Some(50.0), None), now());

            assert_eq!(
                offered_minimum(&effects).map(|limits| limits.minimum_current_a),
                Some(20.0),
                "got {effects:?}"
            );
        }

        /// The claim `HlcPort::derated_supply_capabilities` makes: handing the
        /// energy tree the merged report rather than the unmerged one is
        /// invisible to its other readers, because the merge moves no field
        /// they read. The C++ hands those readers
        /// `get_powersupply_capabilities()` instead, so without this the two
        /// could differ and nothing would say so.
        #[test]
        fn a_meter_floor_does_not_move_what_the_site_is_asked_for() {
            let mut core = reporting_core();
            core.apply(board_support(), now());
            let before = core.apply(flow_request_tick(1), now());

            core.apply(meter(Some(6.0), Some(9.0)), now());
            let after = core.apply(flow_request_tick(2), now());

            let request = |effects: &[Effect]| {
                effects
                    .iter()
                    .find_map(|effect| match effect {
                        Effect::PublishEnergyFlowRequest(request) => Some(request.clone()),
                        _ => None,
                    })
                    .expect("an energy flow request")
            };
            assert_eq!(request(&before), request(&after));
        }
    }

    #[test]
    fn the_transition_line_leads_the_duties_of_the_same_pass() {
        // The order `apply` puts these two in: `log_state_transitions` ahead of
        // `discharge_session_duties`, because `Charger.cpp:166` writes the
        // transition line at the head of a state machine pass, before the
        // entered state's body announces anything.
        //
        // Every other transcript test here reads one kind of line, so moving
        // the transition line behind the duties changed nothing any of them
        // could see. This drives the exact sequence the ordering exists for and
        // asserts it whole: an unplug from `Wait for Auth` crosses `Finished`
        // and then `Idle` in ONE pass, and the `Idle` entry is what raises
        // `SessionFinished`. So `Charger state: Finished->Idle` has to stand
        // above the `Session Finished` line, and the `STOP` that closes the
        // file has to come last of all.
        let mut core = core_up();
        core.apply(plug_in(), now());

        let effects = core.apply(unplug(), now());

        assert_eq!(
            transcript(&effects),
            vec![
                "EVSE Charger state: Wait for Auth->Finished".to_string(),
                "EVSE Charger state: Finished->Idle".to_string(),
                "EVSE Session Finished".to_string(),
                "STOP".to_string(),
            ]
        );
        // And the announcement trails the transcript, so the boundary can read
        // the closed directory back on the shared serial lane.
        assert!(
            position_of(&effects, |effect| matches!(
                effect,
                Effect::SessionLog(SessionLogEffect::Stop)
            )) < position_of(&effects, |effect| published(
                effect,
                SessionEvent::SessionFinished
            )),
            "got {effects:?}"
        );
    }

    #[test]
    fn the_first_transition_of_a_boot_names_the_startup_state_the_cpp_has_not_got() {
        // `logged_state` is initialised from the path rather than from a
        // literal, and this is the only line that can tell. The C++ value
        // initialises both state fields to `Idle` (`Charger.cpp:54`, `:69`) and
        // has no startup state; the port does, and the path is in it until the
        // boot pass leaves it.
        //
        // Nothing drove this. `crossing_the_state_already_named_writes_no_line`
        // and `a_pass_crossing_several_states_writes_a_line_for_each_in_order`
        // both start from `core_up()`, which has already spent the boot edge,
        // so an initialiser hard coded to `Idle` looked identical to them: it
        // only swallows the FIRST line of the transcript. A mutant that hard
        // coded `AcState::Startup` was caught by clippy alone, for leaving the
        // read of the path unused, which was false comfort -- the same mutant
        // written to keep that read survived every test in this file.
        let mut core = core();
        assert_eq!(core.logged_state, AcState::Startup);

        let effects = core.apply(Event::Startup, now());

        assert_eq!(
            transcript(&effects),
            vec!["EVSE Charger state: Startup->Idle".to_string()],
            "a boot names the edge out of the startup state"
        );
        assert_eq!(core.logged_state, AcState::Idle);
    }

    /// Why the third drain is safe, and the premise that makes it safe.
    ///
    /// `apply` drains `take_entered_states` once and says so: "One drain
    /// feeding two consumers, because draining twice would report each crossing
    /// to only one of them." `stop` then adds a drain that feeds exactly one
    /// consumer, the transcript. Anything it takes is therefore hidden from
    /// `request_energy_on_state_edge`, which is the pass's other consumer.
    ///
    /// That is harmless only because of the premise this test pins: neither
    /// path enters `WaitingForAuthentication` or `Finished` from `on_stop`, and
    /// those two are the only states the energy edge watches
    /// (`energyImpl.cpp:130-137`). AC runs `IecInput::StopRequested`, which
    /// sets `StoppingCharging` (`path/iec.rs`); DC enters `StoppingCharging`
    /// from `PrepareCharging` or `Charging` and otherwise moves nothing
    /// (`path/dc.rs`, `Charger.cpp:694`/`:792`).
    ///
    /// Mutation evidence: adding
    /// `effects.extend(self.request_energy_on_state_edge(&crossed))` to `stop`
    /// survives every test in this crate, and it survives BECAUSE of the
    /// premise -- with a stop's crossings, that call returns an empty vector.
    /// So the extra drain is a provably equivalent change today. If `on_stop`
    /// ever learns to reach either of those two states, the priority energy
    /// request on that edge would be dropped silently, with no error and no
    /// log. This test is what turns that into a failure instead.
    #[test]
    fn a_stop_crosses_no_state_the_energy_edge_watches() {
        let watched = [AcState::WaitingForAuthentication, AcState::Finished];

        let mut ac = core_up();
        ac.apply(plug_in(), now());
        ac.apply(authorize(true, AuthorizationKind::Eim), now());
        let effects = ac.apply(
            Event::Command(Command::StopTransaction {
                reason: StopTransactionReason::Local,
                id_tag: None,
            }),
            now(),
        );
        let ac_lines = transcript(&effects);
        assert_eq!(
            ac_lines,
            vec!["EVSE Charger state: PrepareCharging->StoppingCharging".to_string()],
            "an AC stop enters StoppingCharging and nothing else"
        );

        // And the drain has to happen in `stop`, not be left to the end of the
        // pass: the line leads the announcement of the state it names, which is
        // the same order `Charger.cpp` writes them in. Deleting the drain from
        // `stop` moves the line behind both publishes, and nothing in the
        // transcript alone can see that -- the duties a stop raises owe the
        // transcript no line of their own, so this is asserted against the wire.
        assert!(
            position_of(&effects, |effect| matches!(
                effect,
                Effect::SessionLog(SessionLogEffect::Record { .. })
            )) < position_of(&effects, |effect| published(
                effect,
                SessionEvent::StoppingCharging
            )),
            "the transition line leads the state it announces, got {effects:?}"
        );

        // Stated against the state names the energy edge actually matches, so
        // this fails if either the stop path or that predicate moves.
        for state in watched {
            let entered = format!("->{}", session_log::charger_state_name(state));
            assert!(
                !ac_lines.iter().any(|line| line.ends_with(&entered)),
                "a stop must not enter {state:?}, got {ac_lines:?}"
            );
        }
    }

    #[test]
    fn an_authorization_first_start_carries_a_uuid_and_its_reason() {
        // `AuthHandler.cpp:830` reads `session_started.reason` with an
        // unconditional `.value()`, so a start published without the payload
        // aborts the Auth module rather than being ignored there.
        let mut core = core();
        let effects = core.apply(authorize(true, AuthorizationKind::Eim), now());
        let started = report_for(&effects, SessionEvent::SessionStarted);
        assert!(!started.uuid.is_empty(), "the start names the session");
        assert_eq!(started.started, Some(StartSessionReason::Authorized));
        assert_eq!(
            core.session().id.as_deref(),
            Some(started.uuid.as_str()),
            "the published identity is the one the session holds"
        );
    }

    #[test]
    fn a_plug_in_start_names_the_other_reason() {
        let mut core = core();
        let effects = core
            .start_session(StartSessionReason::EvConnected)
            .expect("a healthy source mints");
        // Not `first`: the transcript opens ahead of the announcement, which
        // `the_transcript_opens_before_the_start_reaches_the_wire` pins.
        let report = report_for(&effects, SessionEvent::SessionStarted);
        assert_eq!(report.event, SessionEvent::SessionStarted);
        assert_eq!(report.started, Some(StartSessionReason::EvConnected));
        assert!(!report.uuid.is_empty());
    }

    /// A core that can mint no identity: what a module is left with when the
    /// random device is unreadable and the reserve behind it is spent.
    fn core_without_entropy() -> Core {
        let mut core = core();
        core.session_ids = SessionIds::new(
            crate::core::config::SessionIdType::Uuid,
            Box::new(crate::core::session_id::ExhaustedBytes),
        );
        core
    }

    #[test]
    fn a_plug_in_that_cannot_be_given_an_identity_starts_no_session() {
        let mut core = core_without_entropy();
        let effects = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());

        assert!(
            reports(&effects).is_empty(),
            "a refused start publishes nothing about a session, got {effects:?}"
        );
        assert_eq!(core.session().id, None, "no identity was committed");
        assert!(!core.session().session_active, "no session was opened");
        assert_eq!(
            core.session().phase,
            SessionPhase::Idle,
            "the refused start left no session phase behind"
        );
    }

    #[test]
    fn a_refused_start_surfaces_the_refusal_rather_than_stopping_the_process() {
        let mut core = core_without_entropy();
        let effects = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());

        let raised: Vec<&ErrorReport> = effects
            .iter()
            .filter_map(|effect| match effect {
                Effect::RaiseError(report) => Some(report),
                _ => None,
            })
            .collect();
        assert!(
            raised.iter().any(|report| report.error_type == INTERNAL),
            "the refusal names itself on the interface, got {effects:?}"
        );
        assert!(
            raised
                .iter()
                .any(|report| report.error_type == faults::INOPERATIVE),
            "the refusal blocks charging, got {effects:?}"
        );
    }

    #[test]
    fn a_refused_start_is_the_error_class_not_the_emergency_one() {
        // Nothing is energized at a start, so the refusal is raised with the
        // class the other start time refusal uses. `apply_fault_signals` drives
        // both classes to the same safe state today, so the severity carried on
        // the raise is the only place the choice is visible.
        let mut core = core_without_entropy();
        let effects = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
        let refusal = effects
            .iter()
            .find_map(|effect| match effect {
                Effect::RaiseError(report) if report.error_type == INTERNAL => Some(report),
                _ => None,
            })
            .unwrap_or_else(|| panic!("the refusal is raised, got {effects:?}"));
        assert_eq!(refusal.severity, Severity::Medium);
        assert_eq!(refusal.description, NO_SESSION_ID_DESCRIPTION);
    }

    #[test]
    fn a_refused_start_drives_the_port_to_safe_state() {
        let mut core = core_without_entropy();
        let effects = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
        assert!(
            effects.contains(&Effect::AllowPowerOn(false)),
            "energy is withheld, got {effects:?}"
        );
    }

    #[test]
    fn a_refused_start_does_not_strand_the_vehicle() {
        // The whole point of refusing rather than exiting: the process survives
        // to release the connector. The board support still reports state B and
        // the port still latches on it, so the guarantee is not that nothing
        // locks but that the release is armed and arrives, which a process that
        // exits can offer neither of.
        let release =
            crate::core::path::ac::timer_id(crate::core::path::iec::AcTimer::CpStateFUnlock);
        let mut core = core_without_entropy();
        let effects = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
        let armed = effects
            .iter()
            .find_map(|effect| match effect {
                Effect::StartTimer { id, after } if *id == release => Some(*after),
                _ => None,
            })
            .unwrap_or_else(|| panic!("the connector release is armed, got {effects:?}"));

        let released = core.apply(
            Event::Timer {
                id: release,
                generation: 0,
            },
            now() + armed,
        );
        assert!(
            released.contains(&Effect::UnlockConnector),
            "the release arrives, got {released:?}"
        );
    }

    #[test]
    fn the_refusal_is_withdrawn_when_the_vehicle_leaves() {
        // The refusal is a fault like any other this module raises, so the
        // unplug clears it. A port whose random device recovers is not held out
        // of service by a refusal it has outlived.
        let mut core = core_without_entropy();
        core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
        let effects = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::A)), now());
        assert!(
            effects
                .iter()
                .any(|effect| matches!(effect, Effect::ClearError(report)
                    if report.error_type == INTERNAL)),
            "the refusal is withdrawn, got {effects:?}"
        );
    }

    #[test]
    fn a_source_that_recovers_starts_the_next_session() {
        let mut core = core_without_entropy();
        core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
        core.apply(Event::Bsp(BspEvent::Cp(CpEvent::A)), now());

        core.session_ids = test_session_ids();
        let effects = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
        assert!(
            !report_for(&effects, SessionEvent::SessionStarted)
                .uuid
                .is_empty(),
            "the recovered source mints, got {effects:?}"
        );
        assert!(core.session().session_active);
    }

    #[test]
    fn an_authorization_that_cannot_be_given_an_identity_starts_no_transaction() {
        let mut core = core_without_entropy();
        let effects = core.apply(authorize(true, AuthorizationKind::Eim), now());

        assert!(
            reports(&effects).is_empty(),
            "no session event carries a missing identity, got {effects:?}"
        );
        assert!(
            !effects.iter().any(starts_transaction),
            "no transaction opens without a session, got {effects:?}"
        );
        assert_eq!(core.session().id, None);
        assert!(!core.session().session_active);
        assert!(!core.session().transaction_active);
    }

    #[test]
    fn a_refused_start_holds_no_authorization() {
        // A permission the port cannot act on must not survive the refusal: a
        // later pass would read it and start charging into a session that was
        // never opened.
        let mut core = core_without_entropy();
        core.apply(authorize(true, AuthorizationKind::Eim), now());
        assert!(!core.auth().authorized());
        assert_eq!(core.session().authorized_token, None);
        assert!(!core.session().authorized_plug_and_charge);
    }

    #[test]
    fn a_refused_start_never_reuses_the_previous_session_identity() {
        // The refusal must not fall back to the last id, which is also the
        // previous transaction's identity on the bus.
        let mut core = core();
        let first = report_for(
            &core.apply(authorize(true, AuthorizationKind::Eim), now()),
            SessionEvent::SessionStarted,
        )
        .uuid;
        assert!(!first.is_empty());
        // A withdraw ends the session from a phase where the authorization is
        // not in use (`Charger.cpp:1665`).
        core.session.phase = SessionPhase::Idle;
        core.apply(Event::Command(Command::WithdrawAuthorization), now());
        assert_eq!(core.session().id, None, "the first session released its id");

        core.session_ids = SessionIds::new(
            crate::core::config::SessionIdType::Uuid,
            Box::new(crate::core::session_id::ExhaustedBytes),
        );
        let effects = core.apply(authorize(true, AuthorizationKind::Eim), now());
        for report in reports(&effects) {
            assert_ne!(report.uuid, first, "{report:?}");
        }
        assert_eq!(core.session().id, None, "no identity was resurrected");
        assert!(!core.session().session_active);
    }

    #[test]
    fn only_the_start_carries_a_reason() {
        let mut core = core();
        let effects = core.apply(authorize(true, AuthorizationKind::Eim), now());
        for report in reports(&effects) {
            if report.event == SessionEvent::SessionStarted {
                continue;
            }
            assert_eq!(report.started, None, "{report:?}");
        }
    }

    #[test]
    fn one_identity_spans_a_session_and_the_next_session_differs() {
        let mut core = core();
        let mut first = reports(&core.apply(authorize(true, AuthorizationKind::Eim), now()));
        // A withdraw only ends the session from a phase where the
        // authorization is not in use (`Charger.cpp:1665`).
        core.session.phase = SessionPhase::Idle;
        first.extend(reports(
            &core.apply(Event::Command(Command::WithdrawAuthorization), now()),
        ));
        assert!(
            first
                .iter()
                .any(|report| report.event == SessionEvent::SessionFinished),
            "the first session ended: {first:?}"
        );
        let uuid = first[0].uuid.clone();
        assert!(!uuid.is_empty());
        for report in &first {
            assert_eq!(report.uuid, uuid, "every event of one session: {report:?}");
        }

        let second = reports(&core.apply(authorize(true, AuthorizationKind::Eim), now()));
        let next = second
            .iter()
            .find(|report| report.event == SessionEvent::SessionStarted)
            .expect("a second session started")
            .uuid
            .clone();
        assert!(!next.is_empty());
        assert_ne!(next, uuid, "a new session is a new identity");
        for report in &second {
            assert_eq!(report.uuid, next, "{report:?}");
        }
    }

    #[test]
    fn a_finished_session_releases_its_identity() {
        let mut core = core();
        core.apply(authorize(true, AuthorizationKind::Eim), now());
        assert!(core.session().id.is_some());
        core.session.phase = SessionPhase::Idle;
        core.apply(Event::Command(Command::WithdrawAuthorization), now());
        assert_eq!(
            core.session().id,
            None,
            "`Charger.cpp:1399` clears the uuid once the session is over"
        );
    }

    #[test]
    fn a_reservation_is_not_part_of_a_session() {
        // `evse/evse_managerImpl.cpp:126` clears the uuid on both reservation
        // events for exactly this reason.
        let mut core = core();
        let start = core.apply(
            Event::Command(Command::Reserve { reservation_id: 7 }),
            now(),
        );
        assert_eq!(report_for(&start, SessionEvent::ReservationStart).uuid, "");
        let end = core.apply(Event::Command(Command::CancelReservation), now());
        assert_eq!(report_for(&end, SessionEvent::ReservationEnd).uuid, "");
    }

    #[test]
    fn a_session_event_goes_to_the_ordered_publish_lane() {
        // Consumers read `session_event` as a sequence, so the lane that keeps
        // dispatch order is part of what makes the sequence meaningful.
        let mut core = core();
        for effect in core.apply(authorize(true, AuthorizationKind::Eim), now()) {
            if matches!(effect, Effect::PublishSessionEvent(_)) {
                assert_eq!(effect.context(), Some(effect::ExecContext::Publish));
                assert_eq!(effect.awaited(), None);
            }
        }
    }
    /// The reports a set of effects published, in order.
    fn published_events(effects: &[Effect]) -> Vec<SessionEvent> {
        effects
            .iter()
            .filter_map(|effect| match effect {
                Effect::PublishSessionEvent(report) => Some(report.event),
                _ => None,
            })
            .collect()
    }

    fn plug_in() -> Event {
        Event::Bsp(BspEvent::Cp(CpEvent::B))
    }

    /// A core whose power path has left its startup state, which is what every
    /// plug in needs first.
    /// Runs a command the way a caller blocked on its verdict does, and
    /// answers what the core put on the wire.
    fn awaited(core: &mut Core, command: Command) -> bool {
        awaited_with_effects(core, command).1
    }

    fn awaited_with_effects(core: &mut Core, command: Command) -> (Vec<Effect>, bool) {
        let reply = ReplyToken(0);
        let effects = core.apply(Event::CommandAwaiting { command, reply }, now());
        let answer = effects
            .iter()
            .find_map(|effect| match effect {
                Effect::AnswerCommand { reply: to, answer } if *to == reply => Some(*answer),
                _ => None,
            })
            .expect("an awaited command answers its caller");
        (effects, answer)
    }

    fn core_up() -> Core {
        let mut core = core();
        core.apply(Event::Startup, now());
        core
    }

    /// A port with a vehicle connected and waiting for authorization.
    ///
    /// This is the one state a billing record can be opened from:
    /// `Charger::start_transaction` is called from
    /// `case EvseState::WaitingForAuthentication` and nowhere else
    /// (`Charger.cpp:395` and `:524`), and that state is entered only from a
    /// plug in. Every test whose subject is the record itself starts here, so
    /// the authorization it sends has a vehicle to open the record for.
    fn core_awaiting_authorization() -> Core {
        let mut core = core_up();
        core.apply(plug_in(), now());
        core
    }

    /// The invariant absence buys, enumerated over the domain rather than
    /// sampled: **a deployment with no high level communication port never
    /// addresses the stack, whatever arrives.**
    ///
    /// This is the standing question answered for the collaborator the task
    /// names first. A basic AC port is the one deployment with no `HlcPort`,
    /// and the boundary subscribes to the ISO 15118 and SLAC callbacks whether
    /// a stack is wired or not, so every `HlcEvent` variant can reach such a
    /// core. Before this the cross product was sampled: a handful of arms were
    /// driven on an AC core and the rest only on a DC one, so a single arm that
    /// forgot the `Option` would have been caught by nothing.
    ///
    /// Every variant is listed by hand rather than generated, so a new one is
    /// a compile error here: `HlcEvent` has no `Default` and no iterator, and
    /// an arm added without a row would leave this test asserting less than it
    /// says.
    #[test]
    fn a_deployment_with_no_stack_never_addresses_one() {
        use crate::core::event::HlcSessionFailure;
        use crate::core::hlc::{DynamicModeRequest, EvMaximumLimits, SelectedService};

        let events: Vec<HlcEvent> = vec![
            HlcEvent::SessionSetup {
                evcc_id: "DEADBEEF".to_string(),
            },
            HlcEvent::SelectedService(SelectedService::DcBpt),
            HlcEvent::SaeBidiModeActive,
            HlcEvent::RequiresCableCheck,
            HlcEvent::PreChargeStarted,
            HlcEvent::CurrentDemandStarted,
            HlcEvent::CurrentDemandFinished,
            HlcEvent::StopFromEv(StopReason::EvDisconnected),
            HlcEvent::MatchingStarted(true),
            HlcEvent::MatchingStarted(false),
            HlcEvent::SlacMatched(true),
            HlcEvent::SlacMatched(false),
            HlcEvent::SlacErrorRoutine,
            HlcEvent::DataLinkReady(true),
            HlcEvent::DataLinkReady(false),
            HlcEvent::VehicleMacAddress("00:11:22:33:44:55".to_string()),
            HlcEvent::DataLinkError,
            HlcEvent::DataLinkPause,
            HlcEvent::DataLinkTerminate,
            HlcEvent::SetupFinished,
            HlcEvent::AllowCloseContactor(true),
            HlcEvent::AllowCloseContactor(false),
            HlcEvent::OpenContactorDc,
            HlcEvent::DcEvTarget {
                voltage_v: 400.0,
                current_a: 10.0,
            },
            HlcEvent::DcDynamicChargeMode(DynamicModeRequest {
                max_charge_power_w: 150_000.0,
                min_charge_power_w: 1_000.0,
                max_charge_current_a: 200.0,
                max_voltage_v: 900.0,
                min_voltage_v: 150.0,
                max_discharge_power_w: None,
                min_discharge_power_w: None,
                max_discharge_current_a: None,
            }),
            HlcEvent::DcEvMaximumLimits(EvMaximumLimits {
                maximum_current_a: Some(200.0),
                maximum_voltage_v: Some(700.0),
            }),
            HlcEvent::StateOfCharge { percent: 42.0 },
            HlcEvent::SessionFailed(HlcSessionFailure::UnexpectedSessionEnd),
            HlcEvent::ModeSelected {
                transfer: "AC_three_phase_core".to_string(),
            },
        ];

        for event in events {
            // A fresh core per variant, plugged in, so each arm is driven
            // against a live session rather than against whatever the previous
            // arm left behind.
            let mut core = core_up();
            core.apply(plug_in(), now());
            let effects = core.apply(Event::Hlc(event.clone()), now());

            for effect in &effects {
                assert!(
                    !matches!(
                        effect,
                        Effect::HlcUpdate(_)
                            | Effect::SlacUpdate(_)
                            | Effect::PublishProvidedToken(_)
                            | Effect::PublishHlcSessionFailed { .. }
                    ),
                    "a port with no stack spoke to one on {event:?}: {effect:?}"
                );
            }
        }
    }

    /// The same question for the two events a DC supply produces, which the
    /// C++ subscribes to only inside `if (hlc_enabled)` and
    /// `if (charge_mode == "DC")`, so a basic AC port hears neither.
    ///
    /// Driven because the boundary here subscribes unconditionally: a supply
    /// wired to an AC port would deliver both.
    #[test]
    fn a_deployment_with_no_stack_ignores_a_power_supply_wired_anyway() {
        let mut core = core_up();
        core.apply(plug_in(), now());

        let capabilities = core.apply(
            Event::PowerSupplyCapabilities(Box::new(PowerSupplyCapabilities {
                bidirectional: true,
                max_export_voltage_v: 950.0,
                ..PowerSupplyCapabilities::sane_default()
            })),
            now(),
        );
        assert!(
            capabilities.is_empty(),
            "the C++ installs no such subscription on this deployment: {capabilities:?}"
        );

        // And nothing reached the power path either. Asserting only on the
        // effects is too weak here: the IEC paths ignore every DC event, so a
        // report that fell through into the path would produce no effect and
        // read as correct. The recorder is the only fixture that can tell the
        // two apart, which is one of the things it is for.
        let (mut recording, events, _) = core_recording_path();
        recording.apply(Event::Startup, now());
        events.lock().unwrap().clear();
        recording.apply(
            Event::PowerSupplyCapabilities(Box::new(PowerSupplyCapabilities {
                bidirectional: true,
                max_export_voltage_v: 950.0,
                ..PowerSupplyCapabilities::sane_default()
            })),
            now(),
        );
        let seen = events.lock().unwrap().clone();
        assert!(
            seen.is_empty(),
            "a report this deployment never hears reached the path: {seen:?}"
        );

        let measurement = core.apply(
            Event::SupplyVoltageCurrent {
                voltage_v: 400.0,
                current_a: 10.0,
            },
            now(),
        );
        for effect in &measurement {
            assert!(
                !matches!(effect, Effect::HlcUpdate(_)),
                "a port with no stack told one a present value: {effect:?}"
            );
        }

        // And the session cannot have resolved to bidirectional off a supply
        // this deployment is not listening to.
        assert!(
            !core.session().profile.bidirectional,
            "a basic AC port cannot discharge"
        );
    }

    #[test]
    fn the_startup_transition_drives_the_power_path() {
        // The path holds the startup state, so a boot that emits its effects
        // directly leaves the reducer in it forever and every plug in that
        // follows is refused.
        let (mut core, _, calls) = core_recording_path();

        core.apply(Event::Startup, now());

        let calls_now = calls.lock().unwrap().clone();
        assert!(
            calls_now.contains(&"on_startup"),
            "got {:?}",
            calls_now
        );
    }

    #[test]
    fn the_startup_transition_leaves_the_control_pilot_offering_availability() {
        // The reducer's own boot output, which is the evidence it left the
        // startup state: X1 is only signalled from the Idle entry.
        let mut core = core();

        let effects = core.apply(Event::Startup, now());

        assert!(
            effects.contains(&Effect::SetCpState(crate::core::effect::CpState::X1)),
            "got {effects:?}"
        );
    }

    #[test]
    fn a_disable_that_wins_before_startup_leaves_the_board_output_off() {
        // Driving the path means the boot output is the path's answer, and a
        // port already taken out of service answers with nothing. The end state
        // matches the C++, which enables the board and then has the disable
        // stop it again; an enable restores both.
        let mut core = core();
        core.apply(
            disable(EnableSource::Csms, 100, EnableScope::Connector),
            now(),
        );

        let effects = core.apply(Event::Startup, now());
        assert!(
            !effects.contains(&Effect::BspEnable(true)),
            "got {effects:?}"
        );

        let restored = core.apply(
            enable(EnableSource::Csms, 100, EnableScope::Connector),
            now(),
        );
        assert!(
            restored.contains(&Effect::BspEnable(true)),
            "an enable puts the port back in service, got {restored:?}"
        );
    }

    #[test]
    fn a_plug_in_starts_the_session_and_then_asks_for_authorization() {
        // `Charger.cpp:266-272`: the initialize pass of WaitingForAuthentication
        // starts the session when none is active and asks for authorization
        // straight after, in that order.
        let mut core = core_up();

        let effects = core.apply(plug_in(), now());

        let started = index_of(&effects, |effect| {
            published(effect, SessionEvent::SessionStarted)
        });
        let required = index_of(&effects, |effect| {
            published(effect, SessionEvent::AuthRequired)
        });
        assert!(
            started < required,
            "the start must precede the request, got {effects:?}"
        );
    }

    #[test]
    fn a_plug_in_names_the_vehicle_as_the_reason_it_started() {
        let mut core = core_up();

        let effects = core.apply(plug_in(), now());

        let report = effects
            .iter()
            .filter_map(|effect| match effect {
                Effect::PublishSessionEvent(report)
                    if report.event == SessionEvent::SessionStarted =>
                {
                    Some(report)
                }
                _ => None,
            })
            .next()
            .unwrap_or_else(|| panic!("no start published, got {effects:?}"));
        assert_eq!(report.started, Some(StartSessionReason::EvConnected));
        assert!(!report.uuid.is_empty(), "{report:?}");
        assert_eq!(report.uuid, core.session().id.clone().unwrap());
    }

    #[test]
    fn the_authorization_request_carries_the_session_the_plug_in_opened() {
        let mut core = core_up();

        let effects = core.apply(plug_in(), now());

        let report = effects
            .iter()
            .filter_map(|effect| match effect {
                Effect::PublishSessionEvent(report)
                    if report.event == SessionEvent::AuthRequired =>
                {
                    Some(report)
                }
                _ => None,
            })
            .next()
            .unwrap_or_else(|| panic!("no request published, got {effects:?}"));
        assert_eq!(report.uuid, core.session().id.clone().unwrap());
    }

    #[test]
    fn a_plug_in_marks_the_session_active_and_clears_a_stale_cancellation() {
        // `Charger.cpp:1377-1379`. A cancellation left by the previous session
        // would otherwise discard the first authorization of this one.
        let mut core = core_up();
        core.session.externally_cancelled = true;

        core.apply(plug_in(), now());

        assert!(core.session().session_active);
        assert!(!core.session().externally_cancelled);
        assert_eq!(core.session().phase, SessionPhase::WaitingForAuthorization);
    }

    #[test]
    fn a_plug_in_opens_the_session_on_the_power_path() {
        let (mut core, _, calls) = core_recording_path();
        core.apply(Event::Startup, now());

        core.apply(plug_in(), now());

        let calls_now = calls.lock().unwrap().clone();
        assert!(
            calls_now.contains(&"on_session_start"),
            "got {:?}",
            calls_now
        );
    }

    #[test]
    fn a_repeated_state_b_does_not_start_a_second_session() {
        // The level says a vehicle is present; only the edge says one arrived.
        // Republishing a start would give the session a second identity and
        // every consumer two overlapping transactions.
        let mut core = core_up();
        let first = core.apply(plug_in(), now());
        let id = core.session().id.clone();

        let again = core.apply(plug_in(), now());

        assert!(published_events(&first).contains(&SessionEvent::SessionStarted));
        assert!(
            !published_events(&again).contains(&SessionEvent::SessionStarted),
            "got {again:?}"
        );
        assert_eq!(core.session().id, id, "the identity must not be reminted");
    }

    #[test]
    fn state_b_reached_from_state_c_is_not_a_plug_in() {
        // Sequence 7: the vehicle opened S2 mid session. Treating the level as
        // an arrival restarts the session under the charging vehicle.
        let mut core = core_up();
        core.apply(plug_in(), now());
        core.apply(Event::Bsp(BspEvent::Cp(CpEvent::C)), now());

        let effects = core.apply(plug_in(), now());

        assert!(
            !published_events(&effects).contains(&SessionEvent::SessionStarted),
            "got {effects:?}"
        );
    }

    #[test]
    fn the_simplified_mode_entry_straight_into_state_c_starts_a_session() {
        // Sequence 1.2 (`IECStateMachine.cpp:230-236`): a vehicle that closes
        // S2 without ever resting at B has still arrived, and a port that only
        // watches for state B never notices it.
        let mut core = core_up();
        core.apply(Event::Bsp(BspEvent::Cp(CpEvent::A)), now());

        let effects = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::C)), now());

        assert_eq!(
            published_events(&effects)
                .into_iter()
                .filter(|event| matches!(
                    event,
                    SessionEvent::SessionStarted | SessionEvent::AuthRequired
                ))
                .collect::<Vec<_>>(),
            vec![SessionEvent::SessionStarted, SessionEvent::AuthRequired],
            "got {effects:?}"
        );
    }

    #[test]
    fn a_plug_in_needs_no_authorization_to_reach_the_wire() {
        // The token provider publishes its token only on receiving a start
        // (`DummyTokenProvider/main/auth_token_providerImpl.cpp:12-13`), so a
        // port that waits for an authorization before starting the session
        // waits forever.
        let mut core = core_up();

        let effects = core.apply(plug_in(), now());

        assert!(published_events(&effects).contains(&SessionEvent::SessionStarted));
        assert!(!core.auth().authorized());
    }

    #[test]
    fn an_authorization_after_a_plug_in_does_not_start_a_second_session() {
        // `Charger.cpp:1640-1642` starts a session from an authorization only
        // when none is active, which the plug in already made true.
        let mut core = core_up();
        core.apply(plug_in(), now());

        let effects = core.apply(authorize(true, AuthorizationKind::Eim), now());

        let events = published_events(&effects);
        assert!(
            !events.contains(&SessionEvent::SessionStarted),
            "got {events:?}"
        );
        assert_eq!(
            events
                .iter()
                .filter(|event| matches!(
                    event,
                    SessionEvent::Authorized | SessionEvent::TransactionStarted
                ))
                .count(),
            2,
            "got {events:?}"
        );
    }

    /// Cause B, first half. `Charger::start_transaction` has exactly two call
    /// sites, `Charger.cpp:395` and `:524`, and both sit inside
    /// `case EvseState::WaitingForAuthentication`, a state entered only from
    /// `car_plugged_in`. So a driver who swipes before plugging in is granting
    /// permission, not starting a charge, and the C++ waits for the vehicle.
    ///
    /// The port announced the transaction from the authorization signal
    /// instead, in the same millisecond and whatever the state of the port. A
    /// broker capture of `ocpp201 authorization::test_c09` showed the whole
    /// bracket opening and closing inside 500 ms, before the simulated vehicle
    /// finished the one second sleep that precedes its control pilot B: the
    /// vehicle never connected at all, and 24 suite tests failed behind it.
    #[test]
    fn an_authorization_before_the_vehicle_waits_for_it() {
        let mut core = core_up();

        let authorized = core.apply(authorize(true, AuthorizationKind::Eim), now());

        assert!(
            !published_events(&authorized).contains(&SessionEvent::TransactionStarted),
            "no vehicle is connected, got {authorized:?}"
        );
        assert!(
            !authorized.iter().any(starts_transaction),
            "and the meter is not asked either, got {authorized:?}"
        );
        assert!(!core.session().transaction_active);

        let plugged = core.apply(plug_in(), now());

        // In the C++ order: the announcement comes from inside
        // `start_transaction`, which both call sites make before assigning
        // `PrepareCharging`, and the plug in has already asked for an
        // authorization it turns out to hold.
        //
        // The pass **ends** at `PrepareCharging`. The arriving vehicle's
        // control pilot B is one reading and makes one event: the C++ raises
        // `CarRequestedStopPower` only out of C or D
        // (`IECStateMachine::state_machine`, `IECStateMachine.cpp:198-204`), so
        // the `Charger::process_cp_events_state` arm that would pause
        // (`Charger.cpp:1189-1192`) is never reached by a plug in.
        // A port that paused here told OCPP the vehicle had suspended charging
        // before it had been offered any, which is what
        // `ocpp201 remote_control::test_F06` reads off the wire.
        assert_eq!(
            published_events(&plugged),
            vec![
                SessionEvent::AuthRequired,
                SessionEvent::TransactionStarted,
                SessionEvent::PrepareCharging,
            ],
            "got {plugged:?}"
        );
        assert!(core.session().transaction_active);
    }

    /// Cause B, second half. `Charger::deauthorize_internal`'s guard
    /// (`Charger.cpp:1665`) reads `shared_context.current_state`, and an
    /// authorization that arrived before the vehicle leaves that state `Idle`,
    /// which the guard allows: the C++ drops the permission and ends the
    /// session.
    ///
    /// The port read `SessionPhase` instead, which carries an `Authorized`
    /// value `EvseState` has none of and which the authorization had just
    /// assigned. The withdrawal did nothing, the connector never returned to
    /// available and the transaction was never closed. Across a substituted
    /// suite run a `TransactionEvent(Ended/StopAuthorized)` was followed by a
    /// `StatusNotification(Available)` 0 times out of 14.
    #[test]
    fn an_authorization_taken_back_before_the_vehicle_ends_the_session() {
        let mut core = core_up();
        core.apply(authorize(true, AuthorizationKind::Eim), now());
        assert_eq!(core.session().phase, SessionPhase::Authorized);

        let effects = core.apply(Event::Command(Command::WithdrawAuthorization), now());

        assert!(!core.auth().authorized(), "got {effects:?}");
        assert!(!core.session().session_active, "got {effects:?}");
        assert!(
            published_events(&effects).contains(&SessionEvent::SessionFinished),
            "got {effects:?}"
        );
    }

    #[test]
    fn a_replug_starts_a_second_session_with_an_identity_of_its_own() {
        let mut core = core_up();
        core.apply(plug_in(), now());
        let first = core.session().id.clone().unwrap();
        core.apply(unplug(), now());
        core.apply(
            Event::Command(Command::StopTransaction {
                reason: StopTransactionReason::EvDisconnected,
                id_tag: None,
            }),
            now(),
        );
        core.session.session_active = false;

        let effects = core.apply(plug_in(), now());

        assert!(published_events(&effects).contains(&SessionEvent::SessionStarted));
        assert_ne!(core.session().id.clone().unwrap(), first);
    }

    #[test]
    fn a_port_taken_out_of_service_under_a_vehicle_recognizes_the_next_arrival() {
        // The arbitration decision reaches the transition memory. The pilot was
        // resting at B when service ended, so without it the same B read on the
        // way back looks like a level already resident and the port never
        // starts another session.
        let mut core = core_up();
        core.apply(plug_in(), now());
        core.apply(
            disable(EnableSource::Csms, 100, EnableScope::Connector),
            now(),
        );
        // The session went out of service with the port.
        core.session.session_active = false;
        core.apply(
            enable(EnableSource::Csms, 100, EnableScope::Connector),
            now(),
        );

        let effects = core.apply(plug_in(), now());

        assert!(
            published_events(&effects).contains(&SessionEvent::SessionStarted),
            "got {effects:?}"
        );
    }

    #[test]
    fn a_plug_in_after_an_authorization_first_start_does_not_restart_the_session() {
        // `Charger.cpp:268`: the start is guarded on no session being active.
        // An authorization that came first already opened one, and restarting
        // would remint the identity under a transaction already running.
        let mut core = core_up();
        core.apply(authorize(true, AuthorizationKind::Eim), now());
        let id = core.session().id.clone();
        assert!(id.is_some(), "the authorization opened a session");

        let effects = core.apply(plug_in(), now());

        assert!(
            !published_events(&effects).contains(&SessionEvent::SessionStarted),
            "got {effects:?}"
        );
        assert_eq!(core.session().id, id, "the identity must not be reminted");
        assert_eq!(
            core.session().last_start_reason,
            StartSessionReason::Authorized,
            "the reason the session actually started for must survive"
        );
        assert!(
            published_events(&effects).contains(&SessionEvent::AuthRequired),
            "the request is not guarded (`Charger.cpp:272`), got {effects:?}"
        );
    }

    /// The charging progress half of the session event stream, and the billing
    /// record an ordinary charge leaves behind.
    mod charge_progress {
        use super::*;

        /// Plug in, authorize, draw power. The state edges the AC path crosses
        /// on the way are the only producer of these four events.
        fn charging_core() -> (Core, Vec<Effect>) {
            let mut core = core();
            // The path leaves its startup state only on the startup fact, and
            // a path still in it acts on no plug in at all.
            core.apply(Event::Startup, now());
            let mut seen = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
            seen.extend(core.apply(authorize(true, AuthorizationKind::Eim), now()));
            seen.extend(core.apply(Event::Bsp(BspEvent::Cp(CpEvent::C)), now()));
            (core, seen)
        }

        #[test]
        fn a_charge_publishes_its_progress_events_in_order() {
            let (_, effects) = charging_core();
            let order: Vec<SessionEvent> = reports(&effects).into_iter().map(|r| r.event).collect();
            let progress: Vec<SessionEvent> = order
                .iter()
                .copied()
                .filter(|event| {
                    matches!(
                        event,
                        SessionEvent::PrepareCharging | SessionEvent::ChargingStarted
                    )
                })
                .collect();
            assert_eq!(
                progress,
                vec![SessionEvent::PrepareCharging, SessionEvent::ChargingStarted],
                "got {order:?}"
            );
        }

        #[test]
        fn every_progress_event_carries_the_session_identity() {
            let (core, effects) = charging_core();
            let uuid = core.session().id.clone().expect("a session is live");
            for report in reports(&effects) {
                assert_eq!(report.uuid, uuid, "{report:?}");
            }
        }

        /// The reason this matters: a charge that ends the ordinary way, the
        /// driver unplugs, must close the billing record the authorization
        /// opened. `Charger.cpp:1063-1067`.
        #[test]
        fn an_unplug_during_charging_closes_the_billing_record() {
            let (mut core, _) = charging_core();
            assert!(core.session().transaction_active, "a record is open");
            let opened = core.session().id.clone().expect("a session is live");

            let effects = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::A)), now());

            assert!(
                effects.iter().any(
                    |effect| matches!(effect, Effect::StopTransaction { transaction_id }
                        if *transaction_id == opened)
                ),
                "the record the charge opened is closed, got {effects:?}"
            );
            // The stop is announced before the record closes, because the C++
            // publishes it from the `StoppingCharging` entry (`Charger.cpp:1012`)
            // on the way to `Finished`, which is where the close happens
            // (`Charger.cpp:1064-1067`). An unplug that jumped straight to rest
            // would close the record without ever saying the charge was stopping.
            let order: Vec<SessionEvent> = reports(&effects).into_iter().map(|r| r.event).collect();
            assert_eq!(
                order,
                vec![
                    SessionEvent::StoppingCharging,
                    SessionEvent::ChargingFinished,
                    SessionEvent::TransactionFinished,
                    SessionEvent::SessionFinished
                ],
                "got {effects:?}"
            );
            assert!(!core.session().transaction_active);
            assert!(!core.session().session_active);
            assert_eq!(core.session().id, None);
        }

        /// The DC counterpart of the drive above, through `Core` rather than
        /// against the path.
        ///
        /// The vehicle arrives, is authorized and starts current demand, which
        /// is the only route into `Charging` on DC
        /// (`Charger::notify_currentdemand_started`, `Charger.cpp:2024`).
        fn charging_dc_core() -> (Core, Vec<Effect>) {
            let mut core = dc_core();
            core.apply(Event::Startup, now());
            let mut seen = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
            seen.extend(core.apply(authorize(true, AuthorizationKind::Eim), now()));
            seen.extend(core.apply(Event::Hlc(HlcEvent::CurrentDemandStarted), now()));
            (core, seen)
        }

        #[test]
        fn a_dc_charge_publishes_its_progress_events_in_order() {
            let (_, effects) = charging_dc_core();
            let progress: Vec<SessionEvent> = published_events(&effects)
                .into_iter()
                .filter(|event| {
                    matches!(
                        event,
                        SessionEvent::PrepareCharging | SessionEvent::ChargingStarted
                    )
                })
                .collect();
            assert_eq!(
                progress,
                vec![SessionEvent::PrepareCharging, SessionEvent::ChargingStarted],
                "got {effects:?}"
            );
        }

        /// The relay confirmation reaches the power path through the core, so a
        /// DC session that has it energizes on the vehicle's target and one
        /// that does not holds the target and stays off (`:2662`).
        ///
        /// Asserted here rather than only against the path because the gate has
        /// a route to satisfy: the confirmation is a BSP event, and a core that
        /// stopped handing those to the path would refuse to energize any DC
        /// session at all while every path test stayed green. A mutation that
        /// nailed the gate shut reddened sixteen path tests and nothing here,
        /// which is what said this drive was missing.
        #[test]
        fn a_dc_target_energizes_only_once_the_core_has_the_relay_confirmation() {
            let target = Event::Hlc(HlcEvent::DcEvTarget {
                voltage_v: 400.0,
                current_a: 40.0,
            });
            let modes = |effects: &[Effect]| {
                effects
                    .iter()
                    .filter(|effect| matches!(effect, Effect::SetSupplyMode { .. }))
                    .count()
            };

            let (mut without, _) = charging_dc_core();
            let held = without.apply(target.clone(), now());
            assert_eq!(
                modes(&held),
                0,
                "no relay confirmation, so no energize: {held:?}"
            );
            assert!(
                held.iter()
                    .any(|effect| matches!(effect, Effect::SetSupplySetpoint { .. })),
                "the target itself still reaches the supply: {held:?}"
            );

            let (mut with, _) = charging_dc_core();
            with.apply(Event::Bsp(BspEvent::Cp(CpEvent::PowerOn)), now());
            let energized = with.apply(target, now());
            assert_eq!(
                modes(&energized),
                1,
                "the confirmed contactor energizes: {energized:?}"
            );
        }

        /// The same fact the AC drive above asserts, on the path that reaches a
        /// resting state from a live session without passing through
        /// `Finished`. That route is the reason the close travels with the edge
        /// into the resting state rather than with the `Finished` entry, and
        /// this is the only place the whole chain is driven for it.
        #[test]
        fn an_unplug_during_dc_current_demand_closes_the_billing_record() {
            let (mut core, _) = charging_dc_core();
            assert!(core.session().transaction_active, "a record is open");
            let opened = core.session().id.clone().expect("a session is live");

            let effects = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::A)), now());

            let closed: Vec<&Effect> = effects
                .iter()
                .filter(|effect| matches!(effect, Effect::StopTransaction { .. }))
                .collect();
            assert_eq!(
                closed.len(),
                1,
                "exactly one record closes, got {effects:?}"
            );
            assert!(
                matches!(closed[0], Effect::StopTransaction { transaction_id }
                    if *transaction_id == opened),
                "the record the charge opened is closed, got {effects:?}"
            );
            // The unplug crosses the stopping entry and then goes to rest
            // without a `Finished` in between, which is what the resting state
            // arm of `duties_for_edge` exists for: the close travels with the
            // edge into `Idle` rather than with a `Finished` entry that never
            // runs here.
            assert_eq!(
                published_events(&effects),
                vec![
                    SessionEvent::StoppingCharging,
                    SessionEvent::ChargingFinished,
                    SessionEvent::TransactionFinished,
                    SessionEvent::SessionFinished
                ],
                "got {effects:?}"
            );
            assert!(!core.session().transaction_active);
            assert!(!core.session().session_active);
            assert_eq!(core.session().id, None);
        }

        /// The guard `Charger.cpp:1064` states in its comment: a transaction
        /// cancelled earlier must not produce a second transactionFinished.
        #[test]
        fn a_session_ended_by_deauthorization_and_then_unplugged_stops_the_record_once() {
            let (mut core, _) = charging_core();
            let mut seen = core.apply(
                Event::Command(Command::StopTransaction {
                    reason: StopTransactionReason::Remote,
                    id_tag: None,
                }),
                now(),
            );
            assert!(
                seen.iter().any(stops_transaction),
                "the remote stop closes it, got {seen:?}"
            );

            seen.extend(core.apply(Event::Bsp(BspEvent::Cp(CpEvent::A)), now()));

            assert_eq!(
                seen.iter()
                    .filter(|effect| stops_transaction(effect))
                    .count(),
                1,
                "one close for one record, got {seen:?}"
            );
            assert_eq!(
                reports(&seen)
                    .iter()
                    .filter(|report| report.event == SessionEvent::TransactionFinished)
                    .count(),
                1,
                "one transactionFinished, got {seen:?}"
            );
            assert!(
                reports(&seen)
                    .iter()
                    .any(|report| report.event == SessionEvent::SessionFinished),
                "the unplug still ends the session, got {seen:?}"
            );
            assert!(!core.session().session_active);
        }

        /// A withdraw from a state where the authorization is not in use ends
        /// the session outright, which is the one route that already called
        /// `finish_session`. The unplug that follows must not end it twice.
        ///
        /// Driven on the high level communication path, because that is where
        /// the window is reachable with a vehicle connected: an authorization
        /// that arrives before SLAC has matched sends the pilot through state F
        /// first (`Charger.cpp:404-420`), and the port holds
        /// `WaitingForAuthentication` for the whole detour. The withdraw's
        /// guard allows that state, so the session ends under a connected
        /// vehicle and the unplug still has to arrive.
        #[test]
        fn a_session_already_finished_is_not_finished_again_by_the_unplug() {
            let mut core = ac_hlc_core();
            core.apply(Event::Startup, now());
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
            core.apply(
                Event::Hlc(HlcEvent::RequireAuthPlugAndCharge {
                    token: crate::core::hlc::OpaqueToken::new(serde_json::json!({
                        "id_token": {"value": "CONTRACT", "type": "eMAID"},
                        "authorization_type": "PlugAndCharge",
                    })),
                }),
                now(),
            );
            core.apply(authorize(true, AuthorizationKind::PlugAndCharge), now());
            assert_eq!(
                core.path.state(),
                AcState::WaitingForAuthentication,
                "the detour holds the state the withdraw is allowed from"
            );
            let withdrawn = core.apply(Event::Command(Command::WithdrawAuthorization), now());
            assert!(
                reports(&withdrawn)
                    .iter()
                    .any(|report| report.event == SessionEvent::SessionFinished),
                "the withdraw ended it, got {withdrawn:?}"
            );

            let unplugged = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::A)), now());

            assert!(
                reports(&unplugged).is_empty(),
                "nothing is left to end, got {unplugged:?}"
            );
            assert!(!unplugged.iter().any(stops_transaction));
        }

        /// An unplug before any authorization arrived still ends the session.
        /// `Charger.cpp:316-319` sends that case to `Finished` too.
        #[test]
        fn an_unplug_before_authorization_ends_the_session_without_a_record() {
            let mut core = core();
            core.apply(Event::Startup, now());
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
            assert!(core.session().session_active);

            let effects = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::A)), now());

            assert!(
                !effects.iter().any(stops_transaction),
                "no record was ever opened, got {effects:?}"
            );
            assert!(
                reports(&effects)
                    .iter()
                    .any(|report| report.event == SessionEvent::SessionFinished),
                "got {effects:?}"
            );
            assert!(!core.session().session_active);
            assert_eq!(core.session().id, None);
        }

        /// The session that follows gets its own identity, which is what a
        /// session left open would have denied it.
        #[test]
        fn the_session_after_an_unplug_starts_fresh() {
            let (mut core, _) = charging_core();
            let first = core.session().id.clone().expect("a session is live");
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::A)), now());

            let effects = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());

            let started = report_for(&effects, SessionEvent::SessionStarted);
            assert_ne!(started.uuid, first, "a new identity");
            assert_eq!(started.started, Some(StartSessionReason::EvConnected));
        }

        /// The one route where the `Finished` entry does the closing rather
        /// than inheriting a record already closed: a control pilot fault
        /// stops the charge without going through `Core::stop`, so the record
        /// is still open when the relays are observed open.
        #[test]
        fn a_pilot_fault_that_stops_the_charge_closes_the_record_on_the_relays() {
            let (mut core, _) = charging_core();
            let faulted = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::E)), now());
            assert!(
                reports(&faulted)
                    .iter()
                    .any(|report| report.event == SessionEvent::StoppingCharging),
                "the fault routes to stopping, got {faulted:?}"
            );
            assert!(
                core.session().transaction_active,
                "nothing closed the record yet"
            );

            let opened = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::PowerOff)), now());

            assert!(
                opened.iter().any(stops_transaction),
                "the relays opening closes it, got {opened:?}"
            );
            assert!(!core.session().transaction_active);
            assert!(
                core.session().session_active,
                "a finished session waits for the unplug"
            );
        }

        /// The reason set an announcement carries, and the whole of why the
        /// wire field exists: `Charger.cpp:1005-1017` names the operator's
        /// pause and the want of energy separately, and a consumer that cannot
        /// tell them apart cannot tell a deliberate hold from a grid shortage.
        /// A live charge on a port that will act on an enforced grant: the
        /// board has reported its phase counts, without which the energy tree
        /// discards one, and callers drive grants at `settled` so the startup
        /// window's random delay does not withhold them.
        ///
        /// `charging_core` deliberately does neither, because every other test
        /// here drives the pilot and the commands rather than the budget.
        fn charging_core_taking_grants(t0: Instant) -> Core {
            let mut core = core();
            core.apply(Event::Startup, t0);
            core.apply(
                Event::Bsp(BspEvent::Capabilities(HardwareCapabilities {
                    max_phase_count_import: 1,
                    min_phase_count_import: 1,
                    ..HardwareCapabilities::default()
                })),
                t0,
            );
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), t0);
            core.apply(authorize(true, AuthorizationKind::Eim), t0);
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::C)), t0);
            assert_eq!(core.path.state(), AcState::Charging, "the fixture charges");
            core
        }

        /// Past the five second startup window, so a grant is applied rather
        /// than delayed.
        fn settled(from: Instant) -> Instant {
            from + std::time::Duration::from_secs(10)
        }

        /// An enforced grant of `ampere`, the one route by which the AC
        /// branch of `power_available` changes its answer.
        fn budget_of(ampere: f64) -> Event {
            Event::EnforcedLimits(Box::new(energy::enforce::EnforcedLimits {
                uuid: "evse_manager".to_owned(),
                valid_for_s: 60,
                schedule: Vec::new(),
                limits_root_side: energy::enforce::LimitsRes {
                    ac_max_current_a: Some(energy::flow_request::NumberWithSource::new(
                        ampere, "test",
                    )),
                    ..energy::enforce::LimitsRes::default()
                },
            }))
        }

        /// A grant of nothing, which is what the energy manager sends when the
        /// budget goes away rather than sending no grant at all.
        fn no_budget() -> Event {
            budget_of(0.0)
        }

        fn pause_reasons_of(effects: &[Effect]) -> Vec<PauseReason> {
            match report_for(effects, SessionEvent::ChargingPausedEvse)
                .payload
                .unwrap_or_else(|| panic!("the pause carried no reasons, got {effects:?}"))
            {
                SessionPayload::ChargingPausedEvse { reasons } => reasons,
                other => panic!("a pause carried {other:?}"),
            }
        }

        #[test]
        fn an_operator_pause_names_itself_and_nothing_else() {
            let (mut core, _) = charging_core();

            let paused = core.apply(Event::Command(Command::PauseCharging), now());

            assert_eq!(pause_reasons_of(&paused), vec![PauseReason::UserPause]);
        }

        /// The other reason, reached without an operator asking for anything:
        /// the budget goes away under a live charge and the port settles into
        /// the paused state naming the want of energy alone.
        #[test]
        fn a_pause_for_want_of_energy_names_that_and_not_the_operator() {
            let t0 = now();
            let mut core = charging_core_taking_grants(t0);

            let starved = core.apply(no_budget(), settled(t0));

            assert_eq!(core.path.state(), AcState::ChargingPausedEvse);
            assert_eq!(
                pause_reasons_of(&starved),
                vec![PauseReason::NoEnergy],
                "got {starved:?}"
            );
        }

        /// The assessment is the `ChargingPausedEVSE` body and nothing else, so
        /// no other state announces a pause. The two facts are readable from
        /// every state and neither is about being paused: an idle port with no
        /// budget has no power available and is holding no charge, and without
        /// the state gate it announces one.
        #[test]
        fn only_the_paused_state_announces_a_pause() {
            let t0 = now();
            let mut core = charging_core_taking_grants(t0);
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::A)), settled(t0));
            assert_eq!(core.path.state(), AcState::Idle, "the control");

            let starved = core.apply(no_budget(), settled(t0));

            assert!(
                !starved
                    .iter()
                    .any(|effect| published(effect, SessionEvent::ChargingPausedEvse)),
                "an idle port announced a pause: {starved:?}"
            );
        }

        /// The announcement is raised again when the set changes and not when
        /// it repeats, which is `Charger.cpp:1027-1029`. A port that announced
        /// on every pass would flood a CSMS; one that announced only on the
        /// entry would never correct what it first said.
        #[test]
        fn a_pause_is_announced_again_only_when_the_set_it_named_changes() {
            let t0 = now();
            let mut core = charging_core_taking_grants(t0);
            core.apply(Event::Command(Command::PauseCharging), settled(t0));
            // Both reasons now hold, and the widening is announced.
            let widened = core.apply(no_budget(), settled(t0));
            assert_eq!(
                pause_reasons_of(&widened),
                vec![PauseReason::NoEnergy, PauseReason::UserPause],
                "got {widened:?}"
            );

            let repeated = core.apply(no_budget(), settled(t0));
            assert!(
                !repeated
                    .iter()
                    .any(|effect| published(effect, SessionEvent::ChargingPausedEvse)),
                "the same set was announced twice: {repeated:?}"
            );

            // And the narrowing is announced: the budget returns, the operator
            // pause is all that is left, and the C++ next pass says so.
            let narrowed = core.apply(budget_of(16.0), settled(t0));
            assert_eq!(
                pause_reasons_of(&narrowed),
                vec![PauseReason::UserPause],
                "got {narrowed:?}"
            );
        }

        /// The `Error` reason `PauseReason` has no variant for is unreachable
        /// rather than unported, and this is what says so: a charging
        /// preventing fault takes the port out of the paused state in the same
        /// pass, so no set this module can build has a fatal error standing
        /// behind it.
        #[test]
        fn a_fatal_fault_leaves_the_paused_state_rather_than_naming_a_reason() {
            let (mut core, _) = charging_core();
            core.apply(Event::Command(Command::PauseCharging), now());
            assert_eq!(core.path.state(), AcState::ChargingPausedEvse, "the control");

            let faulted = core.apply(
                Event::Error(ErrorEvent {
                    source: ErrorSource::Bsp,
                    error_type: "evse_board_support/MREC8EmergencyStop".into(),
                    sub_type: String::new(),
                    vendor_id: String::new(),
                    severity: Severity::High,
                    raised: true,
                }),
                now(),
            );

            assert_ne!(
                core.path.state(),
                AcState::ChargingPausedEvse,
                "got {faulted:?}"
            );
            assert!(
                !faulted
                    .iter()
                    .any(|effect| published(effect, SessionEvent::ChargingPausedEvse)),
                "a fault announced a pause: {faulted:?}"
            );
        }

        /// The pause and resume pair, driven through the inbound commands
        /// rather than through the path directly.
        #[test]
        fn a_pause_and_a_resume_reach_the_wire_as_a_pause_and_a_prepare() {
            let (mut core, _) = charging_core();
            let paused = core.apply(Event::Command(Command::PauseCharging), now());
            let resumed = core.apply(Event::Command(Command::ResumeCharging), now());

            // Two edges, not one: `Charger::run_state_machine`'s `Charging`
            // arm takes the pause out of `Charging` through the stopping entry,
            // and the relays already being open settles it into the paused
            // state in the same pass.
            assert_eq!(
                reports(&paused)
                    .into_iter()
                    .map(|r| r.event)
                    .collect::<Vec<_>>(),
                vec![
                    SessionEvent::StoppingCharging,
                    SessionEvent::ChargingPausedEvse
                ]
            );
            assert_eq!(
                reports(&resumed)
                    .into_iter()
                    .map(|r| r.event)
                    .collect::<Vec<_>>(),
                vec![SessionEvent::PrepareCharging]
            );
            assert!(
                core.session().transaction_active,
                "a pause leaves the record open"
            );
            assert!(core.session().session_active);
        }

        /// The vehicle leaves while the EVSE holds the offer withdrawn.
        ///
        /// `Charger.cpp:942-946` routes a departed vehicle out of the EVSE side
        /// pause through `StoppingCharging` exactly as it does out of the EV
        /// side one, so the stop is announced before the record closes. Only
        /// the EV side of that pair was driven before this.
        #[test]
        fn an_unplug_during_an_evse_pause_stops_the_charge_and_closes_the_record() {
            let (mut core, _) = charging_core();
            core.apply(Event::Command(Command::PauseCharging), now());
            let opened = core.session().id.clone().expect("a session is live");

            let effects = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::A)), now());

            assert!(
                effects.iter().any(
                    |effect| matches!(effect, Effect::StopTransaction { transaction_id }
                        if *transaction_id == opened)
                ),
                "got {effects:?}"
            );
            assert_eq!(
                published_events(&effects),
                vec![
                    SessionEvent::StoppingCharging,
                    SessionEvent::ChargingFinished,
                    SessionEvent::TransactionFinished,
                    SessionEvent::SessionFinished
                ],
                "got {effects:?}"
            );
            assert!(!core.session().transaction_active);
            assert!(!core.session().session_active);
        }

        /// The ordinary resume: the vehicle stops drawing, then draws again.
        ///
        /// Two events, in this order, and the order is the whole assertion:
        /// `Charger::process_cp_events_state`'s `ChargingPausedEV` arm answers
        /// the fresh state C by assigning `PrepareCharging`, and
        /// `Charger::run_state_machine`'s `PrepareCharging` arm is the single
        /// entry into `EvseState::Charging`. So a C++ deployment publishes the
        /// preparation and then the start on every EV side resume. There is
        /// still no separate resumed event on the wire, which is why
        /// `SessionEvent::ChargingResumed` was removed rather than mapped.
        ///
        /// This test used to expect `ChargingStarted` alone, on the reasoning
        /// that a single entry into the charging state means a resume looks
        /// like a first start. The single entry is real; what it misses is that
        /// the C++ arrives at that entry through the preparation, which the
        /// port skipped. Anything counting session events, OCPP included, saw
        /// a different sequence for it.
        ///
        /// This pair went undriven through six waves while both neighbouring
        /// pairs were covered, which is why it is asserted through `Core` and
        /// not against the reducer.
        #[test]
        fn an_ev_pause_and_resume_announce_the_pause_and_then_charging_again() {
            let (mut core, _) = charging_core();

            let paused = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
            assert_eq!(
                published_events(&paused),
                vec![SessionEvent::ChargingPausedEv],
                "got {paused:?}"
            );

            let resumed = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::C)), now());
            assert_eq!(
                published_events(&resumed),
                vec![SessionEvent::PrepareCharging, SessionEvent::ChargingStarted],
                "got {resumed:?}"
            );

            // The record the charge opened stays open across the pause. A resume
            // is one charge continuing, not a second one.
            assert!(core.session().transaction_active);
            assert!(!resumed
                .iter()
                .any(|effect| matches!(effect, Effect::StartTransaction { .. })));
        }

        /// Availability arbitration takes an occupied port out of service.
        /// `Charger.cpp:1080-1084` ends the session on the way into `Disabled`
        /// as much as on the way into `Idle`.
        #[test]
        fn a_disable_during_charging_closes_the_record_and_ends_the_session() {
            let (mut core, _) = charging_core();
            let opened = core.session().id.clone().expect("a session is live");

            let effects = core.apply(
                disable(EnableSource::Csms, 1000, EnableScope::Connector),
                now(),
            );

            assert!(
                effects.iter().any(
                    |effect| matches!(effect, Effect::StopTransaction { transaction_id }
                        if *transaction_id == opened)
                ),
                "got {effects:?}"
            );
            let published: Vec<SessionEvent> =
                reports(&effects).into_iter().map(|r| r.event).collect();
            assert_eq!(
                published,
                vec![
                    SessionEvent::StoppingCharging,
                    SessionEvent::ChargingFinished,
                    SessionEvent::TransactionFinished,
                    SessionEvent::SessionFinished
                ],
                "got {effects:?}"
            );
            assert!(!core.session().session_active);
            assert!(!core.session().transaction_active);
        }
    }

    /// The powermeter duties that are neither a session start nor a session
    /// end: the startup cleanup, and what a refused transaction start does.
    mod metering {
        use super::*;

        /// A port with a vehicle connected and waiting for authorization,
        /// which is the state a billing record can be opened from:
        /// `Charger::start_transaction` is called from
        /// `case EvseState::WaitingForAuthentication` alone (`Charger.cpp:395`
        /// and `:524`). The authorization each test below sends is what opens
        /// the record, and it can only do that with the vehicle already there.
        fn core_with_metering(metering: Metering) -> Core {
            let mut core = core();
            core.metering = metering;
            core.apply(Event::Startup, now());
            core.apply(plug_in(), now());
            core
        }

        /// The awaited identity of the metering transaction start in `effects`.
        fn start_id(effects: &[Effect]) -> EffectId {
            effects
                .iter()
                .find_map(|effect| match effect {
                    Effect::StartTransaction { id, .. } => Some(*id),
                    _ => None,
                })
                .expect("an accepted authorization starts the metering transaction")
        }

        fn raises(effect: &Effect, error_type: &str) -> bool {
            matches!(effect, Effect::RaiseError(report) if report.error_type == error_type)
        }

        fn raises_start_failure(effect: &Effect) -> bool {
            raises(effect, faults::POWERMETER_TRANSACTION_START_FAILED)
        }

        /// `Charger::cleanup_transactions_on_startup` (`Charger.cpp:1504-1511`)
        /// closes whatever the meter still holds from before the restart. The
        /// empty transaction id means "cancel every ongoing transaction"
        /// (`interfaces/powermeter.yaml`), which is why it is its own effect and
        /// not a `StopTransaction` carrying an empty string.
        #[test]
        fn startup_cancels_every_transaction_the_meter_still_holds() {
            let mut core = core();

            let effects = core.apply(Event::Startup, now());

            assert!(
                effects.contains(&Effect::CancelAllTransactions),
                "got {effects:?}"
            );
        }

        /// The C++ cleans up before `ready_to_start_charging`
        /// (`EvseManager.cpp:1485-1494`), so no consumer sees the module up
        /// while a stale record is still open in the meter.
        #[test]
        fn the_startup_cancel_precedes_the_ready_announcement() {
            let mut core = core();

            let effects = core.apply(Event::Startup, now());

            let cancel = effects
                .iter()
                .position(|effect| *effect == Effect::CancelAllTransactions)
                .expect("the cleanup runs at startup");
            let ready = effects
                .iter()
                .position(|effect| *effect == Effect::PublishReady(true))
                .expect("the ready announcement runs at startup");
            assert!(cancel < ready, "got {effects:?}");
        }

        /// A port that waits for an external signal still cleans up: the C++
        /// cleanup is outside the `external_ready_to_start_charging` branch
        /// (`EvseManager.cpp:1485` against `:1492`).
        #[test]
        fn the_startup_cancel_does_not_wait_on_the_external_ready_signal() {
            let mut core = core_with(ReadyGate {
                awaits_external_signal: true,
            });

            let effects = core.apply(Event::Startup, now());

            assert!(
                effects.contains(&Effect::CancelAllTransactions),
                "got {effects:?}"
            );
            assert!(
                !effects.contains(&Effect::PublishReady(true)),
                "got {effects:?}"
            );
        }

        /// `Charger::start_transaction` (`Charger.cpp:1427-1432`): a meter that
        /// refuses the start means the customer cannot be billed, so the charge
        /// stops. Only the core may decide that here, which is why the boundary
        /// reports the refusal and this arm acts on it.
        #[test]
        fn a_refused_transaction_start_raises_when_configured_to_fail() {
            let mut core = core_with_metering(Metering {
                fail_on_errors: true,
            });
            let opened = core.apply(authorize(true, AuthorizationKind::Eim), now());
            let id = start_id(&opened);

            let effects = core.apply(
                Event::EffectDone {
                    id: Some(id),
                    outcome: EffectOutcome::Failed("meter busy".to_owned()),
                },
                now(),
            );

            assert!(effects.iter().any(raises_start_failure), "got {effects:?}");
        }

        /// A verdict that arrives after the session it belongs to has ended is
        /// not that session's business any more, and raising on it would take an
        /// idle port out of service.
        ///
        /// The C++ cannot reach this shape: its `start_transaction` is
        /// synchronous inside the state machine, so the verdict is in hand
        /// before the session can end. Here it returns as an event and can
        /// outlive the session that asked for it. Left standing, the raise would
        /// also latch, because the unplug that runs `clear_own_errors` has
        /// already happened.
        #[test]
        fn a_start_verdict_arriving_after_the_session_ended_raises_nothing() {
            let mut core = core_with_metering(Metering {
                fail_on_errors: true,
            });
            // The unplug only ends a session the path has actually entered, so
            // the port has to leave its startup state and see the vehicle first.
            // Authorizing alone leaves the reducer in `Startup`, where an unplug
            // crosses no edge and raises no duty.
            core.apply(Event::Startup, now());
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
            let opened = core.apply(authorize(true, AuthorizationKind::Eim), now());
            let id = start_id(&opened);

            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::A)), now());
            assert!(!core.session().session_active, "the session is over");

            let late = core.apply(
                Event::EffectDone {
                    id: Some(id),
                    outcome: EffectOutcome::Failed("meter busy".to_owned()),
                },
                now(),
            );

            assert!(
                !late.iter().any(raises_start_failure),
                "an idle port is not taken out of service by a dead session, got {late:?}"
            );
        }

        /// The raise is what stops the charge: the fault set answers a blocking
        /// cause with `Inoperative` and the error shutdown class, which is the
        /// route every other refusal in this file takes.
        #[test]
        fn a_refused_transaction_start_takes_the_port_out_of_service() {
            let mut core = core_with_metering(Metering {
                fail_on_errors: true,
            });
            let opened = core.apply(authorize(true, AuthorizationKind::Eim), now());
            let id = start_id(&opened);

            let effects = core.apply(
                Event::EffectDone {
                    id: Some(id),
                    outcome: EffectOutcome::Failed("meter busy".to_owned()),
                },
                now(),
            );

            assert!(
                effects
                    .iter()
                    .any(|effect| raises(effect, faults::INOPERATIVE)),
                "got {effects:?}"
            );
            assert!(core.faults.prevents_charging());
        }

        /// The refusal is carried, not replaced by a description of this port's
        /// own making: the meter said why.
        #[test]
        fn the_refusal_carries_what_the_meter_reported() {
            let mut core = core_with_metering(Metering {
                fail_on_errors: true,
            });
            let opened = core.apply(authorize(true, AuthorizationKind::Eim), now());
            let id = start_id(&opened);

            let effects = core.apply(
                Event::EffectDone {
                    id: Some(id),
                    outcome: EffectOutcome::Failed("meter busy".to_owned()),
                },
                now(),
            );

            let raised = effects
                .iter()
                .find_map(|effect| match effect {
                    Effect::RaiseError(report) if raises_start_failure(effect) => Some(report),
                    _ => None,
                })
                .expect("the refusal is raised");
            assert!(
                raised.description.contains("meter busy"),
                "got {:?}",
                raised.description
            );
            // `ErrorHandling.cpp:355-357` raises it at medium severity. The
            // fault set reads severity to pick the shutdown class, so high here
            // would drive the emergency one for a billing refusal.
            assert_eq!(raised.severity, Severity::Medium);
        }

        /// `fail_on_powermeter_errors` off is the deployment that bills best
        /// effort: the C++ logs and charges on (`Charger.cpp:1428`).
        #[test]
        fn a_refused_transaction_start_is_tolerated_when_not_configured_to_fail() {
            let mut core = core_with_metering(Metering {
                fail_on_errors: false,
            });
            let opened = core.apply(authorize(true, AuthorizationKind::Eim), now());
            let id = start_id(&opened);

            let effects = core.apply(
                Event::EffectDone {
                    id: Some(id),
                    outcome: EffectOutcome::Failed("meter busy".to_owned()),
                },
                now(),
            );

            assert!(!effects.iter().any(raises_start_failure), "got {effects:?}");
            assert!(!core.faults.prevents_charging());
        }

        #[test]
        fn an_accepted_transaction_start_raises_nothing() {
            let mut core = core_with_metering(Metering {
                fail_on_errors: true,
            });
            let opened = core.apply(authorize(true, AuthorizationKind::Eim), now());
            let id = start_id(&opened);

            let effects = core.apply(
                Event::EffectDone {
                    id: Some(id),
                    outcome: EffectOutcome::Ok,
                },
                now(),
            );

            assert!(!effects.iter().any(raises_start_failure), "got {effects:?}");
        }

        /// A completion carrying an identity the core is not awaiting belongs to
        /// something else. Without the check any failed effect at all would read
        /// as a refused billing start.
        ///
        /// The two shapes that are not the awaited one: no identity at all, and
        /// an identity the power path allocated out of the same space. The
        /// second is the runtime half of what `Issued<ByCore>` makes a compile
        /// error, and it is a live identity rather than a written number.
        #[test]
        fn a_failure_the_core_is_not_awaiting_raises_nothing() {
            let (mut core, path_ids, _) = core_recording_path_identities();
            core.metering = Metering {
                fail_on_errors: true,
            };
            let opened = core.apply(authorize(true, AuthorizationKind::Eim), now());
            // Not vacuous: there is a real await outstanding, and neither of
            // the two shapes below is it.
            let _ = start_id(&opened);

            for id in [None, Some(path_identity(&path_ids))] {
                let effects = core.apply(
                    Event::EffectDone {
                        id,
                        outcome: EffectOutcome::Failed("something else".to_owned()),
                    },
                    now(),
                );
                assert!(!effects.iter().any(raises_start_failure), "got {effects:?}");
            }
        }

        /// The verdict is consumed once. A second delivery of the same identity
        /// is not a second refusal.
        #[test]
        fn the_awaited_identity_is_answered_only_once() {
            let mut core = core_with_metering(Metering {
                fail_on_errors: true,
            });
            let opened = core.apply(authorize(true, AuthorizationKind::Eim), now());
            let id = start_id(&opened);

            core.apply(
                Event::EffectDone {
                    id: Some(id),
                    outcome: EffectOutcome::Ok,
                },
                now(),
            );
            let again = core.apply(
                Event::EffectDone {
                    id: Some(id),
                    outcome: EffectOutcome::Failed("meter busy".to_owned()),
                },
                now(),
            );

            assert!(!again.iter().any(raises_start_failure), "got {again:?}");
        }

        /// Each start is awaited under its own identity, so a verdict on an
        /// abandoned start cannot answer the one that replaced it.
        #[test]
        fn a_second_transaction_start_is_awaited_under_a_new_identity() {
            let mut core = core_with_metering(Metering {
                fail_on_errors: true,
            });
            core.apply(Event::Startup, now());
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
            let first = start_id(&core.apply(authorize(true, AuthorizationKind::Eim), now()));
            core.apply(unplug(), now());
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
            let second = start_id(&core.apply(authorize(true, AuthorizationKind::Eim), now()));

            assert_ne!(first, second);

            // And the abandoned one, delivered while the replacement is the
            // one awaited. `a_start_verdict_arriving_after_the_session_ended`
            // above delivers a stale identity into a slot that is empty; this
            // is the other shape, a stale identity against an occupied slot,
            // which is what monotonic rather than one-constant-per-purpose
            // buys. The path side has had this test since the port
            // (`a_verdict_from_an_earlier_cable_check_does_not_advance_a_later_one`).
            let late = core.apply(
                Event::EffectDone {
                    id: Some(first),
                    outcome: EffectOutcome::Failed("meter busy".to_owned()),
                },
                now(),
            );
            assert!(
                !late.iter().any(raises_start_failure),
                "an abandoned start's refusal answered the one that replaced it, got {late:?}"
            );
        }

        /// The core answers its own await and forwards nothing else. A power
        /// path is the only other correlator, so a completion the core is not
        /// awaiting has to reach it: swallowing it would strand every stage
        /// that waits on an effect.
        #[test]
        fn a_completion_the_core_does_not_answer_reaches_the_power_path() {
            // Both shapes that are not the core's own: an identity the power
            // path allocated, and no identity at all. The `Failed` halves of
            // the pair are in `a_failure_the_core_is_not_awaiting_raises_nothing`
            // above; these are the accepted ones, where there is no raise to
            // look for and forwarding is the only observable.
            //
            // The billing start is opened first in both, so the core's slot is
            // occupied rather than empty. An empty slot forwards everything
            // through the `?` and would make either case pass without the
            // identity having been compared at all.
            for nameless in [true, false] {
                let (mut core, path_ids, calls) = core_recording_path_identities();
                let _ = start_id(&core.apply(authorize(true, AuthorizationKind::Eim), now()));
                calls.lock().unwrap().clear();

                let id = if nameless {
                    None
                } else {
                    Some(path_identity(&path_ids))
                };
                core.apply(
                    Event::EffectDone {
                        id,
                        outcome: EffectOutcome::Ok,
                    },
                    now(),
                );

                let calls_now = calls.lock().unwrap().clone();
                assert!(
                    calls_now.contains(&"on_effect_done"),
                    "{id:?} was not forwarded, got {calls_now:?}"
                );
            }
        }

        /// A verdict the core has already answered, delivered a second time.
        ///
        /// The slot is cleared by the first delivery, so the second one is no
        /// longer the core's and takes the other fork: it reaches the path,
        /// like any completion the core is not awaiting. Nothing there matches
        /// it, which is the point, but the routing itself is a cell in its own
        /// right and nothing drove it.
        #[test]
        fn a_second_delivery_of_an_answered_verdict_reaches_the_power_path() {
            let (mut core, _, calls) = core_recording_path_identities();
            let id = start_id(&core.apply(authorize(true, AuthorizationKind::Eim), now()));
            calls.lock().unwrap().clear();

            let done = Event::EffectDone {
                id: Some(id),
                outcome: EffectOutcome::Ok,
            };
            core.apply(done.clone(), now());
            let answered = calls.lock().unwrap().clone();
            assert!(
                !answered.contains(&"on_effect_done"),
                "the first delivery is the core's own, got {answered:?}"
            );

            core.apply(done, now());
            let again = calls.lock().unwrap().clone();
            assert!(
                again.contains(&"on_effect_done"),
                "the second delivery is nobody's and must be forwarded, got {again:?}"
            );
        }

        /// The same for a refused verdict, which leaves
        /// `answer_transaction_start` by the other exit: the raise into the
        /// fault set rather than the persist. Both exits return `Some`, so both
        /// consume the completion, and only the accepted one was driven.
        ///
        /// It matters more here than for an accepted verdict: a refusal handed
        /// down as well would reach `on_effect_done` as
        /// `EffectOutcome::Failed`, which is the input every path treats as its
        /// own stage failing. A billing refusal would then abort a cable check
        /// that had nothing to do with the meter.
        #[test]
        fn a_refused_transaction_start_verdict_does_not_reach_the_power_path() {
            let (mut core, _, calls) = core_recording_path_identities();
            core.metering = Metering {
                fail_on_errors: true,
            };
            let id = start_id(&core.apply(authorize(true, AuthorizationKind::Eim), now()));
            calls.lock().unwrap().clear();

            let refused = core.apply(
                Event::EffectDone {
                    id: Some(id),
                    outcome: EffectOutcome::Failed("meter busy".to_owned()),
                },
                now(),
            );

            assert!(
                refused.iter().any(raises_start_failure),
                "the core answers its own refusal, got {refused:?}"
            );
            let calls_now = calls.lock().unwrap().clone();
            assert!(
                !calls_now.contains(&"on_effect_done"),
                "a billing refusal is not a path's stage failing, got {calls_now:?}"
            );
        }

        /// The other half: the verdict the core does answer is consumed there
        /// and not also handed down, so no path sees a billing verdict.
        #[test]
        fn the_transaction_start_verdict_does_not_reach_the_power_path() {
            let (mut core, _, calls) = core_recording_path();
            let opened = core.apply(authorize(true, AuthorizationKind::Eim), now());
            let id = start_id(&opened);
            calls.lock().unwrap().clear();

            core.apply(
                Event::EffectDone {
                    id: Some(id),
                    outcome: EffectOutcome::Ok,
                },
                now(),
            );

            let calls_now = calls.lock().unwrap().clone();
            assert!(
                !calls_now.contains(&"on_effect_done"),
                "got {:?}",
                calls_now
            );
        }

        /// `Charger::clear_errors_on_unplug` clears it along with the rest
        /// (`Charger.cpp:2289`).
        #[test]
        fn an_unplug_clears_the_refusal() {
            let mut core = core_with_metering(Metering {
                fail_on_errors: true,
            });
            let opened = core.apply(authorize(true, AuthorizationKind::Eim), now());
            let id = start_id(&opened);
            core.apply(
                Event::EffectDone {
                    id: Some(id),
                    outcome: EffectOutcome::Failed("meter busy".to_owned()),
                },
                now(),
            );

            let effects = core.apply(unplug(), now());

            assert!(
                effects.iter().any(|effect| matches!(
                    effect,
                    Effect::ClearError(report)
                        if report.error_type == faults::POWERMETER_TRANSACTION_START_FAILED
                )),
                "got {effects:?}"
            );
            assert!(!core.faults.prevents_charging());
        }
    }

    /// One id space, proven where two allocators used to meet.
    ///
    /// `Core` awaits the metering transaction start and the DC path awaits the
    /// isolation monitor self test, and `Core::apply` offers every completion
    /// to its own await before the path's. While the two identities came from
    /// separate allocators they both began at zero, so on the first session of
    /// a port's life one completion could answer either await.
    ///
    /// The overlap is structural rather than a race: `StartTransaction` is
    /// emitted when authorization is accepted and the cable check runs inside
    /// `PrepareCharging`, so any powermeter slower than the ramp to the
    /// isolation voltage is still owed a verdict when the self test is asked
    /// for.
    ///
    /// What is asserted here is the safety property and not just the identity
    /// arithmetic: an isolation self test is a safety gate, and a gate that a
    /// powermeter reply can pass is a gate that nothing measured.
    /// What the three payload carrying session events name, and what they
    /// deliberately do not.
    ///
    /// The port published all three as bare events until this: no meter value,
    /// no identity, no reservation and no stop reason. Measured against the
    /// real OCPP suite that was one cause behind 240 of 282 regressions, and it
    /// was not one degradation but three, because the consumers disagree about
    /// what an absent payload is. Legacy OCPP 1.6 reads
    /// `session_event.transaction_started.value()` and the module process
    /// terminates on `std::bad_optional_access`; `OCPP201` throws a
    /// `std::runtime_error`; `OCPPmulti` logs and skips, so the transaction
    /// never starts. `types/evse_manager.yaml` documents the field as "data for
    /// TransactionStarted event", so all three readings are fair.
    mod what_the_transaction_lifecycle_names {
        use super::*;

        /// A session reserved under `id`, with nothing plugged in yet.
        fn reserved(id: i64) -> Core {
            let mut core = core();
            core.apply(Event::Startup, now());
            core.apply(Event::Command(Command::Reserve { reservation_id: id }), now());
            core
        }

        /// The payload one published event carries, or a panic saying it
        /// carried none.
        fn payload(effects: &[Effect], event: SessionEvent) -> SessionPayload {
            report_for(effects, event)
                .payload
                .unwrap_or_else(|| panic!("{event:?} carried no payload, got {effects:?}"))
        }

        /// The `TransactionStarted` payload as the identity's value, whether it
        /// is a contract, and the reservation it names.
        fn started(effects: &[Effect]) -> (String, bool, Option<i64>) {
            match payload(effects, SessionEvent::TransactionStarted) {
                SessionPayload::TransactionStarted {
                    id_tag,
                    reservation_id,
                } => (
                    id_tag.value().to_owned(),
                    id_tag.is_plug_and_charge(),
                    reservation_id,
                ),
                other => panic!("a transaction start carried {other:?}"),
            }
        }

        /// The `SessionStarted` payload as the reservation and identity it
        /// names.
        fn session_started(effects: &[Effect]) -> (Option<i64>, Option<String>) {
            match payload(effects, SessionEvent::SessionStarted) {
                SessionPayload::Started {
                    reservation_id,
                    id_tag,
                } => (
                    reservation_id,
                    id_tag.map(|tag| tag.value().to_owned()),
                ),
                other => panic!("a session start carried {other:?}"),
            }
        }

        /// The `TransactionFinished` payload as the reason and the identity that
        /// stopped it.
        fn finished(effects: &[Effect]) -> (StopTransactionReason, Option<String>) {
            match payload(effects, SessionEvent::TransactionFinished) {
                SessionPayload::TransactionFinished { reason, id_tag } => {
                    (reason, id_tag.map(|tag| tag.value().to_owned()))
                }
                other => panic!("a transaction finish carried {other:?}"),
            }
        }

        /// The plug in first drive: a session that reaches a live transaction
        /// with the vehicle connected before the authorization arrives.
        fn charged(core: &mut Core, token: &str, plug_and_charge: bool) -> Vec<Effect> {
            core.apply(plug_in(), now());
            core.apply(
                Event::Command(Command::AuthorizeResponse {
                    token: id_tag_for_tests(token, plug_and_charge),
                    status: AuthorizationStatus::Accepted,
                    certificate: None,
                    tariff: TariffMessages::default(),
                    reservation_id: None,
                }),
                now(),
            )
        }

        #[test]
        fn a_transaction_names_the_identity_that_authorized_it() {
            let mut core = core();
            core.apply(Event::Startup, now());

            let effects = charged(&mut core, "DEADBEEF", false);

            assert_eq!(
                started(&effects),
                ("DEADBEEF".to_owned(), false, None),
                "got {effects:?}"
            );
        }

        /// The two announcements a record closure makes are not one event with a
        /// synonym: the charge finished announcement is bare and the
        /// transaction finished one behind it carries the reason and the
        /// identity that stopped it.
        ///
        /// `Charger::stop_transaction` raises them on two different signals
        /// (`Charger.cpp:1548-1550`), and only the second one's subscriber
        /// fills a payload. A port that gave the first the second's payload
        /// would publish the stop reason twice and would pass any assertion
        /// written on the sequence alone.
        #[test]
        fn a_charge_finished_announcement_is_bare_and_the_record_closure_is_not() {
            let mut core = core();
            core.apply(Event::Startup, now());
            charged(&mut core, "DEADBEEF", false);

            let effects = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::A)), now());

            assert_eq!(
                report_for(&effects, SessionEvent::ChargingFinished).payload,
                None,
                "got {effects:?}"
            );
            assert_eq!(
                finished(&effects),
                (StopTransactionReason::EvDisconnected, None),
                "got {effects:?}"
            );
        }

        /// The identity both announcements name, which is the session's and not
        /// the empty string a report built outside `session_event` would carry.
        #[test]
        fn both_closure_announcements_name_the_session_that_is_closing() {
            let mut core = core();
            core.apply(Event::Startup, now());
            charged(&mut core, "DEADBEEF", false);

            let effects = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::A)), now());

            let uuid = report_for(&effects, SessionEvent::TransactionFinished)
                .uuid
                .clone();
            assert!(!uuid.is_empty(), "got {effects:?}");
            assert_eq!(
                report_for(&effects, SessionEvent::ChargingFinished).uuid,
                uuid
            );
        }

        /// The whole reason the identity is carried rather than reduced to its
        /// value: `OCPP201::process_transaction_started` reads the declared
        /// authorization type to decide the trigger reason, and the contract
        /// half of that decision is invisible in the value alone.
        #[test]
        fn a_contract_reaches_the_payload_as_a_contract() {
            let mut core = core();
            core.apply(Event::Startup, now());

            let effects = charged(&mut core, "EMAID", true);

            assert_eq!(
                started(&effects),
                ("EMAID".to_owned(), true, None),
                "got {effects:?}"
            );
        }

        /// The plug in first order. `SessionStarted` names the reservation
        /// without consuming it, because the C++ consumes only when the start
        /// reason is `Authorized`, and the transaction behind it gets it.
        #[test]
        fn a_reservation_reaches_the_transaction_a_plug_in_first_session_opens() {
            let mut core = reserved(7);

            let opened = core.apply(plug_in(), now());
            assert_eq!(
                session_started(&opened),
                (Some(7), None),
                "the start names it and does not consume it, got {opened:?}"
            );

            let effects = core.apply(
                Event::Command(Command::AuthorizeResponse {
                    token: id_tag_for_tests("DEADBEEF", false),
                    status: AuthorizationStatus::Accepted,
                    certificate: None,
                    tariff: TariffMessages::default(),
                    reservation_id: None,
                }),
                now(),
            );

            assert_eq!(
                started(&effects),
                ("DEADBEEF".to_owned(), false, Some(7)),
                "got {effects:?}"
            );
        }

        /// The authorization first order, which is the other half and not the
        /// same answer. `evse/evse_managerImpl.cpp`'s session started
        /// connection cancels the reservation on an `Authorized` start, so the
        /// `TransactionStarted` published a moment later finds none. Both
        /// consumptions are silent: the C++ passes `signal_event = false`, and
        /// its comment at the transaction start says why - "this allows OCPP1.6
        /// to not move back to available".
        #[test]
        fn an_authorization_first_session_spends_the_reservation_on_the_start() {
            let mut core = reserved(7);

            let effects = core.apply(
                Event::Command(Command::AuthorizeResponse {
                    token: id_tag_for_tests("DEADBEEF", false),
                    status: AuthorizationStatus::Accepted,
                    certificate: None,
                    tariff: TariffMessages::default(),
                    reservation_id: None,
                }),
                now(),
            );

            assert_eq!(
                session_started(&effects),
                (Some(7), Some("DEADBEEF".to_owned())),
                "the start names both, got {effects:?}"
            );

            // The "moment later" is the vehicle arriving:
            // `Charger::start_transaction` is reached from
            // `WaitingForAuthentication`, so the announcement waits for the
            // plug in and finds the reservation already spent when it lands.
            let plugged = core.apply(plug_in(), now());

            assert_eq!(
                started(&plugged).2,
                None,
                "the transaction behind it finds none, got {plugged:?}"
            );
            for pass in [&effects, &plugged] {
                assert!(
                    !pass
                        .iter()
                        .any(|effect| published(effect, SessionEvent::ReservationEnd)),
                    "neither consumption announces, got {pass:?}"
                );
            }
        }

        /// A plug in first start names no identity even when one is already
        /// held: `Charger::start_session` hands the signal a token from its
        /// `authfirst` branch alone.
        #[test]
        fn a_plug_in_first_start_names_no_identity() {
            let mut core = core();
            core.apply(Event::Startup, now());

            let opened = core.apply(plug_in(), now());

            assert_eq!(session_started(&opened), (None, None), "got {opened:?}");
        }

        /// The `authfirst` condition itself, driven directly.
        ///
        /// `Charger::start_session` hands the signal a token from its
        /// `authfirst` branch alone, so a plug in first start names none even
        /// with one held. No production route reaches that pair - a plug in
        /// first start happens before any authorization, and `end_session`
        /// lowers the identity when the session before it ended - so the rule
        /// is driven at the one function that decides it rather than through a
        /// session drive that cannot produce the state. Removing the condition
        /// survived every other test in this module.
        #[test]
        fn a_start_names_a_held_identity_only_when_the_authorization_opened_it() {
            let mut core = core();
            core.session.authorized_token = Some(id_tag_for_tests("HELD", false));

            core.session.last_start_reason = StartSessionReason::EvConnected;
            assert!(
                matches!(
                    core.session_payload(SessionEvent::SessionStarted),
                    Some(SessionPayload::Started { id_tag: None, .. })
                ),
                "a plug in first start names no identity even holding one"
            );

            core.session.last_start_reason = StartSessionReason::Authorized;
            let named = match core.session_payload(SessionEvent::SessionStarted) {
                Some(SessionPayload::Started { id_tag, .. }) => id_tag,
                other => panic!("a session start carried {other:?}"),
            };
            assert_eq!(
                named.map(|tag| tag.value().to_owned()),
                Some("HELD".to_owned()),
                "an authorization first start names the token that opened it"
            );
        }

        /// A transaction start with no identity to name, which is the one cell
        /// of the payload cross product that has no honest answer.
        ///
        /// `TransactionStarted.id_tag` is required by the wire type, so there
        /// is no absent form to publish and a default would be a token nobody
        /// presented. The whole payload goes instead, which returns the module
        /// to the behaviour that cost 240 tests - so it is a logged broken
        /// invariant rather than a supported shape, and unreachable in
        /// production: the only route to this event is `start_transaction`,
        /// which a power path raises only from a state it entered because the
        /// authorization was already held, and `mirror_authorization` assigns
        /// the identity before those signals are walked. Driven at the deciding
        /// function for that reason, the way the `authfirst` condition above
        /// is.
        #[test]
        fn a_transaction_start_with_no_identity_publishes_no_payload() {
            let mut core = core();
            core.session.authorized_token = None;

            assert!(
                core.session_payload(SessionEvent::TransactionStarted).is_none(),
                "an identity that cannot be named must not become a default"
            );
        }

        /// The reservation is not spent by a payload that was not published.
        #[test]
        fn a_refused_transaction_payload_keeps_the_reservation() {
            let mut core = core();
            core.session.reservation_id = Some(7);
            core.session.authorized_token = None;

            core.session_payload(SessionEvent::TransactionStarted);

            assert_eq!(core.session.reservation_id, Some(7));
        }

        /// The unnamed stop, which is the ordinary unplug: "if the stop
        /// transaction reason was already set (e.g. by cancel_transaction), we
        /// keep it, else we know it is EVDisconnected" (`Charger.cpp:1523`).
        #[test]
        fn an_unnamed_stop_is_an_unplug() {
            let mut core = core();
            core.apply(Event::Startup, now());
            charged(&mut core, "DEADBEEF", false);

            let effects = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::A)), now());

            assert_eq!(
                finished(&effects),
                (StopTransactionReason::EvDisconnected, None),
                "got {effects:?}"
            );
        }

        /// Every one of the twenty three, because the narrowing the power paths
        /// read is many to one and cannot be inverted: a reason rebuilt from it
        /// would report `Local` for a `SOCLimitReached` and `Remote` for a
        /// `HardReset`. This is the cell that would catch that.
        #[test]
        fn every_requested_reason_reaches_the_payload_as_itself() {
            for reason in EVERY_STOP_REASON {
                let mut core = core();
                core.apply(Event::Startup, now());
                charged(&mut core, "DEADBEEF", false);

                let effects = core.apply(
                    Event::Command(Command::StopTransaction {
                        reason: *reason,
                        id_tag: Some(id_tag_for_tests("STOPPER", false)),
                    }),
                    now(),
                );

                assert_eq!(
                    finished(&effects),
                    (*reason, Some("STOPPER".to_owned())),
                    "{reason:?} did not reach the payload, got {effects:?}"
                );
            }
        }

        /// A stop request carrying no token names none, which is what the field
        /// means: "only present if transaction was stopped locally".
        #[test]
        fn a_remote_stop_names_no_identity() {
            let mut core = core();
            core.apply(Event::Startup, now());
            charged(&mut core, "DEADBEEF", false);

            let effects = core.apply(
                Event::Command(Command::StopTransaction {
                    reason: StopTransactionReason::Remote,
                    id_tag: None,
                }),
                now(),
            );

            assert_eq!(
                finished(&effects),
                (StopTransactionReason::Remote, None),
                "got {effects:?}"
            );
        }

        /// `Charger::enable_disable` records `EVSEDisabled` beside arming the
        /// teardown (`Charger.cpp:1897`), which is the one reason no stop
        /// request carries.
        #[test]
        fn a_disable_driven_stop_names_itself() {
            let mut core = core();
            core.apply(Event::Startup, now());
            charged(&mut core, "DEADBEEF", false);

            let effects = core.apply(
                Event::Command(Command::EnableDisable {
                    source: EnableSource::LocalApi,
                    state: EnableState::Disable,
                    priority: 1,
                    scope: EnableScope::Connector,
                }),
                now(),
            );

            assert_eq!(
                finished(&effects).0,
                StopTransactionReason::EvseDisabled,
                "got {effects:?}"
            );
        }

        /// `Charger::cancel_transaction` records the pair inside its
        /// `flag_transaction_active` guard, so a stop that finds no record open
        /// names nothing - and the transaction that opens later must not end
        /// under it.
        #[test]
        fn a_stop_with_no_open_transaction_does_not_name_the_next_one() {
            let mut core = core();
            core.apply(Event::Startup, now());
            core.apply(plug_in(), now());
            assert!(!core.session.transaction_active, "no record is open");

            core.apply(
                Event::Command(Command::StopTransaction {
                    reason: StopTransactionReason::MasterPass,
                    id_tag: None,
                }),
                now(),
            );
            // The refused stop changed nothing at all, so the session it
            // arrived in is still able to authorize and the transaction that
            // opens next ends under its own reason rather than under this one.
            let effects = charged(&mut core, "DEADBEEF", false);
            assert!(
                core.session.transaction_active,
                "the refused stop left the session able to open a record, got {effects:?}"
            );
            let effects = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::A)), now());

            assert_eq!(
                finished(&effects).0,
                StopTransactionReason::EvDisconnected,
                "got {effects:?}"
            );
        }

        /// The `flag_transaction_active` guard itself, driven at the state it
        /// writes.
        ///
        /// `Charger::cancel_transaction` records the reason inside
        /// `if (shared_context.flag_transaction_active)`
        /// (`Charger.cpp:1431-1445`), so a stop request that finds no record
        /// open records nothing. Asserted on the state rather than on a
        /// payload, because it has no payload consequence: the reset at the
        /// next transaction start would clear a reason recorded here anyway, so
        /// removing the guard survives every event this module publishes. It is
        /// kept because the C++ keeps it and because the one route that opens a
        /// transaction without reaching that reset - an authorized session
        /// holding no identity, which logs a broken invariant - is exactly
        /// where a stale reason would leak.
        #[test]
        fn a_stop_request_with_no_record_open_records_no_reason() {
            let mut core = core();
            core.apply(Event::Startup, now());
            core.apply(plug_in(), now());
            assert!(!core.session.transaction_active, "no record is open");

            core.apply(
                Event::Command(Command::StopTransaction {
                    reason: StopTransactionReason::MasterPass,
                    id_tag: Some(id_tag_for_tests("STOPPER", false)),
                }),
                now(),
            );

            assert_eq!(core.session.stop_reason, None, "the reason was recorded");
            assert_eq!(core.session.stop_token, None, "the token was recorded");
        }

        /// The reset `Charger::start_transaction` performs at its first two
        /// statements, and the shape that makes it load bearing: a disable
        /// records its reason with no regard for whether a transaction is
        /// running, so an idle port's disable would otherwise be the reason the
        /// next vehicle's transaction ends under.
        #[test]
        fn a_disable_on_an_idle_port_does_not_name_the_next_transaction() {
            let mut core = core();
            core.apply(Event::Startup, now());
            core.apply(
                Event::Command(Command::EnableDisable {
                    source: EnableSource::LocalApi,
                    state: EnableState::Disable,
                    priority: 1,
                    scope: EnableScope::Connector,
                }),
                now(),
            );
            // The record really is taken on an idle port, which is what makes
            // the reset the assertion below reads worth having. Without this
            // the test would pass on a port that never recorded one.
            assert_eq!(
                core.session.stop_reason,
                Some(StopTransactionReason::EvseDisabled),
                "an idle disable records its reason, as `Charger.cpp:1897` does"
            );
            core.apply(
                Event::Command(Command::EnableDisable {
                    source: EnableSource::LocalApi,
                    state: EnableState::Enable,
                    priority: 1,
                    scope: EnableScope::Connector,
                }),
                now(),
            );

            charged(&mut core, "DEADBEEF", false);
            let effects = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::A)), now());

            assert_eq!(
                finished(&effects).0,
                StopTransactionReason::EvDisconnected,
                "got {effects:?}"
            );
        }

        /// A finish with no start of its own, which is the whole of the startup
        /// recovery. `Charger.cpp:1577` names `PowerLoss` here and nowhere
        /// else, and it is the one reason a consumer cannot derive: it is how a
        /// CSMS learns the transaction ended because the charger lost power
        /// rather than because a driver unplugged.
        #[test]
        fn a_recovered_transaction_finishes_on_power_loss() {
            let mut core = core();
            core.persist = test_persist(Some("a-previous-session"));

            let effects = core.apply(Event::Startup, now());

            assert_eq!(
                finished(&effects),
                (StopTransactionReason::PowerLoss, None),
                "got {effects:?}"
            );
        }

        /// The other half of "a finish with no start": in a live session with
        /// no record open, `Core::stop_transaction` is guarded on
        /// `transaction_active` and publishes nothing at all, rather than a
        /// finish a consumer would pair with a transaction that never started.
        #[test]
        fn an_unplug_with_no_record_open_finishes_nothing() {
            let mut core = core();
            core.apply(Event::Startup, now());
            core.apply(plug_in(), now());
            assert!(!core.session.transaction_active, "no record is open");

            let effects = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::A)), now());

            assert!(
                !effects
                    .iter()
                    .any(|effect| published(effect, SessionEvent::TransactionFinished)),
                "got {effects:?}"
            );
        }

        /// The events that carry nothing carry nothing, driven as the whole
        /// list rather than as the four that do. A payload attached to the
        /// wrong event is the defect this catches: `OCPPmulti`'s
        /// `get_meter_value` dispatches on the event and reads whichever
        /// payload that event names, so a `ChargingStarted` carrying a
        /// transaction payload would be read as a charging state change with
        /// none.
        #[test]
        fn no_other_session_event_carries_a_payload() {
            let carriers = [
                SessionEvent::SessionStarted,
                SessionEvent::TransactionStarted,
                SessionEvent::TransactionFinished,
                SessionEvent::ChargingPausedEvse,
            ];
            let mut core = core();
            core.apply(Event::Startup, now());

            let mut seen = 0;
            for event in EVERY_SESSION_EVENT {
                if carriers.contains(event) {
                    continue;
                }
                seen += 1;
                assert!(
                    core.session_payload(*event).is_none(),
                    "{event:?} carries a payload"
                );
            }
            assert_eq!(seen, EVERY_SESSION_EVENT.len() - carriers.len());
        }

        /// The meter is asked before the transaction is announced, and the
        /// announcement is what carries the answer.
        ///
        /// `Charger::start_transaction` calls the meter and signals afterwards
        /// (`Charger.cpp:1497-1513`), and `evse/evse_managerImpl.cpp:207` then
        /// reads `get_start_signed_meter_value()` onto the payload it builds.
        /// This port announced first and asked afterwards, so the field could
        /// only ever be `None` however the boundary rendered it - the whole of
        /// what the OCPP suite saw as a `TransactionEvent(Started)` with no
        /// `Transaction.Begin` signature.
        ///
        /// Asserted by index rather than by containment, because containment
        /// is exactly what the broken shape satisfied. Both effects are
        /// `ExecContext::Publish`, which is what makes this index the order
        /// they run in; `effect::tests::every_metering_transaction_command_shares_the_publish_lane`
        /// holds that half.
        #[test]
        fn the_meter_is_asked_before_the_transaction_is_announced() {
            let mut core = core();
            core.apply(Event::Startup, now());

            let effects = charged(&mut core, "DEADBEEF", false);

            let asked = index_of(&effects, starts_transaction);
            let announced = index_of(&effects, |effect| {
                matches!(
                    effect,
                    Effect::PublishSessionEvent(report)
                        if report.event == SessionEvent::TransactionStarted
                )
            });
            assert!(
                asked < announced,
                "the payload is built before the meter replied, got {effects:?}"
            );
        }

        /// And the announcement still precedes the state event behind it.
        ///
        /// The C++ signals from inside `start_transaction`, which both call
        /// sites run before assigning `PrepareCharging` (`Charger.cpp:395` and
        /// `:524`), so `TransactionStarted` reaches the wire first.
        /// `path::duties_for_edge` encodes that and this is the end to end
        /// check that moving the request ahead of the announcement did not
        /// carry the announcement past the state event with it.
        #[test]
        fn the_transaction_is_announced_before_the_state_it_opens() {
            let mut core = core();
            core.apply(Event::Startup, now());

            let effects = charged(&mut core, "DEADBEEF", false);

            let published = published_events(&effects);
            let announced = published
                .iter()
                .position(|event| *event == SessionEvent::TransactionStarted)
                .unwrap_or_else(|| panic!("no transaction announced, got {published:?}"));
            let prepared = published
                .iter()
                .position(|event| *event == SessionEvent::PrepareCharging)
                .unwrap_or_else(|| panic!("no preparation announced, got {published:?}"));
            assert!(
                announced < prepared,
                "the pair reached the wire out of order, got {published:?}"
            );
        }

        /// A re-validation inside a live session announces nothing, so there
        /// is no second announcement to resurrect a reservation the first one
        /// spent.
        ///
        /// `signal_transaction_started_event` is a statement of
        /// `Charger::start_transaction` (`Charger.cpp:1515`), and both call
        /// sites reach that function only under
        /// `if (not shared_context.flag_transaction_active)`
        /// (`Charger.cpp:394` and `:523`). One record, one announcement. This
        /// test used to assert the opposite, from the same reading of the C++
        /// that put the announcement on the authorization signal.
        #[test]
        fn a_second_authorization_announces_no_second_transaction() {
            let mut core = reserved(7);
            let first = charged(&mut core, "DEADBEEF", false);
            assert_eq!(started(&first).2, Some(7), "got {first:?}");

            let second = core.apply(
                Event::Command(Command::AuthorizeResponse {
                    token: id_tag_for_tests("SECOND", false),
                    status: AuthorizationStatus::Accepted,
                    certificate: None,
                    tariff: TariffMessages::default(),
                    reservation_id: None,
                }),
                now(),
            );

            assert!(
                !published_events(&second).contains(&SessionEvent::TransactionStarted),
                "the record is already open, got {second:?}"
            );
            assert!(
                !second.iter().any(starts_transaction),
                "and the meter is not asked twice, got {second:?}"
            );
        }
    }

    mod effect_identity {
        use super::*;

        /// The awaited identity of the metering transaction start in `effects`.
        fn start_id(effects: &[Effect]) -> EffectId {
            effects
                .iter()
                .find_map(|effect| match effect {
                    Effect::StartTransaction { id, .. } => Some(*id),
                    _ => None,
                })
                .expect("an accepted authorization starts the metering transaction")
        }

        /// The awaited identity of the isolation monitor self test in `effects`.
        fn self_test_id(effects: &[Effect]) -> EffectId {
            effects
                .iter()
                .find_map(|effect| match effect {
                    Effect::ImdSelfTest { id, .. } => Some(*id),
                    _ => None,
                })
                .expect("the cable check asks for a self test at the isolation voltage")
        }

        /// Starting the isolation monitor is what passing the self test buys,
        /// so it is the observable that says which await a completion answered.
        fn starts_the_isolation_monitor(effect: &Effect) -> bool {
            matches!(effect, Effect::ImdStart)
        }

        /// A DC port ramped to the isolation voltage with the billing start
        /// still outstanding, which is the state both awaits are live in.
        ///
        /// Returns the core, the identity the powermeter owes a verdict on, and
        /// the identity the self test is waiting for. The meter is deliberately
        /// never answered inside this fixture.
        fn dc_port_awaiting_a_meter_and_a_self_test() -> (Core, EffectId, EffectId) {
            let mut core = dc_core();
            core.apply(Event::Startup, now());
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
            let authorized = core.apply(authorize(true, AuthorizationKind::Eim), now());

            core.apply(Event::Hlc(HlcEvent::RequiresCableCheck), now());
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::PowerOn)), now());
            let energized = core.apply(
                Event::SupplyVoltageCurrent {
                    voltage_v: 500.0,
                    current_a: 0.5,
                },
                now(),
            );

            (core, start_id(&authorized), self_test_id(&energized))
        }

        /// The identity arithmetic, which is necessary and on its own not
        /// enough: a space that is unique today collides again the moment a
        /// second allocator appears. The two tests below are the ones that
        /// would survive that.
        #[test]
        fn a_billing_start_and_a_self_test_never_share_an_identity() {
            let (_core, meter, self_test) = dc_port_awaiting_a_meter_and_a_self_test();

            assert_ne!(
                meter, self_test,
                "the billing await and a safety gate drew the same identity"
            );
        }

        /// The verdict on the self test reaches the path that asked for it even
        /// while a billing start is outstanding, so the isolation monitor is
        /// started by the isolation self test and by nothing else.
        #[test]
        fn a_self_test_verdict_reaches_the_path_while_billing_is_outstanding() {
            let (mut core, _meter, self_test) = dc_port_awaiting_a_meter_and_a_self_test();

            // The monitor takes the request, which advances nothing on its own,
            // and then publishes the verdict the stage is waiting for.
            let accepted = core.apply(
                Event::EffectDone {
                    id: Some(self_test),
                    outcome: EffectOutcome::Ok,
                },
                now(),
            );
            assert!(
                !accepted.iter().any(starts_the_isolation_monitor),
                "the command completing is not the verdict, got {accepted:?}"
            );

            let effects = core.apply(Event::IsolationSelfTest(true), now());

            assert!(
                effects.iter().any(starts_the_isolation_monitor),
                "the self test verdict did not advance the cable check, got {effects:?}"
            );
        }

        /// The safety property, driven in the ordering that produced the
        /// defect: the meter answers last, after the stage it never belonged to
        /// has already run. Its reply owes a billing record and nothing else.
        ///
        /// An isolation monitor started on this completion is one started on
        /// the strength of a reply that never measured isolation.
        #[test]
        fn a_powermeter_completion_never_satisfies_an_isolation_self_test() {
            let (mut core, meter, self_test) = dc_port_awaiting_a_meter_and_a_self_test();

            core.apply(
                Event::EffectDone {
                    id: Some(self_test),
                    outcome: EffectOutcome::Ok,
                },
                now(),
            );
            for _ in 0..3 {
                core.apply(
                    Event::Isolation(IsolationReading { resistance_ohm: 500_000.0, ..Default::default() }),
                    now(),
                );
            }
            core.apply(
                Event::SupplyVoltageCurrent {
                    voltage_v: 10.0,
                    current_a: 0.0,
                },
                now(),
            );

            let effects = core.apply(
                Event::EffectDone {
                    id: Some(meter),
                    outcome: EffectOutcome::Ok,
                },
                now(),
            );

            assert!(
                !effects.iter().any(starts_the_isolation_monitor),
                "a powermeter reply started the isolation monitor, got {effects:?}"
            );
        }
    }

    /// Session persistence and startup recovery, ported from `PersistentStore`
    /// and the two lifecycle points that write it.
    ///
    /// What is asserted here is WHEN the record is written and removed, because
    /// a store at the wrong lifecycle point recovers the wrong thing: one taken
    /// at the session start would recover a session that was never billed, and
    /// one taken next to the metering request would recover a transaction the
    /// meter refused to open.
    mod session_persistence {
        use super::*;

        fn core_recovering(recovered: Option<&str>) -> Core {
            let mut core = core();
            core.persist = test_persist(recovered);
            core
        }

        /// With a vehicle connected, for the reason `metering`'s own fixture
        /// gives: the authorization each test sends opens the record only from
        /// `WaitingForAuthentication`.
        fn core_recovering_with_metering(metering: Metering) -> Core {
            let mut core = core();
            core.metering = metering;
            core.apply(Event::Startup, now());
            core.apply(plug_in(), now());
            core
        }

        /// A core whose transaction has been closed while its session is still
        /// alive and its metering start verdict has not arrived.
        ///
        /// The route is the one `the_relays_opening_after_a_stop_closes_the_billing_record`
        /// in `src/core/path/ac.rs` pins: charging, an external cancel, then the
        /// relays reported open, which enters `AcState::Finished` and raises
        /// `StopTransaction` alone (`path/mod.rs:207`). The session waits there
        /// for the unplug, so nothing has ended it.
        fn core_past_a_close_with_the_start_still_awaited(metering: Metering) -> (Core, EffectId) {
            let mut core = core();
            core.metering = metering;
            core.apply(Event::Startup, now());
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
            let opened = core.apply(authorize(true, AuthorizationKind::Eim), now());
            let id = start_id(&opened);
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::C)), now());
            let closed = core.apply(
                Event::Command(Command::StopTransaction {
                    reason: StopTransactionReason::Local,
                    id_tag: None,
                }),
                now(),
            );
            let closed_too = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::PowerOff)), now());

            // The window is only a window if the transaction closed and the
            // session did not, so both halves are asserted rather than assumed.
            assert!(
                closed
                    .iter()
                    .chain(closed_too.iter())
                    .any(|effect| matches!(effect, Effect::StopTransaction { .. })),
                "the record must be closed: got {closed:?} then {closed_too:?}"
            );
            assert!(!core.session().transaction_active);
            assert!(
                core.session().session_active,
                "the session waits for the unplug"
            );
            (core, id)
        }

        /// Plug in, authorize, draw power, with the metering start accepted so
        /// the record is written. The path leaves its startup state only on the
        /// startup fact, so that comes first.
        fn charging_core() -> (Core, Vec<Effect>) {
            let mut core = core();
            core.apply(Event::Startup, now());
            let mut seen = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
            let opened = core.apply(authorize(true, AuthorizationKind::Eim), now());
            let id = start_id(&opened);
            seen.extend(opened);
            seen.extend(core.apply(
                Event::EffectDone {
                    id: Some(id),
                    outcome: EffectOutcome::Ok,
                },
                now(),
            ));
            seen.extend(core.apply(Event::Bsp(BspEvent::Cp(CpEvent::C)), now()));
            (core, seen)
        }

        /// The awaited identity of the metering transaction start in `effects`.
        fn start_id(effects: &[Effect]) -> EffectId {
            effects
                .iter()
                .find_map(|effect| match effect {
                    Effect::StartTransaction { id, .. } => Some(*id),
                    _ => None,
                })
                .expect("an accepted authorization starts the metering transaction")
        }

        fn persisted(effects: &[Effect]) -> Option<String> {
            effects.iter().find_map(|effect| match effect {
                Effect::Persist { key, value } if *key == test_session_key() => Some(value.clone()),
                _ => None,
            })
        }

        fn clears_the_record(effect: &Effect) -> bool {
            matches!(effect, Effect::PersistDelete { key } if *key == test_session_key())
        }

        /// Drive a core to an open transaction and answer the metering start
        /// with `outcome`, returning what the verdict produced.
        fn transaction_start_answered(core: &mut Core, outcome: EffectOutcome) -> Vec<Effect> {
            let opened = core.apply(authorize(true, AuthorizationKind::Eim), now());
            let id = start_id(&opened);
            assert_eq!(
                persisted(&opened),
                None,
                "the request itself must not persist, only its verdict: got {opened:?}"
            );
            core.apply(
                Event::EffectDone {
                    id: Some(id),
                    outcome,
                },
                now(),
            )
        }

        /// `Charger.cpp:1439`, the last statement of `start_transaction`: the
        /// uuid is written once the meter has accepted the start.
        #[test]
        fn an_accepted_transaction_start_persists_the_session_uuid() {
            let mut core = core_recovering(None);
            core.apply(Event::Startup, now());
            core.apply(plug_in(), now());
            let opened = core.apply(authorize(true, AuthorizationKind::Eim), now());
            let id = start_id(&opened);
            let live = core.session().id.clone().expect("a session is live");

            let effects = core.apply(
                Event::EffectDone {
                    id: Some(id),
                    outcome: EffectOutcome::Ok,
                },
                now(),
            );

            assert_eq!(persisted(&effects).as_deref(), Some(live.as_str()));
        }

        /// `Charger.cpp:1428`: with `fail_on_powermeter_errors` off the refusal
        /// is logged and the charge continues, so the function reaches its store
        /// at `:1439` exactly as an accepted start does.
        #[test]
        fn a_tolerated_meter_refusal_still_persists_the_session() {
            let mut core = core_recovering_with_metering(Metering {
                fail_on_errors: false,
            });

            let effects =
                transaction_start_answered(&mut core, EffectOutcome::Failed("meter busy".into()));

            assert!(persisted(&effects).is_some(), "got {effects:?}");
        }

        /// The load bearing negative. `Charger.cpp:1431` returns BEFORE the
        /// store, so a start the meter refused with `fail_on_powermeter_errors`
        /// on leaves no record. Persisted a moment earlier, next to the
        /// request, the next boot would announce a `PowerLoss` for a
        /// transaction that never opened.
        #[test]
        fn a_refused_transaction_start_persists_nothing() {
            let mut core = core_recovering_with_metering(Metering {
                fail_on_errors: true,
            });

            let effects =
                transaction_start_answered(&mut core, EffectOutcome::Failed("meter busy".into()));

            assert_eq!(persisted(&effects), None, "got {effects:?}");
            assert!(
                !effects.iter().any(clears_the_record),
                "nothing was stored, so nothing is cleared: got {effects:?}"
            );
        }

        /// The other load bearing negative: the record belongs to the
        /// transaction, not the session. `Charger::start_session`
        /// (`Charger.cpp:1376-1390`) mints the uuid and does not store it, so a
        /// vehicle plugged in and never authorized leaves nothing to recover.
        #[test]
        fn a_session_with_no_transaction_persists_nothing() {
            let mut core = core_recovering(None);
            core.apply(Event::Startup, now());

            let effects = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());

            assert_eq!(persisted(&effects), None, "got {effects:?}");
            assert!(core.session().session_active, "a session did open");
            assert!(!core.session().transaction_active);
        }

        /// `Charger.cpp:1471`, in `stop_transaction` and not `stop_session`: a
        /// closed transaction is not one to recover.
        #[test]
        fn closing_the_transaction_removes_the_record() {
            let (mut core, _) = charging_core();

            let effects = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::A)), now());

            assert!(effects.iter().any(clears_the_record), "got {effects:?}");
        }

        /// The C++ order at `Charger.cpp:1454-1474`: the meter is asked to close
        /// the record, then the store is cleared, then the events announce it.
        #[test]
        fn the_record_is_removed_after_the_meter_close_and_before_the_announcement() {
            let (mut core, _) = charging_core();

            let effects = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::A)), now());

            let stop = effects
                .iter()
                .position(|effect| matches!(effect, Effect::StopTransaction { .. }))
                .expect("the meter record is closed");
            let cleared = effects
                .iter()
                .position(clears_the_record)
                .expect("the persistent record is cleared");
            let finished = effects
                .iter()
                .position(|effect| {
                    matches!(effect, Effect::PublishSessionEvent(report)
                        if report.event == SessionEvent::TransactionFinished)
                })
                .expect("the transaction is announced finished");
            assert!(stop < cleared && cleared < finished, "got {effects:?}");
        }

        /// A boot with nothing left behind announces no resume and closes no
        /// named record, but still cancels whatever the meter holds
        /// (`Charger.cpp:1505-1511` is outside the recovered branch).
        #[test]
        fn a_boot_with_no_record_recovers_nothing_and_still_cancels() {
            let mut core = core_recovering(None);

            let effects = core.apply(Event::Startup, now());

            assert!(
                !reports(&effects)
                    .iter()
                    .any(|report| report.event == SessionEvent::SessionResumed),
                "got {effects:?}"
            );
            assert!(
                !effects
                    .iter()
                    .any(|effect| matches!(effect, Effect::StopTransaction { .. })),
                "got {effects:?}"
            );
            assert!(!effects.iter().any(clears_the_record), "got {effects:?}");
            assert!(
                effects.contains(&Effect::CancelAllTransactions),
                "got {effects:?}"
            );
        }

        /// The whole point of the task. `EvseManager.cpp:1478-1481` announces
        /// the resume and `Charger.cpp:1480-1503` closes the transaction, in
        /// this order and with the clear ahead of the meter call.
        #[test]
        fn a_boot_with_a_record_recovers_the_interrupted_transaction() {
            let mut core = core_recovering(Some("left-over-uuid"));

            let effects = core.apply(Event::Startup, now());

            let resumed = effects
                .iter()
                .position(|effect| {
                    matches!(effect, Effect::PublishSessionEvent(report)
                        if report.event == SessionEvent::SessionResumed)
                })
                .expect("the resume is announced");
            let cleared = effects
                .iter()
                .position(clears_the_record)
                .expect("the record is removed");
            let stop = effects
                .iter()
                .position(|effect| {
                    *effect
                        == Effect::StopTransaction {
                            transaction_id: "left-over-uuid".into(),
                        }
                })
                .expect("the meter record is closed under the recovered name");
            let finished = effects
                .iter()
                .position(|effect| {
                    matches!(effect, Effect::PublishSessionEvent(report)
                        if report.event == SessionEvent::TransactionFinished)
                })
                .expect("the transaction is announced finished");
            let cancel = effects
                .iter()
                .position(|effect| *effect == Effect::CancelAllTransactions)
                .expect("the unnamed cleanup still runs");
            assert!(
                resumed < cleared && cleared < stop && stop < finished && finished < cancel,
                "got {effects:?}"
            );
        }

        /// The recovery closes a transaction **without** announcing the charge
        /// finished, which is the asymmetry between the two routes that close a
        /// record. `Charger::stop_transaction` raises both
        /// (`Charger.cpp:1548-1550`) and
        /// `cleanup_transactions_on_startup` raises the transaction one alone
        /// (`:1577`), because there was no charge in this run to finish.
        ///
        /// Structural in this port, since the recovery builds its report
        /// directly rather than through `session_event`, and pinned anyway: the
        /// natural way to close the gap the other route had was to put the pair
        /// in one helper, and that would have announced a charge finished for a
        /// session this process never had.
        #[test]
        fn recovering_closes_the_record_without_announcing_a_charge_finished() {
            let mut core = core_recovering(Some("left-over-uuid"));

            let effects = core.apply(Event::Startup, now());

            let announced: Vec<SessionEvent> = effects
                .iter()
                .filter_map(|effect| match effect {
                    Effect::PublishSessionEvent(report) => Some(report.event),
                    _ => None,
                })
                .collect();
            assert_eq!(
                announced,
                vec![
                    SessionEvent::SessionResumed,
                    SessionEvent::TransactionFinished
                ],
                "got {effects:?}"
            );
        }

        /// `Charger.cpp:1483` clears before the loop at `:1486`. A recovery that
        /// died on the meter call would otherwise find the same record on every
        /// boot and retry it forever.
        #[test]
        fn the_record_is_removed_before_the_meter_is_asked_to_close_it() {
            let mut core = core_recovering(Some("left-over-uuid"));

            let effects = core.apply(Event::Startup, now());

            let cleared = effects
                .iter()
                .position(clears_the_record)
                .expect("the record is removed");
            let stop = effects
                .iter()
                .position(|effect| matches!(effect, Effect::StopTransaction { .. }))
                .expect("the meter record is closed");
            assert!(cleared < stop, "got {effects:?}");
        }

        /// The resume carries the recovered uuid; the finish carries the current
        /// session's, which at startup is none. `evse_managerImpl.cpp:380` fills
        /// the first from the recovered id and `:276` fills the second from
        /// `charger->get_session_id()`, which has never been set this early.
        #[test]
        fn the_resume_names_the_recovered_session_and_the_finish_names_none() {
            let mut core = core_recovering(Some("left-over-uuid"));

            let effects = core.apply(Event::Startup, now());

            assert_eq!(
                report_for(&effects, SessionEvent::SessionResumed).uuid,
                "left-over-uuid"
            );
            assert_eq!(
                report_for(&effects, SessionEvent::TransactionFinished).uuid,
                ""
            );
        }

        /// The recovery does not open a session. It closes a transaction that
        /// belongs to a run that is over, so nothing about it is live.
        #[test]
        fn recovering_does_not_open_a_session_or_a_transaction() {
            let mut core = core_recovering(Some("left-over-uuid"));

            core.apply(Event::Startup, now());

            assert!(!core.session().session_active);
            assert!(!core.session().transaction_active);
            assert_eq!(core.session().id, None);
        }

        /// `get_session` answers `{}` for an absent key, a value that is not a
        /// string, and a load that threw (`PersistentStore.cpp:30-42`), and both
        /// readers test emptiness. So an unreadable record is exactly an absent
        /// one, and neither raises: a store that cannot be read must not stop
        /// the charger from starting.
        #[test]
        fn an_unreadable_record_boots_as_though_there_were_none() {
            let mut core = core_recovering(Some(""));

            let effects = core.apply(Event::Startup, now());

            assert!(
                !reports(&effects)
                    .iter()
                    .any(|report| report.event == SessionEvent::SessionResumed),
                "got {effects:?}"
            );
            assert!(
                !effects
                    .iter()
                    .any(|effect| matches!(effect, Effect::RaiseError(_))),
                "an unreadable store is not a fault: got {effects:?}"
            );
            assert!(
                effects.contains(&Effect::CancelAllTransactions),
                "got {effects:?}"
            );
        }

        /// The record is consumed by the recovery that clears it, so a second
        /// pass has nothing to find. The C++ reaches the same place by clearing
        /// the store between its two reads.
        #[test]
        fn a_record_is_recovered_once() {
            let mut core = core_recovering(Some("left-over-uuid"));
            core.apply(Event::Startup, now());

            let again = core.apply(Event::Startup, now());

            assert!(
                !reports(&again)
                    .iter()
                    .any(|report| report.event == SessionEvent::SessionResumed),
                "got {again:?}"
            );
        }

        /// A session that ends without ever opening a transaction has no record,
        /// so it must not write one either. `stop_transaction`'s
        /// `transaction_active` guard (`Charger.cpp:1063-1066`) is what makes
        /// this hold, and `Charger::stop_session` does not touch the store.
        #[test]
        fn a_session_that_ends_with_no_transaction_clears_nothing() {
            let mut core = core_recovering(None);
            core.apply(Event::Startup, now());
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
            assert!(core.session().session_active, "a session did open");
            assert!(!core.session().transaction_active);

            let effects = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::A)), now());

            assert!(!effects.iter().any(clears_the_record), "got {effects:?}");
        }

        /// A re-validation inside a live session opens no second billing record
        /// (`Charger.cpp:384`, `:513`), so it writes no second one to the store
        /// either. The record names the session, and the session has not
        /// changed.
        #[test]
        fn a_second_authorization_in_one_session_persists_once() {
            let mut core = core_recovering(None);
            core.apply(Event::Startup, now());
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
            let opened = core.apply(authorize(true, AuthorizationKind::Eim), now());
            let id = start_id(&opened);
            let first = core.apply(
                Event::EffectDone {
                    id: Some(id),
                    outcome: EffectOutcome::Ok,
                },
                now(),
            );
            assert!(persisted(&first).is_some(), "the first start persists");

            let again = core.apply(authorize(true, AuthorizationKind::Eim), now());

            assert!(
                !again
                    .iter()
                    .any(|effect| matches!(effect, Effect::StartTransaction { .. })),
                "no second billing record: got {again:?}"
            );
            assert_eq!(persisted(&again), None, "got {again:?}");
        }

        /// The C++ recovery is outside the `external_ready_to_start_charging`
        /// branch (`EvseManager.cpp:1485` against `:1492`), so a port waiting
        /// for that signal recovers before it arrives. A record left open
        /// through a restart must not wait on an operator.
        #[test]
        fn recovery_does_not_wait_on_the_external_ready_signal() {
            let mut core = core_with(ReadyGate {
                awaits_external_signal: true,
            });
            core.persist = test_persist(Some("left-over-uuid"));

            let effects = core.apply(Event::Startup, now());

            assert!(
                effects.contains(&Effect::StopTransaction {
                    transaction_id: "left-over-uuid".into()
                }),
                "got {effects:?}"
            );
            assert!(effects.iter().any(clears_the_record), "got {effects:?}");
            assert!(
                !effects.contains(&Effect::PublishReady(true)),
                "still waiting for the signal: got {effects:?}"
            );
        }

        /// `Charger` is one state machine for both charge modes and the recovery
        /// sits in the module rather than a power path, so a DC port recovers
        /// the same way. Driven because the two paths are the axis this module
        /// splits on everywhere else.
        #[test]
        fn a_dc_port_recovers_the_same_way() {
            let mut core = dc_core();
            core.persist = test_persist(Some("left-over-uuid"));

            let effects = core.apply(Event::Startup, now());

            assert_eq!(
                report_for(&effects, SessionEvent::SessionResumed).uuid,
                "left-over-uuid"
            );
            assert!(
                effects.contains(&Effect::StopTransaction {
                    transaction_id: "left-over-uuid".into()
                }),
                "got {effects:?}"
            );
        }

        /// The recovery closes the previous run and gets out of the way. A port
        /// that came back from a power loss must still be able to charge, and
        /// the record it writes then is the NEW session's, not the recovered
        /// one.
        #[test]
        fn a_port_that_recovered_still_charges_and_persists_its_new_session() {
            let mut core = core_recovering(Some("left-over-uuid"));
            core.apply(Event::Startup, now());

            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
            let opened = core.apply(authorize(true, AuthorizationKind::Eim), now());
            let id = start_id(&opened);
            let live = core.session().id.clone().expect("a new session is live");
            let effects = core.apply(
                Event::EffectDone {
                    id: Some(id),
                    outcome: EffectOutcome::Ok,
                },
                now(),
            );

            assert_eq!(persisted(&effects).as_deref(), Some(live.as_str()));
            assert_ne!(live, "left-over-uuid", "the new session has its own name");
        }

        /// The window the C++ does not have. `AcState::Finished` closes the
        /// transaction and waits for the unplug (`path/mod.rs:207` raises
        /// `StopTransaction` with no `EndSession`), so a metering start still
        /// in flight can be answered while the session is alive and the
        /// transaction is not.
        ///
        /// Its verdict must persist nothing. In the C++ the store is inside a
        /// synchronous `start_transaction` and the record exists only while the
        /// transaction does; a record written here would survive the session
        /// with nothing to clear it, and the next boot would announce a
        /// `PowerLoss` for a charge that finished normally.
        #[test]
        fn a_start_verdict_answered_after_the_transaction_closed_persists_nothing() {
            let (mut core, id) = core_past_a_close_with_the_start_still_awaited(Metering {
                fail_on_errors: false,
            });

            let late = core.apply(
                Event::EffectDone {
                    id: Some(id),
                    outcome: EffectOutcome::Ok,
                },
                now(),
            );

            assert_eq!(persisted(&late), None, "got {late:?}");
        }

        /// The same window, with the verdict refusing. Raising a powermeter
        /// start failure for a transaction that already finished would take a
        /// port with nothing running out of service, which is the reason
        /// `end_session` already drops a verdict that outlived its session.
        #[test]
        fn a_refused_verdict_answered_after_the_transaction_closed_raises_nothing() {
            let (mut core, id) = core_past_a_close_with_the_start_still_awaited(Metering {
                fail_on_errors: true,
            });

            let late = core.apply(
                Event::EffectDone {
                    id: Some(id),
                    outcome: EffectOutcome::Failed("meter busy".into()),
                },
                now(),
            );

            assert!(
                !late
                    .iter()
                    .any(|effect| matches!(effect, Effect::RaiseError(_))),
                "got {late:?}"
            );
            assert_eq!(persisted(&late), None, "got {late:?}");
        }

        /// The round trip, under one key: what a transaction start writes is
        /// what a boot reads back, and what its close removes is the same name.
        /// Two key formulas, one on each side of the restart, is a recovery that
        /// silently stops recovering.
        #[test]
        fn the_key_written_in_a_session_is_the_key_a_boot_reads() {
            let mut opening = core_recovering(None);
            opening.apply(Event::Startup, now());
            opening.apply(plug_in(), now());
            let opened = opening.apply(authorize(true, AuthorizationKind::Eim), now());
            let id = start_id(&opened);
            let live = opening.session().id.clone().expect("a session is live");
            let stored = opening.apply(
                Event::EffectDone {
                    id: Some(id),
                    outcome: EffectOutcome::Ok,
                },
                now(),
            );
            let Some(Effect::Persist { key, value }) = stored
                .iter()
                .find(|effect| matches!(effect, Effect::Persist { .. }))
                .cloned()
            else {
                panic!("the accepted start persists: got {stored:?}");
            };
            assert_eq!(value, live);

            // The boot that follows the power loss reads that key back.
            let mut booting = core_recovering(Some(&value));
            let recovered = booting.apply(Event::Startup, now());

            assert!(
                recovered.contains(&Effect::StopTransaction {
                    transaction_id: live.clone()
                }),
                "got {recovered:?}"
            );
            assert!(
                recovered.contains(&Effect::PersistDelete { key: key.clone() }),
                "the boot clears the same key the session wrote: got {recovered:?}"
            );
        }
    }

    /// What the energy manager is asked for, driven through the core.
    ///
    /// The request itself is decided in `core::energy` and tested there. What
    /// is asserted here is the wiring: when a publish happens, which ones are
    /// priority, and that the periodic one keeps happening.
    mod what_the_energy_manager_is_asked_for {
        use super::*;
        use crate::core::energy::{PUBLISH_INTERVAL, TIMER_ENERGY_FLOW_REQUEST};

        fn requests(effects: &[Effect]) -> Vec<&crate::core::energy::flow_request::FlowRequest> {
            effects
                .iter()
                .filter_map(|effect| match effect {
                    Effect::PublishEnergyFlowRequest(request) => Some(request.as_ref()),
                    _ => None,
                })
                .collect()
        }

        fn arms_the_publish_timer(effects: &[Effect]) -> bool {
            effects.iter().any(|effect| {
                matches!(
                    effect,
                    Effect::StartTimer { id, after }
                        if *id == TIMER_ENERGY_FLOW_REQUEST && *after == PUBLISH_INTERVAL
                )
            })
        }

        #[test]
        fn the_port_asks_as_soon_as_it_comes_up_and_asks_with_priority() {
            // `energyImpl.cpp:119`. The first request is a priority one, so the
            // energy manager answers the new node rather than merging it into
            // its next run.
            let mut core = core_with(ReadyGate::default());

            let effects = core.apply(Event::Startup, now());

            let sent = requests(&effects);
            assert_eq!(sent.len(), 1, "got {effects:?}");
            assert!(sent[0].priority_request);
            assert!(arms_the_publish_timer(&effects), "got {effects:?}");
        }

        #[test]
        fn the_port_keeps_asking_once_a_second_without_priority() {
            // `energyImpl.cpp:122-128` runs a detached thread that publishes and
            // sleeps a second forever. Here the loop is the timer rearmed by the
            // publish it woke, so the cadence survives a writer that is busy.
            let mut core = core_with(ReadyGate::default());
            core.apply(Event::Startup, now());

            let effects = core.apply(
                Event::Timer {
                    id: TIMER_ENERGY_FLOW_REQUEST,
                    generation: 1,
                },
                now(),
            );

            let sent = requests(&effects);
            assert_eq!(sent.len(), 1, "got {effects:?}");
            assert!(!sent[0].priority_request, "the periodic ask is not urgent");
            assert!(
                arms_the_publish_timer(&effects),
                "the cadence stopped: {effects:?}"
            );
        }

        #[test]
        fn a_vehicle_arriving_asks_again_with_priority() {
            // `energyImpl.cpp:130-137`: the two transitions the C++ hooks are
            // the ones where a budget is about to be needed or about to be
            // released, and both ask for an answer now.
            let mut core = core_with(ReadyGate::default());
            core.apply(Event::Startup, now());
            core.apply(enable(EnableSource::Csms, 1, EnableScope::Evse), now());

            let effects = core.apply(plug_in(), now());

            let sent = requests(&effects);
            assert_eq!(sent.len(), 1, "got {effects:?}");
            assert!(sent[0].priority_request);
            assert_eq!(
                sent[0].evse_state,
                crate::core::energy::flow_request::EvseState::WaitForAuth
            );
        }

        #[test]
        fn a_vehicle_leaving_asks_again_with_priority() {
            let mut core = core_with(ReadyGate::default());
            core.apply(Event::Startup, now());
            core.apply(enable(EnableSource::Csms, 1, EnableScope::Evse), now());
            core.apply(plug_in(), now());
            core.apply(authorize(true, AuthorizationKind::Eim), now());

            let effects = core.apply(unplug(), now());

            let sent = requests(&effects);
            assert_eq!(sent.len(), 1, "got {effects:?}");
            assert!(sent[0].priority_request);
        }

        #[test]
        fn an_ordinary_transition_asks_for_nothing_extra() {
            // Only the two transitions above are hooked. Everything else waits
            // for the periodic publish, which is what keeps a busy session from
            // flooding the energy manager.
            let mut core = core_with(ReadyGate::default());
            core.apply(Event::Startup, now());
            core.apply(enable(EnableSource::Csms, 1, EnableScope::Evse), now());
            core.apply(plug_in(), now());

            let effects = core.apply(authorize(true, AuthorizationKind::Eim), now());

            assert!(requests(&effects).is_empty(), "got {effects:?}");
        }

        #[test]
        fn the_board_report_reaches_the_request() {
            // The report arrives as an event and the next publish carries it,
            // which is what `hw_caps = mod->get_hw_capabilities()` before every
            // publish does in the C++ (`energyImpl.cpp:125`).
            let mut core = core_with(ReadyGate::default());
            core.apply(Event::Startup, now());
            core.apply(
                Event::Bsp(BspEvent::Capabilities(HardwareCapabilities {
                    max_current_a_import: 32.0,
                    min_current_a_import: 6.0,
                    max_phase_count_import: 3,
                    min_phase_count_import: 1,
                    max_current_a_export: 0.0,
                    min_current_a_export: 0.0,
                    max_phase_count_export: 0,
                    min_phase_count_export: 0,
                    supports_changing_phases_during_charging: false,
                    supports_cp_state_e: false,
                })),
                now(),
            );

            let effects = core.apply(
                Event::Timer {
                    id: TIMER_ENERGY_FLOW_REQUEST,
                    generation: 2,
                },
                now(),
            );

            let sent = requests(&effects);
            assert_eq!(
                sent[0]
                    .schedule_import
                    .first()
                    .limits_to_root
                    .ac_max_current_a
                    .as_ref()
                    .map(|limit| limit.value),
                Some(32.0)
            );
        }

        #[test]
        fn the_cable_rating_reaches_the_request() {
            // The rating arrives as a board support event here, while the C++
            // reads it back from the board at each publish
            // (`energyImpl.cpp:278`). Same value, so the route has to exist.
            let mut core = core_with(ReadyGate::default());
            core.apply(Event::Startup, now());
            core.apply(
                Event::Bsp(BspEvent::Capabilities(HardwareCapabilities {
                    max_current_a_import: 32.0,
                    min_current_a_import: 6.0,
                    max_phase_count_import: 3,
                    min_phase_count_import: 1,
                    max_current_a_export: 0.0,
                    min_current_a_export: 0.0,
                    max_phase_count_export: 0,
                    min_phase_count_export: 0,
                    supports_changing_phases_during_charging: false,
                    supports_cp_state_e: false,
                })),
                now(),
            );
            core.apply(Event::Bsp(BspEvent::PpAmpacity(20.0)), now());
            core.apply(enable(EnableSource::Csms, 1, EnableScope::Evse), now());

            // Asserted on the ask a plug in triggers, because a port with no
            // vehicle asks for zero and caps nothing.
            let effects = core.apply(plug_in(), now());

            assert_eq!(
                requests(&effects)[0]
                    .schedule_import
                    .first()
                    .limits_to_root
                    .ac_max_current_a
                    .as_ref()
                    .map(|limit| limit.value),
                Some(20.0),
                "the cable rating did not reach the request"
            );
        }

        #[test]
        fn the_supply_report_reaches_the_request() {
            // The same route for the DC half: what the supply says it can
            // deliver is what the port asks the energy manager for.
            let mut core = dc_core();
            core.apply(Event::Startup, now());
            let mut caps = PowerSupplyCapabilities::sane_default();
            caps.max_export_power_w = 30_000.0;
            core.apply(Event::PowerSupplyCapabilities(Box::new(caps)), now());
            core.apply(enable(EnableSource::Csms, 1, EnableScope::Evse), now());

            let effects = core.apply(plug_in(), now());

            assert_eq!(
                requests(&effects)[0]
                    .schedule_import
                    .first()
                    .limits_to_leaves
                    .total_power_w
                    .as_ref()
                    .map(|power| power.value),
                Some(30_000.0),
                "the supply report did not reach the request"
            );
        }

        #[test]
        fn the_publish_timer_is_not_offered_to_the_power_path() {
            // Every other timer in this module belongs to a path. This one does
            // not, and a path asked about an identity it does not own would
            // have to answer for it.
            let (mut core, _events, calls) = dc_core_recording_path();
            core.apply(Event::Startup, now());
            let before = calls.lock().unwrap().len();

            core.apply(
                Event::Timer {
                    id: TIMER_ENERGY_FLOW_REQUEST,
                    generation: 1,
                },
                now(),
            );

            let calls_now = calls.lock().unwrap().clone();
            assert_eq!(calls_now.len(), before, "the path was asked");
        }
    }
    /// External derating, driven through the core.
    ///
    /// `core::derate` owns the arithmetic and `hlc::dc_limits` owns the seam.
    /// What is asserted here is the wiring the other two cannot see: that the
    /// command reaches the readers holding a copy of the capability report, and
    /// that a derate narrowing nothing reaches nobody.
    mod what_an_external_derate_narrows {
        use super::*;
        use crate::core::derate::ExternalDerating;
        use crate::core::energy::{PUBLISH_INTERVAL, TIMER_ENERGY_FLOW_REQUEST};
        use crate::core::hlc::ProvidedToken;

        /// A supply that can push 30 kW out and pull 20 kW back, so both
        /// directions are distinguishable from the seed zeros.
        fn supply() -> PowerSupplyCapabilities {
            PowerSupplyCapabilities {
                bidirectional: true,
                max_export_power_w: 30_000.0,
                max_import_power_w: Some(20_000.0),
                ..PowerSupplyCapabilities::sane_default()
            }
        }

        /// The export watt ceiling the path was last told to clamp the
        /// vehicle's request against, which is the reader external derating
        /// exists for. Read off the path rather than off the site request,
        /// because the site request carries the supply figure only once a
        /// session is charging and the narrowing is not a session level fact.
        ///
        /// The path is told by the enforced limits handler, which is the sole
        /// producer of its limit set and derives that set from the derated
        /// report the energy tree holds. So every test here drives a pass
        /// before reading: the pass is part of the property, not scaffolding
        /// around it. A derate that never reached the energy tree produces the
        /// undereated ceiling here.
        fn installed_export_watts(events: &RecordedEvents) -> Option<f64> {
            events
                .lock()
                .unwrap()
                .iter()
                .rev()
                .find_map(|event| match event {
                    PathEvent::DcEnforcedLimits { maximum, .. } => Some(maximum.maximum_power_w),
                    _ => None,
                })
        }

        /// Board support wide enough that the supply stays the binding cap, so
        /// the figure under test is the one derating moves. Also what gives the
        /// energy tree its phase count, without which an enforced limits answer
        /// is dropped before it reaches the DC branch.
        fn board_support() -> Event {
            Event::Bsp(BspEvent::Capabilities(HardwareCapabilities {
                max_phase_count_import: 3,
                min_phase_count_import: 1,
                ..HardwareCapabilities::default()
            }))
        }

        /// An enforced limits answer for this node, carrying a site allowance
        /// far above anything the supply can deliver so the ceiling under test
        /// is the supply's own and not the site's share.
        fn enforced(watt: f64) -> Event {
            Event::EnforcedLimits(Box::new(crate::core::energy::enforce::EnforcedLimits {
                uuid: "evse_manager".to_owned(),
                valid_for_s: 60,
                schedule: Vec::new(),
                limits_root_side: crate::core::energy::enforce::LimitsRes {
                    ac_max_current_a: Some(
                        crate::core::energy::flow_request::NumberWithSource::new(32.0, "test"),
                    ),
                    total_power_w: Some(crate::core::energy::flow_request::NumberWithSource::new(
                        watt, "test",
                    )),
                    ac_max_phase_count: None,
                },
            }))
        }

        fn recording_core() -> (Core, RecordedEvents) {
            let (mut core, events, _calls) = dc_core_recording_path();
            core.apply(Event::Startup, now());
            core.apply(board_support(), now());
            core.apply(Event::PowerSupplyCapabilities(Box::new(supply())), now());
            (core, events)
        }

        fn reporting_core() -> Core {
            let mut core = dc_core();
            core.apply(Event::Startup, now());
            core.apply(Event::PowerSupplyCapabilities(Box::new(supply())), now());
            core
        }

        fn derate_export_power(power_w: f64) -> Event {
            Event::Command(Command::SetExternalDerating(ExternalDerating {
                max_export_power_w: Some(power_w),
                ..ExternalDerating::default()
            }))
        }

        /// The end to end fact. Without this wiring the derate would narrow the
        /// vehicle's limit set and leave the site request at the supply's full
        /// ceiling, which is the failure the C++ cannot have: it has no copies
        /// to leave stale.
        #[test]
        fn a_derate_narrows_what_the_path_clamps_against() {
            let (mut core, events) = recording_core();
            core.apply(enforced(300_000.0), now());
            assert_eq!(installed_export_watts(&events), Some(30_000.0));

            core.apply(derate_export_power(10_000.0), now());
            core.apply(enforced(300_000.0), now());

            assert_eq!(installed_export_watts(&events), Some(10_000.0));
        }

        /// A report arriving under a standing derate is narrowed too, which is
        /// the same wiring reached from the other direction.
        #[test]
        fn a_report_arriving_under_a_derate_is_narrowed_on_the_way_in() {
            let (mut core, events, _calls) = dc_core_recording_path();
            core.apply(Event::Startup, now());
            core.apply(board_support(), now());
            core.apply(derate_export_power(10_000.0), now());
            core.apply(Event::PowerSupplyCapabilities(Box::new(supply())), now());
            core.apply(enforced(300_000.0), now());

            assert_eq!(installed_export_watts(&events), Some(10_000.0));
        }

        /// Relaxing restores it. The stored report is the raw one, so there is
        /// something to restore; a port that clamped on the way in would be
        /// stuck at 10 kW forever.
        #[test]
        fn relaxing_a_derate_restores_the_full_ceiling() {
            let (mut core, events) = recording_core();
            core.apply(derate_export_power(10_000.0), now());
            core.apply(enforced(300_000.0), now());
            assert_eq!(installed_export_watts(&events), Some(10_000.0));

            core.apply(
                Event::Command(Command::SetExternalDerating(ExternalDerating::default())),
                now(),
            );
            core.apply(enforced(300_000.0), now());

            assert_eq!(installed_export_watts(&events), Some(30_000.0));
        }

        /// A derate above the capability narrows nothing, so the ceiling the
        /// next pass installs has to be exactly the undereated one.
        ///
        /// Asserted on the installed ceiling rather than on the command's
        /// effects. Every derate emits nothing now that the path is told by the
        /// enforced limits handler instead, so an effects assertion here would
        /// hold whatever the derate did and would pin nothing.
        #[test]
        fn a_derate_that_narrows_nothing_leaves_the_ceiling_alone() {
            let (mut core, events) = recording_core();

            core.apply(derate_export_power(50_000.0), now());
            core.apply(enforced(300_000.0), now());

            assert_eq!(installed_export_watts(&events), Some(30_000.0));
        }

        /// And it tells the vehicle the narrowed report, which is the finding
        /// this asserted the opposite of.
        ///
        /// `set_external_derating` calls
        /// `push_powersupply_capabilities_to_hlc` on a request that changed
        /// (`EvseManager.cpp:2834-2837`), and that push forwards the derated
        /// report. The old assertion argued from a gate on the raw report,
        /// which is not the gate the push has; the consequence was that a
        /// thermal derate left the vehicle holding maxima the supply would
        /// refuse.
        #[test]
        fn a_derate_tells_the_vehicle_the_report_it_narrowed() {
            let mut core = reporting_core();

            let effects = core.apply(derate_export_power(10_000.0), now());

            let forwarded: Vec<f64> = effects
                .iter()
                .filter_map(|effect| match effect {
                    Effect::HlcUpdate(HlcUpdate::PowerSupplyCapabilities(caps)) => {
                        Some(caps.max_export_power_w)
                    }
                    _ => None,
                })
                .collect();
            assert_eq!(forwarded, vec![10_000.0], "got {effects:?}");
        }

        /// The measurement path. It arrives several times a second, so with no
        /// derate standing it must cost nothing, or the port would retell the
        /// site and the vehicle on every reading.
        #[test]
        fn a_present_voltage_with_no_derate_standing_emits_no_limit_refresh() {
            let mut core = reporting_core();

            let effects = core.apply(
                Event::SupplyVoltageCurrent {
                    voltage_v: 400.0,
                    current_a: 0.0,
                },
                now(),
            );

            // Only the present values reach the vehicle. A refresh would add
            // the EVSE limit emissions behind them.
            assert_eq!(
                effects,
                vec![Effect::HlcUpdate(HlcUpdate::DcPresentValues {
                    voltage_v: 400.0,
                    current_a: 0.0,
                })],
                "got {effects:?}"
            );
        }

        /// A derate naming only a current is completed from the measured
        /// voltage (`EvseManager.cpp:93-99`), so the site request narrows only
        /// once a voltage is on the cable.
        #[test]
        fn a_current_only_derate_narrows_at_the_measured_voltage() {
            let (mut core, events) = recording_core();
            core.apply(
                Event::Command(Command::SetExternalDerating(ExternalDerating {
                    max_export_current_a: Some(25.0),
                    ..ExternalDerating::default()
                })),
                now(),
            );
            core.apply(enforced(300_000.0), now());
            // No measurement yet, so the power half cannot be derived and the
            // watt ceiling is still the supply's own.
            assert_eq!(installed_export_watts(&events), Some(30_000.0));

            core.apply(
                Event::SupplyVoltageCurrent {
                    voltage_v: 400.0,
                    current_a: 0.0,
                },
                now(),
            );
            core.apply(enforced(300_000.0), now());

            // 25 A at 400 V is 10 kW.
            assert_eq!(installed_export_watts(&events), Some(10_000.0));
        }

        /// The site request, which is the other reader holding a copy.
        ///
        /// Found by the mutation sweep: feeding the energy tree the **raw**
        /// report survived the whole suite, because every other test here reads
        /// the ceiling off the path. The two readers are fed on different lines
        /// and only this one covers the second.
        mod what_the_port_asks_the_site_for {
            use super::*;

            /// Board support big enough that the supply is the binding cap, so
            /// the figure under test is the one derating moves. A three phase
            /// 32 A report is about 22 kW, well above the 10 kW supply below.
            fn board_support() -> Event {
                Event::Bsp(BspEvent::Capabilities(HardwareCapabilities {
                    min_phase_count_import: 1,
                    max_phase_count_import: 3,
                    max_current_a_import: 32.0,
                    min_current_a_import: 6.0,
                    ..HardwareCapabilities::default()
                }))
            }

            /// A supply whose export ceiling is below what the board can carry.
            fn small_supply() -> PowerSupplyCapabilities {
                PowerSupplyCapabilities {
                    bidirectional: true,
                    max_export_power_w: 10_000.0,
                    max_import_power_w: Some(8_000.0),
                    ..PowerSupplyCapabilities::sane_default()
                }
            }

            fn asked_import_watts(core: &mut Core) -> Option<f64> {
                let effects = core.apply(
                    Event::Timer {
                        id: TIMER_ENERGY_FLOW_REQUEST,
                        generation: 1,
                    },
                    now(),
                );
                effects.iter().find_map(|effect| match effect {
                    Effect::PublishEnergyFlowRequest(request) => Some(
                        request
                            .schedule_import
                            .first()
                            .limits_to_leaves
                            .total_power_w
                            .as_ref()?
                            .value,
                    ),
                    _ => None,
                })
            }

            /// A vehicle is plugged in, because `request_zero_power_in_idle`
            /// is set on this port and an idle node asks for nothing at all
            /// (`needs_energy`). The supply figure only reaches the request
            /// once the node wants energy, which is where derating matters.
            fn supplied_core() -> Core {
                let mut core = dc_core();
                core.apply(Event::Startup, now());
                core.apply(enable(EnableSource::Csms, 1, EnableScope::Evse), now());
                core.apply(plug_in(), now());
                core.apply(board_support(), now());
                core.apply(
                    Event::PowerSupplyCapabilities(Box::new(small_supply())),
                    now(),
                );
                core
            }

            /// A derate arriving after the report narrows the site request, so
            /// the port stops asking the site for power the derate forbids.
            #[test]
            fn a_derate_narrows_the_site_request() {
                let mut core = supplied_core();
                assert_eq!(asked_import_watts(&mut core), Some(10_000.0));

                core.apply(derate_export_power(4_000.0), now());

                assert_eq!(asked_import_watts(&mut core), Some(4_000.0));
            }

            /// And a report arriving under a standing derate is narrowed on the
            /// way in, which is the other of the two lines that feed this
            /// reader.
            #[test]
            fn a_report_arriving_under_a_derate_narrows_the_site_request() {
                let mut core = dc_core();
                core.apply(Event::Startup, now());
                core.apply(enable(EnableSource::Csms, 1, EnableScope::Evse), now());
                core.apply(plug_in(), now());
                core.apply(board_support(), now());
                core.apply(derate_export_power(4_000.0), now());
                core.apply(
                    Event::PowerSupplyCapabilities(Box::new(small_supply())),
                    now(),
                );

                assert_eq!(asked_import_watts(&mut core), Some(4_000.0));
            }
        }

        /// The deferral itself: a derate does not tell the path, it moves the
        /// report the next enforced limits pass derives the path's set from.
        ///
        /// Pinned because the alternative is a live regression rather than a
        /// style choice. A derate cannot name the site allowance, so a set
        /// pushed from here would carry the supply's ceiling undereated by the
        /// energy tree and would widen the DC clamp back to the supply maximum
        /// until the next pass. Deferring is also what the C++ does, which
        /// re-derives on every read and pushes nothing.
        #[test]
        fn a_derate_does_not_tell_the_path_by_itself() {
            let (mut core, events) = recording_core();
            let before = events.lock().unwrap().len();

            core.apply(derate_export_power(10_000.0), now());

            // Snapshotted, then asserted. Reading the guard inside the
            // assertion and naming it again in the failure message locks a
            // non-reentrant mutex twice in one expression, because the
            // comparison operand's guard outlives the message's evaluation.
            // That deadlocks instead of failing, and only on failure, so the
            // suite hangs in exactly the case the assertion exists to catch.
            let after = events.lock().unwrap().clone();
            assert_eq!(after.len(), before, "the path was told directly: {after:?}");

            // And the very next pass carries it, so deferring is not dropping.
            core.apply(enforced(300_000.0), now());
            assert_eq!(installed_export_watts(&events), Some(10_000.0));
        }

        /// The reducer's own wiring for the SLAC identity.
        ///
        /// Found by the mutation sweep: dropping the event in the reducer
        /// survived, because `hlc::autocharge_from_slac` drives the port method
        /// directly and the boundary ledger only pins that the event is built.
        /// Nothing joined the two.
        #[test]
        fn a_mac_address_event_reaches_the_token_provider() {
            let mut core = dc_core_with_autocharge_from_slac();

            let effects = core.apply(
                Event::Hlc(HlcEvent::VehicleMacAddress("AA:BB:CC:DD:EE:FF".into())),
                now(),
            );

            assert_eq!(
                effects,
                vec![Effect::PublishProvidedToken(ProvidedToken::Autocharge {
                    id_token: "VID:AABBCCDDEEFF".to_owned(),
                    connectors: vec![1],
                })],
                "got {effects:?}"
            );
        }

        /// The publish cadence survives a refresh, so a derate cannot stop the
        /// port asking. `PUBLISH_INTERVAL` is named to pin that the refresh
        /// does not rearm the timer on some other schedule.
        #[test]
        fn a_derate_leaves_the_publish_cadence_alone() {
            let mut core = reporting_core();
            core.apply(derate_export_power(10_000.0), now());

            let effects = core.apply(
                Event::Timer {
                    id: TIMER_ENERGY_FLOW_REQUEST,
                    generation: 1,
                },
                now(),
            );

            assert!(
                effects.iter().any(|effect| matches!(
                    effect,
                    Effect::StartTimer { id, after }
                        if *id == TIMER_ENERGY_FLOW_REQUEST && *after == PUBLISH_INTERVAL
                )),
                "the cadence stopped: {effects:?}"
            );
        }
    }
    /// What the vehicle is told when a session stops, `Charger.cpp:1013-1023`
    /// and `:1355-1363`.
    ///
    /// Both blocks sit on `Charger`, which is one state machine for both charge
    /// modes and branches on neither, so every fact here is driven on DC and on
    /// AC from the same table wherever the two can answer the same way. Before
    /// this module the signalling reached only `AcHlc`, because the flag it is
    /// gated on was a field of that path and a DC session had none.
    mod stop_signalling {
        use super::*;
        use crate::core::effect::CpState;

        fn hlc_updates(effects: &[Effect]) -> Vec<HlcUpdate> {
            effects
                .iter()
                .filter_map(|effect| match effect {
                    Effect::HlcUpdate(update) => Some(update.clone()),
                    _ => None,
                })
                .collect()
        }

        /// A DC session in current demand, which is the only route into
        /// `Charging` on DC (`Charger::notify_currentdemand_started`,
        /// `Charger.cpp:2024`).
        fn charging_dc() -> Core {
            let mut core = dc_core();
            core.apply(Event::Startup, now());
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
            core.apply(authorize(true, AuthorizationKind::Eim), now());
            core.apply(Event::Hlc(HlcEvent::CurrentDemandStarted), now());
            assert!(core.session.transaction_active, "a record is open");
            core
        }

        /// An AC session the ISO stack has taken over, which is the AC producer
        /// of the same flag (`EvseManager.cpp:394`).
        fn charging_ac_high_level() -> Core {
            let mut core = ac_hlc_core();
            core.apply(Event::Startup, now());
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
            core.apply(Event::Hlc(HlcEvent::SetupFinished), now());
            core.apply(authorize(true, AuthorizationKind::Eim), now());
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::C)), now());
            assert!(core.session.transaction_active, "a record is open");
            core
        }

        /// The same drive on a port with no high level communication at all.
        fn charging_ac_basic() -> Core {
            let mut core = core();
            core.apply(Event::Startup, now());
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
            core.apply(authorize(true, AuthorizationKind::Eim), now());
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::C)), now());
            core
        }

        fn stop(core: &mut Core, reason: StopTransactionReason) -> Vec<Effect> {
            core.apply(
                Event::Command(Command::StopTransaction {
                    reason,
                    id_tag: None,
                }),
                now(),
            )
        }

        /// The headline. `Charger.cpp:1014` reads `hlc_charging_active`, which
        /// `Charger.cpp:222-223` sets unconditionally true on DC because "for
        /// DC, it is always HLC mode". So every DC session takes the request
        /// branch, and it must take the same one an AC session takes.
        #[test]
        fn a_stopping_session_asks_the_vehicle_to_end_it_on_either_charge_mode() {
            for (mode, mut core) in [("DC", charging_dc()), ("AC", charging_ac_high_level())] {
                let effects = stop(&mut core, StopTransactionReason::Remote);

                assert!(
                    hlc_updates(&effects).contains(&HlcUpdate::StopCharging(true)),
                    "{mode}: the vehicle must be asked to end the session, got {effects:?}"
                );
            }
        }

        /// The `else` branch of the same `if`. A port with no ISO stack has
        /// nobody to ask, and the pilot drop it takes instead belongs to the
        /// path that owns a pilot.
        #[test]
        fn a_stopping_session_with_no_high_level_communication_asks_nobody() {
            let mut core = charging_ac_basic();

            let effects = stop(&mut core, StopTransactionReason::Remote);

            assert!(
                !hlc_updates(&effects)
                    .iter()
                    .any(|update| matches!(update, HlcUpdate::StopCharging(_))),
                "got {effects:?}"
            );
            assert!(
                effects.contains(&Effect::SetCpState(CpState::X1)),
                "the pilot still drops, got {effects:?}"
            );
        }

        /// The route that reaches the stopping entry and the `Idle` entry that
        /// clears `hlc_charging_active` in one pass.
        ///
        /// `Charger::process_cp_events_independent` (`Charger.cpp:1202-1203`)
        /// clears `flag_ev_plugged_in` and nothing else, so the `Charging` arm
        /// reaches `StoppingCharging` (`:782-792`) with the flag still standing
        /// and the entry at `:1014` takes the request branch. Only the `Idle`
        /// entry the state machine reaches afterwards clears it (`:220`).
        ///
        /// Here that is one `Core::apply`, and `AcHlc::end_session` clears the
        /// flag inside it, so a core that read the flag when it discharged the
        /// duty rather than at the pass boundary would ask the vehicle nothing.
        /// That is the whole reason the value is passed in, and this is the
        /// only route on which the difference shows.
        #[test]
        fn an_unplug_from_a_live_high_level_session_still_asks_the_vehicle_to_stop() {
            let mut core = charging_ac_high_level();

            let effects = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::A)), now());

            assert!(
                hlc_updates(&effects).contains(&HlcUpdate::StopCharging(true)),
                "got {effects:?}"
            );
        }

        /// `Charger.cpp:1012` signals the state event and `:1020` asks the
        /// vehicle, in that order and from the same `if (initialize_state)`
        /// block. Consumers read the pair as a sequence, so the order is pinned
        /// rather than left to whichever producer happens to run first.
        #[test]
        fn the_request_follows_the_stopping_announcement() {
            for (mode, mut core) in [("DC", charging_dc()), ("AC", charging_ac_high_level())] {
                let effects = stop(&mut core, StopTransactionReason::Remote);

                let announced = effects
                    .iter()
                    .position(|effect| {
                        matches!(effect, Effect::PublishSessionEvent(report)
                            if report.event == SessionEvent::StoppingCharging)
                    })
                    .unwrap_or_else(|| panic!("{mode}: no stopping announcement, {effects:?}"));
                let asked = effects
                    .iter()
                    .position(|effect| *effect == Effect::HlcUpdate(HlcUpdate::StopCharging(true)))
                    .unwrap_or_else(|| panic!("{mode}: no request, {effects:?}"));
                assert!(announced < asked, "{mode}: got {effects:?}");
            }
        }

        /// `Charger.cpp:1008` discharges the entry's duties inside
        /// `if (initialize_state)`, so the request belongs to the edge. A
        /// vehicle already asked and being waited on must not be asked again.
        #[test]
        fn the_request_is_owed_once_per_entry() {
            for (mode, mut core) in [("DC", charging_dc()), ("AC", charging_ac_high_level())] {
                stop(&mut core, StopTransactionReason::Remote);

                // The relays reported open, which is the next thing the C++
                // waits for in that state (`Charger.cpp:1035-1044`) and the
                // most likely event to re-run an entry that reads the state
                // rather than the edge.
                let later = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::PowerOff)), now());

                assert!(
                    !hlc_updates(&later)
                        .iter()
                        .any(|update| matches!(update, HlcUpdate::StopCharging(_))),
                    "{mode}: got {later:?}"
                );
            }
        }

        /// `Charger::cancel_transaction` (`Charger.cpp:1355-1363`) names two of
        /// the eight stop reasons to the vehicle and the rest none, and it does
        /// so on either charge mode. The name reaches the vehicle before the
        /// request to end the session, which is the C++ order: the error is
        /// sent from `cancel_transaction` itself and the request from the
        /// stopping entry the flags it sets then drive the state machine into.
        #[test]
        fn an_emergency_and_a_power_loss_each_name_themselves_to_the_vehicle() {
            for (mode, build) in [
                ("DC", charging_dc as fn() -> Core),
                ("AC", charging_ac_high_level as fn() -> Core),
            ] {
                for (reason, error) in [
                    (
                        StopTransactionReason::EmergencyStop,
                        EvseError::EmergencyShutdown,
                    ),
                    (
                        StopTransactionReason::PowerLoss,
                        EvseError::UtilityInterruptEvent,
                    ),
                ] {
                    let mut core = build();

                    let effects = stop(&mut core, reason);

                    let told = effects
                        .iter()
                        .position(|effect| {
                            *effect == Effect::HlcUpdate(HlcUpdate::SendError(error))
                        })
                        .unwrap_or_else(|| {
                            panic!("{mode} {reason:?} must reach the vehicle, got {effects:?}")
                        });
                    let asked = effects
                        .iter()
                        .position(|effect| {
                            *effect == Effect::HlcUpdate(HlcUpdate::StopCharging(true))
                        })
                        .unwrap_or_else(|| panic!("{mode}: no request, got {effects:?}"));
                    assert!(told < asked, "{mode} {reason:?}: got {effects:?}");
                }
            }
        }

        /// Every other reason names nothing, which is the C++ shape: it tests
        /// for exactly `EmergencyStop` and `PowerLoss`.
        ///
        /// All twenty one of them rather than the six narrow reasons this used
        /// to walk. The command carries the wire reason now, and the narrowing
        /// is many to one, so walking the narrow form would leave twenty of the
        /// twenty three wire values undriven: this is where a `HardReset` or a
        /// `SOCLimitReached` that named an error to the vehicle would show up.
        #[test]
        fn every_other_stop_reason_names_nothing_to_the_vehicle() {
            for reason in [
                StopTransactionReason::EvDisconnected,
                StopTransactionReason::HardReset,
                StopTransactionReason::Local,
                StopTransactionReason::Other,
                StopTransactionReason::Reboot,
                StopTransactionReason::Remote,
                StopTransactionReason::SoftReset,
                StopTransactionReason::UnlockCommand,
                StopTransactionReason::DeAuthorized,
                StopTransactionReason::EnergyLimitReached,
                StopTransactionReason::GroundFault,
                StopTransactionReason::LocalOutOfCredit,
                StopTransactionReason::MasterPass,
                StopTransactionReason::OvercurrentFault,
                StopTransactionReason::PowerQuality,
                StopTransactionReason::SocLimitReached,
                StopTransactionReason::StoppedByEv,
                StopTransactionReason::TimeLimitReached,
                StopTransactionReason::Timeout,
                StopTransactionReason::ReqEnergyTransferRejected,
                StopTransactionReason::EvseDisabled,
            ] {
                let mut core = charging_dc();

                let effects = stop(&mut core, reason);

                assert!(
                    !hlc_updates(&effects)
                        .iter()
                        .any(|update| matches!(update, HlcUpdate::SendError(_))),
                    "{reason:?}: got {effects:?}"
                );
            }
        }

        /// `Charger.cpp:1357` wraps the whole of `cancel_transaction` in
        /// `if (shared_context.flag_transaction_active)`, so a stop arriving
        /// with no record open names nothing even for the two reasons that
        /// otherwise would.
        #[test]
        fn a_stop_with_no_open_transaction_names_nothing() {
            let mut core = dc_core();
            core.apply(Event::Startup, now());
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
            assert!(!core.session.transaction_active, "no record is open");

            let effects = stop(&mut core, StopTransactionReason::EmergencyStop);

            assert!(
                !hlc_updates(&effects)
                    .iter()
                    .any(|update| matches!(update, HlcUpdate::SendError(_))),
                "got {effects:?}"
            );
        }

        /// A port with no ISO stack has nobody to name it to either, which is
        /// the `hlc_charging_active` half of the same gate (`Charger.cpp:1359`).
        #[test]
        fn a_stop_reason_on_a_port_without_high_level_communication_names_nothing() {
            let mut core = charging_ac_basic();

            let effects = stop(&mut core, StopTransactionReason::EmergencyStop);

            assert!(
                !hlc_updates(&effects)
                    .iter()
                    .any(|update| matches!(update, HlcUpdate::SendError(_))),
                "got {effects:?}"
            );
        }

        /// Puts the session into the state the ISO 15118-20 pause branch needs:
        /// a service selected, which is the only writer of `hlc_d20_active`
        /// (`EvseManager.cpp:961-965` into `Charger.cpp:2135-2136`), and a pause
        /// requested by the EVSE (`Charger::pause_charging`).
        ///
        /// Paused during the **preparation** rather than during the charge, so
        /// the pause flag is left standing for whatever ends the session to
        /// read. A pause during a charge crosses the stopping entry itself now
        /// (`Charger::run_state_machine`'s `Charging` arm), and the tests
        /// below are about the routes that reach the entry carrying a pause
        /// somebody else requested; the mid charge hop is driven in
        /// `the_mid_charge_pause`.
        ///
        /// Faithful, and the reason the two differ: only the `Charging` arm
        /// tests `flag_paused_by_evse`. The `PrepareCharging` arm
        /// does not, so the C++ keeps the flag and acts on it once the charge
        /// starts.
        fn preparing_and_paused_iso15118_20_dc() -> Core {
            let mut core = dc_core();
            core.apply(Event::Startup, now());
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
            core.apply(authorize(true, AuthorizationKind::Eim), now());
            core.apply(
                Event::Hlc(HlcEvent::SelectedService(SelectedService::Dc)),
                now(),
            );
            core.apply(Event::Command(Command::PauseCharging), now());
            assert!(core.session.transaction_active, "a record is open");
            assert!(core.session.paused_by_evse, "the pause is standing");
            assert_eq!(core.path.state(), AcState::PrepareCharging);
            core
        }

        /// The branch that had no reachable producer at all.
        /// `Charger.cpp:1015-1017`: an ISO 15118-20 session the EVSE paused is
        /// asked to pause, **not** to stop, and the two are the arms of one
        /// `if`/`else` so exactly one of them is sent.
        #[test]
        fn an_iso15118_20_session_paused_by_the_evse_is_asked_to_pause() {
            let mut core = preparing_and_paused_iso15118_20_dc();

            let effects = stop(&mut core, StopTransactionReason::Remote);

            let updates = hlc_updates(&effects);
            assert!(
                updates.contains(&HlcUpdate::PauseCharging(true)),
                "got {effects:?}"
            );
            assert!(
                !updates
                    .iter()
                    .any(|update| matches!(update, HlcUpdate::StopCharging(_))),
                "the stop is the branch not taken, got {effects:?}"
            );
        }

        /// Both halves of the gate are load bearing. Dropping either one sends
        /// the wrong one of the two requests, and the vehicle acts on it.
        #[test]
        fn neither_half_of_the_pause_gate_alone_asks_for_a_pause() {
            // A service selection with no pause: an ordinary ISO 15118-20 stop.
            let mut selected_only = charging_dc();
            selected_only.apply(
                Event::Hlc(HlcEvent::SelectedService(SelectedService::Dc)),
                now(),
            );
            // A pause with no service selection: an ISO 15118-2 session, which
            // has no pause request on the wire at all. Paused during the
            // preparation, for the reason
            // `preparing_and_paused_iso15118_20_dc` gives.
            let mut paused_only = dc_core();
            paused_only.apply(Event::Startup, now());
            paused_only.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
            paused_only.apply(authorize(true, AuthorizationKind::Eim), now());
            paused_only.apply(Event::Command(Command::PauseCharging), now());

            for (name, mut core) in [
                ("selected but not paused", selected_only),
                ("paused but not ISO 15118-20", paused_only),
            ] {
                let effects = stop(&mut core, StopTransactionReason::Remote);

                let updates = hlc_updates(&effects);
                assert!(
                    updates.contains(&HlcUpdate::StopCharging(true)),
                    "{name}: got {effects:?}"
                );
                assert!(
                    !updates
                        .iter()
                        .any(|update| matches!(update, HlcUpdate::PauseCharging(_))),
                    "{name}: got {effects:?}"
                );
            }
        }

        /// The pause branch is not a DC one. `Charger.cpp:1015` reads two
        /// session flags and no charge mode, and an AC session can carry both:
        /// `subscribe_selected_service_parameters` is registered outside the
        /// mode branch (`EvseManager.cpp:961`), and an AC pause reaches
        /// `ChargingPausedEVSE`, which leaves for `StoppingCharging` on the
        /// same conditions the charging state does (`Charger.cpp:940-946`).
        ///
        /// Driven because the DC case above would pass with the decision keyed
        /// on the charge mode rather than on the two flags.
        #[test]
        fn an_ac_iso15118_20_session_paused_by_the_evse_is_asked_to_pause_too() {
            let mut core = charging_ac_high_level();
            core.apply(
                Event::Hlc(HlcEvent::SelectedService(SelectedService::Ac)),
                now(),
            );
            core.apply(Event::Command(Command::PauseCharging), now());

            let effects = stop(&mut core, StopTransactionReason::Remote);

            let updates = hlc_updates(&effects);
            assert!(
                updates.contains(&HlcUpdate::PauseCharging(true)),
                "got {effects:?}"
            );
            assert!(
                !updates
                    .iter()
                    .any(|update| matches!(update, HlcUpdate::StopCharging(_))),
                "got {effects:?}"
            );
        }

        /// `Charger::resume_charging` (`Charger.cpp:1338-1345`) lowers the same
        /// flag, so a resumed session is asked to stop again rather than to
        /// pause.
        #[test]
        fn a_resume_puts_the_stop_request_back() {
            let mut core = preparing_and_paused_iso15118_20_dc();
            core.apply(Event::Command(Command::ResumeCharging), now());

            let effects = stop(&mut core, StopTransactionReason::Remote);

            assert!(
                hlc_updates(&effects).contains(&HlcUpdate::StopCharging(true)),
                "got {effects:?}"
            );
        }

        /// Both flags die with the session. `Charger::stop_session`
        /// (`Charger.cpp:1397`) clears the pause and the `Idle` entry (`:229`)
        /// clears the ISO 15118-20 one, so the next vehicle inherits neither.
        #[test]
        fn neither_pause_fact_survives_into_the_next_session() {
            let mut core = preparing_and_paused_iso15118_20_dc();
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::A)), now());

            assert!(!core.session.paused_by_evse);
            assert!(!core.session.iso15118_20_active);
        }

        /// The availability route, which reaches `Core::stop` from a different
        /// call site than the external cancel above (`Charger.cpp:1765` sets
        /// `flag_disable_requested`, and the `Charging` arm leaves for
        /// `StoppingCharging` on it at `:782-783`). Driven on DC because that
        /// is the path the whole lift exists for.
        #[test]
        fn a_disable_arriving_mid_charge_asks_the_dc_vehicle_to_stop() {
            let mut core = charging_dc();

            let effects = core.apply(
                Event::Command(Command::EnableDisable {
                    source: EnableSource::Csms,
                    state: EnableState::Disable,
                    priority: 100,
                    scope: EnableScope::Evse,
                }),
                now(),
            );

            assert!(
                hlc_updates(&effects).contains(&HlcUpdate::StopCharging(true)),
                "got {effects:?}"
            );
        }

        /// The fault route, and the asymmetry it still carries.
        ///
        /// `Charger::error_shutdown` (`Charger.cpp:2272-2283`) signals the error
        /// and the state machine reaches `StoppingCharging` separately, through
        /// `stop_charging_on_fatal_error_internal` at `:782`. Here the error
        /// comes from `HlcPort::on_fault_shutdown`, which is mode independent,
        /// and the request comes from the path's own safe state.
        ///
        /// `AcHlc::to_safe_state` runs `IecInput::ErrorShutdown` through the
        /// reducer, which enters `StoppingCharging` (`iec.rs`, `shutdown`), so
        /// AC gets both. `Dc::to_safe_state` deliberately does not move its
        /// session progress, so DC gets the error alone. That is a ceiling on
        /// the DC session progress rather than on this signalling, and it is
        /// pinned here so the difference is a stated fact.
        #[test]
        fn a_fault_tells_both_modes_why_and_only_ac_also_asks_them_to_stop() {
            let fault = |severity| {
                Event::Error(ErrorEvent {
                    source: ErrorSource::Bsp,
                    error_type: "evse_board_support/MREC8EmergencyStop".into(),
                    sub_type: String::new(),
                    vendor_id: String::new(),
                    severity,
                    raised: true,
                })
            };

            let mut ac = charging_ac_high_level();
            let on_ac = hlc_updates(&ac.apply(fault(Severity::High), now()));
            assert!(
                on_ac.contains(&HlcUpdate::SendError(EvseError::EmergencyShutdown)),
                "got {on_ac:?}"
            );
            assert!(
                on_ac.contains(&HlcUpdate::StopCharging(true)),
                "got {on_ac:?}"
            );

            let mut dc = charging_dc();
            let on_dc = hlc_updates(&dc.apply(fault(Severity::High), now()));
            assert!(
                on_dc.contains(&HlcUpdate::SendError(EvseError::EmergencyShutdown)),
                "got {on_dc:?}"
            );
            assert!(
                !on_dc
                    .iter()
                    .any(|update| matches!(update, HlcUpdate::StopCharging(_))),
                "the DC ceiling: no stopping entry is crossed, got {on_dc:?}"
            );
        }

        /// An unplug out of a paused ISO 15118-20 session asks the vehicle to
        /// pause, on a session that is then finished.
        ///
        /// Odd on the wire and faithful: `Charger.cpp:1015` reads the two flags
        /// and nothing about why the entry was reached, and neither the unplug
        /// (`:1202-1203`, which clears `flag_ev_plugged_in` alone) nor
        /// `cancel_transaction` (`:1348-1373`) clears `flag_paused_by_evse`.
        /// Only `Charger::resume_charging` and `Charger::stop_session` do, and
        /// `stop_session` runs after this entry. Pinned so the oddity is a
        /// recorded C++ behavior rather than a surprise.
        #[test]
        fn an_unplug_from_a_paused_iso15118_20_session_still_asks_for_a_pause() {
            let mut core = charging_ac_high_level();
            core.apply(
                Event::Hlc(HlcEvent::SelectedService(SelectedService::Ac)),
                now(),
            );
            core.apply(Event::Command(Command::PauseCharging), now());

            let effects = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::A)), now());

            assert!(
                hlc_updates(&effects).contains(&HlcUpdate::PauseCharging(true)),
                "got {effects:?}"
            );
        }

        /// Both session flags are set on a port with no high level
        /// communication too, because `Core` writes them without asking which
        /// path is bound. They are inert there: the outer gate is the path's
        /// answer, and `AcBasic` never carries a high level session.
        ///
        /// In a real deployment neither event reaches an `AcBasic` port at all,
        /// since that path is selected only when the ISO stack is not wired.
        /// Driven anyway, because nothing in the core enforces that and a gate
        /// that read only the two session flags would send a pause request out
        /// of a basic charging port.
        #[test]
        fn the_pause_branch_is_inert_on_a_port_without_high_level_communication() {
            let mut core = charging_ac_basic();
            core.apply(
                Event::Hlc(HlcEvent::SelectedService(SelectedService::Ac)),
                now(),
            );
            core.apply(Event::Command(Command::PauseCharging), now());
            assert!(core.session.iso15118_20_active);
            assert!(core.session.paused_by_evse);

            let effects = stop(&mut core, StopTransactionReason::Remote);

            assert!(hlc_updates(&effects).is_empty(), "got {effects:?}");
        }

        /// `Charger::pause_charging` (`Charger.cpp:1331`) acts only on a live
        /// transaction and answers false otherwise, so a pause arriving with no
        /// record open must not arm the branch for the session that follows.
        #[test]
        fn a_pause_with_no_open_transaction_arms_nothing() {
            let mut core = dc_core();
            core.apply(Event::Startup, now());
            core.apply(
                Event::Hlc(HlcEvent::SelectedService(SelectedService::Dc)),
                now(),
            );
            core.apply(Event::Command(Command::PauseCharging), now());

            assert!(!core.session.paused_by_evse);
        }

        /// The mid charge pause: the hop the ISO 15118-20 pause branch exists
        /// for, and the power withdrawal that has to travel with it.
        ///
        /// Everything above this drives the pause request from a **terminating**
        /// route, because until this change that was the only route that
        /// reached the `StoppingCharging` entry with the pause flag standing.
        /// `Charger::run_state_machine`'s `Charging` arm leaves for
        /// `StoppingCharging` on `flag_paused_by_evse` alone, that state's
        /// entry asks the vehicle to
        /// pause, and its exit then settles into `ChargingPausedEVSE`, from
        /// which the session resumes. These drive that cycle.
        mod the_mid_charge_pause {
            use super::*;

            /// An ISO 15118-20 AC charge with the relays confirmed closed, so
            /// the pause has something to withdraw and has to wait for the
            /// board to report them open again.
            fn charging_iso15118_20_ac() -> Core {
                let mut core = charging_ac_high_level();
                core.apply(
                    Event::Hlc(HlcEvent::SelectedService(SelectedService::Ac)),
                    now(),
                );
                core.apply(Event::Bsp(BspEvent::Cp(CpEvent::PowerOn)), now());
                assert_eq!(core.path.state(), AcState::Charging);
                core
            }

            /// A DC charge with the relays confirmed closed. `charging_dc`
            /// reaches `Charging` through current demand alone, which is all the
            /// C++ needs (`Charger::notify_currentdemand_started`), so the board
            /// fact has to be added: without it the pause has relays already
            /// reported open and settles in one pass.
            fn charging_dc_relays_closed() -> Core {
                let mut core = charging_dc();
                core.apply(Event::Bsp(BspEvent::Cp(CpEvent::PowerOn)), now());
                core
            }

            fn pause(core: &mut Core) -> Vec<Effect> {
                core.apply(Event::Command(Command::PauseCharging), now())
            }

            /// The headline, and the one this task exists for. A pause arriving
            /// mid charge asks the vehicle to pause **there and then**, rather
            /// than leaving the request to whatever eventually ends the session.
            #[test]
            fn a_pause_during_a_charge_asks_the_vehicle_to_pause_at_once() {
                let mut core = charging_iso15118_20_ac();

                let effects = pause(&mut core);

                assert!(
                    hlc_updates(&effects).contains(&HlcUpdate::PauseCharging(true)),
                    "got {effects:?}"
                );
            }

            /// The hop itself. `Charger::run_state_machine`'s `Charging` arm
            /// sets `StoppingCharging` and the entry announces it, so the pause
            /// is two edges and not one.
            #[test]
            fn a_pause_during_a_charge_crosses_the_stopping_entry() {
                let mut core = charging_iso15118_20_ac();

                let effects = pause(&mut core);

                assert_eq!(
                    published_events(&effects),
                    vec![SessionEvent::StoppingCharging],
                    "got {effects:?}"
                );
                assert_eq!(
                    core.path.state(),
                    AcState::StoppingCharging,
                    "the relays are still closed, so the stop is not complete"
                );
            }

            /// And it settles. The `StoppingCharging` exit is reached on the relays being
            /// observed open is what completes the stop, and with the vehicle
            /// still plugged in and the port still in service the pause lands in
            /// `ChargingPausedEVSE` rather than `Finished`.
            #[test]
            fn the_relays_opening_settles_the_pause_into_the_paused_state() {
                let mut core = charging_iso15118_20_ac();
                pause(&mut core);

                let effects = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::PowerOff)), now());

                assert_eq!(core.path.state(), AcState::ChargingPausedEvse);
                assert_eq!(
                    published_events(&effects),
                    vec![SessionEvent::ChargingPausedEvse],
                    "got {effects:?}"
                );
                assert!(
                    core.session.transaction_active,
                    "a paused session keeps its billing record open"
                );
            }

            /// The whole point of landing in the paused state rather than in
            /// `Finished`: the session resumes. The `ChargingPausedEVSE` arm
            /// leaves for `PrepareCharging` once no reason to pause is left.
            #[test]
            fn a_resumed_mid_charge_pause_offers_the_pilot_again() {
                let mut core = charging_iso15118_20_ac();
                pause(&mut core);
                core.apply(Event::Bsp(BspEvent::Cp(CpEvent::PowerOff)), now());

                let effects = core.apply(Event::Command(Command::ResumeCharging), now());

                assert!(
                    effects.iter().any(|e| matches!(e, Effect::PwmOn(_))),
                    "got {effects:?}"
                );
                assert_eq!(core.path.state(), AcState::PrepareCharging);
            }

            /// The mid-charge pause the previous change made reachable, driven
            /// all the way back to a live charge.
            ///
            /// Two inputs and two events, in this order. The resume announces
            /// the preparation and stops there, because there is no
            /// `iec_allow_close_contactor` latch here to re-enter `Charging`
            /// without a fresh control pilot edge; the vehicle's state C is
            /// what finishes it. `Charger::run_state_machine`'s
            /// `PrepareCharging` arm emits the same pair from the same two
            /// facts, so the sequence a consumer sees is the same.
            #[test]
            fn a_resume_after_the_mid_charge_pause_announces_the_preparation_then_the_charge() {
                let mut core = charging_iso15118_20_ac();
                pause(&mut core);
                core.apply(Event::Bsp(BspEvent::Cp(CpEvent::PowerOff)), now());
                assert_eq!(core.path.state(), AcState::ChargingPausedEvse);

                let resumed = core.apply(Event::Command(Command::ResumeCharging), now());
                assert_eq!(
                    published_events(&resumed),
                    vec![SessionEvent::PrepareCharging],
                    "got {resumed:?}"
                );

                let charging = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::C)), now());

                assert_eq!(core.path.state(), AcState::Charging);
                assert_eq!(
                    published_events(&charging),
                    vec![SessionEvent::ChargingStarted],
                    "got {charging:?}"
                );
            }

            /// Neither party can resume the other's pause, which is where the
            /// symmetry between the two paused states stops.
            ///
            /// `Charger::process_cp_events_state` has a `ChargingPausedEV` arm
            /// and no `ChargingPausedEVSE` one, so a control pilot state C
            /// arriving under an EVSE pause moves nothing there either; here
            /// the state fails `Iec::pwm_eligible`, which is the same answer
            /// reached through the offer. And `Charger::resume_charging` only
            /// lowers `flag_paused_by_evse`, which the `ChargingPausedEV` arm
            /// never reads, so a resume command arriving under the vehicle's
            /// own pause is dropped as `Iec::resume_requested` drops it.
            #[test]
            fn neither_party_can_resume_the_other_s_pause() {
                let mut core = charging_iso15118_20_ac();
                pause(&mut core);
                core.apply(Event::Bsp(BspEvent::Cp(CpEvent::PowerOff)), now());
                assert_eq!(core.path.state(), AcState::ChargingPausedEvse);

                let asked = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::C)), now());

                assert_eq!(core.path.state(), AcState::ChargingPausedEvse);
                assert_eq!(published_events(&asked), Vec::new(), "got {asked:?}");

                let mut core = charging_iso15118_20_ac();
                core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
                assert_eq!(core.path.state(), AcState::ChargingPausedEv);

                let resumed = core.apply(Event::Command(Command::ResumeCharging), now());

                assert_eq!(core.path.state(), AcState::ChargingPausedEv);
                assert_eq!(published_events(&resumed), Vec::new(), "got {resumed:?}");
            }

            /// A cancel arriving while the pause is still waiting on the relays
            /// must not leave a resumable session behind.
            /// `Charger::cancel_transaction` withdraws the authorization, and
            /// the `StoppingCharging` exit's fatality guard reads it
            /// before it reads the pause.
            #[test]
            fn a_cancel_during_the_pause_hop_finishes_the_session() {
                let mut core = charging_iso15118_20_ac();
                pause(&mut core);

                stop(&mut core, StopTransactionReason::Remote);
                core.apply(Event::Bsp(BspEvent::Cp(CpEvent::PowerOff)), now());

                assert_eq!(core.path.state(), AcState::Finished);
            }

            /// An unplug during the pause hop is over, not paused.
            ///
            /// It still waits for the relays: the `StoppingCharging` arm holds in
            /// `StoppingCharging` on the relays alone and reads none of the
            /// session flags before them, so a departed vehicle does not
            /// shorten the wait. What the unplug decides is where the wait
            /// ends, and the exit's fatality guard reads
            /// `not flag_ev_plugged_in` there.
            #[test]
            fn an_unplug_during_the_pause_hop_finishes_the_session() {
                let mut core = charging_iso15118_20_ac();
                pause(&mut core);

                core.apply(Event::Bsp(BspEvent::Cp(CpEvent::A)), now());
                assert_eq!(
                    core.path.state(),
                    AcState::StoppingCharging,
                    "still parked on the relays"
                );

                core.apply(Event::Bsp(BspEvent::Cp(CpEvent::PowerOff)), now());

                assert_eq!(core.path.state(), AcState::Idle);
                assert!(!core.session.paused_by_evse);
                assert!(!core.session.transaction_active);
            }

            /// A second pause inside one session. The hop runs again, because
            /// `Charger::run_state_machine`'s `Charging` arm reads the flag on
            /// every pass through `Charging`, and the resume put the session
            /// back there.
            #[test]
            fn a_second_pause_in_one_session_hops_again() {
                let mut core = charging_iso15118_20_ac();
                pause(&mut core);
                core.apply(Event::Bsp(BspEvent::Cp(CpEvent::PowerOff)), now());
                core.apply(Event::Command(Command::ResumeCharging), now());
                core.apply(Event::Bsp(BspEvent::Cp(CpEvent::C)), now());
                core.apply(Event::Bsp(BspEvent::Cp(CpEvent::PowerOn)), now());
                assert_eq!(core.path.state(), AcState::Charging);

                let effects = pause(&mut core);

                assert!(
                    hlc_updates(&effects).contains(&HlcUpdate::PauseCharging(true)),
                    "got {effects:?}"
                );
                assert_eq!(core.path.state(), AcState::StoppingCharging);
            }

            /// The DC half. A pause that leaves the supply running and the
            /// relays closed is not a pause, and the order is the DC hazard
            /// order: the energy goes before the vehicle is released.
            #[test]
            fn a_dc_pause_withdraws_the_energy_before_it_releases_the_relays() {
                let mut core = charging_dc_relays_closed();

                let effects = pause(&mut core);

                let off = effects
                    .iter()
                    .position(|e| *e == Effect::SupplyOff)
                    .unwrap_or_else(|| panic!("no supply off in {effects:?}"));
                let released = effects
                    .iter()
                    .position(|e| *e == Effect::AllowPowerOn(false))
                    .unwrap_or_else(|| panic!("no power on withdrawal in {effects:?}"));
                assert!(
                    off < released,
                    "the energy goes before the vehicle is released: {effects:?}"
                );

                // And the re-apply watchdog goes last, so the ordering above is
                // independent of whether it happened to be armed. The same rule
                // the safe state keeps; see `path::dc::tests::hazards`.
                let cancelled = effects
                    .iter()
                    .position(|e| matches!(e, Effect::CancelTimer { .. }))
                    .unwrap_or_else(|| panic!("no watchdog cancellation in {effects:?}"));
                assert!(
                    released < cancelled,
                    "the watchdog is cancelled after the energy is gone: {effects:?}"
                );
            }

            /// And the DC pause crosses the same entry, so the vehicle is told.
            #[test]
            fn a_dc_pause_asks_the_vehicle_to_pause_at_once() {
                let mut core = charging_dc_relays_closed();
                core.apply(
                    Event::Hlc(HlcEvent::SelectedService(SelectedService::Dc)),
                    now(),
                );

                let effects = pause(&mut core);

                assert!(
                    hlc_updates(&effects).contains(&HlcUpdate::PauseCharging(true)),
                    "got {effects:?}"
                );
                assert_eq!(core.path.state(), AcState::StoppingCharging);
            }

            /// The DC pause settles the same way the AC one does: on the board
            /// reporting the relays open, and into the paused state rather than
            /// into a finished session, which is the `StoppingCharging` exit.
            #[test]
            fn a_dc_pause_settles_into_the_paused_state_when_the_relays_open() {
                let mut core = charging_dc_relays_closed();
                pause(&mut core);
                assert_eq!(core.path.state(), AcState::StoppingCharging);

                let effects = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::PowerOff)), now());

                assert_eq!(core.path.state(), AcState::ChargingPausedEvse);
                assert!(
                    published_events(&effects).contains(&SessionEvent::ChargingPausedEvse),
                    "got {effects:?}"
                );
                assert!(
                    core.session.transaction_active,
                    "a paused DC session keeps its billing record open"
                );
            }

            /// The destination is stated per stop, not remembered. A DC cancel
            /// arriving on a session the EVSE had paused re-enters the stopping
            /// entry and restates it, so the relays opening must not settle the
            /// session back into a state it can be resumed from.
            /// the `StoppingCharging` exit's fatality guard.
            #[test]
            fn a_dc_cancel_after_a_pause_does_not_settle_back_into_the_paused_state() {
                let mut core = charging_dc_relays_closed();
                pause(&mut core);
                core.apply(Event::Bsp(BspEvent::Cp(CpEvent::PowerOff)), now());
                assert_eq!(core.path.state(), AcState::ChargingPausedEvse);

                stop(&mut core, StopTransactionReason::Remote);
                assert_eq!(core.path.state(), AcState::StoppingCharging);
                core.apply(Event::Bsp(BspEvent::Cp(CpEvent::PowerOff)), now());

                assert_eq!(core.path.state(), AcState::StoppingCharging);
            }

            /// A resume arriving while the pause is still waiting on
            /// the relays is dropped, and the session lands in the paused state
            /// anyway. `Charger::resume_charging` lowers `flag_paused_by_evse`
            /// and the exit's pause branch then reads
            /// `ChargingPausedEV`, which no route here reaches. Ceiling: the
            /// resume has to be repeated once the pause has settled. Landing in
            /// `ChargingPausedEVSE` rather than in `Finished` is what keeps that
            /// second resume available. Upgrade path: reach `ChargingPausedEv`
            /// from the stopping exit, which needs the vehicle side pause this
            /// exit cannot tell apart from the EVSE one. Owner: RsEvseManager.
            #[test]
            fn a_resume_inside_the_pause_hop_has_to_be_repeated() {
                let mut core = charging_iso15118_20_ac();
                pause(&mut core);

                let resumed = core.apply(Event::Command(Command::ResumeCharging), now());
                assert!(!core.session.paused_by_evse);
                assert!(
                    !resumed.iter().any(|e| matches!(e, Effect::PwmOn(_))),
                    "got {resumed:?}"
                );

                core.apply(Event::Bsp(BspEvent::Cp(CpEvent::PowerOff)), now());
                assert_eq!(core.path.state(), AcState::ChargingPausedEvse);

                let again = core.apply(Event::Command(Command::ResumeCharging), now());
                assert!(
                    again.iter().any(|e| matches!(e, Effect::PwmOn(_))),
                    "got {again:?}"
                );
            }

            /// A DC pause whose relays were already reported open settles in
            /// the same pass. Without it the session parks in
            /// `StoppingCharging` behind a board fact that has been and gone,
            /// and `charging_dc` is exactly that shape: current demand carries
            /// the C++ into `Charging` (`Charger::notify_currentdemand_started`)
            /// and no relay fact is needed to get there.
            #[test]
            fn a_dc_pause_with_the_relays_already_open_settles_in_one_pass() {
                let mut core = charging_dc();
                core.apply(
                    Event::Hlc(HlcEvent::SelectedService(SelectedService::Dc)),
                    now(),
                );

                let effects = pause(&mut core);

                assert_eq!(core.path.state(), AcState::ChargingPausedEvse);
                assert_eq!(
                    published_events(&effects),
                    vec![
                        SessionEvent::StoppingCharging,
                        SessionEvent::ChargingPausedEvse
                    ],
                    "got {effects:?}"
                );
                assert!(
                    hlc_updates(&effects).contains(&HlcUpdate::PauseCharging(true)),
                    "got {effects:?}"
                );
            }

            /// An unplug out of a settled DC pause, which the AC counterpart
            /// already drove, in
            /// `an_unplug_from_a_paused_iso15118_20_session_still_asks_for_a_pause`.
            /// The vehicle is asked to pause on a session that is then finished,
            /// which is faithful: that entry reads the two session flags
            /// and nothing about why the entry was reached, and the unplug
            /// clears neither.
            #[test]
            fn an_unplug_out_of_a_paused_dc_session_still_asks_for_a_pause() {
                let mut core = charging_dc_relays_closed();
                core.apply(
                    Event::Hlc(HlcEvent::SelectedService(SelectedService::Dc)),
                    now(),
                );
                pause(&mut core);
                core.apply(Event::Bsp(BspEvent::Cp(CpEvent::PowerOff)), now());
                assert_eq!(core.path.state(), AcState::ChargingPausedEvse);

                let effects = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::A)), now());

                assert!(
                    hlc_updates(&effects).contains(&HlcUpdate::PauseCharging(true)),
                    "got {effects:?}"
                );
                assert_eq!(core.path.state(), AcState::Idle);
                assert!(!core.session.transaction_active);
            }

            /// A pause arriving during an AC preparation enters the
            /// paused state without crossing the stopping entry, so the vehicle
            /// is told nothing until whatever ends the session reaches one.
            ///
            /// The `PrepareCharging` arm has a stop test of its own and it tests
            /// none of the pause, so the C++ keeps the flag standing and the
            /// `Charging` arm acts on it one pass later. Ceiling: this reducer
            /// has no second read site, so the deferral becomes a direct entry.
            /// Upgrade path: carry the pause as a reducer input the
            /// `PrepareCharging` exit re-reads, which is a new writer of the
            /// `PrepareCharging` to `Charging` transition. Owner: RsEvseManager.
            #[test]
            fn a_pause_during_an_ac_preparation_crosses_no_entry() {
                let mut core = ac_hlc_core();
                core.apply(Event::Startup, now());
                core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
                core.apply(Event::Hlc(HlcEvent::SetupFinished), now());
                core.apply(authorize(true, AuthorizationKind::Eim), now());
                core.apply(
                    Event::Hlc(HlcEvent::SelectedService(SelectedService::Ac)),
                    now(),
                );
                assert_eq!(core.path.state(), AcState::PrepareCharging);

                let effects = pause(&mut core);

                assert_eq!(core.path.state(), AcState::ChargingPausedEvse);
                assert_eq!(
                    published_events(&effects),
                    vec![SessionEvent::ChargingPausedEvse],
                    "got {effects:?}"
                );
                assert!(hlc_updates(&effects).is_empty(), "got {effects:?}");

                // Told at the entry it does reach.
                let stopped = stop(&mut core, StopTransactionReason::Remote);
                assert!(
                    hlc_updates(&stopped).contains(&HlcUpdate::PauseCharging(true)),
                    "got {stopped:?}"
                );
            }

            /// A fault arriving on a paused session finishes it rather than
            /// leaving it resumable, and it is the only route to the stopping
            /// entry that clears none of the four flags the exit reads. So the
            /// destination being restated at every entry is what decides this
            /// one, and a break that only ever raised the pause survived every
            /// other test in the suite.
            ///
            /// A deliberate divergence, and the port's pre-existing one:
            /// The `StoppingCharging` exit reads `stop_charging_on_fatal_error_internal`
            /// beside the pause and settles a faulted session into
            /// `ChargingPausedEVSE` too. This port finishes it, which is what
            /// `a_fault_tells_both_modes_why_and_only_ac_also_asks_them_to_stop`
            /// pins for the signalling half.
            #[test]
            fn a_fault_on_a_paused_session_finishes_it() {
                let mut core = charging_iso15118_20_ac();
                pause(&mut core);
                core.apply(Event::Bsp(BspEvent::Cp(CpEvent::PowerOff)), now());
                assert_eq!(core.path.state(), AcState::ChargingPausedEvse);

                core.apply(
                    Event::Error(ErrorEvent {
                        source: ErrorSource::Bsp,
                        error_type: "evse_board_support/MREC8EmergencyStop".into(),
                        sub_type: String::new(),
                        vendor_id: String::new(),
                        severity: Severity::High,
                        raised: true,
                    }),
                    now(),
                );
                assert_eq!(core.path.state(), AcState::StoppingCharging);
                core.apply(Event::Bsp(BspEvent::Cp(CpEvent::PowerOff)), now());

                assert_eq!(core.path.state(), AcState::Finished);
            }

            /// A DC unplug crosses the stopping entry, so the vehicle is asked
            /// to stop. `Charger::run_state_machine`'s `Charging` arm reaches
            /// `StoppingCharging` on `not flag_ev_plugged_in` for either charge
            /// mode and the entry
            /// then asks; the AC path already did.
            #[test]
            fn a_dc_unplug_asks_the_vehicle_to_stop() {
                let mut core = charging_dc();

                let effects = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::A)), now());

                assert!(
                    hlc_updates(&effects).contains(&HlcUpdate::StopCharging(true)),
                    "got {effects:?}"
                );
                assert_eq!(
                    published_events(&effects)
                        .into_iter()
                        .filter(|e| *e == SessionEvent::StoppingCharging)
                        .count(),
                    1,
                    "announced once, got {effects:?}"
                );
            }
        }
    }

    /// Soft overcurrent detection driven through `Core::apply`: the state gate,
    /// the wake-up, the transcript lines and the error that stops the session.
    ///
    /// The unit level decisions live in `core::soft_oc`. What these drive is the
    /// wiring the detector cannot see: which state it runs in, which figure the
    /// path hands it, and what the raise costs the session.
    mod soft_over_current {
        use super::*;

        use crate::core::soft_oc::{PhaseCurrents, TIMER_SOFT_OVER_CURRENT};

        /// Sixteen amperes offered on the pilot, which the default core's
        /// initial limit already is. Plug in, authorize, draw power.
        fn charging_core() -> Core {
            let mut core = core();
            core.apply(Event::Startup, now());
            core.apply(plug_in(), now());
            core.apply(authorize(true, AuthorizationKind::Eim), now());
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::C)), now());
            // The board reports the contactor it was just told to close. A
            // fixture that skips this leaves the reducer believing the relays
            // are already open, which shortens every route that waits on them.
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::PowerOn)), now());
            assert_eq!(core.path.state(), AcState::Charging);
            core
        }

        fn meter(l1_a: f64, l2_a: f64, l3_a: f64) -> Event {
            Event::Meter(MeterReading {
                energy_wh_import: 1.0,
                power_w: None,
                voltage_v: 230.0,
                current_a: l1_a,
                phase_currents_a: Some(PhaseCurrents { l1_a, l2_a, l3_a }),
                            dc_voltage_v: None,
            })
        }

        /// A record with no phase set at all, which is what a DC only or single
        /// phase meter reports.
        fn meter_without_phases() -> Event {
            Event::Meter(MeterReading {
                energy_wh_import: 1.0,
                power_w: None,
                voltage_v: 230.0,
                current_a: 40.0,
                phase_currents_a: None,
                            dc_voltage_v: None,
            })
        }

        fn logged(effects: &[Effect]) -> Vec<String> {
            effects
                .iter()
                .filter_map(|effect| match effect {
                    Effect::SessionLog(SessionLogEffect::Record { msg, .. }) => Some(msg.clone()),
                    _ => None,
                })
                .collect()
        }

        fn armed_soft_oc_timer(effects: &[Effect]) -> Option<std::time::Duration> {
            effects.iter().find_map(|effect| match effect {
                Effect::StartTimer { id, after } if *id == TIMER_SOFT_OVER_CURRENT => Some(*after),
                _ => None,
            })
        }

        fn soft_oc_deadline() -> Event {
            Event::Timer {
                id: TIMER_SOFT_OVER_CURRENT,
                generation: 1,
            }
        }

        /// The whole feature, end to end: a draw past the tolerance starts the
        /// timer, the deadline raises `MREC4OverCurrentFailure`, and the fault
        /// set answers it by taking the port to safe state.
        #[test]
        fn a_vehicle_overdrawing_past_the_timeout_stops_the_session() {
            let mut core = charging_core();

            let started = core.apply(meter(20.0, 0.0, 0.0), now());
            assert_eq!(
                armed_soft_oc_timer(&started),
                Some(std::time::Duration::from_millis(7000)),
                "the deadline was not armed: {started:?}"
            );
            let lines = logged(&started);
            assert!(
                lines.iter().any(|line| line.contains("starting timer")),
                "no transcript line for the crossing: {lines:?}"
            );
            assert!(
                raised_report(&started).is_none(),
                "raised before the timeout: {started:?}"
            );

            let tripped = core.apply(
                soft_oc_deadline(),
                now() + std::time::Duration::from_secs(8),
            );

            let report = raised_report(&tripped).expect("the error was not raised");
            assert_eq!(report.error_type, faults::MREC4_OVER_CURRENT_FAILURE);
            assert_eq!(report.severity, Severity::High);
            assert!(report.description.contains("triggered"), "{report:?}");
            assert!(report.description.contains("L1:20"), "{report:?}");

            // The raise is not the whole feature: what stops the session is the
            // `Inoperative` the fault set derives from it and the safe state
            // that goes with it.
            let types: Vec<&str> = tripped
                .iter()
                .filter_map(|effect| match effect {
                    Effect::RaiseError(report) => Some(report.error_type.as_str()),
                    _ => None,
                })
                .collect();
            assert!(
                types.contains(&faults::INOPERATIVE),
                "the fault set did not block charging: {types:?}"
            );
            assert!(
                tripped.contains(&Effect::AllowPowerOn(false)),
                "the port was left offering power: {tripped:?}"
            );
        }

        /// The three keys stop being inert: a tolerance wide enough to cover the
        /// draw makes the same measurement no crossing at all.
        #[test]
        fn the_tolerance_setting_decides_whether_the_same_draw_crosses() {
            let mut tight = charging_core();
            assert!(
                armed_soft_oc_timer(&tight.apply(meter(20.0, 0.0, 0.0), now())).is_some(),
                "ten percent should not cover a twenty five percent overdraw"
            );

            let mut wide = charging_core();
            wide.soft_oc = Some(soft_oc::Detection::new(soft_oc::SoftOverCurrentConfig {
                tolerance_percent: 50.0,
                measurement_noise_a: 0.5,
                timeout: std::time::Duration::from_millis(7000),
            }));
            let effects = wide.apply(meter(20.0, 0.0, 0.0), now());
            assert!(
                armed_soft_oc_timer(&effects).is_none(),
                "fifty percent covers it: {effects:?}"
            );
        }

        /// The configured timeout reaches the deadline rather than a constant.
        #[test]
        fn the_configured_timeout_is_the_deadline_that_is_armed() {
            let mut core = charging_core();
            core.soft_oc = Some(soft_oc::Detection::new(soft_oc::SoftOverCurrentConfig {
                tolerance_percent: 10.0,
                measurement_noise_a: 0.5,
                timeout: std::time::Duration::from_millis(6000),
            }));

            assert_eq!(
                armed_soft_oc_timer(&core.apply(meter(20.0, 0.0, 0.0), now())),
                Some(std::time::Duration::from_millis(6000))
            );
        }

        /// `Charger.cpp:1962` drops the latch on a sub limit sample, and the
        /// deadline that was armed for it has to go with it or a vehicle that
        /// corrected itself is stopped anyway.
        #[test]
        fn a_vehicle_that_comes_back_under_the_limit_cancels_the_deadline() {
            let mut core = charging_core();
            core.apply(meter(20.0, 0.0, 0.0), now());

            let effects = core.apply(meter(15.0, 0.0, 0.0), now());
            assert!(
                effects.contains(&Effect::CancelTimer {
                    id: TIMER_SOFT_OVER_CURRENT
                }),
                "the deadline was left armed: {effects:?}"
            );

            // And the deadline, if it were delivered anyway, decides nothing.
            let late = core.apply(
                soft_oc_deadline(),
                now() + std::time::Duration::from_secs(8),
            );
            assert!(raised_report(&late).is_none(), "{late:?}");
        }

        /// The state gate. `check_soft_over_current` is called from `Charging`
        /// and `ChargingPausedEV` only, so an overdraw before the offer stands
        /// is measured by nothing.
        #[test]
        fn a_draw_before_the_charging_state_is_not_measured() {
            let mut core = core();
            core.apply(Event::Startup, now());
            core.apply(plug_in(), now());
            assert_eq!(core.path.state(), AcState::WaitingForAuthentication);

            let effects = core.apply(meter(40.0, 40.0, 40.0), now());
            assert!(
                armed_soft_oc_timer(&effects).is_none(),
                "measured outside the two charging states: {effects:?}"
            );
        }

        /// A DC port holds no detector, so the same record decides nothing.
        /// This is the mode gate the C++ writes as the AC branch of the
        /// charging state.
        #[test]
        fn a_dc_port_runs_no_soft_overcurrent_check() {
            let mut core = dc_core();
            assert!(core.soft_oc.is_none());

            let effects = core.apply(meter(400.0, 400.0, 400.0), now());
            assert!(
                armed_soft_oc_timer(&effects).is_none(),
                "a DC port measured a phase current: {effects:?}"
            );
            assert!(raised_report(&effects).is_none(), "{effects:?}");
        }

        /// `EvseManager.cpp:1157` needs all three phases before it stores any,
        /// so a record carrying none leaves the previous three standing. The
        /// check still runs, which is what the C++ tick does.
        #[test]
        fn a_record_without_all_three_phases_leaves_the_previous_reading_standing() {
            let mut core = charging_core();
            core.apply(meter(20.0, 0.0, 0.0), now());

            // A phaseless record does not lower the stored draw, so the
            // crossing stands and the deadline still trips.
            let quiet = core.apply(meter_without_phases(), now());
            assert!(
                !quiet.contains(&Effect::CancelTimer {
                    id: TIMER_SOFT_OVER_CURRENT
                }),
                "a phaseless record ended the crossing: {quiet:?}"
            );

            let tripped = core.apply(
                soft_oc_deadline(),
                now() + std::time::Duration::from_secs(8),
            );
            assert!(raised_report(&tripped).is_some(), "{tripped:?}");
        }

        /// The transcript carries the crossing and the trigger, and the error
        /// description is the trigger line itself, as one `errstr` is in the
        /// C++.
        #[test]
        fn the_trigger_line_and_the_error_description_are_the_same_string() {
            let mut core = charging_core();
            core.apply(meter(20.0, 1.0, 2.0), now());

            let tripped = core.apply(
                soft_oc_deadline(),
                now() + std::time::Duration::from_secs(8),
            );

            let report = raised_report(&tripped).expect("raised");
            assert!(
                logged(&tripped).contains(&report.description),
                "the transcript and the error disagree: {tripped:?}"
            );
        }

        /// The unplug ends the session and the crossing with it, so the next
        /// vehicle is measured from its own first sample. `clear_own_errors`
        /// already clears the error; what this pins is that the latch went too.
        #[test]
        fn an_unplug_drops_the_standing_crossing() {
            let mut core = charging_core();
            core.apply(meter(20.0, 0.0, 0.0), now());
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::A)), now());
            // The unplug announces the stop and parks on the relays; the
            // session is over only once the board reports them open, which is
            // what `Charger.cpp:1035-1044` waits for and what runs the session
            // end that drops the crossing. A fixture that never closed the
            // contactor reached the same place without this event, which is
            // how this test used to pass by the wrong route.
            assert_eq!(core.path.state(), AcState::StoppingCharging);
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::PowerOff)), now());

            // Same vehicle behavior on a fresh session, thirty seconds later:
            // a new crossing rather than one that has already expired.
            core.apply(plug_in(), now() + std::time::Duration::from_secs(30));
            core.apply(
                authorize(true, AuthorizationKind::Eim),
                now() + std::time::Duration::from_secs(30),
            );
            core.apply(
                Event::Bsp(BspEvent::Cp(CpEvent::C)),
                now() + std::time::Duration::from_secs(30),
            );

            let effects = core.apply(
                meter(20.0, 0.0, 0.0),
                now() + std::time::Duration::from_secs(30),
            );
            assert!(
                armed_soft_oc_timer(&effects).is_some(),
                "the new session inherited the old crossing: {effects:?}"
            );
            assert!(raised_report(&effects).is_none(), "{effects:?}");
        }

        /// The second C++ call site, `Charger.cpp:888`: the check runs while
        /// the vehicle has paused as well as while it is drawing. A vehicle in
        /// state B still has a nominal duty cycle offered to it on basic AC, so
        /// a draw there is measured against the same limit.
        #[test]
        fn the_check_runs_while_the_vehicle_has_paused() {
            let mut core = charging_core();
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::B)), now());
            assert_eq!(core.path.state(), AcState::ChargingPausedEv);

            let effects = core.apply(meter(20.0, 0.0, 0.0), now());
            assert!(
                armed_soft_oc_timer(&effects).is_some(),
                "the paused state did not measure: {effects:?}"
            );

            let tripped = core.apply(
                soft_oc_deadline(),
                now() + std::time::Duration::from_secs(8),
            );
            assert!(raised_report(&tripped).is_some(), "{tripped:?}");
        }

        /// `ChargingPausedEVSE` is the state the C++ deliberately does not
        /// check in, and a crossing latched before it survives it: the deadline
        /// delivered inside the pause discharges nothing, and the first sample
        /// after the offer returns trips at once rather than restarting the
        /// window.
        #[test]
        fn a_crossing_survives_an_evse_side_pause_and_trips_on_resume() {
            let t0 = now();
            let mut core = charging_core();
            core.apply(meter(20.0, 0.0, 0.0), t0);

            core.apply(
                Event::Command(Command::PauseCharging),
                t0 + std::time::Duration::from_secs(1),
            );
            // The pause crosses the stopping entry and waits there for the
            // relays, which the `StoppingCharging` arm waits for, so the
            // board reporting them open is what settles it into the state
            // under test.
            core.apply(
                Event::Bsp(BspEvent::Cp(CpEvent::PowerOff)),
                t0 + std::time::Duration::from_secs(1),
            );
            assert_eq!(core.path.state(), AcState::ChargingPausedEvse);

            let inside = core.apply(soft_oc_deadline(), t0 + std::time::Duration::from_secs(8));
            assert!(
                raised_report(&inside).is_none(),
                "raised from a state the C++ does not check in: {inside:?}"
            );

            // The resume restores the offer and lands in `PrepareCharging`;
            // the vehicle asking for power again is what re-enters `Charging`,
            // and none of the three states touches the latch.
            core.apply(
                Event::Command(Command::ResumeCharging),
                t0 + std::time::Duration::from_secs(9),
            );
            core.apply(
                Event::Bsp(BspEvent::Cp(CpEvent::C)),
                t0 + std::time::Duration::from_secs(9),
            );
            assert_eq!(core.path.state(), AcState::Charging);

            let resumed = core.apply(
                meter(20.0, 0.0, 0.0),
                t0 + std::time::Duration::from_secs(9),
            );
            assert!(
                raised_report(&resumed).is_some(),
                "the crossing was forgotten across the pause: {resumed:?}"
            );
        }

        /// The same detection on the other AC path.
        ///
        /// Every test above drives `AcBasic`. `AcHlc` answers the signalled
        /// current from a different branch and shapes its command stream, so
        /// the wiring from a meter record through to the raise is its own cell.
        #[test]
        fn the_hlc_path_detects_an_overdraw_and_stops_the_session() {
            let t0 = now();
            let mut core = ac_hlc_core();
            core.apply(Event::Startup, t0);
            core.apply(plug_in(), t0);
            core.apply(authorize(true, AuthorizationKind::Eim), t0);
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::C)), t0);
            assert_eq!(core.path.state(), AcState::Charging);

            let started = core.apply(meter(20.0, 0.0, 0.0), t0);
            assert!(
                armed_soft_oc_timer(&started).is_some(),
                "the hlc path did not measure: {started:?}"
            );

            let tripped = core.apply(soft_oc_deadline(), t0 + std::time::Duration::from_secs(8));
            let report = raised_report(&tripped).expect("the error was not raised");
            assert_eq!(report.error_type, faults::MREC4_OVER_CURRENT_FAILURE);
        }

        /// `Effect::SetOvercurrentLimit` is the board's HARDWARE limit and has
        /// nothing to do with this. It travels on the limit, not on a
        /// measurement, and a soft crossing does not move it.
        #[test]
        fn a_soft_crossing_does_not_touch_the_hardware_overcurrent_limit() {
            let mut core = charging_core();

            let started = core.apply(meter(20.0, 0.0, 0.0), now());
            let tripped = core.apply(
                soft_oc_deadline(),
                now() + std::time::Duration::from_secs(8),
            );

            for effects in [started, tripped] {
                assert!(
                    !effects
                        .iter()
                        .any(|effect| matches!(effect, Effect::SetOvercurrentLimit(_))),
                    "the soft check reached the board limit: {effects:?}"
                );
            }
        }
    }

    /// The AC phase switching break driven through `Core::apply`: which route
    /// an accepted change takes, what the break does to the pilot, and what
    /// the board is told at the end of it.
    ///
    /// The reducer's own decisions live in `path::iec`. What these drive is the
    /// wiring: the enforced limits handler choosing a route, the path running
    /// the break, and the deadline coming back through the timer.
    mod phase_switching {
        use super::*;

        use crate::core::path::ac::TIMER_SWITCH_PHASES;

        /// A board that can switch phases, three of them active, a vehicle
        /// drawing power.
        fn charging_core() -> Core {
            let mut core = core();
            core.apply(Event::Startup, now());
            core.apply(
                Event::Bsp(BspEvent::Capabilities(HardwareCapabilities {
                    max_current_a_import: 32.0,
                    min_current_a_import: 6.0,
                    max_phase_count_import: 3,
                    min_phase_count_import: 1,
                    supports_changing_phases_during_charging: true,
                    supports_cp_state_e: false,
                    ..HardwareCapabilities::default()
                })),
                now(),
            );
            core.apply(plug_in(), now());
            core.apply(authorize(true, AuthorizationKind::Eim), now());
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::C)), now());
            // The board reports the contactor it was just told to close. A
            // helper that skipped this would leave the reducer believing the
            // relays are open and would answer the switching break's
            // shortcut with the wrong vehicle.
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::PowerOn)), now());
            assert_eq!(core.path.state(), AcState::Charging);
            core
        }

        /// An enforced limit set carrying a phase count, which is the only
        /// producer of a phase change.
        fn phase_count(count: i64) -> Event {
            Event::EnforcedLimits(Box::new(energy::enforce::EnforcedLimits {
                uuid: "evse_manager".to_owned(),
                valid_for_s: 60,
                schedule: Vec::new(),
                limits_root_side: energy::enforce::LimitsRes {
                    ac_max_current_a: Some(energy::flow_request::NumberWithSource::new(
                        16.0, "test",
                    )),
                    ac_max_phase_count: Some(energy::flow_request::IntegerWithSource::new(
                        count,
                        "energy_manager",
                    )),
                    ..energy::enforce::LimitsRes::default()
                },
            }))
        }

        fn break_deadline() -> Event {
            Event::Timer {
                id: TIMER_SWITCH_PHASES,
                generation: 1,
            }
        }

        fn switched(effects: &[Effect]) -> Option<bool> {
            effects.iter().find_map(|effect| match effect {
                Effect::SwitchThreePhases(three) => Some(*three),
                _ => None,
            })
        }

        /// The whole feature: a narrowing request mid charge takes the pilot
        /// off, announces the break, waits, then moves the relays and restores
        /// the offer.
        #[test]
        fn a_phase_change_mid_charge_runs_the_break_before_moving_the_relays() {
            let t0 = now();
            let mut core = charging_core();

            let entered = core.apply(phase_count(1), t0);

            assert_eq!(core.path.state(), AcState::SwitchPhases);
            assert!(
                switched(&entered).is_none(),
                "the relays moved under load: {entered:?}"
            );
            assert!(
                entered.contains(&Effect::SetCpState(effect::CpState::X1)),
                "the offer stayed on the pilot: {entered:?}"
            );
            assert!(
                published_events(&entered).contains(&SessionEvent::SwitchingPhases),
                "the break was not announced: {entered:?}"
            );
            assert!(
                entered.iter().any(|effect| matches!(
                    effect,
                    Effect::StartTimer { id, after }
                        if *id == TIMER_SWITCH_PHASES
                            && *after == std::time::Duration::from_secs(10)
                )),
                "the configured delay was not armed: {entered:?}"
            );

            let done = core.apply(break_deadline(), t0 + std::time::Duration::from_secs(10));

            assert_eq!(switched(&done), Some(false), "{done:?}");
            // The return state is crossed and announced on the way through,
            // and this vehicle held state C so the break's shortcut charges it
            // again in the same pass. Both edges are published, in order.
            let events = published_events(&done);
            assert!(
                events.contains(&SessionEvent::PrepareCharging),
                "the return state was not announced: {done:?}"
            );
            assert!(
                events.contains(&SessionEvent::ChargingStarted),
                "the resumed charge was not announced: {done:?}"
            );
            assert_eq!(core.path.state(), AcState::Charging);
        }

        /// `Charger.cpp:1534` records the value and returns true, so the count
        /// the energy manager asked for is what this node republishes from that
        /// pass on, before the relays have moved. The watt to current
        /// conversion divides by it too.
        #[test]
        fn the_published_phase_count_follows_the_request_ahead_of_the_relays() {
            let mut core = charging_core();
            core.apply(phase_count(1), now());
            assert_eq!(core.session().limits.nr_of_phases_available, 1);
        }

        /// Every state but the charging one takes the direct board call
        /// (`Charger.cpp:1542`), and no break is entered.
        #[test]
        fn a_phase_change_before_the_vehicle_draws_reaches_the_board_at_once() {
            let mut core = core();
            core.apply(Event::Startup, now());
            core.apply(
                Event::Bsp(BspEvent::Capabilities(HardwareCapabilities {
                    max_current_a_import: 32.0,
                    min_current_a_import: 6.0,
                    max_phase_count_import: 3,
                    min_phase_count_import: 1,
                    supports_changing_phases_during_charging: true,
                    supports_cp_state_e: false,
                    ..HardwareCapabilities::default()
                })),
                now(),
            );
            core.apply(plug_in(), now());
            assert_eq!(core.path.state(), AcState::WaitingForAuthentication);

            let effects = core.apply(phase_count(1), now());

            assert_eq!(switched(&effects), Some(false), "{effects:?}");
            assert_ne!(core.path.state(), AcState::SwitchPhases);
        }

        /// A board that cannot switch is told nothing and the count stays
        /// where it is (`energyImpl.cpp:424-429`).
        #[test]
        fn a_board_that_cannot_switch_gets_no_break_and_no_command() {
            let mut core = core();
            core.apply(Event::Startup, now());
            core.apply(
                Event::Bsp(BspEvent::Capabilities(HardwareCapabilities {
                    max_current_a_import: 32.0,
                    min_current_a_import: 6.0,
                    max_phase_count_import: 3,
                    min_phase_count_import: 1,
                    supports_changing_phases_during_charging: false,
                    supports_cp_state_e: false,
                    ..HardwareCapabilities::default()
                })),
                now(),
            );
            core.apply(plug_in(), now());
            core.apply(authorize(true, AuthorizationKind::Eim), now());
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::C)), now());

            let effects = core.apply(phase_count(1), now());

            assert!(switched(&effects).is_none(), "{effects:?}");
            assert_eq!(core.path.state(), AcState::Charging);
            assert_eq!(core.session().limits.nr_of_phases_available, 3);
        }

        /// An unplug during the break still moves the relays, and the session
        /// comes down through the stop route rather than being dropped at rest.
        #[test]
        fn an_unplug_during_the_break_still_moves_the_relays() {
            let mut core = charging_core();
            core.apply(phase_count(1), now());

            let effects = core.apply(Event::Bsp(BspEvent::Cp(CpEvent::A)), now());

            assert_eq!(switched(&effects), Some(false), "{effects:?}");
            assert!(
                published_events(&effects).contains(&SessionEvent::StoppingCharging),
                "the stop was not announced: {effects:?}"
            );
            // Parked on the relays, not finished: the session ends when the
            // board reports them open. Asserted because this fixture used to
            // leave the contactor believed open, which ran the session all the
            // way to rest here and hid which route the unplug takes.
            assert_eq!(core.path.state(), AcState::StoppingCharging);
        }

        /// The whole round trip a compliant vehicle takes, end to end: it
        /// answers the withdrawn offer by opening S2, so it waits for the
        /// offer to come back and asks for power itself.
        ///
        /// The counterpart to
        /// `a_vehicle_that_holds_state_c_through_the_break_resumes_and_is_measured_again`
        /// below. The two vehicle behaviours reach `Charging` by different
        /// routes and both have to work: this one through its own state C
        /// edge, the other through the break's shortcut.
        #[test]
        fn a_vehicle_that_answers_the_break_resumes_on_its_own_state_c() {
            let t0 = now();
            let mut core = charging_core();
            core.apply(phase_count(1), t0);

            // The vehicle opens S2 and the board reports the relays open,
            // neither of which ends the break.
            core.apply(
                Event::Bsp(BspEvent::Cp(CpEvent::B)),
                t0 + std::time::Duration::from_secs(1),
            );
            core.apply(
                Event::Bsp(BspEvent::Cp(CpEvent::PowerOff)),
                t0 + std::time::Duration::from_secs(1),
            );
            assert_eq!(core.path.state(), AcState::SwitchPhases, "the break ended");

            let done = core.apply(break_deadline(), t0 + std::time::Duration::from_secs(10));
            assert_eq!(switched(&done), Some(false), "{done:?}");
            assert_eq!(
                core.path.state(),
                AcState::PrepareCharging,
                "power was granted to a vehicle in state B"
            );
            assert!(
                !done.contains(&Effect::AllowPowerOn(true)),
                "power was granted to a vehicle in state B: {done:?}"
            );

            // Its own state C is what resumes it.
            let resumed = core.apply(
                Event::Bsp(BspEvent::Cp(CpEvent::C)),
                t0 + std::time::Duration::from_secs(11),
            );

            assert_eq!(core.path.state(), AcState::Charging);
            assert!(
                published_events(&resumed).contains(&SessionEvent::ChargingStarted),
                "the resumed charge was not announced: {resumed:?}"
            );
            assert_eq!(core.session().limits.nr_of_phases_available, 1);
            assert!(
                resumed.contains(&Effect::AllowPowerOn(true)),
                "power was not restored: {resumed:?}"
            );
        }

        /// The strand the captain ruled on, end to end.
        ///
        /// A vehicle that ignores the withdrawn offer and holds control pilot
        /// state C through the whole break gets no fresh state C afterwards.
        /// Before the shortcut the port settled in `PrepareCharging` with the
        /// contactor still closed, the offer restored and the vehicle still
        /// drawing, which is a session physically charging under a state that
        /// runs no soft overcurrent check and never announced
        /// `ChargingStarted`. This drives the whole thing through
        /// `Core::apply`, so the announcement and the resumed check are
        /// asserted where a consumer would see them.
        #[test]
        fn a_vehicle_that_holds_state_c_through_the_break_resumes_and_is_measured_again() {
            let t0 = now();
            let mut core = charging_core();
            core.apply(phase_count(1), t0);
            assert_eq!(core.path.state(), AcState::SwitchPhases);

            // No CpEvent::B and no PowerOff: the vehicle never answered.
            let done = core.apply(break_deadline(), t0 + std::time::Duration::from_secs(10));

            assert_eq!(switched(&done), Some(false), "{done:?}");
            assert_eq!(
                core.path.state(),
                AcState::Charging,
                "the session stranded rather than resuming"
            );
            assert!(
                published_events(&done).contains(&SessionEvent::ChargingStarted),
                "the resumed charge was never announced: {done:?}"
            );
            assert!(
                done.contains(&Effect::AllowPowerOn(true)),
                "power was not granted again: {done:?}"
            );
            assert_eq!(core.session().limits.nr_of_phases_available, 1);

            // And the state it resumed into is one that measures again, which
            // is the half of the strand that mattered: an overdraw after the
            // break is detected rather than ignored.
            let measured = core.apply(
                Event::Meter(MeterReading {
                    energy_wh_import: 1.0,
                    power_w: None,
                    voltage_v: 230.0,
                    current_a: 20.0,
                    phase_currents_a: Some(soft_oc::PhaseCurrents {
                        l1_a: 20.0,
                        l2_a: 0.0,
                        l3_a: 0.0,
                    }),
                                dc_voltage_v: None,
            }),
                t0 + std::time::Duration::from_secs(11),
            );
            assert!(
                measured.iter().any(|effect| matches!(
                    effect,
                    Effect::StartTimer { id, .. } if *id == soft_oc::TIMER_SOFT_OVER_CURRENT
                )),
                "the resumed session is not measured: {measured:?}"
            );
        }

        /// The two features I own, interacting: a soft overcurrent crossing is
        /// already standing when a phase change takes the session into a
        /// break.
        ///
        /// The break withdraws the offer, so the signalled current is zero for
        /// its whole length and a check that ran would read every residual
        /// ampere as a crossing. `the_break_runs_no_soft_overcurrent_check`
        /// covers a crossing that starts inside a break; this is the harder
        /// direction, a latch that is already set when the break begins, and
        /// what it pins is that the deadline delivered inside the break
        /// discharges nothing.
        #[test]
        fn a_standing_crossing_does_not_trip_inside_a_phase_switching_break() {
            let t0 = now();
            let mut core = charging_core();

            // A crossing latches while charging.
            let started = core.apply(
                Event::Meter(MeterReading {
                    energy_wh_import: 1.0,
                    power_w: None,
                    voltage_v: 230.0,
                    current_a: 20.0,
                    phase_currents_a: Some(soft_oc::PhaseCurrents {
                        l1_a: 20.0,
                        l2_a: 0.0,
                        l3_a: 0.0,
                    }),
                                dc_voltage_v: None,
            }),
                t0,
            );
            assert!(
                started.iter().any(|effect| matches!(
                    effect,
                    Effect::StartTimer { id, .. } if *id == soft_oc::TIMER_SOFT_OVER_CURRENT
                )),
                "no crossing latched: {started:?}"
            );

            // The phase change takes the session into the break.
            core.apply(phase_count(1), t0 + std::time::Duration::from_secs(1));
            assert_eq!(core.path.state(), AcState::SwitchPhases);

            // The overcurrent deadline arrives while the break is running.
            let inside = core.apply(
                Event::Timer {
                    id: soft_oc::TIMER_SOFT_OVER_CURRENT,
                    generation: 1,
                },
                t0 + std::time::Duration::from_secs(8),
            );
            assert!(
                raised_report(&inside).is_none(),
                "the break tripped a crossing against a withdrawn offer: {inside:?}"
            );
            assert_eq!(core.path.state(), AcState::SwitchPhases, "the break ended");

            // And the break still completes on its own deadline.
            let done = core.apply(break_deadline(), t0 + std::time::Duration::from_secs(11));
            assert_eq!(switched(&done), Some(false), "{done:?}");
        }

        /// A second request while the break runs replaces what the board will
        /// be told and does not restart the wait.
        #[test]
        fn a_second_request_during_the_break_supersedes_the_first() {
            let t0 = now();
            let mut core = charging_core();
            core.apply(phase_count(1), t0);

            let again = core.apply(phase_count(3), t0 + std::time::Duration::from_secs(2));
            assert!(
                !again.iter().any(|effect| matches!(
                    effect,
                    Effect::StartTimer { id, .. } if *id == TIMER_SWITCH_PHASES
                )),
                "the break restarted: {again:?}"
            );

            let done = core.apply(break_deadline(), t0 + std::time::Duration::from_secs(10));
            assert_eq!(switched(&done), Some(true), "{done:?}");
        }

        /// Soft overcurrent detection does not run during the break, which is
        /// what stops the withdrawn offer from reading every residual ampere as
        /// a crossing. `Charger.cpp` calls the check from two states and this
        /// is not one of them.
        #[test]
        fn the_break_runs_no_soft_overcurrent_check() {
            let mut core = charging_core();
            core.apply(phase_count(1), now());

            let effects = core.apply(
                Event::Meter(MeterReading {
                    energy_wh_import: 1.0,
                    power_w: None,
                    voltage_v: 230.0,
                    current_a: 20.0,
                    phase_currents_a: Some(soft_oc::PhaseCurrents {
                        l1_a: 20.0,
                        l2_a: 0.0,
                        l3_a: 0.0,
                    }),
                                dc_voltage_v: None,
            }),
                now(),
            );

            assert!(
                !effects.iter().any(|effect| matches!(
                    effect,
                    Effect::StartTimer { id, .. } if *id == soft_oc::TIMER_SOFT_OVER_CURRENT
                )),
                "the break measured a draw: {effects:?}"
            );
        }
    }

    /// The two pass-through variables the core owns: `selected_protocol` and
    /// `car_manufacturer`.
    ///
    /// The other six on that group are republications of a record the boundary
    /// received and are pinned by `main.rs`'s `pass_through_variables` source
    /// ledger, because no test can hold a `ModulePublisher`. These two are
    /// decisions, so they are driven here.
    mod what_the_pass_through_variables_report {
        use super::*;
        use crate::core::hlc::manufacturer::CarManufacturer;

        /// The protocol string in `effects`, or `None` if it was not
        /// republished.
        fn protocol(effects: &[Effect]) -> Option<String> {
            effects.iter().find_map(|effect| match effect {
                Effect::PublishSelectedProtocol(protocol) => Some(protocol.clone()),
                _ => None,
            })
        }

        /// The car `effects` named, or `None`.
        fn car(effects: &[Effect]) -> Option<CarManufacturer> {
            effects.iter().find_map(|effect| match effect {
                Effect::PublishCarManufacturer(manufacturer) => Some(*manufacturer),
                _ => None,
            })
        }

        /// Every vehicle record `effects` published, in order.
        ///
        /// A list rather than the first one, because the property under test is
        /// an accumulator: what a later publication still carries is the half
        /// that a per-field consumer reads, and a reader that stopped at the
        /// first would not see it.
        fn records(effects: &[Effect]) -> Vec<EvInfo> {
            effects
                .iter()
                .filter_map(|effect| match effect {
                    Effect::PublishEvInfo(info) => Some(info.clone()),
                    _ => None,
                })
                .collect()
        }

        /// Which of the twenty announcements carry the protocol behind them,
        /// driven as the whole list.
        ///
        /// The split is not a property of the event's meaning: it is which
        /// `Charger` signal the C++ raises it on, and the one
        /// `publish_selected_protocol` call sits at the end of the
        /// `signal_simple_event` connection. `ChargingPausedEvse` is the trap
        /// and it is in this list on the false side: that lambda has an arm for
        /// it, so a port that read the lambda rather than the call sites would
        /// publish after it.
        #[test]
        fn only_the_announcements_the_cpp_signals_simply_carry_the_protocol() {
            let mut core = core();
            core.apply(Event::Startup, now());

            let mut carried = Vec::new();
            for event in EVERY_SESSION_EVENT {
                let effects = core.session_event(*event);
                if protocol(&effects).is_some() {
                    carried.push(*event);
                }
            }

            assert_eq!(
                carried,
                vec![
                    SessionEvent::Authorized,
                    SessionEvent::Deauthorized,
                    SessionEvent::Enabled,
                    SessionEvent::Disabled,
                    SessionEvent::AuthRequired,
                    SessionEvent::PrepareCharging,
                    SessionEvent::ChargingStarted,
                    SessionEvent::ChargingPausedEv,
                    SessionEvent::SwitchingPhases,
                    SessionEvent::StoppingCharging,
                    SessionEvent::ChargingFinished,
                    SessionEvent::SessionFinished,
                    SessionEvent::PluginTimeout,
                ]
            );
        }

        /// The ready sequence publishes it, because the boot enable
        /// announcement is one of the thirteen. This is the first value any
        /// consumer sees and it is `Unknown`: no `setup_*` function
        /// `EvseManager::ready` reaches writes the field.
        #[test]
        fn the_boot_announcement_reports_no_protocol() {
            let mut core = core();

            let effects = core.apply(Event::Startup, now());

            assert_eq!(protocol(&effects).as_deref(), Some("Unknown"));
        }

        /// An availability change reports it too, and through the other of the
        /// two effects that announce a session event. Both go through
        /// `Core::announce_enable`, which is what stops the pair from drifting.
        #[test]
        fn an_availability_change_reports_the_protocol() {
            let mut core = core();
            core.apply(Event::Startup, now());

            let effects = core.apply(
                Event::Command(Command::EnableDisable {
                    source: EnableSource::LocalApi,
                    state: EnableState::Disable,
                    priority: 1,
                    scope: EnableScope::Connector,
                }),
                now(),
            );

            assert!(
                effects
                    .iter()
                    .any(|effect| matches!(effect, Effect::PublishEnableEvent { .. })),
                "the availability change was announced: {effects:?}"
            );
            assert_eq!(protocol(&effects).as_deref(), Some("Unknown"));
        }

        /// A session opening says basic AC, on the next announcement rather
        /// than on the start itself: the C++ writes the field in the session
        /// started lambda and publishes nothing there.
        #[test]
        fn an_open_session_reports_basic_ac_on_the_next_announcement() {
            let mut core = core();
            core.apply(Event::Startup, now());

            let start = core.session_event(SessionEvent::SessionStarted);
            assert_eq!(protocol(&start), None, "the start itself says nothing");

            let next = core.session_event(SessionEvent::AuthRequired);
            assert_eq!(protocol(&next).as_deref(), Some("IEC61851-1"));
        }

        /// A DC port reports basic AC too, between the session opening and
        /// the stack negotiating.
        ///
        /// That reads wrong and is what the C++ does: the assignment is the
        /// first statement of the session started lambda, gated on neither the
        /// charge mode nor whether a stack is wired. A port that gated it on
        /// the mode would report `Unknown` for that window, which is a
        /// different answer from the C++'s on every DC session.
        #[test]
        fn a_dc_port_also_reports_basic_ac_when_a_session_opens() {
            let mut core = dc_core();
            core.apply(Event::Startup, now());

            core.session_event(SessionEvent::SessionStarted);
            let next = core.session_event(SessionEvent::AuthRequired);

            assert_eq!(protocol(&next).as_deref(), Some("IEC61851-1"));
        }

        /// And a DC port that has negotiated reports what it negotiated, which
        /// is the value that window exists to be replaced by.
        #[test]
        fn a_dc_port_reports_what_the_stack_negotiated() {
            let mut core = dc_core();
            core.apply(Event::Startup, now());
            core.session_event(SessionEvent::SessionStarted);

            core.apply(
                Event::Hlc(HlcEvent::SelectedProtocol(
                    "urn:iso:15118:2:2013:MsgDef".to_owned(),
                )),
                now(),
            );
            let next = core.session_event(SessionEvent::ChargingStarted);

            assert_eq!(
                protocol(&next).as_deref(),
                Some("urn:iso:15118:2:2013:MsgDef")
            );
        }

        /// The stack's own spelling reaches the wire, and reaches it on the
        /// next announcement rather than at once, which is what the C++
        /// one-assignment handler does.
        #[test]
        fn a_negotiated_protocol_reaches_the_wire_verbatim() {
            let mut core = core();
            core.apply(Event::Startup, now());
            core.session_event(SessionEvent::SessionStarted);

            let negotiated = core.apply(
                Event::Hlc(HlcEvent::SelectedProtocol("ISO15118-2".to_owned())),
                now(),
            );
            assert!(
                negotiated.is_empty(),
                "the negotiation announces nothing: {negotiated:?}"
            );

            let next = core.session_event(SessionEvent::ChargingStarted);
            assert_eq!(protocol(&next).as_deref(), Some("ISO15118-2"));
        }

        /// The end of a session reports no protocol, in the very announcement
        /// that ends it. That ordering is the C++'s: its simple event lambda
        /// writes `Unknown` after publishing the event and before publishing
        /// the protocol.
        #[test]
        fn the_end_of_a_session_reports_no_protocol() {
            let mut core = core();
            core.apply(Event::Startup, now());
            core.session_event(SessionEvent::SessionStarted);
            core.apply(
                Event::Hlc(HlcEvent::SelectedProtocol("ISO15118-2".to_owned())),
                now(),
            );

            let finish = core.session_event(SessionEvent::SessionFinished);

            assert_eq!(protocol(&finish).as_deref(), Some("Unknown"));
        }

        /// The end of a transaction clears it too, and that one is invisible
        /// until the next announcement because `TransactionFinished` has its
        /// own signal in the C++.
        #[test]
        fn the_end_of_a_transaction_clears_the_protocol_for_the_next_announcement() {
            let mut core = core();
            core.apply(Event::Startup, now());
            core.session_event(SessionEvent::SessionStarted);
            core.apply(
                Event::Hlc(HlcEvent::SelectedProtocol("ISO15118-2".to_owned())),
                now(),
            );

            let finished = core.session_event(SessionEvent::TransactionFinished);
            assert_eq!(protocol(&finished), None, "that event says nothing");

            let next = core.session_event(SessionEvent::StoppingCharging);
            assert_eq!(protocol(&next).as_deref(), Some("Unknown"));
        }

        /// The vehicle names itself and the port names the car.
        ///
        /// `subscribe_evcc_id` is the C++'s only `publish_car_manufacturer`
        /// site, and the same handler is where the autocharge identity is
        /// remembered, so one arrival produces both.
        #[test]
        fn a_session_setup_names_the_car_the_address_belongs_to() {
            let mut core = dc_core();
            core.apply(Event::Startup, now());

            let effects = core.apply(
                Event::Hlc(HlcEvent::SessionSetup {
                    evcc_id: "DC:44:27:1A:BB:CC".to_owned(),
                }),
                now(),
            );

            assert_eq!(car(&effects), Some(CarManufacturer::Tesla), "{effects:?}");
        }

        /// An AC port with a stack names the car too. The C++ gate on that
        /// handler is `autocharge_use_slac_instead_of_hlc` and nothing else -
        /// not the charge mode, and not `hlc_enabled`, which it computes after
        /// installing the subscription - so an AC high level communication port
        /// is one of the two deployments that reach it.
        #[test]
        fn an_ac_port_with_a_stack_also_names_the_car() {
            let mut core = ac_hlc_core();
            core.apply(Event::Startup, now());

            let effects = core.apply(
                Event::Hlc(HlcEvent::SessionSetup {
                    evcc_id: "00:7D:FA:11:22:33".to_owned(),
                }),
                now(),
            );

            assert_eq!(
                car(&effects),
                Some(CarManufacturer::VolkswagenGroup),
                "{effects:?}"
            );
        }

        /// An address in no named range names no car, and says so rather than
        /// saying nothing: `Unknown` is a value the interface declares and the
        /// C++ publishes it.
        #[test]
        fn an_unrecognized_address_names_no_car_out_loud() {
            let mut core = dc_core();
            core.apply(Event::Startup, now());

            let effects = core.apply(
                Event::Hlc(HlcEvent::SessionSetup {
                    evcc_id: "AA:BB:CC:DD:EE:FF".to_owned(),
                }),
                now(),
            );

            assert_eq!(car(&effects), Some(CarManufacturer::Unknown), "{effects:?}");
        }

        /// A port taking the autocharge identity from SLAC installs no
        /// `subscribe_evcc_id` handler at all in the C++, so it publishes no
        /// manufacturer either. The gate is one condition covering both halves
        /// of that handler, which is why it is not a second gate here.
        #[test]
        fn a_port_taking_its_identity_from_slac_names_no_car() {
            let mut core = dc_core_with_autocharge_from_slac();
            core.apply(Event::Startup, now());

            let effects = core.apply(
                Event::Hlc(HlcEvent::SessionSetup {
                    evcc_id: "DC:44:27:1A:BB:CC".to_owned(),
                }),
                now(),
            );

            assert_eq!(car(&effects), None, "{effects:?}");
        }

        /// The MAC address SLAC reports is the alternative source of the same
        /// identity, and it names no car: the C++ derives the manufacturer in
        /// the stack handler only, and the two handlers are mutually exclusive.
        #[test]
        fn the_slac_address_names_no_car() {
            let mut core = dc_core_with_autocharge_from_slac();
            core.apply(Event::Startup, now());

            let effects = core.apply(
                Event::Hlc(HlcEvent::VehicleMacAddress("DC:44:27:1A:BB:CC".to_owned())),
                now(),
            );

            assert_eq!(car(&effects), None, "{effects:?}");
        }

        /// A port with no stack wired hears neither of the two facts, so it
        /// names no car and reports the boot protocol for its whole life.
        /// `core()` is that port: `AcBasic` with `hlc: None`.
        #[test]
        fn a_port_with_no_stack_names_no_car() {
            let mut core = core();
            core.apply(Event::Startup, now());

            let effects = core.apply(
                Event::Hlc(HlcEvent::SessionSetup {
                    evcc_id: "DC:44:27:1A:BB:CC".to_owned(),
                }),
                now(),
            );

            assert_eq!(car(&effects), None, "{effects:?}");
        }

        /// The vehicle names itself and the port publishes the record carrying
        /// it, from the same arrival that names the car.
        ///
        /// This is the field `OCPP201` stamps onto the stop token of
        /// `TransactionEvent(Ended)`, and with no record at all its cache stays
        /// empty and the stamp never happens.
        #[test]
        fn a_session_setup_publishes_the_identity_the_vehicle_gave() {
            let mut core = dc_core();
            core.apply(Event::Startup, now());

            let effects = core.apply(
                Event::Hlc(HlcEvent::SessionSetup {
                    evcc_id: "DC:44:27:1A:BB:CC".to_owned(),
                }),
                now(),
            );

            assert_eq!(
                records(&effects),
                vec![EvInfo {
                    evcc_id: Some("DC:44:27:1A:BB:CC".to_owned()),
                    soc: None,
                }],
                "{effects:?}"
            );
        }

        /// A port taking its identity from SLAC installs no handler at all in
        /// the C++, so it publishes no record either. One gate covers all three
        /// halves of that handler.
        #[test]
        fn a_port_taking_its_identity_from_slac_publishes_no_record() {
            let mut core = dc_core_with_autocharge_from_slac();
            core.apply(Event::Startup, now());

            let effects = core.apply(
                Event::Hlc(HlcEvent::SessionSetup {
                    evcc_id: "DC:44:27:1A:BB:CC".to_owned(),
                }),
                now(),
            );

            assert_eq!(records(&effects), Vec::new(), "{effects:?}");
        }

        /// The record accumulates: a state of charge arriving after the
        /// identity republishes both, which is what makes a per-field consumer
        /// able to read either one at any time.
        #[test]
        fn a_state_of_charge_republishes_the_identity_beside_it() {
            let mut core = dc_core();
            core.apply(Event::Startup, now());
            core.apply(
                Event::Hlc(HlcEvent::SessionSetup {
                    evcc_id: "DC:44:27:1A:BB:CC".to_owned(),
                }),
                now(),
            );

            let effects = core.apply(
                Event::Hlc(HlcEvent::StateOfCharge { percent: 42.0 }),
                now(),
            );

            assert_eq!(
                records(&effects),
                vec![EvInfo {
                    evcc_id: Some("DC:44:27:1A:BB:CC".to_owned()),
                    soc: Some(42.0),
                }],
                "{effects:?}"
            );
        }

        /// An AC port hears the same fact and publishes no record for it. The
        /// C++ installs that subscription inside its `charge_mode == "DC"`
        /// branch, and the `ac_with_soc` deployment reaches the figure through
        /// the mode flip instead, which publishes nothing.
        #[test]
        fn an_ac_port_publishes_no_record_for_a_state_of_charge() {
            let mut core = ac_hlc_core();
            core.apply(Event::Startup, now());

            let effects = core.apply(
                Event::Hlc(HlcEvent::StateOfCharge { percent: 42.0 }),
                now(),
            );

            assert_eq!(records(&effects), Vec::new(), "{effects:?}");
        }

        /// Both ends of a session publish an empty record, so one vehicle's
        /// identity cannot outlive the session that produced it.
        #[test]
        fn both_ends_of_a_session_publish_an_empty_record() {
            let mut core = dc_core();
            core.apply(Event::Startup, now());

            let start = core.session_event(SessionEvent::SessionStarted);
            assert_eq!(records(&start), vec![EvInfo::default()], "{start:?}");

            core.apply(
                Event::Hlc(HlcEvent::SessionSetup {
                    evcc_id: "DC:44:27:1A:BB:CC".to_owned(),
                }),
                now(),
            );

            let finish = core.session_event(SessionEvent::SessionFinished);
            assert_eq!(records(&finish), vec![EvInfo::default()], "{finish:?}");
        }

        /// And the reset is a forget, not just an announcement: the next fact
        /// to arrive rebuilds the record from empty rather than republishing
        /// the departed vehicle's identity beside it.
        #[test]
        fn the_next_session_does_not_republish_the_departed_vehicle() {
            let mut core = dc_core();
            core.apply(Event::Startup, now());
            core.apply(
                Event::Hlc(HlcEvent::SessionSetup {
                    evcc_id: "DC:44:27:1A:BB:CC".to_owned(),
                }),
                now(),
            );
            core.session_event(SessionEvent::SessionFinished);

            let effects = core.apply(
                Event::Hlc(HlcEvent::StateOfCharge { percent: 42.0 }),
                now(),
            );

            assert_eq!(
                records(&effects),
                vec![EvInfo {
                    evcc_id: None,
                    soc: Some(42.0),
                }],
                "{effects:?}"
            );
        }

        /// A port with no stack publishes the two lifecycle records and nothing
        /// else. The C++ resets and publishes above its own `hlc_enabled`
        /// guard, so a deployment with no source for either field still says
        /// the empty record at both ends.
        #[test]
        fn a_port_with_no_stack_still_publishes_the_lifecycle_records() {
            let mut core = core();
            core.apply(Event::Startup, now());

            let start = core.session_event(SessionEvent::SessionStarted);
            let finish = core.session_event(SessionEvent::SessionFinished);

            assert_eq!(records(&start), vec![EvInfo::default()], "{start:?}");
            assert_eq!(records(&finish), vec![EvInfo::default()], "{finish:?}");
        }
    }

    /// Three lifecycle side effects the C++ performs and the port did not, one
    /// shape: something outside the state machine has to happen when a verdict
    /// names a reservation, when an operator forces the connector open, and
    /// when a fatal fault takes the port down.
    ///
    /// They are one module because they were one omission. Each is a second
    /// consumer of an event the port already handled, and each was missed by
    /// reading only the first consumer.
    mod lifecycle_side_effects {
        use super::*;

        /// A live billing record on the plainest port there is.
        fn charging_ac_basic() -> Core {
            let mut core = core_up();
            core.apply(plug_in(), now());
            core.apply(authorize(true, AuthorizationKind::Eim), now());
            core.apply(Event::Bsp(BspEvent::Cp(CpEvent::C)), now());
            assert!(core.session.transaction_active, "a record is open");
            core
        }

        /// An accepted verdict that names a reservation.
        fn verdict_naming(reservation_id: Option<i64>) -> Event {
            Event::Command(Command::AuthorizeResponse {
                token: id_tag_for_tests("DEADBEEF", false),
                status: AuthorizationStatus::Accepted,
                certificate: None,
                tariff: TariffMessages::default(),
                reservation_id,
            })
        }

        /// The reservation a published payload names, or a panic.
        fn reservation_on(effects: &[Effect], event: SessionEvent) -> Option<i64> {
            match report_for(effects, event).payload {
                Some(SessionPayload::Started { reservation_id, .. })
                | Some(SessionPayload::TransactionStarted { reservation_id, .. }) => reservation_id,
                other => panic!("{event:?} carried {other:?}"),
            }
        }

        /// A raised fault of the given severity, which is what decides the
        /// shutdown class (`Severity::High` is the emergency one).
        fn fatal(severity: Severity) -> Event {
            Event::Error(ErrorEvent {
                source: ErrorSource::Bsp,
                error_type: "evse_board_support/MREC8EmergencyStop".to_owned(),
                sub_type: String::new(),
                vendor_id: String::new(),
                severity,
                raised: true,
            })
        }

        /// The non evse specific reservation, end to end. Nothing ever calls
        /// `handle_reserve` for one, so the verdict is the only place its id is
        /// offered and the transaction event is the only place it can land.
        ///
        /// The ordering is the interesting half: the grant opens the session
        /// and the session announces itself before `:461` records the id, so
        /// `SessionStarted` names nothing and the transaction behind it names
        /// the reservation. Recording the id any earlier would put it on both
        /// events, which is a `reservationId` the C++ does not send.
        #[test]
        fn a_verdict_that_names_a_reservation_re_arms_it_for_the_transaction() {
            let mut core = core_up();

            let opened = core.apply(verdict_naming(Some(5)), now());
            assert_eq!(
                reservation_on(&opened, SessionEvent::SessionStarted),
                None,
                "the start it opened cannot name an id recorded after it, got {opened:?}"
            );

            let plugged = core.apply(plug_in(), now());
            assert_eq!(
                reservation_on(&plugged, SessionEvent::TransactionStarted),
                Some(5),
                "the transaction names it, got {plugged:?}"
            );

            for pass in [&opened, &plugged] {
                assert!(
                    !pass
                        .iter()
                        .any(|effect| published(effect, SessionEvent::ReservationStart)
                            || published(effect, SessionEvent::ReservationEnd)),
                    "`reserve(id, false)` is silent, got {pass:?}"
                );
            }
        }

        /// The command route, which is `mod->reserve(id, true)`: the same
        /// refusals and the announcement the silent route suppresses.
        ///
        /// It had none of them. A reservation reached this module as a store
        /// and an announcement whatever the port was doing, so an operator
        /// reserving a bay that was mid charge got a `ReservationStart` for a
        /// reservation the C++ refuses outright, and the id it overwrote was
        /// the one a live transaction was going to be billed under.
        #[test]
        fn a_reservation_is_refused_once_the_port_has_left_idle() {
            let mut core = core_up();
            core.apply(plug_in(), now());
            assert_ne!(core.path.state(), AcState::Idle, "the control");

            let refused = core.apply(
                Event::Command(Command::Reserve { reservation_id: 7 }),
                now(),
            );

            assert!(refused.is_empty(), "got {refused:?}");
            assert_eq!(core.session.reservation_id, None, "and none is held");
        }

        /// `enable_disable` answers the arbitration, not the request.
        ///
        /// The second case where the old answer was knowably wrong rather than
        /// merely optimistic: intake echoed the requested state, so a source
        /// asking to enable a port that a stronger source is holding disabled
        /// was told it had succeeded. `Charger::enable_disable` returns
        /// `is_enabled` off the table (`Charger.cpp:1774`).
        #[test]
        fn an_outranked_enable_answers_the_arbitration_and_not_the_request() {
            let mut core = core_up();
            // Lower priority value wins, so this one outranks what follows.
            core.apply(
                Event::Command(Command::EnableDisable {
                    source: EnableSource::ServiceTechnician,
                    state: EnableState::Disable,
                    priority: 0,
                    scope: EnableScope::Connector,
                }),
                now(),
            );

            let answer = awaited(
                &mut core,
                Command::EnableDisable {
                    source: EnableSource::Csms,
                    state: EnableState::Enable,
                    priority: 5,
                    scope: EnableScope::Connector,
                },
            );

            assert!(
                !answer,
                "the request was Enable and the technician's Disable still stands"
            );
        }

        /// The same command, with nothing outranking it, answers true. Without
        /// this the test above passes on a port that answers false to every
        /// enable.
        #[test]
        fn an_uncontested_enable_answers_true() {
            let mut core = core_up();

            let answer = awaited(
                &mut core,
                Command::EnableDisable {
                    source: EnableSource::Csms,
                    state: EnableState::Enable,
                    priority: 5,
                    scope: EnableScope::Connector,
                },
            );

            assert!(answer);
        }

        /// The three that answer whether a transaction was open.
        ///
        /// All three sit inside `flag_transaction_active` in the C++ and answer
        /// false when it does not hold (`Charger.cpp:1330-1345`, `:1367-1394`).
        /// Intake answered true unconditionally.
        #[test]
        fn pause_resume_and_stop_answer_false_with_no_transaction() {
            for command in [
                Command::PauseCharging,
                Command::ResumeCharging,
                Command::StopTransaction {
                    reason: StopTransactionReason::Local,
                    id_tag: None,
                },
            ] {
                let mut core = core_up();
                assert!(!core.session.transaction_active, "the control");

                let answer = awaited(&mut core, command.clone());

                assert!(!answer, "{command:?} found no transaction to act on");
            }
        }

        /// And answer true once one is open, so the three above are not passing
        /// by refusing everything.
        #[test]
        fn pause_resume_and_stop_answer_true_with_a_transaction() {
            for command in [
                Command::PauseCharging,
                Command::ResumeCharging,
                Command::StopTransaction {
                    reason: StopTransactionReason::Local,
                    id_tag: None,
                },
            ] {
                let mut core = core_up();
                core.session.transaction_active = true;

                let answer = awaited(&mut core, command.clone());

                assert!(answer, "{command:?} had a transaction to act on");
            }
        }

        /// What the wire is told about a refusal.
        ///
        /// The defect this pins: a refused reservation answered `true`, so the
        /// module contradicted a decision it had made one call earlier and a
        /// CSMS recorded a reservation the port had rejected. `handle_reserve`
        /// returns `mod->reserve(id, true)`, and both of the C++ refusals
        /// return false (`EvseManager.cpp:1735-1773`).
        #[test]
        fn a_refused_reservation_answers_false_on_the_wire() {
            let mut core = core_up();
            core.apply(plug_in(), now());
            assert_ne!(core.path.state(), AcState::Idle, "the control");

            let answer = awaited(&mut core, Command::Reserve { reservation_id: 7 });

            assert!(!answer);
        }

        /// The same question the other way round, so the test above cannot pass
        /// by answering `false` to everything.
        #[test]
        fn an_accepted_reservation_answers_true_on_the_wire() {
            let mut core = core_up();

            let answer = awaited(&mut core, Command::Reserve { reservation_id: 7 });

            assert!(answer);
            assert_eq!(core.session.reservation_id, Some(7));
        }

        /// An accept that deliberately announces nothing is still an accept.
        ///
        /// `EvseManager.cpp:1758-1771` returns true from inside the accept
        /// branch whether or not it signalled, so re-making a reservation under
        /// its own id is a success with no `ReservationStart`. Reading the
        /// verdict off the effect list would have called this a refusal, which
        /// is why `Core::reserve` returns it instead.
        #[test]
        fn a_silent_overwrite_answers_true_and_announces_nothing() {
            let mut core = core_up();
            core.apply(
                Event::Command(Command::Reserve { reservation_id: 7 }),
                now(),
            );

            let (effects, answer) =
                awaited_with_effects(&mut core, Command::Reserve { reservation_id: 7 });

            assert!(answer, "an overwrite of its own id is accepted");
            assert!(
                !effects
                    .iter()
                    .any(|effect| matches!(effect, Effect::PublishSessionEvent(_))),
                "and says nothing: {effects:?}"
            );
        }

        /// The overwrite rule on the route that announces. A second operator
        /// reserving the same bay under a different id is refused, and the
        /// refusal is silent: the reservation that stands is not re-announced
        /// either.
        #[test]
        fn a_reservation_under_a_different_id_is_refused_and_says_nothing() {
            let mut core = core_up();
            core.apply(
                Event::Command(Command::Reserve { reservation_id: 7 }),
                now(),
            );

            let refused = core.apply(
                Event::Command(Command::Reserve { reservation_id: 9 }),
                now(),
            );

            assert!(refused.is_empty(), "got {refused:?}");
            assert_eq!(core.session.reservation_id, Some(7), "the held id wins");
        }

        /// `EvseManager.cpp:1759-1761`: the one accepted case that announces
        /// nothing. A reservation re-made under its own nameable id is an
        /// overwrite, and a CSMS told twice that the same reservation started
        /// would count two.
        #[test]
        fn a_reservation_re_made_under_its_own_id_is_accepted_in_silence() {
            let mut core = core_up();
            let first = core.apply(
                Event::Command(Command::Reserve { reservation_id: 7 }),
                now(),
            );
            assert!(
                first
                    .iter()
                    .any(|effect| published(effect, SessionEvent::ReservationStart)),
                "the first is announced, got {first:?}"
            );

            let again = core.apply(
                Event::Command(Command::Reserve { reservation_id: 7 }),
                now(),
            );

            assert!(again.is_empty(), "got {again:?}");
            assert_eq!(core.session.reservation_id, Some(7));
        }

        /// The sentinel, on both sides. A negative id reserves the connector
        /// and names no reservation (`:1755-1757`), so it stores `-1` rather
        /// than itself, and it announces because the id that ends up held is
        /// the sentinel (`:1759`). A nameable id may then replace it.
        #[test]
        fn a_reservation_for_no_nameable_id_holds_the_sentinel() {
            let mut core = core_up();

            let held = core.apply(
                Event::Command(Command::Reserve { reservation_id: -4 }),
                now(),
            );

            assert_eq!(
                core.session.reservation_id,
                Some(-1),
                "a negative id is not stored as itself"
            );
            assert!(
                held.iter()
                    .any(|effect| published(effect, SessionEvent::ReservationStart)),
                "got {held:?}"
            );

            let replaced = core.apply(
                Event::Command(Command::Reserve { reservation_id: 5 }),
                now(),
            );

            assert_eq!(core.session.reservation_id, Some(5));
            assert!(
                replaced
                    .iter()
                    .any(|effect| published(effect, SessionEvent::ReservationStart)),
                "replacing the sentinel is not an overwrite, got {replaced:?}"
            );
        }

        /// `EvseManager::reserve`'s idle refusal (`EvseManager.cpp:1738-1740`),
        /// which is the one that shows on the wire. A vehicle that arrived
        /// first has already taken the port past `Idle`, so the C++ drops the
        /// id and the transaction it opens carries none.
        ///
        /// Reproduced deliberately rather than improved on: the refusal defeats
        /// the stated intent of `:456-460` and the port matching it keeps the
        /// difference a decision rather than a surprise.
        #[test]
        fn a_verdict_reservation_is_refused_once_the_port_has_left_idle() {
            let mut core = core_up();
            core.apply(plug_in(), now());
            assert_eq!(
                core.path.state(),
                AcState::WaitingForAuthentication,
                "the vehicle arrived first"
            );

            let effects = core.apply(verdict_naming(Some(5)), now());

            assert_eq!(
                reservation_on(&effects, SessionEvent::TransactionStarted),
                None,
                "got {effects:?}"
            );
            assert_eq!(core.session.reservation_id, None, "and none is held");
        }

        /// The C++ overwrite rule (`EvseManager.cpp:1744-1756`): an id already
        /// held wins, and the same id is an overwrite rather than a refusal.
        ///
        /// Driven at the deciding function on the silent route, because no
        /// production route reaches the pair there. A verdict that opens a
        /// session spends the held id on `SessionStarted` before this runs, and
        /// every route that keeps one held has taken the port past `Idle`,
        /// where the refusal above answers first.
        #[test]
        fn a_verdict_cannot_replace_a_reservation_the_module_still_holds() {
            let mut core = core_up();
            core.session.reservation_id = Some(7);

            core.reserve(5, false);
            assert_eq!(core.session.reservation_id, Some(7), "the held id wins");

            core.reserve(7, false);
            assert_eq!(
                core.session.reservation_id,
                Some(7),
                "the same id is an overwrite, not a refusal"
            );

            // The C++ sentinel. `AuthHandler::check_evse_reserved_and_send_updates`
            // calls `reserve(evse, -1)` for a non evse specific reservation,
            // and `EvseManager::reserve` answers that by staying reserved with
            // no id it can name (`:1752-1756`), which its own accept condition
            // reads as empty. A verdict may therefore replace it.
            core.session.reservation_id = Some(-1);
            core.reserve(5, false);
            assert_eq!(
                core.session.reservation_id,
                Some(5),
                "a held sentinel is not an id a verdict has to respect"
            );
        }

        /// The safety half of `evse_managerImpl::handle_force_unlock`
        /// (`evse/evse_managerImpl.cpp:515-524`): the record closes while the
        /// connector is still held.
        ///
        /// Both facts are asserted, not just the pair's existence. A transaction
        /// that finished after `Effect::UnlockConnector` would bill the
        /// operator's unlock inside the session it ended, and a port that only
        /// unlocked - which is what this arm did - leaves the record open
        /// forever.
        #[test]
        fn force_unlock_closes_the_record_before_it_opens_the_connector() {
            let mut core = charging_ac_basic();

            let effects = core.apply(Event::Command(Command::ForceUnlock), now());

            let finished = effects
                .iter()
                .position(|effect| {
                    matches!(
                        effect,
                        Effect::PublishSessionEvent(report)
                            if report.event == SessionEvent::TransactionFinished
                                && report.payload
                                    == Some(SessionPayload::TransactionFinished {
                                        reason: StopTransactionReason::UnlockCommand,
                                        id_tag: None,
                                    })
                    )
                })
                .unwrap_or_else(|| panic!("the record must close as an unlock, got {effects:?}"));
            let unlocked = effects
                .iter()
                .position(|effect| *effect == Effect::UnlockConnector)
                .unwrap_or_else(|| panic!("the connector must open, got {effects:?}"));

            assert!(finished < unlocked, "got {effects:?}");
        }

        /// The metering record closes before the connector opens too, which is
        /// the fact a billing consumer reads rather than the announcement.
        #[test]
        fn force_unlock_stops_the_metering_transaction_before_it_opens_the_connector() {
            let mut core = charging_ac_basic();

            let effects = core.apply(Event::Command(Command::ForceUnlock), now());

            let stopped = effects
                .iter()
                .position(|effect| matches!(effect, Effect::StopTransaction { .. }))
                .unwrap_or_else(|| panic!("the meter must be told, got {effects:?}"));
            let unlocked = effects
                .iter()
                .position(|effect| *effect == Effect::UnlockConnector)
                .expect("the connector must open");

            assert!(stopped < unlocked, "got {effects:?}");
        }

        /// The safety half the other two do not pin: the contactor is
        /// commanded open before the lock is.
        ///
        /// An operator unlock during a live session must not open the lock
        /// while anything downstream still believes power is flowing. The
        /// de-energize belongs to the teardown rather than to this arm, so the
        /// two order assertions above go on passing if it ever stops
        /// happening: they watch the record close, not the power stop.
        ///
        /// The guarded case needs no such check. Whatever closed the
        /// transaction already de-energized, so an unlock that finds no record
        /// open finds the port in safe state.
        #[test]
        fn force_unlock_de_energizes_before_it_opens_the_connector() {
            let mut core = charging_ac_basic();

            let effects = core.apply(Event::Command(Command::ForceUnlock), now());

            let de_energized = effects
                .iter()
                .position(|effect| *effect == Effect::AllowPowerOn(false))
                .unwrap_or_else(|| panic!("the contactor must be told, got {effects:?}"));
            let unlocked = effects
                .iter()
                .position(|effect| *effect == Effect::UnlockConnector)
                .unwrap_or_else(|| panic!("the connector must open, got {effects:?}"));

            assert!(de_energized < unlocked, "got {effects:?}");
        }

        /// `Charger::cancel_transaction`'s own guard (`Charger.cpp:1430`): a
        /// stop that finds no transaction names no reason. The unlock still
        /// happens, because the C++ calls `connector_force_unlock`
        /// unconditionally.
        #[test]
        fn force_unlock_with_no_record_open_still_opens_the_connector() {
            let mut core = core_up();

            let effects = core.apply(Event::Command(Command::ForceUnlock), now());

            assert!(
                effects.contains(&Effect::UnlockConnector),
                "got {effects:?}"
            );
            assert_eq!(
                effects,
                vec![Effect::UnlockConnector],
                "an unlock with no record open opens the lock and nothing else"
            );
            assert_eq!(
                core.session.stop_reason, None,
                "a reason recorded outside a transaction would end the next one"
            );
            assert_eq!(
                core.session.phase,
                SessionPhase::Idle,
                "and it does not put an idle session into a stopping route"
            );
        }

        /// `EvseManager.cpp:1288-1293`, the second consumer of
        /// `error_handling->signal_error`. Both classes are driven, because the
        /// C++ arm tests for both and a fix that reached one would look right.
        #[test]
        fn a_fatal_fault_cancels_an_active_reservation() {
            for (severity, class) in [(Severity::High, "emergency"), (Severity::Medium, "error")] {
                let mut core = core_up();
                core.apply(
                    Event::Command(Command::Reserve { reservation_id: 5 }),
                    now(),
                );

                let effects = core.apply(fatal(severity), now());

                assert!(
                    effects
                        .iter()
                        .any(|effect| published(effect, SessionEvent::ReservationEnd)),
                    "{class}: the reservation must end, got {effects:?}"
                );
                assert_eq!(core.session.reservation_id, None, "{class}: and be spent");
            }
        }

        /// The safe state is still reached first, which is the reason the
        /// announcement is appended rather than inserted: a publish must not
        /// come between a fault and the contactors opening.
        #[test]
        fn a_fatal_fault_opens_the_contactors_before_it_announces_anything() {
            let mut core = core_up();
            core.apply(
                Event::Command(Command::Reserve { reservation_id: 5 }),
                now(),
            );

            let effects = core.apply(fatal(Severity::High), now());

            let opened = effects
                .iter()
                .position(|effect| *effect == Effect::AllowPowerOn(false))
                .unwrap_or_else(|| panic!("the contactors must open, got {effects:?}"));
            let announced = effects
                .iter()
                .position(|effect| published(effect, SessionEvent::ReservationEnd))
                .expect("the reservation must end");

            assert!(opened < announced, "got {effects:?}");
        }

        /// The C++ `if (reserved)` guard inside `cancel_reservation`
        /// (`EvseManager.cpp:1778`). A fatal fault is a common event and an
        /// unreserved connector is the common case, so an ungated announcement
        /// would tell every consumer that a reservation it never heard of has
        /// ended.
        #[test]
        fn a_fatal_fault_on_an_unreserved_connector_announces_nothing() {
            for severity in [Severity::High, Severity::Medium] {
                let mut core = core_up();

                let effects = core.apply(fatal(severity), now());

                assert!(
                    !effects
                        .iter()
                        .any(|effect| published(effect, SessionEvent::ReservationEnd)),
                    "got {effects:?}"
                );
            }
        }

        /// The same guard on the command route, which is where the C++ shares
        /// one function between three callers.
        #[test]
        fn cancelling_a_reservation_that_was_never_made_announces_nothing() {
            let mut core = core_up();

            let effects = core.apply(Event::Command(Command::CancelReservation), now());

            assert!(effects.is_empty(), "got {effects:?}");
        }
    }
}
