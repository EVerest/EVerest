// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

//! Typed configuration: turns the raw manifest config into a `Settings`
//! struct, and `Settings` plus live wiring facts into the `PowerPath`
//! implementation `Core` runs.
//!
//! Six of the C++ manifest's keys ask for behaviour this module does not
//! implement: five workarounds, `hack_skoda_enyaq`,
//! `hack_present_current_offset`, `hack_pause_imd_during_precharge`,
//! `hack_fix_hlc_integer_current_requests` and
//! `hack_simplified_mode_limit_10A`, and one whole feature,
//! `enable_nodered_interface`, which is an MQTT interface of arbitrary topics
//! that `everestrs` has no binding to publish on. All six are **declared**
//! here at their C++ defaults and accepted at those defaults, because every
//! one of them defaults to inert: a config that sets one to `false` or to a
//! zero offset asks for the behaviour this module already has, and refusing it
//! would stop a deployment substituting this module over a setting that
//! changes nothing. A value that asks for the behaviour is a named error at
//! startup instead of the setting silently vanishing; see
//! `reject_requested_workarounds`.
//!
//! They were absent from the manifest and refused on their names, which could
//! not tell the two cases apart: the framework reports an **undeclared** key
//! without its value.
//!
//! `ac_with_soc` was the sixth. It is accepted now: `path::ac_with_soc`
//! implements it as a power path of its own, so the mode flip it needs no
//! longer asks for a mutable `charge_mode`.

use std::collections::HashMap;
use std::sync::Arc;
use std::time::Duration;

use anyhow::bail;
use serde_json::Value;

use super::hlc::{ConnectorKind, HlcConfig, HlcPort};
use super::path::ac::{AcBasic, AcHlc};
use super::path::ac_with_soc::AcWithSoc;
use super::path::dc::{
    CableCheckOptions, Dc, DcConfig, IsolationMonitor, OverVoltageMonitor,
};
use super::path::iec::IecConfig;
use super::path::PowerPath;
use super::session::{Limits, PwmStart, Session};

/// Raw config values as handed down by the module framework, keyed by name.
pub type RawConfig = HashMap<String, Value>;

/// Not exposed in the manifest: IEC 61851-1 minimum current.
/// `shared_context.max_current = 6.0` at `Charger.cpp:45`, which
/// `Charger::main_thread` publishes at `:103` before its loop begins.
const INITIAL_CURRENT_LIMIT_A: f64 = 6.0;

/// What the charger may *offer* before the first grant, which is nothing.
///
/// `EvseManager::ready_to_start_charging` says so outright: its comment reads
/// "start with a limit of 0 amps. We will get a budget from EnergyManager" and
/// it calls `set_max_current(0.0, now + 120s)` before it starts the state
/// machine (`EvseManager.cpp:1513-1515`). So the 6 A the constructor stored is
/// overwritten before anything can run on it, and a session waits for the
/// energy manager rather than charging on it (`Charger.cpp:354-355`).
///
/// Not modelled: that call also gives the zero a two minute validity, where
/// this port's zero is simply the value it starts with. Nothing reads the
/// difference - a zero is unavailable either way, and the first grant replaces
/// it with its own deadline.
const INITIAL_OFFERABLE_CURRENT_A: f64 = 0.0;

/// Not exposed in the manifest: number of phases assumed available before the
/// board support reports its own count.
const INITIAL_PHASES_AVAILABLE: i64 = 3;

/// Not exposed in the manifest: current cap during cable check, chosen so the
/// short circuit test keeps resistance within range.
const CABLE_CHECK_CURRENT_LIMIT_A: f64 = 2.0;

const UNSUPPORTED_KEYS: &[&str] = &[
    "hack_skoda_enyaq",
    "hack_present_current_offset",
    "hack_pause_imd_during_precharge",
    "hack_fix_hlc_integer_current_requests",
    "hack_simplified_mode_limit_10A",
    // Not a workaround but the same shape: a feature this module cannot
    // provide at all. `everestrs` publishes only on declared interfaces and
    // has no binding that reaches an arbitrary MQTT topic, and the Node-RED
    // interface is entirely topics below `everest_external/nodered/`. So the
    // key is declared at the C++ default, accepted there, and refused when it
    // is switched on, exactly as the five above are.
    "enable_nodered_interface",
];

/// Refuses behaviour this module does not implement, wherever the config
/// file asks for it.
///
/// The six keys are **declared** in the manifest at their C++ defaults, and
/// that is what makes an existing config substitutable: every one of them
/// defaults to inert, so a config that sets one to `false` or to a zero offset
/// asks for the behaviour this module already has. Refusing on the key's
/// presence, as this did, stopped such a deployment from starting the module
/// over a setting that changes nothing - and the value could not be consulted
/// then, because the framework reports an **undeclared** key without it.
///
/// A value that asks for something is still a refusal to start rather than a
/// warning, because the behaviour it asks for would silently not happen.
///
/// The keys are looked for in the module's own config group and in the config
/// group of each interface this module provides: a key parked in a
/// `config_implementation` block would otherwise be the silent no-op this
/// exists to prevent. The error names the group for anything outside the
/// module's own, because that is where the operator has to go.
pub fn reject_requested_workarounds<'a>(
    keys: impl Iterator<Item = SuppliedKey<'a>>,
) -> anyhow::Result<()> {
    let offending: Vec<String> = keys
        .filter(|key| UNSUPPORTED_KEYS.contains(&key.name) && key.asks_for_something())
        .map(|key| match key.implementation {
            None => key.name.to_owned(),
            Some(implementation) => {
                format!("{} (under implementation {})", key.name, implementation)
            }
        })
        .collect();
    if offending.is_empty() {
        return Ok(());
    }
    bail!(
        "config key(s) {} ask for behaviour RsEvseManager does not implement; \
         each is accepted at its default, which is what the C++ module also does \
         with it unless it is switched on",
        offending.join(", ")
    );
}

/// A key the config supplied, with enough of its value to tell an inert
/// setting from a request.
///
/// `PartialEq` and not `Eq`, because `serde_json::Value` carries a float.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct SuppliedKey<'a> {
    /// The provided implementation whose config group carried the key, or
    /// `None` for the module's own config group.
    pub implementation: Option<&'a str>,
    /// The key's name.
    pub name: &'a str,
    /// The value, or `None` where the framework reported the key without one.
    /// An undeclared key arrives that way, and a key with no value cannot be
    /// asking for anything this module would have to implement: the manifest
    /// declares all six, so an undeclared spelling of one is a different key.
    pub value: Option<&'a Value>,
}

impl SuppliedKey<'_> {
    /// Whether this value asks for the behaviour rather than for the inert
    /// default. Every one of the six is a boolean but one, and that one is an
    /// offset in amperes whose inert value is zero.
    fn asks_for_something(&self) -> bool {
        match self.value {
            None => false,
            Some(Value::Bool(enabled)) => *enabled,
            Some(Value::Number(offset)) => offset.as_f64().is_some_and(|value| value != 0.0),
            // A value of a shape the manifest does not declare is not this
            // check's to judge: the framework validates against the manifest
            // before this runs.
            Some(_) => false,
        }
    }
}

fn get_bool(raw: &RawConfig, key: &str, default: bool) -> bool {
    raw.get(key).and_then(Value::as_bool).unwrap_or(default)
}

fn get_i64(raw: &RawConfig, key: &str, default: i64) -> i64 {
    raw.get(key).and_then(Value::as_i64).unwrap_or(default)
}

fn get_f64(raw: &RawConfig, key: &str, default: f64) -> f64 {
    raw.get(key).and_then(Value::as_f64).unwrap_or(default)
}

fn get_str(raw: &RawConfig, key: &str, default: &str) -> String {
    raw.get(key)
        .and_then(Value::as_str)
        .map(str::to_owned)
        .unwrap_or_else(|| default.to_owned())
}

macro_rules! string_enum {
    ($name:ident { $($variant:ident => $wire:literal),+ $(,)? }, $config_key:literal) => {
        #[derive(Clone, Copy, Debug, Eq, PartialEq)]
        pub enum $name {
            $($variant),+
        }

        impl $name {
            fn parse(raw: &str) -> anyhow::Result<Self> {
                match raw {
                    $($wire => Ok($name::$variant),)+
                    other => bail!(
                        "unsupported {} {:?}: expected one of [{}]",
                        $config_key,
                        other,
                        [$($wire),+].join(", "),
                    ),
                }
            }
        }
    };
}

string_enum!(ChargeMode { Ac => "AC", Dc => "DC" }, "charge_mode");
string_enum!(
    SessionIdType {
        Uuid => "UUID",
        UuidBase64 => "UUID_BASE64",
        ShortBase64 => "SHORT_BASE64",
    },
    "session_id_type"
);
// The pilot state a phase switch signals. Three values, because the C++
// manifest declares three and the framework validates a config against the
// module's own enum: the port declared two, so a deployment configured with
// `E` could not start the module at all.
string_enum!(
    SwitchCpState {
        X1 => "X1",
        E => "E",
        F => "F",
    },
    "switch_3ph1ph_cp_state"
);

impl SwitchCpState {
    /// Whether the switch signals control pilot state F rather than X1.
    ///
    /// The one question the C++ asks of this setting:
    /// `switch_3ph1ph_cp_state_F = (_switch_3ph1ph_cp_state == "F")`
    /// (`Charger.cpp:1571`). So `E` signals X1, as every value but `F` does
    /// there, and it is accepted rather than implemented.
    pub fn signals_state_f(self) -> bool {
        self == SwitchCpState::F
    }
}
string_enum!(
    ReinitMethod {
        CpStateE => "CPStateE",
        CpStateF => "CPStateF",
        CpStateX1 => "CPStateX1",
    },
    "reinit_method"
);
string_enum!(SaeBptMode { V2h => "V2H", V2g => "V2G" }, "sae_j2847_2_bpt_mode");
string_enum!(
    BptChannel {
        None => "None",
        Unified => "Unified",
        Separated => "Separated",
    },
    "bpt_channel"
);
string_enum!(
    BptGeneratorMode {
        None => "None",
        GridFollowing => "GridFollowing",
        GridForming => "GridForming",
    },
    "bpt_generator_mode"
);
string_enum!(
    BptIslandMethod {
        None => "None",
        Active => "Active",
        Passive => "Passive",
    },
    "bpt_grid_code_island_method"
);

#[derive(Clone, Debug, PartialEq)]
pub struct AcSettings {
    pub nominal_voltage_v: f64,
    pub max_reactive_power_var: f64,
    pub hlc_enabled: bool,
    pub hlc_use_5percent: bool,
    pub enforce_hlc: bool,
    pub has_ventilation: bool,
    pub lock_connector_in_state_b: bool,
    pub allow_bpt_with_iso2: bool,
    pub supported_iso_ac_bpt: bool,
    pub switch_3ph1ph_delay_s: i64,
    pub switch_3ph1ph_cp_state: SwitchCpState,
    pub state_f_after_fault_ms: i64,
    pub soft_over_current_tolerance_percent: f64,
    pub soft_over_current_measurement_noise_a: f64,
    pub soft_over_current_timeout_ms: i64,
    pub sleep_before_enabling_pwm_hlc_mode_ms: i64,
    pub sae_bpt_enabled: bool,
    pub sae_bpt_mode: SaeBptMode,
    /// `ac_with_soc`. AC hardware that presents itself as DC until the vehicle
    /// reports a state of charge, then runs as basic AC. It selects
    /// `path::ac_with_soc::AcWithSoc` and is read nowhere else.
    pub with_soc: bool,
    /// `reinit_method`, the pilot level a reinitialization holds.
    pub reinit_method: ReinitMethod,
    /// `reinit_duration_ms`, how long it is held.
    pub reinit_duration_ms: i64,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct DcSettings {
    pub isolation_voltage_v: i64,
    pub relays_open_voltage_v: i64,
    pub relays_closed_timeout_s: i64,
    pub wait_number_of_imd_measurements: i64,
    pub enable_imd_self_test: bool,
    pub enable_imd_self_test_relays_open: bool,
    pub wait_below_60v_before_finish: bool,
    pub ramp_ampere_per_second: i64,
    pub hlc_charge_loop_without_energy_timeout_s: i64,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct PaymentSettings {
    pub enable_eim: bool,
    pub enable_contract: bool,
    pub disable_authentication: bool,
    pub central_contract_validation_allowed: bool,
    pub contract_certificate_installation_enabled: bool,
    pub ev_receipt_required: bool,
    pub enable_autocharge: bool,
    pub autocharge_use_slac_instead_of_hlc: bool,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct BptSettings {
    pub channel: BptChannel,
    pub generator_mode: BptGeneratorMode,
    pub grid_code_island_method: BptIslandMethod,
}

#[derive(Clone, Debug, PartialEq)]
pub struct LoggingSettings {
    pub session_logging: bool,
    pub session_logging_path: String,
    pub session_logging_xml: bool,
    pub logfile_suffix: String,
    pub dbg_hlc_auth_after_tstep: bool,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct TimingSettings {
    pub initial_meter_value_timeout_ms: i64,
    pub internal_over_voltage_duration_ms: i64,
    pub uk_random_delay_enable: bool,
    pub uk_random_delay_max_duration_s: i64,
    pub uk_random_delay_at_any_change: bool,
    pub voltage_plausibility_max_spread_threshold_v: f64,
    pub voltage_plausibility_fault_duration_ms: i64,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct MiscSettings {
    /// `phase_rotation_grid_side`. It reaches only
    /// `energy_flow_request.energy_usage_root`, which is where the C++ applies
    /// it (`energy_grid/energyImpl.cpp:56-58`) and the only place it is read.
    pub phase_rotation_grid_side: PhaseRotation,
    pub request_zero_power_in_idle: bool,
    pub external_ready_to_start_charging: bool,
    pub raise_mrec9: bool,
    pub unlock_when_deauthorized: bool,
    pub inoperative_error_use_vendor_id: bool,
    pub fail_on_powermeter_errors: bool,
    pub zero_power_ignore_pause: bool,
    pub zero_power_allow_ev_to_ignore_pause: bool,
}

/// `phase_rotation_grid_side`, which corrects for a physical L1/L2/L3 wiring
/// rotation at the grid side meter.
///
/// `everest::helpers::PhaseRotation`, named as OCPP names it: each letter is
/// which grid phase the meter's L1, L2 and L3 actually are. `RST` is no
/// rotation.
///
/// Read from a string rather than matched exhaustively on the wire, because
/// `everest::helpers::phase_rotation_from_string` answers `RST` for any value
/// it does not know and the manifest's enum is what refuses the rest.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub enum PhaseRotation {
    /// Reported L1/L2/L3 already match grid L1/L2/L3.
    #[default]
    Rst,
    /// Reported L1 is grid L2, reported L2 is grid L3, reported L3 is grid L1.
    Str,
    /// Reported L1 is grid L3, reported L2 is grid L1, reported L3 is grid L2.
    Trs,
}

impl PhaseRotation {
    /// `everest::helpers::phase_rotation_from_string`, including its fallback:
    /// anything but the two named rotations is no rotation.
    pub fn from_wire(rotation: &str) -> Self {
        match rotation {
            "TRS" => Self::Trs,
            "STR" => Self::Str,
            _ => Self::Rst,
        }
    }
}

/// The framework three-tier mapping this instance runs under, taken from
/// `mapping: module:` in the config rather than from `config_module`, so it is
/// passed alongside the raw config instead of being read out of it.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Mapping {
    pub evse: i64,
    pub connectors: Vec<i64>,
}

/// The whole manifest, typed and grouped. One field or nested struct per
/// config key; see the module doc comment for the six keys accepted at their
/// defaults and refused when they ask for something, and for the sixth that
/// used to be among them.
#[derive(Clone, Debug, PartialEq)]
pub struct Settings {
    /// Which EVSE of the charging station this instance is, from the framework
    /// three-tier mapping.
    pub evse_index: i64,
    /// The connector indices the three-tier mapping assigns to this instance.
    /// The module implements exactly one connector, which `resolve`
    /// enforces.
    pub mapped_connectors: Vec<i64>,
    /// The `connector_id` config key, which is not a connector index despite
    /// its name: it labels the Node-RED topics `everest_external/nodered/{}/..`
    /// and is reported as the EVSE id, while the session events the C++ emits
    /// hardcode connector 1.
    pub legacy_topic_id: i64,
    pub evse_id: String,
    pub evse_id_din: String,
    pub connector_type: String,
    pub charge_mode: ChargeMode,
    pub session_id_type: SessionIdType,
    pub ac: AcSettings,
    pub dc: DcSettings,
    pub payment: PaymentSettings,
    pub bpt: BptSettings,
    pub logging: LoggingSettings,
    pub timing: TimingSettings,
    pub misc: MiscSettings,
}

impl Settings {
    pub fn from_raw(raw: &RawConfig, mapping: &Mapping) -> anyhow::Result<Self> {
        reject_requested_workarounds(raw.iter().map(|(name, value)| SuppliedKey {
            implementation: None,
            name,
            value: Some(value),
        }))?;

        let charge_mode = ChargeMode::parse(&get_str(raw, "charge_mode", "AC"))?;
        let session_id_type = SessionIdType::parse(&get_str(raw, "session_id_type", "UUID"))?;
        let switch_3ph1ph_cp_state =
            SwitchCpState::parse(&get_str(raw, "switch_3ph1ph_cp_state", "X1"))?;
        let sae_bpt_mode = SaeBptMode::parse(&get_str(raw, "sae_j2847_2_bpt_mode", "V2G"))?;
        let reinit_method = ReinitMethod::parse(&get_str(raw, "reinit_method", "CPStateF"))?;
        let bpt_channel = BptChannel::parse(&get_str(raw, "bpt_channel", "None"))?;
        let bpt_generator_mode =
            BptGeneratorMode::parse(&get_str(raw, "bpt_generator_mode", "None"))?;
        let bpt_grid_code_island_method =
            BptIslandMethod::parse(&get_str(raw, "bpt_grid_code_island_method", "None"))?;

        Ok(Self {
            evse_index: mapping.evse,
            mapped_connectors: mapping.connectors.clone(),
            legacy_topic_id: get_i64(raw, "connector_id", 0),
            evse_id: get_str(raw, "evse_id", "DE*PNX*E1234567*1"),
            evse_id_din: get_str(raw, "evse_id_din", "49A80737A45678"),
            connector_type: get_str(raw, "connector_type", "Unknown"),
            charge_mode,
            session_id_type,
            ac: AcSettings {
                nominal_voltage_v: get_f64(raw, "ac_nominal_voltage", 230.0),
                max_reactive_power_var: get_f64(raw, "ac_max_reactive_power", 0.0),
                hlc_enabled: get_bool(raw, "ac_hlc_enabled", false),
                hlc_use_5percent: get_bool(raw, "ac_hlc_use_5percent", true),
                enforce_hlc: get_bool(raw, "ac_enforce_hlc", false),
                has_ventilation: get_bool(raw, "has_ventilation", true),
                lock_connector_in_state_b: get_bool(raw, "lock_connector_in_state_b", true),
                allow_bpt_with_iso2: get_bool(raw, "hack_allow_bpt_with_iso2", false),
                supported_iso_ac_bpt: get_bool(raw, "supported_iso_ac_bpt", false),
                switch_3ph1ph_delay_s: get_i64(raw, "switch_3ph1ph_delay_s", 10),
                switch_3ph1ph_cp_state,
                state_f_after_fault_ms: get_i64(raw, "state_F_after_fault_ms", 300),
                soft_over_current_tolerance_percent: get_f64(
                    raw,
                    "soft_over_current_tolerance_percent",
                    10.0,
                ),
                soft_over_current_measurement_noise_a: get_f64(
                    raw,
                    "soft_over_current_measurement_noise_A",
                    0.5,
                ),
                soft_over_current_timeout_ms: get_i64(raw, "soft_over_current_timeout_ms", 7000),
                sleep_before_enabling_pwm_hlc_mode_ms: get_i64(
                    raw,
                    "sleep_before_enabling_pwm_hlc_mode_ms",
                    500,
                ),
                sae_bpt_enabled: get_bool(raw, "sae_j2847_2_bpt_enabled", false),
                sae_bpt_mode,
                with_soc: get_bool(raw, "ac_with_soc", false),
                reinit_method,
                reinit_duration_ms: get_i64(raw, "reinit_duration_ms", 3000),
            },
            dc: DcSettings {
                isolation_voltage_v: get_i64(raw, "dc_isolation_voltage_V", 0),
                relays_open_voltage_v: get_i64(raw, "cable_check_relays_open_voltage_V", 48),
                relays_closed_timeout_s: get_i64(raw, "cable_check_relays_closed_timeout_s", 5),
                wait_number_of_imd_measurements: get_i64(
                    raw,
                    "cable_check_wait_number_of_imd_measurements",
                    1,
                ),
                enable_imd_self_test: get_bool(raw, "cable_check_enable_imd_self_test", true),
                enable_imd_self_test_relays_open: get_bool(
                    raw,
                    "cable_check_enable_imd_self_test_relays_open",
                    false,
                ),
                wait_below_60v_before_finish: get_bool(
                    raw,
                    "cable_check_wait_below_60V_before_finish",
                    true,
                ),
                ramp_ampere_per_second: get_i64(raw, "dc_ramp_ampere_per_second", 25),
                hlc_charge_loop_without_energy_timeout_s: get_i64(
                    raw,
                    "hlc_charge_loop_without_energy_timeout_s",
                    5,
                ),
            },
            payment: PaymentSettings {
                enable_eim: get_bool(raw, "payment_enable_eim", true),
                enable_contract: get_bool(raw, "payment_enable_contract", true),
                disable_authentication: get_bool(raw, "disable_authentication", false),
                central_contract_validation_allowed: get_bool(
                    raw,
                    "central_contract_validation_allowed",
                    false,
                ),
                contract_certificate_installation_enabled: get_bool(
                    raw,
                    "contract_certificate_installation_enabled",
                    true,
                ),
                ev_receipt_required: get_bool(raw, "ev_receipt_required", false),
                enable_autocharge: get_bool(raw, "enable_autocharge", false),
                autocharge_use_slac_instead_of_hlc: get_bool(
                    raw,
                    "autocharge_use_slac_instead_of_hlc",
                    false,
                ),
            },
            bpt: BptSettings {
                channel: bpt_channel,
                generator_mode: bpt_generator_mode,
                grid_code_island_method: bpt_grid_code_island_method,
            },
            logging: LoggingSettings {
                session_logging: get_bool(raw, "session_logging", false),
                session_logging_path: get_str(raw, "session_logging_path", "/tmp"),
                session_logging_xml: get_bool(raw, "session_logging_xml", true),
                logfile_suffix: get_str(raw, "logfile_suffix", "session_uuid"),
                dbg_hlc_auth_after_tstep: get_bool(raw, "dbg_hlc_auth_after_tstep", false),
            },
            timing: TimingSettings {
                initial_meter_value_timeout_ms: get_i64(
                    raw,
                    "initial_meter_value_timeout_ms",
                    5000,
                ),
                internal_over_voltage_duration_ms: get_i64(
                    raw,
                    "internal_over_voltage_duration_ms",
                    400,
                ),
                uk_random_delay_enable: get_bool(
                    raw,
                    "uk_smartcharging_random_delay_enable",
                    false,
                ),
                uk_random_delay_max_duration_s: get_i64(
                    raw,
                    "uk_smartcharging_random_delay_max_duration",
                    600,
                ),
                uk_random_delay_at_any_change: get_bool(
                    raw,
                    "uk_smartcharging_random_delay_at_any_change",
                    true,
                ),
                voltage_plausibility_max_spread_threshold_v: get_f64(
                    raw,
                    "voltage_plausibility_max_spread_threshold_V",
                    50.0,
                ),
                voltage_plausibility_fault_duration_ms: get_i64(
                    raw,
                    "voltage_plausibility_fault_duration_ms",
                    30000,
                ),
            },
            misc: MiscSettings {
                phase_rotation_grid_side: PhaseRotation::from_wire(&get_str(
                    raw,
                    "phase_rotation_grid_side",
                    "RST",
                )),
                request_zero_power_in_idle: get_bool(raw, "request_zero_power_in_idle", false),
                external_ready_to_start_charging: get_bool(
                    raw,
                    "external_ready_to_start_charging",
                    false,
                ),
                raise_mrec9: get_bool(raw, "raise_mrec9", false),
                unlock_when_deauthorized: get_bool(raw, "unlock_when_deauthorized", false),
                inoperative_error_use_vendor_id: get_bool(
                    raw,
                    "inoperative_error_use_vendor_id",
                    false,
                ),
                fail_on_powermeter_errors: get_bool(raw, "fail_on_powermeter_errors", true),
                zero_power_ignore_pause: get_bool(raw, "zero_power_ignore_pause", true),
                zero_power_allow_ev_to_ignore_pause: get_bool(
                    raw,
                    "zero_power_allow_ev_to_ignore_pause",
                    false,
                ),
            },
        })
    }

    fn iec_config(&self) -> IecConfig {
        IecConfig {
            initial_current_limit_a: INITIAL_OFFERABLE_CURRENT_A,
            has_ventilation: self.ac.has_ventilation,
            lock_connector_in_state_b: self.ac.lock_connector_in_state_b,
            switch_phases_cp_state: self.ac.switch_3ph1ph_cp_state,
            switch_phases_delay: Duration::from_secs(self.ac.switch_3ph1ph_delay_s.max(0) as u64),
            reinit_method: self.ac.reinit_method,
            reinit_duration: Duration::from_millis(self.ac.reinit_duration_ms.max(0) as u64),
            // Parsed into `DcSettings` because the C++ key is documented for
            // DC, but the arm that reads it is the shared `Charging` arm and it
            // fires for any high level session, AC included
            // (`Charger.cpp:837`).
            hlc_no_energy_timeout: Duration::from_secs(
                self.dc.hlc_charge_loop_without_energy_timeout_s.max(0) as u64,
            ),
            // `Charger.cpp:46` and `:2051` compare against this one connector
            // value, and the manifest spells it exactly this way.
            type2_socket: self.connector_type == "IEC62196Type2Socket",
        }
    }

    /// The three optional cable check steps, for the isolation monitor that
    /// runs them.
    ///
    /// They are not on `DcConfig`: they are read only inside a cable check
    /// sequence, which runs only where a monitor is wired, so they belong to
    /// `IsolationMonitor` rather than to the path. All three parsed into
    /// `DcSettings` and reached nothing at all until this existed.
    fn cable_check_options(&self) -> CableCheckOptions {
        CableCheckOptions {
            imd_self_test: self.dc.enable_imd_self_test,
            imd_self_test_relays_open: self.dc.enable_imd_self_test_relays_open,
            wait_below_60v_before_finish: self.dc.wait_below_60v_before_finish,
        }
    }

    fn dc_config(&self) -> DcConfig {
        DcConfig {
            isolation_voltage_v: self.dc.isolation_voltage_v as f64,
            relays_open_voltage_v: self.dc.relays_open_voltage_v as f64,
            relays_closed_timeout: Duration::from_secs(
                self.dc.relays_closed_timeout_s.max(0) as u64
            ),
            imd_measurements: self
                .dc
                .wait_number_of_imd_measurements
                .clamp(0, u8::MAX as i64) as u8,
            ramp_ampere_per_second: self.dc.ramp_ampere_per_second as f64,
            cable_check_current_limit_a: CABLE_CHECK_CURRENT_LIMIT_A,
            connector: ConnectorKind::parse(&self.connector_type),
            internal_over_voltage_duration: Duration::from_millis(
                self.timing.internal_over_voltage_duration_ms.max(0) as u64,
            ),
            plausibility_max_spread_v: self.timing.voltage_plausibility_max_spread_threshold_v,
            plausibility_fault_duration: Duration::from_millis(
                self.timing.voltage_plausibility_fault_duration_ms.max(0) as u64,
            ),
            no_energy_timeout: Duration::from_secs(
                self.dc.hlc_charge_loop_without_energy_timeout_s.max(0) as u64,
            ),
        }
    }

    /// The session the module starts with. Both the current limit and the PWM
    /// start derivation live here so nothing outside restates them.
    pub fn initial_session(&self) -> Session {
        Session::new(
            self.pwm_start(),
            Limits {
                max_current_a: INITIAL_CURRENT_LIMIT_A,
                nr_of_phases_available: INITIAL_PHASES_AVAILABLE,
            },
        )
    }

    fn pwm_start(&self) -> PwmStart {
        if self.ac.enforce_hlc {
            PwmStart::FivePercentEnforced
        } else if self.ac.hlc_use_5percent {
            PwmStart::FivePercent
        } else {
            PwmStart::Nominal
        }
    }
}

/// What is actually connected, as opposed to what config asks for. `resolve`
/// needs both: DC and AC-with-HLC are unsound without their wiring in place.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Wiring {
    pub hlc: bool,
    pub slac: bool,
    pub powersupply_dc: bool,
    pub imd: bool,
    pub over_voltage_monitor: bool,
    /// Whether a car side powermeter is connected. `resolve` does not read
    /// it; the billing meter preference does, and that preference is a fact
    /// about what is wired rather than about what config asks for.
    pub powermeter_car_side: bool,
    /// Whether a connector lock is connected. `resolve` does not read it:
    /// a fixed cable port is sound. `force_unlock` answers with it, because
    /// the command's return value reports whether there is a lock to open at
    /// all (`evse_managerImpl::handle_force_unlock`).
    pub connector_lock: bool,
}

/// `hlc_enabled` in the C++ (`EvseManager.cpp:174-187`), in one place.
///
/// The C++ derives it in two steps: SLAC is disabled unless the deployment
/// asks for high level communication or runs DC (`:174-176`), and high level
/// communication is then disabled unless both the requirement is wired and
/// SLAC survived (`:185-187`). Collapsed here, because both steps read the
/// same three facts.
///
/// **It has exactly one consumer, and the consumer is a constructor.**
/// `HlcConfig::for_deployment` calls it and answers `None` where it is false,
/// so an `HlcConfig` that exists was derived enabled and the presence of the
/// port built from it is this predicate's answer rather than a second copy of
/// it. `resolve` reads the port's presence and never this function, which is
/// what keeps a path that speaks ISO 15118 from being paired with a port that
/// thinks it is disabled. `PowerPath::requires_hlc()` was that second copy and
/// is deleted.
pub fn hlc_enabled(settings: &Settings, wiring: &Wiring) -> bool {
    wiring.hlc
        && wiring.slac
        && match settings.charge_mode {
            ChargeMode::Dc => true,
            // `EvseManager::init`'s `slac_enabled` derivation keeps SLAC for an `ac_with_soc` port
            // whether or not `ac_hlc_enabled` is set, and it has to: the mode
            // gets its state of charge out of an ISO 15118 DC handshake, so a
            // port with the stack disabled could never flip at all.
            ChargeMode::Ac => settings.ac.hlc_enabled || settings.ac.with_soc,
        }
}

/// The collaborators this deployment actually has.
///
/// One value rather than two returns, because the pairing is the invariant:
/// the power path and the high level communication port are chosen in one
/// `match` in `resolve`, so `AcBasic` is produced only in the arm that has no
/// `HlcConfig` and `AcHlc` only in an arm that has one. Nothing outside can
/// pair them differently, and there is no predicate left to ask a second time.
///
/// `PowerPath` used to carry a `requires_hlc()` method saying the same thing
/// in a `bool`. It was deleted with this type: two answers to one question is
/// the state divergence that replaces an unreachable-code problem with a
/// worse one, and the answer that survives is the one that cannot be wrong,
/// which is whether the port is there.
pub struct Deployment {
    pub path: Box<dyn PowerPath>,
    /// `None` on an AC deployment with no ISO 15118 stack. Every other mode
    /// is refused without one; see `resolve`.
    pub hlc: Option<HlcPort>,
}

impl Deployment {
    /// The configuration handle for the boundary's two synchronous command
    /// answers, derived from the port rather than resolved again beside it.
    pub fn hlc_config(&self) -> Option<Arc<HlcConfig>> {
        self.hlc.as_ref().map(|port| Arc::clone(port.config()))
    }
}

/// Picks and constructs the collaborators this instance runs for its whole
/// lifetime. DC is unsound without high level communication, SLAC and a DC
/// power supply, so it is rejected outright rather than silently degraded to
/// something else.
pub fn resolve(settings: &Settings, wiring: &Wiring) -> anyhow::Result<Deployment> {
    if settings.mapped_connectors.len() != 1 {
        bail!(
            "RsEvseManager serves exactly one connector, but the three-tier mapping gives \
             evse {} the connectors {:?}",
            settings.evse_index,
            settings.mapped_connectors
        );
    }

    // The one derivation of `hlc_enabled`, and its one consumer. Everything
    // below reads the port's presence instead, so no arm restates the
    // predicate and no arm can pair a path with a port that disagrees with it.
    let hlc =
        HlcConfig::for_deployment(settings, wiring).map(|c| HlcPort::new(Arc::new(c), settings.charge_mode));

    match (settings.charge_mode, hlc) {
        // DC is unsound without the stack, and the port's absence on DC is
        // exactly a missing `hlc` or `slac`: `hlc_enabled`'s charge mode arm is
        // unconditionally true there, so those two are the whole of what it
        // read.
        (ChargeMode::Dc, None) => bail!(
            "charge_mode DC requires {} to be wired, but it is not connected",
            absent_stack_requirements(wiring).join(", ")
        ),
        // The one DC requirement the port cannot speak for.
        (ChargeMode::Dc, Some(_)) if !wiring.powersupply_dc => bail!(
            "charge_mode DC requires powersupply_DC to be wired, but it is not connected"
        ),
        (ChargeMode::Dc, Some(hlc)) => Ok(Deployment {
            path: Box::new(Dc::new(
                settings.dc_config(),
                IsolationMonitor::for_wiring(wiring, settings.cable_check_options()),
                OverVoltageMonitor::for_wiring(wiring),
            )),
            hlc: Some(hlc),
        }),

        // The same refusal DC gets, and for a sharper reason: the mode
        // presents DC to the vehicle over ISO 15118, so a port without the
        // stack and SLAC wired has no way to get the state of charge the whole
        // mode exists for. `EvseManager::setup_fake_DC_mode` dereferences
        // `r_hlc[0]` with no guard at all, so the C++ answer to this deployment
        // is a crash at `ready`.
        //
        // The DC power supply is deliberately not required. There is no supply
        // in this mode and never was: the DC envelope the vehicle is told is
        // hardcoded in `setup_fake_DC_mode`, and every DC subscription the C++
        // installs sits inside its `charge_mode == "DC"` branch, which an
        // `ac_with_soc` port is not in.
        (ChargeMode::Ac, None) if settings.ac.with_soc => bail!(
            "ac_with_soc requires {} to be wired, but it is not connected",
            absent_stack_requirements(wiring).join(", ")
        ),
        (ChargeMode::Ac, Some(hlc)) if settings.ac.with_soc => Ok(Deployment {
            path: Box::new(AcWithSoc::new(settings.iec_config())),
            hlc: Some(hlc),
        }),

        (ChargeMode::Ac, Some(hlc)) => Ok(Deployment {
            path: Box::new(AcHlc::new(settings.iec_config(), settings.pwm_start())),
            hlc: Some(hlc),
        }),
        // The one deployment with no port at all, and the only arm that
        // produces `AcBasic`. A basic AC port speaks IEC 61851 and nothing
        // else, so there is no stack here to hold a disabled handle to.
        (ChargeMode::Ac, None) => Ok(Deployment {
            path: Box::new(AcBasic::new(settings.iec_config())),
            hlc: None,
        }),
    }
}

/// Whichever of the two requirements `hlc_enabled` reads is not connected.
///
/// Called only where the resolved port came back absent on a mode that needs
/// one, so on DC and on `ac_with_soc` the list is never empty: those are the
/// modes whose `hlc_enabled` arm reads nothing but these two.
fn absent_stack_requirements(wiring: &Wiring) -> Vec<&'static str> {
    let mut missing = Vec::new();
    if !wiring.hlc {
        missing.push("hlc");
    }
    if !wiring.slac {
        missing.push("slac");
    }
    missing
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::core::session::SessionPhase;
    use serde_json::json;

    fn raw(pairs: &[(&str, Value)]) -> RawConfig {
        pairs
            .iter()
            .cloned()
            .map(|(k, v)| (k.to_string(), v))
            .collect()
    }

    fn one_connector() -> Mapping {
        Mapping {
            evse: 1,
            connectors: vec![1],
        }
    }

    fn settings_with(pairs: &[(&str, Value)]) -> Settings {
        Settings::from_raw(&raw(pairs), &one_connector()).unwrap()
    }

    /// The two switching break keys reach the reducer that reads them.
    ///
    /// The whole failure this port's configuration policy exists to prevent is
    /// a key that parses into `Settings` and is read by nothing. Both of these
    /// were exactly that until the break was ported, and a default substituted
    /// here rather than the configured value would put them straight back:
    /// every other test of the break passes on the defaults.
    /// The three values the C++ manifest declares, each accepted, and the one
    /// question the setting is read through.
    ///
    /// `E` is the one that matters: the port declared two values and the
    /// framework validates a config against the module's own enum, so a
    /// deployment configured with `E` - which is a value the C++ accepts and
    /// reads as X1 - could not start this module at all. Accepting it and
    /// reading it as X1 is what `Charger.cpp:1571` does with it.
    #[test]
    fn every_pilot_state_the_cpp_accepts_for_a_phase_switch_is_accepted_here() {
        for (configured, signals_f) in [("X1", false), ("E", false), ("F", true)] {
            let settings = settings_with(&[("switch_3ph1ph_cp_state", json!(configured))]);
            assert_eq!(
                settings.iec_config().switch_phases_cp_state.signals_state_f(),
                signals_f,
                "{configured} signals the wrong pilot state"
            );
        }

        assert!(
            SwitchCpState::parse("X2").is_err(),
            "a value no manifest declares is still refused"
        );
    }

    #[test]
    fn the_switching_break_keys_reach_the_control_pilot_reducer() {
        let configured = settings_with(&[
            ("switch_3ph1ph_cp_state", json!("F")),
            ("switch_3ph1ph_delay_s", json!(25)),
        ])
        .iec_config();
        assert_eq!(configured.switch_phases_cp_state, SwitchCpState::F);
        assert_eq!(
            configured.switch_phases_delay,
            Duration::from_secs(25),
            "the configured delay did not reach the reducer"
        );

        let defaults = settings_with(&[]).iec_config();
        assert_eq!(defaults.switch_phases_cp_state, SwitchCpState::X1);
        assert_eq!(defaults.switch_phases_delay, Duration::from_secs(10));
    }

    /// A negative delay is not representable on the wire as anything a
    /// `Duration` can hold, and the manifest sets no minimum, so it becomes no
    /// break rather than a panic in the conversion.
    #[test]
    fn a_negative_switching_delay_becomes_no_break_at_all() {
        let settings = settings_with(&[("switch_3ph1ph_delay_s", json!(-5))]);
        assert_eq!(settings.iec_config().switch_phases_delay, Duration::ZERO);
    }

    fn full_wiring() -> Wiring {
        Wiring {
            hlc: true,
            slac: true,
            powersupply_dc: true,
            imd: true,
            over_voltage_monitor: true,
            powermeter_car_side: true,
            connector_lock: true,
        }
    }

    #[test]
    fn the_connector_type_reaches_the_dc_path() {
        // The isolation fault threshold the DC path applies is chosen by
        // connector type (`EvseManager.cpp:2004-2007`), and the only route from
        // the config key to the path is this field. Without it a megawatt
        // charging port aborts every cable check on a resistance the standard
        // considers sound.
        assert_eq!(
            settings_with(&[
                ("charge_mode", json!("DC")),
                ("connector_type", json!("cMCS")),
            ])
            .dc_config()
            .connector,
            ConnectorKind::Mcs
        );
        assert_eq!(
            settings_with(&[
                ("charge_mode", json!("DC")),
                ("connector_type", json!("cCCS2")),
            ])
            .dc_config()
            .connector,
            ConnectorKind::Other
        );
    }

    #[test]
    fn dc_missing_hlc_is_named_in_the_error() {
        let settings = settings_with(&[("charge_mode", json!("DC"))]);
        let wiring = Wiring {
            hlc: false,
            ..full_wiring()
        };

        let err = resolve(&settings, &wiring).err().unwrap();

        assert!(err.to_string().contains("hlc"), "{err}");
    }

    #[test]
    fn dc_missing_slac_is_named_in_the_error() {
        let settings = settings_with(&[("charge_mode", json!("DC"))]);
        let wiring = Wiring {
            slac: false,
            ..full_wiring()
        };

        let err = resolve(&settings, &wiring).err().unwrap();

        assert!(err.to_string().contains("slac"), "{err}");
    }

    #[test]
    fn dc_missing_powersupply_is_named_in_the_error() {
        let settings = settings_with(&[("charge_mode", json!("DC"))]);
        let wiring = Wiring {
            powersupply_dc: false,
            ..full_wiring()
        };

        let err = resolve(&settings, &wiring).err().unwrap();

        assert!(err.to_string().contains("powersupply_DC"), "{err}");
    }

    #[test]
    fn dc_with_full_wiring_selects_dc() {
        let settings = settings_with(&[("charge_mode", json!("DC"))]);

        let path = resolve(&settings, &full_wiring()).unwrap().path;

        assert_eq!(path.name(), "Dc");
    }

    #[test]
    fn ac_enforce_hlc_wins_over_5percent() {
        let settings = settings_with(&[
            ("ac_enforce_hlc", json!(true)),
            ("ac_hlc_use_5percent", json!(true)),
        ]);

        assert_eq!(settings.pwm_start(), PwmStart::FivePercentEnforced);
    }

    #[test]
    fn ac_5percent_without_enforce() {
        let settings = settings_with(&[
            ("ac_enforce_hlc", json!(false)),
            ("ac_hlc_use_5percent", json!(true)),
        ]);

        assert_eq!(settings.pwm_start(), PwmStart::FivePercent);
    }

    #[test]
    fn ac_nominal_when_neither_enforce_nor_5percent_set() {
        let settings = settings_with(&[
            ("ac_enforce_hlc", json!(false)),
            ("ac_hlc_use_5percent", json!(false)),
        ]);

        assert_eq!(settings.pwm_start(), PwmStart::Nominal);
    }

    #[test]
    fn ac_falls_back_to_basic_when_hlc_configured_but_not_wired() {
        let settings = settings_with(&[("ac_hlc_enabled", json!(true))]);
        let wiring = Wiring {
            hlc: false,
            ..full_wiring()
        };

        let path = resolve(&settings, &wiring).unwrap().path;

        assert_eq!(path.name(), "AcBasic");
    }

    #[test]
    fn ac_uses_hlc_path_when_enabled_and_wired() {
        let settings = settings_with(&[("ac_hlc_enabled", json!(true))]);

        let path = resolve(&settings, &full_wiring()).unwrap().path;

        assert_eq!(path.name(), "AcHlc");
    }

    #[test]
    fn ac_basic_when_hlc_not_requested_even_if_wired() {
        let settings = settings_with(&[("ac_hlc_enabled", json!(false))]);

        let path = resolve(&settings, &full_wiring()).unwrap().path;

        assert_eq!(path.name(), "AcBasic");
    }

    #[test]
    fn high_level_communication_needs_slac_as_well_as_the_stack() {
        // `EvseManager.cpp:174-187` disables SLAC unless the deployment asks
        // for high level communication, and then disables high level
        // communication unless SLAC survived. So a stack wired without SLAC is
        // an ordinary AC port: it takes the basic path and, because the whole
        // `if (hlc_enabled)` block is skipped, it also tells the stack nothing
        // and advertises no transfer modes.
        let settings = settings_with(&[("ac_hlc_enabled", json!(true))]);
        let without_slac = Wiring {
            slac: false,
            ..full_wiring()
        };

        assert!(!hlc_enabled(&settings, &without_slac));
        assert_eq!(
            resolve(&settings, &without_slac).unwrap().path.name(),
            "AcBasic"
        );
        assert!(hlc_enabled(&settings, &full_wiring()));
    }

    /// The pairing this module now owns, over the whole cross product of the
    /// two facts `hlc_enabled` reads and the three AC shapes.
    ///
    /// `PowerPath::requires_hlc()` used to answer this question a second time,
    /// as a `bool` on the path. It is deleted: presence is the answer, and this
    /// test is what says the pairing `resolve` produces is the one the deleted
    /// predicate would have claimed. A basic AC port is the only cell with no
    /// port, and it is also the only cell that produces `AcBasic`.
    #[test]
    fn the_only_path_without_a_port_is_the_basic_one() {
        for (name, settings, wiring, port_expected) in [
            ("AcBasic", settings_with(&[]), full_wiring(), false),
            (
                "AcBasic",
                settings_with(&[("ac_hlc_enabled", json!(true))]),
                Wiring {
                    hlc: false,
                    ..full_wiring()
                },
                false,
            ),
            (
                "AcBasic",
                settings_with(&[("ac_hlc_enabled", json!(true))]),
                Wiring {
                    slac: false,
                    ..full_wiring()
                },
                false,
            ),
            (
                "AcHlc",
                settings_with(&[("ac_hlc_enabled", json!(true))]),
                full_wiring(),
                true,
            ),
            (
                "AcWithSoc",
                settings_with(&[("ac_with_soc", json!(true))]),
                full_wiring(),
                true,
            ),
            (
                "Dc",
                settings_with(&[("charge_mode", json!("DC"))]),
                full_wiring(),
                true,
            ),
        ] {
            let deployment = resolve(&settings, &wiring).expect("the deployment is accepted");
            assert_eq!(deployment.path.name(), name, "{wiring:?}");
            assert_eq!(
                deployment.hlc.is_some(),
                port_expected,
                "{name} on {wiring:?}"
            );
            // And the boundary's handle is the port's own, so the two cannot
            // disagree about whether there is a stack.
            assert_eq!(
                deployment.hlc_config().is_some(),
                deployment.hlc.is_some(),
                "{name} on {wiring:?}"
            );
            // The predicate is the constructor's one input and nothing else
            // reads it, so the port's presence is exactly its answer.
            assert_eq!(
                deployment.hlc.is_some(),
                hlc_enabled(&settings, &wiring),
                "{name} on {wiring:?}"
            );
        }
    }

    /// Both keys reach the isolation monitor that runs the steps they name.
    ///
    /// All three parsed into `DcSettings` and were dropped: `Dc::new` seeded
    /// `CableCheckOptions::default()` and the only writer was a setter with no
    /// caller outside tests. A default substituted here rather than the
    /// configured value would put them straight back, which is why each is
    /// asserted against its non default value.
    #[test]
    fn the_cable_check_option_keys_reach_the_isolation_monitor() {
        let settings = settings_with(&[
            ("charge_mode", json!("DC")),
            ("cable_check_enable_imd_self_test", json!(false)),
            ("cable_check_enable_imd_self_test_relays_open", json!(true)),
            ("cable_check_wait_below_60V_before_finish", json!(false)),
        ]);
        assert_eq!(
            settings.cable_check_options(),
            CableCheckOptions {
                imd_self_test: false,
                imd_self_test_relays_open: true,
                wait_below_60v_before_finish: false,
            }
        );
    }

    #[test]
    fn a_stack_that_is_not_wired_is_not_enabled_either() {
        let settings = settings_with(&[("ac_hlc_enabled", json!(true))]);
        assert!(!hlc_enabled(
            &settings,
            &Wiring {
                hlc: false,
                ..full_wiring()
            }
        ));
    }

    /// `ac_with_soc` used to be refused at startup. It selects a path now.
    mod ac_with_soc {
        use super::*;

        fn with_soc() -> Settings {
            settings_with(&[("ac_with_soc", json!(true))])
        }

        #[test]
        fn the_key_is_accepted_and_selects_the_mode() {
            assert!(reject_requested_workarounds(
                [SuppliedKey {
                    implementation: None,
                    name: "ac_with_soc",
                    value: None,
                }]
                .into_iter()
            )
            .is_ok());
            assert_eq!(
                resolve(&with_soc(), &full_wiring()).unwrap().path.name(),
                "AcWithSoc"
            );
        }

        /// `EvseManager::init`'s `slac_enabled` derivation keeps SLAC for an `ac_with_soc` port
        /// whether or not `ac_hlc_enabled` is set, and it has to: the mode gets
        /// its state of charge out of an ISO 15118 handshake.
        #[test]
        fn the_stack_is_enabled_without_the_ac_high_level_key() {
            let settings = with_soc();
            assert!(!settings.ac.hlc_enabled, "the AC key is off");
            assert!(hlc_enabled(&settings, &full_wiring()));
        }

        /// `EvseManager::setup_fake_DC_mode` dereferences `r_hlc[0]` with no
        /// guard, so the C++ answer to this deployment is a crash at `ready`.
        #[test]
        fn the_mode_is_refused_without_the_stack_or_slac_wired() {
            for (missing, wiring) in [
                (
                    "hlc",
                    Wiring {
                        hlc: false,
                        ..full_wiring()
                    },
                ),
                (
                    "slac",
                    Wiring {
                        slac: false,
                        ..full_wiring()
                    },
                ),
            ] {
                let message = resolve(&with_soc(), &wiring)
                    .err()
                    .expect("the deployment must be refused")
                    .to_string();
                assert!(message.contains("ac_with_soc"), "{message}");
                assert!(message.contains(missing), "{message}");
            }
        }

        /// The DC supply is deliberately not required: there is none in this
        /// mode, and the DC envelope the vehicle is told is hardcoded in
        /// `setup_fake_DC_mode`.
        #[test]
        fn the_mode_needs_no_dc_power_supply() {
            let wiring = Wiring {
                powersupply_dc: false,
                imd: false,
                ..full_wiring()
            };
            assert_eq!(
                resolve(&with_soc(), &wiring).unwrap().path.name(),
                "AcWithSoc"
            );
        }

        /// An AC deployment that does not set the key is unaffected.
        #[test]
        fn an_ordinary_ac_port_still_takes_the_ac_paths() {
            assert_eq!(
                resolve(&settings_with(&[]), &full_wiring()).unwrap().path.name(),
                "AcBasic"
            );
            assert_eq!(
                resolve(
                    &settings_with(&[("ac_hlc_enabled", json!(true))]),
                    &full_wiring()
                ).unwrap().path.name(),
                "AcHlc"
            );
        }

        /// A DC deployment that also sets the key is still DC. `EvseManager`
        /// reads `config.charge_mode == "DC"` at every gate the mode would
        /// otherwise reach, and `ready` takes the `if (config.ac_with_soc)`
        /// branch only after `charge_mode` has already decided the
        /// subscriptions.
        #[test]
        fn a_dc_deployment_that_also_sets_the_key_stays_dc() {
            let settings =
                settings_with(&[("charge_mode", json!("DC")), ("ac_with_soc", json!(true))]);
            assert_eq!(resolve(&settings, &full_wiring()).unwrap().path.name(), "Dc");
        }
    }

    #[test]
    fn the_reinit_keys_reach_the_control_pilot_reducer() {
        for (wire, expected) in [
            ("CPStateE", ReinitMethod::CpStateE),
            ("CPStateF", ReinitMethod::CpStateF),
            ("CPStateX1", ReinitMethod::CpStateX1),
        ] {
            let configured = settings_with(&[
                ("reinit_method", json!(wire)),
                ("reinit_duration_ms", json!(4500)),
            ])
            .iec_config();
            assert_eq!(configured.reinit_method, expected, "{wire}");
            assert_eq!(configured.reinit_duration, Duration::from_millis(4500));
        }

        // The manifest defaults.
        let defaults = settings_with(&[]).iec_config();
        assert_eq!(defaults.reinit_method, ReinitMethod::CpStateF);
        assert_eq!(defaults.reinit_duration, Duration::from_millis(3000));
    }

    #[test]
    fn an_unknown_reinit_method_is_rejected() {
        let err = Settings::from_raw(
            &raw(&[("reinit_method", json!("CPStateD"))]),
            &one_connector(),
        )
        .unwrap_err();
        assert!(err.to_string().contains("CPStateD"), "{err}");
        assert!(err.to_string().contains("reinit_method"), "{err}");
    }

    /// A negative duration is no hold at all, the same answer the switching
    /// break gives.
    #[test]
    fn a_negative_reinit_duration_becomes_no_hold_at_all() {
        assert_eq!(
            settings_with(&[("reinit_duration_ms", json!(-1))])
                .iec_config()
                .reinit_duration,
            Duration::ZERO
        );
    }

    #[test]
    fn unknown_charge_mode_is_rejected() {
        let err = Settings::from_raw(
            &raw(&[("charge_mode", json!("Wireless"))]),
            &one_connector(),
        )
        .unwrap_err();

        assert!(err.to_string().contains("Wireless"), "{err}");
    }

    /// The substitutability half, and the reason the six keys are declared.
    ///
    /// Every one of them defaults to inert in the C++ manifest, so a config
    /// that sets one to `false` or to a zero offset asks for the behaviour this
    /// module already has. Refused on the key's presence, as they were, such a
    /// deployment could not start this module over settings that change
    /// nothing - and the value could not even be consulted, because the
    /// framework reports an undeclared key without it.
    #[test]
    fn an_existing_config_that_sets_the_workarounds_inert_starts_the_module() {
        let inert: Vec<(&str, Value)> = UNSUPPORTED_KEYS
            .iter()
            .map(|key| {
                let value = if *key == "hack_present_current_offset" {
                    json!(0)
                } else {
                    json!(false)
                };
                (*key, value)
            })
            .collect();

        let settings = Settings::from_raw(&raw(&inert), &one_connector());

        assert!(settings.is_ok(), "{:?}", settings.err());
    }

    /// And the refusal still stands for a value that asks for something, one
    /// key at a time, so accepting the inert value cannot be mistaken for
    /// accepting the workaround.
    #[test]
    fn each_unsupported_key_is_named_in_its_own_error() {
        for key in UNSUPPORTED_KEYS {
            let err =
                Settings::from_raw(&raw(&[(key, json!(true))]), &one_connector()).unwrap_err();
            assert!(
                err.to_string().contains(key),
                "error for {key} did not name it: {err}"
            );
        }
    }

    #[test]
    fn multiple_unsupported_keys_are_all_named() {
        let err = Settings::from_raw(
            &raw(&[
                ("hack_skoda_enyaq", json!(true)),
                ("hack_present_current_offset", json!(1.0)),
            ]),
            &one_connector(),
        )
        .unwrap_err();

        let message = err.to_string();
        assert!(message.contains("hack_skoda_enyaq"), "{message}");
        assert!(message.contains("hack_present_current_offset"), "{message}");
    }

    /// A value that asks for the behaviour, for each of the six. Five are
    /// booleans and the other is an offset in amperes, so `true` and a nonzero
    /// number are the two shapes a request arrives in.
    fn requested(key: &str) -> Value {
        if key == "hack_present_current_offset" {
            json!(3)
        } else {
            json!(true)
        }
    }

    #[test]
    fn every_unsupported_key_is_refused_under_a_provided_interface_too() {
        for key in UNSUPPORTED_KEYS {
            let asked = requested(key);
            let err = reject_requested_workarounds(
                [SuppliedKey {
                    implementation: Some("evse"),
                    name: key,
                    value: Some(&asked),
                }]
                .into_iter(),
            )
            .unwrap_err();

            let message = err.to_string();
            assert!(
                message.contains(key),
                "error for {key} did not name it: {message}"
            );
            assert!(
                message.contains("evse"),
                "error for {key} did not name the group: {message}"
            );
        }
    }

    #[test]
    fn a_key_in_the_module_group_is_named_without_a_group() {
        let asked = json!(true);
        let err = reject_requested_workarounds(
            [SuppliedKey {
                implementation: None,
                name: "hack_skoda_enyaq",
                value: Some(&asked),
            }]
            .into_iter(),
        )
        .unwrap_err();

        let message = err.to_string();
        assert!(message.contains("hack_skoda_enyaq"), "{message}");
        assert!(!message.contains("under implementation"), "{message}");
    }

    #[test]
    fn a_supported_key_under_a_provided_interface_is_not_refused() {
        // Undeclared there and therefore reported, but not one of the six, so
        // the framework's own log is the whole story and startup continues.
        assert!(reject_requested_workarounds(
            [
                SuppliedKey {
                    implementation: Some("energy_grid"),
                    name: "hack_allow_bpt_with_iso2",
                    value: None,
                },
                SuppliedKey {
                    implementation: None,
                    name: "a_key_nobody_declares",
                    value: None,
                },
            ]
            .into_iter(),
        )
        .is_ok());
    }

    #[test]
    fn the_same_excluded_key_in_two_groups_is_named_once_per_group() {
        // Reachable: nothing stops a config file from setting the key in
        // `config_module` and in a `config_implementation` block. Each place
        // the operator has to edit gets its own mention.
        let enabled = json!(true);
        let err = reject_requested_workarounds(
            [
                SuppliedKey {
                    implementation: None,
                    name: "hack_skoda_enyaq",
                    value: Some(&enabled),
                },
                SuppliedKey {
                    implementation: Some("evse"),
                    name: "hack_skoda_enyaq",
                    value: Some(&enabled),
                },
            ]
            .into_iter(),
        )
        .unwrap_err();

        assert_eq!(
            err.to_string(),
            "config key(s) hack_skoda_enyaq, hack_skoda_enyaq (under implementation evse) \
             ask for behaviour RsEvseManager does not implement; each is accepted at \
             its default, which is what the C++ module also does with it unless it is \
             switched on"
        );
    }

    #[test]
    fn keys_from_every_group_are_reported_in_one_error() {
        let offset = json!(3);
        let enabled = json!(true);
        let err = reject_requested_workarounds(
            [
                SuppliedKey {
                    implementation: None,
                    name: "hack_present_current_offset",
                    value: Some(&offset),
                },
                SuppliedKey {
                    implementation: Some("token_provider"),
                    name: "hack_skoda_enyaq",
                    value: Some(&enabled),
                },
            ]
            .into_iter(),
        )
        .unwrap_err();

        let message = err.to_string();
        assert!(message.contains("hack_present_current_offset"), "{message}");
        assert!(
            message.contains("hack_skoda_enyaq (under implementation token_provider)"),
            "{message}"
        );
    }

    #[test]
    fn defaults_match_the_manifest() {
        let settings = Settings::from_raw(&RawConfig::new(), &one_connector()).unwrap();

        assert_eq!(settings.charge_mode, ChargeMode::Ac);
        assert_eq!(settings.evse_id, "DE*PNX*E1234567*1");
        assert!(settings.payment.enable_eim);
        assert!(settings.ac.has_ventilation);
        assert_eq!(settings.dc.wait_number_of_imd_measurements, 1);
        // On by default, so an installation that says nothing does not charge a
        // customer it cannot bill.
        assert!(settings.misc.fail_on_powermeter_errors);
    }

    #[test]
    fn the_powermeter_failure_setting_can_be_turned_off() {
        let settings = settings_with(&[("fail_on_powermeter_errors", json!(false))]);

        assert!(!settings.misc.fail_on_powermeter_errors);
    }

    #[test]
    fn the_legacy_topic_id_comes_from_the_connector_id_key() {
        let settings = settings_with(&[("connector_id", json!(4))]);

        assert_eq!(settings.legacy_topic_id, 4);
    }

    #[test]
    fn the_evse_index_comes_from_the_three_tier_mapping_not_from_a_config_key() {
        let settings = Settings::from_raw(
            &raw(&[("connector_id", json!(4))]),
            &Mapping {
                evse: 2,
                connectors: vec![1],
            },
        )
        .unwrap();

        assert_eq!(settings.evse_index, 2);
        assert_eq!(settings.legacy_topic_id, 4);
    }

    #[test]
    fn the_initial_session_starts_idle_at_the_iec_minimum_current() {
        let settings = settings_with(&[]);

        let session = settings.initial_session();

        assert_eq!(session.phase, SessionPhase::Idle);
        assert_eq!(session.limits.max_current_a, INITIAL_CURRENT_LIMIT_A);
        assert_eq!(
            session.limits.nr_of_phases_available,
            INITIAL_PHASES_AVAILABLE
        );
    }

    /// The published figure and the offerable one are deliberately different
    /// at startup, which is the C++ state: 6 A stored and announced, and out of
    /// validity, so nothing may be offered on it. A port that started
    /// offerable would charge a vehicle at the IEC minimum with no grant behind
    /// it, which `Charger.cpp:354-355` waits rather than do.
    #[test]
    fn the_port_announces_the_iec_minimum_and_offers_nothing_until_a_grant() {
        let settings = settings_with(&[]);

        assert_eq!(
            settings.initial_session().limits.max_current_a,
            INITIAL_CURRENT_LIMIT_A,
            "announced"
        );
        assert_eq!(
            settings.iec_config().initial_current_limit_a, 0.0,
            "offerable"
        );
    }

    #[test]
    fn the_initial_session_carries_the_configured_pwm_start() {
        let settings = settings_with(&[("ac_enforce_hlc", json!(true))]);

        assert_eq!(
            settings.initial_session().profile.pwm_start,
            PwmStart::FivePercentEnforced
        );
    }

    #[test]
    fn a_mapping_to_more_than_one_connector_is_refused_and_named() {
        let settings = Settings::from_raw(
            &RawConfig::new(),
            &Mapping {
                evse: 1,
                connectors: vec![1, 2],
            },
        )
        .unwrap();

        let err = resolve(&settings, &full_wiring()).err().unwrap();

        let message = err.to_string();
        assert!(message.contains("[1, 2]"), "{message}");
        assert!(message.contains("evse 1"), "{message}");
    }

    #[test]
    fn a_mapping_to_no_connector_is_refused_and_named() {
        let settings = Settings::from_raw(
            &RawConfig::new(),
            &Mapping {
                evse: 3,
                connectors: Vec::new(),
            },
        )
        .unwrap();

        let err = resolve(&settings, &full_wiring()).err().unwrap();

        let message = err.to_string();
        assert!(message.contains("evse 3"), "{message}");
        assert!(message.contains("[]"), "{message}");
    }

    #[test]
    fn a_mapping_to_exactly_one_connector_is_accepted() {
        let settings = settings_with(&[]);

        assert!(resolve(&settings, &full_wiring()).is_ok());
    }
}
