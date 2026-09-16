// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

//! Outbound intents produced by the core.
//!
//! The core returns effects as values and never performs them. A single outbound
//! EVerest command can block for 300 seconds and the Rust bindings expose no way
//! to bound it, so effects are executed off the writer thread.

use std::marker::PhantomData;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;
use std::time::Duration;

use super::enable::EnableEntry;
use super::event::{HlcSessionFailure, PowerSupplyCapabilities, ReplyToken, Severity};
use super::hlc::ac_params::{AcParameters, AcPowerSet, Power};
use super::hlc::authz::{AuthorizationResponse, ProvidedToken};
use super::hlc::cable_check::IsolationStatus;
use super::hlc::dc_limits::{MaximumLimits, MinimumLimits, PhysicalValues};
use super::hlc::session::SessionSetup;
use super::hlc::manufacturer::CarManufacturer;
use super::hlc::{BptSetup, EvInfo, SaeBidiMode};
use super::path::dc::OverVoltageThresholds;
use super::session::{
    EnergyTransferMode, Limits, PauseReason, SessionEvent, StartSessionReason,
    StopTransactionReason,
};
use super::session_log::SessionLogEffect;
use super::token::IdTag;

/// Device ownership for ordered outbound commands. Each optional device is a
/// single slot in this module. No order is promised between different devices.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Ord, PartialOrd)]
pub enum Device {
    Supply,
    Bsp,
    Imd,
    OverVoltageMonitor,
    ConnectorLock,
}

/// The normal execution domain of an effect.
///
/// Device and store lanes execute FIFO through local runner completion. An RPC
/// timeout does not cancel a request already sent to a peer. Publish retains the
/// existing ordering between announcements, metering and HLC calls. There is no
/// new cross-device or device/publish/store ordering guarantee.
///
/// The executor's `safety_context` policy retains the existing safety bypass;
/// those commands are explicitly excepted from device FIFO until the shutdown
/// versus in-flight RPC contract is decided.
///
/// Every variant here is a lane the executor actually runs. There is no lane for
/// effects the executor never sees: `Effect::context` answers `None` for those.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Ord, PartialOrd)]
pub enum ExecContext {
    Safety,
    Publish,
    Device(Device),
    Store,
}

/// Identifies one requested effect, so its completion can be attributed to the
/// stage that asked for it. Chosen by the core, never by the boundary: an
/// identifier the core has not seen is one it cannot correlate.
///
/// The field is private and `EffectIds::allocate` is the only producer, so that
/// sentence is now what the compiler enforces rather than what this comment
/// asks for.
///
/// This is the erased form: what an effect carries out and what a completion
/// carries back. Every identity is comparable with every other one, whoever
/// asked for it, which is what keeps the one space one. The owner tag lives on
/// `Issued`, beside this, and never on this.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Hash, PartialOrd, Ord)]
pub struct EffectId(u64);

mod owner {
    /// Closes the owner roster. A marker declared anywhere else cannot
    /// implement `EffectOwner`, so a third correlator is added here beside the
    /// two or not at all, and a second space cannot arrive as a new tag.
    pub trait Sealed {}

    impl Sealed for super::ByCore {}
    impl Sealed for super::ByPath {}
}

/// Who a handle on the one space belongs to.
///
/// The tag rides in a `PhantomData`, so it cannot reach the counter and cannot
/// change a number. It decides exactly one thing: whose await slot an identity
/// may be put into.
pub trait EffectOwner: owner::Sealed + Copy + Eq + std::fmt::Debug {}

/// The core's own awaits: the metering transaction start, today.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ByCore;

/// The power path the core drives. One tag for all of them, because
/// `PowerPath` is a trait object and cannot carry a per-path tag; the paths
/// that await nothing simply never allocate.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ByPath;

impl EffectOwner for ByCore {}
impl EffectOwner for ByPath {}

/// Monotonic source of `EffectId`: one space per core, not one per state
/// machine that awaits something.
///
/// `one_space` starts that space and `delegate` is the only other handle on it,
/// never a second counter. That is what lets `Core` and the power path it
/// drives both await completions without either being able to claim the
/// other's verdict. Allocation still happens only during `Core::apply`, so it
/// stays deterministic under test in the way a `TimerId` constant already is.
///
/// It has to be one space, because `Core` correlates ahead of the paths rather
/// than beside them: `Core::apply` offers every `EffectDone` to its own await
/// before `on_effect_done` reaches a path. An identity both could be waiting on
/// is therefore one the wrong await answers, in both directions. Two
/// independent allocators, each starting at zero, once let a DC isolation
/// monitor self test be passed by a powermeter reply; `architecture.md` records
/// it.
///
/// Monotonic rather than a fixed constant per purpose, because a request that is
/// abandoned and reissued must not be satisfied by the abandoned verdict. This
/// is what a timer generation does for `TimerId`.
///
/// Ownership is a tag beside the counter and not a partition of it. There is
/// one `Arc<AtomicU64>` per port; `one_space` is its only origin, `delegate` its
/// only other handle, and neither `Clone` nor `Default` exists to make a third
/// way. `O` is a `PhantomData` no arithmetic can see, and the identities it
/// tags all erase to the one comparable `EffectId`. A tag can therefore refuse
/// an await, and can never split the numbers in two.
#[derive(Debug)]
pub struct EffectIds<O: EffectOwner> {
    next: Arc<AtomicU64>,
    owner: PhantomData<O>,
}

impl EffectIds<ByCore> {
    /// Starts the one space a port has.
    ///
    /// `pub(super)` rather than `pub`, so nothing in `boundary` or `main.rs`
    /// can start a space: the loop carries identities and mints none.
    /// `Core::new` is its only production caller, and it delegates in the next
    /// line, so the pair a port runs on cannot come from two roots.
    pub(super) fn one_space() -> Self {
        Self {
            next: Arc::new(AtomicU64::new(0)),
            owner: PhantomData,
        }
    }

    /// A space for a fixture that drives one half of the port on its own.
    ///
    /// The one door tests come through, so `one_space` itself keeps exactly one
    /// production call site and `grep` can say so. Every identity a fixture
    /// names is still allocated, never written.
    #[cfg(test)]
    pub(crate) fn one_space_for_tests() -> Self {
        Self::one_space()
    }

    /// The power path's handle on this same counter.
    ///
    /// The one delegation edge there is: it goes core to path, and
    /// `EffectIds<ByPath>` has no `delegate` of its own, so the graph of
    /// handles on a space is one root and one delegate rather than a tree
    /// anything can extend.
    pub fn delegate(&self) -> EffectIds<ByPath> {
        EffectIds {
            next: Arc::clone(&self.next),
            owner: PhantomData,
        }
    }
}

impl<O: EffectOwner> EffectIds<O> {
    /// Takes `&mut self` although the counter is shared: allocating is a write,
    /// and a caller holding this by shared reference is one that believes it is
    /// only reading.
    pub fn allocate(&mut self) -> Issued<O> {
        Issued {
            id: EffectId(self.next.fetch_add(1, Ordering::Relaxed)),
            owner: PhantomData,
        }
    }
}

/// One identity, together with the owner that issued it.
///
/// An await slot holds this rather than a bare `EffectId`, so the slot's type
/// says who is allowed to fill it: `Core::answer_transaction_start` awaits an
/// `Issued<ByCore>` and there is no way to make one out of an identity a path
/// allocated, or out of the identity a completion carries back.
///
/// It is not a second identity type. `effect_id` erases it to the one
/// `EffectId` every owner's identities share, and `answers` compares against
/// exactly that, so nothing here can make two identities incomparable.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Issued<O: EffectOwner> {
    id: EffectId,
    owner: PhantomData<O>,
}

impl<O: EffectOwner> Issued<O> {
    /// The erased identity, as the effect carries it out and the completion
    /// carries it back.
    pub fn effect_id(self) -> EffectId {
        self.id
    }

    /// Whether the completion the loop delivered is the verdict on this
    /// request.
    ///
    /// `None` answers nothing: an effect no stage awaited completes without an
    /// identity, and treating that as the awaited one is how an unrelated
    /// failure used to read as a refused billing start.
    pub fn answers(self, delivered: Option<EffectId>) -> bool {
        delivered == Some(self.id)
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, Hash, PartialOrd, Ord)]
pub struct TimerId(pub u64);

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum CpState {
    X1,
    F,
    E,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SupplyMode {
    Off,
    Export,
    Import,
}

/// The charging phase a mode change belongs to, reported with it.
///
/// `EvseManager` holds this as `power_supply_DC_charging_phase`, assigned by the
/// three ISO 15118 subscribers that begin a phase and returned to `Other` when
/// the supply goes off. A driver is entitled to act on it: one that is told
/// `Export` under `Other` may decline to energize, because
/// `types/power_supply_DC.yaml` defines `Other` as "switching it off or any
/// other internal testing not related to real charging".
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub enum ChargingPhase {
    #[default]
    Other,
    CableCheck,
    PreCharge,
    Charging,
}

/// One error raise or clear on this module's own `evse_manager` interface.
///
/// Raise and clear carry the same payload so that both are built at one site and
/// neither can grow a field the other lacks. A clear reaches a framework call
/// that takes the type alone, so the remaining fields describe the error being
/// withdrawn rather than parameterizing the call.
///
/// `message` and `description` are two different values, not one written twice.
/// The C++ `raise_inoperative_error` puts the primary cause's fully qualified
/// type in `message` and the joined short descriptions in `description`, and an
/// OCPP 1.6 `StatusNotification` renders them into different fields:
/// `info` from `message`, `vendorErrorCode` from `description` and `vendorId`
/// from `vendor_id` (`get_error_info` in `modules/EVSE/OCPP/OCPP.cpp`). Filling
/// any of the three from another loses the one it overwrote.
#[derive(Clone, Debug, PartialEq)]
pub struct ErrorReport {
    pub error_type: String,
    pub sub_type: String,
    pub severity: Severity,
    pub vendor_id: String,
    pub description: String,
    pub message: String,
}

/// One session event on the wire, with the identity of the session it belongs
/// to.
///
/// The core builds this at one site (`Core::session_event`), so no producer can
/// emit a session event without its identity: the uuid is not a parameter any
/// of the nine callers passes.
#[derive(Clone, Debug, PartialEq)]
pub struct SessionEventReport {
    /// `shared_context.session_uuid`, empty when the event belongs to no
    /// session. The reservation pair is the only such case
    /// (`evse/evse_managerImpl.cpp:126`).
    pub uuid: String,
    pub event: SessionEvent,
    /// The `SessionStarted` payload, present for that event and no other.
    /// `AuthHandler.cpp:830` reads its `reason` with an unconditional
    /// `.value()`, so a start published without it aborts the Auth module.
    pub started: Option<StartSessionReason>,
    /// What the three payload carrying session events name beyond their event
    /// and their session, present for the event it belongs to and no other.
    ///
    /// Kept as one field rather than three so that the three are mutually
    /// exclusive by construction: `SessionEvent` decides which arm is built,
    /// at the one construction site, and no event can carry two payloads or
    /// the wrong one.
    pub payload: Option<SessionPayload>,
}

/// The payload one session event carries.
///
/// The remaining fields of the three wire payload types are absent here
/// because they are absent from this port, not because they are optional; see
/// `Core::session_event` and `docs/architecture.md`.
#[derive(Clone, Debug, PartialEq)]
pub enum SessionPayload {
    /// `types::evse_manager::SessionStarted`, minus its `reason`, which
    /// `started` above carries because `AuthHandler` aborts without it and
    /// nothing else in this payload is required by a consumer.
    Started {
        /// `evse/evse_managerImpl.cpp`'s session started connection fills this
        /// only when the reservation is still held, and consumes it only on an
        /// authorization first start.
        reservation_id: Option<i64>,
        /// Present only on an authorization first start: the C++ passes
        /// `provided_id_token` to the signal from the `authfirst` branch alone
        /// (`Charger::start_session`), so a plug in start names no token even
        /// when one is already held.
        id_tag: Option<IdTag>,
    },
    /// `types::evse_manager::TransactionStarted`.
    TransactionStarted {
        /// Required by the wire type, and read by every consumer:
        /// `id_tag.id_token` becomes the OCPP `idToken`, `request_id` the
        /// `remoteStartId` and `parent_id_token` the `groupIdToken`.
        id_tag: IdTag,
        reservation_id: Option<i64>,
    },
    /// `types::evse_manager::TransactionFinished`.
    TransactionFinished {
        reason: StopTransactionReason,
        /// "Only present if transaction was stopped locally"
        /// (`types/evse_manager.yaml`).
        id_tag: Option<IdTag>,
    },
    /// `types::evse_manager::ChargingPausedEVSEReasons`, the set the EVSE is
    /// holding the charge for.
    ///
    /// The only payload whose event can be announced more than once in a
    /// session: `Charger` raises it again whenever the set changes while the
    /// state is resident, which is what tells a consumer that a pause it was
    /// told about for want of energy is now also a pause the operator asked
    /// for. Never empty, because an empty set is not a pause.
    ChargingPausedEvse { reasons: Vec<PauseReason> },
}

#[derive(Clone, Debug, PartialEq)]
pub enum Effect {
    // Safety actuation. Anything that removes energy or releases the vehicle.
    AllowPowerOn(bool),
    SupplyOff,
    SetCpState(CpState),
    UnlockConnector,
    /// Control pilot output enable on the board support. `false` must prevent
    /// new charging sessions; `evse_board_support.yaml` lets the board signal
    /// unavailability with a hardware appropriate CP state or a mechanical
    /// block, and requires it to reapply the cached duty cycle when re-enabled.
    BspEnable(bool),

    // Device actuation (the executor retains its explicit safety bypass).
    LockConnector,
    PwmOn(f64),
    PwmOff,
    SetOvercurrentLimit(f64),
    SwitchThreePhases(bool),
    SetSupplyMode {
        mode: SupplyMode,
        /// What the mode change is for. `Off` always reports `Other`, which is
        /// the value `types/power_supply_DC.yaml` defines for switching off.
        phase: ChargingPhase,
    },
    SetSupplySetpoint {
        /// Selects the independent import/export limit even at zero current.
        mode: SupplyMode,
        voltage_v: f64,
        current_a: f64,
    },
    ImdStart,
    ImdStop,
    ImdSelfTest {
        id: EffectId,
        voltage_v: f64,
    },
    /// The two thresholds the over voltage monitor watches for.
    ///
    /// They are not the same number and must not be filled from one: the
    /// emergency limit is the IEC step above what the vehicle and the supply
    /// jointly negotiated, while the error limit is the vehicle's own maximum.
    /// Emergency is evaluated first and immediately; error only after the
    /// monitor's configured duration.
    ///
    /// `OverVoltageThresholds` has private fields and one constructor,
    /// `OverVoltageMonitor::thresholds`, so this variant cannot be written by
    /// anything that does not hold the monitor it is addressed to. It carried
    /// two bare `f64` fields until then, and spent six days as a variant with
    /// no production producer at all.
    OverVoltageLimits(OverVoltageThresholds),
    OverVoltageStart,
    OverVoltageStop,
    /// Open the billing transaction on the powermeter.
    ///
    /// `transaction_id` is `Session::id` and not a second identity. The C++
    /// fills `TransactionReq::transaction_id` from `shared_context.session_uuid`
    /// (`Charger::start_transaction`) and stops on that same string
    /// (`Charger::stop_transaction`), so the session identity on the bus and the
    /// metering transaction identity are one value.
    StartTransaction {
        /// The identity the refusal verdict is correlated against. A meter that
        /// refuses the start takes the port out of service when
        /// `fail_on_powermeter_errors` is on, so the core has to know which
        /// completion is this start's and not some other effect's.
        id: EffectId,
        transaction_id: String,
        /// The identity the accepted authorization carried, whole. Absent when
        /// the authorization named none.
        ///
        /// `Charger::start_transaction` fills two fields of the request off it:
        /// `identification_data` from `id_token.id_token.value` and
        /// `identification_type` from `id_token.id_token.type` through
        /// `utils::convert_to_ocmf_identification_type`. Carried as the one
        /// record rather than as those two facts side by side, because the two
        /// are what a legal metrology meter signs together: a value billed
        /// under another credential's type is a signed record naming the wrong
        /// kind of user, and [`IdTag`]'s single constructor is what makes that
        /// unrepresentable.
        id_token: Option<IdTag>,
        /// The terms the transaction is opened under,
        /// `validation_result.tariff_messages.at(0).content` in
        /// `Charger::start_transaction`, and `None` where the C++ leaves
        /// `tariff_text` unset.
        ///
        /// The selection is already made: `TariffMessages::text` is the one
        /// reader of the list, so the boundary is handed the message rather
        /// than the choice.
        tariff_text: Option<String>,
    },
    /// Close the billing transaction opened under `transaction_id`.
    ///
    /// Never empty: `powermeter.yaml` gives the empty string the meaning "cancel
    /// every ongoing transaction", which is a startup cleanup and not a session
    /// ending.
    StopTransaction {
        transaction_id: String,
    },
    /// Cancel every transaction the powermeter still holds, whoever opened it.
    ///
    /// `powermeter.yaml` gives `stop_transaction` an empty transaction id that
    /// meaning, and says it exists for exactly this: clearing records left
    /// dangling in the meter across a restart. It is its own variant rather than
    /// a `StopTransaction` carrying an empty string, so no session ending route
    /// can reach the cancel-everything semantics by holding an empty identity.
    CancelAllTransactions,
    Persist {
        key: String,
        value: String,
    },
    PersistDelete {
        key: String,
    },
    /// One instruction for the per session transcript
    /// (`modules/EVSE/EvseManager/SessionLog.cpp`).
    ///
    /// The core says what a session owes the transcript and in what order; the
    /// boundary stamps the wall clock, writes the files and renders the EVerest
    /// log line, because `core` holds neither a clock nor a filesystem.
    ///
    /// Never awaited and never fatal. A transcript that cannot be written is
    /// reported and abandoned, so a logging fault cannot stop a charge.
    SessionLog(SessionLogEffect),
    HlcUpdate(HlcUpdate),
    /// What the module tells the SLAC layer.
    ///
    /// The C++ sends these from the same callbacks that inform the charger
    /// (`EvseManager.cpp:377`, `:384`, `:391`), so both halves of a data link
    /// request travel together rather than one of them being a consequence of
    /// the other.
    SlacUpdate(SlacUpdate),
    /// The advertised energy transfer mode set, on this module's own
    /// `evse_manager` interface. Published from the ready sequence
    /// (`EvseManager.cpp:1505`) and again on every derivation change
    /// (`EvseManager::publish_and_update_supported_energy_transfers`).
    ///
    /// A deployment without high level communication publishes the empty set
    /// here rather than nothing, which is what the C++ does: the ready sequence
    /// publishes the monitor unconditionally and only the HLC branch fills it.
    PublishSupportedTransferModes(Vec<EnergyTransferMode>),
    /// Which car the vehicle's own MAC address belongs to, on this module's own
    /// `evse_manager` interface (`EvseManager::publish_car_manufacturer`).
    ///
    /// One producer and one publish site, which is the C++'s: the handler for
    /// the identity the stack reports. The C++ has a second write to the member
    /// it publishes, on plug in, that publishes nothing; see
    /// `HlcPort::note_vehicle_identity` for why that one is not ported.
    PublishCarManufacturer(CarManufacturer),
    /// What the vehicle has said about itself, on this module's own
    /// `evse_manager` interface (`EvseManager::publish_ev_info`).
    ///
    /// The C++ publishes the whole accumulated record on every field that
    /// arrives and an empty one at each end of a session, so consumers see a
    /// per-session accumulator and read it field by field. Two fields have a
    /// source here; the rest stay absent, which is what they are.
    PublishEvInfo(EvInfo),
    /// `selected_protocol`, on this module's own `evse_manager` interface
    /// (`evse/evse_managerImpl.cpp:383`).
    ///
    /// Emitted behind the announcement of every session event the C++ raises
    /// through `Charger::signal_simple_event`, which is where its one
    /// `publish_selected_protocol` call sits. `core::protocol::published_after`
    /// is the predicate and carries the enumeration.
    ///
    /// Carries the rendered string rather than the state, because the state has
    /// a variant that carries the stack's own text and rendering it twice would
    /// be two answers to one question.
    PublishSelectedProtocol(String),
    PublishSessionEvent(SessionEventReport),
    /// A token offered to the `Auth` module on this module's own
    /// `token_provider` interface (`p_token_provider->publish_provided_token`,
    /// `EvseManager.cpp:1009` and `:1041`).
    PublishProvidedToken(ProvidedToken),
    /// The stack gave up on a high level communication session
    /// (`EvseManager.cpp:365-370`). Carries the session identity the core owns,
    /// which is what the consumer correlates against `session_event`.
    PublishHlcSessionFailed {
        uuid: Option<String>,
        reason: HlcSessionFailure,
    },
    /// The availability announcement. It carries an ordinary `SessionEvent`, but
    /// it is its own effect because the wire attaches the deciding source to this
    /// pair of events and to no other (`evse/evse_managerImpl.cpp:334-341`).
    /// Keeping it separate is what makes the source impossible to omit.
    PublishEnableEvent {
        event: SessionEvent,
        source: EnableEntry,
    },
    PublishLimits(Limits),
    PublishEnforcedLimits(Box<crate::core::energy::enforce::EnforcedLimits>),
    /// What this node asks the energy manager for, on its own `energy_grid`
    /// interface (`publish_energy_flow_request`, `energyImpl.cpp:320`).
    ///
    /// Boxed. It is by far the largest payload any effect carries, two
    /// schedules of limits with their source strings, and it is emitted once a
    /// second, so the indirection is paid on the one variant that would
    /// otherwise widen every effect in the module.
    PublishEnergyFlowRequest(Box<crate::core::energy::flow_request::FlowRequest>),
    /// The UK random delay countdown, on this module's own `random_delay`
    /// interface (`publish_countdown`, `energyImpl.cpp:491` and `:497`).
    ///
    /// Emitted only while the feature is enabled, which is where both C++
    /// publishes sit: a disabled feature publishes nothing rather than a zero
    /// countdown, so a consumer can tell "no delay" from "no feature".
    PublishRandomDelayCountdown(crate::core::energy::random_delay::CountDown),
    PublishReady(bool),
    /// Raise an error on this module's own `evse_manager` interface.
    RaiseError(ErrorReport),
    /// Withdraw one previously raised there.
    ClearError(ErrorReport),
    /// Whether the ready publish is held back for
    /// `external_ready_to_start_charging` (`EvseManager.cpp:1491`).
    PublishWaitingForExternalReady(bool),

    // Timers are scheduled through the loop so the core stays free of I/O.
    /// The verdict for a command whose caller is blocked on it.
    ///
    /// Not a device action and not an announcement: the writer intercepts it in
    /// `perform` and completes the waiter itself, exactly as it does the two
    /// timer effects. `context` answers `None` for all three for one reason, a
    /// blocked peer must not be able to delay them.
    AnswerCommand {
        reply: ReplyToken,
        answer: bool,
    },
    StartTimer {
        id: TimerId,
        after: Duration,
    },
    CancelTimer {
        id: TimerId,
    },
}

#[derive(Clone, Debug, PartialEq)]
pub enum HlcUpdate {
    /// `call_authorization_response`, the one answer the vehicle waits for.
    ///
    /// Four call sites in the C++ and one variant here, because the four differ
    /// only in the pair of values they carry: the plug and charge grant
    /// (`EvseManager.cpp:1894`), the external identification grant (`:1004`,
    /// `:1902`), a forwarded plug and charge refusal
    /// (`evse/evse_managerImpl.cpp:451`), and the unknown status that escapes
    /// the vehicle's authorization loop when nothing authorized at all
    /// (`EvseManager.cpp:415`).
    AuthorizationResponse(AuthorizationResponse),
    /// `call_cable_check_finished` (`EvseManager.cpp:2026`, `:2287`, `:2433`,
    /// `:2462` and `:2539`). The completion verdict, sent once per cable check
    /// attempt; see `core::hlc::cable_check` for why once and not the C++
    /// count.
    CableCheckFinished(bool),
    /// `call_update_isolation_status` (`EvseManager.cpp:2011`, `:2015`, `:2025`
    /// and `:2260`).
    ///
    /// A separate command from `CableCheckFinished` beside it, and separate
    /// here for the same reason: the two carry different types, so a status and
    /// a verdict cannot be built into each other's variant. That matters
    /// because the C++ sends them adjacently at three of the four sites, and a
    /// port that gave both a `bool` payload would let a transposition compile.
    IsolationStatus(IsolationStatus),
    ContactorClosed(bool),
    /// `call_stop_charging`. One command with two meanings in the C++, both
    /// preserved: `true` asks the vehicle to end the session
    /// (`EvseManager.cpp:412`, from the stopping entry at `Charger.cpp:1020`),
    /// and `false` clears a request left over from the previous session
    /// (`EvseManager.cpp:1126`, on plug in).
    StopCharging(bool),
    /// `call_pause_charging` (`EvseManager.cpp:413`, from the stopping entry at
    /// `Charger.cpp:1017`).
    ///
    /// The alternative to `StopCharging` on the same edge, not an addition to
    /// it: `Charger.cpp:1015-1021` is one `if`/`else`, so exactly one of the two
    /// reaches the vehicle per entry. It is asked only of an ISO 15118-20
    /// session, which is the `hlc_d20_active` half of that gate; the interface
    /// says the same, "only in ISO15118-20"
    /// (`interfaces/ISO15118_charger.yaml:123-128`).
    ///
    /// The payload is the wire `pause` argument. The C++ has one producer and
    /// it passes `true`; `false` is representable because the command is, the
    /// same way `StopCharging` carries both meanings.
    PauseCharging(bool),
    /// Relay of the SLAC data link state (`EvseManager.cpp:1235`).
    DlinkReady(bool),
    /// `call_set_powersupply_capabilities` (`EvseManager.hpp:268-270`), which
    /// the C++ sends only when the report changed.
    ///
    /// Boxed. The report is the whole 24 field wire type, which is an order of
    /// magnitude larger than any other variant here, and it arrives a handful
    /// of times per port lifetime while `DcPresentValues` beside it arrives
    /// every few hundred milliseconds. Paying for the indirection on the rare
    /// one keeps the common one cheap.
    PowerSupplyCapabilities(Box<PowerSupplyCapabilities>),
    /// `call_set_charging_parameters` (`EvseManager.hpp:275-279` for DC,
    /// `EvseManager.cpp:447-449` for AC).
    ChargingParameters(PhysicalValues),
    /// `call_update_dc_minimum_limits` (`EvseManager.hpp:281-286`).
    DcMinimumLimits(MinimumLimits),
    DcMaximumLimits(MaximumLimits),
    /// `call_update_ac_maximum_limits` (`EvseManager.cpp:1842`) and
    /// `call_update_ac_minimum_limits` (`:1861`).
    ///
    /// Two variants over one payload type, because `AcEvseMaximumPower` and
    /// `AcEvseMinimumPower` are field for field identical wire types. The
    /// variant is what says which command a value is bound for, so a value
    /// cannot be sent as the other one.
    AcMaximumLimits(AcPowerSet),
    AcMinimumLimits(AcPowerSet),
    /// `call_update_ac_parameters` (`EvseManager.cpp:1867`), the general AC
    /// announcement: nominal frequency and voltage, the connector list and the
    /// reactive power ceiling.
    AcParameters(AcParameters),
    /// `call_update_ac_max_current` (`EvseManager.cpp:1245`), the ISO 15118-2
    /// spelling of the live AC limit.
    AcMaxCurrent(f64),
    /// `call_update_ac_target_values` (`EvseManager.cpp:1257`), the
    /// ISO 15118-20 spelling of the same limit.
    ///
    /// It carries the target active power alone. The wire type has two more
    /// fields, a target frequency and a target reactive power, and the C++ has
    /// an open `TODO(SL)` for each (`:1255-1256`); neither has a producer here,
    /// so the boundary leaves both absent rather than this variant carrying two
    /// fields nothing fills.
    AcTargetPower(Power),
    /// `call_update_ac_present_power` (`EvseManager.cpp:1186`), the meter's live
    /// power reaching the vehicle.
    AcPresentPower(Power),
    /// `call_update_meter_info` (`EvseManager.cpp:1183`), the billing meter's
    /// whole record reaching the vehicle. It is what the stack renders into
    /// `MeterInfo` on `ChargingStatusRes` and `CurrentDemandRes`, so without it
    /// a vehicle asking for a metered reading is told nothing at all.
    ///
    /// **No payload.** The record is eight nested wire types deep and the core
    /// models the four figures it decides on; carrying the rest as a
    /// serialization on every reading would be a copy per second for a message
    /// the core never reads. The boundary fills it from the same cache the
    /// three session event payloads read, which its own arrival wrote before
    /// this effect existed. So this variant says *when*, and the record says
    /// what.
    MeterInfo,
    /// `call_update_dc_present_values` (`EvseManager.cpp:716`, and the boot
    /// zero at `:558-561`).
    DcPresentValues {
        voltage_v: f64,
        current_a: f64,
    },
    /// An error the vehicle is told about while a session is live
    /// (`EvseManager.cpp:420-426`).
    SendError(EvseError),

    /// Whether the vehicle must be sent a metering receipt
    /// (`EvseManager.cpp:971`).
    ReceiptRequired(bool),
    /// The one time identity and mode announcement (`EvseManager.cpp:974`).
    ///
    /// `debug_mode` is `config.session_logging`. The three values travel in a
    /// named struct because the generated Rust `setup` takes its arguments
    /// alphabetically, `(debug_mode, evse_id, sae_j2847_mode)`, while the
    /// interface and the C++ call both read `(evse_id, sae_mode, debug_mode)`.
    /// A positional port of the C++ call would therefore pass them in the wrong
    /// order.
    Setup {
        evse_id: String,
        evse_id_din: String,
        sae_mode: SaeBidiMode,
        debug_mode: bool,
    },
    /// The bidirectional power transfer setup, sent only when the deployment
    /// names both a channel and a generator mode (`EvseManager.cpp:977-993`).
    BptSetup(BptSetup),
    /// Clear whatever the stack still holds from a previous run
    /// (`EvseManager.cpp:995`).
    ResetError,
    /// The advertised energy transfer mode set, as the stack is told it
    /// (`EvseManager::publish_and_update_supported_energy_transfers`).
    TransferModes(Vec<EnergyTransferMode>),
    /// What the vehicle may pay with and what the module will do about its
    /// contract, re-derived and re-sent at each of the three C++ trigger points
    /// (`EvseManager.cpp:362`, `:1307`, `:1343`).
    ///
    /// The three values travel in a named struct for the same reason `Setup`
    /// does: the generated Rust `session_setup` takes them alphabetically,
    /// `(central_contract_validation_allowed, payment_options,
    /// supported_certificate_service)`, while the interface and the C++ call
    /// both read `(payment_options, supported_certificate_service,
    /// central_contract_validation_allowed)`. The two booleans sit on opposite
    /// ends of the call in the two orders, so a positional port swaps them and
    /// compiles.
    SessionSetup(SessionSetup),
}

/// `types::iso15118::EvseError`, the errors the vehicle can be told about.
///
/// Only the two the module originates. `Charger.cpp:1360` and `:1362` name
/// `Error_EmergencyShutdown` and `Error_UtilityInterruptEvent`, and
/// `Charger.cpp:2268` and `:2283` name the first again. The interface declares
/// three more, `Error_Contactor`, `Error_RCD` and `Error_Malfunction`, which the
/// C++ never sends and this port therefore has no producer for.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum EvseError {
    EmergencyShutdown,
    UtilityInterruptEvent,
}

/// What the module tells the SLAC layer.
///
/// The three data link relays and the three matching lifecycle commands. The
/// lifecycle half has two producers, both ported: the pilot handler
/// (`EvseManager.cpp:1094-1119`), which is `HlcPort::on_pilot`, and
/// `Charger::signal_slac_start` (`EvseManager.cpp:423`), which is
/// `HlcPort::on_session_resume`.
///
/// `Charger::signal_slac_reset` (`:425`) has one of its two emitters ported:
/// the fatal error exit from `WaitingForAuthentication` (`Charger.cpp:319-322`),
/// which is the `HlcPort::on_slac_reset` call in `Core::apply_fault_signals`.
/// The other is `Charger::request_error_sequence`, which this port does not
/// carry at all (see `SlacClientSubscriber::on_request_error_routine` in
/// `main.rs`).
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SlacUpdate {
    /// `enter_bcd`. The pilot entered B, C or D, so matching starts or
    /// restarts. `EvseManager.cpp:1097`, `:1109` and `:423`.
    EnterBcd,
    /// `leave_bcd`. The pilot left B, C or D. `EvseManager.cpp:1099` and
    /// `:1113`.
    LeaveBcd,
    /// `reset(false)`: stop SLAC. The only `reset` the C++ sends - the
    /// `reset(true)` beside it at `EvseManager.cpp:1104` is commented out, so
    /// this variant carries no argument rather than one with a single value.
    ///
    /// Two production call sites, and they are not one fact: `:1115` sends it
    /// inline from the SLAC state report on an unplug, and `:425` relays
    /// `Charger::signal_slac_reset`, which the charger raises from its
    /// `WaitingForAuthentication` fatal error exit (`Charger.cpp:321`) and
    /// from `Charger::request_error_sequence` (`:2140`).
    Reset,
    /// `EvseManager.cpp:377`. Terminate the data link and restart matching.
    DlinkError,
    /// `EvseManager.cpp:384`. Power saving, staying matched.
    DlinkPause,
    /// `EvseManager.cpp:391`. Terminate the data link and become unmatched.
    DlinkTerminate,
}

impl Effect {
    /// Exhaustive by construction. A new variant will not compile until its
    /// execution context is chosen.
    ///
    /// `None` is not a lane and is deliberately not spelled as one: it says the
    /// executor never runs this effect at all. The writer intercepts the two
    /// timer effects in `EventLoop::perform` and arms them against its own timer
    /// service, so they reach no worker. Answering `Some(lane)` for them would
    /// name a queue nothing is ever put on, which is how the retired ordinary
    /// pool kept three threads alive for an empty classification.
    pub fn context(&self) -> Option<ExecContext> {
        match self {
            Effect::AllowPowerOn(_)
            | Effect::SetCpState(_)
            | Effect::BspEnable(_)
            | Effect::PwmOn(_)
            | Effect::PwmOff
            | Effect::SetOvercurrentLimit(_)
            | Effect::SwitchThreePhases(_) => Some(ExecContext::Device(Device::Bsp)),
            Effect::SupplyOff | Effect::SetSupplyMode { .. } | Effect::SetSupplySetpoint { .. } => {
                Some(ExecContext::Device(Device::Supply))
            }
            Effect::LockConnector | Effect::UnlockConnector => Some(ExecContext::Device(Device::ConnectorLock)),
            Effect::ImdStart | Effect::ImdStop | Effect::ImdSelfTest { .. } => Some(ExecContext::Device(Device::Imd)),
            Effect::OverVoltageLimits(_) | Effect::OverVoltageStart | Effect::OverVoltageStop => {
                Some(ExecContext::Device(Device::OverVoltageMonitor))
            }
            Effect::Persist { .. } | Effect::PersistDelete { .. } => Some(ExecContext::Store),

            // Writes on this module's own `evse_manager` interface. Consumers
            // read `session_event` as a sequence, so the order these reach the
            // wire is part of the contract. An error raise and its clear are
            // here for the same reason: they are announcements on the module's
            // own interface, and a clear overtaking its raise would latch the
            // EVSE inoperative.
            Effect::PublishSessionEvent(_)
            | Effect::PublishCarManufacturer(_)
            | Effect::PublishEvInfo(_)
            | Effect::PublishSelectedProtocol(_)
            | Effect::PublishEnableEvent { .. }
            | Effect::PublishLimits(_)
            | Effect::PublishEnforcedLimits(_)
            | Effect::PublishEnergyFlowRequest(_)
            | Effect::PublishRandomDelayCountdown(_)
            | Effect::PublishSupportedTransferModes(_)
            | Effect::PublishHlcSessionFailed { .. }
            | Effect::PublishProvidedToken(_)
            | Effect::PublishReady(_)
            | Effect::PublishWaitingForExternalReady(_)
            | Effect::RaiseError(_)
            | Effect::ClearError(_)
            // Here and not on the pool for two reasons. A transcript is an
            // ordered record, and the pool has three threads, so a record could
            // overtake the one before it. And the directory a start derives has
            // to exist before `PublishSessionEvent` reads it back for
            // `session_started.logging_path`, which the C++ gets from
            // `startSession` returning the path (`evse/evse_managerImpl.cpp:169-174`);
            // one serial lane for both is what keeps that a fact rather than a
            // race. The C++ writes its transcript inline on the charger's own
            // state machine thread, so a lane shared with this module's
            // publishes is already less coupled than the original.
            | Effect::SessionLog(_)
            // The three metering transaction commands, on this lane for the
            // same reason `SessionLog` is: what they answer is read back by a
            // `PublishSessionEvent` behind them. `TransactionStarted` carries
            // the meter's answer to the start and `TransactionFinished`
            // carries both of its answers to the stop
            // (`evse/evse_managerImpl.cpp:207` and `:279-280`), and
            // `Charger::start_transaction` can fill them because it calls the
            // meter and signals afterwards on one thread. On the pool the two
            // race, and a payload built before the meter replied carries
            // nothing whatever order the core pushed them in. One lane is what
            // makes "ask, then announce" a fact.
            //
            // `CancelAllTransactions` is here too although it feeds no
            // payload: it is the meter's "close every open record"
            // (`Charger.cpp:1510`) and startup pushes it directly behind the
            // named close of a recovered record. Left on the pool it could
            // overtake that close and wipe the record whose signed values the
            // recovery announces.
            //
            // The cost is that a slow meter now delays this module's
            // publishes. The C++ pays more for the same guarantee: its meter
            // call is inline on the charger's own state machine thread, so a
            // slow meter there delays safety actuation as well, and this lane
            // does not.
            | Effect::StartTransaction { .. }
            | Effect::StopTransaction { .. }
            | Effect::CancelAllTransactions
            // Every `HlcUpdate`, for the same reason again: the twenty five
            // `ISO15118_charger` commands below are one conversation with one
            // stateful peer, and they write `v2g_ctx->evse_v2g_data` in place
            // (`EvseV2G/charger/ISO15118_chargerImpl.cpp`). What one writes
            // another reads back or overwrites, so the order they reach the
            // peer is the order the peer observes.
            //
            // The pair that proved it: `handle_session_setup` ends by resetting
            // `evse_processing[PHASE_AUTH]` to `Ongoing` (`:255-257`) and
            // `handle_authorization_response` sets the same field to `Finished`
            // (`:295`). A setup that lands second therefore erases the grant in
            // front of it, the stack keeps answering the vehicle `Ongoing`, and
            // `PowerDeliveryReq(Start)` never comes. On the pool the core's
            // correct emission order - `core/mod.rs:2337-2343` appends the
            // setup, `:1415` the response - inverted often enough to cost four
            // plug and charge tests, and to read as a flake twice.
            //
            // Blanket rather than that one pair. The C++ makes every one of
            // them as a direct call on the emitting thread, so its order over
            // the whole conversation is total; reproducing it for a subset
            // reproduces it only where the choosing was right, and the comment
            // this replaces is what that choosing already cost. EvseV2G names
            // the same hazard from its own side, at `:343`: "we need to use
            // locks on v2g-ctx in all commands as they are running in different
            // threads".
            //
            // The cost is the meter's, priced above: a slow stack now delays
            // this module's publishes. It buys more than HLC ordering, because
            // sharing the lane with `PublishSessionEvent` also keeps the C++'s
            // order *between* a command and an announcement, which a lane of
            // this module's own would not.
            | Effect::HlcUpdate(_)
            // Every `SlacUpdate`, for the third time and the same reason. The six
            // relays are one conversation with one stateful peer, and the core
            // emits them in ordered pairs within a single pass: the AC stopping
            // entry pushes `LeaveBcd` and then `Reset` (`core/hlc/mod.rs:1001`
            // and `:1003`, pinned by `core/mod.rs:6038`). On the pool those two
            // can invert, and on a replug a delayed `LeaveBcd` or `Reset` lands
            // on top of the next attempt's `EnterBcd` and erases it.
            //
            // The C++ has no such window: `EvseManager.cpp` calls SLAC inline on
            // the thread that decided, so its order over the whole conversation
            // is total. This is the same treatment `HlcUpdate` received above
            // after a broker capture caught the `session_setup` inversion, and
            // the same argument applies without waiting for a second capture to
            // prove it twice.
            | Effect::SlacUpdate(_) => Some(ExecContext::Publish),

            // No lane: the writer intercepts all three in `EventLoop::perform`
            // and settles them itself, so none reaches the executor. The timers it
            // arms; the reply it hands to the waiting caller.
            Effect::StartTimer { .. } | Effect::CancelTimer { .. } | Effect::AnswerCommand { .. } => None,
        }
    }

    /// The identifier the core will correlate this effect's completion against,
    /// or `None` when no state machine awaits it.
    ///
    /// Exhaustive by construction, like `context`. A new variant will not
    /// compile until it declares whether the core awaits it, so awaitedness is a
    /// stated property of every variant rather than an omission. Adding an await
    /// later means adding the field, which the compiler then demands at every
    /// construction site.
    pub fn awaited(&self) -> Option<EffectId> {
        match self {
            Effect::ImdSelfTest { id, .. } => Some(*id),
            Effect::StartTransaction { id, .. } => Some(*id),

            // The waiting caller is correlated by its `ReplyToken`, which the
            // boundary minted; no state machine awaits this.
            Effect::AnswerCommand { .. }
            | Effect::AllowPowerOn(_)
            | Effect::SupplyOff
            | Effect::SetCpState(_)
            | Effect::UnlockConnector
            | Effect::BspEnable(_)
            | Effect::LockConnector
            | Effect::PwmOn(_)
            | Effect::PwmOff
            | Effect::SetOvercurrentLimit(_)
            | Effect::SwitchThreePhases(_)
            | Effect::SetSupplyMode { .. }
            | Effect::SetSupplySetpoint { .. }
            | Effect::ImdStart
            | Effect::ImdStop
            | Effect::OverVoltageLimits(_)
            | Effect::OverVoltageStart
            | Effect::OverVoltageStop
            | Effect::StopTransaction { .. }
            | Effect::CancelAllTransactions
            | Effect::Persist { .. }
            | Effect::PersistDelete { .. }
            | Effect::HlcUpdate(_)
            | Effect::SlacUpdate(_)
            | Effect::SessionLog(_)
            | Effect::PublishSessionEvent(_)
            | Effect::PublishCarManufacturer(_)
            | Effect::PublishEvInfo(_)
            | Effect::PublishSelectedProtocol(_)
            | Effect::PublishProvidedToken(_)
            | Effect::PublishHlcSessionFailed { .. }
            | Effect::PublishEnableEvent { .. }
            | Effect::PublishLimits(_)
            | Effect::PublishEnforcedLimits(_)
            | Effect::PublishEnergyFlowRequest(_)
            | Effect::PublishRandomDelayCountdown(_)
            | Effect::PublishSupportedTransferModes(_)
            | Effect::PublishReady(_)
            | Effect::PublishWaitingForExternalReady(_)
            | Effect::RaiseError(_)
            | Effect::ClearError(_)
            | Effect::StartTimer { .. }
            | Effect::CancelTimer { .. } => None,
        }
    }
}

#[derive(Clone, Debug, PartialEq)]
pub enum EffectOutcome {
    Ok,
    Failed(String),
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::core::config::Wiring;
    use crate::core::enable::{EnableSource, EnableState};
    use crate::core::path::dc::OverVoltageMonitor;

    #[test]
    fn energy_removal_and_release_name_the_device_they_mutate() {
        for effect in [
            Effect::AllowPowerOn(false),
            Effect::SupplyOff,
            Effect::SetCpState(CpState::F),
            Effect::UnlockConnector,
        ] {
            assert!(
                matches!(effect.context(), Some(ExecContext::Device(_))),
                "{effect:?}"
            );
        }
    }

    #[test]
    fn an_awaited_effect_carries_the_identity_the_core_chose() {
        let mut ids = EffectIds::one_space_for_tests();
        let first = ids.allocate().effect_id();
        let second = ids.allocate().effect_id();
        assert_ne!(first, second, "each request is its own identity");
        assert_eq!(
            Effect::ImdSelfTest {
                id: second,
                voltage_v: 500.0
            }
            .awaited(),
            Some(second)
        );
        assert_eq!(
            Effect::StartTransaction {
                id: first,
                transaction_id: "s1".into(),
                id_token: None,
                tariff_text: None,
            }
            .awaited(),
            Some(first)
        );
    }

    #[test]
    fn an_effect_no_stage_awaits_carries_no_identity() {
        for effect in [
            Effect::SupplyOff,
            Effect::PwmOff,
            Effect::ImdStart,
            Effect::StopTransaction {
                transaction_id: "s1".into(),
            },
            Effect::CancelAllTransactions,
        ] {
            assert_eq!(effect.awaited(), None, "{effect:?}");
        }
    }

    #[test]
    fn identity_allocation_is_deterministic() {
        // Two allocators driven identically agree, which is what makes a stage
        // sequence assertable without a clock or a counter in the boundary.
        //
        // It is also why the space has to be shared rather than merely unique
        // per allocator: agreeing is exactly what two separate counters do, so
        // a second allocator anywhere collides with the first from its first
        // allocation onward. The test below is the other half of that.
        //
        // Two roots is what a second space looks like, and this is the only
        // place in the crate that asks for one. `one_space` itself is
        // `pub(super)` and called once, in `Core::new`; every other space,
        // including both of these, comes through the `#[cfg(test)]` door.
        let mut a = EffectIds::one_space_for_tests();
        let mut b = EffectIds::one_space_for_tests();
        for _ in 0..4 {
            assert_eq!(a.allocate(), b.allocate());
        }
    }

    /// The one hole the compiler cannot close, closed by reading the source.
    ///
    /// `one_space` is `pub(super)`, which keeps `boundary` and `main.rs` out;
    /// `scripts/unconstructable.py` proves that half. Rust has no visibility
    /// that admits a module's own body while excluding its descendants, so
    /// `core::config` and `core::path` can reach it too, and a space started
    /// there is exactly the second space this type exists to prevent. There is
    /// nothing to express that with, so this counts the call sites instead.
    ///
    /// Tests are not exempted, they are routed: every fixture space comes
    /// through `one_space_for_tests`, which is `#[cfg(test)]` and so cannot
    /// exist in a shipped binary. A new caller anywhere, production or test,
    /// fails this and has to say why.
    #[test]
    fn one_file_outside_this_one_starts_an_effect_identity_space() {
        fn rs_files(dir: &std::path::Path, out: &mut Vec<std::path::PathBuf>) {
            let Ok(entries) = std::fs::read_dir(dir) else {
                return;
            };
            for entry in entries.flatten() {
                let path = entry.path();
                if path.is_dir() {
                    rs_files(&path, out);
                } else if path.extension().is_some_and(|e| e == "rs") {
                    out.push(path);
                }
            }
        }

        let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("src");
        assert!(root.is_dir(), "no src/ under {}", root.display());
        let mut files = Vec::new();
        rs_files(&root, &mut files);
        files.sort();

        // `one_space(` and not `one_space_for_tests(`: the test door is the
        // thing being permitted, so matching it would make this vacuous.
        let mut sites = Vec::new();
        for file in files {
            let Ok(text) = std::fs::read_to_string(&file) else {
                continue;
            };
            let relative = file
                .strip_prefix(&root)
                .unwrap_or(&file)
                .to_string_lossy()
                .into_owned();
            if relative == "core/effect.rs" {
                continue;
            }
            let calls = text
                .split("one_space")
                .skip(1)
                .filter(|rest| rest.trim_start().starts_with('('))
                .count();
            for _ in 0..calls {
                sites.push(relative.clone());
            }
        }

        assert_eq!(
            sites,
            vec!["core/mod.rs".to_owned()],
            "a space is started somewhere other than `Core::new`, or no longer \
             started there at all. One counter per port is the whole invariant: \
             two allocators each beginning at zero once let a DC isolation \
             monitor self test be passed by a powermeter reply. A fixture that \
             needs its own space calls `EffectIds::one_space_for_tests`."
        );
    }

    #[test]
    fn a_delegated_handle_draws_from_one_space() {
        // What makes `Core` and its power path unable to claim each other's
        // verdicts: the delegate is another handle on one counter, not a
        // counter of its own. `Core::new` hands the path one for this reason.
        //
        // Asserted as a strict order rather than against the literals 0..3,
        // because the two owners' identities are differently typed and this is
        // the whole point of `effect_id`: the tag decides whose await slot an
        // identity may enter, and never whether two identities can be
        // compared. Two counters would make these pairwise equal, not ordered.
        let mut core_side = EffectIds::one_space_for_tests();
        let mut path_side = core_side.delegate();

        let first = core_side.allocate().effect_id();
        let second = path_side.allocate().effect_id();
        let third = core_side.allocate().effect_id();
        let fourth = path_side.allocate().effect_id();

        assert!(
            first < second && second < third && third < fourth,
            "one counter drawn from alternately, got {first:?} {second:?} {third:?} {fourth:?}"
        );
    }

    #[test]
    fn slow_peer_calls_have_device_or_store_context() {
        // A blocking peer no longer lets a second worker overwrite its state.
        for effect in [
            Effect::LockConnector,
            Effect::SwitchThreePhases(true),
            Effect::Persist {
                key: "k".into(),
                value: "v".into(),
            },
        ] {
            assert!(
                matches!(
                    effect.context(),
                    Some(ExecContext::Device(_) | ExecContext::Store)
                ),
                "{effect:?}"
            );
        }
    }
    #[test]
    fn the_advertised_transfer_mode_set_goes_out_on_the_publish_lane() {
        // A write on this module's own `evse_manager` interface, so it belongs
        // with the other writes there rather than with the outbound calls. The
        // ready sequence emits it immediately before the enable announcement
        // and consumers read that pair in order, which a shared pool would
        // not preserve.
        assert_eq!(
            Effect::PublishSupportedTransferModes(vec![EnergyTransferMode::DcExtended]).context(),
            Some(ExecContext::Publish)
        );
    }

    #[test]
    fn every_metering_transaction_command_shares_the_publish_lane() {
        // The lane is what makes "ask the meter, then announce" a fact rather
        // than a race, and nothing about the variants shows it. `Core::start_transaction`
        // pushes `StartTransaction` ahead of the `TransactionStarted` publish
        // and `Core::stop_transaction` pushes `StopTransaction` ahead of the
        // `TransactionFinished` publish, each so the payload can read the
        // meter's answer back (`evse/evse_managerImpl.cpp:207` and
        // `:279-280`). Split across two lanes that ordering is dispatch order
        // only, three pool threads run the request while the publish thread
        // renders the payload, and the payload carries nothing.
        //
        // `CancelAllTransactions` is here although it feeds no payload:
        // `Core::on_startup` pushes it directly behind the named close of a
        // recovered record, and on the pool it could overtake that close and
        // cancel the record whose signed values the recovery announces.
        for effect in [
            Effect::StartTransaction {
                id: EffectIds::one_space_for_tests().allocate().effect_id(),
                transaction_id: "s1".into(),
                id_token: None,
                tariff_text: None,
            },
            Effect::StopTransaction {
                transaction_id: "s1".into(),
            },
            Effect::CancelAllTransactions,
        ] {
            assert_eq!(effect.context(), Some(ExecContext::Publish), "{effect:?}");
        }

        // And the publishes they feed, so the pair is asserted rather than
        // each half separately: a lane rename that moved both would leave the
        // three assertions above green and the guarantee gone.
        assert_eq!(
            Effect::PublishSessionEvent(SessionEventReport {
                uuid: "s1".into(),
                event: SessionEvent::TransactionStarted,
                started: None,
                payload: None,
            })
            .context(),
            Some(ExecContext::Publish)
        );
    }

    #[test]
    fn every_transcript_instruction_goes_out_on_the_serial_publish_lane() {
        // The classification is load bearing twice over and neither reason is
        // visible from the variant. A transcript is an ordered record and the
        // a shared pool would have several threads, so on one a record could
        // overtake the one before it. And the directory a start derives has to
        // exist before the `PublishSessionEvent` behind it reads it back for
        // `session_started.logging_path`; one serial lane for both is what makes
        // that a fact rather than a race.
        //
        // All three variants, because the lane is chosen per variant and a
        // future arm added to `SessionLogEffect` gets no lane of its own.
        for request in [
            SessionLogEffect::Start {
                session_uuid: "s1".into(),
            },
            SessionLogEffect::Stop,
            SessionLogEffect::evse("Session Started: Authorized"),
        ] {
            assert_eq!(
                Effect::SessionLog(request.clone()).context(),
                Some(ExecContext::Publish),
                "{request:?}"
            );
        }
    }

    #[test]
    fn everything_the_stack_is_told_goes_out_on_the_ordered_lane() {
        // Outbound calls to a required interface, and on the ordered lane
        // regardless, because the stack reads them as a sequence: each one
        // writes `v2g_ctx->evse_v2g_data` in place, so what one leaves there
        // the next reads back or overwrites.
        for update in [
            HlcUpdate::ReceiptRequired(true),
            HlcUpdate::Setup {
                evse_id: "DE*PNX*E1".into(),
                evse_id_din: "49A8".into(),
                sae_mode: SaeBidiMode::None,
                debug_mode: false,
            },
            HlcUpdate::BptSetup(BptSetup {
                channel: crate::core::hlc::BptChannel::Unified,
                generator_mode: crate::core::hlc::GeneratorMode::GridFollowing,
                grid_code_detection: None,
            }),
            HlcUpdate::ResetError,
            HlcUpdate::TransferModes(vec![EnergyTransferMode::Mcs]),
        ] {
            let effect = Effect::HlcUpdate(update);
            assert_eq!(effect.context(), Some(ExecContext::Publish), "{effect:?}");
            assert_eq!(effect.awaited(), None, "{effect:?}");
        }
    }

    #[test]
    fn a_session_setup_cannot_overtake_the_authorization_response_it_erases() {
        // The regression this pair is here for. `handle_session_setup` ends by
        // resetting `evse_processing[PHASE_AUTH]` to `Ongoing`
        // (`EvseV2G/charger/ISO15118_chargerImpl.cpp:255-257`) and
        // `handle_authorization_response` sets it to `Finished` (`:295`), so a
        // setup that arrives second erases a grant issued just before it and
        // the vehicle is answered `Ongoing` until it gives up.
        //
        // The core emits them in the right order already - `core/mod.rs`
        // appends the setup from the `Authorized` session event and the
        // response after it. What this asserts is that the order survives the
        // executor, which it does only while both sit on a lane of one thread.
        // On the three-thread pool they inverted often enough to fail four
        // plug and charge tests.
        let setup = Effect::HlcUpdate(HlcUpdate::SessionSetup(SessionSetup {
            payment_options: Vec::new(),
            supported_certificate_service: false,
            central_contract_validation_allowed: false,
            fake_dc: false,
        }));
        let response = Effect::HlcUpdate(HlcUpdate::AuthorizationResponse(
            crate::core::hlc::AuthorizationResponse::TIMED_OUT,
        ));
        assert_eq!(setup.context(), response.context());
        assert_eq!(
            setup.context(),
            Some(ExecContext::Publish),
            "the two share the ordered lane, which is what makes sharing it sharing an order"
        );
    }

    #[test]
    fn carrying_an_error_out_of_the_core_is_publish_context() {
        // The safety lane holds actuation only. An `Inoperative` raise removes
        // the EVSE from service, but the removal is the safe state actuation
        // emitted alongside it; the raise itself announces a decision already
        // acted on, and putting a publish on the lane whose whole invariant is
        // that it holds nothing but actuation would not make the port safe any
        // sooner.
        //
        // It is a write on the module's own interface, so it belongs with the
        // other writes there, and a clear must not overtake its raise.
        let report = ErrorReport {
            error_type: "evse_manager/Inoperative".into(),
            sub_type: String::new(),
            severity: Severity::High,
            vendor_id: "EVerest".into(),
            description: "DiodeFault".into(),
            message: "evse_board_support/DiodeFault".into(),
        };
        for effect in [
            Effect::RaiseError(report.clone()),
            Effect::ClearError(report),
        ] {
            assert_ne!(
                effect.context(),
                Some(ExecContext::Safety),
                "an announcement never joins the actuation lane: {effect:?}"
            );
            assert_eq!(effect.context(), Some(ExecContext::Publish), "{effect:?}");
            assert_eq!(effect.awaited(), None, "{effect:?}");
        }
    }

    #[test]
    fn every_variant_declares_the_lane_its_kind_belongs_to() {
        // The rule, applied to one witness per variant: actuation toward safe
        // state is Safety, an effect whose order some other effect observes is
        // Publish, and everything left is Ordinary. Timer effects never reach
        // the executor, and sit in the unordered lane.
        // The two awaited variants need a witness identity. It is allocated,
        // not written: there is no way to write one.
        let mut ids = EffectIds::one_space_for_tests();
        let report = ErrorReport {
            error_type: "evse_manager/Inoperative".into(),
            sub_type: String::new(),
            severity: Severity::High,
            vendor_id: String::new(),
            description: String::new(),
            message: String::new(),
        };
        let session = SessionEventReport {
            uuid: "s".into(),
            event: SessionEvent::Authorized,
            started: None,
            payload: None,
        };
        let table = [
            (Effect::AllowPowerOn(true), Some(ExecContext::Device(Device::Bsp))),
            (Effect::SupplyOff, Some(ExecContext::Device(Device::Supply))),
            (
                Effect::SetCpState(CpState::X1),
                Some(ExecContext::Device(Device::Bsp)),
            ),
            (
                Effect::UnlockConnector,
                Some(ExecContext::Device(Device::ConnectorLock)),
            ),
            (Effect::BspEnable(true), Some(ExecContext::Device(Device::Bsp))),
            (Effect::PublishSessionEvent(session), Some(ExecContext::Publish)),
            (
                Effect::PublishEnableEvent {
                    event: SessionEvent::Enabled,
                    source: EnableEntry {
                        source: EnableSource::Csms,
                        state: EnableState::Enable,
                        priority: 0,
                    },
                },
                Some(ExecContext::Publish),
            ),
            (
                Effect::PublishLimits(Limits {
                    max_current_a: 32.0,
                    nr_of_phases_available: 3,
                }),
                Some(ExecContext::Publish),
            ),
            (Effect::PublishReady(true), Some(ExecContext::Publish)),
            (
                Effect::PublishWaitingForExternalReady(true),
                Some(ExecContext::Publish),
            ),
            (Effect::RaiseError(report.clone()), Some(ExecContext::Publish)),
            (Effect::ClearError(report), Some(ExecContext::Publish)),
            (
                Effect::LockConnector,
                Some(ExecContext::Device(Device::ConnectorLock)),
            ),
            (Effect::PwmOn(50.0), Some(ExecContext::Device(Device::Bsp))),
            (Effect::PwmOff, Some(ExecContext::Device(Device::Bsp))),
            (
                Effect::SetOvercurrentLimit(32.0),
                Some(ExecContext::Device(Device::Bsp)),
            ),
            (
                Effect::SetSupplyMode { mode: SupplyMode::Export, phase: ChargingPhase::Other },
                Some(ExecContext::Device(Device::Supply)),
            ),
            (
                Effect::SetSupplySetpoint {
                    mode: SupplyMode::Export,
                    voltage_v: 400.0,
                    current_a: 10.0,
                },
                Some(ExecContext::Device(Device::Supply)),
            ),
            (Effect::ImdStart, Some(ExecContext::Device(Device::Imd))),
            (Effect::ImdStop, Some(ExecContext::Device(Device::Imd))),
            (
                Effect::ImdSelfTest {
                    id: ids.allocate().effect_id(),
                    voltage_v: 500.0,
                },
                Some(ExecContext::Device(Device::Imd)),
            ),
            (
                Effect::OverVoltageLimits(
                    OverVoltageMonitor::for_wiring(&Wiring {
                        over_voltage_monitor: true,
                        ..Wiring::default()
                    })
                    .expect("a wired monitor")
                    .thresholds(500.0, 950.0),
                ),
                Some(ExecContext::Device(Device::OverVoltageMonitor)),
            ),
            (
                Effect::OverVoltageStart,
                Some(ExecContext::Device(Device::OverVoltageMonitor)),
            ),
            (
                Effect::OverVoltageStop,
                Some(ExecContext::Device(Device::OverVoltageMonitor)),
            ),
            (
                Effect::StartTransaction {
                    id: ids.allocate().effect_id(),
                    transaction_id: "s1".into(),
                    id_token: None,
                    tariff_text: None,
                },
                Some(ExecContext::Publish),
            ),
            (
                Effect::StopTransaction {
                    transaction_id: "s1".into(),
                },
                Some(ExecContext::Publish),
            ),
            (Effect::CancelAllTransactions, Some(ExecContext::Publish)),
            (
                Effect::Persist {
                    key: "k".into(),
                    value: "v".into(),
                },
                Some(ExecContext::Store),
            ),
            (
                Effect::PersistDelete { key: "k".into() },
                Some(ExecContext::Store),
            ),
            (
                Effect::HlcUpdate(HlcUpdate::AuthorizationResponse(
                    crate::core::hlc::AuthorizationResponse::TIMED_OUT,
                )),
                Some(ExecContext::Publish),
            ),
            (
                Effect::StartTimer {
                    id: TimerId(0),
                    after: Duration::from_secs(1),
                },
                None,
            ),
            (Effect::CancelTimer { id: TimerId(0) }, None),
        ];

        for (effect, expected) in &table {
            assert_eq!(effect.context(), *expected, "{effect:?}");
        }

        // One witness per variant. `context` is exhaustive over `Effect`, so a
        // new variant that is not added here leaves the rule unstated for it.
        assert_eq!(
            table.len(),
            32,
            "add the new variant's witness to this table"
        );
    }

    #[test]
    fn board_support_enable_and_disable_name_the_same_device() {
        // Both mutate the BSP. The executor explicitly bypasses this normal
        // domain for disable until the shutdown contract is decided.
        for effect in [Effect::BspEnable(false), Effect::BspEnable(true)] {
            assert_eq!(
                effect.context(),
                Some(ExecContext::Device(Device::Bsp)),
                "{effect:?}"
            );
        }
    }
}
