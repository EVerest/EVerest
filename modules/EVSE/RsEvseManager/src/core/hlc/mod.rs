// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

//! The high level communication port.
//!
//! `HlcPort` owns everything the module tells the ISO 15118 stack once, at boot,
//! and the one fact it keeps telling it: which energy transfer modes this EVSE
//! advertises. The set is derived rather than configured, from connector type,
//! live hardware capability and two settings, so it changes while the module
//! runs and every change is republished.

pub mod ac_params;
pub mod authz;
pub mod bpt;
pub mod cable_check;
pub mod dc_limits;
pub mod dlink;
pub mod manufacturer;
pub mod session;
pub mod setup;

use std::sync::Arc;

use crate::core::config::ChargeMode;
use crate::core::derate::ExternalDerating;
use crate::core::effect::{Effect, EvseError, HlcUpdate, SlacUpdate};
use crate::core::event::{
    CpEdges, HlcSessionFailure, PowerSupplyCapabilities, PowermeterCapabilities,
};
use crate::core::powermeter_limits::MeterFloorsChanged;
use crate::core::session::EnergyTransferMode;

pub use ac_params::{AcConnector, AcParameters, AcPowerSet, Power, SelectedService};
pub use authz::{
    AuthorizationHeld, AuthorizationResponse, Authz, OpaqueToken, ProvidedToken, Route, Verdict,
};
pub use bpt::Bpt;
pub use dc_limits::{
    DcLimits, DcTarget, DynamicModeRequest, EvMaximumLimits, MaximumLimits, MinimumLimits,
    PhysicalValues,
};
pub use dlink::{DataLinkRequest, DlinkPilot, TerminatePause};
pub use manufacturer::CarManufacturer;
pub use session::{
    PaymentOption, PlugAndCharge, PlugAndChargeConfiguration, SessionSetup, Trigger,
};
pub use setup::{
    BptChannel, BptSetup, GeneratorMode, GridCodeIslandingDetection, HlcConfig, PresentedMode,
    SaeBidiMode,
};

/// The connector distinction the advertised set derivation makes.
///
/// The C++ resolves `config.connector_type` into an optional enum and warns on
/// an unrecognized name (`EvseManager.cpp:118-123`). Every read of it is
/// `has_value() and value() == cMCS` (`:223`, `:539`, `:549`, `:2005`,
/// `evse/evse_managerImpl.cpp:544`), so an absent value and a known non-MCS
/// connector are indistinguishable in behavior and the whole thing collapses to
/// two cases.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ConnectorKind {
    /// `cMCS`, the megawatt charging system connector.
    Mcs,
    Other,
}

impl ConnectorKind {
    pub fn parse(raw: &str) -> Self {
        if raw == "cMCS" {
            ConnectorKind::Mcs
        } else {
            ConnectorKind::Other
        }
    }

    pub fn is_mcs(self) -> bool {
        self == ConnectorKind::Mcs
    }
}

/// The board support capability values the two AC derivations read.
///
/// The advertised set derivation reads four: the import phase counts choose the
/// AC mode set, and the export pair decides whether the port can push power at
/// all, which gates both bidirectional modes. The AC power envelope derivation
/// (`ac_params`) reads those plus the four current extremes, because it turns
/// each into watts at the nominal voltage.
///
/// Every field names its direction. The four currents are all `f64` and the
/// three phase counts are all `i64`, so a transposition between an import and
/// an export figure compiles and reads plausibly; the name is the only thing
/// that stops it.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct AcCapabilities {
    pub min_phase_count_import: i64,
    pub max_phase_count_import: i64,
    pub max_current_a_import: f64,
    pub min_current_a_import: f64,
    pub max_current_a_export: f64,
    pub min_current_a_export: f64,
    pub max_phase_count_export: i64,
}

/// The AC advertised set, a port of `get_supported_ac_energy_transfers`
/// (`modules/EVSE/EvseManager/energy_transfer_modes.cpp:12-37`).
///
/// The phase clamp is the C++ clamp and its asymmetry is deliberate there: the
/// minimum is clamped into one to three, and the maximum is then clamped into
/// the minimum to three, so a board reporting a maximum below its minimum
/// advertises the minimum rather than nothing.
pub fn supported_ac_transfer_modes(
    caps: AcCapabilities,
    supported_iso_ac_bpt: bool,
    der_available: bool,
) -> Vec<EnergyTransferMode> {
    let min_phases = caps.min_phase_count_import.clamp(1, 3);
    let max_phases = caps.max_phase_count_import.clamp(min_phases, 3);

    let mut modes = Vec::new();
    for (count, mode) in [
        (1, EnergyTransferMode::AcSinglePhase),
        (2, EnergyTransferMode::AcTwoPhase),
        (3, EnergyTransferMode::AcThreePhase),
    ] {
        if count >= min_phases && count <= max_phases {
            modes.push(mode);
        }
    }

    let export_capable = caps.max_current_a_export > 0.0 && caps.max_phase_count_export >= 1;
    if supported_iso_ac_bpt && export_capable {
        modes.push(EnergyTransferMode::AcBpt);
    }
    if der_available && export_capable {
        modes.push(EnergyTransferMode::AcDerIec);
    }
    modes
}

/// The DC advertised set, a port of the boot branch at
/// `modules/EVSE/EvseManager/EvseManager.cpp:537-556` and of the power supply
/// capability arm at `:220-233`, which derive the same two element set.
///
/// The MCS connector relabels both members rather than adding to them: an MCS
/// port advertises `MCS`, never `DC_extended`.
pub fn supported_dc_transfer_modes(
    connector: ConnectorKind,
    bidirectional: bool,
) -> Vec<EnergyTransferMode> {
    let (mode, bpt_mode) = if connector.is_mcs() {
        (EnergyTransferMode::Mcs, EnergyTransferMode::McsBpt)
    } else {
        (EnergyTransferMode::DcExtended, EnergyTransferMode::DcBpt)
    };
    let mut modes = vec![mode];
    if bidirectional {
        modes.push(bpt_mode);
    }
    modes
}

/// The relabelling half of `update_allowed_energy_transfer_modes`
/// (`evse/evse_managerImpl.cpp:526-565`).
///
/// A free function over `HlcConfig` rather than a method on `HlcPort`, because
/// the command answers its caller synchronously and therefore has to be decided
/// at the boundary, where the port does not live. It reads only the two
/// configuration facts it needs, so there is one definition of the filter and
/// not one per caller.
///
/// It does not intersect the request with the advertised set and it does not
/// change it, which is the C++ behavior: the command reaches the stack directly
/// and the published `supported_energy_transfer_modes` variable keeps saying
/// what the derivation last said.
///
/// The C++ has a second behavior here that is not ported, because it is a
/// defect rather than a decision. `std::transform` writes through
/// `filtered_energy_transfer_modes.begin()` after only `reserve()`, so it
/// writes past `end()` of an empty vector and leaves `size()` at zero. The
/// emptiness check that follows therefore always holds, so the C++ command
/// always answers `IncompatibleEnergyTransfer` and never reaches
/// `call_update_energy_transfer_modes`. See the divergence recorded in
/// `docs/architecture.md`.
///
/// There is no `NoHlc` answer here. That refusal was a second statement of
/// `HlcConfig`'s existence, and the boundary already answers it from the
/// absence of the config it holds; see `Intake::update_allowed_energy_transfer_modes`.
pub fn filter_allowed(
    config: &HlcConfig,
    requested: &[EnergyTransferMode],
) -> Result<Vec<EnergyTransferMode>, UpdateRefusal> {
    let filtered: Vec<EnergyTransferMode> = requested
        .iter()
        .map(|mode| relabel_for_connector(config.connector, *mode))
        .collect();
    if filtered.is_empty() {
        return Err(UpdateRefusal::IncompatibleEnergyTransfer);
    }
    Ok(filtered)
}

fn relabel_for_connector(connector: ConnectorKind, mode: EnergyTransferMode) -> EnergyTransferMode {
    if !connector.is_mcs() {
        return mode;
    }
    // The C++ names `DC` and `DC_BPT`, the two ISO 15118-20 spellings, and not
    // `DC_extended`. That is its own inconsistency and it is preserved:
    // `DC_extended` is what the module's own DC derivation advertises, so an
    // MCS port asked to allow `DC_extended` relays it unchanged and the vehicle
    // is offered a mode the port never advertised. No shipped caller sends it,
    // and inventing the extra arm here would be a behavior divergence with
    // nothing behind it.
    match mode {
        EnergyTransferMode::Dc => EnergyTransferMode::Mcs,
        EnergyTransferMode::DcBpt => EnergyTransferMode::McsBpt,
        other => other,
    }
}

/// The advertised set the C++ installs before the board support has reported.
///
/// `EvseManager.cpp:148-160` writes safe defaults into `hw_capabilities` in
/// `init`, and the AC derivation at `:452` reads that same monitor, so a
/// deployment whose board is slow to publish still advertises something. The
/// values are the C++ values, not a Rust convenience: a minimum phase count of
/// zero clamped up to one and a maximum of one give the single phase mode alone.
/// The four current extremes are written as explicit zeros by the C++ seed
/// (`EvseManager.cpp:152-157`), so they are zeros here. A port whose board is
/// slow to publish announces a zero power envelope rather than a made up one.
const SAFE_DEFAULT_AC_CAPABILITIES: AcCapabilities = AcCapabilities {
    min_phase_count_import: 0,
    max_phase_count_import: 1,
    max_current_a_import: 0.0,
    min_current_a_import: 0.0,
    max_current_a_export: 0.0,
    min_current_a_export: 0.0,
    max_phase_count_export: 0,
};

/// Why an `update_allowed_energy_transfer_modes` request was refused.
///
/// The wire enum has four values (`types/evse_manager.yaml:660-667`). `Accepted`
/// is the success case and is the `Ok` half of the result;
/// `ServiceRenegotiationFailed` is not produced here, as it is not produced in
/// the C++ either.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum UpdateRefusal {
    /// Nothing survived the filter, so there would be nothing to offer the
    /// vehicle.
    IncompatibleEnergyTransfer,
}

/// What the vehicle has said about itself this session, as the C++ `ev_info`
/// member accumulates it: every handler that writes a field publishes the whole
/// record.
///
/// Two of `EVInfo`'s twenty six fields, because two is what this port has a
/// source for. The wire type makes every field optional and both consumers test
/// them one at a time (`OCPP201::init_evse_subscriptions`), so a record
/// carrying the two it knows says strictly more than no record at all.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct EvInfo {
    /// From the stack's `subscribe_evcc_id` handler.
    pub evcc_id: Option<String>,
    /// From `subscribe_dc_ev_status`, which the C++ installs only in its DC
    /// branch.
    pub soc: Option<f64>,
}

/// The high level communication port.
///
/// It holds the advertised energy transfer mode set and the two capability
/// facts it is derived from, which is the C++ `supported_energy_transfers`
/// monitor plus the two capability monitors that feed it.
pub struct HlcPort {
    /// The resolved configuration, shared rather than copied.
    ///
    /// The boundary answers two commands synchronously off the same values
    /// (`update_allowed_energy_transfer_modes` and `set_der_available`) and
    /// takes its handle from this port through `Deployment::hlc_config`, so the
    /// two sides cannot hold two derivations of one answer.
    config: Arc<HlcConfig>,
    charge_mode: ChargeMode,
    ac_caps: AcCapabilities,
    /// The DC limit and present value port. It owns the live power supply
    /// capability, which the advertised set derivation reads for its
    /// `bidirectional` field alone. The C++ boots it from
    /// `get_sane_default_power_supply_capabilities` (`EvseManager.cpp:23-35`,
    /// installed at `:212`), whose `bidirectional` is false, so a DC port never
    /// advertises a bidirectional mode until the supply says it can.
    dc_limits: DcLimits,
    /// The bidirectional and DER facts: the SAE session flag, the selected
    /// ISO 15118-20 service, the DER availability declaration and the
    /// ADR-0018 discharge withdrawal latch. They live together because they
    /// are the sources of one answer, which is the port's single decision
    /// about the direction of power flow.
    bpt: Bpt,
    advertised: Vec<EnergyTransferMode>,
    /// The plug and charge state. Seeded from configuration and afterwards
    /// owned by `set_plug_and_charge_configuration`, which is why it is a field
    /// here rather than being read back out of `config` each time.
    pnc: PlugAndCharge,
    /// The two authorization waiting flags and the decisions made off them.
    authz: Authz,
    /// The autocharge identity, derived once from the vehicle identity the
    /// stack reports and held until the next one (`EvseManager.cpp:1016-1028`
    /// fills the C++ member the same way, and never clears it).
    autocharge_id_token: Option<String>,
    /// The per-session vehicle record. It lives here because both fields that
    /// have a source arrive through the stack, and a deployment without one
    /// publishes an empty record at the two lifecycle points and nothing else.
    ev_info: EvInfo,
    /// `slac_unmatched` (`EvseManager.hpp:422`), the second bit the SLAC state
    /// handler writes.
    ///
    /// Value initialized false as it is there, so a port that has heard nothing
    /// from SLAC yet still resets it on unplug; the SLAC module publishes
    /// `UNMATCHED` at startup, which is what closes that window. The polarity
    /// is the C++'s too, because its one reader is written against the negative
    /// (`EvseManager.cpp:1112`) and a port holding the positive would have to
    /// invert it back to be read.
    slac_unmatched: bool,
    /// `shared_context.hlc_charging_terminate_pause`. See `TerminatePause` for
    /// what reads it and why the port holds it at all.
    terminate_pause: TerminatePause,
}

impl HlcPort {
    /// A port exists only where an `HlcConfig` does, and an `HlcConfig` exists
    /// only where `hlc_enabled` held; see `HlcConfig::for_deployment`. So there
    /// is no disabled port and nothing here needs to ask whether it is one.
    pub fn new(config: Arc<HlcConfig>, charge_mode: ChargeMode) -> Self {
        let config_pnc = config.plug_and_charge;
        let mut port = Self {
            config,
            charge_mode,
            ac_caps: SAFE_DEFAULT_AC_CAPABILITIES,
            dc_limits: DcLimits::new(),
            bpt: Bpt::default(),
            advertised: Vec::new(),
            pnc: config_pnc,
            authz: Authz::default(),
            autocharge_id_token: None,
            ev_info: EvInfo::default(),
            slac_unmatched: false,
            terminate_pause: TerminatePause::default(),
        };
        port.advertised = port.derive();
        port
    }

    /// The shared configuration handle, for the boundary's synchronous command
    /// answers. Reached only through `Deployment::hlc_config`, so absence there
    /// is this port's absence and not a second reading of it.
    pub fn config(&self) -> &Arc<HlcConfig> {
        &self.config
    }

    /// The set as it stands. A deployment without high level communication
    /// holds no port to ask, and `Core::advertised_transfer_modes` answers the
    /// empty set the C++ leaves its monitor holding.
    pub fn advertised(&self) -> &[EnergyTransferMode] {
        &self.advertised
    }

    /// The whole boot sequence (`EvseManager.cpp:346-363`, `:544-561` and
    /// `:971-995`).
    ///
    /// The DC half sits between the session setup and the tail, which is where
    /// `ready` has it, and it is empty on an AC port because the C++ block it
    /// comes from is inside `if (config.charge_mode == "DC")`.
    pub fn boot(&mut self, fake_dc: bool, floors: Option<PowermeterCapabilities>) -> Vec<Effect> {
        let mode_specific = if self.runs_dc_limits() {
            self.dc_limits.boot(floors)
        } else if self.runs_ac_params() {
            // `EvseManager.cpp:447-449`. The AC branch's one boot emission,
            // which sits in the same place in `ready` the DC block does. The
            // three AC envelope emissions are **not** here: their only C++ call
            // site is the board support capability handler (`:287`), so a port
            // whose board never publishes announces no envelope at all.
            vec![Effect::HlcUpdate(HlcUpdate::ChargingParameters(
                ac_params::boot_physical_values(&self.config),
            ))]
        } else {
            Vec::new()
        };
        setup::boot(
            &self.config,
            &self.advertised,
            self.pnc,
            fake_dc,
            mode_specific,
        )
    }

    /// The second and every later announcement, which only the `ac_with_soc`
    /// path asks for. See `setup::announce`.
    ///
    /// It deliberately does not touch `advertised`. That field is the derivation
    /// this port owns, from `charge_mode` and the two capability facts, and the
    /// fake DC set is not a derivation: it is a hardcoded pair. Storing it here
    /// would make `charge_mode` mutable, which is the whole thing the
    /// `ac_with_soc` power path exists to avoid. The consequence is the C++
    /// consequence, and it is a real one: an AC capability report arriving while
    /// the fake DC mode is presented recomputes the AC set and overwrites the DC
    /// announcement, because `EvseManager` keeps one
    /// `supported_energy_transfers` for both modes and
    /// `recompute_and_publish_supported_ac_energy_transfers` writes it under no
    /// mode guard.
    pub fn announce(&self, mode: PresentedMode) -> Vec<Effect> {
        setup::announce(&self.config, mode)
    }

    /// Maximum export voltage the supply reports, an input to the cable check
    /// voltage derivation.
    pub fn dc_max_export_voltage_v(&self) -> f64 {
        self.dc_limits.max_export_voltage_v()
    }

    /// Minimum export voltage the supply reports, the gate on the voltage to
    /// earth check.
    pub fn dc_min_export_voltage_v(&self) -> f64 {
        self.dc_limits.min_export_voltage_v()
    }

    /// The DC supply's present output reaching the vehicle
    /// (`EvseManager.cpp:695-726`).
    pub fn note_dc_present_values(&self, voltage_v: f64, current_a: f64) -> Vec<Effect> {
        if !self.runs_dc_limits() {
            return Vec::new();
        }
        self.dc_limits.note_present_values(voltage_v, current_a)
    }

    /// Records the present voltage the external derate derives against
    /// (`EvseManager.cpp:722`, read at `:2689`).
    ///
    /// Answers whether the derated capability report moved, so the caller can
    /// retell the readers that hold a copy. Gated like the emission beside it:
    /// the C++ writes `ev_info.present_voltage` inside the DC branch of
    /// `subscribe_voltage_current`, so an AC port never records one.
    pub fn note_dc_present_voltage(&mut self, voltage_v: f64) -> bool {
        if !self.runs_dc_limits() {
            return false;
        }
        self.dc_limits.note_present_voltage(voltage_v)
    }

    /// An external source narrowed what the supply may deliver
    /// (`dc_external_derate/dc_external_derateImpl.cpp:15-17`).
    ///
    /// Not gated on `runs_dc_limits`. The C++ stores the request in
    /// `set_external_derating` whatever the charge mode is, and the interface is
    /// provided unconditionally, so an AC port accepts one and simply has no
    /// capability report for it to narrow.
    pub fn note_external_derating(
        &mut self,
        requested: ExternalDerating,
        floors: Option<PowermeterCapabilities>,
    ) -> (Vec<Effect>, bool) {
        self.dc_limits.note_external_derating(requested, floors)
    }

    /// `get_powersupply_capabilities_for_hlc()` against a report as it
    /// arrives: external derating and the car side power meter's floors.
    ///
    /// Separate from `derated_supply_capabilities` below because this one does
    /// not depend on the report having been stored, and the store is gated on
    /// `runs_dc_limits` while the arrival is not. With no derate and no meter
    /// it is the identity, so a port with neither is unaffected.
    ///
    /// This replaced a derate only variant when the meter merge landed. There
    /// is no unmerged arrival reader left: everything downstream of the
    /// arrival is a reader that talks to the vehicle.
    pub fn for_hlc(
        &self,
        capabilities: PowerSupplyCapabilities,
        floors: Option<PowermeterCapabilities>,
    ) -> PowerSupplyCapabilities {
        self.dc_limits.for_hlc_only(capabilities, floors)
    }

    /// The DC push of `EvseManager::update_powermeter_capabilities`, and
    /// nothing else.
    ///
    /// The store, the change comparison and the transcript line belong to
    /// `Core::car_side_meter`: the C++ does all three outside its
    /// `if (hlc_enabled and config.charge_mode == "DC")` guard, so an AC port
    /// with a car side meter records the report and tells the vehicle nothing,
    /// and a basic AC port does that with no port here at all.
    ///
    /// Empty on an AC port for the same reason it is in the C++. It never
    /// later tells the vehicle either: `charge_mode` is immutable here and the
    /// fake DC announcement is a per emission flag rather than a mode flip.
    pub fn note_floors(
        &mut self,
        change: MeterFloorsChanged,
        floors: Option<PowermeterCapabilities>,
    ) -> Vec<Effect> {
        if !self.runs_dc_limits() {
            return Vec::new();
        }
        self.dc_limits.note_floors(change, floors)
    }

    /// `get_powersupply_capabilities_for_hlc()`, for the readers that keep
    /// their own copy and need it recomputed when no new report has arrived.
    ///
    /// The one such reader is the energy tree, whose copy feeds the enforced
    /// limits handler; that handler reads
    /// `get_powersupply_capabilities_for_hlc` in the C++
    /// (`energyImpl::handle_enforce_limits`), so the merged report is the right
    /// one to hand it. Its other readers there take the unmerged one, and the
    /// two agree at every one of those sites because they read power ceilings
    /// and conversion efficiencies and the merge moves no such field. Pinned by
    /// `a_meter_floor_does_not_move_what_the_site_is_asked_for`.
    ///
    /// `None` where this port has no DC capability set at all, which is where
    /// the stored report is still the boot seed and handing it to a reader would
    /// replace a real report with a seed rather than refresh anything.
    pub fn derated_supply_capabilities(
        &self,
        floors: Option<PowermeterCapabilities>,
    ) -> Option<PowerSupplyCapabilities> {
        self.runs_dc_limits()
            .then(|| self.dc_limits.capabilities_for_hlc(floors))
    }

    /// The SLAC layer reported the vehicle's MAC address
    /// (`subscribe_ev_mac_address`, `EvseManager.cpp:179-183`).
    ///
    /// The autocharge alternative to `note_vehicle_identity`: the same identity
    /// from a lower layer, and the C++ installs exactly one of the two
    /// subscriptions. This one **publishes at once** rather than remembering,
    /// and that difference is the C++ one rather than an accident. The stack's
    /// `evcc_id` arrives inside a session whose authorization this module is
    /// already arbitrating, so the token it builds waits for the request that
    /// will consume it; a SLAC MAC address arrives before any of that exists and
    /// has nothing to wait for.
    ///
    /// The C++ gate is `autocharge_use_slac_instead_of_hlc and slac_enabled and
    /// enable_autocharge` (`:179`). Its `slac_enabled` half is the boundary's,
    /// because the subscription exists only where SLAC is wired. `hlc_enabled`
    /// is deliberately **not** consulted: the C++ computes it at `:185-187`,
    /// after installing this subscription at `:179-183`, so an AC port with SLAC
    /// wired and no ISO 15118 stack still offers an autocharge token.
    ///
    /// The identity is not remembered. The C++ does not remember it either: the
    /// SLAC arm builds a temporary and `autocharge_token` stays value
    /// initialized, which is the defect `Authz::on_require_eim` names. Recording
    /// it here would quietly fix that path instead of porting it.
    pub fn note_vehicle_mac_address(&mut self, mac_address: &str) -> Vec<Effect> {
        if !self.config.enable_autocharge || !self.config.autocharge_from_slac {
            return Vec::new();
        }
        vec![Effect::PublishProvidedToken(ProvidedToken::Autocharge {
            id_token: authz::autocharge_id_token(mac_address),
            connectors: self.config.connectors.clone(),
        })]
    }

    /// Whether this port has a DC limit set to tell the vehicle about.
    ///
    /// Both C++ gates: `subscribe_capabilities` and `subscribe_voltage_current`
    /// sit inside `if (hlc_enabled)` and inside
    /// `if (config.charge_mode == "DC")` (`EvseManager.cpp:214-215` and
    /// `:528`-`:695`), so an AC port with a power supply wired anyway hears
    /// neither.
    fn runs_dc_limits(&self) -> bool {
        self.charge_mode == ChargeMode::Dc
    }

    /// One of the three re-derivations of what the vehicle may pay with
    /// (`EvseManager.cpp:362`, `:1307`, `:1343`).
    ///
    /// The trigger names the call site rather than the caller deciding, because
    /// the three sites do not agree on what to offer and the difference is the
    /// point: see `session::derive`.
    pub fn session_setup(&self, trigger: Trigger, fake_dc: bool) -> Vec<Effect> {
        vec![Effect::HlcUpdate(HlcUpdate::SessionSetup(session::derive(
            trigger,
            self.pnc,
            self.config.payment_enable_eim,
            fake_dc,
        )))]
    }

    /// `set_plug_and_charge_configuration` (`evse/evse_managerImpl.cpp:511-524`).
    ///
    /// It produces no effects. The C++ setters only write the atomics
    /// (`EvseManager.cpp:1770-1780`); nothing re-derives until the next trigger
    /// point, so a change made mid session reaches the vehicle at the next
    /// session start or finish and not before.
    pub fn configure_plug_and_charge(&mut self, request: &PlugAndChargeConfiguration) {
        self.pnc.apply(request);
    }

    /// The variable publish half of
    /// `publish_and_update_supported_energy_transfers`, which is also the whole
    /// of the ready sequence publish at `EvseManager.cpp:1505`. `Core` reaches
    /// the ready one through `advertised`, so this stays private.
    fn publish_advertised(&self) -> Vec<Effect> {
        vec![Effect::PublishSupportedTransferModes(
            self.advertised.clone(),
        )]
    }

    /// The vehicle named itself (`subscribe_evcc_id`,
    /// `EvseManager.cpp:1016-1028`).
    ///
    /// The C++ installs this handler only when the autocharge identity is not
    /// taken from SLAC (`:1016`), so with that setting on the identity is never
    /// recorded and the token the external identification handler would publish
    /// is never built. The gate is kept here rather than at the boundary,
    /// because it decides what is remembered and not what is delivered.
    ///
    /// The car manufacturer is published from the same handler and under the
    /// same gate, which is why it is one derivation here rather than two: the
    /// C++ reads one `token` and calls `create_autocharge_token` and
    /// `get_manufacturer_from_mac` on it, so the identity that is remembered
    /// and the manufacturer that is announced can never disagree about which
    /// address they came from.
    ///
    /// The third thing that handler publishes is the `ev_info` record, whose
    /// `evcc_id` field is one of its twenty six. It is carried, under the same
    /// gate as the other two, because the record is per field: both consumers
    /// read it one `has_value` at a time, so the twenty four fields this port
    /// has no source for cost nothing and the one it has is the stop token's
    /// only feed.
    ///
    /// The C++ also lowers `car_manufacturer` back to `Unknown` when a car
    /// plugs in (`EvseManager.cpp:1108`) and does **not** publish it there, so
    /// the value on the wire outlives the session that produced it until the
    /// next vehicle names itself. That reset is a write to a member with no
    /// other reader, so it is not ported: reproducing it would need a field
    /// nothing reads, and publishing on it would be a message the C++ does not
    /// send.
    pub fn note_vehicle_identity(&mut self, evcc_id: &str) -> Vec<Effect> {
        if self.config.autocharge_from_slac {
            return Vec::new();
        }
        self.autocharge_id_token = Some(authz::autocharge_id_token(evcc_id));
        self.ev_info.evcc_id = Some(evcc_id.to_owned());
        vec![
            Effect::PublishCarManufacturer(manufacturer::from_mac(evcc_id)),
            Effect::PublishEvInfo(self.ev_info.clone()),
        ]
    }

    /// The vehicle reported its state of charge, through the stack's
    /// `subscribe_dc_ev_status` handler.
    ///
    /// Only a DC port hears it: the C++ installs that subscription inside its
    /// `config.charge_mode == "DC"` branch, so an AC port with `ac_with_soc` on
    /// reaches the same wire fact through the mode flip alone and publishes no
    /// record for it.
    pub fn note_state_of_charge(&mut self, percent: f64) -> Vec<Effect> {
        if !self.runs_dc_limits() {
            return Vec::new();
        }
        self.ev_info.soc = Some(percent);
        vec![Effect::PublishEvInfo(self.ev_info.clone())]
    }

    /// Forgets the vehicle record, so the next session starts with the empty
    /// one its own publication announced. The C++ does it in the two lambdas
    /// `signal_session_started_event` and `signal_simple_event` install.
    pub fn forget_vehicle_record(&mut self) {
        self.ev_info = EvInfo::default();
    }

    /// The vehicle asked for an external identification authorization
    /// (`EvseManager.cpp:998-1014`).
    pub fn on_require_auth_eim(&mut self, held: AuthorizationHeld) -> Vec<Effect> {
        self.authz
            .on_require_eim(&self.config, held, self.autocharge_id_token.as_deref())
    }

    /// The vehicle presented a contract and asked to be authorized on it
    /// (`EvseManager.cpp:1030-1045`).
    pub fn on_require_auth_plug_and_charge(
        &mut self,
        held: AuthorizationHeld,
        token: OpaqueToken,
    ) -> Vec<Effect> {
        self.authz
            .on_require_plug_and_charge(&self.config, held, token)
    }

    /// An authorization verdict has been applied, so the vehicle may now have
    /// its answer (`EvseManager::charger_was_authorized`).
    ///
    /// Ungated: with no stack wired nothing can be waiting, so the gate would
    /// decide nothing.
    pub fn on_authorization_settled(&mut self, held: AuthorizationHeld) -> Vec<Effect> {
        self.authz.on_authorization_settled(held)
    }

    /// How a verdict from `Auth` is routed
    /// (`evse/evse_managerImpl.cpp:424-455`).
    ///
    /// Ungated for the same reason, with one difference worth naming: a refused
    /// contract produces an answer for a stack that may not be there. The C++
    /// indexes `r_hlc[0]` here with no emptiness check at all, so it would be
    /// worse than harmless; here the boundary finds no slot and drops it, the
    /// same way it drops every other update.
    pub fn route_verdict(&self, verdict: Verdict) -> Route {
        self.authz.route(verdict)
    }

    /// The vehicle left (`EvseManager.cpp:1099-1100`).
    ///
    /// The C++ clear sits inside the SLAC arm, which is not a narrowing that
    /// matters: high level communication is disabled wherever SLAC is
    /// (`EvseManager.cpp:185-187`), so the flags cannot be up when that arm is
    /// skipped. The reset that does all the work is the one on arrival.
    pub fn on_unplug(&mut self) {
        self.authz.clear();
        // The `Idle` entry's clear (`Charger.cpp:236`). The port has no `Idle`
        // entry of its own to hang it on, and the unplug is what reaches that
        // entry; the other route into `Idle`, an enable out of `Disabled`, has
        // no session behind it to carry a verdict.
        self.terminate_pause = TerminatePause::Unknown;
    }

    /// A board support capability report (`EvseManager.cpp:280-288`).
    ///
    /// Two consumers in the C++ order: the advertised set is re-derived and
    /// republished when it changed (`:286`), and then the AC power envelope is
    /// re-announced (`:287`). Both sit inside the same
    /// `config.charge_mode == "AC" and hlc_enabled` guard at `:281`.
    ///
    /// The two gates differ in what they do with an unchanged report. The
    /// advertised set is republished only on a change; `update_hlc_ac_parameters`
    /// has no change gate at all, so a board republishing the same report
    /// re-announces the whole envelope. Preserved, the same asymmetry
    /// `dc_limits::note_capabilities` carries.
    pub fn note_ac_capabilities(&mut self, caps: AcCapabilities) -> Vec<Effect> {
        self.ac_caps = caps;
        let mut effects = self.republish_if_changed();
        if self.runs_ac_params() {
            effects.extend(ac_params::capability_emissions(&self.config, self.ac_caps));
        }
        effects
    }

    /// The charge began, which is where the C++ forgets the last data link
    /// verdict (`Charger.cpp:803-804`, the statement beside the `ChargingStarted`
    /// announcement the port raises from the same entry).
    ///
    /// So a pause is judged by what the charge it interrupted ended with, and a
    /// session that pauses twice does not carry the first verdict into the
    /// second resume.
    pub fn note_charging_started(&mut self) {
        self.terminate_pause = TerminatePause::Unknown;
    }

    /// The vehicle selected an ISO 15118-20 service
    /// (`subscribe_selected_service_parameters`, `EvseManager.cpp:961-965`).
    ///
    /// No effects: the C++ handler stores the value, raises the charger's
    /// ISO 15118-20 flag and writes a session log line. The store is what the
    /// two limit emissions read.
    ///
    /// `charger->set_hlc_d20_active()` (`:964`) is not carried
    /// through. Ceiling: the ISO 15118-20 pause the AC stopping entry would
    /// send instead of a stop request (`Charger.cpp:1015-1017`) stays
    /// unreachable, so an ISO 15118-20 AC session is stopped with a stop
    /// request as an ISO 15118-2 one is. Upgrade path: route this fact into the
    /// AC path, which is where the stopping entry decides, and give
    /// `AcHlc::stop_request_owed` the second arm. Owner: RsEvseManager.
    pub fn note_selected_service(&mut self, service: SelectedService) {
        self.bpt.note_selected_service(service);
    }

    /// The SAE J2847/2 bidirectional session became active
    /// (`subscribe_sae_bidi_mode_active`, `EvseManager.cpp:931-942`).
    ///
    /// No effects. This port owns the bidirectional fact; the core routes the
    /// V2H schedule rewrite to the energy tree at the same transition.
    pub fn note_sae_bidi_active(&mut self) {
        self.bpt.note_sae_bidi_active();
    }

    /// `EvseManager.cpp:594`, the one writer of `false` on the SAE flag.
    pub fn note_current_demand_finished(&mut self) {
        self.bpt.note_current_demand_finished();
    }

    /// The bidirectional fact as it stands, resolved once from its three
    /// sources and vetoed by the ADR-0018 withdrawal latch.
    pub fn bidirectional(&self) -> bool {
        self.bpt.bidirectional(self.config.allow_bpt_with_iso2)
    }

    pub fn selected_service(&self) -> Option<SelectedService> {
        self.bpt.selected_service()
    }

    pub fn sae_bidi_active(&self) -> bool {
        self.bpt.sae_bidi_active()
    }

    pub fn allow_bpt_with_iso2(&self) -> bool {
        self.config.allow_bpt_with_iso2
    }

    /// ADR-0018. Reports whether this call is the withdrawal edge.
    pub fn withdraw_discharge(&mut self) -> bool {
        self.bpt.withdraw_discharge()
    }

    /// The capability came back, or the session ended. Reports whether this
    /// call is the edge.
    pub fn release_discharge_withdrawal(&mut self) -> bool {
        self.bpt.release_discharge_withdrawal()
    }

    /// `set_der_available` (`evse/evse_managerImpl.cpp:567-576`).
    ///
    /// The C++ stores the declaration unconditionally and recomputes the
    /// advertised set only on an AC port (`:572`), because the function it
    /// calls is `recompute_and_publish_supported_ac_energy_transfers` and
    /// there is no DC counterpart to call.
    ///
    /// **That charge mode gate has no counterpart here and is deliberately not
    /// reproduced.** `derive` is the one place the charge mode decides what is
    /// advertised, and its DC arm reads the supply's bidirectional capability
    /// and never the DER declaration, so a recompute on a DC port can only
    /// return the set it already held and `republish_if_changed` answers with
    /// nothing. A gate here would be a second statement of the same fact, and
    /// a dead one: a mutation sweep removing it killed no test, which is what
    /// says it decides nothing. `a_dc_port_stores_the_declaration_and_republishes_nothing`
    /// pins the behavior at the place that does decide it.
    ///
    /// The `NoHlc` answer is not decided here either: the command answers its
    /// caller synchronously, so the boundary answers it from
    /// `HlcConfig::enabled`, the same `is_hlc_enabled()` the C++ guard reads. A
    /// port reaching this method has already been found enabled.
    pub fn set_der_available(&mut self, available: bool) -> Vec<Effect> {
        if !self.bpt.set_der_available(available) {
            return Vec::new();
        }
        self.republish_if_changed()
    }

    /// The live AC limit as the charger changed it
    /// (`charger->signal_max_current`, `EvseManager.cpp:1241-1260`).
    ///
    /// Gated on high level communication alone, with no charge mode branch,
    /// which is the C++ gate. That is observable and it is preserved: a DC port
    /// running DIN 70121 or ISO 15118-2 selects no ISO 15118-20 service, so
    /// every limit change sends it an AC ampere count it has no use for.
    pub fn note_ac_current_limit(&self, ampere: f64) -> Vec<Effect> {
        ac_params::current_limit_update(
            &self.config,
            self.ac_caps,
            self.bpt.selected_service(),
            ampere,
        )
        .map(Effect::HlcUpdate)
        .into_iter()
        .collect()
    }

    /// The meter's live power reaching the vehicle
    /// (`EvseManager.cpp:1163-1169`).
    ///
    /// Gated on high level communication alone for the same reason, and on the
    /// selected service inside `ac_params::present_power_update`.
    pub fn note_ac_present_power(&self, power: Option<Power>) -> Vec<Effect> {
        ac_params::present_power_update(self.bpt.selected_service(), power)
            .map(Effect::HlcUpdate)
            .into_iter()
            .collect()
    }

    /// `call_update_meter_info` (`EvseManager.cpp:1183`), which sits one line
    /// above the present power beside it and under a **weaker** guard: the
    /// power is sent only for a selected ISO 15118-20 service and only when the
    /// meter reports one, while the record is sent on every reading of the
    /// billing meter to any port with a stack.
    ///
    /// So it is the port's existence that gates this and nothing else, which is
    /// why it takes no argument and asks nothing of the reading.
    pub fn note_meter_record(&self) -> Vec<Effect> {
        vec![Effect::HlcUpdate(HlcUpdate::MeterInfo)]
    }

    /// Whether this port has an AC power envelope to tell the vehicle about.
    ///
    /// Both halves of the C++ guard at `EvseManager.cpp:281` and `:446`.
    fn runs_ac_params(&self) -> bool {
        self.charge_mode == ChargeMode::Ac
    }

    /// What the stack is told when a vehicle arrives
    /// (`EvseManager.cpp:1121-1129`).
    ///
    /// Three commands in the C++ order, all of them clearing something the
    /// previous session may have left standing: the stack's own error state, a
    /// contactor closed report, and a stop request. The C++ zeroes
    /// `latest_target_voltage` and `latest_target_current` in the same block;
    /// those belong to the DC limits port, which is unported.
    ///
    /// The C++ forwards these after pushing the pilot event onto the charger's
    /// queue (`:1118`), so the power path sees the arrival first. `Core` calls
    /// this from the same place, for the same reason.
    pub fn on_plug_in(&mut self) -> Vec<Effect> {
        // The primary reset of the two authorization waiting flags, and the
        // unconditional one: a new vehicle inherits nothing from the last one
        // (`EvseManager.cpp:1123-1130`, under "Reset HLC auth waiting flags on
        // new session").
        self.authz.clear();
        vec![
            Effect::HlcUpdate(HlcUpdate::ResetError),
            Effect::HlcUpdate(HlcUpdate::ContactorClosed(false)),
            Effect::HlcUpdate(HlcUpdate::StopCharging(false)),
        ]
    }

    /// What the stack is told when the relays close or open
    /// (`EvseManager.cpp:1131-1143`).
    ///
    /// The command is named for AC and is wired for both charge modes: the
    /// forwarding block it sits in is gated on `hlc_enabled` alone, with no
    /// charge mode branch, and the DC cable check depends on the vehicle
    /// hearing that the contactor closed.
    pub fn on_contactor(&self, closed: bool) -> Vec<Effect> {
        vec![Effect::HlcUpdate(HlcUpdate::ContactorClosed(closed))]
    }

    /// The SLAC data link state, relayed to the stack unchanged
    /// (`EvseManager.cpp:1232-1238`). The C++ gates the relay on
    /// `hlc_enabled` and decides nothing else.
    pub fn on_data_link_ready(&self, ready: bool) -> Vec<Effect> {
        vec![Effect::HlcUpdate(HlcUpdate::DlinkReady(ready))]
    }

    /// The SLAC half of a data link request (`EvseManager.cpp:377`, `:384`,
    /// `:391`).
    ///
    /// Mode independent and unconditional, which is why it is here and not on a
    /// power path: the C++ sends it from the callback rather than as a
    /// consequence of what the charger decided, so a port that can do nothing
    /// on the pilot still leaves the logical network.
    pub fn on_data_link(&mut self, request: DataLinkRequest) -> Vec<Effect> {
        // `EvseManager.cpp:388`, the first line of the terminate callback and
        // of no other. A pause keeps the selected service, because the session
        // it belongs to can resume; a terminate ends the session, so the next
        // vehicle inherits nothing.
        if request == DataLinkRequest::Terminate {
            self.bpt.forget_selected_service();
        }
        // `Charger::dlink_pause` and `Charger::dlink_terminate` each write the
        // verdict as their first statement; `Charger::dlink_error` writes none.
        if let Some(verdict) = request.verdict() {
            self.terminate_pause = verdict;
        }
        vec![Effect::SlacUpdate(request.slac_relay())]
    }

    /// The matching lifecycle commands one control pilot reading owes SLAC.
    ///
    /// `EvseManager.cpp:1094-1119`, a block of its own at the head of the pilot
    /// handler: it runs before the charger's queue push at `:1122` and before
    /// the stack block `on_plug_in` and `on_contactor` carry at `:1125-1146`.
    /// The C++ comment at `:1102-1107` says why matching is started as early as
    /// the handler can start it.
    ///
    /// The four arms are `else if` there because `IECStateMachine` delivers one
    /// `CPEvent` per call and the handler runs once per event. It pushes an
    /// arrival and an `EFtoBCD` for the same F to B reading when no vehicle was
    /// latched, so a C++ deployment sends `enter_bcd` twice, in that order.
    /// Preserved: the arrival is tested first here for the same reason.
    ///
    /// Gated on this port's presence, which is `hlc_enabled` where the C++
    /// gates on `slac_enabled`. The two differ only in whether the stack
    /// requirement is wired (`config::hlc_enabled`), so what is narrowed is a
    /// deployment that matches a data link no stack would ever speak over;
    /// `on_data_link` already carries the same narrowing.
    pub fn on_pilot(&mut self, edges: CpEdges) -> Vec<Effect> {
        let mut effects = Vec::new();
        if edges.plugged_in {
            effects.push(Effect::SlacUpdate(SlacUpdate::EnterBcd));
        }
        if edges.entered_bcd {
            effects.push(Effect::SlacUpdate(SlacUpdate::EnterBcd));
        }
        if edges.left_bcd {
            effects.push(Effect::SlacUpdate(SlacUpdate::LeaveBcd));
        }
        if edges.unplugged {
            // `EvseManager.cpp:1112` takes a local copy first, because there
            // `call_leave_bcd` is a synchronous command that can have SLAC
            // reporting `UNMATCHED` back before the next line runs. Here both
            // the command and the report are events on one queue, so the read
            // is ordered by construction and the copy is the C++'s guard
            // against a race this port does not have.
            let was_matched = !self.slac_unmatched;
            effects.push(Effect::SlacUpdate(SlacUpdate::LeaveBcd));
            if was_matched {
                effects.push(Effect::SlacUpdate(SlacUpdate::Reset));
            }
        }
        effects
    }

    /// The SLAC state report's second bit (`EvseManager.cpp:1238-1250`).
    ///
    /// One writer, three arms, one test: the C++ writes `slac_unmatched` true
    /// in the `UNMATCHED` arm and false in both others, which is the negation
    /// of the `matching_started` bit `main.rs` derives from the same report.
    pub fn note_matching_started(&mut self, started: bool) {
        self.slac_unmatched = !started;
    }

    /// SLAC is woken when the EVSE's own pause ends (`Charger.cpp:1020-1022`).
    ///
    /// A vehicle that paused with `D-LINK_PAUSE` is still matched and the C++
    /// deliberately sends it nothing; every other verdict left the link down,
    /// so matching has to start again before the session can.
    pub fn on_session_resume(&self) -> Vec<Effect> {
        if !self.terminate_pause.wakes_slac() {
            return Vec::new();
        }
        vec![Effect::SlacUpdate(SlacUpdate::EnterBcd)]
    }

    /// The fatal error exit from `WaitingForAuthentication`
    /// (`Charger.cpp:319-322`), the one `signal_slac_reset` emitter this port
    /// carries.
    pub fn on_slac_reset(&self) -> Vec<Effect> {
        vec![Effect::SlacUpdate(SlacUpdate::Reset)]
    }

    /// A fault took the port down, and the vehicle is told why.
    ///
    /// `Charger::emergency_shutdown` (`Charger.cpp:2268`) and
    /// `Charger::error_shutdown` (`:2283`) each close with
    /// `signal_hlc_error(Error_EmergencyShutdown)`. Both name the same error,
    /// neither is gated on a live session and neither branches on the charge
    /// mode, so this sits on the port rather than on a power path: the C++
    /// gate is `r_hlc.empty()` alone (`EvseManager.cpp:420-426`), which is the
    /// absent requirement this port's own existence answers for.
    pub fn on_fault_shutdown(&self) -> Vec<Effect> {
        vec![Effect::HlcUpdate(HlcUpdate::SendError(
            EvseError::EmergencyShutdown,
        ))]
    }

    /// The stack gave up on a session (`EvseManager.cpp:365-370`).
    ///
    /// Republished on this module's own interface with the session identity
    /// attached, which the caller supplies because the port holds no session.
    /// The C++ reads `charger->get_session_id()`, which is empty between
    /// sessions, so an absent identity is representable there too.
    pub fn on_session_failed(
        &self,
        uuid: Option<String>,
        reason: HlcSessionFailure,
    ) -> Vec<Effect> {
        vec![Effect::PublishHlcSessionFailed { uuid, reason }]
    }

    /// A DC power supply capability report (`EvseManager.cpp:217-239`).
    ///
    /// Gated on high level communication because the whole C++ subscription is
    /// (`:214-216`), so a deployment without it never hears the report at all
    /// and its limit set stays at the boot seed. Nothing else reads that set.
    ///
    /// **The announcement goes out ahead of the three stack commands, which is
    /// the one place this reverses the C++.** The C++ calls
    /// `update_powersupply_capabilities` first (`:218`) and derives the
    /// advertised set after (`:220-233`), and it can: each of its
    /// `r_hlc[0]->call_*` runs on the subscription's own thread, a millisecond
    /// apart. Here all four share the single ordered lane, where the lane's own
    /// note says one outbound call can block for the full command timeout, so
    /// the C++ position puts this module's own `supported_energy_transfer_modes`
    /// behind three peer round trips.
    ///
    /// It is a variable the device model latches once. `EvseManager` `ready`
    /// is the signal OCPP waits on before it builds the OCPP 2.x device model,
    /// and `EverestDeviceModelStorage` reads the set exactly then: `DCDERCtrlr`
    /// exists only if it carries `DC_BPT`, and `V2XChargingCtrlr.Enabled` is
    /// taken from the same read and never refreshed. An announcement that
    /// arrives after that read is an announcement nobody hears, and the bus
    /// carried the stale set for 166 ms when this sat last.
    ///
    /// Nothing observes the order against those three: they address different
    /// facts on a different peer, and no payload built later reads this
    /// variable back. That is the `ExecContext` membership rule, so this is
    /// the ordering the rule already asks for. `note_ac_capabilities` announces
    /// first for the same reason.
    pub fn note_dc_capabilities(
        &mut self,
        capabilities: PowerSupplyCapabilities,
        floors: Option<PowermeterCapabilities>,
    ) -> Vec<Effect> {
        if !self.runs_dc_limits() {
            return Vec::new();
        }
        // Stored first: `derive` reads `dc_limits.bidirectional()`, and this
        // report is what moves it.
        let to_stack = self.dc_limits.note_capabilities(capabilities, floors);
        let mut effects = self.republish_if_changed();
        effects.extend(to_stack);
        effects
    }

    /// `update_supported_energy_transfers` followed by
    /// `publish_and_update_supported_energy_transfers`, which the C++ runs only
    /// when the set actually changed (`EvseManager.cpp:1796-1806`).
    fn republish_if_changed(&mut self) -> Vec<Effect> {
        let derived = self.derive();
        if derived == self.advertised {
            return Vec::new();
        }
        self.advertised = derived;
        let mut effects = self.publish_advertised();
        effects.push(Effect::HlcUpdate(HlcUpdate::TransferModes(
            self.advertised.clone(),
        )));
        effects
    }

    fn derive(&self) -> Vec<EnergyTransferMode> {
        match self.charge_mode {
            ChargeMode::Ac => supported_ac_transfer_modes(
                self.ac_caps,
                self.config.supported_iso_ac_bpt,
                self.bpt.der_available(),
            ),
            ChargeMode::Dc => {
                supported_dc_transfer_modes(self.config.connector, self.dc_limits.bidirectional())
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::core::session::StartSessionReason;

    fn ac_caps(min_import: i64, max_import: i64) -> AcCapabilities {
        AcCapabilities {
            min_phase_count_import: min_import,
            max_phase_count_import: max_import,
            ..AcCapabilities::default()
        }
    }

    fn exporting(min_import: i64, max_import: i64) -> AcCapabilities {
        AcCapabilities {
            min_phase_count_import: min_import,
            max_phase_count_import: max_import,
            max_current_a_export: 16.0,
            max_phase_count_export: 3,
            ..AcCapabilities::default()
        }
    }

    // The boot sequence and the derivation over a live port.

    use crate::core::config::{Mapping, RawConfig, Settings, Wiring};
    use crate::core::effect::{Effect, HlcUpdate};
    use serde_json::{json, Value};

    fn settings_with(pairs: &[(&str, Value)]) -> Settings {
        let raw: RawConfig = pairs
            .iter()
            .cloned()
            .map(|(k, v)| (k.to_string(), v))
            .collect();
        Settings::from_raw(
            &raw,
            &Mapping {
                evse: 1,
                connectors: vec![1],
            },
        )
        .unwrap()
    }

    /// The wiring an `HlcConfig` exists on: the stack and SLAC connected.
    /// `HlcConfig::for_deployment` reads nothing else off the wiring.
    fn fully_wired() -> Wiring {
        Wiring {
            hlc: true,
            slac: true,
            ..Wiring::default()
        }
    }

    /// A port over the given configuration. There is no disabled port to build:
    /// `HlcConfig::for_deployment` answers `None` for a deployment
    /// `hlc_enabled` would have been false on, and the tests that used to build
    /// a disabled port assert that absence instead; see
    /// `a_basic_ac_deployment_resolves_to_no_configuration_at_all`.
    ///
    /// The AC key is defaulted on and overridable, because on AC the
    /// configuration exists only where it or `ac_with_soc` is set.
    fn port(charge_mode: &str, pairs: &[(&str, Value)]) -> HlcPort {
        let mut all = vec![
            ("charge_mode", json!(charge_mode)),
            ("ac_hlc_enabled", json!(true)),
        ];
        all.extend_from_slice(pairs);
        let settings = settings_with(&all);
        let mode = settings.charge_mode;
        HlcPort::new(
            Arc::new(
                HlcConfig::for_deployment(&settings, &fully_wired())
                    .expect("a wired deployment has a configuration"),
            ),
            mode,
        )
    }

    /// A three phase import, single phase export board report with non zero
    /// current extremes, so every envelope figure is distinguishable from the
    /// safe default zeros.
    fn three_phase_caps() -> AcCapabilities {
        AcCapabilities {
            min_phase_count_import: 1,
            max_phase_count_import: 3,
            max_current_a_import: 32.0,
            min_current_a_import: 6.0,
            max_current_a_export: 16.0,
            min_current_a_export: 2.0,
            max_phase_count_export: 1,
        }
    }

    /// The names of the AC envelope commands in the effect list, in order.
    /// Names rather than payloads, because the payloads are pinned in
    /// `ac_params` and what a port level test decides is which commands go out
    /// and in what order.
    fn ac_envelope_commands(effects: &[Effect]) -> Vec<String> {
        effects
            .iter()
            .filter_map(|effect| match effect {
                Effect::HlcUpdate(HlcUpdate::AcMaximumLimits(_)) => Some("AcMaximumLimits"),
                Effect::HlcUpdate(HlcUpdate::AcMinimumLimits(_)) => Some("AcMinimumLimits"),
                Effect::HlcUpdate(HlcUpdate::AcParameters(_)) => Some("AcParameters"),
                _ => None,
            })
            .map(str::to_string)
            .collect()
    }

    fn transfer_modes_told_to_the_stack(effects: &[Effect]) -> Vec<Vec<EnergyTransferMode>> {
        effects
            .iter()
            .filter_map(|effect| match effect {
                Effect::HlcUpdate(HlcUpdate::TransferModes(modes)) => Some(modes.clone()),
                _ => None,
            })
            .collect()
    }

    fn setups_told_to_the_stack(effects: &[Effect]) -> Vec<SessionSetup> {
        effects
            .iter()
            .filter_map(|effect| match effect {
                Effect::HlcUpdate(HlcUpdate::SessionSetup(setup)) => Some(setup.clone()),
                _ => None,
            })
            .collect()
    }

    /// The re-announcement `ac_with_soc` asks for on each flip.
    mod mode_announcement {
        use super::*;

        /// `EvseManager::setup_fake_DC_mode`, in its order: the present values,
        /// the maximum set, the minimum set, the identity announcement, then
        /// the advertised set. The figures are hardcoded there and describe no
        /// hardware.
        #[test]
        fn the_fake_dc_announcement_runs_in_the_ported_order() {
            let effects = port("AC", &[("ac_with_soc", json!(true))]).announce(PresentedMode::Dc);

            assert_eq!(
                effects,
                vec![
                    Effect::HlcUpdate(HlcUpdate::DcPresentValues {
                        voltage_v: 400.0,
                        current_a: 0.0,
                    }),
                    Effect::HlcUpdate(HlcUpdate::DcMaximumLimits(MaximumLimits {
                        maximum_current_a: 400.0,
                        maximum_voltage_v: 1000.0,
                        maximum_power_w: 200_000.0,
                        maximum_discharge_current_a: None,
                        maximum_discharge_power_w: None,
                    })),
                    Effect::HlcUpdate(HlcUpdate::DcMinimumLimits(MinimumLimits {
                        minimum_current_a: 0.0,
                        minimum_voltage_v: 0.0,
                        minimum_power_w: 0.0,
                        minimum_discharge_current_a: None,
                        minimum_discharge_power_w: None,
                    })),
                    Effect::HlcUpdate(HlcUpdate::Setup {
                        evse_id: "DE*PNX*E1234567*1".to_string(),
                        evse_id_din: "49A80737A45678".to_string(),
                        sae_mode: SaeBidiMode::None,
                        debug_mode: false,
                    }),
                    Effect::PublishSupportedTransferModes(vec![
                        EnergyTransferMode::DcExtended,
                        EnergyTransferMode::DcCore,
                    ]),
                    Effect::HlcUpdate(HlcUpdate::TransferModes(vec![
                        EnergyTransferMode::DcExtended,
                        EnergyTransferMode::DcCore,
                    ])),
                ]
            );
        }

        /// Nothing, and it is the C++ answer.
        /// `EvseManager::setup_AC_mode` sends its `call_setup` and its
        /// advertised set only inside `if (ac_hlc_enabled)`, which is the
        /// function's argument; both call sites that reach it in this mode,
        /// `switch_AC_mode` and the `subscribe_dlink_error` arm, pass false. So
        /// the AC transfer mode list it builds two statements earlier is
        /// computed and discarded.
        #[test]
        fn the_ac_announcement_tells_the_stack_nothing() {
            let effects = port("AC", &[("ac_with_soc", json!(true))]).announce(PresentedMode::Ac);
            assert_eq!(effects, Vec::new(), "got {effects:?}");
        }

        /// The fake DC set is not stored, so the derivation this port owns is
        /// untouched by a flip. That is what keeps `charge_mode` immutable, and
        /// it is why an AC capability report arriving afterwards overwrites the
        /// announcement, exactly as
        /// `recompute_and_publish_supported_ac_energy_transfers` does in the
        /// C++.
        #[test]
        fn announcing_the_fake_dc_set_does_not_change_the_derived_one() {
            let mut port = port("AC", &[("ac_with_soc", json!(true))]);
            let before = port.advertised().to_vec();

            port.announce(PresentedMode::Dc);

            assert_eq!(port.advertised(), before.as_slice());
            let republished = port.note_ac_capabilities(three_phase_caps());
            assert!(
                republished.contains(&Effect::PublishSupportedTransferModes(
                    port.advertised().to_vec()
                )),
                "a later capability report republishes the AC set, got {republished:?}"
            );
        }
    }

    #[test]
    fn the_boot_sequence_runs_in_the_ported_order() {
        let effects = port("DC", &[("evse_id", json!("DE*ABC*E1"))]).boot(false, None);
        assert_eq!(
            effects,
            vec![
                Effect::HlcUpdate(HlcUpdate::SessionSetup(SessionSetup {
                    payment_options: vec![PaymentOption::ExternalPayment, PaymentOption::Contract],
                    supported_certificate_service: true,
                    central_contract_validation_allowed: false,
                    fake_dc: false,
                })),
                // `:561-564`, the DC block's direct push. The seed report is
                // forwarded because nothing has been sent yet; see
                // `dc_limits::boot`.
                Effect::HlcUpdate(HlcUpdate::PowerSupplyCapabilities(Box::new(
                    PowerSupplyCapabilities::sane_default()
                ))),
                Effect::HlcUpdate(HlcUpdate::ChargingParameters(PhysicalValues {
                    ac_nominal_voltage_v: None,
                    dc_current_regulation_tolerance_a: Some(0.5),
                    dc_peak_current_ripple_a: Some(0.5),
                    dc_energy_to_be_delivered_wh: Some(10_000.0),
                })),
                Effect::HlcUpdate(HlcUpdate::DcMinimumLimits(MinimumLimits::default())),
                Effect::HlcUpdate(HlcUpdate::DcPresentValues {
                    voltage_v: 0.0,
                    current_a: 0.0,
                }),
                Effect::HlcUpdate(HlcUpdate::ReceiptRequired(false)),
                Effect::HlcUpdate(HlcUpdate::Setup {
                    evse_id: "DE*ABC*E1".to_string(),
                    evse_id_din: "49A80737A45678".to_string(),
                    sae_mode: SaeBidiMode::None,
                    debug_mode: false,
                }),
                Effect::PublishSupportedTransferModes(vec![EnergyTransferMode::DcExtended]),
                Effect::HlcUpdate(HlcUpdate::TransferModes(vec![
                    EnergyTransferMode::DcExtended
                ])),
                Effect::HlcUpdate(HlcUpdate::ResetError),
            ]
        );
    }

    #[test]
    fn the_session_logging_setting_travels_as_the_stacks_debug_mode() {
        let effects = port("DC", &[("session_logging", json!(true))]).boot(false, None);
        assert!(effects.contains(&Effect::HlcUpdate(HlcUpdate::Setup {
            evse_id: "DE*PNX*E1234567*1".to_string(),
            evse_id_din: "49A80737A45678".to_string(),
            sae_mode: SaeBidiMode::None,
            debug_mode: true,
        })));
    }

    /// `EvseManager.cpp:362` sits near the head of the `if (hlc_enabled)` block
    /// and `:971` in its tail, so the vehicle's payment options are settled
    /// before anything else the stack is told. The DC limit block at `:544-561`
    /// sits between the two.
    #[test]
    fn the_session_setup_is_the_first_thing_the_stack_is_told() {
        let effects = port("DC", &[("ev_receipt_required", json!(true))]).boot(false, None);
        assert!(matches!(
            effects.first(),
            Some(Effect::HlcUpdate(HlcUpdate::SessionSetup(_)))
        ));
        let receipt_at = effects
            .iter()
            .position(|effect| *effect == Effect::HlcUpdate(HlcUpdate::ReceiptRequired(true)))
            .expect("the receipt setting is announced");
        let dc_at = effects
            .iter()
            .position(|effect| {
                matches!(effect, Effect::HlcUpdate(HlcUpdate::ChargingParameters(_)))
            })
            .expect("a DC port announces its physical values");
        assert!(dc_at < receipt_at, "{effects:?}");
    }

    /// An AC port emits none of the DC block: the C++ has it inside
    /// `if (config.charge_mode == "DC")`.
    ///
    /// The physical values command is the exception, because both branches send
    /// it. What tells them apart is which half of it they fill, so the two are
    /// distinguished by payload here and not by absence.
    #[test]
    fn an_ac_ports_boot_carries_no_dc_limit_block() {
        let effects = port("AC", &[("ac_hlc_enabled", json!(true))]).boot(false, None);
        assert!(
            !effects.iter().any(|effect| matches!(
                effect,
                Effect::HlcUpdate(
                    HlcUpdate::DcMinimumLimits(_) | HlcUpdate::DcPresentValues { .. }
                )
            )),
            "{effects:?}"
        );
        assert!(
            effects.contains(&Effect::HlcUpdate(HlcUpdate::ChargingParameters(
                PhysicalValues {
                    ac_nominal_voltage_v: Some(230.0),
                    dc_current_regulation_tolerance_a: None,
                    dc_peak_current_ripple_a: None,
                    dc_energy_to_be_delivered_wh: None,
                }
            ))),
            "the AC nominal voltage is announced, and the DC fields stay absent: {effects:?}"
        );
    }

    /// `:447-449` is the AC branch's only boot emission. The three envelope
    /// commands come from the board support handler at `:287` and from nowhere
    /// else, so a port whose board has not published announces no envelope.
    #[test]
    fn an_ac_ports_boot_announces_no_power_envelope() {
        let effects = port("AC", &[("ac_hlc_enabled", json!(true))]).boot(false, None);
        assert!(
            !effects.iter().any(|effect| matches!(
                effect,
                Effect::HlcUpdate(
                    HlcUpdate::AcMaximumLimits(_)
                        | HlcUpdate::AcMinimumLimits(_)
                        | HlcUpdate::AcParameters(_)
                )
            )),
            "{effects:?}"
        );
    }

    /// A DC port takes the DC arm of the same branch, so the AC nominal voltage
    /// is absent from its physical values. The two arms fill disjoint halves and
    /// a port that filled both would be announcing a setup it does not have.
    #[test]
    fn a_dc_ports_boot_names_no_ac_nominal_voltage() {
        let effects = port("DC", &[]).boot(false, None);
        let announced: Vec<PhysicalValues> = effects
            .iter()
            .filter_map(|effect| match effect {
                Effect::HlcUpdate(HlcUpdate::ChargingParameters(values)) => Some(*values),
                _ => None,
            })
            .collect();
        assert_eq!(announced.len(), 1, "{effects:?}");
        assert_eq!(announced[0].ac_nominal_voltage_v, None);
    }

    #[test]
    fn boot_tells_the_stack_the_configured_payment_options_once() {
        let effects = port("DC", &[("payment_enable_contract", json!(false))]).boot(false, None);
        assert_eq!(
            setups_told_to_the_stack(&effects),
            vec![SessionSetup {
                payment_options: vec![PaymentOption::ExternalPayment],
                supported_certificate_service: false,
                central_contract_validation_allowed: false,
                fake_dc: false,
            }]
        );
    }

    #[test]
    fn each_trigger_point_re_emits_one_session_setup() {
        let port = port("DC", &[]);
        for trigger in [
            Trigger::Authorized,
            Trigger::SessionFinished,
            Trigger::SessionStarted(StartSessionReason::EvConnected),
            Trigger::SessionStarted(StartSessionReason::Authorized),
        ] {
            let effects = port.session_setup(trigger, false);
            assert_eq!(
                setups_told_to_the_stack(&effects).len(),
                1,
                "{trigger:?} emitted {} setups",
                setups_told_to_the_stack(&effects).len()
            );
            assert_eq!(effects.len(), 1, "{trigger:?} emitted something else too");
        }
    }

    /// The `SessionFinished` only contract option, over the live port rather
    /// than over the derivation alone (`EvseManager.cpp:1294-1301`).
    #[test]
    fn only_a_finished_session_is_re_offered_the_contract() {
        let port = port("DC", &[]);
        let offered = |trigger| {
            setups_told_to_the_stack(&port.session_setup(trigger, false))
                .first()
                .expect("a session setup")
                .payment_options
                .contains(&PaymentOption::Contract)
        };
        assert!(offered(Trigger::SessionFinished));
        assert!(!offered(Trigger::Authorized));
    }

    /// The command owns the state after boot, so the next trigger point sees
    /// what it wrote and not what configuration said.
    #[test]
    fn the_plug_and_charge_command_reaches_the_next_session_setup() {
        let mut port = port("DC", &[]);
        assert!(
            setups_told_to_the_stack(&port.session_setup(Trigger::SessionFinished, false))[0]
                .payment_options
                .contains(&PaymentOption::Contract)
        );

        port.configure_plug_and_charge(&PlugAndChargeConfiguration {
            enabled: Some(false),
            ..PlugAndChargeConfiguration::default()
        });
        assert!(
            !setups_told_to_the_stack(&port.session_setup(Trigger::SessionFinished, false))[0]
                .payment_options
                .contains(&PaymentOption::Contract)
        );
    }

    /// The command produces nothing by itself. The C++ setters only write the
    /// atomics, so a change made mid session does not reach the vehicle until
    /// the next trigger point.
    #[test]
    fn the_plug_and_charge_command_tells_the_stack_nothing_by_itself() {
        let mut port = port("DC", &[]);
        let before = port.session_setup(Trigger::SessionFinished, false);
        port.configure_plug_and_charge(&PlugAndChargeConfiguration {
            central_validation_allowed: Some(true),
            ..PlugAndChargeConfiguration::default()
        });
        // Nothing was emitted, and the change is visible only on the next
        // derivation.
        assert_ne!(before, port.session_setup(Trigger::SessionFinished, false));
    }

    /// The whole of the old `if (hlc_enabled)` gate, in one assertion.
    ///
    /// There used to be seven tests here, each building a port over a disabled
    /// `HlcConfig` and asserting that one method returned nothing: the boot
    /// sequence, the re-announcement, the session setup, the AC envelope, the
    /// two AC limit emissions and the transfer mode refusal. Each pinned one
    /// `if !self.config.enabled { return Vec::new(); }` guard, and there were
    /// thirty of those. This replaces all seven, because the guards are gone: a
    /// deployment without a stack has no configuration, so it has no port, so
    /// there is no method on it to call.
    ///
    /// What the C++ still publishes for such a deployment is the empty
    /// advertised set (`EvseManager.cpp:1505` publishes the monitor
    /// unconditionally), and that answer moved to where the absence is:
    /// `Core::advertised_transfer_modes`.
    #[test]
    fn a_basic_ac_deployment_resolves_to_no_configuration_at_all() {
        let settings = settings_with(&[("charge_mode", json!("AC"))]);
        assert!(!settings.ac.hlc_enabled, "neither AC key is set");
        assert!(!settings.ac.with_soc);
        assert!(HlcConfig::for_deployment(&settings, &fully_wired()).is_none());
    }

    #[test]
    fn the_bidirectional_setup_needs_both_the_channel_and_the_generator_mode() {
        let bpt = |channel: &str, generator: &str| {
            port(
                "DC",
                &[
                    ("bpt_channel", json!(channel)),
                    ("bpt_generator_mode", json!(generator)),
                ],
            )
            .boot(false, None)
            .iter()
            .any(|effect| matches!(effect, Effect::HlcUpdate(HlcUpdate::BptSetup(_))))
        };
        assert!(bpt("Unified", "GridFollowing"));
        assert!(!bpt("Unified", "None"));
        assert!(!bpt("None", "GridFollowing"));
        assert!(!bpt("None", "None"));
    }

    #[test]
    fn the_bidirectional_setup_carries_the_configured_channel_mode_and_detection() {
        let effects = port(
            "DC",
            &[
                ("bpt_channel", json!("Separated")),
                ("bpt_generator_mode", json!("GridForming")),
                ("bpt_grid_code_island_method", json!("Passive")),
            ],
        )
        .boot(false, None);
        assert!(
            effects.contains(&Effect::HlcUpdate(HlcUpdate::BptSetup(BptSetup {
                channel: BptChannel::Separated,
                generator_mode: GeneratorMode::GridForming,
                grid_code_detection: Some(GridCodeIslandingDetection::Passive),
            })))
        );
    }

    #[test]
    fn an_unset_island_detection_method_leaves_the_field_absent() {
        let effects = port(
            "DC",
            &[
                ("bpt_channel", json!("Unified")),
                ("bpt_generator_mode", json!("GridFollowing")),
            ],
        )
        .boot(false, None);
        assert!(
            effects.contains(&Effect::HlcUpdate(HlcUpdate::BptSetup(BptSetup {
                channel: BptChannel::Unified,
                generator_mode: GeneratorMode::GridFollowing,
                grid_code_detection: None,
            })))
        );
    }

    #[test]
    fn the_bidirectional_setup_is_announced_before_the_error_reset() {
        let effects = port(
            "DC",
            &[
                ("bpt_channel", json!("Unified")),
                ("bpt_generator_mode", json!("GridFollowing")),
            ],
        )
        .boot(false, None);
        let bpt = effects
            .iter()
            .position(|effect| matches!(effect, Effect::HlcUpdate(HlcUpdate::BptSetup(_))))
            .expect("bidirectional setup");
        let reset = effects
            .iter()
            .position(|effect| effect == &Effect::HlcUpdate(HlcUpdate::ResetError))
            .expect("error reset");
        assert!(bpt < reset);
    }

    /// The SAE activation raises the flag whatever mode the deployment
    /// configured, which is `EvseManager.cpp:932`: the assignment precedes the
    /// mode branch at `:934-941` and is not inside any arm of it. The branch
    /// chooses between the V2H limit rewrite, a V2G log line and an error for
    /// an unrecognized name, and all three leave the flag raised.
    ///
    /// The mode axis is otherwise undrivable here, because the V2H arm is the
    /// local energy limit rewrite and that is unported (see
    /// `docs/architecture.md`). This asserts what that means: the two modes are
    /// indistinguishable in this port's behavior, so a reader is not left to
    /// assume the V2H half is present.
    ///
    /// The C++ has a third case, an unrecognized mode name, which reaches
    /// `:939-940` and logs an error with the flag already raised. It is not
    /// driven because it is not representable: this port refuses an
    /// unrecognized `sae_j2847_2_bpt_mode` when the configuration is parsed
    /// (`config.rs`), so no such deployment starts. That is the port's existing
    /// configuration strictness and not a decision made here.
    #[test]
    fn the_sae_activation_resolves_the_fact_whatever_mode_was_configured() {
        for mode in ["V2H", "V2G"] {
            let mut port = port(
                "DC",
                &[
                    ("sae_j2847_2_bpt_enabled", json!(true)),
                    ("sae_j2847_2_bpt_mode", json!(mode)),
                ],
            );
            assert!(!port.bidirectional(), "the control for {mode}");

            port.note_sae_bidi_active();

            assert!(port.bidirectional(), "{mode} must raise the flag");
        }
    }

    /// The config source needs no vehicle and no stack signal: it resolves the
    /// fact from the moment the port exists, which is what
    /// `hack_allow_bpt_with_iso2` means.
    #[test]
    fn the_config_source_resolves_the_fact_from_boot() {
        let plain = port("DC", &[]);
        assert!(!plain.bidirectional());

        let hacked = port("DC", &[("hack_allow_bpt_with_iso2", json!(true))]);
        assert!(hacked.bidirectional());
    }

    #[test]
    fn only_a_dc_deployment_that_enables_sae_announces_a_bidirectional_mode() {
        // `EvseManager.cpp:929` is the only assignment and it sits inside the DC
        // branch, inside `if (config.sae_j2847_2_bpt_enabled)`.
        let sae_mode = |charge_mode: &str, enabled: bool, mode: &str| {
            port(
                charge_mode,
                &[
                    ("sae_j2847_2_bpt_enabled", json!(enabled)),
                    ("sae_j2847_2_bpt_mode", json!(mode)),
                ],
            )
            .config
            .sae_mode
        };
        assert_eq!(sae_mode("DC", true, "V2H"), SaeBidiMode::V2h);
        assert_eq!(sae_mode("DC", true, "V2G"), SaeBidiMode::V2g);
        assert_eq!(sae_mode("DC", false, "V2H"), SaeBidiMode::None);
        assert_eq!(sae_mode("AC", true, "V2H"), SaeBidiMode::None);
    }

    fn dc_capabilities(bidirectional: bool) -> PowerSupplyCapabilities {
        PowerSupplyCapabilities {
            bidirectional,
            ..PowerSupplyCapabilities::sane_default()
        }
    }

    /// A capability report drives two things: the advertised set and the DC
    /// limit emissions. These tests are about the first, so the second is
    /// dropped here rather than restated in each of them. `dc_limits`'s own
    /// tests own the emissions.
    fn advertised_set_effects(port: &mut HlcPort, bidirectional: bool) -> Vec<Effect> {
        port.note_dc_capabilities(dc_capabilities(bidirectional), None)
            .into_iter()
            .filter(|effect| {
                !matches!(
                    effect,
                    Effect::HlcUpdate(
                        HlcUpdate::PowerSupplyCapabilities(_)
                            | HlcUpdate::ChargingParameters(_)
                            | HlcUpdate::DcMinimumLimits(_)
                            | HlcUpdate::DcPresentValues { .. }
                    )
                )
            })
            .collect()
    }

    #[test]
    fn a_dc_port_advertises_nothing_bidirectional_until_the_supply_says_it_can() {
        let mut port = port("DC", &[]);
        assert_eq!(port.advertised(), [EnergyTransferMode::DcExtended]);

        let effects = advertised_set_effects(&mut port, true);
        assert_eq!(
            port.advertised(),
            [EnergyTransferMode::DcExtended, EnergyTransferMode::DcBpt]
        );
        assert_eq!(
            effects,
            vec![
                Effect::PublishSupportedTransferModes(vec![
                    EnergyTransferMode::DcExtended,
                    EnergyTransferMode::DcBpt
                ]),
                Effect::HlcUpdate(HlcUpdate::TransferModes(vec![
                    EnergyTransferMode::DcExtended,
                    EnergyTransferMode::DcBpt
                ])),
            ]
        );
    }

    #[test]
    fn the_advertised_set_is_announced_before_the_stack_is_told_anything() {
        // The whole unfiltered report, because the order is the point.
        //
        // Every effect here shares one lane, and an outbound call on it can
        // block for the full command timeout. So a `PublishSupportedTransferModes`
        // behind the three stack commands reaches this module's own interface
        // only after three peer round trips: measured at 166 ms, against the
        // 86 ms OCPP leaves between the `ready` it waits on and the read
        // `EverestDeviceModelStorage` makes of this variable. The set it reads
        // is what decides whether `DCDERCtrlr` exists at all, so a late
        // announcement costs the whole DER device model.
        let mut port = port("DC", &[]);
        let effects = port.note_dc_capabilities(dc_capabilities(true), None);

        let announced = effects
            .iter()
            .position(|effect| matches!(effect, Effect::PublishSupportedTransferModes(_)))
            .expect("the bidirectional report moves the set, so it is announced");
        let first_told = effects
            .iter()
            .position(|effect| matches!(effect, Effect::HlcUpdate(_)))
            .expect("the report is forwarded to the stack");

        assert!(
            announced < first_told,
            "the announcement is behind the stack commands: {effects:?}"
        );
    }

    #[test]
    fn a_capability_report_that_changes_nothing_republishes_nothing() {
        // `update_supported_energy_transfers` returns false on an unchanged set
        // and the publish is gated on it (`EvseManager.cpp:1796-1806`), so a
        // supply that repeats its capabilities does not restate the set.
        let mut port = port("DC", &[]);
        assert!(advertised_set_effects(&mut port, false).is_empty());
        assert!(!advertised_set_effects(&mut port, true).is_empty());
        assert!(advertised_set_effects(&mut port, true).is_empty());
    }

    #[test]
    fn losing_bidirectional_capability_narrows_the_advertised_set_again() {
        let mut port = port("DC", &[]);
        advertised_set_effects(&mut port, true);
        let effects = advertised_set_effects(&mut port, false);
        assert_eq!(port.advertised(), [EnergyTransferMode::DcExtended]);
        assert_eq!(
            transfer_modes_told_to_the_stack(&effects),
            vec![vec![EnergyTransferMode::DcExtended]]
        );
    }

    #[test]
    fn an_mcs_dc_port_advertises_the_mcs_modes_from_boot() {
        let mut port = port("DC", &[("connector_type", json!("cMCS"))]);
        assert_eq!(port.advertised(), [EnergyTransferMode::Mcs]);
        advertised_set_effects(&mut port, true);
        assert_eq!(
            port.advertised(),
            [EnergyTransferMode::Mcs, EnergyTransferMode::McsBpt]
        );
    }

    #[test]
    fn an_ac_port_boots_on_the_safe_default_capabilities() {
        // Zero minimum and one maximum phase, which is the single phase mode.
        let port = port("AC", &[("ac_hlc_enabled", json!(true))]);
        assert_eq!(port.advertised(), [EnergyTransferMode::AcSinglePhase]);
    }

    #[test]
    fn a_board_report_widens_the_advertised_ac_set() {
        let mut port = port("AC", &[("ac_hlc_enabled", json!(true))]);
        let effects = port.note_ac_capabilities(AcCapabilities {
            min_phase_count_import: 1,
            max_phase_count_import: 3,
            max_current_a_export: 0.0,
            max_phase_count_export: 0,
            ..AcCapabilities::default()
        });
        assert_eq!(
            port.advertised(),
            [
                EnergyTransferMode::AcSinglePhase,
                EnergyTransferMode::AcTwoPhase,
                EnergyTransferMode::AcThreePhase
            ]
        );
        assert_eq!(
            transfer_modes_told_to_the_stack(&effects),
            vec![vec![
                EnergyTransferMode::AcSinglePhase,
                EnergyTransferMode::AcTwoPhase,
                EnergyTransferMode::AcThreePhase
            ]]
        );
    }

    #[test]
    fn a_board_report_arriving_mid_session_republishes_the_narrowed_set() {
        // Derating is the reason the C++ subscribes rather than reading once
        // (`EvseManager.cpp:216`), and nothing about the recomputation is gated
        // on a session being idle.
        let mut port = port("AC", &[("ac_hlc_enabled", json!(true))]);
        port.note_ac_capabilities(AcCapabilities {
            min_phase_count_import: 1,
            max_phase_count_import: 3,
            max_current_a_export: 0.0,
            max_phase_count_export: 0,
            ..AcCapabilities::default()
        });
        let effects = port.note_ac_capabilities(AcCapabilities {
            min_phase_count_import: 1,
            max_phase_count_import: 1,
            max_current_a_export: 0.0,
            max_phase_count_export: 0,
            ..AcCapabilities::default()
        });
        assert_eq!(port.advertised(), [EnergyTransferMode::AcSinglePhase]);
        assert_eq!(
            transfer_modes_told_to_the_stack(&effects),
            vec![vec![EnergyTransferMode::AcSinglePhase]]
        );
    }

    #[test]
    fn the_ac_bpt_setting_only_reaches_the_set_once_the_board_reports_export() {
        let mut port = port(
            "AC",
            &[
                ("ac_hlc_enabled", json!(true)),
                ("supported_iso_ac_bpt", json!(true)),
            ],
        );
        assert_eq!(port.advertised(), [EnergyTransferMode::AcSinglePhase]);
        port.note_ac_capabilities(AcCapabilities {
            min_phase_count_import: 1,
            max_phase_count_import: 1,
            max_current_a_export: 16.0,
            max_phase_count_export: 1,
            ..AcCapabilities::default()
        });
        assert_eq!(
            port.advertised(),
            [EnergyTransferMode::AcSinglePhase, EnergyTransferMode::AcBpt]
        );
    }

    #[test]
    fn an_mcs_connector_does_not_relabel_the_ac_modes() {
        // The MCS relabelling lives in the DC derivation and in the external
        // narrowing only. The AC derivation
        // (`energy_transfer_modes.cpp:12-37`) never reads the connector type,
        // so an MCS labelled AC port advertises the ordinary AC modes and the
        // ordinary `AC_BPT`, not an MCS mode.
        let mut port = port(
            "AC",
            &[
                ("ac_hlc_enabled", json!(true)),
                ("connector_type", json!("cMCS")),
                ("supported_iso_ac_bpt", json!(true)),
            ],
        );
        port.note_ac_capabilities(AcCapabilities {
            min_phase_count_import: 1,
            max_phase_count_import: 3,
            max_current_a_export: 32.0,
            max_phase_count_export: 3,
            ..AcCapabilities::default()
        });
        assert_eq!(
            port.advertised(),
            [
                EnergyTransferMode::AcSinglePhase,
                EnergyTransferMode::AcTwoPhase,
                EnergyTransferMode::AcThreePhase,
                EnergyTransferMode::AcBpt,
            ]
        );
    }

    #[test]
    fn a_dc_port_ignores_a_board_capability_report() {
        // The C++ recomputation is gated on `charge_mode == "AC"`
        // (`EvseManager.cpp:281`), and the AC derivation would otherwise
        // overwrite a DC set with AC modes.
        let mut port = port("DC", &[]);
        let effects = port.note_ac_capabilities(AcCapabilities {
            min_phase_count_import: 1,
            max_phase_count_import: 3,
            max_current_a_export: 16.0,
            max_phase_count_export: 3,
            ..AcCapabilities::default()
        });
        assert!(effects.is_empty());
        assert_eq!(port.advertised(), [EnergyTransferMode::DcExtended]);
    }

    #[test]
    fn an_ac_port_ignores_a_supply_capability_report() {
        let mut port = port("AC", &[("ac_hlc_enabled", json!(true))]);
        assert!(
            port.note_dc_capabilities(dc_capabilities(true), None).is_empty(),
            "the C++ subscribes the report only on a DC port"
        );
        assert_eq!(port.advertised(), [EnergyTransferMode::AcSinglePhase]);
    }

    #[test]
    fn a_request_passes_through_untouched_on_a_non_mcs_connector() {
        let port = port("DC", &[]);
        assert_eq!(
            filter_allowed(
                &port.config,
                &[EnergyTransferMode::DcExtended, EnergyTransferMode::DcBpt]
            ),
            Ok(vec![
                EnergyTransferMode::DcExtended,
                EnergyTransferMode::DcBpt
            ])
        );
    }

    #[test]
    fn an_mcs_connector_leaves_the_iso_2_spelling_of_dc_alone() {
        // `evse/evse_managerImpl.cpp:546-552` names `DC` and `DC_BPT`, the two
        // ISO 15118-20 spellings, and not `DC_extended`, which is what the
        // module's own DC derivation advertises. Preserved rather than
        // corrected, and pinned so the asymmetry is visible rather than
        // looking like an oversight in this port.
        let port = port("DC", &[("connector_type", json!("cMCS"))]);
        assert_eq!(
            filter_allowed(&port.config, &[EnergyTransferMode::DcExtended]),
            Ok(vec![EnergyTransferMode::DcExtended])
        );
    }

    #[test]
    fn an_mcs_connector_relabels_the_dc_modes_of_a_request() {
        let port = port("DC", &[("connector_type", json!("cMCS"))]);
        assert_eq!(
            filter_allowed(
                &port.config,
                &[
                    EnergyTransferMode::Dc,
                    EnergyTransferMode::DcBpt,
                    EnergyTransferMode::AcSinglePhase,
                ]
            ),
            Ok(vec![
                EnergyTransferMode::Mcs,
                EnergyTransferMode::McsBpt,
                EnergyTransferMode::AcSinglePhase,
            ])
        );
    }

    #[test]
    fn a_request_narrowed_to_nothing_is_refused_as_incompatible() {
        // The interface declares `minItems: 1`, so an empty list should not
        // arrive over the bus, and the handler is still handed a plain vector.
        let port = port("DC", &[]);
        assert_eq!(
            filter_allowed(&port.config, &[]),
            Err(UpdateRefusal::IncompatibleEnergyTransfer)
        );
    }

    #[test]
    fn an_accepted_request_leaves_the_advertised_set_alone() {
        // The C++ command calls the stack directly and never touches the
        // monitor, so the published variable keeps saying what the derivation
        // said (`evse/evse_managerImpl.cpp:562-564`).
        let port = port("DC", &[]);
        filter_allowed(&port.config, &[EnergyTransferMode::AcSinglePhase]).unwrap();
        assert_eq!(port.advertised(), [EnergyTransferMode::DcExtended]);
    }

    #[test]
    fn a_single_phase_board_advertises_one_ac_mode() {
        assert_eq!(
            supported_ac_transfer_modes(ac_caps(1, 1), false, false),
            vec![EnergyTransferMode::AcSinglePhase]
        );
    }

    #[test]
    fn a_three_phase_board_advertises_every_phase_count_between_its_bounds() {
        assert_eq!(
            supported_ac_transfer_modes(ac_caps(1, 3), false, false),
            vec![
                EnergyTransferMode::AcSinglePhase,
                EnergyTransferMode::AcTwoPhase,
                EnergyTransferMode::AcThreePhase,
            ]
        );
    }

    #[test]
    fn a_board_that_cannot_charge_on_fewer_than_three_phases_advertises_only_three() {
        assert_eq!(
            supported_ac_transfer_modes(ac_caps(3, 3), false, false),
            vec![EnergyTransferMode::AcThreePhase]
        );
    }

    #[test]
    fn a_zero_minimum_phase_count_is_clamped_up_to_one() {
        // The default the C++ installs before the board reports is
        // `min_phase_count_import = 0` with a maximum of 1
        // (`EvseManager.cpp:148-155`), and the clamp is what stops that
        // becoming an empty set at boot.
        assert_eq!(
            supported_ac_transfer_modes(ac_caps(0, 1), false, false),
            vec![EnergyTransferMode::AcSinglePhase]
        );
    }

    #[test]
    fn a_maximum_phase_count_below_the_minimum_still_advertises_the_minimum() {
        // The C++ clamps the maximum into `[min, 3]`, so an inconsistent board
        // report never produces an empty advertised set.
        assert_eq!(
            supported_ac_transfer_modes(ac_caps(3, 1), false, false),
            vec![EnergyTransferMode::AcThreePhase]
        );
    }

    #[test]
    fn a_phase_count_report_outside_one_to_three_is_clamped_rather_than_trusted() {
        // The C++ clamps because the board publishes whatever it likes and the
        // three ISO 15118 AC modes are the only ones there are. Two of the
        // three out of range shapes are not merely wrong here, they are fatal:
        // `i64::clamp` panics when its lower bound exceeds its upper, so an
        // unclamped minimum above three would take down the writer thread on a
        // capability report, and a negative minimum would advertise nothing at
        // all.
        assert_eq!(
            supported_ac_transfer_modes(ac_caps(4, 4), false, false),
            vec![EnergyTransferMode::AcThreePhase]
        );
        assert_eq!(
            supported_ac_transfer_modes(ac_caps(-1, -1), false, false),
            vec![EnergyTransferMode::AcSinglePhase]
        );
        assert_eq!(
            supported_ac_transfer_modes(ac_caps(0, 9), false, false),
            vec![
                EnergyTransferMode::AcSinglePhase,
                EnergyTransferMode::AcTwoPhase,
                EnergyTransferMode::AcThreePhase,
            ]
        );
    }

    #[test]
    fn ac_bpt_needs_both_the_setting_and_an_export_capable_board() {
        assert_eq!(
            supported_ac_transfer_modes(exporting(1, 1), true, false),
            vec![EnergyTransferMode::AcSinglePhase, EnergyTransferMode::AcBpt]
        );
        // The setting alone is not enough.
        assert_eq!(
            supported_ac_transfer_modes(ac_caps(1, 1), true, false),
            vec![EnergyTransferMode::AcSinglePhase]
        );
        // Nor is the board alone.
        assert_eq!(
            supported_ac_transfer_modes(exporting(1, 1), false, false),
            vec![EnergyTransferMode::AcSinglePhase]
        );
    }

    #[test]
    fn an_export_current_without_an_export_phase_is_not_export_capable() {
        // Both halves of `export_capable` are load bearing
        // (`energy_transfer_modes.cpp:30`).
        let current_only = AcCapabilities {
            min_phase_count_import: 1,
            max_phase_count_import: 1,
            max_current_a_export: 16.0,
            max_phase_count_export: 0,
            ..AcCapabilities::default()
        };
        let phases_only = AcCapabilities {
            min_phase_count_import: 1,
            max_phase_count_import: 1,
            max_current_a_export: 0.0,
            max_phase_count_export: 3,
            ..AcCapabilities::default()
        };
        assert_eq!(
            supported_ac_transfer_modes(current_only, true, true),
            vec![EnergyTransferMode::AcSinglePhase]
        );
        assert_eq!(
            supported_ac_transfer_modes(phases_only, true, true),
            vec![EnergyTransferMode::AcSinglePhase]
        );
    }

    #[test]
    fn ac_der_is_advertised_after_ac_bpt_and_needs_export_capability_too() {
        assert_eq!(
            supported_ac_transfer_modes(exporting(1, 1), true, true),
            vec![
                EnergyTransferMode::AcSinglePhase,
                EnergyTransferMode::AcBpt,
                EnergyTransferMode::AcDerIec,
            ]
        );
        assert_eq!(
            supported_ac_transfer_modes(exporting(1, 1), false, true),
            vec![
                EnergyTransferMode::AcSinglePhase,
                EnergyTransferMode::AcDerIec
            ]
        );
    }

    #[test]
    fn the_dc_set_is_the_extended_mode_and_its_bidirectional_partner() {
        assert_eq!(
            supported_dc_transfer_modes(ConnectorKind::Other, false),
            vec![EnergyTransferMode::DcExtended]
        );
        assert_eq!(
            supported_dc_transfer_modes(ConnectorKind::Other, true),
            vec![EnergyTransferMode::DcExtended, EnergyTransferMode::DcBpt]
        );
    }

    #[test]
    fn an_mcs_connector_replaces_the_dc_modes_rather_than_adding_to_them() {
        assert_eq!(
            supported_dc_transfer_modes(ConnectorKind::Mcs, false),
            vec![EnergyTransferMode::Mcs]
        );
        assert_eq!(
            supported_dc_transfer_modes(ConnectorKind::Mcs, true),
            vec![EnergyTransferMode::Mcs, EnergyTransferMode::McsBpt]
        );
    }

    #[test]
    fn only_the_mcs_connector_name_selects_the_mcs_modes() {
        // Every other `ConnectorTypeEnum` name, plus the unset and the invalid
        // cases, take the same branch. Listed rather than sampled because the
        // C++ optional makes "unset" and "unrecognized" behave as a third
        // spelling of the same answer.
        for raw in [
            "cCCS1",
            "cCCS2",
            "cG105",
            "cTesla",
            "cType1",
            "cType2",
            "s309_1P_16A",
            "s309_1P_32A",
            "s309_3P_16A",
            "s309_3P_32A",
            "sBS1361",
            "sCEE-7_7",
            "sType2",
            "sType3",
            "Other1PhMax16A",
            "Other1PhOver16A",
            "Other3Ph",
            "Pan",
            "wInductive",
            "wResonant",
            "Undetermined",
            "Unknown",
            "",
            "not a connector type at all",
        ] {
            assert_eq!(
                ConnectorKind::parse(raw),
                ConnectorKind::Other,
                "connector type {raw:?}"
            );
        }
        assert_eq!(ConnectorKind::parse("cMCS"), ConnectorKind::Mcs);
    }

    /// The two resets of the authorization waiting flags, each on its own.
    ///
    /// Driven here rather than through `Core`, because a control pilot sequence
    /// cannot reach a second arrival without a departure in between, so a core
    /// level test of the arrival is always satisfied by the departure that
    /// preceded it. The port has no such ordering, which is what makes the two
    /// separable at all.
    mod resetting_the_authorization_bridge {
        use super::*;

        fn a_token() -> OpaqueToken {
            OpaqueToken::new(json!({
                "id_token": {"value": "CONTRACT", "type": "eMAID"},
                "authorization_type": "PlugAndCharge",
            }))
        }

        fn contract_held() -> AuthorizationHeld {
            AuthorizationHeld {
                eim: false,
                pnc: true,
                charging: false,
            }
        }

        /// A port with a contract request outstanding.
        fn awaiting_a_contract() -> HlcPort {
            let mut port = port("AC", &[("ac_hlc_enabled", json!(true))]);
            port.on_require_auth_plug_and_charge(AuthorizationHeld::default(), a_token());
            port
        }

        #[test]
        fn an_outstanding_request_is_answered_when_nothing_has_reset_it() {
            // The control: without this the two tests below would pass against
            // a bridge that never answers anything.
            let mut port = awaiting_a_contract();

            assert!(
                !port.on_authorization_settled(contract_held()).is_empty(),
                "the request is outstanding, so the grant is its answer"
            );
        }

        #[test]
        fn a_vehicle_arriving_resets_the_bridge_with_no_departure_in_between() {
            // `EvseManager.cpp:1123-1130`, the primary reset and the
            // unconditional one.
            let mut port = awaiting_a_contract();

            port.on_plug_in();

            assert!(
                port.on_authorization_settled(contract_held()).is_empty(),
                "a new vehicle inherits nothing from the last one"
            );
        }

        #[test]
        fn a_vehicle_leaving_resets_the_bridge_on_its_own() {
            // `EvseManager.cpp:1099-1100`, inside the SLAC arm there and
            // ungated here; see `on_unplug`.
            let mut port = awaiting_a_contract();

            port.on_unplug();

            assert!(
                port.on_authorization_settled(contract_held()).is_empty(),
                "the vehicle that asked is gone"
            );
        }
    }

    /// `set_der_available` on an AC port, whose whole effect is to widen the
    /// advertised set with `AC_DER_IEC` (`evse/evse_managerImpl.cpp:571-574`
    /// into `recompute_and_publish_supported_ac_energy_transfers`,
    /// `EvseManager.cpp:1812-1819`).
    mod der_availability {
        use super::*;

        /// An AC port whose board reports an export capability, which is the
        /// other half of the `AC_DER_IEC` gate
        /// (`energy_transfer_modes.cpp:12-37`).
        fn an_export_capable_ac_port() -> HlcPort {
            let mut port = port("AC", &[("ac_hlc_enabled", json!(true))]);
            port.note_ac_capabilities(three_phase_caps());
            port
        }

        #[test]
        fn a_declared_der_widens_the_advertised_set_with_the_der_mode() {
            let mut port = an_export_capable_ac_port();
            assert!(!port.advertised().contains(&EnergyTransferMode::AcDerIec));

            let effects = port.set_der_available(true);

            assert!(port.advertised().contains(&EnergyTransferMode::AcDerIec));
            assert_eq!(
                effects,
                vec![
                    Effect::PublishSupportedTransferModes(port.advertised().to_vec()),
                    Effect::HlcUpdate(HlcUpdate::TransferModes(port.advertised().to_vec())),
                ]
            );
        }

        #[test]
        fn a_withdrawn_der_narrows_the_set_again() {
            let mut port = an_export_capable_ac_port();
            port.set_der_available(true);

            assert!(!port.set_der_available(false).is_empty());
            assert!(!port.advertised().contains(&EnergyTransferMode::AcDerIec));
        }

        #[test]
        fn a_repeated_declaration_republishes_nothing() {
            let mut port = an_export_capable_ac_port();
            assert!(!port.set_der_available(true).is_empty());
            assert!(port.set_der_available(true).is_empty());
        }

        /// A port whose board reports no export capability advertises no DER
        /// mode however the command is answered, so the declaration is stored
        /// and nothing is republished.
        #[test]
        fn a_port_that_cannot_export_advertises_no_der_mode() {
            let mut port = port("AC", &[("ac_hlc_enabled", json!(true))]);

            assert!(port.set_der_available(true).is_empty());
            assert!(!port.advertised().contains(&EnergyTransferMode::AcDerIec));
        }

        /// `evse/evse_managerImpl.cpp:572` gates the recompute on
        /// `charge_mode == "AC"`, so a DC port stores the declaration and
        /// republishes nothing. The DC derivation reads the supply's
        /// bidirectional capability and never the DER declaration, so there
        /// would be nothing for a recompute to change.
        #[test]
        fn a_dc_port_stores_the_declaration_and_republishes_nothing() {
            let mut port = port("DC", &[]);
            let before = port.advertised().to_vec();

            assert!(port.set_der_available(true).is_empty());

            assert_eq!(port.advertised(), before.as_slice());
        }
    }

    /// The AC power envelope reaches the stack from the board support handler
    /// and only for an AC port with high level communication
    /// (`EvseManager.cpp:281-288`).
    #[test]
    fn a_board_report_announces_the_ac_power_envelope() {
        let mut port = port(
            "AC",
            &[
                ("ac_hlc_enabled", json!(true)),
                ("ac_nominal_voltage", json!(230.0)),
            ],
        );
        let effects = port.note_ac_capabilities(three_phase_caps());
        assert_eq!(
            ac_envelope_commands(&effects),
            vec![
                "AcMaximumLimits".to_string(),
                "AcMinimumLimits".to_string(),
                "AcParameters".to_string()
            ],
            "all three, in the C++ order: {effects:?}"
        );
    }

    /// `:286` gates the advertised set on a change and `:287` gates the
    /// envelope on nothing, so a repeated report still re-announces the
    /// envelope alone.
    #[test]
    fn a_repeated_board_report_re_announces_the_envelope_but_not_the_advertised_set() {
        let mut port = port("AC", &[("ac_hlc_enabled", json!(true))]);
        port.note_ac_capabilities(three_phase_caps());
        let effects = port.note_ac_capabilities(three_phase_caps());
        assert!(
            transfer_modes_told_to_the_stack(&effects).is_empty(),
            "{effects:?}"
        );
        assert_eq!(ac_envelope_commands(&effects).len(), 3, "{effects:?}");
    }

    /// `:281`. A DC port hears the same board report and announces nothing:
    /// the whole block is inside the AC branch.
    #[test]
    fn a_dc_port_announces_no_ac_power_envelope() {
        let mut port = port("DC", &[]);
        let effects = port.note_ac_capabilities(three_phase_caps());
        assert!(ac_envelope_commands(&effects).is_empty(), "{effects:?}");
    }

    /// The branch checkbox six asks for, at the port: one limit change, two
    /// sessions, two different commands. The port level assertion is that the
    /// selected service it holds is what chooses between them, which is the
    /// part `ac_params` cannot answer for on its own.
    #[test]
    fn the_selected_service_chooses_which_limit_command_a_change_becomes() {
        let mut port = port(
            "AC",
            &[
                ("ac_hlc_enabled", json!(true)),
                ("ac_nominal_voltage", json!(230.0)),
            ],
        );
        port.note_ac_capabilities(three_phase_caps());

        assert_eq!(
            port.note_ac_current_limit(16.0),
            vec![Effect::HlcUpdate(HlcUpdate::AcMaxCurrent(16.0))],
            "no service selected yet"
        );

        port.note_selected_service(SelectedService::Ac);
        assert_eq!(
            port.note_ac_current_limit(16.0),
            vec![Effect::HlcUpdate(HlcUpdate::AcTargetPower(Power {
                total_w: 11_040.0,
                ..Power::default()
            }))],
            "the same change, told as a power"
        );
    }

    /// `:388`. A terminate ends the session and the selected service with it, so
    /// the next limit change falls back to the ISO 15118-2 spelling. A pause
    /// does not, because the session it belongs to can resume.
    #[test]
    fn a_data_link_terminate_forgets_the_selected_service_and_a_pause_keeps_it() {
        let mut port = port("AC", &[("ac_hlc_enabled", json!(true))]);
        port.note_ac_capabilities(three_phase_caps());
        port.note_selected_service(SelectedService::AcBpt);

        port.on_data_link(DataLinkRequest::Pause);
        assert!(
            matches!(
                port.note_ac_current_limit(16.0).as_slice(),
                [Effect::HlcUpdate(HlcUpdate::AcTargetPower(_))]
            ),
            "a pause keeps the service"
        );

        port.on_data_link(DataLinkRequest::Error);
        assert!(
            matches!(
                port.note_ac_current_limit(16.0).as_slice(),
                [Effect::HlcUpdate(HlcUpdate::AcTargetPower(_))]
            ),
            "an error keeps it too, the C++ clears it in the terminate arm alone"
        );

        port.on_data_link(DataLinkRequest::Terminate);
        assert_eq!(
            port.note_ac_current_limit(16.0),
            vec![Effect::HlcUpdate(HlcUpdate::AcMaxCurrent(16.0))],
            "a terminate forgets it"
        );
    }

    /// `:1163-1169`. The meter reading reaches the vehicle only once an
    /// ISO 15118-20 service has been selected.
    #[test]
    fn the_meter_present_power_waits_for_a_selected_service() {
        let mut port = port("AC", &[("ac_hlc_enabled", json!(true))]);
        let reading = Power {
            total_w: 3_300.0,
            ..Power::default()
        };

        assert!(port.note_ac_present_power(Some(reading)).is_empty());
        port.note_selected_service(SelectedService::Ac);
        assert_eq!(
            port.note_ac_present_power(Some(reading)),
            vec![Effect::HlcUpdate(HlcUpdate::AcPresentPower(reading))]
        );
    }

    /// Neither emission has a charge mode branch in the C++, and the difference
    /// is observable: a DC port running DIN 70121 or ISO 15118-2 selects no
    /// ISO 15118-20 service, so every limit change sends it an AC ampere count.
    /// Preserved rather than narrowed.
    #[test]
    fn a_dc_port_is_told_the_ac_ampere_count_too() {
        let port = port("DC", &[]);
        assert_eq!(
            port.note_ac_current_limit(200.0),
            vec![Effect::HlcUpdate(HlcUpdate::AcMaxCurrent(200.0))]
        );
    }

    /// The autocharge identity taken from SLAC (`EvseManager.cpp:179-183`).
    ///
    /// Two settings decide whether the fact becomes a token, and the C++ spends
    /// them at the subscription. They are pinned here because both are easy to
    /// invert and neither failure is visible: the wrong one publishes a token a
    /// deployment never asked for, and the other publishes none where it did.
    mod autocharge_from_slac {
        use super::*;

        fn slac_port(enable_autocharge: bool, from_slac: bool) -> HlcPort {
            port(
                "DC",
                &[
                    ("enable_autocharge", json!(enable_autocharge)),
                    ("autocharge_use_slac_instead_of_hlc", json!(from_slac)),
                    // Distinctive, so the assertion below proves the token
                    // carries `config.connector_id` and not some other index
                    // that happens to be zero too.
                    ("connector_id", json!(3)),
                ],
            )
        }

        fn offered(effects: &[Effect]) -> Option<ProvidedToken> {
            effects.iter().find_map(|effect| match effect {
                Effect::PublishProvidedToken(token) => Some(token.clone()),
                _ => None,
            })
        }

        /// Both settings on, which is the only combination that publishes. The
        /// identity is the MAC address with its colons removed behind the
        /// `VID:` prefix, and the connector list is this EVSE's.
        #[test]
        fn a_mac_address_becomes_an_autocharge_token() {
            let mut port = slac_port(true, true);
            assert_eq!(
                offered(&port.note_vehicle_mac_address("AA:BB:CC:DD:EE:FF")),
                Some(ProvidedToken::Autocharge {
                    id_token: "VID:AABBCCDDEEFF".to_owned(),
                    connectors: vec![3],
                })
            );
        }

        /// The `enable_autocharge` half. Without it the C++ installs no
        /// subscription at all, so nothing is offered.
        #[test]
        fn a_port_with_autocharge_disabled_offers_nothing() {
            let mut port = slac_port(false, true);
            assert!(port
                .note_vehicle_mac_address("AA:BB:CC:DD:EE:FF")
                .is_empty());
        }

        /// The `autocharge_use_slac_instead_of_hlc` half. With it off the
        /// identity is the stack's to report and this arm must stay silent, or
        /// a port would offer two tokens for one vehicle.
        #[test]
        fn a_port_taking_the_identity_from_the_stack_offers_nothing() {
            let mut port = slac_port(true, false);
            assert!(port
                .note_vehicle_mac_address("AA:BB:CC:DD:EE:FF")
                .is_empty());
        }

        #[test]
        fn a_port_with_both_settings_off_offers_nothing() {
            let mut port = slac_port(false, false);
            assert!(port
                .note_vehicle_mac_address("AA:BB:CC:DD:EE:FF")
                .is_empty());
        }

        /// A MAC address already without separators is the same identity, so
        /// the two spellings cannot authorize as two different vehicles.
        #[test]
        fn a_mac_address_without_colons_is_the_same_identity() {
            let mut port = slac_port(true, true);
            assert_eq!(
                offered(&port.note_vehicle_mac_address("AABBCCDDEEFF")),
                offered(&port.note_vehicle_mac_address("AA:BB:CC:DD:EE:FF"))
            );
        }

        /// The SLAC arm publishes and does not remember, which is what the C++
        /// does: its `autocharge_token` member stays value initialized under
        /// this setting, and the external identification handler says so. If
        /// this ever starts remembering, that handler silently changes too.
        #[test]
        fn the_slac_identity_is_not_remembered_for_the_eim_handler() {
            let mut port = slac_port(true, true);
            port.note_vehicle_mac_address("AA:BB:CC:DD:EE:FF");

            let held = AuthorizationHeld::default();
            assert!(offered(&port.on_require_auth_eim(held)).is_none());
        }

        /// And the two sources stay exclusive from the other side: under this
        /// setting the stack's own report is ignored, so only one of the two
        /// ever feeds the identity.
        #[test]
        fn the_stacks_report_is_ignored_under_this_setting() {
            let mut port = slac_port(true, true);
            port.note_vehicle_identity("11:22:33:44:55:66");

            let held = AuthorizationHeld::default();
            assert!(offered(&port.on_require_auth_eim(held)).is_none());
        }
    }
}
