// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

//! Everything that enters the core.
//!
//! One event type, one queue, one total order. There are no intake priority
//! classes: a reserved lane that admitted raises but not clears is how the
//! previous implementation could latch an EVSE inoperative until restart.

use super::auth::{AuthorizationStatus, CertificateStatus, TariffMessages};
use super::derate::ExternalDerating;
use super::effect::{EffectId, EffectOutcome, TimerId};
use super::enable::{EnableSource, EnableState};
use super::hlc::dc_limits::{DynamicModeRequest, EvMaximumLimits};
use super::hlc::{OpaqueToken, PlugAndChargeConfiguration, Power, SelectedService};
use super::session::{EnableScope, EnergyTransferMode, StopReason, StopTransactionReason};
use super::soft_oc::PhaseCurrents;
use super::token::IdTag;

/// How bad the reporter says the fault is. `Severity::High` is what separates an
/// emergency shutdown from an ordinary error shutdown
/// (`ErrorHandling.cpp:247-251`), so it travels with the report rather than
/// being decided by the receiver.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Severity {
    Low,
    Medium,
    High,
}

/// Where an error came from. One variant per error reporting requirement of the
/// C++ module, declared in the order `errors_prevent_charging` collects them at
/// `ErrorHandling.cpp:209-235`, which is also the order that decides the primary
/// cause. `Ord` is derived from that declaration order and the fault set sorts
/// on it, so reordering these variants reorders the reported causes.
#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd)]
pub enum ErrorSource {
    /// This module's own `evse` interface, which reports the errors the C++
    /// raises on `p_evse` itself.
    Evse,
    Bsp,
    ConnectorLock,
    AcRcd,
    IsolationMonitor,
    PowerSupplyDc,
    Powermeter,
    Slac,
    /// The ISO 15118 charger.
    Hlc,
    OverVoltageMonitor,
}

/// Raise and clear are the same variant carrying a flag, so no code path can
/// deliver one without the other being equally deliverable.
///
/// `sub_type` is part of the identity, not decoration: the cause set is keyed on
/// the pair (`ErrorHandling.hpp:113-118`), and the module raises the same type
/// under two different sub types, `MREC22ResistanceFault` with `"Resistance"`
/// and with `"VoltageToEarth"` (`ErrorHandling.cpp:369-372`, `Charger.cpp:2290`).
#[derive(Clone, Debug, PartialEq)]
pub struct ErrorEvent {
    pub source: ErrorSource,
    pub error_type: String,
    pub sub_type: String,
    pub vendor_id: String,
    pub severity: Severity,
    pub raised: bool,
}

/// A decoded ISO 15118 message as `types::iso15118::V2gMessages` carries it.
///
/// The four representations are absent as empty strings rather than as
/// `Option`, which is what the C++ reduces them to at the handler
/// (`EvseManager.cpp:1878-1881`, four `value_or("")` calls) and what the
/// transcript writes.
/// What the car side power meter can measure, from `subscribe_capabilities`
/// (`EvseManager.cpp:249-250`).
///
/// Both figures are floors, not ceilings: below them the meter cannot measure
/// within its accuracy class, which German Calibration Law requires it to stay
/// inside. Absent means the meter named no floor for that direction.
///
/// The floors are merged into the DC capabilities advertised to the vehicle by
/// `core::powermeter_limits`. `hlc::dc_limits` owns the two seams that reach it,
/// `DcLimits::announced_capabilities` for what the stack is told directly and
/// `DcLimits::capabilities_for_hlc` for the readers that derive a limit set;
/// `Core::handle`'s arm writes the transcript line and drives the push.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct PowermeterCapabilities {
    /// `min_import_current_A`, the charging direction.
    pub min_import_current_a: Option<f64>,
    /// `min_export_current_A`, the discharging direction.
    pub min_export_current_a: Option<f64>,
}

impl PowermeterCapabilities {
    /// One figure as the C++ renders it, which prints `N/A` for an absent
    /// optional rather than omitting the field
    /// (`EvseManager::update_powermeter_capabilities`).
    fn render(value: Option<f64>) -> String {
        match value {
            Some(amps) => format!("{amps}"),
            None => "N/A".to_string(),
        }
    }

    /// The transcript line, now the C++ one verbatim
    /// (`EvseManager::update_powermeter_capabilities`). It carried a
    /// `NOT applied to any limit` clause while the merge was unported, so an
    /// operator comparing two transcripts could tell the ported half from the
    /// unported one; the merge is ported and the clause is gone with it.
    pub fn transcript_line(&self) -> String {
        format!(
            "Received power meter capabilities: min_import_current_A \
             (charging): {}, min_export_current_A (discharging): {}",
            Self::render(self.min_import_current_a),
            Self::render(self.min_export_current_a),
        )
    }
}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct V2gMessage {
    /// `v2g_message_id_to_string(v2g_messages.id)`.
    pub id: String,
    pub xml: String,
    pub json: String,
    pub exi_hex: String,
    pub exi_base64: String,
}

impl V2gMessage {
    /// Whether the vehicle sent it.
    ///
    /// `EvseManager.cpp:1884-1888` routes a message id without `Res` in it to
    /// the car side and one with `Res` to the EVSE side. The comment above that
    /// line claims the opposite ("All messages from EVSE contain Req"); the
    /// code is right, since the EV sends requests and the SECC answers them, so
    /// the code is what is ported.
    pub fn from_vehicle(&self) -> bool {
        !self.id.contains("Res")
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum CpEvent {
    A,
    B,
    C,
    D,
    E,
    F,
    PowerOn,
    PowerOff,
    Disconnected,
}

impl CpEvent {
    /// Whether this reading means the cable is out. `Disconnected` is the same
    /// fact reported by the board rather than read off the pilot, so the two
    /// end a session alike. `path::ac::cp_to_input` maps exactly this pair onto
    /// `CarUnplugged`, and a test there holds the two in step.
    pub fn is_unplug(self) -> bool {
        matches!(self, CpEvent::A | CpEvent::Disconnected)
    }
}

/// Levels the control pilot can rest at. `RawCPState`
/// (`IECStateMachine.hpp:57`).
///
/// `CpEvent` also carries board reports that are not pilot levels, and those
/// leave the level where it was, exactly as the C++ passes them through without
/// touching `last_cp_state` (`IECStateMachine.cpp:102-113`).
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub enum CpLevel {
    /// The port is out of service, and the level a port has read nothing from
    /// yet: `IECStateMachine.hpp:137` value initializes `last_cp_state` here,
    /// which is what makes the first B of a port's life a plug in.
    #[default]
    Disabled,
    A,
    B,
    C,
    D,
    E,
    F,
}

impl CpLevel {
    /// The level a reading rests the pilot at, or `None` for a board report.
    pub fn of(event: CpEvent) -> Option<Self> {
        Some(match event {
            // Reported by the board rather than read off the pilot, but it is
            // the same fact as state A and `CpEvent::is_unplug` pairs the two.
            CpEvent::A | CpEvent::Disconnected => CpLevel::A,
            CpEvent::B => CpLevel::B,
            CpEvent::C => CpLevel::C,
            CpEvent::D => CpLevel::D,
            CpEvent::E => CpLevel::E,
            CpEvent::F => CpLevel::F,
            CpEvent::PowerOn | CpEvent::PowerOff => return None,
        })
    }

    /// Whether a vehicle arriving from this level counts as an arrival.
    ///
    /// `IECStateMachine.cpp:204-205` and `:231-232`, which are the same
    /// condition written twice: state A and a port out of service both mean no
    /// vehicle, and state F means the port was faulted or unavailable while one
    /// may or may not have been present, which is what the latch answers.
    fn admits_plug_in(self, car_plugged_in: bool) -> bool {
        matches!(self, CpLevel::A | CpLevel::Disabled) || (!car_plugged_in && self == CpLevel::F)
    }
}

/// The `CPEvent`s one control pilot reading makes, as `IECStateMachine`
/// derives them.
///
/// Five of the ten. The first four are what `EvseManager.cpp:1094-1119`
/// forwards to the SLAC layer; the fifth is read by the power path instead.
/// They are answered together because they are answered from the same one
/// assignment, and because every one of them is a fact about the pair
/// (previous level, this reading) that only this type remembers.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct CpEdges {
    /// `CPEvent::CarPluggedIn` (`IECStateMachine.cpp:208-212`, `:235-239`).
    pub plugged_in: bool,
    /// `CPEvent::CarUnplugged` (`IECStateMachine.cpp:183-185`).
    ///
    /// Narrower than `CpEvent::is_unplug`, which answers the reading alone:
    /// a repeated state A, and a port reading A on its way back into service,
    /// are both `is_unplug` and neither is a departure.
    pub unplugged: bool,
    /// `CPEvent::EFtoBCD` (`IECStateMachine.cpp:215-218`). State B out of E or
    /// F, which is where matching restarts. The C++ does not raise it for the
    /// simplified mode entry straight into C or D.
    pub entered_bcd: bool,
    /// `CPEvent::BCDtoEF` (`IECStateMachine.cpp:305-308` and `:322-325`).
    pub left_bcd: bool,
    /// `CPEvent::CarRequestedStopPower`, pushed by `IECStateMachine::
    /// state_machine`'s state B case (`IECStateMachine.cpp:198-204`). State B
    /// out of C or D, which is the vehicle opening S2.
    ///
    /// The previous level is the whole of the discrimination, which is why it
    /// is answered here and not at the reading: state B is also how a vehicle
    /// arrives and how matching restarts, and neither of those withdraws power.
    pub requested_stop_power: bool,
    /// `CPEvent::CarRequestedPower`, pushed by `IECStateMachine::state_machine`
    /// for state C or D arrived at from B (`IECStateMachine.cpp:241-243`). The
    /// vehicle closing S2, and the exact mirror of `requested_stop_power`.
    ///
    /// The previous level discriminates here for the same reason: C or D out of
    /// A is the simplified mode plug in, which the C++ reports as
    /// `CarRequestedPower` only later and only once PWM starts, so an arrival
    /// is not a request.
    pub requested_power: bool,
}

/// Control pilot transition memory: `last_cp_state` and `car_plugged_in`
/// (`IECStateMachine.hpp:135-137`).
///
/// A plug in is an edge, not a level. Without this a port re-reading state B
/// looks like a vehicle arriving over and over, and a vehicle that only opened
/// S2 looks like one that just arrived.
///
/// One instance, owned by the single writer. The three `PowerPath`
/// implementations deliberately keep no copy: the derived edge reaches them as
/// a `PowerPath::on_session_start` call, so there is nothing for them to
/// disagree about.
#[derive(Clone, Copy, Debug, Default)]
pub struct CpTracker {
    last: CpLevel,
    /// Latched at a plug in and released only at state A
    /// (`IECStateMachine.cpp:171`, `:207`, `:234`). It is what stops an F to B
    /// being a second arrival for a vehicle that never left.
    car_plugged_in: bool,
}

impl CpTracker {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn level(&self) -> CpLevel {
        self.last
    }

    pub fn car_plugged_in(&self) -> bool {
        self.car_plugged_in
    }

    /// Record one control pilot reading and answer which edges it made.
    ///
    /// Table A.6 sequences 1.1 and 1.2, ported from `IECStateMachine.cpp:203-208`
    /// and `:230-236`. Both sequences share one condition here rather than
    /// being written twice: state B and the simplified mode entry straight into
    /// C or D differ in what follows the arrival, not in what makes it one.
    ///
    /// The other three edges are derived in the same pass and not by a second
    /// method the caller has to remember to call first: every one of them is a
    /// fact about the pair (previous level, this reading), and the assignment
    /// below is what destroys the previous level.
    pub fn observe(&mut self, event: CpEvent) -> CpEdges {
        let Some(level) = CpLevel::of(event) else {
            return CpEdges::default();
        };
        let last = std::mem::replace(&mut self.last, level);
        let mut edges = CpEdges {
            // `IECStateMachine.cpp:215-218` and `:322-325`, both written
            // against the raw levels rather than against the arrival, so a
            // vehicle that never left keeps producing them.
            entered_bcd: level == CpLevel::B && matches!(last, CpLevel::E | CpLevel::F),
            left_bcd: matches!(level, CpLevel::E | CpLevel::F)
                && matches!(last, CpLevel::B | CpLevel::C | CpLevel::D),
            // `IECStateMachine::state_machine` `:198`. Written against the
            // raw levels like
            // the two above, so a vehicle that pauses, resumes and pauses again
            // withdraws power every time.
            requested_stop_power: level == CpLevel::B
                && matches!(last, CpLevel::C | CpLevel::D),
            // `IECStateMachine.cpp:241-243`, the mirror of the line above and
            // written against the raw levels for the same reason.
            requested_power: matches!(level, CpLevel::C | CpLevel::D) && last == CpLevel::B,
            ..CpEdges::default()
        };
        match level {
            // `IECStateMachine.cpp:171`. The vehicle is gone, so the next
            // arrival at any level is a new one.
            CpLevel::A => {
                self.car_plugged_in = false;
                // `IECStateMachine.cpp:183-185`. A port re-reading state A, or
                // reading it as it comes back into service, saw no departure:
                // there was nothing there to leave.
                edges.unplugged = !matches!(last, CpLevel::A | CpLevel::Disabled);
            }
            CpLevel::B | CpLevel::C | CpLevel::D => {
                edges.plugged_in = last.admits_plug_in(self.car_plugged_in);
                self.car_plugged_in |= edges.plugged_in;
            }
            // A fault or an out of service port is not an arrival, and neither
            // releases the vehicle: only state A does.
            CpLevel::E | CpLevel::F | CpLevel::Disabled => {}
        }
        edges
    }

    /// Availability arbitration took the port out of service, which the C++
    /// reads as a pilot level of its own (`IECStateMachine.cpp:155-163`).
    /// Restoring service and plugging in is then an arrival again.
    pub fn note_disabled(&mut self) {
        self.last = CpLevel::Disabled;
    }
}

#[derive(Clone, Debug, PartialEq)]
pub enum BspEvent {
    Cp(CpEvent),
    NrOfPhasesAvailable(i64),
    PpAmpacity(f64),
    Capabilities(HardwareCapabilities),
}

/// The board support capability report.
///
/// Every field names its direction, because the two directions are separate
/// values on the wire (`types/evse_board_support.yaml`) and the C++ reads the
/// import pair for the charge limit and the export pair for whether the port can
/// push power at all. The struct that stood here carried `max_current_a`,
/// `min_current_a` and `max_phase_count` filled from the *export* fields, which
/// nothing read; naming the direction is what stops that being a question.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct HardwareCapabilities {
    pub max_current_a_import: f64,
    pub min_current_a_import: f64,
    pub max_phase_count_import: i64,
    pub min_phase_count_import: i64,
    pub max_current_a_export: f64,
    pub min_current_a_export: f64,
    pub max_phase_count_export: i64,
    pub min_phase_count_export: i64,
    pub supports_changing_phases_during_charging: bool,
    /// Whether the board can drive the control pilot to 0 V.
    ///
    /// `EvseManager::init`, which seeds its own capability handle seeds it false and `:262` hands the reported value
    /// to `Charger::set_supports_cp_state_E`. One reader here, the
    /// reinitialization, which refuses to run its `CPStateE` method without it
    /// rather than asking the board for a level it has said it cannot produce.
    pub supports_cp_state_e: bool,
}

impl From<HardwareCapabilities> for crate::core::hlc::AcCapabilities {
    fn from(caps: HardwareCapabilities) -> Self {
        Self {
            min_phase_count_import: caps.min_phase_count_import,
            max_phase_count_import: caps.max_phase_count_import,
            max_current_a_import: caps.max_current_a_import,
            min_current_a_import: caps.min_current_a_import,
            max_current_a_export: caps.max_current_a_export,
            min_current_a_export: caps.min_current_a_export,
            max_phase_count_export: caps.max_phase_count_export,
        }
    }
}

/// The DC power supply capability report,
/// `types::power_supply_DC::Capabilities` in full.
///
/// Eight fields have a reader in this port: `bidirectional` chooses the
/// advertised set, the two tolerances become the physical setup values, and the
/// export and import extremes become the EVSE limit sets. The rest are here
/// because the report is **forwarded whole**: `call_set_powersupply_capabilities`
/// (`EvseManager.hpp:269`) hands the struct to the ISO 15118 stack unchanged, so
/// the stack is the reader of the nominal set and of the two conversion
/// efficiencies. Narrowing the type would drop those from the wire message,
/// which is silent and looks exactly like a report the supply never sent.
///
/// The efficiencies have a second reader, the energy flow request builder,
/// which puts each on the schedule of its own direction (`core::energy`).
///
/// `Default` is not derived, because a value initialized report is not a report
/// the C++ ever holds: `get_sane_default_power_supply_capabilities`
/// (`EvseManager.cpp:23-35`) is what the member is seeded with, and it names a
/// non zero maximum export voltage and non zero tolerances. `sane_default`
/// below is that seed.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct PowerSupplyCapabilities {
    pub bidirectional: bool,
    pub max_export_voltage_v: f64,
    pub min_export_voltage_v: f64,
    pub max_export_current_a: f64,
    pub min_export_current_a: f64,
    pub max_export_power_w: f64,
    pub current_regulation_tolerance_a: f64,
    pub peak_current_ripple_a: f64,
    /// The import half is optional on the wire and stays optional here. A
    /// supply that reports itself bidirectional but names no import limit is
    /// representable, and the discharge limit derivation has to answer for it
    /// rather than assume a number.
    pub max_import_voltage_v: Option<f64>,
    pub min_import_voltage_v: Option<f64>,
    pub max_import_current_a: Option<f64>,
    pub min_import_current_a: Option<f64>,
    pub max_import_power_w: Option<f64>,
    /// Forwarded to the stack and read by nothing here. Kept so the forward
    /// carries what the supply reported.
    pub conversion_efficiency_export: Option<f64>,
    pub conversion_efficiency_import: Option<f64>,
    pub nominal_max_export_current_a: Option<f64>,
    pub nominal_max_export_power_w: Option<f64>,
    pub nominal_max_export_voltage_v: Option<f64>,
    pub nominal_max_import_current_a: Option<f64>,
    pub nominal_max_import_power_w: Option<f64>,
    pub nominal_max_import_voltage_v: Option<f64>,
    pub nominal_min_export_current_a: Option<f64>,
    pub nominal_min_export_voltage_v: Option<f64>,
    pub nominal_min_import_current_a: Option<f64>,
    pub nominal_min_import_voltage_v: Option<f64>,
}

impl PowerSupplyCapabilities {
    /// `get_sane_default_power_supply_capabilities` (`EvseManager.cpp:23-35`),
    /// which `EvseManager.cpp:212` seeds the member with on boot. The values
    /// are the C++ values: a maximum export voltage of sixty volts, which is
    /// the safe threshold, and a peak ripple and regulation tolerance of half
    /// an ampere each. Everything else is zero, so a port whose supply never
    /// reports offers the vehicle nothing.
    pub const fn sane_default() -> Self {
        Self {
            bidirectional: false,
            max_export_voltage_v: 60.0,
            min_export_voltage_v: 0.0,
            max_export_current_a: 0.0,
            min_export_current_a: 0.0,
            max_export_power_w: 0.0,
            current_regulation_tolerance_a: 0.5,
            peak_current_ripple_a: 0.5,
            max_import_voltage_v: None,
            min_import_voltage_v: None,
            max_import_current_a: None,
            min_import_current_a: None,
            max_import_power_w: None,
            conversion_efficiency_export: None,
            conversion_efficiency_import: None,
            nominal_max_export_current_a: None,
            nominal_max_export_power_w: None,
            nominal_max_export_voltage_v: None,
            nominal_max_import_current_a: None,
            nominal_max_import_power_w: None,
            nominal_max_import_voltage_v: None,
            nominal_min_export_current_a: None,
            nominal_min_export_voltage_v: None,
            nominal_min_import_current_a: None,
            nominal_min_import_voltage_v: None,
        }
    }
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct MeterReading {
    pub energy_wh_import: f64,
    /// `p.power_W`, optional on the wire and optional here.
    ///
    /// Absent and zero are different facts to the one reader there is: the
    /// present power the vehicle is told is gated on `p.power_W` having a value
    /// (`EvseManager.cpp:1167`), so a meter that reports no power figure must
    /// leave the vehicle's last figure standing rather than replacing it with
    /// zero watts.
    ///
    /// Carried whole rather than as its total, because the command forwards the
    /// wire type and the per phase figures are part of what the vehicle is told.
    pub power_w: Option<Power>,
    pub voltage_v: f64,
    /// The DC voltage alone, absent where the meter reports none.
    ///
    /// Distinct from `voltage_v` above, which falls back to L1 and then to
    /// zero: the plausibility comparison needs the meter's own DC figure and
    /// must not be handed an AC phase voltage or a zero standing in for a
    /// missing one. `EvseManager.cpp:1172` gates its feed on exactly this.
    pub dc_voltage_v: Option<f64>,
    pub current_a: f64,
    /// The three AC phase currents, present only when the record carries all
    /// three.
    ///
    /// `EvseManager.cpp:1157` gates `set_current_drawn_by_vehicle` on
    /// `p.current_A and .L1 and .L2 and .L3`, so a record missing one phase
    /// updates none of them. Carrying the set as one option rather than three
    /// puts that gate in the type: there is no way to read a fresh L1 beside a
    /// stale L3.
    pub phase_currents_a: Option<PhaseCurrents>,
}

/// `Default` is for fixtures only: production fills every field at the one
/// boundary site that builds this, and a defaulted resistance of zero would be
/// a fault rather than a neutral value.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct IsolationReading {
    pub resistance_ohm: f64,
    /// The voltage the monitor measured, which gates the voltage to earth
    /// check: below the supply's minimum export voltage the earth readings are
    /// not trusted, because a poorly synchronised monitor reports them against
    /// a voltage that has already been ramped down.
    pub voltage_v: Option<f64>,
    /// L1e and L2e to protective earth, IEC 61851-23:2023 6.3.1.112.2. Absent
    /// when the monitor does not report them, which is not a failure: the C++
    /// check answers "in range" when it cannot check.
    pub voltage_to_earth_l1e_v: Option<f64>,
    pub voltage_to_earth_l2e_v: Option<f64>,
}

/// Identifies the one caller waiting for a command's verdict.
///
/// Minted at the boundary, because the boundary is the party that waits on it;
/// `EffectId` is minted by the core and correlates a completion instead. A
/// plain integer, so `Event` and `Command` keep deriving `Clone`, `Debug` and
/// `PartialEq` and no boundary type reaches the core.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Hash)]
pub struct ReplyToken(pub u64);

#[derive(Clone, Debug, PartialEq)]
pub enum Command {
    /// `evse_managerImpl::handle_enable_disable`, which is one call carrying
    /// one `EnableDisableSource` (`evse/evse_managerImpl.cpp:472-478`).
    ///
    /// One command carrying the state, because the wire has one call carrying
    /// the state and the third state is not a vote: `Unassigned` means this
    /// source no longer cares, and the arbitration skips such a row
    /// (`Charger.cpp:1855-1868` records it, `:1776-1853` skips it). Two
    /// commands forced the boundary to choose a vote for it, and the choice it
    /// made was `Enable`: a source withdrawing at priority zero then **won**
    /// the arbitration and forced the port enabled.
    EnableDisable {
        source: EnableSource,
        state: EnableState,
        priority: i64,
        scope: EnableScope,
    },
    AuthorizeResponse {
        /// The identity the verdict is about, whole: it reaches the wire again
        /// on `SessionStarted`, `TransactionStarted` and `TransactionFinished`.
        ///
        /// The authorization kind used to travel beside it, computed at the
        /// boundary off the same record. `AuthorizationKind::of` reads it off
        /// the token here instead, so one input yields one answer.
        token: IdTag,
        /// The verdict itself. Whether it was accepted is read off this rather
        /// than carried beside it, so the two cannot disagree.
        status: AuthorizationStatus,
        /// Optional on the wire, and only a refused plug and charge verdict
        /// reads it (`evse/evse_managerImpl.cpp:451-453`).
        certificate: Option<CertificateStatus>,
        /// The tariff the verdict carried, `ValidationResult::tariff_messages`.
        ///
        /// The third of the record's fields this command carries, and the only
        /// one an accepted verdict acts on: it is what the metering transaction
        /// is opened under, and there is no other source for it in this module.
        /// `Charger::authorize` takes the whole `ValidationResult` beside the
        /// token for exactly this.
        ///
        /// Carried on the command rather than fetched when the transaction
        /// opens, because by then the verdict is gone: the metering start is
        /// reached from `AuthSignal::Authorized`, which is produced by `Auth`
        /// and carries no payload. `Auth` is the one writer that records it.
        tariff: TariffMessages,
        /// The reservation the verdict named,
        /// `ValidationResult::reservation_id`.
        ///
        /// The fourth field of the record this command carries, and the only
        /// route by which a non evse specific reservation reaches this module
        /// at all: that kind is never handed to `handle_reserve`, so the
        /// transaction event has no other source for its id
        /// (`evse/evse_managerImpl.cpp:453-461`).
        reservation_id: Option<i64>,
    },
    WithdrawAuthorization,
    Reserve {
        reservation_id: i64,
    },
    CancelReservation,
    PauseCharging,
    ResumeCharging,
    /// An external consumer narrowing what the stack may offer the vehicle
    /// (`evse/evse_managerImpl.cpp:526-565`).
    ///
    /// The list arrives already relabelled for the connector, because the
    /// command answers its caller synchronously and the verdict is therefore
    /// decided at the boundary. Only the accepted list reaches here.
    UpdateAllowedTransferModes(Vec<EnergyTransferMode>),
    /// What the module may offer the vehicle to pay with, set from outside
    /// (`evse/evse_managerImpl.cpp:511-524`).
    ///
    /// It changes state and nothing else. The stack is retold at the next
    /// session setup trigger point, which is what the C++ does: its three
    /// setters write atomics and no derivation runs until the next event.
    SetPlugAndChargeConfiguration(PlugAndChargeConfiguration),
    /// Whether DER directive support is wired for this EVSE
    /// (`evse/evse_managerImpl.cpp:567-576`).
    ///
    /// A boot time wiring declaration rather than the vehicle's runtime DER
    /// capability, which is what `interfaces/evse_manager.yaml:139-147` says
    /// it is. The `NoHlc` refusal is decided at the boundary, because the
    /// command answers its caller synchronously; only an accepted declaration
    /// reaches here.
    SetDerAvailable(bool),
    /// An external source narrowing what the DC supply may deliver, on this
    /// module's own `dc_external_derate` interface
    /// (`dc_external_derate/dc_external_derateImpl.cpp:15-17`).
    ///
    /// A `Command` rather than an event of its own, for the reason
    /// `SetPlugAndChargeConfiguration` is one: it arrives on a provided
    /// interface, it replaces a stored value wholesale, and the C++ handler
    /// does nothing else at all.
    SetExternalDerating(ExternalDerating),
    StopTransaction {
        /// The wire reason, whole. `Charger::cancel_transaction` stores it
        /// verbatim as the reason the `TransactionFinished` will name
        /// (`Charger.cpp:1444`); what the power path is asked to do comes from
        /// `StopTransactionReason::narrow`, so the two cannot disagree.
        reason: StopTransactionReason,
        /// `StopTransactionRequest::id_tag`, "only present if transaction was
        /// stopped locally" (`types/evse_manager.yaml`). Stored beside the
        /// reason (`Charger.cpp:1445`) and published on the finish.
        id_tag: Option<IdTag>,
    },
    ForceUnlock,
    ExternalReadyToStartCharging,
    /// The four `uk_random_delay` commands
    /// (`random_delay/uk_random_delayImpl.cpp:15-31`).
    ///
    /// Each is a state change with no output of its own; the next enforced
    /// limit is what observes it. `Enable` and `Disable` both cancel a running
    /// delay as well as moving the flag, so `Enable` on an already enabled
    /// feature is a cancel.
    RandomDelayEnable,
    RandomDelayDisable,
    RandomDelayCancel,
    /// The maximum, in seconds. Unbounded on the wire
    /// (`interfaces/uk_random_delay.yaml`), so zero and negatives arrive here;
    /// what they mean is decided where the delay is drawn.
    RandomDelaySetDuration(i64),
}

#[derive(Clone, Debug, PartialEq)]
pub enum HlcEvent {
    SessionSetup {
        evcc_id: String,
    },
    /// The vehicle selected an ISO 15118-20 service in `ServiceSelectionReq`
    /// (`subscribe_selected_service_parameters`, `EvseManager.cpp:961-965`).
    ///
    /// Only the energy transfer field. The wire type carries six more, the
    /// connector, the control mode, the mobility needs mode, the pricing and
    /// the two bidirectional fields, and the C++ handler reads none of them:
    /// the other three statements in it raise a flag, write a log line and name
    /// the service in that line.
    SelectedService(SelectedService),
    /// The SAE J2847/2 bidirectional session became active
    /// (`subscribe_sae_bidi_mode_active`, `EvseManager.cpp:931-942`).
    ///
    /// Payloadless on the wire and payloadless here: the stack signals that
    /// the mode the EVSE announced at `call_setup` is now running, and which
    /// mode that is came from configuration rather than from the vehicle.
    ///
    /// The C++ subscription is registered inside the DC branch and inside
    /// `if (config.sae_j2847_2_bpt_enabled)` (`:927`), so an AC port and a
    /// port without the setting never receive it.
    SaeBidiModeActive,
    RequiresCableCheck,
    PreChargeStarted,
    CurrentDemandStarted,
    CurrentDemandFinished,
    StopFromEv(StopReason),
    /// SLAC reported its matching state. `true` for every state but
    /// `UNMATCHED`, which is the inversion `EvseManager.cpp:1215-1225` makes
    /// before it calls `Charger::set_matching_started`.
    ///
    /// This is the only producer of that fact. Deriving it from a V2G session
    /// setup instead is sound but late: a session setup cannot precede a
    /// completed match, so it reports matching started only once matching has
    /// already finished, and it never reports the return to unmatched at all.
    MatchingStarted(bool),
    /// Whether the SLAC link is **matched**, which is a narrower fact than the
    /// one above and a separate C++ member.
    ///
    /// `EvseManager::ready`'s `subscribe_state` arm on the SLAC requirement calls `set_matching_started` and
    /// `set_slac_matched` from the same handler, and `Charger` keeps both:
    /// `matching_started` is true from the first matching packet onward, while
    /// `slac_matched` is true only in the `MATCHED` state.
    /// `Charger::set_slac_matched` writes `matching_started` as
    /// `matched or matching_started`, so the second is a subset of the first
    /// and neither can stand in for the other.
    ///
    /// One reader, `Charger::process_pending_reinit_request`, which will not
    /// break the control pilot while an ISO 15118 session is still matched. The
    /// port would otherwise have to guess, and guessing early cuts the link
    /// under a live session.
    SlacMatched(bool),
    /// SLAC asked for the error routine: `subscribe_request_error_routine`
    /// (`EvseManager.cpp:1253-1256`) into `Charger::request_error_sequence`
    /// (`Charger.cpp:2133-2147`).
    ///
    /// Payloadless on the wire and payloadless here. The stack asks for a
    /// pilot kick so matching can start again; which states admit one, and the
    /// SLAC reset that travels with an admitted one, are the charger's, so
    /// this variant carries the request and decides nothing.
    SlacErrorRoutine,
    /// The SLAC data link came up or went away
    /// (`EvseManager.cpp:1232-1238`). Relayed to the stack unchanged; the C++
    /// forwards it with no charger involvement.
    DataLinkReady(bool),
    /// SLAC reported the vehicle's MAC address (`subscribe_ev_mac_address`,
    /// `EvseManager.cpp:180-182`), which is the autocharge identity when it is
    /// taken from SLAC rather than from the ISO 15118 stack.
    ///
    /// Grouped with the other two SLAC facts above rather than given a top
    /// level variant, because the decision it drives belongs to the same port
    /// that owns the stack's `evcc_id`: the two are alternative sources of one
    /// identity and only one of them is ever wired.
    VehicleMacAddress(String),
    /// The stack reported which protocol it negotiated
    /// (`subscribe_selected_protocol`, `EvseManager.cpp:1070-1071`).
    ///
    /// Carried as the stack's own string. Nothing in this module decides on it
    /// and nothing compares it: it exists to be reported on the `evse_manager`
    /// interface, so narrowing it to an enum here would invent names for
    /// protocols the stacks in tree spell for themselves.
    SelectedProtocol(String),
    /// `D-LINK_ERROR.req` from the stack (`EvseManager.cpp:372-379`).
    DataLinkError,
    /// `D-LINK_PAUSE.req` from the stack (`EvseManager.cpp:381-386`).
    DataLinkPause,
    /// `D-LINK_TERMINATE.req` from the stack (`EvseManager.cpp:388-392`).
    DataLinkTerminate,
    /// `v2g_setup_finished` (`EvseManager.cpp:394`), which is the AC producer
    /// of `hlc_charging_active` through `Charger::set_hlc_charging_active`
    /// (`Charger.cpp:2118-2121`). It is not the only writer of that fact: the
    /// `Idle` entry writes it directly, `false` for AC and `true` for DC
    /// (`Charger.cpp:217-224`). This is the AC one.
    ///
    /// On AC it is the vehicle's ISO 15118 power delivery request arriving, and
    /// it is what tells the authorization loop to keep the five percent offer
    /// up rather than withdraw it. Deriving it from `CurrentDemandStarted`
    /// instead reads a DC concept: `Charger::notify_currentdemand_started`
    /// (`Charger.cpp:2024-2030`) moves the state and touches
    /// `hlc_charging_active` not at all.
    SetupFinished,
    /// The high level communication half of the contactor permission
    /// (`EvseManager.cpp:395-403`, `Charger.cpp:2123-2127`). `true` from
    /// `ac_close_contactor`, `false` from `ac_open_contactor`.
    AllowCloseContactor(bool),
    /// `dc_open_contactor` (`EvseManager.cpp:827-832`), which removes the DC
    /// supply and stops the isolation monitor. A different fact from
    /// `AllowCloseContactor(false)` despite the similar name: that one withdraws
    /// a permission, this one de-energizes.
    OpenContactorDc,
    /// The vehicle's requested DC target (`subscribe_dc_ev_target_voltage_current`,
    /// `EvseManager.cpp:734-738`). Stored raw and clamped downstream, because
    /// the clamp re-runs against whatever limits are in force when the
    /// re-apply watchdog fires.
    DcEvTarget {
        voltage_v: f64,
        current_a: f64,
    },
    /// The vehicle's ISO 15118-20 dynamic control mode request
    /// (`subscribe_d20_dc_dynamic_charge_mode`, `EvseManager.cpp:740-825`).
    DcDynamicChargeMode(DynamicModeRequest),
    /// The maxima the vehicle reports for itself
    /// (`subscribe_dc_ev_maximum_limits`, `EvseManager.cpp:851-870`).
    DcEvMaximumLimits(EvMaximumLimits),
    /// The vehicle's battery state of charge, from `subscribe_dc_ev_status`
    /// (`types::iso15118::DcEvStatus::dc_ev_ress_soc`, a percentage).
    ///
    /// One reader, and it is the whole reason `ac_with_soc` exists: the
    /// subscription the C++ installs for it sits inside
    /// `if (config.ac_with_soc)` and does nothing with the number except call
    /// `EvseManager::switch_AC_mode`. The other two fields of the wire type,
    /// the ready flag and the error code, have no reader in the C++ either.
    ///
    /// The percentage travels even though the flip does not read it, because
    /// the fact being announced is "the vehicle reported a state of charge" and
    /// a variant carrying nothing would be indistinguishable, to a later reader
    /// that wants the figure, from one that never had it.
    StateOfCharge {
        percent: f64,
    },
    /// The stack gave up on the session and named why
    /// (`EvseManager.cpp:365-370`).
    SessionFailed(HlcSessionFailure),
    /// The EV selected an energy transfer mode. This is the one binding that is
    /// genuinely per session rather than per configuration.
    ModeSelected {
        transfer: String,
    },
    /// The vehicle asked to be authorized by external identification means
    /// (`EvseManager.cpp:998`). It carries nothing: the request is the whole
    /// fact, and who the vehicle is came earlier with the session setup.
    RequireAuthEim,
    /// The vehicle presented a contract and asked to be authorized on it
    /// (`EvseManager.cpp:1030`). The token travels unread; see `OpaqueToken`.
    RequireAuthPlugAndCharge {
        token: OpaqueToken,
    },
}

/// Why the stack gave up on a high level communication session.
///
/// `types::evse_manager::HlcSessionFailedReasonEnum`, carried through this
/// module unchanged: `EvseManager.cpp:365-370` attaches the session identity and
/// republishes it on its own interface, deciding nothing.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum HlcSessionFailure {
    ProtocolNegotiationFailed,
    AuthorizationFailed,
    ChargingParametersNotAccepted,
    EnergyTransferSetupFailed,
    ChargingInterrupted,
    FailedTlsHandshake,
    UnexpectedSessionEnd,
}

#[derive(Clone, Debug, PartialEq)]
pub enum Event {
    Bsp(BspEvent),
    Error(ErrorEvent),
    Meter(MeterReading),
    Isolation(IsolationReading),
    /// A voltage sample published by the hardware over voltage monitor.
    ///
    /// `EvseManager` subscribes to it and feeds the software watchdog and the
    /// plausibility monitor from it; the hardware monitor raises on its own
    /// thresholds separately.
    OverVoltageMeasurement { voltage_v: f64 },
    /// The isolation monitor's verdict on a self test it was asked to run.
    ///
    /// `EvseManager.cpp` stores this in `selftest_result` and the cable check
    /// stage waits for it: the `start_self_test` command completing only says
    /// the monitor accepted the request.
    IsolationSelfTest(bool),
    SupplyVoltageCurrent {
        voltage_v: f64,
        current_a: f64,
    },
    EnforcedLimits(Box<crate::core::energy::enforce::EnforcedLimits>),
    /// A DC power supply capability report. Its own event rather than a
    /// `BspEvent`, because it comes from a different requirement.
    ///
    /// Boxed for the same reason the effect that forwards it is: it is by far
    /// the largest payload any event carries and by far the rarest.
    PowerSupplyCapabilities(Box<PowerSupplyCapabilities>),
    /// A car side power meter capability report, for the transcript and
    /// nothing else, the same shape [`Event::V2gMessage`] uses.
    ///
    /// The C++ also merges the floors it carries into the DC capabilities sent
    /// to the vehicle. This port records the report and applies nothing, so
    /// the fact reaches a sink an operator reads and reaches no limit. Pinned
    /// by `core::tests::powermeter_capabilities`.
    PowermeterCapabilities(PowermeterCapabilities),
    Hlc(HlcEvent),
    /// One decoded ISO 15118 message, for the transcript and nothing else
    /// (`subscribe_v2g_messages`, `EvseManager.cpp:1049-1050`).
    ///
    /// Its own event rather than an `HlcEvent`, because it carries no fact any
    /// state machine reads. The C++ handler is a method on `EvseManager` that
    /// writes one session log record and returns (`:1873-1889`); nothing in
    /// `Charger` or the stack port ever sees it.
    ///
    /// Boxed: four payload strings, one of which is a whole XML document, and
    /// one arrives per protocol message. Unboxed it would be the widest variant
    /// in the enum and every event in the module would pay for it.
    V2gMessage(Box<V2gMessage>),
    Command(Command),
    /// A command whose caller is blocked on the verdict.
    ///
    /// Its own variant rather than an `Option<ReplyToken>` on
    /// [`Event::Command`]: only five of this module's commands return a
    /// decision on the wire, and the rest genuinely answer nothing, so the
    /// split is a fact about the interface. `apply` routes both to
    /// `apply_command`, which is what keeps the awaited path from drifting
    /// away from the fire-and-forget one.
    CommandAwaiting {
        command: Command,
        reply: ReplyToken,
    },
    Timer {
        id: TimerId,
        generation: u64,
    },
    /// One requested effect finished. `id` is the identity the core chose when
    /// it requested the effect, and is `None` for effects no state machine
    /// awaits, which are then correlated with nothing by anyone.
    EffectDone {
        id: Option<EffectId>,
        outcome: EffectOutcome,
    },
    /// The module has finished wiring and the framework has called `on_ready`.
    /// The ready sequence is a core transition rather than boundary code, the
    /// same shape `Shutdown` uses to reach safe state.
    Startup,
    Shutdown,
    /// The timer thread stopped without being told to. It is the only source of
    /// deadlines in this module, so every stage timeout, the control pilot
    /// pause and the bounded wait before a fatal error releases a latched
    /// vehicle stop with it, and the writer never notices on its own: it wakes
    /// on events, and the events it is waiting for are exactly the ones that
    /// stopped. Posted by an RAII guard on the timer thread, never by a poll.
    TimerThreadDied,
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Feeds a sequence and returns the plug in answer for each reading, so a
    /// transition is asserted as the pair (history, reading) rather than as a
    /// level.
    fn walk(readings: &[CpEvent]) -> Vec<bool> {
        let mut tracker = CpTracker::new();
        readings
            .iter()
            .map(|cp| tracker.observe(*cp).plugged_in)
            .collect()
    }

    #[test]
    fn the_first_pilot_reading_of_a_ports_life_is_a_plug_in() {
        // `last_cp_state` value initializes to the out of service level
        // (`IECStateMachine.hpp:137`), so a port that boots with a vehicle
        // already on it still reports an arrival.
        assert_eq!(walk(&[CpEvent::B]), vec![true]);
    }

    #[test]
    fn a_repeated_state_b_is_not_a_second_plug_in() {
        // The level says a vehicle is present; only the edge says one arrived.
        assert_eq!(
            walk(&[CpEvent::B, CpEvent::B, CpEvent::B]),
            [true, false, false]
        );
    }

    #[test]
    fn state_b_reached_from_state_c_is_not_a_plug_in() {
        // Sequence 7: the vehicle opened S2. It did not arrive.
        assert_eq!(
            walk(&[CpEvent::A, CpEvent::B, CpEvent::C, CpEvent::B]),
            [false, true, false, false]
        );
    }

    #[test]
    fn state_c_straight_from_state_a_is_the_simplified_mode_plug_in() {
        // Sequence 1.2 (`IECStateMachine.cpp:230-236`): a vehicle that closes
        // S2 without ever resting at B has still arrived.
        assert_eq!(walk(&[CpEvent::A, CpEvent::C]), [false, true]);
    }

    #[test]
    fn state_d_straight_from_state_a_is_a_plug_in() {
        // State D falls through to the state C handler in the C++, so it shares
        // the same arrival rule.
        assert_eq!(walk(&[CpEvent::A, CpEvent::D]), [false, true]);
    }

    #[test]
    fn state_c_reached_from_state_b_is_not_a_plug_in() {
        assert_eq!(
            walk(&[CpEvent::A, CpEvent::B, CpEvent::C]),
            [false, true, false]
        );
    }

    #[test]
    fn a_vehicle_that_never_left_does_not_arrive_again_after_a_fault() {
        // `IECStateMachine.cpp:205`: F to B is an arrival only with the latch
        // clear. Without the latch a fault mid session would start a second
        // session for the vehicle already on the cable.
        assert_eq!(
            walk(&[CpEvent::A, CpEvent::B, CpEvent::F, CpEvent::B]),
            [false, true, false, false]
        );
    }

    #[test]
    fn a_vehicle_arriving_at_a_faulted_pilot_after_an_unplug_is_a_plug_in() {
        // The release at state A (`IECStateMachine.cpp:171`) is only observable
        // through this sequence: the vehicle left, the port then faulted, and a
        // new vehicle arrives while the pilot still rests at F. Without the
        // release the latch says a vehicle is present and the arrival is
        // swallowed, so the port never starts a session again.
        assert_eq!(
            walk(&[CpEvent::B, CpEvent::A, CpEvent::F, CpEvent::B]),
            [true, false, false, true]
        );
    }

    #[test]
    fn a_fault_before_any_vehicle_arrived_still_admits_a_plug_in() {
        assert_eq!(walk(&[CpEvent::F, CpEvent::B]), [false, true]);
    }

    #[test]
    fn an_unplug_releases_the_vehicle_latch() {
        assert_eq!(
            walk(&[CpEvent::B, CpEvent::A, CpEvent::B]),
            [true, false, true]
        );
    }

    #[test]
    fn a_disconnected_reading_releases_the_latch_the_way_state_a_does() {
        assert_eq!(
            walk(&[CpEvent::B, CpEvent::Disconnected, CpEvent::B]),
            [true, false, true]
        );
    }

    #[test]
    fn state_e_admits_no_plug_in() {
        // Only A, the out of service level and a vehicle free F do
        // (`IECStateMachine.cpp:204-205`). E is a diode or short fault and says
        // nothing about a vehicle having arrived.
        assert_eq!(walk(&[CpEvent::E, CpEvent::B]), [false, false]);
    }

    #[test]
    fn board_reports_leave_the_pilot_level_where_it_was() {
        // `IECStateMachine.cpp:102-113` passes these through without touching
        // `last_cp_state`. Letting them clear the level would make the next B a
        // fresh arrival.
        assert_eq!(
            walk(&[
                CpEvent::A,
                CpEvent::B,
                CpEvent::C,
                CpEvent::PowerOn,
                CpEvent::PowerOff,
                CpEvent::B,
            ]),
            [false, true, false, false, false, false]
        );
    }

    #[test]
    fn a_board_report_is_never_a_plug_in_of_its_own() {
        for cp in [CpEvent::PowerOn, CpEvent::PowerOff] {
            assert!(!CpTracker::new().observe(cp).plugged_in, "{cp:?}");
            assert!(CpLevel::of(cp).is_none(), "{cp:?}");
        }
    }

    #[test]
    fn taking_the_port_out_of_service_restores_the_plug_in_edge() {
        // `IECStateMachine.cpp:155-163`: an unavailable port reads its own
        // level, so restoring service and plugging in is an arrival again.
        let mut tracker = CpTracker::new();
        assert!(tracker.observe(CpEvent::B).plugged_in);
        assert!(!tracker.observe(CpEvent::B).plugged_in);
        tracker.note_disabled();
        assert!(
            tracker.observe(CpEvent::B).plugged_in,
            "a port back in service admits an arrival"
        );
    }

    #[test]
    fn taking_the_port_out_of_service_does_not_release_the_vehicle() {
        // The C++ Disabled entry clears the simplified mode flag and the timers
        // but deliberately not `car_plugged_in` (`IECStateMachine.cpp:155-163`
        // against `:170-171`). Only state A releases a vehicle.
        let mut tracker = CpTracker::new();
        tracker.observe(CpEvent::B);
        tracker.note_disabled();
        assert!(tracker.car_plugged_in());
    }

    #[test]
    fn every_reading_the_core_calls_an_unplug_is_the_level_that_releases_the_vehicle() {
        // `CpEvent::is_unplug` and the level mapping are two derivations of one
        // fact. Held in step here rather than by whoever edits one of them next.
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
            assert_eq!(
                CpLevel::of(cp) == Some(CpLevel::A),
                cp.is_unplug(),
                "{cp:?} is an unplug on one side only"
            );
        }
    }

    /// A tracker resting at `last` with the vehicle latch in `latch`, or `None`
    /// where that pair cannot exist. State A clears the latch on entry, so
    /// resting at A with a vehicle latched is unreachable by construction.
    fn resting_at(last: CpLevel, latch: bool) -> Option<CpTracker> {
        let mut tracker = CpTracker::new();
        // A plug in is the only thing that sets the latch, and state B from the
        // out of service level is the shortest one.
        if latch {
            assert!(tracker.observe(CpEvent::B).plugged_in);
        }
        match (last, latch) {
            (CpLevel::A, true) => return None,
            (CpLevel::Disabled, _) => tracker.note_disabled(),
            (CpLevel::A, false) => {
                tracker.observe(CpEvent::A);
            }
            // Entered from E, which admits no plug in, so the latch is left as
            // it was rather than being set by the arrival itself.
            (level, _) => {
                tracker.observe(CpEvent::E);
                tracker.observe(match level {
                    CpLevel::B => CpEvent::B,
                    CpLevel::C => CpEvent::C,
                    CpLevel::D => CpEvent::D,
                    CpLevel::E => CpEvent::E,
                    CpLevel::F => CpEvent::F,
                    CpLevel::A | CpLevel::Disabled => unreachable!(),
                });
            }
        }
        assert_eq!(tracker.level(), last);
        assert_eq!(tracker.car_plugged_in(), latch);
        Some(tracker)
    }

    #[test]
    fn the_whole_transition_table_agrees_with_the_ported_condition() {
        // Seven levels by seven levels by both latch states. The narrative tests
        // above pin the sequences that matter; this leaves no quadrant of the
        // table undriven, which is how a divergence in the arms the C++ writes
        // twice (`IECStateMachine.cpp:204-205` against `:231-232`) would
        // otherwise hide.
        const LEVELS: [(CpLevel, CpEvent); 7] = [
            (CpLevel::Disabled, CpEvent::F),
            (CpLevel::A, CpEvent::A),
            (CpLevel::B, CpEvent::B),
            (CpLevel::C, CpEvent::C),
            (CpLevel::D, CpEvent::D),
            (CpLevel::E, CpEvent::E),
            (CpLevel::F, CpEvent::F),
        ];
        let mut driven = 0;
        for (last, _) in LEVELS {
            for latch in [false, true] {
                let Some(base) = resting_at(last, latch) else {
                    continue;
                };
                for (level, reading) in LEVELS {
                    if level == CpLevel::Disabled {
                        // No reading rests the pilot there; arbitration does,
                        // and `note_disabled` is its own entry point.
                        continue;
                    }
                    let mut tracker = base;
                    let plugged_in = tracker.observe(reading).plugged_in;
                    // The condition as the C++ writes it, restated once.
                    let expected = matches!(level, CpLevel::B | CpLevel::C | CpLevel::D)
                        && (matches!(last, CpLevel::A | CpLevel::Disabled)
                            || (!latch && last == CpLevel::F));
                    assert_eq!(
                        plugged_in, expected,
                        "{last:?} (latch {latch}) to {level:?}"
                    );
                    driven += 1;
                }
            }
        }
        assert_eq!(driven, 78, "every reachable combination is driven");
    }

    /// `IECStateMachine.cpp:215-218` pushes `EFtoBCD` for state B out of E or
    /// F and for nothing else: the simplified mode entry straight into C or D
    /// at `:230-239` pushes an arrival and no `EFtoBCD`.
    #[test]
    fn only_state_b_out_of_e_or_f_enters_bcd() {
        for last in [
            CpEvent::A,
            CpEvent::B,
            CpEvent::C,
            CpEvent::D,
            CpEvent::E,
            CpEvent::F,
        ] {
            for reading in [CpEvent::B, CpEvent::C, CpEvent::D] {
                let mut tracker = CpTracker::new();
                tracker.observe(last);
                let expected = reading == CpEvent::B && matches!(last, CpEvent::E | CpEvent::F);
                assert_eq!(
                    tracker.observe(reading).entered_bcd,
                    expected,
                    "{last:?} then {reading:?}"
                );
            }
        }
    }

    /// `IECStateMachine.cpp:305-308` and `:322-325`, which are the same
    /// condition written into the two fault levels.
    #[test]
    fn leaving_b_c_or_d_for_e_or_f_leaves_bcd() {
        for last in [
            CpEvent::A,
            CpEvent::B,
            CpEvent::C,
            CpEvent::D,
            CpEvent::E,
            CpEvent::F,
        ] {
            for reading in [CpEvent::E, CpEvent::F] {
                let mut tracker = CpTracker::new();
                tracker.observe(last);
                let expected = matches!(last, CpEvent::B | CpEvent::C | CpEvent::D);
                assert_eq!(
                    tracker.observe(reading).left_bcd,
                    expected,
                    "{last:?} then {reading:?}"
                );
            }
        }
    }

    /// `IECStateMachine.cpp:183-185`. A departure needs something to have been
    /// there, which is why this is narrower than `CpEvent::is_unplug`: that one
    /// answers the reading alone and both of these readings are `is_unplug`.
    #[test]
    fn state_a_is_a_departure_only_where_a_vehicle_could_have_been() {
        let mut fresh = CpTracker::new();
        assert!(
            !fresh.observe(CpEvent::A).unplugged,
            "nothing was plugged in"
        );

        let mut plugged = CpTracker::new();
        plugged.observe(CpEvent::B);
        assert!(plugged.observe(CpEvent::A).unplugged);
        assert!(
            !plugged.observe(CpEvent::A).unplugged,
            "the second reading of state A is the same absence"
        );
    }

    /// The edges are answered from one reading, so an F to B with no vehicle
    /// latched makes both an arrival and an `EFtoBCD`. The C++ raises both
    /// too, as two `CPEvent`s in that order, and its SLAC handler sends
    /// `enter_bcd` for each. It raises no stop power for it, because the
    /// vehicle was never drawing.
    #[test]
    fn state_b_out_of_f_with_no_vehicle_latched_is_both_edges() {
        let mut tracker = CpTracker::new();
        tracker.observe(CpEvent::F);
        let edges = tracker.observe(CpEvent::B);
        assert!(edges.plugged_in);
        assert!(edges.entered_bcd);
        assert!(!edges.left_bcd);
        assert!(!edges.unplugged);
        assert!(!edges.requested_stop_power);
    }

    /// `IECStateMachine::state_machine` (`IECStateMachine.cpp:198-204`). The
    /// guard the port did without: every
    /// state B was read as the vehicle opening S2, and once the plug in
    /// reached `PrepareCharging` in the same pass, the arriving vehicle
    /// announced a pause it had never taken
    /// (`ocpp201 remote_control::test_F06`).
    ///
    /// Every reading against every resting level, because the wrong answer
    /// here is one the level alone cannot show.
    #[test]
    fn only_state_b_out_of_c_or_d_withdraws_power() {
        for last in [
            CpEvent::A,
            CpEvent::B,
            CpEvent::C,
            CpEvent::D,
            CpEvent::E,
            CpEvent::F,
        ] {
            for reading in [
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
                let mut tracker = CpTracker::new();
                tracker.observe(last);
                let expected =
                    reading == CpEvent::B && matches!(last, CpEvent::C | CpEvent::D);
                assert_eq!(
                    tracker.observe(reading).requested_stop_power,
                    expected,
                    "{last:?} then {reading:?}"
                );
            }
        }
    }


    /// A board report rests the pilot nowhere, so it makes no edge at all.
    #[test]
    fn a_board_report_makes_no_edge() {
        for cp in [CpEvent::PowerOn, CpEvent::PowerOff] {
            let mut tracker = CpTracker::new();
            tracker.observe(CpEvent::B);
            assert_eq!(tracker.observe(cp), CpEdges::default(), "{cp:?}");
        }
    }

    #[test]
    fn the_level_a_tracker_reports_is_the_last_one_it_read() {
        let mut tracker = CpTracker::new();
        assert_eq!(tracker.level(), CpLevel::Disabled);
        for cp in [CpEvent::A, CpEvent::B, CpEvent::C, CpEvent::E, CpEvent::F] {
            tracker.observe(cp);
            assert_eq!(tracker.level(), CpLevel::of(cp).unwrap(), "{cp:?}");
        }
    }
}
