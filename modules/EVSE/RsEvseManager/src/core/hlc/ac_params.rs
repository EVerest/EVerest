// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

//! The AC parameter and limit port.
//!
//! What the module tells the ISO 15118 stack about an AC EVSE: the nominal
//! voltage at boot, the power envelope the board support report implies, and
//! the live limit as it changes. The last of those has two spellings, and
//! choosing between them is the whole of what this module decides: an
//! ISO 15118-2 session is told an ampere count and an ISO 15118-20 AC session
//! is told a target power.
//!
//! The sibling of `dc_limits` for the other charge mode. The two are
//! deliberately not merged: they emit disjoint commands, they read disjoint
//! halves of `SetupPhysicalValues`, and the AC envelope is derived from the
//! board support report while the DC one is derived from the power supply
//! report.

use super::setup::HlcConfig;
use super::AcCapabilities;
use crate::core::effect::{Effect, HlcUpdate};
use crate::core::hlc::dc_limits::PhysicalValues;

/// `types::units::Power`.
///
/// The three phase figures are optional on the wire and stay optional here,
/// because that is how the C++ reports a port with fewer than three phases:
/// `update_hlc_ac_parameters` fills `L2` and `L3` only when the phase count
/// reaches them (`EvseManager.cpp:1834-1835`), so an absent figure means "this
/// port has no such phase" rather than "zero watts on it".
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Power {
    pub total_w: f64,
    pub l1_w: Option<f64>,
    pub l2_w: Option<f64>,
    pub l3_w: Option<f64>,
}

/// `types::iso15118::AcEvseMaximumPower` and `types::iso15118::AcEvseMinimumPower`.
///
/// One struct for two wire types, which are field for field identical and are
/// filled by the same code shape at `EvseManager.cpp:1831-1840` and
/// `:1850-1860`. Which of the two a value is travels with the `HlcUpdate`
/// variant carrying it, so nothing here can be sent as the wrong one.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct AcPowerSet {
    pub charge_power: Power,
    /// Optional on the wire. This emitter always fills it, including for a port
    /// that cannot export at all, which is the C++ unconditional
    /// `discharge_power.emplace(...)` (`:1838`, `:1857`). A port with no export
    /// capability therefore reports a zero discharge envelope rather than none.
    pub discharge_power: Option<Power>,
}

/// `types::iso15118::Connector`, narrowed to the two values the AC connector
/// list can hold.
///
/// The wire enum has fourteen. `update_hlc_ac_parameters` builds the list from
/// exactly two of them (`EvseManager.cpp:1863-1866`), and no other value is
/// reachable through this emission, so the other twelve would be surface with
/// no producer. The MCS spellings the advertised set derivation deals in are a
/// different question on a different command; see `ConnectorKind`.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum AcConnector {
    SinglePhase,
    ThreePhase,
}

/// `types::iso15118::AcParameters`, the general AC announcement.
#[derive(Clone, Debug, PartialEq)]
pub struct AcParameters {
    pub nominal_frequency_hz: f64,
    pub nominal_voltage_v: f64,
    pub connectors: Vec<AcConnector>,
    /// Both absent in every emission the C++ makes (`EvseManager.cpp:1868`
    /// passes `std::nullopt` for each). They are carried rather than dropped
    /// because the wire message has them and a port that fills them later fills
    /// them here; nothing in this module produces a value for either.
    pub max_power_asymmetry_w: Option<f64>,
    pub power_ramp_limitation_percent_per_min: Option<f64>,
    /// Present only when the deployment names a positive maximum, which is the
    /// C++ ternary at `:1869-1870`. A zero setting means "not declared" and
    /// must not reach the vehicle as a zero var ceiling, because that is the
    /// de-normalization base every AC_DER_IEC percentage curve divides by.
    pub evse_max_reactive_power_var: Option<f64>,
}

/// `types::iso15118::ServiceCategory`, the service the vehicle selected in
/// `ServiceSelectionReq`.
///
/// All thirteen wire values, so the boundary conversion is total. That is load
/// bearing rather than tidy: only two readers exist today, one asking whether
/// any ISO 15118-20 service was selected and one asking whether it was an AC
/// one, and a narrowing to those two questions would fold every other service
/// into a catch-all. A new value added upstream would then join the catch-all
/// silently instead of failing to compile at the one place that has to decide
/// what it means.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SelectedService {
    Ac,
    Dc,
    Wpt,
    DcAcdp,
    AcBpt,
    DcBpt,
    DcAcdpBpt,
    Mcs,
    McsBpt,
    AcDerIec,
    AcDerSae,
    Internet,
    ParkingStatus,
}

impl SelectedService {
    /// Whether this service is one of the two the AC target power emission
    /// answers for (`EvseManager.cpp:1249-1250`).
    ///
    /// `AcDerIec` and `AcDerSae` are AC services and are deliberately **not**
    /// included: the C++ names `AC` and `AC_BPT` and no others, so a DER
    /// session takes neither branch and the vehicle is told no limit at all.
    /// That is a gap in the C++ rather than a decision, and it is not fixed
    /// here, because widening it changes what a DER vehicle is offered.
    pub fn is_ac_target_power_service(self) -> bool {
        matches!(self, SelectedService::Ac | SelectedService::AcBpt)
    }

    /// Whether this service is a bidirectional one, the selected service term
    /// of the bidirectional resolution.
    ///
    /// The C++ asks this question with two equality tests against two
    /// categories, `AC_BPT` (`EvseManager.cpp:2670-2671`) and `DC_BPT`
    /// (`:2675-2676`). `DcAcdpBpt` and `McsBpt` are named here and by no C++
    /// site: they are the same suffix on the other two connectors, and a
    /// bidirectional selection this port failed to recognize would be read as
    /// a charge only session, which is the direction that silently refuses a
    /// discharge rather than permitting one.
    ///
    /// `AcDerIec` and `AcDerSae` are **not** included. They are AC services and
    /// a DER session may well discharge, but the C++ names neither, and the
    /// selected service is not what declares a DER session bidirectional: the
    /// SAE flag and the config source are. Widening this would permit an export
    /// on a selection the C++ reads as import only.
    pub fn is_bidirectional(self) -> bool {
        matches!(
            self,
            SelectedService::AcBpt
                | SelectedService::DcBpt
                | SelectedService::DcAcdpBpt
                | SelectedService::McsBpt
        )
    }
}

/// The nominal frequency the C++ announces, hardcoded at
/// `EvseManager.cpp:1868` next to its own `TODO(sl): Getting nominal frequency`.
/// Hardcoded here too, so the port does not invent a configuration key the C++
/// manifest never had.
const NOMINAL_FREQUENCY_HZ: f64 = 50.0;

/// `EvseManager.cpp:447-449`, the AC half of the boot physical values.
///
/// Only `ac_nominal_voltage` is set. The three DC fields are the other call
/// site's (`EvseManager.hpp:275-279`) and are left absent rather than filled
/// with zeros, which a stack cannot tell apart from a real declaration.
pub fn boot_physical_values(config: &HlcConfig) -> PhysicalValues {
    PhysicalValues {
        ac_nominal_voltage_v: Some(config.ac_nominal_voltage_v),
        dc_current_regulation_tolerance_a: None,
        dc_peak_current_ripple_a: None,
        dc_energy_to_be_delivered_wh: None,
    }
}

/// `update_hlc_ac_parameters` (`EvseManager.cpp:1821-1872`), the three
/// emissions a board support capability report owes, in the C++ order.
///
/// Ungated: the caller applies the `config.charge_mode == "AC" and hlc_enabled`
/// guard the call site has (`:281`), because that guard decides whether this
/// port runs at all and not what it says.
pub fn capability_emissions(config: &HlcConfig, caps: AcCapabilities) -> Vec<Effect> {
    vec![
        Effect::HlcUpdate(HlcUpdate::AcMaximumLimits(maximum_power(config, caps))),
        Effect::HlcUpdate(HlcUpdate::AcMinimumLimits(minimum_power(config, caps))),
        Effect::HlcUpdate(HlcUpdate::AcParameters(parameters(config, caps))),
    ]
}

/// `:1826-1842`. The per phase figure is the nominal voltage times the current
/// ceiling, and the total is that figure times the phase count.
fn maximum_power(config: &HlcConfig, caps: AcCapabilities) -> AcPowerSet {
    AcPowerSet {
        charge_power: phase_power(
            config.ac_nominal_voltage_v * caps.max_current_a_import,
            caps.max_phase_count_import,
        ),
        discharge_power: Some(phase_power(
            config.ac_nominal_voltage_v * caps.max_current_a_export,
            caps.max_phase_count_export,
        )),
    }
}

/// `:1844-1861`.
///
/// The phase counts are the **maximum** counts in both halves, not the minimum
/// ones (`:1853-1854` reads `max_phase_count_import` and `:1858-1859` reads
/// `max_phase_count_export`). That is the C++ as written and it is preserved:
/// the figure it produces is the minimum current the port can hold on every
/// phase it has, which is what a vehicle needs in order to know whether the
/// envelope it is offered has an interval at all. Deriving it from the minimum
/// phase count instead would name a floor the port never sits at.
fn minimum_power(config: &HlcConfig, caps: AcCapabilities) -> AcPowerSet {
    AcPowerSet {
        charge_power: phase_power(
            config.ac_nominal_voltage_v * caps.min_current_a_import,
            caps.max_phase_count_import,
        ),
        discharge_power: Some(phase_power(
            config.ac_nominal_voltage_v * caps.min_current_a_export,
            caps.max_phase_count_export,
        )),
    }
}

/// The one shape both power sets are built in (`:1831-1840`, `:1850-1860`).
///
/// `L1` is always named and the other two are named only once the phase count
/// reaches them. The thresholds are the C++ thresholds and they differ: `L2`
/// needs at least two phases and `L3` needs exactly three, so a port reporting
/// four phases names `L2` and not `L3`. Preserved rather than tidied, because
/// the phase count is clamped nowhere on this path and a board reporting a
/// count above three is the only input that can tell the two rules apart.
fn phase_power(per_phase_w: f64, phase_count: i64) -> Power {
    Power {
        total_w: per_phase_w * phase_count as f64,
        l1_w: Some(per_phase_w),
        l2_w: (phase_count >= 2).then_some(per_phase_w),
        l3_w: (phase_count == 3).then_some(per_phase_w),
    }
}

/// `:1863-1870`. The general announcement.
fn parameters(config: &HlcConfig, caps: AcCapabilities) -> AcParameters {
    let mut connectors = vec![AcConnector::SinglePhase];
    // `:1864`. Exactly three, so a single or two phase port offers the single
    // phase connector alone.
    if caps.max_phase_count_import == 3 {
        connectors.push(AcConnector::ThreePhase);
    }
    AcParameters {
        nominal_frequency_hz: NOMINAL_FREQUENCY_HZ,
        nominal_voltage_v: config.ac_nominal_voltage_v,
        connectors,
        max_power_asymmetry_w: None,
        power_ramp_limitation_percent_per_min: None,
        evse_max_reactive_power_var: (config.ac_max_reactive_power_var > 0.0)
            .then_some(config.ac_max_reactive_power_var),
    }
}

/// The live limit as the charger changed it, in whichever spelling the session
/// negotiated (`EvseManager.cpp:1241-1260`).
///
/// The branch is the whole of this function and the two arms are not
/// interchangeable: an ISO 15118-2 session is told an ampere count, and an
/// ISO 15118-20 AC session is told a target power derived from that count, the
/// nominal voltage and the phase count. Sending the wrong one to either leaves
/// the vehicle with no limit it understands.
///
/// `None` in three cases, each of which the C++ arrives at differently:
///
/// - No service selected and a negative ampere count. The C++ reads
///   `selected_d20_energy_service.value()` at `:1249` after that conjunction
///   fails, so it dereferences an empty optional and throws
///   `std::bad_optional_access` out of a signal handler. This port answers
///   `None`, because a discharge request on a session with no ISO 15118-20
///   service has no spelling either arm can carry.
/// - A service was selected and it is not `AC` or `AC_BPT` (`:1249-1250`). A DC
///   session hears nothing on this path, which is right: it is told its limits
///   through `dc_limits`.
/// - The negative ampere count on a selected AC service reaches the target
///   power arm and is **not** filtered, so a discharge request does produce a
///   negative target power. The `ampere >= 0.0` guard belongs to the
///   ISO 15118-2 arm alone (`:1244`), and that asymmetry is the point: an
///   ampere count on the wire has no sign to carry a direction in, while a
///   target power does.
pub fn current_limit_update(
    config: &HlcConfig,
    caps: AcCapabilities,
    selected: Option<SelectedService>,
    ampere: f64,
) -> Option<HlcUpdate> {
    match selected {
        // `:1244-1246`.
        None if ampere >= 0.0 => Some(HlcUpdate::AcMaxCurrent(ampere)),
        None => None,
        Some(service) if service.is_ac_target_power_service() => {
            // `:1252-1253`. One figure, the total, and no per phase breakdown:
            // the C++ brace initializes `types::units::Power` with a single
            // value, leaving `L1` through `L3` absent.
            Some(HlcUpdate::AcTargetPower(Power {
                total_w: ampere
                    * config.ac_nominal_voltage_v
                    * caps.max_phase_count_import as f64,
                ..Power::default()
            }))
        }
        Some(_) => None,
    }
}

/// The meter's live power reaching the vehicle (`EvseManager.cpp:1167-1168`).
///
/// Gated on **any** ISO 15118-20 service having been selected, not on an AC
/// one. That is the C++ gate as written, and it is not narrowed here: the
/// command is an AC one and a DC session is told its present values through
/// `dc_limits`, so the wide gate sends a DC session an AC message it has no use
/// for. Narrowing it would be a behavior divergence, and the C++ reading is
/// what a deployment is calibrated against.
///
/// `None` when the meter reported no power figure at all, which is the
/// `p.power_W` half of the same conjunction. The reading is forwarded whole
/// rather than as its total, because the command takes the wire type and the
/// per phase figures are part of what the vehicle is told.
pub fn present_power_update(
    selected: Option<SelectedService>,
    power: Option<Power>,
) -> Option<HlcUpdate> {
    match (selected, power) {
        (Some(_), Some(power)) => Some(HlcUpdate::AcPresentPower(power)),
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::core::config::{Mapping, RawConfig, Settings, Wiring};
    use serde_json::json;

    /// The wiring an `HlcConfig` exists on: the stack and SLAC connected.
    /// `HlcConfig::for_deployment` reads nothing else.
    fn fully_wired() -> Wiring {
        Wiring {
            hlc: true,
            slac: true,
            ..Wiring::default()
        }
    }

    /// Built through `HlcConfig::for_deployment` rather than by naming every
    /// field, so a new configuration key cannot be given a test only value that
    /// the real resolution never produces.
    fn config(nominal_voltage_v: f64, max_reactive_power_var: f64) -> HlcConfig {
        let raw: RawConfig = [
            ("charge_mode".to_string(), json!("AC")),
            ("ac_hlc_enabled".to_string(), json!(true)),
            ("ac_nominal_voltage".to_string(), json!(nominal_voltage_v)),
            (
                "ac_max_reactive_power".to_string(),
                json!(max_reactive_power_var),
            ),
        ]
        .into_iter()
        .collect();
        let settings = Settings::from_raw(
            &raw,
            &Mapping {
                evse: 1,
                connectors: vec![1],
            },
        )
        .expect("the AC settings above resolve");
        // Wired and enabled, which is the only shape an `HlcConfig` has:
        // `for_deployment` is the one constructor and it answers `None` for a
        // deployment `hlc_enabled` would have been false on.
        HlcConfig::for_deployment(&settings, &fully_wired())
            .expect("a wired deployment has a configuration")
    }

    /// Three phase import, single phase export, so every asymmetry between the
    /// two halves shows up in the figures rather than cancelling out.
    fn caps() -> AcCapabilities {
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

    /// `EvseManager.cpp:447-449`. The AC nominal voltage alone.
    #[test]
    fn the_boot_physical_values_name_the_ac_nominal_voltage_and_nothing_else() {
        assert_eq!(
            boot_physical_values(&config(230.0, 0.0)),
            PhysicalValues {
                ac_nominal_voltage_v: Some(230.0),
                dc_current_regulation_tolerance_a: None,
                dc_peak_current_ripple_a: None,
                dc_energy_to_be_delivered_wh: None,
            }
        );
    }

    /// `update_hlc_ac_parameters`, all three emissions in the C++ order.
    #[test]
    fn a_board_report_names_the_maximum_the_minimum_and_the_general_parameters() {
        assert_eq!(
            capability_emissions(&config(230.0, 4_000.0), caps()),
            vec![
                Effect::HlcUpdate(HlcUpdate::AcMaximumLimits(AcPowerSet {
                    // 230 V times 32 A, on three phases.
                    charge_power: Power {
                        total_w: 22_080.0,
                        l1_w: Some(7_360.0),
                        l2_w: Some(7_360.0),
                        l3_w: Some(7_360.0),
                    },
                    // 230 V times 16 A, on one phase.
                    discharge_power: Some(Power {
                        total_w: 3_680.0,
                        l1_w: Some(3_680.0),
                        l2_w: None,
                        l3_w: None,
                    }),
                })),
                Effect::HlcUpdate(HlcUpdate::AcMinimumLimits(AcPowerSet {
                    // 230 V times 6 A, on three phases.
                    charge_power: Power {
                        total_w: 4_140.0,
                        l1_w: Some(1_380.0),
                        l2_w: Some(1_380.0),
                        l3_w: Some(1_380.0),
                    },
                    // 230 V times 2 A, on one phase.
                    discharge_power: Some(Power {
                        total_w: 460.0,
                        l1_w: Some(460.0),
                        l2_w: None,
                        l3_w: None,
                    }),
                })),
                Effect::HlcUpdate(HlcUpdate::AcParameters(AcParameters {
                    nominal_frequency_hz: 50.0,
                    nominal_voltage_v: 230.0,
                    connectors: vec![AcConnector::SinglePhase, AcConnector::ThreePhase],
                    max_power_asymmetry_w: None,
                    power_ramp_limitation_percent_per_min: None,
                    evse_max_reactive_power_var: Some(4_000.0),
                })),
            ]
        );
    }

    /// The import and the export halves read different capability fields, and a
    /// transposition between them is the failure this pins: it would compile,
    /// because all four are `f64`.
    #[test]
    fn the_charge_envelope_reads_the_import_pair_and_the_discharge_one_the_export_pair() {
        // Import current alone changed, so only the charge halves may move.
        let widened = AcCapabilities {
            max_current_a_import: 64.0,
            ..caps()
        };
        let effects = capability_emissions(&config(230.0, 0.0), widened);
        let baseline = capability_emissions(&config(230.0, 0.0), caps());

        let (maximum, minimum) = (&effects[0], &effects[1]);
        assert_ne!(maximum, &baseline[0], "the charge maximum reads the import");
        assert_eq!(minimum, &baseline[1], "the minimum reads no import maximum");

        // Export current alone changed, so only the discharge halves may move.
        let exporting = AcCapabilities {
            min_current_a_export: 4.0,
            ..caps()
        };
        let effects = capability_emissions(&config(230.0, 0.0), exporting);
        assert_eq!(&effects[0], &baseline[0], "the maximum reads no export minimum");
        assert_ne!(
            &effects[1], &baseline[1],
            "the minimum discharge reads the export minimum"
        );
    }

    /// `:1834-1835`. The two per phase thresholds are not the same threshold.
    #[test]
    fn the_second_phase_needs_two_phases_and_the_third_needs_exactly_three() {
        assert_eq!(
            phase_power(100.0, 1),
            Power {
                total_w: 100.0,
                l1_w: Some(100.0),
                l2_w: None,
                l3_w: None,
            }
        );
        assert_eq!(
            phase_power(100.0, 2),
            Power {
                total_w: 200.0,
                l1_w: Some(100.0),
                l2_w: Some(100.0),
                l3_w: None,
            }
        );
        assert_eq!(
            phase_power(100.0, 3),
            Power {
                total_w: 300.0,
                l1_w: Some(100.0),
                l2_w: Some(100.0),
                l3_w: Some(100.0),
            }
        );
        // A board reporting more phases than the connector standard has is the
        // only input that tells `>= 2` and `== 3` apart.
        assert_eq!(
            phase_power(100.0, 4),
            Power {
                total_w: 400.0,
                l1_w: Some(100.0),
                l2_w: Some(100.0),
                l3_w: None,
            }
        );
    }

    /// `:1864`. Exactly three phases earns the three phase connector.
    #[test]
    fn the_three_phase_connector_is_offered_only_by_a_three_phase_port() {
        for (phases, expected) in [
            (1, vec![AcConnector::SinglePhase]),
            (2, vec![AcConnector::SinglePhase]),
            (3, vec![AcConnector::SinglePhase, AcConnector::ThreePhase]),
        ] {
            let caps = AcCapabilities {
                max_phase_count_import: phases,
                ..caps()
            };
            assert_eq!(
                parameters(&config(230.0, 0.0), caps).connectors,
                expected,
                "{phases} phases"
            );
        }
    }

    /// `:1869-1870`. Zero means "not declared", not "zero var".
    #[test]
    fn an_undeclared_reactive_power_ceiling_is_absent_rather_than_zero() {
        assert_eq!(
            parameters(&config(230.0, 0.0), caps()).evse_max_reactive_power_var,
            None
        );
        assert_eq!(
            parameters(&config(230.0, 1.0), caps()).evse_max_reactive_power_var,
            Some(1.0)
        );
    }

    /// The branch checkbox six asks for: one limit change, two sessions, two
    /// different commands.
    #[test]
    fn the_same_limit_change_reaches_iso2_as_amperes_and_iso20_as_a_target_power() {
        let config = config(230.0, 0.0);

        assert_eq!(
            current_limit_update(&config, caps(), None, 16.0),
            Some(HlcUpdate::AcMaxCurrent(16.0)),
            "no ISO 15118-20 service selected, so the ampere count goes out"
        );
        assert_eq!(
            current_limit_update(&config, caps(), Some(SelectedService::Ac), 16.0),
            // 16 A at 230 V on three phases.
            Some(HlcUpdate::AcTargetPower(Power {
                total_w: 11_040.0,
                l1_w: None,
                l2_w: None,
                l3_w: None,
            })),
            "an ISO 15118-20 AC service is told a power instead"
        );
    }

    /// `:1249-1250` names both AC spellings, so a bidirectional AC session
    /// takes the same arm as a plain one.
    #[test]
    fn a_bidirectional_ac_service_is_told_a_target_power_like_a_plain_one() {
        let config = config(230.0, 0.0);
        assert_eq!(
            current_limit_update(&config, caps(), Some(SelectedService::AcBpt), 16.0),
            current_limit_update(&config, caps(), Some(SelectedService::Ac), 16.0)
        );
    }

    /// Every other service takes neither arm. The DC ones are told their limits
    /// through `dc_limits`; the DER ones are the C++ gap named on
    /// `is_ac_target_power_service`.
    #[test]
    fn no_other_selected_service_hears_an_ac_limit() {
        let config = config(230.0, 0.0);
        for service in [
            SelectedService::Dc,
            SelectedService::Wpt,
            SelectedService::DcAcdp,
            SelectedService::DcBpt,
            SelectedService::DcAcdpBpt,
            SelectedService::Mcs,
            SelectedService::McsBpt,
            SelectedService::AcDerIec,
            SelectedService::AcDerSae,
            SelectedService::Internet,
            SelectedService::ParkingStatus,
        ] {
            assert_eq!(
                current_limit_update(&config, caps(), Some(service), 16.0),
                None,
                "{service:?}"
            );
        }
    }

    /// `:1244`. The guard belongs to the ISO 15118-2 arm alone.
    #[test]
    fn a_discharge_request_is_dropped_without_a_service_and_carried_with_one() {
        let config = config(230.0, 0.0);

        assert_eq!(
            current_limit_update(&config, caps(), None, -16.0),
            None,
            "an ampere count on the wire cannot carry a direction"
        );
        assert_eq!(
            current_limit_update(&config, caps(), Some(SelectedService::AcBpt), -16.0),
            Some(HlcUpdate::AcTargetPower(Power {
                total_w: -11_040.0,
                ..Power::default()
            })),
            "a target power can, so the guard does not apply to it"
        );
    }

    /// Zero is on the offered side of the guard, which is what withdraws an
    /// ISO 15118-2 offer rather than leaving the last one standing.
    #[test]
    fn a_zero_limit_is_still_offered_to_an_iso2_session() {
        assert_eq!(
            current_limit_update(&config(230.0, 0.0), caps(), None, 0.0),
            Some(HlcUpdate::AcMaxCurrent(0.0))
        );
    }

    /// The target power reads the import phase count, so a single phase port
    /// converts the same ampere count into a third of the watts.
    #[test]
    fn the_target_power_scales_with_the_import_phase_count() {
        let single = AcCapabilities {
            max_phase_count_import: 1,
            ..caps()
        };
        assert_eq!(
            current_limit_update(&config(230.0, 0.0), single, Some(SelectedService::Ac), 16.0),
            Some(HlcUpdate::AcTargetPower(Power {
                total_w: 3_680.0,
                ..Power::default()
            }))
        );
    }

    /// `:1167`. Both halves of the conjunction are needed.
    #[test]
    fn the_present_power_needs_both_a_reading_and_a_selected_service() {
        let reading = Power {
            total_w: 3_300.0,
            l1_w: Some(1_100.0),
            l2_w: Some(1_100.0),
            l3_w: Some(1_100.0),
        };

        assert_eq!(
            present_power_update(Some(SelectedService::Ac), Some(reading)),
            Some(HlcUpdate::AcPresentPower(reading))
        );
        assert_eq!(
            present_power_update(None, Some(reading)),
            None,
            "no ISO 15118-20 service selected"
        );
        assert_eq!(
            present_power_update(Some(SelectedService::Ac), None),
            None,
            "the meter reported no power figure"
        );
    }

    /// The gate is any ISO 15118-20 service and not an AC one, unlike the limit
    /// emission above. The two gates genuinely differ in the C++ and the
    /// difference is observable.
    #[test]
    fn a_dc_session_is_told_the_present_power_too() {
        let reading = Power {
            total_w: 50_000.0,
            ..Power::default()
        };
        assert_eq!(
            present_power_update(Some(SelectedService::Dc), Some(reading)),
            Some(HlcUpdate::AcPresentPower(reading))
        );
    }
}
