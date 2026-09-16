// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

//! The everestrs entrypoint.
//!
//! Deliberately thin. Everything here needs the generated bindings, which need a
//! CMake build, so nothing here is unit testable and nothing here decides
//! anything. Callbacks append an `Event` and return. Effects are performed by
//! `EverestEffects` and nothing else. Both are mechanical translations between
//! the generated types and `core`.

#![allow(non_snake_case)]

include!(concat!(env!("OUT_DIR"), "/generated.rs"));

use std::sync::{Arc, Mutex, OnceLock};
use std::time::Duration;

use generated::{
    types, AcRcdClientSubscriber, AuthTokenProviderServiceSubscriber,
    ConnectorLockClientSubscriber, Context, DcExternalDerateServiceSubscriber,
    EnergyServiceSubscriber, EvseBoardSupportClientSubscriber, EvseManagerServiceSubscriber,
    Iso15118ChargerClientSubscriber, IsolationMonitorClientSubscriber, KvsClientSubscriber, Module,
    ModuleConfig, ModulePublisher, OnReadySubscriber, OverVoltageMonitorClientSubscriber,
    PowerSupplyDcClientSubscriber, PowermeterClientSubscriber, SlacClientSubscriber,
    UkRandomDelayServiceSubscriber,
};
use serde_json::json;

use RsEvseManager::boundary::random::OsBytes;
use RsEvseManager::boundary::replies::Replies;
use RsEvseManager::boundary::{self, EffectRunner, EventSender, LoopHandle};
use RsEvseManager::core::auth::{Auth, TariffMessages};
use RsEvseManager::core::config::{
    reject_requested_workarounds, resolve, ChargeMode, Deployment, Mapping, PhaseRotation,
    Settings, SuppliedKey, Wiring,
};
use RsEvseManager::core::derate::ExternalDerating;
use RsEvseManager::core::effect::{
    ChargingPhase, CpState, Effect, EffectOutcome, ErrorReport, HlcUpdate, SessionEventReport,
    SessionPayload, SlacUpdate, SupplyMode,
};
use RsEvseManager::core::enable::{EnableEntry, EnableSource, EnableState};
use RsEvseManager::core::energy::flow_request::{
    EntryTime, EvseState, FlowRequest, IntegerWithSource, LimitsReq, NodeType, NumberWithSource,
    Schedule, ScheduleReqEntry, SetpointValue,
};
use RsEvseManager::core::energy::enforce::{
    EnforcedLimits, LimitsRes, PricePerKwh, ScheduleResEntry,
};
use RsEvseManager::core::energy::random_delay::{CountDown, RandomDelay, RandomDelaySettings};
use RsEvseManager::core::energy::{EnergyConfig, EnergyTree, NodeUuid};
use RsEvseManager::core::event::{
    BspEvent, Command, CpEvent, ErrorEvent, ErrorSource, Event, HardwareCapabilities, HlcEvent,
    IsolationReading, MeterReading, PowerSupplyCapabilities, PowermeterCapabilities, Severity,
    V2gMessage,
};
use RsEvseManager::core::faults::Faults;
use RsEvseManager::core::hlc::{
    AcConnector, AcParameters, AcPowerSet, BptChannel, BptSetup, CarManufacturer,
    DynamicModeRequest, EvInfo, EvMaximumLimits, GeneratorMode, GridCodeIslandingDetection,
    HlcConfig, MinimumLimits, OpaqueToken, PaymentOption, PlugAndChargeConfiguration, Power,
    SaeBidiMode, SelectedService, UpdateRefusal,
};
use RsEvseManager::core::persist::SessionStore;
use RsEvseManager::core::session::{
    EnableScope, EnergyTransferMode, PauseReason, SessionEvent, StartSessionReason, StopReason,
    StopTransactionReason,
};
use RsEvseManager::core::session_id::SessionIds;
use RsEvseManager::core::session_log::{self, SessionLogEffect, SessionLogger};
use RsEvseManager::core::soft_oc::{Detection, PhaseCurrents, SoftOverCurrentConfig};
use RsEvseManager::core::token::{IdTag, IdTokenType};
use RsEvseManager::core::{Core, CoreParts, Metering, ReadyGate};

/// The `powermeter` interface is required twice. One `Intake` serves both slots,
/// so `Context::name` is what tells them apart. Only the grid side feeds the
/// core, matching the C++ energy management input.
const POWERMETER_GRID_SIDE: &str = "powermeter_grid_side";
const POWERMETER_CAR_SIDE: &str = "powermeter_car_side";

/// Turns a generated error enum back into its wire string. The variants carry
/// `#[serde(rename = "group/name")]` and the framework deserializes them with
/// `serde_yaml::from_str`, so `serde_yaml::to_string` is the exact inverse. It
/// terminates the scalar with a newline, hence the trim.
/// The wire spelling of an ISO 15118 message id,
/// `types::iso15118::v2g_message_id_to_string`.
///
/// Serialized rather than matched over the 80 odd variants of the generated
/// enum, so a message the interface gains is named correctly here without an
/// edit, and so the spelling here cannot drift from the wire. The direction of
/// a message is decided from this string
/// (`core::event::V2gMessage::from_vehicle`), so a wrong name would put a
/// vehicle message in the EVSE column: an id that cannot be named is dropped to
/// an empty string, which reads as an EVSE message but names nothing, rather
/// than being guessed at.
fn v2g_message_id(id: &types::iso15118::V2gMessageId) -> String {
    match ::everestrs::serde_json::to_value(id) {
        Ok(::everestrs::serde_json::Value::String(name)) => name,
        other => {
            log::error!("cannot name an ISO 15118 message id: {other:?}");
            String::new()
        }
    }
}

fn error_type_string<T: everestrs::serde::Serialize>(error_type: &T) -> String {
    match everestrs::serde_yaml::to_string(error_type) {
        Ok(text) => text.trim().to_owned(),
        Err(error) => {
            log::error!("cannot serialize error type: {error}");
            String::new()
        }
    }
}

/// One outbound error raise, as the framework's `ErrorType`.
///
/// `description`, `message` and `vendor_id` are three different values that an
/// OCPP 1.6 `StatusNotification` renders into three different fields, so each
/// comes from its own field on the report and none is filled from another
/// (`get_error_info` in `modules/EVSE/OCPP/OCPP.cpp`).
fn raised_error<T>(error_type: T, report: &ErrorReport) -> ::everestrs::ErrorType<T> {
    ::everestrs::ErrorType {
        error_type,
        sub_type: report.sub_type.clone(),
        description: report.description.clone(),
        message: report.message.clone(),
        vendor_id: report.vendor_id.clone(),
        severity: severity_to(report.severity),
    }
}

/// One inbound framework error, as the core's `Event::Error`.
///
/// Fourteen subscriber handlers used to spell this out; the seven interfaces
/// that can raise a charging preventing error each have a raise and a clear, and
/// the two halves differ only in the flag. Writing the six fields once is what
/// keeps a field that arrives on the wire from being filled at six sites and
/// dropped at eight.
fn error_event<T: everestrs::serde::Serialize>(
    source: ErrorSource,
    error: ::everestrs::ErrorType<T>,
    raised: bool,
) -> Event {
    Event::Error(ErrorEvent {
        source,
        error_type: error_type_string(&error.error_type),
        sub_type: error.sub_type,
        vendor_id: error.vendor_id,
        severity: severity_from(error.severity),
        raised,
    })
}

/// The errors this module raises on its own `evse_manager` interface, matched
/// against the wire string the core carries.
///
/// The core names errors by their wire string, which is what an inbound error
/// also arrives as, so a lookup keyed on `error_type_string` cannot drift from
/// the inbound direction the way a hand written table would.
const RAISABLE_ERRORS: [generated::errors::evse_manager::Error; 5] = [
    generated::errors::evse_manager::Error::EvseManager(
        generated::errors::evse_manager::EvseManagerError::Inoperative,
    ),
    generated::errors::evse_manager::Error::EvseManager(
        generated::errors::evse_manager::EvseManagerError::Mrec9AuthorizationTimeout,
    ),
    generated::errors::evse_manager::Error::EvseManager(
        generated::errors::evse_manager::EvseManagerError::Internal,
    ),
    generated::errors::evse_manager::Error::EvseManager(
        generated::errors::evse_manager::EvseManagerError::PowermeterTransactionStartFailed,
    ),
    generated::errors::evse_manager::Error::EvseManager(
        generated::errors::evse_manager::EvseManagerError::Mrec4OverCurrentFailure,
    ),
];

fn raisable_error(error_type: &str) -> Option<generated::errors::evse_manager::Error> {
    RAISABLE_ERRORS
        .iter()
        .find(|candidate| error_type_string(candidate) == error_type)
        .cloned()
}

/// The inverse of `severity_from`, for the errors the module raises itself.
fn severity_to(severity: Severity) -> everestrs::ErrorSeverity {
    match severity {
        Severity::Low => everestrs::ErrorSeverity::Low,
        Severity::Medium => everestrs::ErrorSeverity::Medium,
        Severity::High => everestrs::ErrorSeverity::High,
    }
}

/// Same trick for the string valued enums the core carries as `String`.
fn wire_string<T: everestrs::serde::Serialize>(value: &T) -> String {
    match everestrs::serde_yaml::to_string(value) {
        Ok(text) => text.trim().to_owned(),
        Err(error) => {
            log::error!("cannot serialize enum: {error}");
            String::new()
        }
    }
}

/// The wire spelling of an energy transfer mode.
///
/// Total in both directions, because
/// `update_allowed_energy_transfer_modes` relays a list another module chose
/// and a value with no local spelling would vanish from what the vehicle is
/// then offered. `transfer_mode_from` and `transfer_mode_enum` are inverses and
/// a test pins that over every wire value.
fn transfer_mode_enum(mode: EnergyTransferMode) -> types::iso15118::EnergyTransferMode {
    use types::iso15118::EnergyTransferMode as Wire;
    match mode {
        EnergyTransferMode::AcSinglePhase => Wire::AC_single_phase_core,
        EnergyTransferMode::AcTwoPhase => Wire::AC_two_phase,
        EnergyTransferMode::AcThreePhase => Wire::AC_three_phase_core,
        EnergyTransferMode::DcCore => Wire::DC_core,
        EnergyTransferMode::DcExtended => Wire::DC_extended,
        EnergyTransferMode::DcComboCore => Wire::DC_combo_core,
        EnergyTransferMode::DcUnique => Wire::DC_unique,
        EnergyTransferMode::Dc => Wire::DC,
        EnergyTransferMode::AcBpt => Wire::AC_BPT,
        EnergyTransferMode::AcBptDer => Wire::AC_BPT_DER,
        EnergyTransferMode::AcDerIec => Wire::AC_DER_IEC,
        EnergyTransferMode::AcDerSae => Wire::AC_DER_SAE,
        EnergyTransferMode::DcBpt => Wire::DC_BPT,
        EnergyTransferMode::DcAcdp => Wire::DC_ACDP,
        EnergyTransferMode::DcAcdpBpt => Wire::DC_ACDP_BPT,
        EnergyTransferMode::Wpt => Wire::WPT,
        EnergyTransferMode::Mcs => Wire::MCS,
        EnergyTransferMode::McsBpt => Wire::MCS_BPT,
    }
}

fn transfer_mode_from(wire: &types::iso15118::EnergyTransferMode) -> EnergyTransferMode {
    use types::iso15118::EnergyTransferMode as Wire;
    match wire {
        Wire::AC_single_phase_core => EnergyTransferMode::AcSinglePhase,
        Wire::AC_two_phase => EnergyTransferMode::AcTwoPhase,
        Wire::AC_three_phase_core => EnergyTransferMode::AcThreePhase,
        Wire::DC_core => EnergyTransferMode::DcCore,
        Wire::DC_extended => EnergyTransferMode::DcExtended,
        Wire::DC_combo_core => EnergyTransferMode::DcComboCore,
        Wire::DC_unique => EnergyTransferMode::DcUnique,
        Wire::DC => EnergyTransferMode::Dc,
        Wire::AC_BPT => EnergyTransferMode::AcBpt,
        Wire::AC_BPT_DER => EnergyTransferMode::AcBptDer,
        Wire::AC_DER_IEC => EnergyTransferMode::AcDerIec,
        Wire::AC_DER_SAE => EnergyTransferMode::AcDerSae,
        Wire::DC_BPT => EnergyTransferMode::DcBpt,
        Wire::DC_ACDP => EnergyTransferMode::DcAcdp,
        Wire::DC_ACDP_BPT => EnergyTransferMode::DcAcdpBpt,
        Wire::WPT => EnergyTransferMode::Wpt,
        Wire::MCS => EnergyTransferMode::Mcs,
        Wire::MCS_BPT => EnergyTransferMode::McsBpt,
    }
}

fn sae_bidi_mode_enum(mode: SaeBidiMode) -> types::iso15118::SaeJ2847BidiMode {
    match mode {
        SaeBidiMode::None => types::iso15118::SaeJ2847BidiMode::None,
        SaeBidiMode::V2g => types::iso15118::SaeJ2847BidiMode::V2G,
        SaeBidiMode::V2h => types::iso15118::SaeJ2847BidiMode::V2H,
    }
}

/// The inbound direction. The stack names the failure and this module attaches
/// the session identity and republishes it (`EvseManager.cpp:365-370`), so the
/// reason travels through unchanged and the two mappers are each other's
/// inverse.
fn hlc_session_failure_from(
    reason: &types::evse_manager::HlcSessionFailedReasonEnum,
) -> RsEvseManager::core::event::HlcSessionFailure {
    use types::evse_manager::HlcSessionFailedReasonEnum as Wire;
    use RsEvseManager::core::event::HlcSessionFailure as Reason;
    match reason {
        Wire::ProtocolNegotiationFailed => Reason::ProtocolNegotiationFailed,
        Wire::AuthorizationFailed => Reason::AuthorizationFailed,
        Wire::ChargingParametersNotAccepted => Reason::ChargingParametersNotAccepted,
        Wire::EnergyTransferSetupFailed => Reason::EnergyTransferSetupFailed,
        Wire::ChargingInterrupted => Reason::ChargingInterrupted,
        Wire::FailedTLSHandshake => Reason::FailedTlsHandshake,
        Wire::UnexpectedSessionEnd => Reason::UnexpectedSessionEnd,
    }
}

/// Whether SLAC matching has begun, which is the one bit
/// `EvseManager.cpp:1215-1225` takes from the SLAC state: it tests for
/// `UNMATCHED` and treats every other state as matching started, so `MATCHING`
/// and `MATCHED` are indistinguishable to the charger.
fn matching_started_from(state: &types::slac::State) -> bool {
    !matches!(state, types::slac::State::UNMATCHED)
}

/// Whether the SLAC link is matched, which is the narrower half of the same
/// handler: `EvseManager::ready`'s `subscribe_state` arm on the SLAC requirement passes `true` to
/// `Charger::set_slac_matched` in the `MATCHED` arm alone and `false` in both
/// others, so `MATCHING` is matching started and not matched.
fn slac_matched_from(state: &types::slac::State) -> bool {
    matches!(state, types::slac::State::MATCHED)
}

fn hlc_session_failure_enum(
    reason: RsEvseManager::core::event::HlcSessionFailure,
) -> types::evse_manager::HlcSessionFailedReasonEnum {
    use types::evse_manager::HlcSessionFailedReasonEnum as Wire;
    use RsEvseManager::core::event::HlcSessionFailure as Reason;
    match reason {
        Reason::ProtocolNegotiationFailed => Wire::ProtocolNegotiationFailed,
        Reason::AuthorizationFailed => Wire::AuthorizationFailed,
        Reason::ChargingParametersNotAccepted => Wire::ChargingParametersNotAccepted,
        Reason::EnergyTransferSetupFailed => Wire::EnergyTransferSetupFailed,
        Reason::ChargingInterrupted => Wire::ChargingInterrupted,
        Reason::FailedTlsHandshake => Wire::FailedTLSHandshake,
        Reason::UnexpectedSessionEnd => Wire::UnexpectedSessionEnd,
    }
}

/// The wire carries only the two the module originates. The interface declares
/// five, and the other three are reported by the board support layer rather
/// than by this module, so mapping them here would offer a route no core signal
/// can reach.
fn evse_error_enum(error: RsEvseManager::core::effect::EvseError) -> types::iso15118::EvseError {
    use RsEvseManager::core::effect::EvseError as Cause;
    match error {
        Cause::EmergencyShutdown => types::iso15118::EvseError::Error_EmergencyShutdown,
        Cause::UtilityInterruptEvent => types::iso15118::EvseError::Error_UtilityInterruptEvent,
    }
}

/// The isolation status as the wire spells it.
///
/// Three of the wire's five, because the core enum carries three: `Invalid` and
/// `Warning` have no producer in `modules/EVSE/EvseManager/` either. Written as
/// an exhaustive match rather than with a fallback arm, so widening the core
/// enum is a compile failure here instead of a silent collapse onto whichever
/// value the fallback named.
fn isolation_status_enum(
    status: RsEvseManager::core::hlc::cable_check::IsolationStatus,
) -> types::iso15118::IsolationStatus {
    use RsEvseManager::core::hlc::cable_check::IsolationStatus as Status;
    match status {
        Status::Valid => types::iso15118::IsolationStatus::Valid,
        Status::Fault => types::iso15118::IsolationStatus::Fault,
        Status::NoImd => types::iso15118::IsolationStatus::No_IMD,
    }
}

/// The verdict as the wire spells it, both ways.
///
/// Every value, because the module relays a refusal it did not decide
/// (`evse/evse_managerImpl.cpp:451`). The two functions are the same table read
/// in opposite directions, so a value added to one and forgotten in the other
/// does not compile.
fn authorization_status_of(
    status: types::authorization::AuthorizationStatus,
) -> RsEvseManager::core::auth::AuthorizationStatus {
    use types::authorization::AuthorizationStatus as Wire;
    use RsEvseManager::core::auth::AuthorizationStatus as Verdict;
    match status {
        Wire::Accepted => Verdict::Accepted,
        Wire::Blocked => Verdict::Blocked,
        Wire::ConcurrentTx => Verdict::ConcurrentTx,
        Wire::Expired => Verdict::Expired,
        Wire::Invalid => Verdict::Invalid,
        Wire::NoCredit => Verdict::NoCredit,
        Wire::NotAllowedTypeEVSE => Verdict::NotAllowedTypeEvse,
        Wire::NotAtThisLocation => Verdict::NotAtThisLocation,
        Wire::NotAtThisTime => Verdict::NotAtThisTime,
        Wire::Unknown => Verdict::Unknown,
        Wire::PinRequired => Verdict::PinRequired,
        Wire::Timeout => Verdict::Timeout,
    }
}

fn authorization_status_enum(
    status: RsEvseManager::core::auth::AuthorizationStatus,
) -> types::authorization::AuthorizationStatus {
    use types::authorization::AuthorizationStatus as Wire;
    use RsEvseManager::core::auth::AuthorizationStatus as Verdict;
    match status {
        Verdict::Accepted => Wire::Accepted,
        Verdict::Blocked => Wire::Blocked,
        Verdict::ConcurrentTx => Wire::ConcurrentTx,
        Verdict::Expired => Wire::Expired,
        Verdict::Invalid => Wire::Invalid,
        Verdict::NoCredit => Wire::NoCredit,
        Verdict::NotAllowedTypeEvse => Wire::NotAllowedTypeEVSE,
        Verdict::NotAtThisLocation => Wire::NotAtThisLocation,
        Verdict::NotAtThisTime => Wire::NotAtThisTime,
        Verdict::Unknown => Wire::Unknown,
        Verdict::PinRequired => Wire::PinRequired,
        Verdict::Timeout => Wire::Timeout,
    }
}

fn certificate_status_of(
    status: types::authorization::CertificateStatus,
) -> RsEvseManager::core::auth::CertificateStatus {
    use types::authorization::CertificateStatus as Wire;
    use RsEvseManager::core::auth::CertificateStatus as Verdict;
    match status {
        Wire::Accepted => Verdict::Accepted,
        Wire::SignatureError => Verdict::SignatureError,
        Wire::CertificateExpired => Verdict::CertificateExpired,
        Wire::CertificateRevoked => Verdict::CertificateRevoked,
        Wire::NoCertificateAvailable => Verdict::NoCertificateAvailable,
        Wire::CertChainError => Verdict::CertChainError,
        Wire::ContractCancelled => Verdict::ContractCancelled,
    }
}

fn certificate_status_enum(
    status: RsEvseManager::core::auth::CertificateStatus,
) -> types::authorization::CertificateStatus {
    use types::authorization::CertificateStatus as Wire;
    use RsEvseManager::core::auth::CertificateStatus as Verdict;
    match status {
        Verdict::Accepted => Wire::Accepted,
        Verdict::SignatureError => Wire::SignatureError,
        Verdict::CertificateExpired => Wire::CertificateExpired,
        Verdict::CertificateRevoked => Wire::CertificateRevoked,
        Verdict::NoCertificateAvailable => Wire::NoCertificateAvailable,
        Verdict::CertChainError => Wire::CertChainError,
        Verdict::ContractCancelled => Wire::ContractCancelled,
    }
}

/// The wire token the core asked to be offered.
///
/// Filled by name in both arms. The autocharge arm sets the two constants the
/// C++ sets beside the identity (`EvseManager.cpp:44-47`): the token type is a
/// MAC address and the authorization type is autocharge. The plug and charge
/// arm starts from the token the stack handed over and replaces the connector
/// list, which is the one field the C++ changes (`:1033-1034`).
fn provided_token_payload(
    token: &RsEvseManager::core::hlc::ProvidedToken,
) -> Option<types::authorization::ProvidedIdToken> {
    use RsEvseManager::core::hlc::ProvidedToken as Offer;
    match token {
        Offer::Autocharge {
            id_token,
            connectors,
        } => Some(types::authorization::ProvidedIdToken {
            authorization_type: types::authorization::AuthorizationType::Autocharge,
            certificate: None,
            connectors: Some(connectors.clone()),
            id_token: types::authorization::IdToken {
                additional_info: None,
                r#type: types::authorization::IdTokenType::MacAddress,
                value: id_token.clone(),
            },
            iso_15118_certificate_hash_data: None,
            parent_id_token: None,
            prevalidated: None,
            request_id: None,
        }),
        Offer::PlugAndCharge { token, connectors } => {
            match ::everestrs::serde_json::from_value::<types::authorization::ProvidedIdToken>(
                token.payload().clone(),
            ) {
                Ok(mut relayed) => {
                    relayed.connectors = Some(connectors.clone());
                    Some(relayed)
                }
                // The payload is this boundary's own serialization of the same
                // type, so this cannot fire without the interface having
                // changed underneath a running process.
                Err(error) => {
                    log::error!("a plug and charge token could not be relayed: {error}");
                    None
                }
            }
        }
    }
}

/// Filled by name. The generated struct orders its fields alphabetically and
/// `grid_code_detection` is optional, so a positional literal would compile
/// with the channel and the generator mode transposed if their types ever
/// converge.
fn bpt_setup_payload(setup: &BptSetup) -> types::iso15118::BptSetup {
    types::iso15118::BptSetup {
        bpt_channel: match setup.channel {
            BptChannel::Unified => types::iso15118::BptChannel::Unified,
            BptChannel::Separated => types::iso15118::BptChannel::Separated,
        },
        generator_mode: match setup.generator_mode {
            GeneratorMode::GridFollowing => types::iso15118::GeneratorMode::GridFollowing,
            GeneratorMode::GridForming => types::iso15118::GeneratorMode::GridForming,
        },
        grid_code_detection: setup.grid_code_detection.map(|method| match method {
            GridCodeIslandingDetection::Active => {
                types::iso15118::GridCodeIslandingDetectionMethod::Active
            }
            GridCodeIslandingDetection::Passive => {
                types::iso15118::GridCodeIslandingDetectionMethod::Passive
            }
        }),
    }
}

/// The capability report on its way back out to the ISO 15118 stack.
///
/// Filled by name, twenty four fields deep. Deliberately **not** written as a
/// hand rolled positional constructor: every one of the nominal fields is
/// `Option<f64>` and they differ only in `min`, `max`, `export` and `import`,
/// so a transposition compiles, serializes and reaches the vehicle.
///
/// The best available pin on that is the compiler, and it is only available
/// where the types differ, which here they do not. The next best is this
/// function being the single place the mapping is written, so a review of one
/// screen covers every field.
fn power_supply_capabilities_payload(
    caps: &PowerSupplyCapabilities,
) -> types::power_supply_DC::Capabilities {
    types::power_supply_DC::Capabilities {
        bidirectional: caps.bidirectional,
        max_export_voltage_v: caps.max_export_voltage_v,
        min_export_voltage_v: caps.min_export_voltage_v,
        max_export_current_a: caps.max_export_current_a,
        min_export_current_a: caps.min_export_current_a,
        max_export_power_w: caps.max_export_power_w,
        current_regulation_tolerance_a: caps.current_regulation_tolerance_a,
        peak_current_ripple_a: caps.peak_current_ripple_a,
        max_import_voltage_v: caps.max_import_voltage_v,
        min_import_voltage_v: caps.min_import_voltage_v,
        max_import_current_a: caps.max_import_current_a,
        min_import_current_a: caps.min_import_current_a,
        max_import_power_w: caps.max_import_power_w,
        conversion_efficiency_export: caps.conversion_efficiency_export,
        conversion_efficiency_import: caps.conversion_efficiency_import,
        nominal_max_export_current_a: caps.nominal_max_export_current_a,
        nominal_max_export_power_w: caps.nominal_max_export_power_w,
        nominal_max_export_voltage_v: caps.nominal_max_export_voltage_v,
        nominal_max_import_current_a: caps.nominal_max_import_current_a,
        nominal_max_import_power_w: caps.nominal_max_import_power_w,
        nominal_max_import_voltage_v: caps.nominal_max_import_voltage_v,
        nominal_min_export_current_a: caps.nominal_min_export_current_a,
        nominal_min_export_voltage_v: caps.nominal_min_export_voltage_v,
        nominal_min_import_current_a: caps.nominal_min_import_current_a,
        nominal_min_import_voltage_v: caps.nominal_min_import_voltage_v,
    }
}

/// The minimum limit set on its way out (`call_update_dc_minimum_limits`).
///
/// The three required fields and the optional discharge pair, filled by name.
/// The three required ones are adjacent `f64` differing only in `current`,
/// `voltage` and `power`, and the wire order is alphabetical while the C++
/// assigns them current, voltage, power.
fn minimum_limits_payload(limits: &MinimumLimits) -> types::iso15118::DcEvseMinimumLimits {
    types::iso15118::DcEvseMinimumLimits {
        evse_minimum_current_limit: limits.minimum_current_a,
        evse_minimum_voltage_limit: limits.minimum_voltage_v,
        evse_minimum_power_limit: limits.minimum_power_w,
        evse_minimum_discharge_current_limit: limits.minimum_discharge_current_a,
        evse_minimum_discharge_power_limit: limits.minimum_discharge_power_w,
    }
}

fn maximum_limits_payload(limits: &RsEvseManager::core::hlc::MaximumLimits) -> types::iso15118::DcEvseMaximumLimits {
    types::iso15118::DcEvseMaximumLimits {
        evse_maximum_current_limit: limits.maximum_current_a,
        evse_maximum_voltage_limit: limits.maximum_voltage_v,
        evse_maximum_power_limit: limits.maximum_power_w,
        evse_maximum_discharge_current_limit: limits.maximum_discharge_current_a,
        evse_maximum_discharge_power_limit: limits.maximum_discharge_power_w,
    }
}

/// `types::units::Power` on its way in, from the meter.
///
/// Filled by name in both directions. The three phase figures are
/// `Option<f64>` and differ only in a digit, which is exactly the shape that
/// transposes silently: `l_2` read into `l3_w` compiles and every total still
/// adds up.
fn power_from(power: types::units::Power) -> Power {
    Power {
        total_w: power.total,
        l1_w: power.l_1,
        l2_w: power.l_2,
        l3_w: power.l_3,
    }
}

/// The same type on its way out.
fn power_payload(power: &Power) -> types::units::Power {
    types::units::Power {
        total: power.total_w,
        l_1: power.l1_w,
        l_2: power.l2_w,
        l_3: power.l3_w,
    }
}

/// An AC power envelope on its way out. One helper for both wire types, which
/// the two call sites keep apart by naming the constructor they hand it to.
fn ac_power_set_payload(set: &AcPowerSet) -> (types::units::Power, Option<types::units::Power>) {
    (
        power_payload(&set.charge_power),
        set.discharge_power.as_ref().map(power_payload),
    )
}

/// `types::iso15118::Connector`, narrowed on the way in and widened here.
fn ac_connector_enum(connector: AcConnector) -> types::iso15118::Connector {
    match connector {
        AcConnector::SinglePhase => types::iso15118::Connector::SinglePhase,
        AcConnector::ThreePhase => types::iso15118::Connector::ThreePhase,
    }
}

/// The service the vehicle selected, on its way in.
///
/// Total over the wire enum, so a value added upstream fails to compile here
/// rather than folding into a catch-all that changes what the two limit
/// emissions decide.
fn selected_service_from(category: &types::iso15118::ServiceCategory) -> SelectedService {
    use types::iso15118::ServiceCategory as Wire;
    match category {
        Wire::AC => SelectedService::Ac,
        Wire::DC => SelectedService::Dc,
        Wire::WPT => SelectedService::Wpt,
        Wire::DC_ACDP => SelectedService::DcAcdp,
        Wire::AC_BPT => SelectedService::AcBpt,
        Wire::DC_BPT => SelectedService::DcBpt,
        Wire::DC_ACDP_BPT => SelectedService::DcAcdpBpt,
        Wire::MCS => SelectedService::Mcs,
        Wire::MCS_BPT => SelectedService::McsBpt,
        Wire::AC_DER_IEC => SelectedService::AcDerIec,
        Wire::AC_DER_SAE => SelectedService::AcDerSae,
        Wire::Internet => SelectedService::Internet,
        Wire::ParkingStatus => SelectedService::ParkingStatus,
    }
}

/// The general AC announcement on its way out.
///
/// Filled by name, and the two `f64` it carries are a frequency in hertz and a
/// voltage in volts sitting next to each other. The wire struct orders its
/// fields alphabetically, `connectors` first, while the C++ brace initializes
/// them frequency, voltage, connectors (`EvseManager.cpp:1867-1870`), so a
/// positional port would put the voltage in the frequency and still compile.
fn ac_parameters_payload(parameters: &AcParameters) -> types::iso15118::AcParameters {
    types::iso15118::AcParameters {
        nominal_frequency: parameters.nominal_frequency_hz,
        nominal_voltage: parameters.nominal_voltage_v,
        connectors: parameters
            .connectors
            .iter()
            .copied()
            .map(ac_connector_enum)
            .collect(),
        max_power_asymmetry: parameters.max_power_asymmetry_w,
        power_ramp_limitation: parameters.power_ramp_limitation_percent_per_min,
        evse_max_reactive_power: parameters.evse_max_reactive_power_var,
    }
}

/// The ISO 15118-20 target on its way out (`call_update_ac_target_values`).
///
/// The active power is the only field with a producer. The other two are the
/// C++ `TODO(SL)` pair at `EvseManager.cpp:1255-1256`, and they are left absent
/// rather than zeroed: a zero target frequency is a declaration that the EVSE
/// wants zero hertz, and a missing one is the EVSE declining to dictate a
/// frequency at all. The wire type makes both optional precisely so those two
/// can be told apart.
fn ac_target_values_payload(power: &Power) -> types::iso15118::AcTargetValues {
    types::iso15118::AcTargetValues {
        target_active_power: power_payload(power),
        target_frequency: None,
        target_reactive_power: None,
    }
}

fn payment_option_enum(option: PaymentOption) -> types::iso15118::PaymentOption {
    match option {
        PaymentOption::Contract => types::iso15118::PaymentOption::Contract,
        PaymentOption::ExternalPayment => types::iso15118::PaymentOption::ExternalPayment,
    }
}

/// Read by name. The wire struct carries three optional booleans in
/// alphabetical order and two of them are opposite halves of the same feature,
/// so a positional read would compile with them transposed.
fn plug_and_charge_from(
    wire: &types::evse_manager::PlugAndChargeConfiguration,
) -> PlugAndChargeConfiguration {
    PlugAndChargeConfiguration {
        enabled: wire.pnc_enabled,
        central_validation_allowed: wire.central_contract_validation_allowed,
        certificate_installation_enabled: wire.contract_certificate_installation_enabled,
    }
}

fn update_refusal_enum(
    refusal: UpdateRefusal,
) -> types::evse_manager::UpdateAllowedEnergyTransferModesResult {
    use types::evse_manager::UpdateAllowedEnergyTransferModesResult as Result;
    match refusal {
        UpdateRefusal::IncompatibleEnergyTransfer => Result::IncompatibleEnergyTransfer,
    }
}

/// `pwm_on` is declared in percent (`interfaces/evse_board_support.yaml`,
/// maximum 100) while the core carries a 0..1 duty cycle fraction. The C++ does
/// the same conversion at `IECStateMachine.cpp:374`, which calls
/// `call_pwm_on(value * 100)`.
fn pwm_percent(duty: f64) -> f64 {
    duty * 100.0
}

fn cp_event_from(event: types::board_support_common::BspEventAutoGenEvent) -> CpEvent {
    use types::board_support_common::BspEventAutoGenEvent as Wire;
    match event {
        Wire::A => CpEvent::A,
        Wire::B => CpEvent::B,
        Wire::C => CpEvent::C,
        Wire::D => CpEvent::D,
        Wire::E => CpEvent::E,
        Wire::F => CpEvent::F,
        Wire::PowerOn => CpEvent::PowerOn,
        Wire::PowerOff => CpEvent::PowerOff,
        Wire::Disconnected => CpEvent::Disconnected,
    }
}

/// The proximity pilot reports a cable ampacity class, not an ampere value.
/// `A_63_3ph_70_1ph` is phase count dependent and resolves to the lower of the
/// two here, because the phase count is not known at this point and under
/// reporting a cable rating is the safe direction.
fn pp_ampacity_amps(ampacity: types::board_support_common::ProximityPilotAutoGenAmpacity) -> f64 {
    use types::board_support_common::ProximityPilotAutoGenAmpacity as Wire;
    match ampacity {
        Wire::None => 0.0,
        Wire::A_13 => 13.0,
        Wire::A_20 => 20.0,
        Wire::A_32 => 32.0,
        Wire::A_63_3ph_70_1ph => 63.0,
    }
}

/// The reported severity decides whether a blocking fault forces an emergency
/// shutdown or an ordinary error shutdown (`ErrorHandling.cpp:247-251`), so it is
/// carried through rather than assumed.
fn severity_from(severity: everestrs::ErrorSeverity) -> Severity {
    match severity {
        everestrs::ErrorSeverity::Low => Severity::Low,
        everestrs::ErrorSeverity::Medium => Severity::Medium,
        everestrs::ErrorSeverity::High => Severity::High,
        // The binding is a C++ enum behind a `repr(u8)`, so the compiler cannot
        // prove the three named values are all of them. An unnamed value is a
        // framework side defect; the conservative reading of an unknown severity
        // is the most serious one, since it decides emergency versus ordinary
        // error shutdown.
        other => {
            log::warn!("unknown error severity {other:?}, treating it as high");
            Severity::High
        }
    }
}

/// The stop reason, carried rather than narrowed.
///
/// This was the narrowing to `StopReason` until the payload needed the reason
/// itself: five wire values collapse to `Remote`, five to `Error` and eight to
/// `Local`, so the reason a consumer reads off `TransactionFinished` could not
/// be rebuilt from the narrow form without inventing one. The narrowing is
/// `StopTransactionReason::narrow`, in the core, driven from the value this
/// carries.
fn stop_transaction_reason_from(
    reason: types::evse_manager::StopTransactionReason,
) -> StopTransactionReason {
    use types::evse_manager::StopTransactionReason as Wire;
    match reason {
        Wire::EmergencyStop => StopTransactionReason::EmergencyStop,
        Wire::EVDisconnected => StopTransactionReason::EvDisconnected,
        Wire::HardReset => StopTransactionReason::HardReset,
        Wire::Local => StopTransactionReason::Local,
        Wire::Other => StopTransactionReason::Other,
        Wire::PowerLoss => StopTransactionReason::PowerLoss,
        Wire::Reboot => StopTransactionReason::Reboot,
        Wire::Remote => StopTransactionReason::Remote,
        Wire::SoftReset => StopTransactionReason::SoftReset,
        Wire::UnlockCommand => StopTransactionReason::UnlockCommand,
        Wire::DeAuthorized => StopTransactionReason::DeAuthorized,
        Wire::EnergyLimitReached => StopTransactionReason::EnergyLimitReached,
        Wire::GroundFault => StopTransactionReason::GroundFault,
        Wire::LocalOutOfCredit => StopTransactionReason::LocalOutOfCredit,
        Wire::MasterPass => StopTransactionReason::MasterPass,
        Wire::OvercurrentFault => StopTransactionReason::OvercurrentFault,
        Wire::PowerQuality => StopTransactionReason::PowerQuality,
        Wire::SOCLimitReached => StopTransactionReason::SocLimitReached,
        Wire::StoppedByEV => StopTransactionReason::StoppedByEv,
        Wire::TimeLimitReached => StopTransactionReason::TimeLimitReached,
        Wire::Timeout => StopTransactionReason::Timeout,
        Wire::ReqEnergyTransferRejected => StopTransactionReason::ReqEnergyTransferRejected,
        Wire::EVSEDisabled => StopTransactionReason::EvseDisabled,
    }
}

/// The same mapping back out, for `TransactionFinished.reason`.
///
/// Written out rather than derived, in both directions, so a value added to the
/// interface fails to compile here rather than reaching a consumer as whichever
/// value a catch all arm chose.
fn stop_transaction_reason_wire(
    reason: StopTransactionReason,
) -> types::evse_manager::StopTransactionReason {
    use types::evse_manager::StopTransactionReason as Wire;
    match reason {
        StopTransactionReason::EmergencyStop => Wire::EmergencyStop,
        StopTransactionReason::EvDisconnected => Wire::EVDisconnected,
        StopTransactionReason::HardReset => Wire::HardReset,
        StopTransactionReason::Local => Wire::Local,
        StopTransactionReason::Other => Wire::Other,
        StopTransactionReason::PowerLoss => Wire::PowerLoss,
        StopTransactionReason::Reboot => Wire::Reboot,
        StopTransactionReason::Remote => Wire::Remote,
        StopTransactionReason::SoftReset => Wire::SoftReset,
        StopTransactionReason::UnlockCommand => Wire::UnlockCommand,
        StopTransactionReason::DeAuthorized => Wire::DeAuthorized,
        StopTransactionReason::EnergyLimitReached => Wire::EnergyLimitReached,
        StopTransactionReason::GroundFault => Wire::GroundFault,
        StopTransactionReason::LocalOutOfCredit => Wire::LocalOutOfCredit,
        StopTransactionReason::MasterPass => Wire::MasterPass,
        StopTransactionReason::OvercurrentFault => Wire::OvercurrentFault,
        StopTransactionReason::PowerQuality => Wire::PowerQuality,
        StopTransactionReason::SocLimitReached => Wire::SOCLimitReached,
        StopTransactionReason::StoppedByEv => Wire::StoppedByEV,
        StopTransactionReason::TimeLimitReached => Wire::TimeLimitReached,
        StopTransactionReason::Timeout => Wire::Timeout,
        StopTransactionReason::ReqEnergyTransferRejected => Wire::ReqEnergyTransferRejected,
        StopTransactionReason::EvseDisabled => Wire::EVSEDisabled,
    }
}

/// One `ProvidedIdToken` on its way in, carried whole.
///
/// The two facts beside the payload are read off this same record, once, which
/// is what `IdTag` requires of its constructor: nothing else builds one.
fn id_tag_from(token: &types::authorization::ProvidedIdToken) -> Option<IdTag> {
    match ::everestrs::serde_json::to_value(token) {
        Ok(payload) => Some(IdTag::new(
            payload,
            token.id_token.value.clone(),
            id_token_type_of(&token.id_token.r#type),
            token.authorization_type == types::authorization::AuthorizationType::PlugAndCharge,
        )),
        // An identity that cannot be carried is one that cannot be published
        // either, and the caller drops the whole record rather than publishing
        // a token nobody presented.
        Err(error) => {
            log::error!("an id token could not be carried: {error}");
            None
        }
    }
}

/// `types::authorization::IdTokenType` as the core names it.
///
/// Total, so the interface gaining a ninth credential kind is a compile error
/// here rather than a token silently billed as something else.
fn id_token_type_of(wire: &types::authorization::IdTokenType) -> IdTokenType {
    use types::authorization::IdTokenType as Wire;
    match wire {
        Wire::Central => IdTokenType::Central,
        Wire::eMAID => IdTokenType::EMaid,
        Wire::MacAddress => IdTokenType::MacAddress,
        Wire::ISO14443 => IdTokenType::Iso14443,
        Wire::ISO15693 => IdTokenType::Iso15693,
        Wire::KeyCode => IdTokenType::KeyCode,
        Wire::Local => IdTokenType::Local,
        Wire::NoAuthorization => IdTokenType::NoAuthorization,
    }
}

/// The OCMF identification type a credential is billed under, ported whole from
/// `utils::convert_to_ocmf_identification_type` in `EvseManager/utils.hpp`.
///
/// Eight credential kinds onto eight of `OCMFIdentificationType`'s eighteen
/// values. `MacAddress` is the one that maps to `UNDEFINED`, and it does so in
/// the C++ too: OCMF has no autocharge type, so a vehicle identified by its own
/// address is a user of unstated kind. The other ten OCMF values - `DENIED`,
/// `EVCCID`, `EVCOID`, `ISO7812`, `CARD_TXN_NR`, the two numbered `CENTRAL`
/// variants, the two numbered `LOCAL` variants and `PHONE_NUMBER` - are not
/// reachable from any credential this interface can present, so this module
/// never signs one.
///
/// It lives here rather than in `core` because the mapping's far side is a wire
/// enum `core` cannot name, and a second core mirror of it would only be
/// translated again on the way out. What `core` decides is which credential is
/// held; what this decides is nothing.
fn ocmf_identification_type(
    token_type: IdTokenType,
) -> types::powermeter::OCMFIdentificationType {
    use types::powermeter::OCMFIdentificationType as Ocmf;
    match token_type {
        IdTokenType::Central => Ocmf::CENTRAL,
        IdTokenType::EMaid => Ocmf::EMAID,
        IdTokenType::Iso14443 => Ocmf::ISO14443,
        IdTokenType::Iso15693 => Ocmf::ISO15693,
        IdTokenType::KeyCode => Ocmf::KEY_CODE,
        IdTokenType::Local => Ocmf::LOCAL,
        IdTokenType::NoAuthorization => Ocmf::NONE,
        IdTokenType::MacAddress => Ocmf::UNDEFINED,
    }
}

/// The same record on its way back out, onto a session event payload.
///
/// The payload is this boundary's own serialization of the same type, so the
/// failure arm cannot fire without the interface having changed underneath a
/// running process; it is the shape `Effect::PublishProvidedToken` already uses
/// to relay a plug and charge token.
fn provided_id_token(tag: &IdTag) -> Option<types::authorization::ProvidedIdToken> {
    match ::everestrs::serde_json::from_value::<types::authorization::ProvidedIdToken>(
        tag.payload().clone(),
    ) {
        Ok(token) => Some(token),
        Err(error) => {
            log::error!("an id token could not be published: {error}");
            None
        }
    }
}

fn enable_source_from(
    wire: &types::evse_manager::EnableDisableSourceAutoGenEnableSource,
) -> EnableSource {
    use types::evse_manager::EnableDisableSourceAutoGenEnableSource as Wire;
    match wire {
        Wire::Unspecified => EnableSource::Unspecified,
        Wire::LocalAPI => EnableSource::LocalApi,
        Wire::LocalKeyLock => EnableSource::LocalKeyLock,
        Wire::ServiceTechnician => EnableSource::ServiceTechnician,
        Wire::RemoteKeyLock => EnableSource::RemoteKeyLock,
        Wire::MobileApp => EnableSource::MobileApp,
        Wire::FirmwareUpdate => EnableSource::FirmwareUpdate,
        Wire::CSMS => EnableSource::Csms,
    }
}

fn enable_source_wire(
    source: EnableSource,
) -> types::evse_manager::EnableDisableSourceAutoGenEnableSource {
    use types::evse_manager::EnableDisableSourceAutoGenEnableSource as Wire;
    match source {
        EnableSource::Unspecified => Wire::Unspecified,
        EnableSource::LocalApi => Wire::LocalAPI,
        EnableSource::LocalKeyLock => Wire::LocalKeyLock,
        EnableSource::ServiceTechnician => Wire::ServiceTechnician,
        EnableSource::RemoteKeyLock => Wire::RemoteKeyLock,
        EnableSource::MobileApp => Wire::MobileApp,
        EnableSource::FirmwareUpdate => Wire::FirmwareUpdate,
        EnableSource::Csms => Wire::CSMS,
    }
}

/// The state a source reported, as the core names it.
///
/// All three states travel, `Unassigned` included: it is not a vote but a
/// source saying it no longer cares, and the arbitration skips such a row
/// (`Charger.cpp:1776-1853`). Turned into an `Enable` here, as it was, a source
/// withdrawing kept its priority and won, so a high authority source letting
/// go forced the port enabled over a disable that should have stood.
fn enable_state_from(
    state: &types::evse_manager::EnableDisableSourceAutoGenEnableState,
) -> EnableState {
    use types::evse_manager::EnableDisableSourceAutoGenEnableState as Wire;
    match state {
        Wire::Unassigned => EnableState::Unassigned,
        Wire::Disable => EnableState::Disable,
        Wire::Enable => EnableState::Enable,
    }
}

fn enable_state_wire(
    state: EnableState,
) -> types::evse_manager::EnableDisableSourceAutoGenEnableState {
    use types::evse_manager::EnableDisableSourceAutoGenEnableState as Wire;
    match state {
        EnableState::Unassigned => Wire::Unassigned,
        EnableState::Disable => Wire::Disable,
        EnableState::Enable => Wire::Enable,
    }
}

fn enable_source_struct(entry: EnableEntry) -> types::evse_manager::EnableDisableSource {
    types::evse_manager::EnableDisableSource {
        enable_source: enable_source_wire(entry.source),
        enable_state: enable_state_wire(entry.state),
        enable_priority: entry.priority,
    }
}

/// The core `SessionEvent` set is narrower than the wire enum by design: the
/// wire carries notifications the core reports through other effects.
/// A reading with nothing in it but the two fields the wire type requires.
///
/// The `SessionStarted` payload requires a meter value and no `Effect` carries
/// one. Reported as zero rather than omitted, because the field is not
/// optional; filling it is the metering owner's.
fn empty_powermeter(timestamp: String) -> types::powermeter::Powermeter {
    types::powermeter::Powermeter {
        energy_wh_import: types::units::Energy {
            total: 0.0,
            l_1: None,
            l_2: None,
            l_3: None,
        },
        timestamp,
        var: None,
        var_signed: None,
        current_a: None,
        current_a_signed: None,
        energy_wh_export: None,
        energy_wh_export_signed: None,
        energy_wh_import_signed: None,
        frequency_hz: None,
        frequency_hz_signed: None,
        meter_id: None,
        phase_seq_error: None,
        power_w: None,
        power_w_signed: None,
        signed_meter_value: None,
        temperatures: None,
        voltage_v: None,
        voltage_v_signed: None,
    }
}

fn start_session_reason(reason: StartSessionReason) -> types::evse_manager::StartSessionReason {
    use types::evse_manager::StartSessionReason as Wire;
    match reason {
        StartSessionReason::EvConnected => Wire::EVConnected,
        StartSessionReason::Authorized => Wire::Authorized,
    }
}

/// `core::session::PauseReason` onto the wire enum.
///
/// Written out and exhaustive, as the other converters here are: the wire
/// declares a third value, `Error`, which `PauseReason` deliberately has no
/// variant for, so a variant added on either side has to be decided here
/// rather than mapped onto a neighbour.
fn pause_reason_wire(
    reason: PauseReason,
) -> types::evse_manager::PauseChargingEVSEReasonEnum {
    use types::evse_manager::PauseChargingEVSEReasonEnum as Wire;
    match reason {
        PauseReason::NoEnergy => Wire::NoEnergy,
        PauseReason::UserPause => Wire::UserPause,
    }
}

fn session_event_enum(event: SessionEvent) -> types::evse_manager::SessionEventEnum {
    use types::evse_manager::SessionEventEnum as Wire;
    match event {
        SessionEvent::Authorized => Wire::Authorized,
        SessionEvent::Deauthorized => Wire::Deauthorized,
        SessionEvent::Enabled => Wire::Enabled,
        SessionEvent::Disabled => Wire::Disabled,
        SessionEvent::PluginTimeout => Wire::PluginTimeout,
        SessionEvent::SessionStarted => Wire::SessionStarted,
        SessionEvent::SessionResumed => Wire::SessionResumed,
        SessionEvent::AuthRequired => Wire::AuthRequired,
        SessionEvent::TransactionStarted => Wire::TransactionStarted,
        SessionEvent::PrepareCharging => Wire::PrepareCharging,
        SessionEvent::ChargingStarted => Wire::ChargingStarted,
        SessionEvent::ChargingPausedEv => Wire::ChargingPausedEV,
        SessionEvent::ChargingPausedEvse => Wire::ChargingPausedEVSE,
        SessionEvent::SwitchingPhases => Wire::SwitchingPhases,
        SessionEvent::StoppingCharging => Wire::StoppingCharging,
        SessionEvent::ChargingFinished => Wire::ChargingFinished,
        SessionEvent::TransactionFinished => Wire::TransactionFinished,
        SessionEvent::SessionFinished => Wire::SessionFinished,
        SessionEvent::ReservationStart => Wire::ReservationStart,
        SessionEvent::ReservationEnd => Wire::ReservationEnd,
    }
}

/// `core::hlc::CarManufacturer` onto the wire enum.
///
/// Written out rather than derived, and exhaustive in both directions: the
/// interface declares exactly these three and the core names exactly these
/// three, so a fourth appearing on either side fails to compile here rather
/// than reaching a consumer as one of the others.
fn car_manufacturer_wire(manufacturer: CarManufacturer) -> types::evse_manager::CarManufacturer {
    use types::evse_manager::CarManufacturer as Wire;
    match manufacturer {
        CarManufacturer::VolkswagenGroup => Wire::VolkswagenGroup,
        CarManufacturer::Tesla => Wire::Tesla,
        CarManufacturer::Unknown => Wire::Unknown,
    }
}

/// `core::hlc::EvInfo` onto the wire record.
///
/// Every field is written out and twenty four of them are `None`, because the
/// generated type derives no `Default`: a field added to `EVInfo` stops
/// compiling here until someone decides whether this port has a source for it.
/// The C++ builds the same record the same way, from a value initialized member
/// whose unset fields are absent on the wire.
fn ev_info_wire(info: &EvInfo) -> types::evse_manager::EVInfo {
    types::evse_manager::EVInfo {
        evcc_id: info.evcc_id.clone(),
        soc: info.soc,
        ac_max_charge_power: None,
        ac_max_discharge_power: None,
        ac_min_charge_power: None,
        ac_min_discharge_power: None,
        ac_present_active_power: None,
        ac_present_reactive_power: None,
        battery_bulk_soc: None,
        battery_capacity: None,
        battery_full_soc: None,
        departure_time: None,
        estimated_time_bulk: None,
        estimated_time_full: None,
        max_energy_request: None,
        maximum_current_limit: None,
        maximum_power_limit: None,
        maximum_voltage_limit: None,
        min_energy_request: None,
        minimum_current_limit: None,
        present_current: None,
        present_voltage: None,
        remaining_energy_needed: None,
        target_current: None,
        target_energy_request: None,
        target_voltage: None,
    }
}

/// One place that says a pass-through republication failed.
///
/// Every caller is republishing a value this module does not own, on its own
/// `evse` interface, exactly as the C++ subscription callbacks that carry the
/// same variable do. There is nothing to retry and nothing to decide: the log
/// is the whole of the handling. It exists as one function so the call sites
/// cannot each pick a different level, and so a republication that starts
/// failing is one string to search for.
fn report_republish(variable: &str, result: ::everestrs::Result<()>) {
    if let Err(error) = result {
        log::error!("{variable} could not be republished on the evse interface: {error}");
    }
}

/// The meter that bills, ported from `EvseManager::r_powermeter_billing`: the
/// car side when one is connected, the grid side otherwise.
///
/// Generic over the slot type so the choice is testable without a live
/// publisher. Both slots are declared `max_connections: 1`, so the first entry
/// is the only entry.
fn billing_meter<'a, T>(car_side: &'a [T], grid_side: &'a [T]) -> Option<&'a T> {
    car_side.first().or_else(|| grid_side.first())
}

/// The three AC phase currents of one meter record, or none.
///
/// `EvseManager.cpp:1157` requires `p.current_A and .L1 and .L2 and .L3` before
/// it calls `set_current_drawn_by_vehicle`, so a record missing any one phase
/// updates none of them. Answering with one option rather than three keeps that
/// gate at the boundary and out of the reader.
///
/// Filled by name. Three same typed `Option<f64>` fields in wire order is
/// exactly the shape a positional literal transposes, and a transposition here
/// would be invisible: soft overcurrent detection takes the largest of the
/// three, so only the description would name the wrong phase.
fn phase_currents_from(current: &types::units::Current) -> Option<PhaseCurrents> {
    Some(PhaseCurrents {
        l1_a: current.l_1?,
        l2_a: current.l_2?,
        l3_a: current.l_3?,
    })
}

/// Whether readings from this requirement slot are the ones the module takes.
///
/// The same preference, because it is the same meter: the C++ has one
/// `subscribe_powermeter` and it is on `r_powermeter_billing()`
/// (`EvseManager.cpp:1151-1152`). One slot feeds the module for that reason. Two
/// interleaved streams would be a reading the C++ never has, and a consumer
/// added later would have no way to tell which meter it was looking at.
fn meter_feeds_the_core(slot: &str, car_side_wired: bool) -> bool {
    if car_side_wired {
        slot == POWERMETER_CAR_SIDE
    } else {
        slot == POWERMETER_GRID_SIDE
    }
}

/// Whether a fault on this powermeter slot reaches the fault set at all.
///
/// Two gates, both the C++'s and both at its one construction site
/// (`EvseManager.cpp:310-315`): `ErrorHandling` is handed the **billing** slot,
/// so a fault on the meter that is not billing is not this module's business,
/// and it is handed an empty vector unless `fail_on_powermeter_errors` is set,
/// so a deployment that did not ask for it has no powermeter fault stop a
/// charge. The port raised both on either slot, so a warning from an unused
/// grid side meter took the port out of service.
///
/// A free function of its three facts, as the sibling rule above is, so both
/// halves can be driven rather than only the call site being pinned.
fn powermeter_fault_reaches_the_module(
    slot: &str,
    car_side_wired: bool,
    fail_on_powermeter_errors: bool,
) -> bool {
    fail_on_powermeter_errors && meter_feeds_the_core(slot, car_side_wired)
}

/// Whether this slot's *capability* report is the one the module merges into
/// what the vehicle is offered.
///
/// Narrower than the reading gate above, and deliberately: the C++ subscribes
/// `capabilities` on `r_powermeter_car_side[0]` alone
/// (`EvseManager.cpp:245-251`), not on the billing slot. The floor it describes
/// is a property of the meter between the port and the vehicle, so a grid side
/// minimum merged into the vehicle's offer would raise a floor nothing measures
/// there - and on a deployment with both meters wired the two reports landed in
/// one record, whichever arrived last.
fn meter_reports_the_floors(slot: &str) -> bool {
    slot == POWERMETER_CAR_SIDE
}

/// The OCMF transaction request, ported from `Charger::start_transaction`.
///
/// Filled by name. `evse_id` and `transaction_id` are both `String` and sit next
/// to each other in the generated struct, so a positional habit here bills the
/// EVSE id as the transaction.
///
/// Two fields are wider than anything this module knows and are named rather
/// than invented:
///
/// - `identification_level` is `std::nullopt` in the C++ too, being unknown to
///   EVerest.
/// - `identification_flags` is empty in the C++ too, for the same reason.
fn transaction_request(
    evse_id: &str,
    transaction_id: &str,
    id_token: Option<&IdTag>,
    tariff_text: Option<&str>,
) -> types::powermeter::TransactionReq {
    types::powermeter::TransactionReq {
        evse_id: evse_id.to_owned(),
        transaction_id: transaction_id.to_owned(),
        // Hardcoded in the C++ as well: a transaction only opens behind an
        // accepted authorization, so the user is always assigned.
        identification_status: types::powermeter::OCMFUserIdentificationStatus::ASSIGNED,
        identification_flags: Vec::new(),
        // `Charger::start_transaction` reads this off the token's declared
        // `id_token.type` and off nothing else, so it is derived from the same
        // record `identification_data` comes from rather than from the
        // authorization kind beside it. An `ISO14443` card is billed as one.
        //
        // A transaction with no identity at all is not reachable from `core` -
        // the metering start is pushed out of `AuthSignal::Authorized`, one
        // statement after the token is recorded - and `UNDEFINED` is what OCMF
        // names for a user of unstated kind, which is the only honest answer if
        // it ever were.
        identification_type: id_token.map_or(
            types::powermeter::OCMFIdentificationType::UNDEFINED,
            |tag| ocmf_identification_type(tag.token_type()),
        ),
        identification_level: None,
        identification_data: id_token.map(|tag| tag.value().to_owned()),
        // The message `core` selected out of the verdict's tariff messages, and
        // `None` where `Charger::start_transaction`'s `empty()` guard leaves the
        // field unset. Never defaulted: this decides what a legal metrology
        // meter signs, and there is no source for a tariff in this module other
        // than the authorizing party's own answer.
        tariff_text: tariff_text.map(str::to_owned),
    }
}

/// A powermeter answer that arrived. The status is the meter's own verdict, so
/// a delivered command carrying `UNEXPECTED_ERROR` is a failed effect and not a
/// successful call. The core reads that verdict back through
/// `Event::EffectDone` and decides what it means; reporting it is all this does.
fn metering_status(
    status: &types::powermeter::TransactionRequestStatus,
    error: Option<&str>,
) -> EffectOutcome {
    match status {
        // A meter without OCMF transaction support is a wiring fact, and the
        // C++ moves on rather than treating it as a fault.
        types::powermeter::TransactionRequestStatus::OK
        | types::powermeter::TransactionRequestStatus::NOT_SUPPORTED => EffectOutcome::Ok,
        types::powermeter::TransactionRequestStatus::UNEXPECTED_ERROR => EffectOutcome::Failed(
            error
                .unwrap_or("powermeter refused the transaction")
                .to_owned(),
        ),
    }
}

/// Framework callbacks land here. Holds intake and identity, and nothing else:
/// no state, no lock, no reference to `Core`.
///
/// A callback whose payload has no `Event` counterpart yet logs at debug and
/// returns. The trait method must exist because the generated dispatch calls
/// it; whether the core consumes it is a separate question that the task
/// introducing that behavior answers.
/// The billing meter's latest whole reading, `EvseManager`'s
/// `latest_powermeter_data_billing`.
///
/// Written by `Intake::on_powermeter` and read by `EverestEffects` when it
/// renders a session event payload, which is one writer and one reader on one
/// input rather than a second copy of anything the core decides: the core is
/// handed `MeterReading`, a five field projection every charging decision reads,
/// and cannot hold this because it cannot name a wire type at all
/// (`src/lib.rs`). Three wire payloads require the whole record and no `Effect`
/// can carry it, which is the same shape as `session_started.logging_path`, a
/// boundary owned fact the publish reads back.
///
/// `None` until the first reading arrives. The C++ field is default constructed
/// and published as-is before then, which `empty_powermeter` is.
type BillingMeter = Arc<Mutex<Option<types::powermeter::Powermeter>>>;

/// The two whole meter records the energy flow request carries, one per
/// requirement slot.
///
/// `energyImpl::init` keeps them as two subscriptions of its own
/// (`energy_grid/energyImpl.cpp:42-60`), each guarded on its slot being wired,
/// and each one simply replaces the field. They are separate from
/// `BillingMeter` because they are separate facts: the billing record is
/// whichever single slot this module bills from, and these are one per slot
/// whatever that preference is. A deployment with only a grid side meter fills
/// the root and leaves the leaves absent.
///
/// The core cannot hold them for the reason `BillingMeter` gives: `core` names
/// no wire type, and the whole record is what the field carries.
#[derive(Default)]
struct EnergyUsage {
    /// `energy_usage_root`, the grid side reading with
    /// `phase_rotation_grid_side` already applied, because the C++ applies it
    /// in the subscription rather than at the publish (`energyImpl.cpp:58`).
    root: Option<types::powermeter::Powermeter>,
    /// `energy_usage_leaves`, the car side reading verbatim.
    leaves: Option<types::powermeter::Powermeter>,
}

/// Remaps a per-phase measurement's L1, L2 and L3 according to `rotation`.
///
/// `everest::helpers::rotate_phases`. The members a rotation leaves alone are
/// the ones that are not per phase: the total, the DC figure and the neutral.
/// Written as a macro over the field names because the twelve measurements
/// below are five different types with the same three fields and Rust has no
/// structural bound that names them.
macro_rules! rotate_phases {
    ($measurement:expr, $rotation:expr) => {
        if let Some(measurement) = $measurement.as_mut() {
            let (l1, l2, l3) = (
                measurement.l_1.clone(),
                measurement.l_2.clone(),
                measurement.l_3.clone(),
            );
            match $rotation {
                PhaseRotation::Trs => {
                    measurement.l_1 = l2;
                    measurement.l_2 = l3;
                    measurement.l_3 = l1;
                }
                PhaseRotation::Str => {
                    measurement.l_1 = l3;
                    measurement.l_2 = l1;
                    measurement.l_3 = l2;
                }
                PhaseRotation::Rst => {}
            }
        }
    };
}

/// `everest::helpers::apply_phase_rotation`: the twelve per-phase measurements
/// of a meter record, remapped, and every other field left as it was.
///
/// The twelve are the C++ list verbatim, and `energy_wh_import` is the one of
/// them the wire type declares as required rather than optional, so it is
/// wrapped to reach the same macro. A measurement missing from this list is a
/// phase that stays wrong for a rotated meter, which is why the list is the
/// C++ one rather than one derived here.
fn apply_phase_rotation(
    mut meter: types::powermeter::Powermeter,
    rotation: PhaseRotation,
) -> types::powermeter::Powermeter {
    if rotation == PhaseRotation::Rst {
        return meter;
    }
    let mut energy_wh_import = Some(meter.energy_wh_import);
    rotate_phases!(energy_wh_import, rotation);
    meter.energy_wh_import = energy_wh_import.expect("the option was just built from a value");
    rotate_phases!(meter.energy_wh_export, rotation);
    rotate_phases!(meter.power_w, rotation);
    rotate_phases!(meter.voltage_v, rotation);
    rotate_phases!(meter.var, rotation);
    rotate_phases!(meter.current_a, rotation);
    rotate_phases!(meter.energy_wh_import_signed, rotation);
    rotate_phases!(meter.energy_wh_export_signed, rotation);
    rotate_phases!(meter.power_w_signed, rotation);
    rotate_phases!(meter.voltage_v_signed, rotation);
    rotate_phases!(meter.var_signed, rotation);
    rotate_phases!(meter.current_a_signed, rotation);
    meter
}

/// The signed meter values the billing meter answered a transaction with.
///
/// `Charger::shared_context.start_signed_meter_value` and
/// `.stop_signed_meter_value` (`Charger.hpp:373-374`), which the two
/// transaction payloads read back through `get_start_signed_meter_value` and
/// `get_stop_signed_meter_value` (`evse/evse_managerImpl.cpp:207` and
/// `:279-280`).
///
/// Boundary state rather than the core's, for the reason [`BillingMeter`]
/// gives: this is a wire type the core cannot name (`src/lib.rs`) and no
/// `Effect` can carry one. It is not a second account of anything the core
/// decides either - the core decides when a record opens and closes, and this
/// is only what the meter signed for it.
///
/// One writer and one reader, and both are the `ExecContext::Publish` thread:
/// `Effect::StartTransaction` and `Effect::StopTransaction` write it and the
/// `Effect::PublishSessionEvent` the core pushes behind each of them reads it.
/// `Effect::context` says why those three share a lane; without that they race
/// and the payload carries whatever the meter had not yet answered.
#[derive(Default)]
struct SignedMeterValues {
    /// What the meter signed when the record opened, or what a stop recovered
    /// for a record whose start signed nothing.
    start: Option<types::units_signed::SignedMeterValue>,
    /// What the meter signed when the record closed.
    stop: Option<types::units_signed::SignedMeterValue>,
}

struct Intake {
    events: EventSender,
    settings: Arc<Settings>,
    /// See [`BillingMeter`]. Held here because `on_powermeter` is the only
    /// writer.
    billing_meter: BillingMeter,
    /// See [`EnergyUsage`]. Filled here and read by `EverestEffects` when it
    /// renders an energy flow request, which is the same split
    /// `billing_meter` has.
    energy_usage: Arc<Mutex<EnergyUsage>>,
    /// Filled immediately after `Module::start` returns, which is the earliest
    /// point at which connected slot counts are known. Commands answered before
    /// that fall back to reporting no high level communication.
    wiring: OnceLock<Wiring>,
    /// The resolved high level communication configuration, or `None` on a
    /// deployment with no stack.
    ///
    /// Taken from the `HlcPort` the core holds, through
    /// `Deployment::hlc_config`, so the two sides read one value rather than
    /// two derivations of it and cannot disagree about whether there is a
    /// stack at all. The outer `OnceLock` is "not filled yet", which is a
    /// different thing from `None` and answers the same way; see the two
    /// commands that read it. Filled alongside `wiring`, because whether high
    /// level communication is enabled depends on what is connected.
    hlc: OnceLock<Option<Arc<HlcConfig>>>,
    /// Callers blocked on a command's verdict, shared with the writer, which
    /// completes them. Five commands return a decision the core makes, so
    /// intake has to wait for it rather than answer ahead of it; see
    /// `boundary::replies` and `docs/effect-ordering-timing.md`.
    replies: Arc<Replies>,
}

impl Intake {
    fn send(&self, event: Event) {
        self.events.send(event);
    }

    /// Posts a command and blocks for the verdict the core reaches, which is
    /// what the C++ returns from these five handlers.
    ///
    /// Bounded, and a breach answers `false`: two paths leave a command with no
    /// answer at all, and neither is a reason to hold the caller forever. The
    /// bound and the reason blocking is safe here are in
    /// `boundary::replies`.
    fn send_awaiting(&self, command: Command) -> bool {
        let pending = self.replies.register();
        self.send(Event::CommandAwaiting {
            command,
            reply: pending.token(),
        });
        pending.wait()
    }

    /// The two facts `powermeter_fault_reaches_the_module` decides on, read
    /// off this intake. The gate is a boundary one rather than a core one
    /// because it is a wiring decision in the C++ too: the subscription does
    /// not exist, so nothing downstream has to know the setting.
    fn powermeter_faults_reach_the_module(&self, slot: &str) -> bool {
        powermeter_fault_reaches_the_module(
            slot,
            self.car_side_meter_wired(),
            self.settings.misc.fail_on_powermeter_errors,
        )
    }

    /// Record a reading against the slot it came from, which is
    /// `energyImpl::init`'s two subscriptions
    /// (`energy_grid/energyImpl.cpp:42-60`).
    ///
    /// The grid side is rotated on the way in rather than on the way out,
    /// because that is where the C++ rotates it, and because doing it here
    /// means one rotation per reading instead of one per publish.
    ///
    /// A slot name this module does not know records nothing. There are two
    /// and `Context::name` names one of them, so the arm is unreachable; it is
    /// written rather than made a catch-all so a third slot is a silent drop
    /// nowhere.
    fn note_energy_usage(&self, slot: &str, value: &types::powermeter::Powermeter) {
        let mut usage = self
            .energy_usage
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        match slot {
            POWERMETER_GRID_SIDE => {
                usage.root = Some(apply_phase_rotation(
                    value.clone(),
                    self.settings.misc.phase_rotation_grid_side,
                ));
            }
            POWERMETER_CAR_SIDE => usage.leaves = Some(value.clone()),
            other => log::warn!("a powermeter reading arrived on unknown slot {other}"),
        }
    }

    /// Readings can arrive before `wiring` is filled. Reporting no car side
    /// meter until it is takes the grid side, which is the fallback the C++
    /// selector already ends on, rather than taking neither.
    fn car_side_meter_wired(&self) -> bool {
        self.wiring
            .get()
            .is_some_and(|wiring| wiring.powermeter_car_side)
    }

    /// Whether there is a connector lock to open.
    ///
    /// Same fallback shape as `car_side_meter_wired`: a command answered
    /// before `wiring` is filled reports no lock, which is the answer that
    /// declines rather than the one that claims an unlock nothing performed.
    fn connector_lock_wired(&self) -> bool {
        self.wiring.get().is_some_and(|wiring| wiring.connector_lock)
    }
}

impl OnReadySubscriber for Intake {
    /// Injects one event, and republishes the one pass-through variable that is
    /// a configuration fact rather than a fact about a session. The ready
    /// sequence, which publishes the boot enable state and then `ready`, is a
    /// core transition.
    ///
    /// `evse_managerImpl::ready` publishes `evse_id` here and nowhere else,
    /// under its own comment "publish evse id at least once". It never changes
    /// while the process lives, so one publish at the lifecycle point the
    /// framework has just declared is the whole of it.
    ///
    /// Said from this callback rather than as an effect because no core state
    /// selects it and no event changes it: an effect would need a transition
    /// invented to carry it, and that transition would then be the thing under
    /// test rather than the publish. The same reasoning puts the other four
    /// republications in the subscriber that receives them.
    fn on_ready(&self, publishers: &ModulePublisher) {
        report_republish(
            "evse_id",
            publishers.evse.evse_id(self.settings.evse_id.clone()),
        );
        self.send(Event::Startup);
    }
}

// Required interfaces.

impl EvseBoardSupportClientSubscriber for Intake {
    fn on_event(&self, _context: &Context, value: types::board_support_common::BspEvent) {
        self.send(Event::Bsp(BspEvent::Cp(cp_event_from(value.event))));
    }

    fn on_ac_nr_of_phases_available(&self, _context: &Context, value: i64) {
        self.send(Event::Bsp(BspEvent::NrOfPhasesAvailable(value)));
    }

    fn on_ac_pp_ampacity(
        &self,
        _context: &Context,
        value: types::board_support_common::ProximityPilot,
    ) {
        self.send(Event::Bsp(BspEvent::PpAmpacity(pp_ampacity_amps(
            value.ampacity,
        ))));
    }

    /// Feeds the core the ten fields its derivations read, and republishes the
    /// record whole.
    ///
    /// Two separate things, and the second is why the first cannot serve as it.
    /// `EvseManager`'s board support capabilities subscription hands `charger`
    /// three of the fields and then calls `publish_hw_capabilities(c)` with the
    /// argument it received, unchanged; the OCPP 2.0.1 device model reads
    /// `max_current_A_import`, `max_phase_count_import` and `connector_type`
    /// off it, and `connector_type` is not one of the ten the core is given.
    /// So the projection below is for the decisions and the republication is
    /// the record, in the same shape and for the same reason as the billing
    /// meter cell: `core` cannot name a wire type at all (`src/lib.rs`).
    ///
    /// Republished after the core is told, which is the C++ statement order:
    /// its handler tells `charger` and `bsp` first and publishes last. The
    /// order is not observable from outside the module - the send only queues -
    /// and it is kept because there is no reason to differ.
    ///
    /// The C++ handler additionally blocks on `ready_for_capabilities` before
    /// it does either, so that no reading reaches a `charger` that does not
    /// exist yet. That gate has no counterpart here and needs none: readings
    /// arriving before the writer thread starts queue rather than being
    /// delivered, which is the same guarantee without a wait.
    fn on_capabilities(
        &self,
        context: &Context,
        value: types::evse_board_support::HardwareCapabilities,
    ) {
        // Every field names its direction on both sides, because the two are
        // separate values on the wire and the derivations read different ones:
        // the import pair chooses the AC transfer mode set, the export pair
        // gates the bidirectional modes.
        self.send(Event::Bsp(BspEvent::Capabilities(HardwareCapabilities {
            max_current_a_import: value.max_current_a_import,
            min_current_a_import: value.min_current_a_import,
            max_phase_count_import: value.max_phase_count_import,
            min_phase_count_import: value.min_phase_count_import,
            max_current_a_export: value.max_current_a_export,
            min_current_a_export: value.min_current_a_export,
            max_phase_count_export: value.max_phase_count_export,
            min_phase_count_export: value.min_phase_count_export,
            supports_changing_phases_during_charging: value
                .supports_changing_phases_during_charging,
            supports_cp_state_e: value.supports_cp_state_e,
        })));
        report_republish(
            "hw_capabilities",
            context.publisher.evse.hw_capabilities(value),
        );
    }

    /// `EvseManager.cpp:253-254`: the board's request is handed to
    /// `charger->cancel_transaction(r)`, which is the same call the
    /// `stop_transaction` command makes. So it carries the same two fields and
    /// reaches the core as the same command.
    ///
    /// It used to arrive as a board event of its own with the reason and the
    /// token dropped, and no power path acted on that event, so a physical stop
    /// button during a charge did nothing at all.
    fn on_request_stop_transaction(
        &self,
        _context: &Context,
        value: types::evse_manager::StopTransactionRequest,
    ) {
        self.send(Event::Command(Command::StopTransaction {
            reason: stop_transaction_reason_from(value.reason),
            id_tag: value.id_tag.as_ref().and_then(id_tag_from),
        }));
    }

    /// Republished verbatim on the `evse` interface, which the core does not
    /// participate in.
    ///
    /// `evse_managerImpl::ready`'s board support telemetry subscription
    /// publishes the record it was handed and nothing else. Its other statement
    /// there, an external MQTT publish of the plug temperature under
    /// `everest_external/nodered/`, is unported for the reason
    /// `docs/architecture.md` gives for the transcript's external publication:
    /// `everestrs` exposes `publish_variable` on declared interfaces and has no
    /// binding that reaches an arbitrary topic. `enable_nodered_interface` is
    /// declared in this manifest at the C++ default and refused when it is
    /// switched on, so a deployment that sets it is told rather than served a
    /// port with the interface silently missing.
    fn on_telemetry(&self, context: &Context, value: types::evse_board_support::Telemetry) {
        report_republish("telemetry", context.publisher.evse.telemetry(value));
    }

    fn on_error_raised(
        &self,
        _context: &Context,
        error: ::everestrs::ErrorType<generated::errors::evse_board_support::Error>,
    ) {
        self.send(error_event(ErrorSource::Bsp, error, true));
    }

    fn on_error_cleared(
        &self,
        _context: &Context,
        error: ::everestrs::ErrorType<generated::errors::evse_board_support::Error>,
    ) {
        // Identical but for the flag. Raise and clear travel the same route by
        // construction, so neither can be deliverable while the other is not.
        self.send(error_event(ErrorSource::Bsp, error, false));
    }
}

impl AcRcdClientSubscriber for Intake {
    fn on_rcd_current_m_a(&self, _context: &Context, _value: f64) {
        // The residual current value itself is only diagnostic. What matters is
        // the error the RCD raises, which arrives below.
    }

    fn on_error_raised(
        &self,
        _context: &Context,
        error: ::everestrs::ErrorType<generated::errors::ac_rcd::Error>,
    ) {
        self.send(error_event(ErrorSource::AcRcd, error, true));
    }

    fn on_error_cleared(
        &self,
        _context: &Context,
        error: ::everestrs::ErrorType<generated::errors::ac_rcd::Error>,
    ) {
        self.send(error_event(ErrorSource::AcRcd, error, false));
    }
}

impl ConnectorLockClientSubscriber for Intake {
    fn on_error_raised(
        &self,
        _context: &Context,
        error: ::everestrs::ErrorType<generated::errors::connector_lock::Error>,
    ) {
        self.send(error_event(ErrorSource::ConnectorLock, error, true));
    }

    fn on_error_cleared(
        &self,
        _context: &Context,
        error: ::everestrs::ErrorType<generated::errors::connector_lock::Error>,
    ) {
        self.send(error_event(ErrorSource::ConnectorLock, error, false));
    }
}

impl PowermeterClientSubscriber for Intake {
    fn on_powermeter(&self, context: &Context, value: types::powermeter::Powermeter) {
        // Choosing between the two slots is metering behavior rather than
        // intake, so it is decided by a named rule and not by a comparison
        // written here.
        // Both slots first, and above the billing gate, because the two are
        // not that gate's business: `energyImpl::init` subscribes each slot
        // separately and neither subscription consults which one bills. A
        // reading from the slot this module does not bill from is still the
        // one the energy manager is owed for that slot.
        self.note_energy_usage(context.name, &value);
        if !meter_feeds_the_core(context.name, self.car_side_meter_wired()) {
            return;
        }
        // Republished verbatim, first, because that is the order the C++ two
        // subscriptions to this same variable fire in: `evse_managerImpl::init`
        // registers the republication and `EvseManager::ready` registers the
        // one that feeds the charging decisions, so the wire sees the reading
        // before anything in the module acts on it. It is the same gate as the
        // core's rather than a second one, so a deployment cannot bill from the
        // car side meter while reporting the grid side one.
        //
        // This is the variable the OCPP MeterValues collapse was: all three
        // consumers subscribe it on `evse_manager` and the port published
        // nothing, so `TriggerMessage(meter_values)` had nothing to answer
        // with.
        report_republish(
            "powermeter",
            context.publisher.evse.powermeter(value.clone()),
        );
        // The whole record, cached for the three session event payloads that
        // require it. `EvseManager.cpp`'s billing meter subscription stores it
        // in the same place in the same order, before it tells anything else
        // about the reading. The projection below is what every charging
        // decision reads; see `BillingMeter` for why both exist.
        *self
            .billing_meter
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner()) = Some(value.clone());
        // The DC figure alone, and absent when the meter reports none. The
        // `voltage_v` field below falls back to L1 and then to zero, neither of
        // which the plausibility comparison may be handed. Read before that
        // field consumes the option.
        let dc_voltage_v = value.voltage_v.as_ref().and_then(|voltage| voltage.dc);
        self.send(Event::Meter(MeterReading {
            energy_wh_import: value.energy_wh_import.total,
            // Carried whole and left absent when the meter reports none, so
            // the one reader can tell "no figure" from "zero watts": the
            // present power the vehicle is told is gated on `p.power_W` having
            // a value (`EvseManager.cpp:1167`).
            power_w: value.power_w.map(power_from),
            voltage_v: value
                .voltage_v
                .and_then(|voltage| voltage.dc.or(voltage.l_1))
                .unwrap_or(0.0),
            dc_voltage_v,
            current_a: value
                .current_a
                .as_ref()
                .and_then(|current| current.dc.or(current.l_1))
                .unwrap_or(0.0),
            phase_currents_a: value.current_a.as_ref().and_then(phase_currents_from),
        }));
    }

    /// `EvseManager.cpp:249-250`, the car side meter's measurable floors.
    ///
    /// Recorded in the transcript and merged as a floor into the DC
    /// capabilities the vehicle is offered, which is what
    /// `EvseManager::update_powermeter_capabilities` does with the same report.
    /// Filled by name, since both fields are `Option<f64>` and a positional
    /// literal would compile with the directions swapped.
    fn on_capabilities(&self, context: &Context, value: types::powermeter::Capabilities) {
        if !meter_reports_the_floors(context.name) {
            return;
        }
        self.send(Event::PowermeterCapabilities(PowermeterCapabilities {
            min_import_current_a: value.min_import_current_a,
            min_export_current_a: value.min_export_current_a,
        }));
    }

    /// Republished verbatim on the `evse` interface, no core involvement.
    ///
    /// The billing meter's own key, which `evse_managerImpl::init` republishes
    /// beside the reading itself and from the same slot. Gated the same way for
    /// the same reason: the C++ subscribes only the billing slot, so a
    /// deployment with both meters wired publishes the car side key and not the
    /// grid side one, and a key from the meter that is not billing would name
    /// the wrong signature to a consumer checking one.
    fn on_public_key_ocmf(&self, context: &Context, value: String) {
        if !meter_feeds_the_core(context.name, self.car_side_meter_wired()) {
            return;
        }
        report_republish(
            "powermeter_public_key_ocmf",
            context.publisher.evse.powermeter_public_key_ocmf(value),
        );
    }

    fn on_error_raised(
        &self,
        context: &Context,
        error: ::everestrs::ErrorType<generated::errors::powermeter::Error>,
    ) {
        if !self.powermeter_faults_reach_the_module(context.name) {
            return;
        }
        self.send(error_event(ErrorSource::Powermeter, error, true));
    }

    fn on_error_cleared(
        &self,
        context: &Context,
        error: ::everestrs::ErrorType<generated::errors::powermeter::Error>,
    ) {
        if !self.powermeter_faults_reach_the_module(context.name) {
            return;
        }
        self.send(error_event(ErrorSource::Powermeter, error, false));
    }
}

impl IsolationMonitorClientSubscriber for Intake {
    fn on_isolation_measurement(
        &self,
        _context: &Context,
        value: types::isolation_monitor::IsolationMeasurement,
    ) {
        // Filled by name. The generator inserts an underscore before a digit
        // run, so the wire's `voltage_to_earth_l1e_V` is
        // `voltage_to_earth_l_1_e_v` here and the two earth fields are adjacent
        // and same typed.
        self.send(Event::Isolation(IsolationReading {
            resistance_ohm: value.resistance_f_ohm,
            voltage_v: value.voltage_v,
            voltage_to_earth_l1e_v: value.voltage_to_earth_l_1_e_v,
            voltage_to_earth_l2e_v: value.voltage_to_earth_l_2_e_v,
        }));
    }

    fn on_self_test_result(&self, _context: &Context, result: bool) {
        log::debug!("isolation monitor self test result {result}");
        self.send(Event::IsolationSelfTest(result));
    }

    fn on_error_raised(
        &self,
        _context: &Context,
        error: ::everestrs::ErrorType<generated::errors::isolation_monitor::Error>,
    ) {
        self.send(error_event(ErrorSource::IsolationMonitor, error, true));
    }

    fn on_error_cleared(
        &self,
        _context: &Context,
        error: ::everestrs::ErrorType<generated::errors::isolation_monitor::Error>,
    ) {
        self.send(error_event(ErrorSource::IsolationMonitor, error, false));
    }
}

impl OverVoltageMonitorClientSubscriber for Intake {
    fn on_voltage_measurement_v(&self, _context: &Context, value: f64) {
        // Not diagnostic. `EvseManager` subscribes to this stream and feeds it
        // to a software watchdog that shadows the hardware monitor on the same
        // thresholds, so a monitor that stops raising is not the only thing
        // standing between the vehicle and an over voltage.
        self.send(Event::OverVoltageMeasurement { voltage_v: value });
    }

    fn on_error_raised(
        &self,
        _context: &Context,
        error: ::everestrs::ErrorType<generated::errors::over_voltage_monitor::Error>,
    ) {
        self.send(error_event(ErrorSource::OverVoltageMonitor, error, true));
    }

    fn on_error_cleared(
        &self,
        _context: &Context,
        error: ::everestrs::ErrorType<generated::errors::over_voltage_monitor::Error>,
    ) {
        self.send(error_event(ErrorSource::OverVoltageMonitor, error, false));
    }
}

impl PowerSupplyDcClientSubscriber for Intake {
    fn on_voltage_current(
        &self,
        _context: &Context,
        value: types::power_supply_DC::VoltageCurrent,
    ) {
        self.send(Event::SupplyVoltageCurrent {
            voltage_v: value.voltage_v,
            current_a: value.current_a,
        });
    }

    fn on_mode(&self, _context: &Context, value: types::power_supply_DC::Mode) {
        // The core commands the mode; the echo is confirmation, not input.
        log::debug!("power supply reports mode {value:?}");
    }

    /// `EvseManager.cpp:217-238`. The whole report travels: eight fields have a
    /// reader in the core and the rest are forwarded to the ISO 15118 stack
    /// unchanged, the way `call_set_powersupply_capabilities` does.
    ///
    /// Filled by name. Twenty four fields, most of them `Option<f64>` and many
    /// of them differing only in the words `min`, `max`, `export`, `import` and
    /// `nominal`, so a positional fill would transpose silently and compile.
    fn on_capabilities(&self, _context: &Context, value: types::power_supply_DC::Capabilities) {
        self.send(Event::PowerSupplyCapabilities(Box::new(
            PowerSupplyCapabilities {
                bidirectional: value.bidirectional,
                max_export_voltage_v: value.max_export_voltage_v,
                min_export_voltage_v: value.min_export_voltage_v,
                max_export_current_a: value.max_export_current_a,
                min_export_current_a: value.min_export_current_a,
                max_export_power_w: value.max_export_power_w,
                current_regulation_tolerance_a: value.current_regulation_tolerance_a,
                peak_current_ripple_a: value.peak_current_ripple_a,
                max_import_voltage_v: value.max_import_voltage_v,
                min_import_voltage_v: value.min_import_voltage_v,
                max_import_current_a: value.max_import_current_a,
                min_import_current_a: value.min_import_current_a,
                max_import_power_w: value.max_import_power_w,
                conversion_efficiency_export: value.conversion_efficiency_export,
                conversion_efficiency_import: value.conversion_efficiency_import,
                nominal_max_export_current_a: value.nominal_max_export_current_a,
                nominal_max_export_power_w: value.nominal_max_export_power_w,
                nominal_max_export_voltage_v: value.nominal_max_export_voltage_v,
                nominal_max_import_current_a: value.nominal_max_import_current_a,
                nominal_max_import_power_w: value.nominal_max_import_power_w,
                nominal_max_import_voltage_v: value.nominal_max_import_voltage_v,
                nominal_min_export_current_a: value.nominal_min_export_current_a,
                nominal_min_export_voltage_v: value.nominal_min_export_voltage_v,
                nominal_min_import_current_a: value.nominal_min_import_current_a,
                nominal_min_import_voltage_v: value.nominal_min_import_voltage_v,
            },
        )));
    }

    fn on_error_raised(
        &self,
        _context: &Context,
        error: ::everestrs::ErrorType<generated::errors::power_supply_dc::Error>,
    ) {
        self.send(error_event(ErrorSource::PowerSupplyDc, error, true));
    }

    fn on_error_cleared(
        &self,
        _context: &Context,
        error: ::everestrs::ErrorType<generated::errors::power_supply_dc::Error>,
    ) {
        self.send(error_event(ErrorSource::PowerSupplyDc, error, false));
    }
}

impl SlacClientSubscriber for Intake {
    /// `EvseManager.cpp:1238-1250`. The state is logged and two bits of it
    /// reach the module.
    ///
    /// The second bit is `slac_unmatched` (`:1241`, `:1244`), and its reader is
    /// the unplug arm at `EvseManager.cpp:1112-1116`: it takes a copy before
    /// `call_leave_bcd` overwrites it and issues `call_reset(false)` only when
    /// SLAC was still matched. It travels as the negation of `MatchingStarted`
    /// rather than as a third event, because the C++ derives both from the one
    /// `UNMATCHED` test; `HlcPort::note_matching_started` performs the
    /// inversion at the single place that stores it.
    fn on_state(&self, _context: &Context, value: types::slac::State) {
        log::debug!("slac state {value:?}");
        // Two facts from one report, which is what the C++ handler does too:
        // it calls `Charger::set_matching_started` and `set_slac_matched` from
        // the same arm, and `Charger` keeps both members. They are sent as two
        // events rather than one carrying the state, so each reaches the reader
        // that asked for it and neither reader has to re-derive the other's.
        self.send(Event::Hlc(HlcEvent::MatchingStarted(
            matching_started_from(&value),
        )));
        self.send(Event::Hlc(HlcEvent::SlacMatched(slac_matched_from(&value))));
    }

    /// `EvseManager.cpp:1232-1238`. Relayed to the stack unchanged. The C++
    /// gates the relay on `hlc_enabled`; the gate lives on `HlcPort` here, so
    /// the fact travels and the port decides whether anyone is listening.
    fn on_dlink_ready(&self, _context: &Context, value: bool) {
        log::debug!("slac data link ready {value}");
        self.send(Event::Hlc(HlcEvent::DataLinkReady(value)));
    }

    /// `subscribe_ev_mac_address` (`EvseManager.cpp:180-182`). The autocharge
    /// identity when it is taken from SLAC rather than from the ISO 15118
    /// stack.
    ///
    /// The C++ gates the subscription itself on three settings (`:179`). Only
    /// the `slac_enabled` one is answered here, by this callback existing at
    /// all; the other two decide what is done with the identity rather than
    /// whether it arrived, so the fact travels and the port decides. That also
    /// keeps the decision testable, which a gate at the subscription is not.
    fn on_ev_mac_address(&self, _context: &Context, value: String) {
        log::debug!("slac reported vehicle mac address");
        self.send(Event::Hlc(HlcEvent::VehicleMacAddress(value)));
    }

    /// `subscribe_request_error_routine` (`EvseManager.cpp:1253-1256`) into
    /// `Charger::request_error_sequence` (`Charger.cpp:2133-2147`), which is a
    /// different route from the data link error: it runs the `T_step_EF`
    /// detour back to whichever of `WaitingForAuthentication` or
    /// `PrepareCharging` the port is in, resets SLAC, and never touches the
    /// contactor permission.
    fn on_request_error_routine(&self, _context: &Context, _value: ()) {
        log::debug!("slac requested the error routine");
        self.send(Event::Hlc(HlcEvent::SlacErrorRoutine));
    }

    fn on_error_raised(
        &self,
        _context: &Context,
        error: ::everestrs::ErrorType<generated::errors::slac::Error>,
    ) {
        // `ErrorSource` has no SLAC variant: the C++ does not treat a SLAC
        // error as charging preventing either.
        log::warn!(
            "slac error raised: {}",
            error_type_string(&error.error_type)
        );
    }

    fn on_error_cleared(
        &self,
        _context: &Context,
        error: ::everestrs::ErrorType<generated::errors::slac::Error>,
    ) {
        log::info!(
            "slac error cleared: {}",
            error_type_string(&error.error_type)
        );
    }
}

impl KvsClientSubscriber for Intake {}

impl Iso15118ChargerClientSubscriber for Intake {
    fn on_evcc_id(&self, _context: &Context, value: String) {
        self.send(Event::Hlc(HlcEvent::SessionSetup { evcc_id: value }));
    }

    fn on_start_cable_check(&self, _context: &Context, _value: ()) {
        self.send(Event::Hlc(HlcEvent::RequiresCableCheck));
    }

    fn on_start_pre_charge(&self, _context: &Context, _value: ()) {
        self.send(Event::Hlc(HlcEvent::PreChargeStarted));
    }

    fn on_current_demand_started(&self, _context: &Context, _value: ()) {
        self.send(Event::Hlc(HlcEvent::CurrentDemandStarted));
    }

    fn on_current_demand_finished(&self, _context: &Context, _value: ()) {
        self.send(Event::Hlc(HlcEvent::CurrentDemandFinished));
    }

    fn on_requested_energy_transfer_mode(
        &self,
        _context: &Context,
        value: types::iso15118::EnergyTransferMode,
    ) {
        self.send(Event::Hlc(HlcEvent::ModeSelected {
            transfer: wire_string(&value),
        }));
    }

    fn on_ev_termination(&self, _context: &Context, value: types::iso15118::EvTermination) {
        log::info!(
            "EV terminated the session: code {:?}, explanation {:?}",
            value.ev_termination_code,
            value.ev_termination_explanation
        );
        self.send(Event::Hlc(HlcEvent::StopFromEv(StopReason::Local)));
    }

    /// `EvseManager.cpp:387-392`. The C++ also resets
    /// `selected_d20_energy_service`, which belongs to the ISO 15118-20 service
    /// selection and is unported: nothing here holds a selected service.
    fn on_dlink_terminate(&self, _context: &Context, _value: ()) {
        self.send(Event::Hlc(HlcEvent::DataLinkTerminate));
    }

    /// `EvseManager.cpp:372-379`.
    fn on_dlink_error(&self, _context: &Context, _value: ()) {
        self.send(Event::Hlc(HlcEvent::DataLinkError));
    }

    /// `EvseManager.cpp:380-386`.
    fn on_dlink_pause(&self, _context: &Context, _value: ()) {
        self.send(Event::Hlc(HlcEvent::DataLinkPause));
    }

    // The arms below are unported, not uninteresting. No decision was made to
    // discard any of them. Twenty three of the thirty one carry a fact the C++
    // acts on: each has a live `subscribe_<name>` handler in
    // `modules/EVSE/EvseManager/EvseManager.cpp` that writes a limit, a display
    // value, an authorization decision or a contactor permission. Until they
    // are ported, each of those logs on receipt so that a drop is visible
    // rather than silent, at `warn` for a fact arriving once or a few times per
    // session and at `debug` for a fact carried in the charge loop, where a
    // `warn` every 250 ms to 1 s would bury the log. The remaining eight have
    // no counterpart doing work, so they are not parity gaps and stay silent.
    // `tests::dropped_hlc_facts` pins that split against this source.
    //
    // Thirteen of the facts still dropped here are one class: the vehicle's own
    // reported figures. Six are DC, the bulk and full charge marks, energy and
    // remaining time; seven are AC, `ac_eamount`, the vehicle's voltage,
    // current and power extremes, its present powers and its dynamic control
    // mode request. Every one of them is stored in the C++ `ev_info` record and
    // republished (`EvseManager.cpp:454-526` for the AC seven), and none is
    // read by any decision in `modules/EVSE/EvseManager/`.
    //
    // They stay dropped because nothing has asked for them, not because the
    // record they belong to is missing: `Effect::PublishEvInfo` carries it, and
    // it carries the two fields that do have a source here. The record is per
    // field on the wire and both consumers read it one `has_value` at a time,
    // so each of these thirteen can be ported on its own the day it is wanted,
    // without waiting for the other twelve.

    /// `EvseManager.cpp:396-399`. Named for AC and wired for both charge
    /// modes: the subscription sits in the block gated on high level
    /// communication alone.
    fn on_ac_close_contactor(&self, _context: &Context, _value: ()) {
        self.send(Event::Hlc(HlcEvent::AllowCloseContactor(true)));
    }
    fn on_ac_eamount(&self, _context: &Context, _value: f64) {
        log::warn!("unported HLC fact dropped: ac_eamount");
    }
    fn on_ac_ev_dynamic_control_mode(
        &self,
        _context: &Context,
        _value: types::iso15118::AcEvDynamicModeValues,
    ) {
        log::debug!("unported HLC fact dropped: ac_ev_dynamic_control_mode");
    }
    fn on_ac_ev_max_current(&self, _context: &Context, _value: f64) {
        log::warn!("unported HLC fact dropped: ac_ev_max_current");
    }
    fn on_ac_ev_max_voltage(&self, _context: &Context, _value: f64) {
        log::warn!("unported HLC fact dropped: ac_ev_max_voltage");
    }
    fn on_ac_ev_min_current(&self, _context: &Context, _value: f64) {
        log::warn!("unported HLC fact dropped: ac_ev_min_current");
    }
    fn on_ac_ev_power_limits(&self, _context: &Context, _value: types::iso15118::AcEvPowerLimits) {
        log::warn!("unported HLC fact dropped: ac_ev_power_limits");
    }
    fn on_ac_ev_present_powers(
        &self,
        _context: &Context,
        _value: types::iso15118::AcEvPresentPowerValues,
    ) {
        log::debug!("unported HLC fact dropped: ac_ev_present_powers");
    }
    /// `EvseManager.cpp:401-404`. Withdraws the permission; it does not open
    /// the relays, which the pilot edge does.
    fn on_ac_open_contactor(&self, _context: &Context, _value: ()) {
        self.send(Event::Hlc(HlcEvent::AllowCloseContactor(false)));
    }
    /// `EvseManager.cpp:740-825`. Narrowed to the eight fields the handler
    /// reads; the six energy and departure figures beside them have no reader
    /// there.
    ///
    /// Filled by name. Six of the eight are same typed and differ only in
    /// `min`, `max`, `charge` and `discharge`, which is the shape that
    /// transposes silently.
    fn on_d_20_dc_dynamic_charge_mode(
        &self,
        _context: &Context,
        value: types::iso15118::DcChargeDynamicModeValues,
    ) {
        self.send(Event::Hlc(HlcEvent::DcDynamicChargeMode(
            DynamicModeRequest {
                max_charge_power_w: value.max_charge_power,
                min_charge_power_w: value.min_charge_power,
                max_charge_current_a: value.max_charge_current,
                max_voltage_v: value.max_voltage,
                min_voltage_v: value.min_voltage,
                max_discharge_power_w: value.max_discharge_power,
                min_discharge_power_w: value.min_discharge_power,
                max_discharge_current_a: value.max_discharge_current,
            },
        )));
    }
    fn on_dc_bulk_charging_complete(&self, _context: &Context, _value: bool) {}
    fn on_dc_bulk_soc(&self, _context: &Context, _value: f64) {
        log::warn!("unported HLC fact dropped: dc_bulk_soc");
    }
    fn on_dc_charging_complete(&self, _context: &Context, _value: bool) {}
    fn on_dc_ev_energy_capacity(&self, _context: &Context, _value: f64) {
        log::warn!("unported HLC fact dropped: dc_ev_energy_capacity");
    }
    fn on_dc_ev_energy_request(&self, _context: &Context, _value: f64) {
        log::debug!("unported HLC fact dropped: dc_ev_energy_request");
    }
    /// `EvseManager.cpp:851-870`. Two of the three fields travel: the current
    /// and voltage maxima clamp the vehicle's own target and the voltage is an
    /// input to the cable check voltage derivation. The power maximum has no
    /// reader in the C++ either beyond the `ev_info` republication, whose
    /// `maximum_power_limit` field this port has no source for.
    fn on_dc_ev_maximum_limits(
        &self,
        _context: &Context,
        value: types::iso15118::DcEvMaximumLimits,
    ) {
        self.send(Event::Hlc(HlcEvent::DcEvMaximumLimits(EvMaximumLimits {
            maximum_current_a: value.dc_ev_maximum_current_limit,
            maximum_voltage_v: value.dc_ev_maximum_voltage_limit,
        })));
    }
    fn on_dc_ev_present_voltage(&self, _context: &Context, _value: f64) {}
    fn on_dc_ev_remaining_time(
        &self,
        _context: &Context,
        _value: types::iso15118::DcEvRemainingTime,
    ) {
        log::debug!("unported HLC fact dropped: dc_ev_remaining_time");
    }
    /// `subscribe_dc_ev_status` as the `if (config.ac_with_soc)` block
    /// installs it. The state of charge is what the fake DC mode exists to
    /// collect; the ready flag and the error code beside it have no reader in
    /// the C++ either.
    fn on_dc_ev_status(&self, _context: &Context, value: types::iso15118::DcEvStatus) {
        self.send(Event::Hlc(HlcEvent::StateOfCharge {
            percent: value.dc_ev_ress_soc,
        }));
    }
    /// `EvseManager.cpp:734-738`. Stored raw and clamped downstream, so the
    /// re-apply watchdog can clamp it again against limits that changed since.
    fn on_dc_ev_target_voltage_current(
        &self,
        _context: &Context,
        value: types::iso15118::DcEvTargetValues,
    ) {
        self.send(Event::Hlc(HlcEvent::DcEvTarget {
            voltage_v: value.dc_ev_target_voltage,
            current_a: value.dc_ev_target_current,
        }));
    }
    fn on_dc_full_soc(&self, _context: &Context, _value: f64) {
        log::warn!("unported HLC fact dropped: dc_full_soc");
    }
    /// `EvseManager.cpp:827-833`. A different fact from an open contactor
    /// permission despite the name: it de-energizes rather than withdrawing a
    /// permission. The C++ subscribes it only on a DC port; here it always
    /// travels and the AC path states its own inertness.
    fn on_dc_open_contactor(&self, _context: &Context, _value: ()) {
        self.send(Event::Hlc(HlcEvent::OpenContactorDc));
    }
    fn on_departure_time(&self, _context: &Context, _value: String) {
        log::warn!("unported HLC fact dropped: departure_time");
    }
    fn on_display_parameters(
        &self,
        _context: &Context,
        _value: types::iso15118::DisplayParameters,
    ) {
    }
    fn on_ev_app_protocol(&self, _context: &Context, _value: types::iso15118::AppProtocols) {}
    /// `EvseManager.cpp:365-370`. Republished on this module's own interface
    /// with the session identity attached, which the core supplies.
    fn on_hlc_session_failed(
        &self,
        _context: &Context,
        value: types::evse_manager::HlcSessionFailedReasonEnum,
    ) {
        self.send(Event::Hlc(HlcEvent::SessionFailed(
            hlc_session_failure_from(&value),
        )));
    }
    fn on_meter_info_requested(&self, _context: &Context, _value: ()) {}
    /// `EvseManager.cpp:998-1014`. The request carries nothing; who the vehicle
    /// is arrived earlier with the session setup.
    fn on_require_auth_eim(&self, _context: &Context, _value: ()) {
        self.send(Event::Hlc(HlcEvent::RequireAuthEim));
    }
    /// `EvseManager.cpp:1030-1045`. The token travels as its own serialization,
    /// because the core republishes it without reading it; see `OpaqueToken`.
    fn on_require_auth_pnc(
        &self,
        _context: &Context,
        value: types::authorization::ProvidedIdToken,
    ) {
        match ::everestrs::serde_json::to_value(&value) {
            Ok(payload) => self.send(Event::Hlc(HlcEvent::RequireAuthPlugAndCharge {
                token: OpaqueToken::new(payload),
            })),
            // A token that cannot be carried is a token that cannot be offered
            // to `Auth` either, so the request is dropped rather than answered
            // with something the vehicle did not present.
            Err(error) => log::error!("a plug and charge request could not be carried: {error}"),
        }
    }
    /// `EvseManager.cpp:931-942`. Payloadless on the wire: the stack signals
    /// that the mode the EVSE announced at `call_setup` is now running, and
    /// which mode that is came from configuration.
    ///
    /// The C++ callback also calls `setup_v2h_mode` when the configured mode is
    /// V2H (`:934-935`), which rewrites the local energy limits. That half is
    /// not ported: the energy flow request derives its budget from the
    /// capability reports and takes no externally set limits, so there is
    /// nothing for the rewrite to reach. See `docs/architecture.md`.
    fn on_sae_bidi_mode_active(&self, _context: &Context, _value: ()) {
        self.send(Event::Hlc(HlcEvent::SaeBidiModeActive));
    }
    fn on_selected_payment_option(
        &self,
        _context: &Context,
        _value: types::iso15118::PaymentOption,
    ) {
    }
    /// `subscribe_selected_protocol` (`EvseManager.cpp:1070-1071`), which is
    /// one assignment to the field the `evse_manager` interface reports.
    ///
    /// Travels through the core rather than being republished from here,
    /// because it is not the value the interface carries: `selected_protocol`
    /// is state with five writers and this handler is one of them. The core
    /// owns the value (`core::protocol`) and decides when it reaches the wire.
    fn on_selected_protocol(&self, _context: &Context, value: String) {
        self.send(Event::Hlc(HlcEvent::SelectedProtocol(value)));
    }
    /// `EvseManager.cpp:961-969`. Registered outside the charge mode branch,
    /// so it is active for an AC port and a DC port alike, and the fact it
    /// carries is read in both: the AC limit spelling and the meter present
    /// power, whose gate is any ISO 15118-20 service and not an AC one.
    ///
    /// Only the energy transfer field is carried. Of the handler's other three
    /// statements, one raises the charger's ISO 15118-20 flag, which is not
    /// ported (see `HlcPort::note_selected_service`), and two write a session
    /// log line naming the same value.
    fn on_selected_service_parameters(
        &self,
        _context: &Context,
        value: types::iso15118::SelectedServiceParameters,
    ) {
        self.send(Event::Hlc(HlcEvent::SelectedService(
            selected_service_from(&value.energy_transfer),
        )));
    }
    fn on_supported_app_protocols_secc(
        &self,
        _context: &Context,
        _value: types::iso15118::SupportedAppProtocols,
    ) {
    }
    /// `EvseManager::log_v2g_message` (`EvseManager.cpp:1873-1889`), the only
    /// source of protocol payloads for the transcript and the only six argument
    /// session log call site in the C++.
    ///
    /// The C++ installs this subscription only when `config.session_logging` is
    /// set (`:1048`) and then re-checks the same key inside the handler
    /// (`:1874`). An everestrs subscription is static, so the gate is the
    /// logger: it discards a record it was not asked for and one that arrives
    /// outside a session, which is the same two conditions its `output` checks
    /// (`SessionLog.cpp:197`).
    ///
    /// The four payload representations are reduced to owned strings here, as
    /// `:1878-1881` reduces the same four optionals with `value_or("")`. Filled
    /// by name: `exi` is the hex form and `exi_base64` the base64 one, and both
    /// are `Option<String>` on the wire, so a positional literal would compile
    /// with the two encodings swapped.
    fn on_v_2_g_messages(&self, _context: &Context, value: types::iso15118::V2gMessages) {
        self.send(Event::V2gMessage(Box::new(V2gMessage {
            id: v2g_message_id(&value.id),
            xml: value.xml.unwrap_or_default(),
            json: value.v_2_g_json.unwrap_or_default(),
            exi_hex: value.exi.unwrap_or_default(),
            exi_base64: value.exi_base_64.unwrap_or_default(),
        })));
    }
    /// `EvseManager.cpp:394` into `Charger::set_hlc_charging_active`
    /// (`Charger.cpp:2118-2121`), the only producer of that fact.
    fn on_v_2_g_setup_finished(&self, _context: &Context, _value: ()) {
        self.send(Event::Hlc(HlcEvent::SetupFinished));
    }

    fn on_error_raised(
        &self,
        _context: &Context,
        error: ::everestrs::ErrorType<generated::errors::iso_15118_charger::Error>,
    ) {
        log::warn!(
            "high level communication error raised: {}",
            error_type_string(&error.error_type)
        );
    }

    fn on_error_cleared(
        &self,
        _context: &Context,
        error: ::everestrs::ErrorType<generated::errors::iso_15118_charger::Error>,
    ) {
        log::info!(
            "high level communication error cleared: {}",
            error_type_string(&error.error_type)
        );
    }
}

// Provided interfaces. A command that only instructs pushes an `Event::Command`
// and returns immediately: the answer the caller gets is acceptance of the
// request, not its completion. The core is the single writer, so nothing here
// can read charging state back synchronously.

impl AuthTokenProviderServiceSubscriber for Intake {}

impl EnergyServiceSubscriber for Intake {
    fn enforce_limits(
        &self,
        _context: &Context,
        value: types::energy::EnforcedLimits,
    ) -> ::everestrs::Result<()> {
        self.send(Event::EnforcedLimits(Box::new(EnforcedLimits {
            uuid: value.uuid,
            valid_for_s: value.valid_for,
            schedule: value
                .schedule
                .into_iter()
                .map(|entry| ScheduleResEntry {
                    timestamp: entry.timestamp,
                    limits_to_root: limits_res_from(entry.limits_to_root),
                    price_per_kwh: entry.price_per_kwh.map(price_per_kwh_from),
                })
                .collect(),
            limits_root_side: limits_res_from(value.limits_root_side),
        })));
        Ok(())
    }
}

impl EvseManagerServiceSubscriber for Intake {
    fn authorize_response(
        &self,
        _context: &Context,
        provided_token: types::authorization::ProvidedIdToken,
        validation_result: types::authorization::ValidationResult,
    ) -> ::everestrs::Result<()> {
        // The whole record, because it reaches the wire again on three session
        // events. The authorization kind used to be computed here and sent
        // beside the token's value; `AuthorizationKind::of` reads it off the
        // carried record in the core instead, so one input yields one answer.
        let Some(token) = id_tag_from(&provided_token) else {
            // A verdict whose identity cannot be carried is not applied: the
            // transaction it would open must name a token, and this is the
            // only route that supplies one.
            return Ok(());
        };
        self.send(Event::Command(Command::AuthorizeResponse {
            token,
            status: authorization_status_of(validation_result.authorization_status),
            certificate: validation_result
                .certificate_status
                .map(certificate_status_of),
            // The verdict's own tariff text, in wire order and verbatim. Which
            // message the metering transaction is opened under is `core`'s
            // decision (`TariffMessages::text`), so the whole list travels and
            // nothing here picks one; `format` and `language` reach no field on
            // the powermeter interface and are named as dropped in
            // `TariffMessages`.
            tariff: TariffMessages::new(
                validation_result
                    .tariff_messages
                    .into_iter()
                    .map(|message| message.content)
                    .collect(),
            ),
            // `evse_managerImpl::handle_authorize_response` re-arms this id on
            // the module itself (`:453-461`), so it is a fact about the
            // session and not a field of the verdict's payload.
            reservation_id: validation_result.reservation_id,
        }));
        Ok(())
    }

    fn withdraw_authorization(&self, _context: &Context) -> ::everestrs::Result<()> {
        self.send(Event::Command(Command::WithdrawAuthorization));
        Ok(())
    }

    fn enable_disable(
        &self,
        _context: &Context,
        cmd_source: types::evse_manager::EnableDisableSource,
        connector_id: i64,
    ) -> ::everestrs::Result<bool> {
        let source = enable_source_from(&cmd_source.enable_source);
        let priority = cmd_source.enable_priority;
        // `connector_id` is the enable scope on the wire. `EnableScope` names it
        // in the core, which is where the arbitration lives.
        let scope = EnableScope::from_wire(connector_id);
        // All three states travel; see `enable_state_from`.
        let state = enable_state_from(&cmd_source.enable_state);
        // The wire contract is the resulting state, which only the enable table
        // in the core knows, so this waits for the arbitration rather than
        // echoing the request. The two differ whenever another source outranks
        // this one: `Charger::enable_disable` returns `is_enabled` from the
        // table (`Charger.cpp:1774`), not the state it was handed.
        Ok(self.send_awaiting(Command::EnableDisable {
            source,
            state,
            priority,
            scope,
        }))
    }

    /// `mod->reserve(id, true)` and nothing else
    /// (`evse/evse_managerImpl.cpp:481-483`), so the verdict is the core's: it
    /// refuses a port that is not idle and one already held under a different
    /// id. Answering before it had decided reported those refusals as success,
    /// and a CSMS reads a reservation result as a durable fact.
    fn reserve(&self, _context: &Context, reservation_id: i64) -> ::everestrs::Result<bool> {
        Ok(self.send_awaiting(Command::Reserve { reservation_id }))
    }

    fn cancel_reservation(&self, _context: &Context) -> ::everestrs::Result<()> {
        self.send(Event::Command(Command::CancelReservation));
        Ok(())
    }

    /// Both answer whether a transaction was open, which is the whole of
    /// `Charger::pause_charging` and `::resume_charging`
    /// (`Charger.cpp:1330-1345`).
    fn pause_charging(&self, _context: &Context) -> ::everestrs::Result<bool> {
        Ok(self.send_awaiting(Command::PauseCharging))
    }

    fn resume_charging(&self, _context: &Context) -> ::everestrs::Result<bool> {
        Ok(self.send_awaiting(Command::ResumeCharging))
    }

    fn stop_transaction(
        &self,
        _context: &Context,
        request: types::evse_manager::StopTransactionRequest,
    ) -> ::everestrs::Result<bool> {
        // `Charger::cancel_transaction` sits entirely inside its
        // `flag_transaction_active` guard and answers false when it does not
        // hold (`Charger.cpp:1367-1394`), so a stop that finds nothing open is
        // a refusal on the wire and not a silent no-op.
        Ok(self.send_awaiting(Command::StopTransaction {
            reason: stop_transaction_reason_from(request.reason),
            // `evse_managerImpl::handle_stop_transaction` hands
            // `request.id_tag` straight to `Charger::cancel_transaction`, which
            // stores it as `stop_transaction_id_token`. Absent when the stop
            // did not come from a local token, which is what the field means.
            id_tag: request.id_tag.as_ref().and_then(id_tag_from),
        }))
    }

    /// `evse_managerImpl::handle_force_unlock`, which does nothing and answers
    /// `false` when no connector lock is connected: the return value reports
    /// whether there was a lock to open, and OCPP renders a `false` as
    /// `UnlockStatus::NotSupported`. A fixed cable port is the deployment that
    /// has none, so answering `true` there claims an unlock that no hardware
    /// performed.
    fn force_unlock(&self, _context: &Context, connector_id: i64) -> ::everestrs::Result<bool> {
        log::debug!("force_unlock scope from connector_id {connector_id}");
        if !self.connector_lock_wired() {
            return Ok(false);
        }
        self.send(Event::Command(Command::ForceUnlock));
        Ok(true)
    }

    fn external_ready_to_start_charging(&self, _context: &Context) -> ::everestrs::Result<bool> {
        self.send(Event::Command(Command::ExternalReadyToStartCharging));
        Ok(self.settings.misc.external_ready_to_start_charging)
    }

    fn get_evse(&self, _context: &Context) -> ::everestrs::Result<types::evse_manager::Evse> {
        // Identity only, which is all this command carries. The connector set is
        // the single connector the module implements, asserted at startup by
        // `config::resolve`.
        Ok(types::evse_manager::Evse {
            id: self.settings.legacy_topic_id,
            // Both optional on the wire but always sent, as
            // `evse_managerImpl::handle_get_evse` sends them: each is a
            // manifest key with a default, so neither is ever absent.
            evse_id: Some(self.settings.evse_id.clone()),
            evse_id_din: Some(self.settings.evse_id_din.clone()),
            connectors: vec![types::evse_manager::Connector {
                id: 1,
                r#type: everestrs::serde_yaml::from_str(&self.settings.connector_type).ok(),
                // Both static, as `handle_get_evse` fills them: settled at
                // configuration time, so neither can be read from a session.
                charge_mode: match self.settings.charge_mode {
                    ChargeMode::Ac => types::evse_manager::ChargeMode::AC,
                    ChargeMode::Dc => types::evse_manager::ChargeMode::DC,
                },
                // `mod->is_hlc_enabled()`, which here is the existence of the
                // configuration, the same guard `set_der_available` reads. The
                // C++ default initializes `hlc_enabled` to false so a read
                // before `init` answers false; an unfilled cell answers the
                // same way.
                hlc_capable: matches!(self.hlc.get(), Some(Some(_))),
            }],
        })
    }

    /// `evse/evse_managerImpl.cpp:511-524`, which forwards each field that is
    /// present to its own setter and answers nothing.
    ///
    /// The state it writes is read at the next session setup trigger point, so
    /// a change made mid session reaches the vehicle at the next session start
    /// or finish and not before. That is the C++ behavior too: its three
    /// setters write atomics and re-derive nothing.
    fn set_plug_and_charge_configuration(
        &self,
        _context: &Context,
        plug_and_charge_configuration: types::evse_manager::PlugAndChargeConfiguration,
    ) -> ::everestrs::Result<()> {
        self.send(Event::Command(Command::SetPlugAndChargeConfiguration(
            plug_and_charge_from(&plug_and_charge_configuration),
        )));
        Ok(())
    }

    fn update_allowed_energy_transfer_modes(
        &self,
        _context: &Context,
        allowed_energy_transfer_modes: Vec<types::iso15118::EnergyTransferMode>,
    ) -> ::everestrs::Result<types::evse_manager::UpdateAllowedEnergyTransferModesResult> {
        // The verdict is decided here because the command answers its caller
        // synchronously and the writer thread cannot be waited on. It is a pure
        // function of the connector type and the request, both of which the
        // intake holds, so there is no second definition of the filter: only
        // the accepted list travels on, and the core's arm is the call.
        let requested: Vec<EnergyTransferMode> = allowed_energy_transfer_modes
            .iter()
            .map(transfer_mode_from)
            .collect();
        // Before `hlc` is filled nothing is known about what is connected,
        // which is the same window the enable answer above documents.
        let Some(Some(config)) = self.hlc.get() else {
            return Ok(types::evse_manager::UpdateAllowedEnergyTransferModesResult::NoHlc);
        };
        match RsEvseManager::core::hlc::filter_allowed(config, &requested) {
            Ok(accepted) => {
                self.send(Event::Command(Command::UpdateAllowedTransferModes(
                    accepted,
                )));
                Ok(types::evse_manager::UpdateAllowedEnergyTransferModesResult::Accepted)
            }
            Err(refusal) => Ok(update_refusal_enum(refusal)),
        }
    }

    /// `evse/evse_managerImpl.cpp:567-576`.
    ///
    /// The refusal is decided here rather than in the core, because the command
    /// answers its caller synchronously and the writer thread cannot be waited
    /// on. It is the same guard the C++ applies, `is_hlc_enabled()`
    /// (`EvseManager.hpp:298`), and here that guard is the existence of the
    /// configuration: a deployment with no stack holds none. Before `hlc` is
    /// filled nothing is known about what is connected, which answers the same
    /// way and is the window the two commands above document.
    fn set_der_available(
        &self,
        _context: &Context,
        available: bool,
    ) -> ::everestrs::Result<types::evse_manager::SetDerAvailableResult> {
        let Some(Some(_)) = self.hlc.get() else {
            return Ok(types::evse_manager::SetDerAvailableResult::NoHlc);
        };
        self.send(Event::Command(Command::SetDerAvailable(available)));
        Ok(types::evse_manager::SetDerAvailableResult::Accepted)
    }
}

/// The four command handlers are each one state change and no output
/// (`random_delay/uk_random_delayImpl.cpp:15-31`), so all four post and
/// return. What a caller observes is the next enforced limit, which is where
/// the countdown is published from.
impl UkRandomDelayServiceSubscriber for Intake {
    fn enable(&self, _context: &Context) -> ::everestrs::Result<()> {
        self.send(Event::Command(Command::RandomDelayEnable));
        Ok(())
    }

    fn disable(&self, _context: &Context) -> ::everestrs::Result<()> {
        self.send(Event::Command(Command::RandomDelayDisable));
        Ok(())
    }

    fn cancel(&self, _context: &Context) -> ::everestrs::Result<()> {
        self.send(Event::Command(Command::RandomDelayCancel));
        Ok(())
    }

    /// The value is passed on as it arrives. The interface bounds nothing
    /// (`interfaces/uk_random_delay.yaml`) and neither does the C++ handler,
    /// so refusing here would leave a caller believing a maximum the port does
    /// not have; what zero and negatives mean is decided where the delay is
    /// drawn (`core::energy::random_delay::RandomDelay::draw`).
    fn set_duration_s(&self, _context: &Context, value: i64) -> ::everestrs::Result<()> {
        self.send(Event::Command(Command::RandomDelaySetDuration(value)));
        Ok(())
    }
}

impl DcExternalDerateServiceSubscriber for Intake {
    /// `dc_external_derateImpl::handle_set_external_derating`
    /// (`dc_external_derate/dc_external_derateImpl.cpp:15-17`), which forwards
    /// to `EvseManager::set_external_derating` (`EvseManager.cpp:2701-2704`).
    ///
    /// A store and nothing else, so the command answers its caller before the
    /// reducer has looked at it, exactly as the C++ answers before any reader
    /// has. There is no verdict to decide here and none to report: the C++
    /// accepts every request, including one that caps a quantity the supply
    /// does not have.
    ///
    /// Filled by name. All four wire fields are `Option<f64>` and the generated
    /// struct orders them alphabetically, so the two directions sit adjacent
    /// and a positional literal would compile with import and export swapped.
    fn set_external_derating(
        &self,
        _context: &Context,
        derate: types::dc_external_derate::ExternalDerating,
    ) -> ::everestrs::Result<()> {
        self.send(Event::Command(Command::SetExternalDerating(
            ExternalDerating {
                max_export_current_a: derate.max_export_current_a,
                max_export_power_w: derate.max_export_power_w,
                max_import_current_a: derate.max_import_current_a,
                max_import_power_w: derate.max_import_power_w,
            },
        )));
        Ok(())
    }
}

/// Performs effects against the outside world. One thread of this runs safety
/// actuation, one runs publishes on this module's own interface in dispatch
/// order, and one per device and one for the store run the outbound calls.
///
/// An effect whose requirement is not wired is `Ok`, not `Failed`: the core
/// already refused to select a path that needs it. See `config::resolve`.
///
/// `Effect::StartTimer`, `Effect::CancelTimer` and `Effect::AnswerCommand`
/// never reach here. The writer handles all three itself, because arming a
/// deadline and answering a blocked caller are each a lock plus a notify and
/// must not be able to queue behind a blocked peer.
///
/// Two wire payloads are wider than the `Effect` variant that drives them, so
/// they are filled from what is available and named here rather than being
/// silently invented. Both are fields on `Effect` in `src/core/effect.rs`:
///
/// - `AllowPowerOn(bool)` has no reason, so the DC cable check and precharge
///   reasons the interface distinguishes cannot be produced.
/// - `AllowPowerOn(bool)` above is the only one left of this kind.
struct EverestEffects {
    publishers: ModulePublisher,
    settings: Arc<Settings>,
    /// This module's configured instance id, `mod->info.id`. One reader, the
    /// `limits` publication, which names the node the figure belongs to
    /// (`evse/evse_managerImpl.cpp:402`). It is **not** the EVSE id: that
    /// names the charge point to a driver, this names the module instance to
    /// the other modules, and a consumer correlating the two streams by it
    /// gets a different answer from each.
    node_id: String,
    /// See [`BillingMeter`]. Read here and written nowhere, which is what makes
    /// the payload a reading of the meter rather than a second account of it.
    billing_meter: BillingMeter,
    /// See [`EnergyUsage`]. Read here and written nowhere, for the same reason.
    energy_usage: Arc<Mutex<EnergyUsage>>,
    /// See [`SignedMeterValues`]. Behind a mutex because `EffectRunner::run`
    /// takes `&self`; uncontended, because the three effects that touch it are
    /// all `ExecContext::Publish` and one thread runs that lane.
    signed_meter_values: Mutex<SignedMeterValues>,
    /// The per session transcript (`modules/EVSE/EvseManager/SessionLog.cpp`).
    ///
    /// It lives here because it needs the two things `core` refuses to hold: a
    /// wall clock and a filesystem. The core decides every record and their
    /// order and hands them over as `Effect::SessionLog`; this side stamps the
    /// time, writes the files and renders the EVerest log line.
    ///
    /// Behind a mutex because `EffectRunner::run` takes `&self` and is called
    /// from every lane. Uncontended in practice: `Effect::SessionLog` is
    /// classified `ExecContext::Publish`, so one thread reaches it, and that is
    /// also what keeps the transcript in order and lets a start be read back by
    /// the `PublishSessionEvent` behind it.
    ///
    /// `None` on a deployment with `session_logging` off. A logger that exists
    /// is enabled, so nothing here asks whether it is; see
    /// `SessionLogger::for_settings`. The field held an inert logger until
    /// then, which is the shape this whole file's 447 lines shipped in for six
    /// days with no constructor call in `main` at all.
    session_log: Mutex<Option<SessionLogger>>,
}

impl EverestEffects {
    fn timestamp() -> String {
        chrono::Utc::now().to_rfc3339_opts(chrono::SecondsFormat::Millis, true)
    }

    /// The billing meter's latest whole reading, or a reading with nothing in
    /// it when none has arrived.
    ///
    /// The empty answer is the C++ answer and not a stand in:
    /// `latest_powermeter_data_billing` is a default constructed `Powermeter`
    /// until the first `subscribe_powermeter` callback replaces it, and
    /// `get_latest_powermeter_data_billing` returns it either way. A port with
    /// no meter wired at all publishes that value for the whole of its life.
    fn meter_value(&self) -> types::powermeter::Powermeter {
        self.billing_meter
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner())
            .clone()
            .unwrap_or_else(|| empty_powermeter(Self::timestamp()))
    }

    /// See [`SignedMeterValues`].
    fn signed(&self) -> std::sync::MutexGuard<'_, SignedMeterValues> {
        self.signed_meter_values
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner())
    }

    /// `Charger.cpp:1480-1481`, the two statements ahead of the request.
    ///
    /// A record that opens starts with neither value, so a start the meter
    /// refused or does not support cannot leave the previous transaction's
    /// signature on this one's announcement.
    fn forget_signed_meter_values(&self) {
        *self.signed() = SignedMeterValues::default();
    }

    /// `Charger.cpp:1509`, and inside the `OK` arm there as it is here: a meter
    /// that answers `NOT_SUPPORTED` signs nothing, and an `UNEXPECTED_ERROR`
    /// answer is a refusal rather than a signature.
    fn note_start_signature(&self, signed: Option<types::units_signed::SignedMeterValue>) {
        self.signed().start = signed;
    }

    /// `Charger.cpp:1537-1541`, the `OK` arm of the stop.
    ///
    /// The start is filled only when the start did not fill it, which is the
    /// C++'s own guard and the whole of what
    /// `test_meter_signed_meter_values_no_start` measures: a meter configured
    /// to sign nothing at the start reports the opening reading on the close
    /// instead, and the `TransactionFinished` has to carry both.
    fn note_stop_signature(
        &self,
        recovered_start: Option<types::units_signed::SignedMeterValue>,
        signed: Option<types::units_signed::SignedMeterValue>,
    ) {
        let mut values = self.signed();
        if values.start.is_none() {
            values.start = recovered_start;
        }
        values.stop = signed;
    }

    /// The `SessionStarted` payload. Absent on every other event.
    ///
    /// `AuthHandler.cpp:830` reads `reason` with an unconditional `.value()`, so
    /// the start must carry this or the Auth module aborts on it.
    ///
    /// `signed_meter_value` is absent because the C++ leaves it absent here
    /// too: its session started connection assigns every other field of the
    /// payload and never that one.
    fn session_started(
        &self,
        report: &SessionEventReport,
    ) -> Option<types::evse_manager::SessionStarted> {
        let reason = report.started?;
        // The two remaining fields come from the payload the core decided,
        // which is present on exactly this event; see `Core::session_payload`.
        let (reservation_id, id_tag) = match &report.payload {
            Some(SessionPayload::Started {
                reservation_id,
                id_tag,
            }) => (*reservation_id, id_tag.as_ref().and_then(provided_id_token)),
            _ => (None, None),
        };
        Some(types::evse_manager::SessionStarted {
            reason: start_session_reason(reason),
            meter_value: self.meter_value(),
            id_tag,
            // The directory the transcript actually opened, which the C++
            // gets from `startSession` returning it
            // (`evse/evse_managerImpl.cpp:169-174`). Absent when logging is
            // off, and absent when it was on and could not open, so a
            // consumer can tell a written transcript from a failed one
            // without reading the log.
            logging_path: self.session_log_dir(),
            reservation_id,
            signed_meter_value: None,
        })
    }

    /// The `TransactionStarted` payload. Absent on every other event.
    ///
    /// `signed_meter_value` is `Charger::get_start_signed_meter_value`
    /// (`evse/evse_managerImpl.cpp:207`), read out of [`SignedMeterValues`].
    /// It is filled rather than absent because the core now pushes
    /// `Effect::StartTransaction` ahead of this announcement and both run on
    /// the one publish lane, so the meter has answered by the time this is
    /// built. `Core::start_transaction` and `Effect::context` are the two
    /// halves of that.
    fn transaction_started(
        &self,
        report: &SessionEventReport,
    ) -> Option<types::evse_manager::TransactionStarted> {
        let Some(SessionPayload::TransactionStarted {
            id_tag,
            reservation_id,
        }) = &report.payload
        else {
            return None;
        };
        Some(types::evse_manager::TransactionStarted {
            // Required by the wire type, so a token that cannot be rendered
            // takes the payload with it rather than reaching a consumer as a
            // token nobody presented. `provided_id_token` says why that cannot
            // happen without the interface having changed under a running
            // process.
            id_tag: provided_id_token(id_tag)?,
            meter_value: self.meter_value(),
            reservation_id: *reservation_id,
            signed_meter_value: self.signed().start.clone(),
        })
    }

    /// The meter snapshot the `signal_simple_event` subscriber stamps onto
    /// three of its arms, and `None` for every other event.
    ///
    /// `evse/evse_managerImpl.cpp:352-361` fills
    /// `authorization_event.meter_value` for the two authorization
    /// announcements and `charging_state_changed_event.meter_value` for the
    /// three charging state ones, from the same
    /// `get_latest_powermeter_data_billing` the rest of this boundary reads.
    /// `ChargingPausedEVSE` is in the second group although that lambda's arm
    /// for it never runs: its own signal fills the same field
    /// (`:314-316`), so the group is the same three either way.
    ///
    /// One function for the pair because the two wire types are distinct and
    /// structurally identical, so a snapshot put on the wrong one compiles and
    /// publishes a field no consumer of that event reads.
    fn authorization_event(
        &self,
        report: &SessionEventReport,
    ) -> Option<types::evse_manager::AuthorizationEvent> {
        matches!(
            report.event,
            SessionEvent::Authorized | SessionEvent::Deauthorized
        )
        .then(|| types::evse_manager::AuthorizationEvent {
            meter_value: self.meter_value(),
        })
    }

    /// The other half of the pair above. See `authorization_event`.
    fn charging_state_changed_event(
        &self,
        report: &SessionEventReport,
    ) -> Option<types::evse_manager::ChargingStateChangedEvent> {
        matches!(
            report.event,
            SessionEvent::ChargingStarted
                | SessionEvent::ChargingPausedEv
                | SessionEvent::ChargingPausedEvse
        )
        .then(|| types::evse_manager::ChargingStateChangedEvent {
            meter_value: self.meter_value(),
        })
    }

    /// The reading a session closes on
    /// (`evse/evse_managerImpl.cpp:333-336`), which is the last one this
    /// module publishes for the session and the one a consumer settles the
    /// energy total from.
    fn session_finished(
        &self,
        report: &SessionEventReport,
    ) -> Option<types::evse_manager::SessionFinished> {
        (report.event == SessionEvent::SessionFinished).then(|| {
            types::evse_manager::SessionFinished {
                meter_value: self.meter_value(),
            }
        })
    }

    /// The pause reason set, on the one event that carries one.
    ///
    /// `evse/evse_managerImpl.cpp:320` puts the set the charger's own signal
    /// hands it straight onto this field. The order is the C++ push order and
    /// travels from `Core::pause_reasons`, because the wire type is a list and
    /// a consumer reading its first entry reads the same one either side.
    fn charging_paused_evse(
        &self,
        report: &SessionEventReport,
    ) -> Option<types::evse_manager::ChargingPausedEVSEReasons> {
        let Some(SessionPayload::ChargingPausedEvse { reasons }) = &report.payload else {
            return None;
        };
        Some(types::evse_manager::ChargingPausedEVSEReasons {
            reasons: reasons.iter().copied().map(pause_reason_wire).collect(),
        })
    }

    /// The `TransactionFinished` payload. Absent on every other event.
    ///
    /// The two signed values are `Charger::get_stop_signed_meter_value` and
    /// `Charger::get_start_signed_meter_value` in that order
    /// (`evse/evse_managerImpl.cpp:279-280`), so the field named for the start
    /// carries the opening reading and the bare one carries the closing
    /// reading. Swapping them would publish a well formed record of the wrong
    /// energy, which is why they are named here rather than passed positionally.
    fn transaction_finished(
        &self,
        report: &SessionEventReport,
    ) -> Option<types::evse_manager::TransactionFinished> {
        let Some(SessionPayload::TransactionFinished { reason, id_tag }) = &report.payload else {
            return None;
        };
        let signed = self.signed();
        Some(types::evse_manager::TransactionFinished {
            meter_value: self.meter_value(),
            reason: Some(stop_transaction_reason_wire(*reason)),
            id_tag: id_tag.as_ref().and_then(provided_id_token),
            signed_meter_value: signed.stop.clone(),
            start_signed_meter_value: signed.start.clone(),
        })
    }

    /// The open transcript's directory, if one is open.
    fn session_log_dir(&self) -> Option<String> {
        self.session_log
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner())
            .as_ref()
            .and_then(|logger| logger.session_dir())
            .map(str::to_string)
    }

    /// Carry out one transcript instruction.
    ///
    /// Always `EffectOutcome::Ok`. A transcript that cannot be written is
    /// reported to the logger, which logs it, names the path and the reason, and
    /// abandons the session's transcript; nothing is handed back to the core.
    /// That is the C++ behavior made structural: every one of its seven
    /// filesystem failure paths logs and continues, and there is no route by
    /// which a logging fault can stop a charge.
    fn session_log(&self, request: &SessionLogEffect) -> EffectOutcome {
        let now = Self::timestamp();
        let mut held = self
            .session_log
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        // A deployment with logging off holds no logger, so there is nothing
        // to instruct. The core still emits the records, which is the C++
        // shape: `SessionLog::evse` and friends are called unconditionally and
        // the object decides. Here the object's absence decides.
        let Some(logger) = held.as_mut() else {
            return EffectOutcome::Ok;
        };
        let output = match request {
            SessionLogEffect::Start { session_uuid } => logger.start_session(
                &now,
                &session_log::suffix_for(&self.settings.logging.logfile_suffix, session_uuid),
            ),
            SessionLogEffect::Stop => logger.stop_session(&now),
            SessionLogEffect::Record {
                origin,
                iso15118,
                msg,
                payload,
            } => {
                let owned = payload.as_deref().cloned().unwrap_or_default();
                let payload = owned.borrow();
                match origin {
                    session_log::Origin::Evse => logger.evse(&now, *iso15118, msg, payload),
                    session_log::Origin::Car => logger.car(&now, *iso15118, msg, payload),
                    session_log::Origin::Sys => logger.sys(&now, msg),
                }
            }
        };

        for line in &output.lines {
            log::info!("{}", line.render());
        }
        // The C++ hands each of these to an MQTT functor that publishes
        // `everest_api/<module id>/var/hlc_log` (`SessionLog.cpp:249`, wired at
        // `EvseManager.cpp:138-141`). Unported: everestrs exposes
        // `publish_variable` on declared interfaces and nothing else, so a Rust
        // module has no binding that can reach an external topic. The core
        // still produces the bodies, so wiring this is one call once it does.
        // Recorded in `docs/architecture.md`.
        let _unpublished = &output.publications;

        for action in &output.actions {
            if let Err(error) = perform_log_action(action) {
                logger.note_failure(action, &error.to_string());
            }
        }
        EffectOutcome::Ok
    }

    /// The core named an error type that this interface does not declare, so
    /// nothing can be raised for it. Reported rather than dropped: an error the
    /// module decided on and could not announce is a defect in the pair.
    fn unraisable(report: &ErrorReport) -> EffectOutcome {
        log::error!("no evse_manager error matches {:?}", report.error_type);
        EffectOutcome::Failed(format!("unknown error type {:?}", report.error_type))
    }

    /// The meter the start and the stop both name.
    ///
    /// One site rather than one per arm: a start on the car side meter closed by
    /// a stop on the grid side one leaves a transaction open forever, and that
    /// disagreement is not expressible from here.
    fn billing(&self) -> Option<&generated::PowermeterClientPublisher> {
        billing_meter(
            &self.publishers.powermeter_car_side_slots,
            &self.publishers.powermeter_grid_side_slots,
        )
    }

    fn outcome(result: ::everestrs::Result<()>) -> EffectOutcome {
        match result {
            Ok(()) => EffectOutcome::Ok,
            Err(error) => EffectOutcome::Failed(error.to_string()),
        }
    }
}

/// Carry out one filesystem action the transcript asked for.
///
/// The whole of `core::session_log`'s I/O surface, in one place with four arms,
/// so there is no second site that could open a transcript by a different route.
/// Appends open, write and close rather than holding a handle: the C++ keeps two
/// `std::ofstream`s and flushes after every record (`SessionLog.cpp:233`,
/// `:241`), which is the same guarantee that a transcript survives a kill, and
/// a handle held here would have to be reconciled with a logger that decides
/// paths on the other side of a mutex.
fn perform_log_action(action: &session_log::Action) -> std::io::Result<()> {
    use std::io::Write;
    match action {
        session_log::Action::CreateDir { path } => std::fs::create_dir_all(path),
        session_log::Action::Truncate { path } => std::fs::File::create(path).map(|_| ()),
        session_log::Action::Append { path, text } => std::fs::OpenOptions::new()
            .append(true)
            .open(path)
            .and_then(|mut file| file.write_all(text.as_bytes())),
        session_log::Action::Rename { from, to } => std::fs::rename(from, to),
    }
}

impl boundary::supply::SupplySetpointApi for generated::PowerSupplyDcClientPublisher {
    fn set_import_voltage_current(&self, current_a: f64, voltage_v: f64) -> EffectOutcome {
        EverestEffects::outcome(self.set_import_voltage_current(current_a, voltage_v))
    }

    fn set_export_voltage_current(&self, current_a: f64, voltage_v: f64) -> EffectOutcome {
        EverestEffects::outcome(self.set_export_voltage_current(current_a, voltage_v))
    }
}

impl EffectRunner for EverestEffects {
    fn run(&self, effect: &Effect) -> EffectOutcome {
        let publishers = &self.publishers;
        match effect {
            Effect::AllowPowerOn(allow) => {
                use types::evse_board_support::PowerOnOffAutoGenReason as Reason;
                Self::outcome(publishers.bsp.allow_power_on(
                    types::evse_board_support::PowerOnOff {
                        allow_power_on: *allow,
                        reason: if *allow {
                            Reason::FullPowerCharging
                        } else {
                            Reason::PowerOff
                        },
                    },
                ))
            }

            Effect::SupplyOff => match publishers.powersupply_dc_slots.first() {
                Some(supply) => Self::outcome(supply.set_mode(
                    types::power_supply_DC::Mode::Off,
                    types::power_supply_DC::ChargingPhase::Other,
                )),
                None => EffectOutcome::Ok,
            },

            Effect::SetCpState(state) => Self::outcome(match state {
                CpState::X1 => publishers.bsp.cp_state_x_1(),
                CpState::F => publishers.bsp.cp_state_f(),
                CpState::E => publishers.bsp.cp_state_e(),
            }),

            Effect::UnlockConnector => match publishers.connector_lock_slots.first() {
                Some(lock) => Self::outcome(lock.unlock()),
                None => EffectOutcome::Ok,
            },

            Effect::BspEnable(on) => Self::outcome(publishers.bsp.enable(*on)),

            Effect::LockConnector => match publishers.connector_lock_slots.first() {
                Some(lock) => Self::outcome(lock.lock()),
                None => EffectOutcome::Ok,
            },

            // `evse_board_support` declares no `pwm_off`, so a zero percent duty
            // cycle is how PWM is turned off. That is also the C++ path: its
            // `set_pwm(0)` calls `call_pwm_on(0)` and turns `pwm_running` off.
            Effect::PwmOn(duty) => Self::outcome(publishers.bsp.pwm_on(pwm_percent(*duty))),
            Effect::PwmOff => Self::outcome(publishers.bsp.pwm_on(pwm_percent(0.0))),

            Effect::SetOvercurrentLimit(amps) => {
                Self::outcome(publishers.bsp.ac_set_overcurrent_limit_a(*amps))
            }

            Effect::SwitchThreePhases(three_phases) => Self::outcome(
                publishers
                    .bsp
                    .ac_switch_three_phases_while_charging(*three_phases),
            ),

            Effect::SetSupplyMode { mode, phase } => {
                match publishers.powersupply_dc_slots.first() {
                    Some(supply) => Self::outcome(supply.set_mode(
                        match mode {
                            SupplyMode::Off => types::power_supply_DC::Mode::Off,
                            SupplyMode::Export => types::power_supply_DC::Mode::Export,
                            SupplyMode::Import => types::power_supply_DC::Mode::Import,
                        },
                        match phase {
                            ChargingPhase::Other => types::power_supply_DC::ChargingPhase::Other,
                            ChargingPhase::CableCheck => {
                                types::power_supply_DC::ChargingPhase::CableCheck
                            }
                            ChargingPhase::PreCharge => {
                                types::power_supply_DC::ChargingPhase::PreCharge
                            }
                            ChargingPhase::Charging => {
                                types::power_supply_DC::ChargingPhase::Charging
                            }
                        },
                    )),
                    None => EffectOutcome::Ok,
                }
            }

            Effect::SetSupplySetpoint {
                mode,
                voltage_v,
                current_a,
            } => match publishers.powersupply_dc_slots.first() {
                Some(supply) => boundary::supply::write_supply_setpoint(
                    supply, *mode, *voltage_v, *current_a,
                ),
                None => EffectOutcome::Ok,
            },

            Effect::ImdStart => match publishers.imd_slots.first() {
                Some(imd) => Self::outcome(imd.start()),
                None => EffectOutcome::Ok,
            },

            Effect::ImdStop => match publishers.imd_slots.first() {
                Some(imd) => Self::outcome(imd.stop()),
                None => EffectOutcome::Ok,
            },

            Effect::ImdSelfTest { voltage_v, .. } => match publishers.imd_slots.first() {
                Some(imd) => Self::outcome(imd.start_self_test(*voltage_v)),
                None => EffectOutcome::Ok,
            },

            Effect::OverVoltageLimits(thresholds) => {
                match publishers.over_voltage_monitor_slots.first() {
                    Some(monitor) => Self::outcome(
                        monitor.set_limits(thresholds.emergency_v(), thresholds.error_v()),
                    ),
                    None => EffectOutcome::Ok,
                }
            }

            Effect::OverVoltageStart => match publishers.over_voltage_monitor_slots.first() {
                Some(monitor) => Self::outcome(monitor.start()),
                None => EffectOutcome::Ok,
            },

            Effect::OverVoltageStop => match publishers.over_voltage_monitor_slots.first() {
                Some(monitor) => Self::outcome(monitor.stop()),
                None => EffectOutcome::Ok,
            },

            // No billing meter connected is a wiring choice and not a failure,
            // the way an absent isolation monitor or over voltage monitor is.
            Effect::StartTransaction {
                transaction_id,
                id_token,
                tariff_text,
                ..
            } => {
                self.forget_signed_meter_values();
                match self.billing() {
                    Some(meter) => match meter.start_transaction(transaction_request(
                        &self.settings.evse_id,
                        transaction_id,
                        id_token.as_ref(),
                        tariff_text.as_deref(),
                    )) {
                        Ok(response) => {
                            if response.status == types::powermeter::TransactionRequestStatus::OK {
                                self.note_start_signature(response.signed_meter_value);
                            }
                            metering_status(&response.status, response.error.as_deref())
                        }
                        Err(error) => Self::outcome(Err(error)),
                    },
                    None => EffectOutcome::Ok,
                }
            }

            Effect::StopTransaction { transaction_id } => match self.billing() {
                Some(meter) => match meter.stop_transaction(transaction_id.clone()) {
                    Ok(response) => {
                        if response.status == types::powermeter::TransactionRequestStatus::OK {
                            self.note_stop_signature(
                                response.start_signed_meter_value,
                                response.signed_meter_value,
                            );
                        }
                        metering_status(&response.status, response.error.as_deref())
                    }
                    Err(error) => Self::outcome(Err(error)),
                },
                None => EffectOutcome::Ok,
            },

            // The empty transaction id is the interface's "cancel every ongoing
            // transaction" (`interfaces/powermeter.yaml`), which is what
            // `Charger::cleanup_transactions_on_startup` sends at
            // `Charger.cpp:1510`. It is written here, at the one site that may
            // mean it, rather than reachable by any effect that carries an
            // identity.
            Effect::CancelAllTransactions => match self.billing() {
                Some(meter) => match meter.stop_transaction(String::new()) {
                    Ok(response) => metering_status(&response.status, response.error.as_deref()),
                    Err(error) => Self::outcome(Err(error)),
                },
                None => EffectOutcome::Ok,
            },

            Effect::Persist { key, value } => match publishers.store_slots.first() {
                Some(store) => Self::outcome(store.store(key.clone(), json!(value)).map(|_| ())),
                None => EffectOutcome::Ok,
            },

            Effect::PersistDelete { key } => match publishers.store_slots.first() {
                Some(store) => Self::outcome(store.delete(key.clone())),
                None => EffectOutcome::Ok,
            },

            // The data link relays and the matching lifecycle. A deployment
            // with no SLAC wired cannot be running high level communication at
            // all, so an absent slot is not a failure, the same way an absent
            // isolation monitor is not.
            Effect::SlacUpdate(update) => match publishers.slac_slots.first() {
                Some(slac) => Self::outcome(match update {
                    SlacUpdate::EnterBcd => slac.enter_bcd(),
                    SlacUpdate::LeaveBcd => slac.leave_bcd(),
                    // The only `reset` the C++ sends; see `SlacUpdate::Reset`.
                    SlacUpdate::Reset => slac.reset(false),
                    SlacUpdate::DlinkError => slac.dlink_error(),
                    SlacUpdate::DlinkPause => slac.dlink_pause(),
                    SlacUpdate::DlinkTerminate => slac.dlink_terminate(),
                }),
                None => EffectOutcome::Ok,
            },

            // The wire field is a bare `String` where the core holds an
            // `Option`, because the C++ reads `charger->get_session_id()`, which
            // is empty between sessions. An empty uuid therefore means the same
            // thing on both sides rather than being a lost value.
            Effect::PublishHlcSessionFailed { uuid, reason } => {
                Self::outcome(publishers.evse.hlc_session_failed(
                    types::evse_manager::HlcSessionFailedEvent {
                        reason: hlc_session_failure_enum(*reason),
                        uuid: uuid.clone().unwrap_or_default(),
                    },
                ))
            }

            Effect::HlcUpdate(update) => match publishers.hlc_slots.first() {
                Some(hlc) => Self::outcome(match update {
                    // Named arguments. The two are adjacent enums in both
                    // orders, which is exactly the shape that transposes
                    // silently, so neither is written positionally.
                    HlcUpdate::AuthorizationResponse(response) => hlc.authorization_response(
                        authorization_status_enum(response.status),
                        certificate_status_enum(response.certificate),
                    ),
                    HlcUpdate::CableCheckFinished(ok) => hlc.cable_check_finished(*ok),
                    HlcUpdate::IsolationStatus(status) => {
                        hlc.update_isolation_status(isolation_status_enum(*status))
                    }
                    HlcUpdate::DlinkReady(ready) => hlc.dlink_ready(*ready),
                    HlcUpdate::SendError(error) => hlc.send_error(evse_error_enum(*error)),
                    HlcUpdate::ContactorClosed(closed) => hlc.ac_contactor_closed(*closed),
                    HlcUpdate::StopCharging(stop) => hlc.stop_charging(*stop),
                    HlcUpdate::PauseCharging(pause) => hlc.pause_charging(*pause),
                    HlcUpdate::ReceiptRequired(required) => hlc.receipt_is_required(*required),
                    // Named arguments. The generated signature is
                    // `(debug_mode, evse_id, sae_j_2847_mode)`, alphabetical,
                    // while the interface and the C++ call both read
                    // `(evse_id, sae_mode, debug_mode)`.
                    HlcUpdate::Setup {
                        evse_id,
                        evse_id_din,
                        sae_mode,
                        debug_mode,
                    } => hlc.setup(
                        *debug_mode,
                        types::iso15118::EVSEID {
                            evse_id: evse_id.clone(),
                            evse_id_din: Some(evse_id_din.clone()),
                        },
                        sae_bidi_mode_enum(*sae_mode),
                    ),
                    // Named arguments. The generated signature is
                    // `(central_contract_validation_allowed, fake_dc_enabled,
                    // payment_options, supported_certificate_service)`,
                    // alphabetical, while the interface and the C++ call both
                    // read `(payment_options, supported_certificate_service,
                    // central_contract_validation_allowed, fake_dc_enabled)`,
                    // so the booleans sit in a different order in each.
                    //
                    // `fake_dc_enabled` is the AC-with-SoC mode switch, which
                    // `EvseManager::init` seeds from `config.ac_with_soc` and
                    // then flips in `setup_fake_DC_mode` and `setup_AC_mode`.
                    // It rides on the message rather than being decided here,
                    // because the power path owns the mode: see
                    // `PowerPath::presents_fake_dc`.
                    HlcUpdate::SessionSetup(setup) => hlc.session_setup(
                        setup.central_contract_validation_allowed,
                        setup.fake_dc,
                        setup
                            .payment_options
                            .iter()
                            .copied()
                            .map(payment_option_enum)
                            .collect(),
                        setup.supported_certificate_service,
                    ),
                    HlcUpdate::BptSetup(setup) => hlc.bpt_setup(bpt_setup_payload(setup)),
                    HlcUpdate::ResetError => hlc.reset_error(),
                    // The report goes back out as the wire type it arrived as,
                    // filled by name for the same reason it was read by name.
                    HlcUpdate::PowerSupplyCapabilities(caps) => {
                        hlc.set_powersupply_capabilities(power_supply_capabilities_payload(caps))
                    }
                    HlcUpdate::ChargingParameters(values) => {
                        hlc.set_charging_parameters(types::iso15118::SetupPhysicalValues {
                            ac_nominal_voltage: values.ac_nominal_voltage_v,
                            dc_current_regulation_tolerance: values
                                .dc_current_regulation_tolerance_a,
                            dc_peak_current_ripple: values.dc_peak_current_ripple_a,
                            dc_energy_to_be_delivered: values.dc_energy_to_be_delivered_wh,
                        })
                    }
                    HlcUpdate::DcMinimumLimits(limits) => {
                        hlc.update_dc_minimum_limits(minimum_limits_payload(limits))
                    }
                    HlcUpdate::DcMaximumLimits(limits) => {
                        hlc.update_dc_maximum_limits(maximum_limits_payload(limits))
                    }
                    HlcUpdate::DcPresentValues {
                        voltage_v,
                        current_a,
                    } => {
                        hlc.update_dc_present_values(types::iso15118::DcEvsePresentVoltageCurrent {
                            evse_present_voltage: *voltage_v,
                            evse_present_current: Some(*current_a),
                        })
                    }
                    HlcUpdate::TransferModes(modes) => hlc.update_energy_transfer_modes(
                        modes.iter().copied().map(transfer_mode_enum).collect(),
                    ),
                    // The two envelope halves are the same shape, and the two
                    // commands take them as **different** wire types, so the
                    // payload is built once and the type is named at each call.
                    // Handing the maximum payload to the minimum command is a
                    // type error rather than a silent swap, which is a stronger
                    // pin than any test could be; verified by hand.
                    HlcUpdate::AcMaximumLimits(set) => {
                        let (charge_power, discharge_power) = ac_power_set_payload(set);
                        hlc.update_ac_maximum_limits(types::iso15118::AcEvseMaximumPower {
                            charge_power,
                            discharge_power,
                        })
                    }
                    HlcUpdate::AcMinimumLimits(set) => {
                        let (charge_power, discharge_power) = ac_power_set_payload(set);
                        hlc.update_ac_minimum_limits(types::iso15118::AcEvseMinimumPower {
                            charge_power,
                            discharge_power,
                        })
                    }
                    HlcUpdate::AcParameters(parameters) => {
                        hlc.update_ac_parameters(ac_parameters_payload(parameters))
                    }
                    HlcUpdate::AcMaxCurrent(amps) => hlc.update_ac_max_current(*amps),
                    HlcUpdate::AcTargetPower(power) => {
                        hlc.update_ac_target_values(ac_target_values_payload(power))
                    }
                    HlcUpdate::AcPresentPower(power) => {
                        hlc.update_ac_present_power(power_payload(power))
                    }
                    // The record itself, read from the same cache the three
                    // session event payloads read, which the arrival that
                    // produced this effect wrote before it. `meter_value`'s
                    // empty answer is unreachable through this variant: its one
                    // producer is that arrival.
                    HlcUpdate::MeterInfo => {
                        hlc.update_meter_info(self.meter_value())
                    }
                }),
                // `EvseManager.cpp:421-423` logs on exactly this: the error
                // send is the one update whose absent slot the C++ names out
                // loud, because a fault the vehicle is never told about looks
                // identical to one it was. The other updates reach an absent
                // slot in the ordinary course of a deployment without a stack
                // and stay silent.
                None => {
                    if matches!(update, HlcUpdate::SendError(_)) {
                        log::warn!("HLC module not connected, cannot send error!");
                    }
                    EffectOutcome::Ok
                }
            },

            // On this module's own `token_provider` interface, which is what
            // makes the `Auth` module validate it and answer with
            // `authorize_response`.
            Effect::PublishProvidedToken(token) => match provided_token_payload(token) {
                Some(payload) => Self::outcome(publishers.token_provider.provided_token(payload)),
                None => EffectOutcome::Ok,
            },

            Effect::PublishSupportedTransferModes(modes) => {
                Self::outcome(publishers.evse.supported_energy_transfer_modes(
                    modes.iter().copied().map(transfer_mode_enum).collect(),
                ))
            }

            Effect::SessionLog(request) => self.session_log(request),

            Effect::PublishCarManufacturer(manufacturer) => Self::outcome(
                publishers
                    .evse
                    .car_manufacturer(car_manufacturer_wire(*manufacturer)),
            ),

            Effect::PublishEvInfo(info) => {
                Self::outcome(publishers.evse.ev_info(ev_info_wire(info)))
            }

            Effect::PublishSelectedProtocol(protocol) => {
                Self::outcome(publishers.evse.selected_protocol(protocol.clone()))
            }

            Effect::PublishSessionEvent(report) => Self::outcome(publishers.evse.session_event(
                types::evse_manager::SessionEvent {
                    event: session_event_enum(report.event),
                    connector_id: Some(1),
                    timestamp: Self::timestamp(),
                    uuid: report.uuid.clone(),
                    // The three meter snapshots, each answering `None` unless
                    // the event is one of its own; see `authorization_event`.
                    authorization_event: self.authorization_event(report),
                    charging_paused_evse: self.charging_paused_evse(report),
                    charging_state_changed_event: self
                        .charging_state_changed_event(report),
                    session_finished: self.session_finished(report),
                    // Each of the three answers `None` unless `report.payload`
                    // holds its own arm, and the core builds at most one arm
                    // per event, so no event can carry two payloads or a
                    // payload the wrong event owns.
                    session_started: self.session_started(report),
                    source: None,
                    transaction_finished: self.transaction_finished(report),
                    transaction_started: self.transaction_started(report),
                },
            )),

            Effect::PublishEnableEvent { event, source } => Self::outcome(
                publishers
                    .evse
                    .session_event(types::evse_manager::SessionEvent {
                        event: session_event_enum(*event),
                        connector_id: Some(1),
                        timestamp: Self::timestamp(),
                        uuid: String::new(),
                        // None of the four belongs to an availability
                        // announcement: the C++ arm for `Enabled` and
                        // `Disabled` sets `connector_id` and `source` and
                        // nothing else (`evse/evse_managerImpl.cpp:343-350`).
                        authorization_event: None,
                        charging_paused_evse: None,
                        charging_state_changed_event: None,
                        session_finished: None,
                        session_started: None,
                        source: Some(enable_source_struct(*source)),
                        transaction_finished: None,
                        transaction_started: None,
                    }),
            ),

            Effect::PublishWaitingForExternalReady(waiting) => {
                Self::outcome(publishers.evse.waiting_for_external_ready(*waiting))
            }

            Effect::PublishLimits(limits) => {
                Self::outcome(publishers.evse.limits(types::evse_manager::Limits {
                    max_current: limits.max_current_a,
                    nr_of_phases_available: limits.nr_of_phases_available,
                    uuid: Some(self.node_id.clone()),
                }))
            }

            Effect::PublishEnforcedLimits(value) => {
                Self::outcome(publishers.evse.enforced_limits(enforced_limits(value)))
            }

            Effect::PublishEnergyFlowRequest(request) => {
                let usage = self
                    .energy_usage
                    .lock()
                    .unwrap_or_else(|poisoned| poisoned.into_inner());
                Self::outcome(
                    publishers
                        .energy_grid
                        .energy_flow_request(energy_flow_request(request, &usage)),
                )
            }

            Effect::PublishRandomDelayCountdown(countdown) => {
                Self::outcome(publishers.random_delay.countdown(count_down(countdown)))
            }

            Effect::PublishReady(ready) => Self::outcome(publishers.evse.ready(*ready)),

            Effect::RaiseError(report) => match raisable_error(&report.error_type) {
                Some(error_type) => {
                    publishers.evse.raise_error(raised_error(error_type, report));
                    EffectOutcome::Ok
                }
                None => Self::unraisable(report),
            },

            Effect::ClearError(report) => match raisable_error(&report.error_type) {
                // The generated clear passes `clear_all`, so every sub type of
                // this type goes with it. The core raises none, so there is
                // nothing else to lose.
                Some(error_type) => {
                    publishers.evse.clear_error(error_type);
                    EffectOutcome::Ok
                }
                None => Self::unraisable(report),
            },

            Effect::StartTimer { .. }
            | Effect::CancelTimer { .. }
            | Effect::AnswerCommand { .. } => {
                log::error!("a writer-handled effect reached the executor: {effect:?}");
                EffectOutcome::Failed("the writer handles this one".to_owned())
            }
        }
    }
}

/// Which optional requirements are actually connected. Config asks; this
/// answers. `config::resolve` needs both, because DC and AC-with-HLC are unsound
/// without their wiring in place.
///
/// A requirement with `min_connections: 0` and `max_connections: 1` is generated
/// as `<id>_slots: Vec<..>`, so emptiness is the connection test. `bsp` is 1..1
/// and is not a slot.
/// What the previous run left in the key value store, ported from
/// `PersistentStore::get_session` (`PersistentStore.cpp:30-42`).
///
/// Every way of not having a record answers `None`, and none of them is an
/// error: no `store` requirement connected (`active` is false there,
/// `PersistentStore.cpp:11-13`), a key that was never written, a value that is
/// not a string, and a load the store refused. The C++ collapses all four to
/// `{}` and both readers test only emptiness. That is the right call rather
/// than an oversight: recovery closes a transaction from a run that is already
/// over, so a store that cannot be read must not stop the charger from
/// starting.
fn recovered_session(publishers: &ModulePublisher, key: &str) -> Option<String> {
    let store = publishers.store_slots.first()?;
    match store.load(key.to_owned()) {
        Ok(serde_json::Value::String(session_uuid)) => Some(session_uuid),
        // `interfaces/kvs.yaml` says `load` returns null for a key that does
        // not exist, which is every ordinary boot.
        Ok(serde_json::Value::Null) => None,
        Ok(other) => {
            log::warn!("the record under {key} is {other}, not a session uuid; ignoring it");
            None
        }
        Err(error) => {
            log::warn!("could not read the record under {key}: {error}; starting without it");
            None
        }
    }
}

fn wiring_of(publishers: &ModulePublisher) -> Wiring {
    Wiring {
        hlc: !publishers.hlc_slots.is_empty(),
        slac: !publishers.slac_slots.is_empty(),
        powersupply_dc: !publishers.powersupply_dc_slots.is_empty(),
        imd: !publishers.imd_slots.is_empty(),
        over_voltage_monitor: !publishers.over_voltage_monitor_slots.is_empty(),
        powermeter_car_side: !publishers.powermeter_car_side_slots.is_empty(),
        connector_lock: !publishers.connector_lock_slots.is_empty(),
    }
}

/// The three-tier mapping this instance serves, which is framework metadata
/// rather than a config key.
// PENDING: everestrs exposes `get_module_configs` and `get_module_connections`
// but not the module mapping, so `mapping: module: {evse, connector}` cannot be
// read here. Until it can, the EVSE index comes from the legacy `connector_id`
// label, which every shipped config sets to the same value as its mapped EVSE,
// and the connector set is the single connector the module implements.
fn module_mapping(config: &ModuleConfig) -> Mapping {
    Mapping {
        evse: config.connector_id,
        connectors: vec![1],
    }
}

/// The wall clock stamp a schedule entry carries.
///
/// `core` reads no clock, so it names which reading it wants and this takes it.
/// `TopOfHour` truncates rather than rounds, which is what
/// `date::floor<hours>` does at `energy_grid/energyImpl.cpp:58`.
///
/// The C++ adds the elapsed leap seconds because it stamps from a
/// `utc_clock`; this does not, so the two differ by the current leap second
/// count. The field is a schedule position and the first entry is active
/// immediately whatever it says, so half a minute at the top of an hour changes
/// nothing a reader acts on.
fn entry_timestamp(when: EntryTime) -> String {
    use chrono::Timelike;

    let now = chrono::Utc::now();
    let at = match when {
        EntryTime::Now => now,
        EntryTime::TopOfHour => now
            .with_minute(0)
            .and_then(|at| at.with_second(0))
            .and_then(|at| at.with_nanosecond(0))
            .unwrap_or(now),
    };
    at.to_rfc3339_opts(chrono::SecondsFormat::Millis, true)
}

/// `types::uk_random_delay::CountDown`, whose `start_time` the core cannot
/// produce: `core` reads no wall clock, so it carries how long ago the delay
/// began and the instant is reconstructed here.
///
/// The C++ formats a stamp frozen at the start of the delay
/// (`energy_grid/energyImpl.cpp:461` into `:489`). This recomputes it per
/// publish from a monotonic interval, which differs only if the wall clock is
/// stepped mid delay, and then this is the answer that still points at the
/// real start.
fn count_down(countdown: &CountDown) -> types::uk_random_delay::CountDown {
    types::uk_random_delay::CountDown {
        countdown_s: countdown.countdown_s,
        current_limit_after_delay_a: countdown.current_limit_after_delay_a,
        current_limit_during_delay_a: countdown.current_limit_during_delay_a,
        start_time: countdown.started_ago.and_then(|ago| {
            chrono::Duration::from_std(ago)
                .ok()
                .and_then(|ago| chrono::Utc::now().checked_sub_signed(ago))
                .map(|at| at.to_rfc3339_opts(chrono::SecondsFormat::Millis, true))
        }),
    }
}

fn number_with_source(number: &NumberWithSource) -> types::energy::NumberWithSource {
    types::energy::NumberWithSource {
        value: number.value,
        source: number.source.clone(),
    }
}

fn integer_with_source(integer: &IntegerWithSource) -> types::energy::IntegerWithSource {
    types::energy::IntegerWithSource {
        value: integer.value,
        source: integer.source.clone(),
    }
}

fn number_with_source_from(number: types::energy::NumberWithSource) -> NumberWithSource {
    NumberWithSource {
        value: number.value,
        source: number.source,
    }
}

fn integer_with_source_from(integer: types::energy::IntegerWithSource) -> IntegerWithSource {
    IntegerWithSource {
        value: integer.value,
        source: integer.source,
    }
}

fn price_per_kwh_from(price: types::energy_price_information::PricePerkWh) -> PricePerKwh {
    PricePerKwh {
        timestamp: price.timestamp,
        value: price.value,
        currency: price.currency,
    }
}

fn price_per_kwh(price: &PricePerKwh) -> types::energy_price_information::PricePerkWh {
    types::energy_price_information::PricePerkWh {
        timestamp: price.timestamp.clone(),
        value: price.value,
        currency: price.currency.clone(),
    }
}

fn limits_res_from(limits: types::energy::LimitsRes) -> LimitsRes {
    LimitsRes {
        total_power_w: limits.total_power_w.map(number_with_source_from),
        ac_max_current_a: limits.ac_max_current_a.map(number_with_source_from),
        ac_max_phase_count: limits.ac_max_phase_count.map(integer_with_source_from),
    }
}

fn limits_res(limits: &LimitsRes) -> types::energy::LimitsRes {
    types::energy::LimitsRes {
        total_power_w: limits.total_power_w.as_ref().map(number_with_source),
        ac_max_current_a: limits.ac_max_current_a.as_ref().map(number_with_source),
        ac_max_phase_count: limits.ac_max_phase_count.as_ref().map(integer_with_source),
    }
}

fn enforced_limits(value: &EnforcedLimits) -> types::energy::EnforcedLimits {
    types::energy::EnforcedLimits {
        uuid: value.uuid.clone(),
        valid_for: value.valid_for_s,
        limits_root_side: limits_res(&value.limits_root_side),
        schedule: value
            .schedule
            .iter()
            .map(|entry| types::energy::ScheduleResEntry {
                timestamp: entry.timestamp.clone(),
                limits_to_root: limits_res(&entry.limits_to_root),
                price_per_kwh: entry.price_per_kwh.as_ref().map(price_per_kwh),
            })
            .collect(),
    }
}

/// Seven fields, four of which are the same two wire types in the same two
/// directions. Filled by name, and the maxima and minima are written adjacently
/// so a transposition reads as one.
fn limits_req(limits: &LimitsReq) -> types::energy::LimitsReq {
    types::energy::LimitsReq {
        total_power_w: limits.total_power_w.as_ref().map(number_with_source),
        ac_max_current_a: limits.ac_max_current_a.as_ref().map(number_with_source),
        ac_min_current_a: limits.ac_min_current_a.as_ref().map(number_with_source),
        ac_max_phase_count: limits.ac_max_phase_count.as_ref().map(integer_with_source),
        ac_min_phase_count: limits.ac_min_phase_count.as_ref().map(integer_with_source),
        ac_supports_changing_phases_during_charging: limits
            .ac_supports_changing_phases_during_charging,
        ac_number_of_active_phases: limits.ac_number_of_active_phases,
    }
}

fn schedule_entry(entry: &ScheduleReqEntry) -> types::energy::ScheduleReqEntry {
    types::energy::ScheduleReqEntry {
        timestamp: entry_timestamp(entry.timestamp),
        limits_to_root: limits_req(&entry.limits_to_root),
        limits_to_leaves: limits_req(&entry.limits_to_leaves),
        conversion_efficiency: entry.conversion_efficiency,
        // No site in `modules/EVSE/EvseManager/` fills a price, so neither does
        // this one.
        price_per_kwh: None,
    }
}

fn schedule(schedule: &Schedule) -> Vec<types::energy::ScheduleReqEntry> {
    schedule.entries().map(schedule_entry).collect()
}

fn node_type(node_type: NodeType) -> types::energy::NodeType {
    match node_type {
        NodeType::Undefined => types::energy::NodeType::Undefined,
        NodeType::Evse => types::energy::NodeType::Evse,
        NodeType::Generic => types::energy::NodeType::Generic,
    }
}

fn evse_state(state: EvseState) -> types::energy::EvseState {
    match state {
        EvseState::Unplugged => types::energy::EvseState::Unplugged,
        EvseState::WaitForAuth => types::energy::EvseState::WaitForAuth,
        EvseState::WaitForEnergy => types::energy::EvseState::WaitForEnergy,
        EvseState::PrepareCharging => types::energy::EvseState::PrepareCharging,
        EvseState::PausedEv => types::energy::EvseState::PausedEV,
        EvseState::PausedEvse => types::energy::EvseState::PausedEVSE,
        EvseState::Charging => types::energy::EvseState::Charging,
        EvseState::Finished => types::energy::EvseState::Finished,
        EvseState::Disabled => types::energy::EvseState::Disabled,
    }
}

/// What the core decided, as the wire says it.
///
/// The four fields filled with nothing are the ones this node does not answer
/// for: it is a leaf, so it has no children; it declares no optimizer target,
/// as the C++ declares none either; it carries neither meter record, and it
/// writes no setpoints. See `core::energy::flow_request::FlowRequest`.
fn energy_flow_request(
    request: &FlowRequest,
    usage: &EnergyUsage,
) -> types::energy::EnergyFlowRequest {
    types::energy::EnergyFlowRequest {
        uuid: request.uuid.as_str().to_owned(),
        node_type: node_type(request.node_type),
        priority_request: Some(request.priority_request),
        evse_state: Some(evse_state(request.evse_state)),
        schedule_import: schedule(&request.schedule_import),
        schedule_export: schedule(&request.schedule_export),
        children: Vec::new(),
        optimizer_target: None,
        // The two whole meter records, one per slot, which the C++ keeps on
        // its own request object and replaces from two subscriptions
        // (`energy_grid/energyImpl.cpp:42-60`). Absent until the slot has
        // reported, which is what a wire optional says and what the C++ sends
        // for a slot that is not wired at all.
        energy_usage_root: usage.root.clone(),
        energy_usage_leaves: usage.leaves.clone(),
        schedule_setpoints: request
            .schedule_setpoints
            .iter()
            .map(|entry| types::energy::ScheduleSetpointEntry {
                timestamp: entry_timestamp(entry.timestamp),
                setpoint: Some(types::energy::SetpointType {
                    priority: entry.priority,
                    source: entry.source.clone(),
                    ac_current_a: match entry.value {
                        SetpointValue::AcCurrent(value) => Some(value),
                        SetpointValue::TotalPower(_) => None,
                    },
                    total_power_w: match entry.value {
                        SetpointValue::TotalPower(value) => Some(value),
                        SetpointValue::AcCurrent(_) => None,
                    },
                    frequency_table: None,
                }),
            })
            .collect(),
    }
}

/// This module's configured id, which is what the energy tree knows it by.
///
/// `everestrs` parses the same argument to reach the runtime
/// (`everestrs/src/lib.rs:706-718`) and keeps it to itself, so it is read here
/// from the argument vector the framework already parsed. Reached only after
/// `Module::new` returned, so the argument is present by construction: a
/// missing one exits inside the runtime's own parse before this runs.
fn module_id(mut args: impl Iterator<Item = String>) -> anyhow::Result<String> {
    while let Some(argument) = args.next() {
        if argument == "--module" {
            return args
                .next()
                .ok_or_else(|| anyhow::anyhow!("--module was given no value"));
        }
        if let Some(id) = argument.strip_prefix("--module=") {
            return Ok(id.to_owned());
        }
    }
    Err(anyhow::anyhow!(
        "no --module argument, so this node has no identity in the energy tree"
    ))
}

fn fail(error: anyhow::Error) -> ! {
    log::error!("RsEvseManager refused to start: {error:#}");
    std::process::exit(1);
}

/// The framework's undeclared config keys in the shape the core's refusal reads:
/// the module's own group carries no implementation id, every other group is the
/// id of an interface this module provides.
fn supplied_keys(undeclared: &[everestrs::UndeclaredConfigKey]) -> Vec<SuppliedKey<'_>> {
    undeclared
        .iter()
        .map(|key| SuppliedKey {
            implementation: (key.group != everestrs::MODULE_CONFIG_GROUP)
                .then_some(key.group.as_str()),
            name: key.name.as_str(),
            // The framework discards an undeclared key's value before it
            // reports it (`everestrs::UndeclaredConfigKey`), so nothing here
            // can say what one asked for. All five workaround keys are
            // declared, so an undeclared spelling of one is a different key
            // and asks for nothing; the declared ones are read from the raw
            // config, where their values are.
            value: None,
        })
        .collect()
}

#[everestrs::main]
fn main(module: &Module) {
    // Intake exists before the framework can call anything, so an event arriving
    // during start queues rather than being lost. The queue is unbounded, so
    // there is nothing to overflow while the writer is not yet running.
    let (sender, queue) = boundary::event_channel();

    // A configuration error is a refusal to start, never a degraded start. The
    // config is readable before start because it only needs the runtime.
    let config = module.get_config();

    // The raw config only carries keys the manifest declares. An excluded key is
    // undeclared by construction, so this accessor is the only place it appears.
    // It reports the module's own config group and the config group of every
    // interface this module provides, so a key parked in a
    // `config_implementation` block is refused as well.
    let undeclared = module.get_undeclared_config_keys();
    if let Err(error) = reject_requested_workarounds(supplied_keys(&undeclared).into_iter()) {
        fail(error);
    }

    let settings = match Settings::from_raw(&module.get_raw_config(), &module_mapping(&config)) {
        Ok(settings) => Arc::new(settings),
        Err(error) => fail(error),
    };

    let replies = Arc::new(Replies::default());
    let billing_meter: BillingMeter = Arc::new(Mutex::new(None));
    let energy_usage: Arc<Mutex<EnergyUsage>> = Arc::new(Mutex::new(EnergyUsage::default()));
    let intake = Arc::new(Intake {
        events: sender.clone(),
        settings: Arc::clone(&settings),
        billing_meter: Arc::clone(&billing_meter),
        energy_usage: Arc::clone(&energy_usage),
        wiring: OnceLock::new(),
        hlc: OnceLock::new(),
        replies: Arc::clone(&replies),
    });

    // One subscriber per provided interface then one per required interface,
    // both in the order the generated wrapper declares, which is alphabetical by
    // implementation id rather than manifest order. A requirement that is not
    // exactly one-to-one takes a closure per slot instead of a subscriber.
    let publishers = module.start(
        intake.clone(),     // on_ready
        intake.clone(),     // dc_external_derate
        intake.clone(),     // energy_grid
        intake.clone(),     // evse
        intake.clone(),     // random_delay
        intake.clone(),     // token_provider
        |_| intake.clone(), // ac_rcd
        intake.clone(),     // bsp, the only one-to-one requirement
        |_| intake.clone(), // connector_lock
        |_| intake.clone(), // hlc
        |_| intake.clone(), // imd
        |_| intake.clone(), // over_voltage_monitor
        |_| intake.clone(), // powermeter_car_side
        |_| intake.clone(), // powermeter_grid_side
        |_| intake.clone(), // powersupply_DC
        |_| intake.clone(), // slac
        |_| intake.clone(), // store
    );

    let wiring = wiring_of(publishers);
    let _ = intake.wiring.set(wiring);

    // Resolved once, in one place, and shared: `config::resolve` chooses the
    // power path and the high level communication port together, so the
    // boundary's verdict on `update_allowed_energy_transfer_modes` and the
    // core's advertised set derivation cannot disagree about the connector or
    // about whether high level communication is on at all. The handle below is
    // the port's own, not a second resolution beside it.
    let deployment = match resolve(&settings, &wiring) {
        Ok(deployment) => deployment,
        Err(error) => fail(error),
    };
    let _ = intake.hlc.set(deployment.hlc_config());
    let Deployment { path, hlc } = deployment;

    // A missing random source is a refusal to start, not a session that mints a
    // guessable id.
    let session_id_bytes = match OsBytes::open() {
        Ok(source) => source,
        Err(error) => fail(error),
    };

    let node_id = match module_id(std::env::args()) {
        Ok(id) => id,
        Err(error) => fail(error),
    };
    log::info!(
        "RsEvseManager: charge mode {:?}, path {}, wiring {wiring:?}",
        settings.charge_mode,
        path.name()
    );
    // Read before the core exists, as `EvseManager::init` reads it
    // (`EvseManager.cpp:1478`) before the ready sequence that consumes it.
    // Events queue until `boundary::start` below, so nothing can reach the core
    // ahead of this and no `Event::Startup` can find the record unread.
    let persist = SessionStore::new(
        &node_id,
        recovered_session(publishers, &SessionStore::key_for(&node_id)),
    );

    let core = Core::new(
        path,
        settings.initial_session(),
        CoreParts {
            ready: ReadyGate {
                awaits_external_signal: settings.misc.external_ready_to_start_charging,
            },
            // `disable_authentication` reaches `Auth` as the identity it
            // authorizes with, which is what the C++ setting does at its one
            // read site (`evse/evse_managerImpl.cpp:149-158`).
            auth: if settings.payment.disable_authentication {
                Auth::new(settings.misc.raise_mrec9).free_charging()
            } else {
                Auth::new(settings.misc.raise_mrec9)
            },
            faults: Faults::new(settings.misc.inoperative_error_use_vendor_id),
            session_ids: SessionIds::new(settings.session_id_type, session_id_bytes),
            metering: Metering {
                fail_on_errors: settings.misc.fail_on_powermeter_errors,
            },
            // `initial_meter_value_timeout_ms`, negative clamped to zero,
            // because the setting's description says zero means "do not wait"
            // and a negative `wait_for` returns at once in the C++ too.
            initial_meter_timeout: std::time::Duration::from_millis(
                settings.timing.initial_meter_value_timeout_ms.max(0) as u64,
            ),
            hlc,
            // A node with no identity would have every enforced limit the
            // energy manager sends back silently discarded, so an id that
            // cannot be read is a refusal to start rather than a degraded one.
            energy: EnergyTree::new(
                NodeUuid::from_module_id(&node_id),
                EnergyConfig {
                    charge_mode: settings.charge_mode,
                    ac_nominal_voltage_v: settings.ac.nominal_voltage_v,
                    sae_v2h: settings.ac.sae_bpt_mode
                        == RsEvseManager::core::config::SaeBptMode::V2h,
                    request_zero_power_in_idle: settings.misc.request_zero_power_in_idle,
                },
                // `EvseManager.cpp:130-131` seeds the two runtime movable
                // pieces from configuration and `energyImpl.cpp:32` seeds the
                // generator. The seed carries the node id, which the C++
                // `time(0)` seed does not; see `boundary::random::seed`.
                RandomDelay::new(
                    RandomDelaySettings {
                        enabled_at_boot: settings.timing.uk_random_delay_enable,
                        max_duration_s_at_boot: settings.timing.uk_random_delay_max_duration_s,
                        at_any_change: settings.timing.uk_random_delay_at_any_change,
                    },
                    RsEvseManager::boundary::random::seed(&node_id),
                ),
            ),
            persist,
            // AC only, which is the whole of the C++ gate: the DC branch of the
            // charging state calls the enforce target route and never
            // `check_soft_over_current` (`Charger.cpp:839-852`).
            soft_oc: match settings.charge_mode {
                ChargeMode::Ac => Some(Detection::new(SoftOverCurrentConfig {
                    tolerance_percent: settings.ac.soft_over_current_tolerance_percent,
                    measurement_noise_a: settings.ac.soft_over_current_measurement_noise_a,
                    timeout: Duration::from_millis(
                        settings.ac.soft_over_current_timeout_ms.max(0) as u64
                    ),
                })),
                ChargeMode::Dc => None,
            },
        },
    );

    // `EvseManager.cpp:137-145`, at the same point in the lifecycle: the root
    // and the payload switch from configuration, before anything can ask for a
    // record. There is no enable step: `for_settings` reads the feature key and
    // answers `None` for a deployment that did not ask for the feature, so this
    // deployment either has a logger or has none.
    //
    // `logfile_suffix` is per session and is read by
    // `EverestEffects::session_log`.
    let session_log = SessionLogger::for_settings(&settings.logging);
    // Said once, at boot, and only where there is a logger to say it. The
    // defect this replaces was silence: a port with logging on and no session
    // yet writes nothing, which from the filesystem is indistinguishable from a
    // broken feature. Nothing is said when the feature is off, which is what
    // the C++ does.
    if let Some(logger) = session_log.as_ref() {
        log::info!("{}", logger.announcement());
    }

    let effects = Arc::new(EverestEffects {
        publishers: publishers.clone(),
        settings: Arc::clone(&settings),
        node_id: node_id.clone(),
        billing_meter,
        energy_usage,
        signed_meter_values: Mutex::new(SignedMeterValues::default()),
        session_log: Mutex::new(session_log),
    });
    let _loop_handle: LoopHandle = boundary::start(core, effects, sender, queue, replies);

    // The framework owns the process lifetime. Parking here keeps `loop_handle`
    // alive, and dropping it pushes `Event::Shutdown` and waits for safe state.
    //
    // PENDING: confirm how everestrs signals termination. `main` never returns
    // in the shipped Rust modules, so nothing drops `loop_handle` on SIGTERM and
    // safe state is not reached on kill. That needs a signal handler calling
    // `loop_handle.shutdown()`; it is the only correct exit path and it is not
    // wired here because the everestrs side of it is unconfirmed.
    loop {
        std::thread::park();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The two translations at the error boundary, in both directions.
    ///
    /// Nothing else in the module compares an `ErrorReport` field against the
    /// framework field it lands in, so a raise that filled `message` from
    /// `description` compiled and shipped. These assert the whole record at
    /// once: a per field check passes on a report whose fields all hold the
    /// same string, which is exactly the shape the defect had.
    mod error_boundary {
        use super::*;

        fn inoperative() -> ErrorReport {
            // Five distinct values, so no field can be filled from another and
            // still pass.
            ErrorReport {
                error_type: "evse_manager/Inoperative".into(),
                sub_type: "a sub type".into(),
                severity: Severity::High,
                vendor_id: "https://chargex.inl.gov".into(),
                description: "MREC2GroundFailure".into(),
                message: "evse_board_support/MREC2GroundFailure".into(),
            }
        }

        #[test]
        fn a_raise_carries_each_report_field_into_its_own_framework_field() {
            let report = inoperative();
            let raised = raised_error("witness", &report);

            assert_eq!(raised.error_type, "witness");
            assert_eq!(raised.sub_type, "a sub type");
            assert_eq!(raised.description, "MREC2GroundFailure");
            assert_eq!(
                raised.message, "evse_board_support/MREC2GroundFailure",
                "the message keeps the interface prefix the description strips"
            );
            assert_eq!(raised.vendor_id, "https://chargex.inl.gov");
            assert!(matches!(raised.severity, ::everestrs::ErrorSeverity::High));
        }

        #[test]
        fn a_raise_never_fills_one_field_from_another() {
            // The defect this closes: `message` was `description`. Stated as a
            // relation rather than as literals, so it also catches the mirror
            // mistakes (description from message, vendor id from either).
            let report = inoperative();
            let raised = raised_error((), &report);

            assert_ne!(raised.message, raised.description);
            assert_ne!(raised.vendor_id, raised.description);
            assert_ne!(raised.vendor_id, raised.message);
            assert_ne!(raised.sub_type, raised.description);
        }

        #[test]
        fn an_inbound_error_keeps_its_sub_type_and_vendor_id() {
            // Both used to be dropped at the boundary and replaced with an
            // empty string, which made every cause look unqualified and
            // vendorless to `core::faults`.
            let inbound = ::everestrs::ErrorType {
                error_type: "witness",
                sub_type: "some_subtype".to_owned(),
                description: "ignored here".to_owned(),
                message: "also ignored here".to_owned(),
                vendor_id: "https://chargex.inl.gov".to_owned(),
                severity: ::everestrs::ErrorSeverity::Medium,
            };

            let Event::Error(event) = error_event(ErrorSource::Bsp, inbound, true) else {
                panic!("an inbound error is an Event::Error");
            };
            assert_eq!(event.source, ErrorSource::Bsp);
            assert_eq!(event.sub_type, "some_subtype");
            assert_eq!(event.vendor_id, "https://chargex.inl.gov");
            assert_eq!(event.severity, Severity::Medium);
            assert!(event.raised);
        }

        #[test]
        fn a_clear_travels_the_same_route_as_its_raise() {
            // The flag is the only difference, so neither direction can grow a
            // field the other lacks.
            let build = |raised| {
                let inbound = ::everestrs::ErrorType {
                    error_type: "witness",
                    sub_type: "some_subtype".to_owned(),
                    description: String::new(),
                    message: String::new(),
                    vendor_id: "acme".to_owned(),
                    severity: ::everestrs::ErrorSeverity::Low,
                };
                let Event::Error(event) = error_event(ErrorSource::AcRcd, inbound, raised) else {
                    panic!("an inbound error is an Event::Error");
                };
                event
            };

            let raised = build(true);
            let cleared = build(false);
            assert!(raised.raised && !cleared.raised);
            assert_eq!(raised.sub_type, cleared.sub_type);
            assert_eq!(raised.vendor_id, cleared.vendor_id);
            assert_eq!(raised.error_type, cleared.error_type);
            assert_eq!(raised.severity, cleared.severity);
        }
    }

    /// Every value of `types::authorization::IdTokenType`, written out rather
    /// than derived, so the interface gaining a credential kind fails this list
    /// instead of reaching a signed metrology record as a default.
    const EVERY_WIRE_TOKEN_TYPE: &[types::authorization::IdTokenType] = {
        use types::authorization::IdTokenType as Wire;
        &[
            Wire::Central,
            Wire::eMAID,
            Wire::MacAddress,
            Wire::ISO14443,
            Wire::ISO15693,
            Wire::KeyCode,
            Wire::Local,
            Wire::NoAuthorization,
        ]
    };

    /// A `ProvidedIdToken` as an `Auth` module sends one.
    fn provided_token(
        value: &str,
        token_type: types::authorization::IdTokenType,
        plug_and_charge: bool,
    ) -> types::authorization::ProvidedIdToken {
        types::authorization::ProvidedIdToken {
            id_token: types::authorization::IdToken {
                value: value.to_owned(),
                r#type: token_type,
                additional_info: None,
            },
            authorization_type: if plug_and_charge {
                types::authorization::AuthorizationType::PlugAndCharge
            } else {
                types::authorization::AuthorizationType::RFID
            },
            parent_id_token: None,
            connectors: None,
            prevalidated: None,
            request_id: None,
            certificate: None,
            iso_15118_certificate_hash_data: None,
        }
    }

    /// The identity as the core receives it, built through the production
    /// constructor rather than beside it: `id_tag_from` is the only thing that
    /// makes an `IdTag` in this module, so a test that bypassed it would not be
    /// testing what the meter is billed from.
    fn wire_id_tag(
        value: &str,
        token_type: types::authorization::IdTokenType,
        plug_and_charge: bool,
    ) -> IdTag {
        id_tag_from(&provided_token(value, token_type, plug_and_charge))
            .expect("a provided token serializes")
    }

    /// Every value of `types::iso15118::EnergyTransferMode`, written out rather
    /// than derived, so adding one to the interface fails this list rather than
    /// passing silently.
    const EVERY_WIRE_TRANSFER_MODE: &[types::iso15118::EnergyTransferMode] = {
        use types::iso15118::EnergyTransferMode as Wire;
        &[
            Wire::AC_single_phase_core,
            Wire::AC_two_phase,
            Wire::AC_three_phase_core,
            Wire::DC_core,
            Wire::DC_extended,
            Wire::DC_combo_core,
            Wire::DC_unique,
            Wire::DC,
            Wire::AC_BPT,
            Wire::AC_BPT_DER,
            Wire::AC_DER_IEC,
            Wire::AC_DER_SAE,
            Wire::DC_BPT,
            Wire::DC_ACDP,
            Wire::DC_ACDP_BPT,
            Wire::WPT,
            Wire::MCS,
            Wire::MCS_BPT,
        ]
    };

    /// The energy tree an `evse_manager` node would hold.
    fn energy_tree(charge_mode: RsEvseManager::core::config::ChargeMode) -> EnergyTree {
        EnergyTree::new(
            NodeUuid::from_module_id("evse_manager"),
            EnergyConfig {
                charge_mode,
                ac_nominal_voltage_v: 230.0,
                sae_v2h: false,
                request_zero_power_in_idle: true,
            },
            // The manifest defaults, which have the delay off, so a limit
            // reaching this tree is enforced as it arrived.
            RandomDelay::new(
                RandomDelaySettings {
                    enabled_at_boot: false,
                    max_duration_s_at_boot: 600,
                    at_any_change: true,
                },
                0,
            ),
        )
    }

    /// The C++ republishes the struct it was handed, mutating only
    /// `ac_max_current_A` (`energyImpl.cpp:504-515`), so this port's trip
    /// through `core` has to be lossless. The price information is the field
    /// with no reader in `core` at all, which is exactly why it is the one a
    /// conversion drops without any gate noticing.
    /// Two `f64` fields side by side in `types::uk_random_delay::CountDown`
    /// that mean opposite things: the limit after the delay is the one the
    /// energy manager asked for and the limit during it is the one being
    /// applied. Swapping them would tell an operator the port is doing the
    /// reverse of what it is doing, and nothing else in the module would
    /// notice, so the mapping is pinned with two values that cannot be
    /// confused.
    #[test]
    fn the_countdown_names_the_after_and_during_limits_the_right_way_round() {
        let wire = count_down(&CountDown {
            countdown_s: 41,
            current_limit_after_delay_a: 6.0,
            current_limit_during_delay_a: 32.0,
            started_ago: None,
        });
        assert_eq!(wire.countdown_s, 41);
        assert_eq!(wire.current_limit_after_delay_a, 6.0);
        assert_eq!(wire.current_limit_during_delay_a, 32.0);
    }

    /// `core` reads no wall clock, so the start of a running delay travels as
    /// an interval and the instant is reconstructed here. Absent where the C++
    /// leaves the optional unset, which is both zero countdown branches.
    #[test]
    fn a_running_countdown_carries_a_start_time_and_an_idle_one_does_not() {
        let idle = count_down(&CountDown {
            countdown_s: 0,
            current_limit_after_delay_a: 16.0,
            current_limit_during_delay_a: 0.0,
            started_ago: None,
        });
        assert_eq!(idle.start_time, None);

        let before = chrono::Utc::now();
        let running = count_down(&CountDown {
            countdown_s: 300,
            current_limit_after_delay_a: 16.0,
            current_limit_during_delay_a: 0.0,
            started_ago: Some(std::time::Duration::from_secs(300)),
        });
        let started = chrono::DateTime::parse_from_rfc3339(
            &running
                .start_time
                .expect("a running delay carries its start"),
        )
        .expect("not RFC3339");
        let ago = before.signed_duration_since(started.with_timezone(&chrono::Utc));
        assert!(
            ago.num_seconds() >= 299 && ago.num_seconds() <= 301,
            "the delay started {ago} ago, not five minutes"
        );
    }

    #[test]
    fn the_enforced_limits_survive_the_trip_through_the_core_intact() {
        let wire = types::energy::EnforcedLimits {
            uuid: "evse_manager".to_owned(),
            valid_for: 42,
            limits_root_side: types::energy::LimitsRes {
                total_power_w: Some(types::energy::NumberWithSource {
                    value: 11_000.0,
                    source: "optimizer/power".to_owned(),
                }),
                ac_max_current_a: Some(types::energy::NumberWithSource {
                    value: 16.0,
                    source: "optimizer/current".to_owned(),
                }),
                ac_max_phase_count: Some(types::energy::IntegerWithSource {
                    value: 3,
                    source: "optimizer/phases".to_owned(),
                }),
            },
            schedule: vec![types::energy::ScheduleResEntry {
                timestamp: "2026-09-03T10:00:00.000Z".to_owned(),
                limits_to_root: types::energy::LimitsRes {
                    total_power_w: Some(types::energy::NumberWithSource {
                        value: 7_400.0,
                        source: "optimizer/later".to_owned(),
                    }),
                    ac_max_current_a: None,
                    ac_max_phase_count: None,
                },
                price_per_kwh: Some(types::energy_price_information::PricePerkWh {
                    timestamp: "2026-09-03T10:00:00.000Z".to_owned(),
                    value: 0.31,
                    currency: "EUR".to_owned(),
                }),
            }],
        };

        let carried = EnforcedLimits {
            uuid: wire.uuid.clone(),
            valid_for_s: wire.valid_for,
            schedule: wire
                .schedule
                .iter()
                .cloned()
                .map(|entry| ScheduleResEntry {
                    timestamp: entry.timestamp,
                    limits_to_root: limits_res_from(entry.limits_to_root),
                    price_per_kwh: entry.price_per_kwh.map(price_per_kwh_from),
                })
                .collect(),
            limits_root_side: limits_res_from(wire.limits_root_side.clone()),
        };

        let republished = enforced_limits(&carried);

        assert_eq!(republished.uuid, wire.uuid);
        assert_eq!(republished.valid_for, wire.valid_for);
        assert_eq!(
            republished.limits_root_side.total_power_w.map(|n| n.source),
            Some("optimizer/power".to_owned())
        );
        assert_eq!(
            republished
                .limits_root_side
                .ac_max_phase_count
                .map(|n| n.value),
            Some(3)
        );
        assert_eq!(republished.schedule.len(), 1);
        let entry = &republished.schedule[0];
        assert_eq!(entry.timestamp, "2026-09-03T10:00:00.000Z");
        assert_eq!(
            entry.limits_to_root.total_power_w.as_ref().map(|n| n.value),
            Some(7_400.0)
        );
        let price = entry
            .price_per_kwh
            .as_ref()
            .expect("the price information has to reach the republish");
        assert_eq!(price.value, 0.31);
        assert_eq!(price.currency, "EUR");
        assert_eq!(price.timestamp, "2026-09-03T10:00:00.000Z");
    }

    #[test]
    fn the_node_identity_is_read_from_the_argument_the_framework_parsed() {
        // Both spellings clap accepts, because the framework parses the same
        // vector with clap and a config that used the other one would leave
        // this node without an identity.
        assert_eq!(
            module_id(
                ["RsEvseManager", "--module", "evse_manager_1"]
                    .map(String::from)
                    .into_iter()
            )
            .unwrap(),
            "evse_manager_1"
        );
        assert_eq!(
            module_id(
                ["RsEvseManager", "--module=evse_manager_2"]
                    .map(String::from)
                    .into_iter()
            )
            .unwrap(),
            "evse_manager_2"
        );
    }

    #[test]
    fn a_node_with_no_identity_refuses_rather_than_inventing_one() {
        // Two nodes sharing an invented identity would each answer the other's
        // enforced limits, so there is no fallback worth having.
        assert!(module_id(["RsEvseManager"].map(String::from).into_iter()).is_err());
        assert!(module_id(["RsEvseManager", "--module"].map(String::from).into_iter()).is_err());
    }

    fn undeclared(group: &str, name: &str) -> everestrs::UndeclaredConfigKey {
        everestrs::UndeclaredConfigKey {
            group: group.to_owned(),
            name: name.to_owned(),
        }
    }

    #[test]
    fn a_key_of_the_modules_own_group_names_no_implementation() {
        let reported = [undeclared(everestrs::MODULE_CONFIG_GROUP, "hack_simplified_mode_limit_10A")];

        assert_eq!(
            supplied_keys(&reported),
            vec![SuppliedKey {
                implementation: None,
                name: "hack_simplified_mode_limit_10A",
                value: None,
            }]
        );
    }

    #[test]
    fn a_key_of_a_provided_interfaces_group_keeps_that_implementation_id() {
        // The five ids here are this module's `provides` block. Losing the id
        // would report an interface's key as one of the module's own.
        for implementation in [
            "evse",
            "energy_grid",
            "token_provider",
            "random_delay",
            "dc_external_derate",
        ] {
            let reported = [undeclared(implementation, "hack_simplified_mode_limit_10A")];

            assert_eq!(
                supplied_keys(&reported),
                vec![SuppliedKey {
                    implementation: Some(implementation),
                    name: "hack_simplified_mode_limit_10A",
                    value: None,
                }]
            );
        }
    }

    #[test]
    fn every_reported_group_survives_one_pass_in_the_order_it_arrived() {
        // The framework reports a sorted map of sorted sets, so the module's own
        // group comes first and the implementations follow. The mapping must
        // preserve that order and lose no entry, because the refusal message is
        // built from it.
        let reported = [
            undeclared(everestrs::MODULE_CONFIG_GROUP, "hack_simplified_mode_limit_10A"),
            undeclared("dc_external_derate", "hack_skoda_enyaq"),
            undeclared("evse", "hack_present_current_offset"),
            undeclared("evse", "some_other_key"),
        ];

        assert_eq!(
            supplied_keys(&reported),
            vec![
                SuppliedKey {
                    implementation: None,
                    name: "hack_simplified_mode_limit_10A",
                    value: None,
                },
                SuppliedKey {
                    implementation: Some("dc_external_derate"),
                    name: "hack_skoda_enyaq",
                    value: None,
                },
                SuppliedKey {
                    implementation: Some("evse"),
                    name: "hack_present_current_offset",
                    value: None,
                },
                SuppliedKey {
                    implementation: Some("evse"),
                    name: "some_other_key",
                    value: None,
                },
            ]
        );

        // And none of them refuses startup, because the framework discarded
        // every value before reporting it. The five are declared in the
        // manifest now, so a key of that name arriving **undeclared** is a copy
        // parked in a group that does not declare it: the framework logs it and
        // the C++ module ignores it, because its own manifest declares these
        // keys in the module group alone. What a request looks like is a value,
        // and the values are in the raw config.
        assert!(
            reject_requested_workarounds(supplied_keys(&reported).into_iter()).is_ok(),
            "a name with no value asks for nothing"
        );
    }

    /// The refusal reads values, and an undeclared report has none.
    ///
    /// This asserted the opposite, that either group's report refused startup
    /// on the name alone. That was stricter than the C++, whose manifest also
    /// declares these keys in the module group alone and which starts on a
    /// misplaced copy; and it is what stopped a deployment that sets one of
    /// them to its inert default from substituting this module, because the
    /// framework strips an undeclared key's value before the module sees it.
    ///
    /// The refusal that remains is in `Settings::from_raw`, over the raw
    /// config, where the declared keys arrive with their values.
    #[test]
    fn an_undeclared_report_of_an_excluded_key_does_not_refuse_startup() {
        for group in [everestrs::MODULE_CONFIG_GROUP, "evse"] {
            let reported = [undeclared(group, "hack_skoda_enyaq")];

            assert!(
                reject_requested_workarounds(supplied_keys(&reported).into_iter()).is_ok(),
                "{group} refused a name with no value"
            );
        }

        // What does refuse: the declared key, with a value that asks for the
        // workaround.
        let asked = serde_json::json!(true);
        let error = reject_requested_workarounds(
            [SuppliedKey {
                implementation: None,
                name: "hack_skoda_enyaq",
                value: Some(&asked),
            }]
            .into_iter(),
        )
        .expect_err("the workaround was asked for");
        assert!(error.to_string().contains("hack_skoda_enyaq"), "{error}");
    }

    #[test]
    fn a_key_outside_the_five_does_not_refuse_startup_from_any_group() {
        // Nothing reported at all, and a supported key the manifest happens not
        // to declare under that group. Neither is the refusal's business.
        assert!(reject_requested_workarounds(supplied_keys(&[]).into_iter()).is_ok());
        assert!(reject_requested_workarounds(
            supplied_keys(&[undeclared("evse", "hack_allow_bpt_with_iso2")]).into_iter()
        )
        .is_ok());
    }

    #[test]
    fn an_energy_flow_request_reaches_the_wire_as_the_core_decided_it() {
        use RsEvseManager::core::config::ChargeMode;
        use RsEvseManager::core::energy::Publish;
        use RsEvseManager::core::path::iec::AcState;

        let mut tree = energy_tree(ChargeMode::Ac);
        tree.note_capabilities(RsEvseManager::core::event::HardwareCapabilities {
            max_current_a_import: 32.0,
            min_current_a_import: 6.0,
            max_phase_count_import: 3,
            min_phase_count_import: 1,
            max_current_a_export: 16.0,
            min_current_a_export: 4.0,
            max_phase_count_export: 3,
            min_phase_count_export: 1,
            supports_changing_phases_during_charging: false,
            supports_cp_state_e: false,
        });

        let wire = energy_flow_request(
            &tree.flow_request(Publish {
                charger_state: AcState::Charging,
                bidirectional: false,
                priority: true,
            }),
            &EnergyUsage::default(),
        );

        assert_eq!(wire.uuid, "evse_manager");
        assert_eq!(wire.node_type, types::energy::NodeType::Evse);
        assert_eq!(wire.priority_request, Some(true));
        assert_eq!(wire.evse_state, Some(types::energy::EvseState::Charging));
        assert_eq!(wire.schedule_import.len(), 1);
        assert_eq!(wire.schedule_export.len(), 1);
        // A leaf with no meter record and no setpoints of its own.
        assert!(wire.children.is_empty());
        assert!(wire.schedule_setpoints.is_empty());
        assert!(wire.energy_usage_root.is_none());
        assert!(wire.energy_usage_leaves.is_none());

        let import = &wire.schedule_import[0];
        assert_eq!(
            import.limits_to_root.ac_max_current_a,
            Some(types::energy::NumberWithSource {
                value: 32.0,
                source: "evse_manager/evse_board_support_caps".to_owned(),
            })
        );
        assert_eq!(
            import
                .limits_to_root
                .ac_min_current_a
                .as_ref()
                .map(|limit| limit.value),
            Some(6.0)
        );
        assert_eq!(
            import.limits_to_root.ac_max_phase_count,
            Some(types::energy::IntegerWithSource {
                value: 3,
                source: "evse_manager/evse_board_support_caps".to_owned(),
            })
        );
        assert_eq!(import.limits_to_root.ac_number_of_active_phases, Some(3));
        assert_eq!(
            import
                .limits_to_root
                .ac_supports_changing_phases_during_charging,
            Some(false)
        );
        assert_eq!(
            import
                .limits_to_leaves
                .ac_max_current_a
                .as_ref()
                .map(|limit| limit.value),
            Some(32.0)
        );
        // The export side is the export side: its root ceiling is the export
        // pair of the board report, not the import pair.
        let export = &wire.schedule_export[0];
        assert_eq!(
            export
                .limits_to_root
                .ac_max_current_a
                .as_ref()
                .map(|limit| limit.value),
            Some(16.0)
        );
        assert_eq!(
            export
                .limits_to_root
                .ac_min_current_a
                .as_ref()
                .map(|limit| limit.value),
            Some(4.0)
        );
    }

    #[test]
    fn a_dc_request_carries_watts_and_the_efficiency_of_its_direction() {
        use RsEvseManager::core::config::ChargeMode;
        use RsEvseManager::core::energy::Publish;
        use RsEvseManager::core::event::PowerSupplyCapabilities;
        use RsEvseManager::core::path::iec::AcState;

        let mut tree = energy_tree(ChargeMode::Dc);
        let mut caps = PowerSupplyCapabilities::sane_default();
        caps.max_export_power_w = 30_000.0;
        caps.conversion_efficiency_export = Some(0.95);
        tree.note_supply_capabilities(caps);

        let wire = energy_flow_request(
            &tree.flow_request(Publish {
                charger_state: AcState::Charging,
                bidirectional: false,
                priority: false,
            }),
            &EnergyUsage::default(),
        );

        assert_eq!(
            wire.schedule_import[0]
                .limits_to_leaves
                .total_power_w
                .as_ref()
                .map(|power| power.value),
            Some(30_000.0)
        );
        // Idle carries the efficiency and the active branch does not, because
        // the schedule the active branch sends is the locally derived one; see
        // `core::energy`.
        let idle = energy_flow_request(
            &tree.flow_request(Publish {
                charger_state: AcState::Idle,
                bidirectional: false,
                priority: false,
            }),
            &EnergyUsage::default(),
        );
        assert_eq!(idle.schedule_import[0].conversion_efficiency, Some(0.95));
    }

    #[test]
    fn every_simplified_evse_state_has_its_own_wire_value() {
        // A collapse here would tell the energy manager the port is doing
        // something it is not, and the two enums are spelled differently
        // enough that one could pass unnoticed.
        let mut seen = Vec::new();
        for state in [
            EvseState::Unplugged,
            EvseState::WaitForAuth,
            EvseState::WaitForEnergy,
            EvseState::PrepareCharging,
            EvseState::PausedEv,
            EvseState::PausedEvse,
            EvseState::Charging,
            EvseState::Finished,
            EvseState::Disabled,
        ] {
            let wire = evse_state(state);
            assert!(!seen.contains(&wire), "{state:?} is reached twice");
            seen.push(wire);
        }
        assert_eq!(seen.len(), 9);
    }

    #[test]
    fn a_top_of_hour_stamp_carries_no_minutes_and_no_seconds() {
        // The C++ floors to the hour (`energyImpl.cpp:58`), which is what makes
        // the first entry of a cleared schedule sit in the recent past.
        let stamp = entry_timestamp(EntryTime::TopOfHour);
        let at = chrono::DateTime::parse_from_rfc3339(&stamp).expect("not RFC3339");

        use chrono::Timelike;
        assert_eq!(at.minute(), 0, "{stamp}");
        assert_eq!(at.second(), 0, "{stamp}");
        assert_eq!(at.nanosecond(), 0, "{stamp}");
    }

    #[test]
    fn a_now_stamp_is_a_timestamp_the_wire_can_read() {
        let stamp = entry_timestamp(EntryTime::Now);
        assert!(
            chrono::DateTime::parse_from_rfc3339(&stamp).is_ok(),
            "{stamp}"
        );
    }

    #[test]
    fn every_wire_energy_transfer_mode_round_trips() {
        // `update_allowed_energy_transfer_modes` relays a list another module
        // chose, so a value that does not survive the trip in and back out
        // would be silently replaced in what the vehicle is offered.
        for wire in EVERY_WIRE_TRANSFER_MODE {
            assert_eq!(
                &transfer_mode_enum(transfer_mode_from(wire)),
                wire,
                "{wire:?} does not round trip"
            );
        }
    }

    #[test]
    fn no_two_wire_transfer_modes_share_a_core_spelling() {
        // The round trip alone would still pass if two wire values both mapped
        // onto one core value and that value mapped back onto one of them.
        let mut seen = Vec::new();
        for wire in EVERY_WIRE_TRANSFER_MODE {
            let mode = transfer_mode_from(wire);
            assert!(!seen.contains(&mode), "{mode:?} is reached twice");
            seen.push(mode);
        }
        assert_eq!(seen.len(), EVERY_WIRE_TRANSFER_MODE.len());
    }

    /// Every value of `types::evse_manager::StopTransactionReason`, written out
    /// rather than derived, so a value added to the interface fails this list.
    const EVERY_WIRE_STOP_REASON: &[types::evse_manager::StopTransactionReason] = {
        use types::evse_manager::StopTransactionReason as Wire;
        &[
            Wire::EmergencyStop,
            Wire::EVDisconnected,
            Wire::HardReset,
            Wire::Local,
            Wire::Other,
            Wire::PowerLoss,
            Wire::Reboot,
            Wire::Remote,
            Wire::SoftReset,
            Wire::UnlockCommand,
            Wire::DeAuthorized,
            Wire::EnergyLimitReached,
            Wire::GroundFault,
            Wire::LocalOutOfCredit,
            Wire::MasterPass,
            Wire::OvercurrentFault,
            Wire::PowerQuality,
            Wire::SOCLimitReached,
            Wire::StoppedByEV,
            Wire::TimeLimitReached,
            Wire::Timeout,
            Wire::ReqEnergyTransferRejected,
            Wire::EVSEDisabled,
        ]
    };

    /// The stop reason a consumer reads off `TransactionFinished` is the one the
    /// request named, for all twenty three of them.
    ///
    /// This replaces a two case test of the narrowing that used to live here.
    /// The availability report's three states, each carried as itself.
    ///
    /// The third one is the one that matters and it is the one a two-command
    /// intake could not carry: `Unassigned` is a source saying it no longer
    /// cares, and the arbitration skips such a row. Read as an `Enable` it was
    /// a vote at the source's own priority, so a high authority source letting
    /// go forced the port enabled over a disable that should have stood, which
    /// `a_source_that_withdraws_stops_deciding_rather_than_voting_to_enable`
    /// in `core` drives.
    ///
    /// A round trip, because the two directions are the same table written
    /// twice and the module publishes the winning source back out.
    #[test]
    fn every_availability_state_survives_the_round_trip_to_the_core_and_back() {
        use types::evse_manager::EnableDisableSourceAutoGenEnableState as Wire;

        let mut carried = Vec::new();
        for wire in [Wire::Unassigned, Wire::Disable, Wire::Enable] {
            let state = enable_state_from(&wire);
            assert_eq!(
                enable_state_wire(state),
                wire,
                "{wire:?} does not survive the round trip"
            );
            assert!(
                !carried.contains(&state),
                "{wire:?} collides with an earlier state"
            );
            carried.push(state);
        }
        assert_eq!(carried.len(), 3);
    }

    /// The narrowing is many to one, so it is not invertible and the payload
    /// cannot be built from it; what has to hold instead is that the carry is a
    /// bijection, which is what a round trip over the whole enum says.
    #[test]
    fn every_stop_reason_survives_the_round_trip_to_the_core_and_back() {
        for wire in EVERY_WIRE_STOP_REASON {
            let carried = stop_transaction_reason_from(wire.clone());
            assert_eq!(
                &stop_transaction_reason_wire(carried),
                wire,
                "{wire:?} does not come back as itself, via {carried:?}"
            );
        }
    }

    #[test]
    fn no_two_wire_stop_reasons_share_a_core_spelling() {
        // The round trip alone would still pass if two wire values both mapped
        // onto one core value and that value mapped back onto one of them.
        let mut seen = Vec::new();
        for wire in EVERY_WIRE_STOP_REASON {
            let carried = stop_transaction_reason_from(wire.clone());
            assert!(!seen.contains(&carried), "{carried:?} is reached twice");
            seen.push(carried);
        }
        assert_eq!(seen.len(), EVERY_WIRE_STOP_REASON.len());
    }

    /// The narrowing the power paths read, which the carry above replaced at
    /// this boundary and the core now derives.
    #[test]
    fn a_disable_driven_stop_keeps_its_own_reason_off_the_wire() {
        // The wire reason used to collapse onto Remote for want of a core
        // variant, which lost the distinction an OCPP backend reports.
        use types::evse_manager::StopTransactionReason as Wire;
        assert_eq!(
            stop_transaction_reason_from(Wire::EVSEDisabled).narrow(),
            StopReason::EvseDisabled
        );
        assert_eq!(
            stop_transaction_reason_from(Wire::Remote).narrow(),
            StopReason::Remote
        );
    }

    #[test]
    fn a_duty_cycle_fraction_becomes_a_percentage_on_the_wire() {
        // `pwm_on` is declared in percent with a maximum of 100, while the core
        // carries a 0..1 fraction, so the boundary is the only place the factor
        // may be applied.
        assert_eq!(pwm_percent(0.05), 5.0);
        assert_eq!(pwm_percent(0.97), 97.0);
    }

    #[test]
    fn turning_pwm_off_is_a_zero_percent_duty_cycle() {
        // `evse_board_support` declares `pwm_on` and no `pwm_off`.
        assert_eq!(pwm_percent(0.0), 0.0);
    }

    #[test]
    fn an_error_enum_serializes_back_to_its_wire_string() {
        use generated::errors::evse_board_support as bsp_errors;
        let error =
            bsp_errors::Error::EvseBoardSupport(bsp_errors::EvseBoardSupportError::DiodeFault);
        assert_eq!(error_type_string(&error), "evse_board_support/DiodeFault");
    }

    #[test]
    fn every_enable_source_round_trips_through_the_wire() {
        use types::evse_manager::EnableDisableSourceAutoGenEnableSource as Wire;
        for source in [
            EnableSource::Unspecified,
            EnableSource::LocalApi,
            EnableSource::LocalKeyLock,
            EnableSource::ServiceTechnician,
            EnableSource::RemoteKeyLock,
            EnableSource::MobileApp,
            EnableSource::FirmwareUpdate,
            EnableSource::Csms,
        ] {
            let wire: Wire = enable_source_wire(source);
            assert_eq!(enable_source_from(&wire), source);
        }
    }

    #[test]
    fn the_availability_announcement_maps_onto_its_own_wire_pair() {
        use types::evse_manager::SessionEventEnum as Wire;
        assert_eq!(session_event_enum(SessionEvent::Enabled), Wire::Enabled);
        assert_eq!(session_event_enum(SessionEvent::Disabled), Wire::Disabled);
    }

    /// The recovery announcement has to reach the wire as the variant the C++
    /// publishes (`evse/evse_managerImpl.cpp:382`). A core that recovers a
    /// transaction and a boundary that cannot name the event would leave the
    /// resume unannounced with every gate green.
    #[test]
    fn the_startup_resume_maps_onto_the_wire_variant_the_cpp_publishes() {
        use types::evse_manager::SessionEventEnum as Wire;
        assert_eq!(
            session_event_enum(SessionEvent::SessionResumed),
            Wire::SessionResumed
        );
    }

    /// Both pause reasons reach the wire as themselves, and neither reaches it
    /// as the other.
    ///
    /// The wire enum declares three values and this port names two of them, so
    /// the trap is not a missing arm but a swapped pair: `UserPause` published
    /// for a pause for want of energy tells an operator the charge is being
    /// held on purpose.
    #[test]
    fn each_pause_reason_maps_onto_its_own_wire_value() {
        use types::evse_manager::PauseChargingEVSEReasonEnum as Wire;
        assert_eq!(pause_reason_wire(PauseReason::NoEnergy), Wire::NoEnergy);
        assert_eq!(pause_reason_wire(PauseReason::UserPause), Wire::UserPause);
    }

    /// The charge finished announcement has to reach the wire as its own
    /// variant. It is published immediately before `TransactionFinished` and
    /// sits immediately before it in the wire enum too, so that neighbour
    /// compiles and produces a plausible looking pair of identical events;
    /// three API modules key their finished state on this one and would never
    /// reach it.
    /// Every config key the C++ module declares is declared here too.
    ///
    /// Substitutability is the whole point of the port, and it is decided by
    /// the manifest before a line of this module runs: the framework refuses
    /// to load a module whose manifest does not declare a key the config sets,
    /// so an undeclared key is a deployment that cannot start rather than a
    /// feature that is missing.
    ///
    /// Two keys had drifted out with nothing objecting, which is why this is a
    /// test and not a review note. `phase_rotation_grid_side` was a feature
    /// this module simply had not ported; `enable_nodered_interface` is one it
    /// cannot, and is declared at the C++ default and refused when switched on
    /// (`core::config::reject_requested_workarounds`). Either way the answer
    /// is the same: declare the key.
    ///
    /// The comparison is over the module's own `config:` block. A key the port
    /// deliberately does not declare would go in `NOT_DECLARED` with its
    /// reason; it is empty, and an empty list is the claim that there is no
    /// such key.
    mod the_manifest_declares_what_the_cpp_declares {
        const PORT: &str = include_str!("../manifest.yaml");
        const CPP: &str = include_str!("../../EvseManager/manifest.yaml");

        /// `(key, why this module does not declare it)`.
        const NOT_DECLARED: &[(&str, &str)] = &[];

        /// The keys of the module's own `config:` block, which is the one
        /// between `config:` at column zero and the next such key.
        fn config_keys(manifest: &str) -> Vec<&str> {
            let body = manifest
                .split_once("\nconfig:\n")
                .expect("a manifest declares a module config block")
                .1;
            let body = body
                .split_once("\nprovides:")
                .expect("a manifest declares what it provides")
                .0;
            let mut keys: Vec<&str> = body
                .lines()
                .filter_map(|line| {
                    let key = line.strip_prefix("  ")?;
                    let name = key.strip_suffix(':')?;
                    (!name.starts_with(' ') && !name.starts_with('#')).then_some(name)
                })
                .collect();
            keys.sort_unstable();
            keys.dedup();
            keys
        }

        #[test]
        fn no_cpp_config_key_is_undeclared_here() {
            let ours = config_keys(PORT);
            let missing: Vec<&str> = config_keys(CPP)
                .into_iter()
                .filter(|key| !ours.contains(key))
                .filter(|key| !NOT_DECLARED.iter().any(|(named, _)| named == key))
                .collect();

            assert!(
                missing.is_empty(),
                "config key(s) {missing:?} are declared by the C++ module and not here, \
                 so a deployment that sets one cannot load this module"
            );
        }

        /// And the census is not vacuous: both blocks are found and both are
        /// the size a module manifest is. A parser that found nothing would
        /// report every key as declared.
        #[test]
        fn both_config_blocks_were_actually_read() {
            assert!(config_keys(CPP).len() > 50, "{:?}", config_keys(CPP).len());
            assert!(config_keys(PORT).len() > 50, "{:?}", config_keys(PORT).len());
            assert!(
                config_keys(CPP).contains(&"enable_nodered_interface"),
                "the C++ block is the module's own"
            );
        }
    }

    /// The phase rotation and the two usage records the energy flow request
    /// carries, which `energyImpl` keeps per slot.
    ///
    /// Both were absent: the request serialized `energy_usage_root` and
    /// `energy_usage_leaves` as `None` whatever the meters reported, and
    /// `phase_rotation_grid_side` was not even a key this manifest declared,
    /// so a deployment that set it could not start the module.
    mod the_energy_usage_records {
        use super::*;

        /// `everest::helpers`' own test fixture, field for field.
        fn a_reading() -> types::powermeter::Powermeter {
            types::powermeter::Powermeter {
                energy_wh_import: types::units::Energy {
                    total: 0.0,
                    l_1: Some(1.0),
                    l_2: Some(2.0),
                    l_3: Some(3.0),
                },
                voltage_v: Some(types::units::Voltage {
                    dc: None,
                    l_1: Some(231.0),
                    l_2: Some(232.0),
                    l_3: Some(233.0),
                }),
                current_a: Some(types::units::Current {
                    dc: None,
                    l_1: Some(11.0),
                    l_2: Some(12.0),
                    l_3: Some(13.0),
                    n: None,
                }),
                power_w: Some(types::units::Power {
                    total: 0.0,
                    l_1: Some(21.0),
                    l_2: Some(22.0),
                    l_3: Some(23.0),
                }),
                frequency_hz: Some(types::units::Frequency {
                    l_1: 50.0,
                    l_2: Some(50.0),
                    l_3: Some(50.0),
                }),
                ..empty_powermeter("2024-01-01T00:00:00Z".to_owned())
            }
        }

        fn phases(meter: &types::powermeter::Powermeter) -> (Vec<f64>, Vec<f64>, Vec<f64>, Vec<f64>) {
            let voltage = meter.voltage_v.as_ref().expect("a voltage");
            let current = meter.current_a.as_ref().expect("a current");
            let power = meter.power_w.as_ref().expect("a power");
            let energy = &meter.energy_wh_import;
            (
                vec![
                    voltage.l_1.unwrap(),
                    voltage.l_2.unwrap(),
                    voltage.l_3.unwrap(),
                ],
                vec![
                    current.l_1.unwrap(),
                    current.l_2.unwrap(),
                    current.l_3.unwrap(),
                ],
                vec![power.l_1.unwrap(), power.l_2.unwrap(), power.l_3.unwrap()],
                vec![energy.l_1.unwrap(), energy.l_2.unwrap(), energy.l_3.unwrap()],
            )
        }

        /// `HelpersTest.phase_rotation_no_op_values`: the identity, and the
        /// fallback for anything the notation does not name.
        #[test]
        fn no_rotation_leaves_every_phase_where_it_was() {
            for notation in ["RST", "", "garbage"] {
                let rotation = PhaseRotation::from_wire(notation);
                assert_eq!(rotation, PhaseRotation::Rst, "{notation}");

                let rotated = apply_phase_rotation(a_reading(), rotation);

                assert_eq!(phases(&rotated), phases(&a_reading()), "{notation}");
            }
        }

        /// `HelpersTest.phase_rotation_TRS`, on all four of the measurements
        /// that test names, plus the two figures a rotation must not move.
        #[test]
        fn the_trs_rotation_moves_every_per_phase_measurement() {
            let rotated = apply_phase_rotation(a_reading(), PhaseRotation::Trs);

            assert_eq!(
                phases(&rotated),
                (
                    vec![232.0, 233.0, 231.0],
                    vec![12.0, 13.0, 11.0],
                    vec![22.0, 23.0, 21.0],
                    vec![2.0, 3.0, 1.0],
                )
            );
            // The totals and the frequency are invariant, which the C++ test
            // asserts for the same reason: they are not per phase.
            assert_eq!(rotated.power_w.as_ref().unwrap().total, 0.0);
            assert_eq!(rotated.energy_wh_import.total, 0.0);
            assert_eq!(rotated.frequency_hz.as_ref().unwrap().l_1, 50.0);
        }

        /// `HelpersTest.phase_rotation_STR`, the other direction. Driven
        /// because one rotation alone passes with the two swapped: `STR` and
        /// `TRS` are each other's inverse.
        #[test]
        fn the_str_rotation_is_the_other_direction() {
            let rotated = apply_phase_rotation(a_reading(), PhaseRotation::Str);

            assert_eq!(
                phases(&rotated).0,
                vec![233.0, 231.0, 232.0],
                "reported L1 is grid L2"
            );
            assert_eq!(
                PhaseRotation::from_wire("STR"),
                PhaseRotation::Str,
                "and the notation names it"
            );
            assert_eq!(PhaseRotation::from_wire("TRS"), PhaseRotation::Trs);
        }

        /// A measurement the meter did not report stays unreported: a
        /// rotation that invented an empty one would put three `None` phases
        /// on the wire where the C++ sends no measurement at all.
        ///
        /// **Not mutation covered, and said so rather than counted.** The
        /// option types give this: no compiling rewrite of the macro can
        /// materialize an absent measurement, because the generated wire
        /// structs derive no `Default`. It is here as a statement of the
        /// intent a different representation would have to keep, not as
        /// coverage of a reachable mistake.
        #[test]
        fn a_measurement_the_meter_did_not_report_is_not_invented() {
            let rotated = apply_phase_rotation(
                empty_powermeter("2024-01-01T00:00:00Z".to_owned()),
                PhaseRotation::Trs,
            );

            assert!(rotated.voltage_v.is_none());
            assert!(rotated.current_a.is_none());
            assert!(rotated.power_w.is_none());
            assert!(rotated.var.is_none());
            assert!(rotated.energy_wh_export.is_none());
        }

        /// The request carries whichever slots have reported and nothing for
        /// the ones that have not, which is what the C++ two guarded
        /// subscriptions produce.
        #[test]
        fn the_request_carries_the_records_the_slots_reported() {
            use RsEvseManager::core::config::ChargeMode;
            use RsEvseManager::core::energy::Publish;
            use RsEvseManager::core::path::iec::AcState;

            let tree = energy_tree(ChargeMode::Ac);
            let publish = || Publish {
                charger_state: AcState::Idle,
                bidirectional: false,
                priority: false,
            };

            let empty = energy_flow_request(&tree.flow_request(publish()), &EnergyUsage::default());
            assert!(empty.energy_usage_root.is_none());
            assert!(empty.energy_usage_leaves.is_none());

            let usage = EnergyUsage {
                root: Some(a_reading()),
                leaves: None,
            };
            let root_only = energy_flow_request(&tree.flow_request(publish()), &usage);
            assert_eq!(
                root_only.energy_usage_root.as_ref().map(phases),
                Some(phases(&a_reading())),
                "the grid side record reaches the root"
            );
            assert!(
                root_only.energy_usage_leaves.is_none(),
                "and a slot that has not reported carries nothing"
            );
        }
    }

    #[test]
    fn the_charge_finished_announcement_maps_onto_its_own_wire_variant() {
        use types::evse_manager::SessionEventEnum as Wire;
        assert_eq!(
            session_event_enum(SessionEvent::ChargingFinished),
            Wire::ChargingFinished
        );
        assert_ne!(
            session_event_enum(SessionEvent::ChargingFinished),
            session_event_enum(SessionEvent::TransactionFinished)
        );
    }

    /// The switching break has to reach the wire as its own variant. It sits
    /// next to `StoppingCharging` in the wire enum and one state entry away
    /// from `PrepareCharging` in the core, so either neighbour would compile
    /// and would tell a consumer the session is ending or restarting rather
    /// than that it is taking a break.
    #[test]
    fn the_switching_break_maps_onto_its_own_wire_variant() {
        use types::evse_manager::SessionEventEnum as Wire;
        assert_eq!(
            session_event_enum(SessionEvent::SwitchingPhases),
            Wire::SwitchingPhases
        );
    }

    #[test]
    fn the_announced_source_carries_the_deciding_entry() {
        let published = enable_source_struct(EnableEntry {
            source: EnableSource::Csms,
            state: EnableState::Disable,
            priority: 7,
        });
        assert_eq!(published.enable_priority, 7);
        assert_eq!(
            published.enable_state,
            types::evse_manager::EnableDisableSourceAutoGenEnableState::Disable
        );
        assert_eq!(
            published.enable_source,
            types::evse_manager::EnableDisableSourceAutoGenEnableSource::CSMS
        );
    }

    /// The AC conversions at the boundary.
    ///
    /// Each is a pure function over adjacent same typed fields, which is the
    /// one failure mode a type checked port still has: `l_2` read into `l3_w`,
    /// or a nominal voltage written into a nominal frequency, compiles and
    /// reads plausibly. Nothing downstream can tell the difference, so the
    /// pinning has to happen here.
    mod ac_conversions {
        use super::*;

        fn wire_power() -> types::units::Power {
            types::units::Power {
                total: 3_300.0,
                // Three distinct values, so a transposition between any two
                // shows up rather than cancelling.
                l_1: Some(1_000.0),
                l_2: Some(1_100.0),
                l_3: Some(1_200.0),
            }
        }

        #[test]
        fn a_meter_power_keeps_each_phase_on_its_own_phase_coming_in() {
            let power = power_from(wire_power());
            assert_eq!(power.total_w, 3_300.0);
            assert_eq!(power.l1_w, Some(1_000.0));
            assert_eq!(power.l2_w, Some(1_100.0));
            assert_eq!(power.l3_w, Some(1_200.0));
        }

        #[test]
        fn a_power_going_back_out_is_the_one_that_came_in() {
            assert_eq!(power_payload(&power_from(wire_power())), wire_power());
        }

        /// An absent phase figure stays absent rather than becoming a zero,
        /// which is a declaration the port never made.
        #[test]
        fn an_absent_phase_figure_survives_the_round_trip() {
            let single = types::units::Power {
                total: 3_300.0,
                l_1: Some(3_300.0),
                l_2: None,
                l_3: None,
            };
            assert_eq!(power_payload(&power_from(single.clone())), single);
        }

        /// The envelope halves are the same type and the two commands take
        /// different ones, so this pins that the charge half stays the charge
        /// half.
        #[test]
        fn an_envelope_keeps_its_charge_and_discharge_halves_apart() {
            let set = AcPowerSet {
                charge_power: Power {
                    total_w: 22_080.0,
                    ..Power::default()
                },
                discharge_power: Some(Power {
                    total_w: 3_680.0,
                    ..Power::default()
                }),
            };
            let (charge, discharge) = ac_power_set_payload(&set);
            assert_eq!(charge.total, 22_080.0);
            assert_eq!(discharge.expect("filled").total, 3_680.0);
        }

        /// A hertz figure and a volt figure, adjacent and both `f64`. The wire
        /// struct orders its fields alphabetically and the C++ brace
        /// initializes them frequency, voltage, connectors, so the two orders
        /// disagree and a positional port swaps them.
        #[test]
        fn the_general_parameters_keep_the_frequency_and_the_voltage_apart() {
            let payload = ac_parameters_payload(&AcParameters {
                nominal_frequency_hz: 50.0,
                nominal_voltage_v: 230.0,
                connectors: vec![AcConnector::SinglePhase, AcConnector::ThreePhase],
                max_power_asymmetry_w: None,
                power_ramp_limitation_percent_per_min: None,
                evse_max_reactive_power_var: Some(4_000.0),
            });

            assert_eq!(payload.nominal_frequency, 50.0);
            assert_eq!(payload.nominal_voltage, 230.0);
            assert_eq!(
                payload.connectors,
                vec![
                    types::iso15118::Connector::SinglePhase,
                    types::iso15118::Connector::ThreePhase
                ]
            );
            assert_eq!(payload.evse_max_reactive_power, Some(4_000.0));
            assert_eq!(payload.max_power_asymmetry, None);
            assert_eq!(payload.power_ramp_limitation, None);
        }

        /// The two fields the C++ leaves as open `TODO`s stay absent. A zero
        /// target frequency is a declaration and a missing one is not, and the
        /// wire type makes both optional so the two can be told apart.
        #[test]
        fn a_target_power_declares_no_frequency_and_no_reactive_power() {
            let payload = ac_target_values_payload(&Power {
                total_w: 11_040.0,
                ..Power::default()
            });
            assert_eq!(payload.target_active_power.total, 11_040.0);
            assert_eq!(payload.target_frequency, None);
            assert_eq!(payload.target_reactive_power, None);
        }

        /// Total over the wire enum, and each value distinct. A conversion that
        /// folded two services together would let a DC session take the AC
        /// target power arm.
        #[test]
        fn every_selected_service_maps_to_its_own_value() {
            use types::iso15118::ServiceCategory as Wire;
            let pairs = [
                (Wire::AC, SelectedService::Ac),
                (Wire::DC, SelectedService::Dc),
                (Wire::WPT, SelectedService::Wpt),
                (Wire::DC_ACDP, SelectedService::DcAcdp),
                (Wire::AC_BPT, SelectedService::AcBpt),
                (Wire::DC_BPT, SelectedService::DcBpt),
                (Wire::DC_ACDP_BPT, SelectedService::DcAcdpBpt),
                (Wire::MCS, SelectedService::Mcs),
                (Wire::MCS_BPT, SelectedService::McsBpt),
                (Wire::AC_DER_IEC, SelectedService::AcDerIec),
                (Wire::AC_DER_SAE, SelectedService::AcDerSae),
                (Wire::Internet, SelectedService::Internet),
                (Wire::ParkingStatus, SelectedService::ParkingStatus),
            ];
            for (wire, expected) in &pairs {
                assert_eq!(selected_service_from(wire), *expected, "{wire:?}");
            }

            // The two the AC target power arm answers for are exactly the two
            // the C++ names, and no mapping collapses onto them.
            let ac_like: Vec<&SelectedService> = pairs
                .iter()
                .map(|(_, service)| service)
                .filter(|service| service.is_ac_target_power_service())
                .collect();
            assert_eq!(
                ac_like,
                vec![&SelectedService::Ac, &SelectedService::AcBpt]
            );
        }
    }

    #[test]
    fn the_metering_request_names_the_transaction_and_the_evse_apart() {
        // `evse_id` and `transaction_id` are both `String` and adjacent in the
        // generated struct, so a swap compiles and silently bills the wrong
        // identity. The C++ passes `shared_context.session_uuid` as the
        // transaction id (`Charger::start_transaction`) and `evse_id` as the
        // EVSE id.
        let tag = wire_id_tag("tok", types::authorization::IdTokenType::ISO14443, false);
        let request = transaction_request("DE*PNX*E1*1", "sess-uuid", Some(&tag), None);

        assert_eq!(request.evse_id, "DE*PNX*E1*1");
        assert_eq!(request.transaction_id, "sess-uuid");
        assert_eq!(request.identification_data, Some("tok".to_owned()));
    }

    #[test]
    fn the_metering_request_carries_the_ocmf_assignment_the_cpp_sends() {
        let request = transaction_request("evse", "sess", None, None);

        assert_eq!(
            request.identification_status,
            types::powermeter::OCMFUserIdentificationStatus::ASSIGNED
        );
        assert!(request.identification_flags.is_empty());
        assert_eq!(request.identification_level, None);
        assert_eq!(request.identification_data, None);
        assert_eq!(request.tariff_text, None);
    }

    /// `utils::convert_to_ocmf_identification_type` in `EvseManager/utils.hpp`,
    /// written out as a table rather than re-derived, so this disagrees with the
    /// mapping instead of restating it.
    ///
    /// The last row is the one worth reading twice: `MacAddress` is the only
    /// credential the C++ answers `UNDEFINED` for, because OCMF has no
    /// autocharge type.
    fn the_cpp_ocmf_mapping() -> Vec<(
        types::authorization::IdTokenType,
        types::powermeter::OCMFIdentificationType,
    )> {
        use types::authorization::IdTokenType as Wire;
        use types::powermeter::OCMFIdentificationType as Ocmf;
        vec![
            (Wire::Central, Ocmf::CENTRAL),
            (Wire::eMAID, Ocmf::EMAID),
            (Wire::ISO14443, Ocmf::ISO14443),
            (Wire::ISO15693, Ocmf::ISO15693),
            (Wire::KeyCode, Ocmf::KEY_CODE),
            (Wire::Local, Ocmf::LOCAL),
            (Wire::NoAuthorization, Ocmf::NONE),
            (Wire::MacAddress, Ocmf::UNDEFINED),
        ]
    }

    #[test]
    fn every_credential_reaches_the_meter_as_the_ocmf_type_the_cpp_sends() {
        // The whole of `convert_to_ocmf_identification_type`, driven from a
        // wire token through `id_tag_from` and into the request the meter is
        // handed, which is the path a deployment takes.
        for (wire, expected) in the_cpp_ocmf_mapping() {
            let tag = wire_id_tag("id", wire.clone(), false);

            let request = transaction_request("evse", "sess", Some(&tag), None);

            assert_eq!(request.identification_type, expected, "{wire:?}");
        }
    }

    #[test]
    fn the_ocmf_mapping_covers_every_credential_exactly_once() {
        // A table cannot be made exhaustive by the compiler. Without this, a
        // row deleted from the table takes its case with it and the loop above
        // still passes.
        let mapping = the_cpp_ocmf_mapping();
        assert_eq!(mapping.len(), EVERY_WIRE_TOKEN_TYPE.len());
        for wire in EVERY_WIRE_TOKEN_TYPE {
            assert_eq!(
                mapping.iter().filter(|(named, _)| named == wire).count(),
                1,
                "{wire:?} is named once in the table"
            );
        }
    }

    #[test]
    fn a_card_is_not_billed_as_a_contract_and_neither_is_undefined() {
        // The two rows the pricing suite's `assert_called_with` compares. This
        // port answered `EMAID` or `UNDEFINED` and nothing else, so a card
        // reached a signed metrology record as a user of unstated kind.
        let card = wire_id_tag(
            "RFID_VALID1",
            types::authorization::IdTokenType::ISO14443,
            false,
        );
        let contract = wire_id_tag("emaid", types::authorization::IdTokenType::eMAID, true);

        assert_eq!(
            transaction_request("evse", "sess", Some(&card), None).identification_type,
            types::powermeter::OCMFIdentificationType::ISO14443
        );
        assert_eq!(
            transaction_request("evse", "sess", Some(&contract), None).identification_type,
            types::powermeter::OCMFIdentificationType::EMAID
        );
    }

    #[test]
    fn the_authorization_kind_no_longer_decides_the_ocmf_type() {
        // `identification_type` reads `id_token.type`, and `authorization_type`
        // is not that question. A contract presented over external
        // identification is still an eMAID and a card negotiated over ISO 15118
        // is still a card, which is exactly the pair the old bool could not
        // tell apart.
        let contract_over_eim =
            wire_id_tag("emaid", types::authorization::IdTokenType::eMAID, false);
        let card_over_plug_and_charge = wire_id_tag(
            "card",
            types::authorization::IdTokenType::ISO14443,
            true,
        );

        assert_eq!(
            transaction_request("evse", "sess", Some(&contract_over_eim), None)
                .identification_type,
            types::powermeter::OCMFIdentificationType::EMAID
        );
        assert_eq!(
            transaction_request("evse", "sess", Some(&card_over_plug_and_charge), None)
                .identification_type,
            types::powermeter::OCMFIdentificationType::ISO14443
        );
    }

    #[test]
    fn a_transaction_with_no_identity_is_billed_as_an_unstated_kind() {
        // Not reachable from `core`, and `UNDEFINED` is what OCMF names for a
        // user of unstated kind, so it is the only answer that does not claim a
        // credential nobody presented.
        assert_eq!(
            transaction_request("evse", "sess", None, None).identification_type,
            types::powermeter::OCMFIdentificationType::UNDEFINED
        );
    }

    #[test]
    fn the_metering_request_carries_the_tariff_text_verbatim() {
        // `Charger::start_transaction` puts
        // `validation_result.tariff_messages.at(0).content` here unchanged, and
        // the string is what a legal metrology meter signs, so it is relayed
        // rather than reformatted.
        let tag = wire_id_tag(
            "RFID_VALID1",
            types::authorization::IdTokenType::ISO14443,
            false,
        );

        let request = transaction_request(
            "evse",
            "sess",
            Some(&tag),
            Some("GBP 0.12/kWh, no idle fee"),
        );

        assert_eq!(
            request.tariff_text.as_deref(),
            Some("GBP 0.12/kWh, no idle fee")
        );
    }

    #[test]
    fn a_transaction_with_no_tariff_leaves_the_field_unset() {
        // The C++'s `empty()` guard. Nothing is defaulted in: an unset field
        // says the authorizing party quoted no price, and any string here would
        // be one this module invented.
        let tag = wire_id_tag(
            "RFID_VALID1",
            types::authorization::IdTokenType::ISO14443,
            false,
        );

        assert_eq!(
            transaction_request("evse", "sess", Some(&tag), None).tariff_text,
            None
        );
    }

    #[test]
    fn the_whole_request_matches_what_the_pricing_suite_asserts() {
        // The pricing tests assert one `assert_called_with` over the whole
        // request, so every field has to be right at once; two of them being
        // right in separate tests is what let this port fail eight runs with a
        // green suite. This is that assertion, field by field, on the case
        // those tests drive: an ISO 14443 card with a tariff quoted.
        let tag = wire_id_tag(
            "RFID_VALID1",
            types::authorization::IdTokenType::ISO14443,
            false,
        );

        let request = transaction_request(
            "1",
            "9a88f6f2-0000-0000-0000-000000000000",
            Some(&tag),
            Some("GBP 0.12/kWh, no idle fee"),
        );

        assert_eq!(request.evse_id, "1");
        assert_eq!(
            request.transaction_id,
            "9a88f6f2-0000-0000-0000-000000000000"
        );
        assert_eq!(
            request.identification_data.as_deref(),
            Some("RFID_VALID1")
        );
        assert_eq!(
            request.identification_status,
            types::powermeter::OCMFUserIdentificationStatus::ASSIGNED
        );
        assert!(request.identification_flags.is_empty());
        assert_eq!(
            request.identification_type,
            types::powermeter::OCMFIdentificationType::ISO14443
        );
        assert_eq!(request.identification_level, None);
        assert_eq!(
            request.tariff_text.as_deref(),
            Some("GBP 0.12/kWh, no idle fee")
        );
    }

    #[test]
    fn a_token_type_off_the_wire_reaches_the_core_as_itself() {
        // The inbound half of the same path, over every value the interface
        // names. `id_tag_from` is the only constructor of an `IdTag` in
        // production, so a value it mistranslated would be billed wrong with
        // nothing else to catch it. Two distinct wire values reaching one core
        // value is what this rules out.
        let mut seen = Vec::new();
        for wire in EVERY_WIRE_TOKEN_TYPE {
            let tag = wire_id_tag("id", wire.clone(), false);

            assert!(
                !seen.contains(&tag.token_type()),
                "{wire:?} collides with an earlier credential kind"
            );
            seen.push(tag.token_type());
        }
        assert_eq!(seen.len(), EVERY_WIRE_TOKEN_TYPE.len());
    }

    #[test]
    fn a_meter_that_refuses_the_transaction_is_not_a_success() {
        // The call returns a status rather than throwing, so a request that
        // arrived and was refused looks like a delivered command. The C++ logs
        // it at error (`Charger::start_transaction`), and reporting `Ok` here
        // would hide the one thing this arm exists to do.
        use types::powermeter::TransactionRequestStatus as Status;

        assert_eq!(
            metering_status(&Status::UNEXPECTED_ERROR, Some("meter busy")),
            EffectOutcome::Failed("meter busy".to_owned())
        );
        assert_eq!(metering_status(&Status::OK, None), EffectOutcome::Ok);
        // Not every meter implements OCMF transactions, and the C++ moves on
        // rather than treating that as a fault.
        assert_eq!(
            metering_status(&Status::NOT_SUPPORTED, None),
            EffectOutcome::Ok
        );
    }

    #[test]
    fn the_car_side_meter_bills_whenever_one_is_connected() {
        // `EvseManager::r_powermeter_billing` prefers the car side and falls
        // back to the grid side, so a two meter installation bills on the meter
        // nearest the vehicle.
        assert_eq!(billing_meter(&["car"], &["grid"]), Some(&"car"));
        assert_eq!(billing_meter(&[], &["grid"]), Some(&"grid"));
        assert_eq!(billing_meter(&["car"], &[]), Some(&"car"));
        assert_eq!(billing_meter::<&str>(&[], &[]), None);
    }

    /// The subscription follows the same preference. The C++ has exactly one
    /// `subscribe_powermeter`, and it is on `r_powermeter_billing()`
    /// (`EvseManager.cpp:1151-1152`), so the meter that bills is also the only
    /// meter whose readings reach the module at all.
    #[test]
    fn the_meter_that_bills_is_the_meter_whose_readings_are_taken() {
        assert!(meter_feeds_the_core(POWERMETER_CAR_SIDE, true));
        assert!(!meter_feeds_the_core(POWERMETER_GRID_SIDE, true));

        assert!(meter_feeds_the_core(POWERMETER_GRID_SIDE, false));
        assert!(!meter_feeds_the_core(POWERMETER_CAR_SIDE, false));
    }

    /// Both halves of the fault gate, over every combination. A gate that lost
    /// either half is a port taken out of service by a meter it is not billing
    /// from, or by a meter on a deployment that asked for faults to be
    /// tolerated.
    #[test]
    fn a_powermeter_fault_reaches_the_module_only_from_the_billing_slot_and_only_if_asked() {
        for (slot, car_side_wired, fail_on_errors, expected) in [
            (POWERMETER_CAR_SIDE, true, true, true),
            (POWERMETER_GRID_SIDE, true, true, false),
            (POWERMETER_GRID_SIDE, false, true, true),
            (POWERMETER_CAR_SIDE, false, true, false),
            // The setting off: no slot reaches the module, which is the empty
            // vector the C++ hands `ErrorHandling`.
            (POWERMETER_CAR_SIDE, true, false, false),
            (POWERMETER_GRID_SIDE, false, false, false),
        ] {
            assert_eq!(
                powermeter_fault_reaches_the_module(slot, car_side_wired, fail_on_errors),
                expected,
                "{slot} with car side wired {car_side_wired} and the setting {fail_on_errors}"
            );
        }
    }

    /// The capability report does **not** follow that preference, and the
    /// difference is the point: the C++ subscribes `capabilities` on
    /// `r_powermeter_car_side[0]` alone (`EvseManager.cpp:245-251`), because
    /// the floor it describes is a property of the meter between the port and
    /// the vehicle.
    ///
    /// The port subscribed both slots into one record, so on a deployment with
    /// both meters wired the vehicle was offered whichever floor arrived last,
    /// and a grid side minimum could raise a floor nothing measures at the
    /// vehicle.
    #[test]
    fn only_the_car_side_meter_reports_the_floors_the_vehicle_is_offered() {
        assert!(meter_reports_the_floors(POWERMETER_CAR_SIDE));
        assert!(!meter_reports_the_floors(POWERMETER_GRID_SIDE));

        // And it is not the billing preference: a port with no car side meter
        // bills from the grid side one and still merges no floor from it,
        // because there is no meter at the vehicle to describe.
        assert!(meter_feeds_the_core(POWERMETER_GRID_SIDE, false));
        assert!(!meter_reports_the_floors(POWERMETER_GRID_SIDE));
    }

    /// The all three or nothing gate, and that each phase lands on its own
    /// field.
    #[test]
    fn a_phase_current_set_is_taken_only_when_all_three_phases_are_present() {
        let complete = types::units::Current {
            dc: None,
            n: None,
            l_1: Some(1.0),
            l_2: Some(2.0),
            l_3: Some(3.0),
        };
        assert_eq!(
            phase_currents_from(&complete),
            Some(PhaseCurrents {
                l1_a: 1.0,
                l2_a: 2.0,
                l3_a: 3.0,
            })
        );

        // Each phase on its own is enough to refuse the whole set, which is the
        // `and .L1 and .L2 and .L3` of `EvseManager.cpp:1157`.
        for missing in [
            types::units::Current {
                dc: None,
                n: None,
                l_1: None,
                l_2: Some(2.0),
                l_3: Some(3.0),
            },
            types::units::Current {
                dc: None,
                n: None,
                l_1: Some(1.0),
                l_2: None,
                l_3: Some(3.0),
            },
            types::units::Current {
                dc: None,
                n: None,
                l_1: Some(1.0),
                l_2: Some(2.0),
                l_3: None,
            },
        ] {
            assert_eq!(phase_currents_from(&missing), None, "{missing:?}");
        }

        // A DC only record carries no phases at all.
        assert_eq!(
            phase_currents_from(&types::units::Current {
                dc: Some(200.0),
                n: None,
                l_1: None,
                l_2: None,
                l_3: None,
            }),
            None
        );
    }

    /// The soft overcurrent error has to be in `RAISABLE_ERRORS` or the raise
    /// is logged and dropped here, leaving the session to come down on the
    /// `Inoperative` alone with no MREC4 on the wire for OCPP to report.
    ///
    /// Pinned against the core's own constant rather than a literal, so the two
    /// spellings cannot drift.
    #[test]
    fn the_soft_overcurrent_error_reaches_the_wire_rather_than_the_log() {
        assert!(
            raisable_error(RsEvseManager::core::faults::MREC4_OVER_CURRENT_FAILURE).is_some(),
            "MREC4OverCurrentFailure is not raisable, so the raise is dropped"
        );
    }

    #[test]
    fn every_control_pilot_state_maps_across() {
        use types::board_support_common::BspEventAutoGenEvent as Wire;
        assert_eq!(cp_event_from(Wire::A), CpEvent::A);
        assert_eq!(cp_event_from(Wire::B), CpEvent::B);
        assert_eq!(cp_event_from(Wire::C), CpEvent::C);
        assert_eq!(cp_event_from(Wire::D), CpEvent::D);
        assert_eq!(cp_event_from(Wire::E), CpEvent::E);
        assert_eq!(cp_event_from(Wire::F), CpEvent::F);
        assert_eq!(cp_event_from(Wire::PowerOn), CpEvent::PowerOn);
        assert_eq!(cp_event_from(Wire::PowerOff), CpEvent::PowerOff);
        assert_eq!(cp_event_from(Wire::Disconnected), CpEvent::Disconnected);
    }

    /// `EvseManager.cpp:1215-1225` derives one bit from three SLAC states, and
    /// the inversion is the whole of it: every state but `UNMATCHED` means
    /// matching has begun.
    #[test]
    fn every_slac_state_but_unmatched_means_matching_has_started() {
        use types::slac::State;
        assert!(!matching_started_from(&State::UNMATCHED));
        assert!(matching_started_from(&State::MATCHING));
        assert!(matching_started_from(&State::MATCHED));
    }

    /// The stack names the failure on the wire and this module republishes it
    /// on its own interface unchanged (`EvseManager.cpp:365-370`), so the two
    /// directions have to agree or a reason changes identity in transit.
    ///
    /// A round trip alone pins a bijection, not an identity: relabelling a pair
    /// consistently in both mappers passes it. So each row also names the core
    /// value absolutely. The riskiest row is the last, where the two spellings
    /// differ in case, `FailedTlsHandshake` against `FailedTLSHandshake`.
    #[test]
    fn every_session_failure_reason_survives_the_round_trip() {
        use types::evse_manager::HlcSessionFailedReasonEnum as Wire;
        use RsEvseManager::core::event::HlcSessionFailure as Reason;
        for (wire, reason) in [
            (
                Wire::ProtocolNegotiationFailed,
                Reason::ProtocolNegotiationFailed,
            ),
            (Wire::AuthorizationFailed, Reason::AuthorizationFailed),
            (
                Wire::ChargingParametersNotAccepted,
                Reason::ChargingParametersNotAccepted,
            ),
            (
                Wire::EnergyTransferSetupFailed,
                Reason::EnergyTransferSetupFailed,
            ),
            (Wire::ChargingInterrupted, Reason::ChargingInterrupted),
            (Wire::FailedTLSHandshake, Reason::FailedTlsHandshake),
            (Wire::UnexpectedSessionEnd, Reason::UnexpectedSessionEnd),
        ] {
            assert_eq!(hlc_session_failure_from(&wire), reason, "{wire:?} inbound");
            assert_eq!(
                hlc_session_failure_enum(reason),
                wire,
                "{reason:?} outbound"
            );
        }
    }

    /// `Charger` raises exactly two of the interface's five
    /// (`Charger.cpp:1360`, `:1362`, `:2268`, `:2283`), so the mapper carries
    /// two. Pinned so that widening the core enum without widening this is a
    /// failure rather than a compile that sends the wrong error.
    #[test]
    fn the_two_errors_the_module_originates_reach_the_vehicle_as_themselves() {
        use RsEvseManager::core::effect::EvseError as Cause;
        assert_eq!(
            evse_error_enum(Cause::EmergencyShutdown),
            types::iso15118::EvseError::Error_EmergencyShutdown
        );
        assert_eq!(
            evse_error_enum(Cause::UtilityInterruptEvent),
            types::iso15118::EvseError::Error_UtilityInterruptEvent
        );
    }

    /// The three isolation statuses the module originates reach the wire as
    /// themselves.
    ///
    /// `No_IMD` is the one that matters here: the wire spells it with an
    /// underscore and in capitals, unlike every neighbour, so a mapper that
    /// reached for the plausible spelling would not compile, but one that
    /// reached for the plausible **neighbour** would. `Invalid` sits next to
    /// `Valid` in the wire enum and reads as a near synonym of it.
    #[test]
    fn the_three_isolation_statuses_the_module_originates_reach_the_vehicle_as_themselves() {
        use RsEvseManager::core::hlc::cable_check::IsolationStatus as Status;
        assert_eq!(
            isolation_status_enum(Status::Valid),
            types::iso15118::IsolationStatus::Valid
        );
        assert_eq!(
            isolation_status_enum(Status::Fault),
            types::iso15118::IsolationStatus::Fault
        );
        assert_eq!(
            isolation_status_enum(Status::NoImd),
            types::iso15118::IsolationStatus::No_IMD
        );
    }

    /// `call_stop_charging` carries two meanings and the boundary has to carry
    /// both: `true` asks the vehicle to end the session
    /// (`EvseManager.cpp:412`), `false` clears a request the previous session
    /// left standing (`EvseManager.cpp:1126`, on plug in). An arm that sends a
    /// literal answers one of the two and silently mistranslates the other.
    ///
    /// Asserted against the source because the arm calls a method on a
    /// generated publisher, which needs a live runtime to construct, so there
    /// is no value to compare. The same reason `dropped_hlc_facts` below reads
    /// its own source.
    ///
    /// Read as a window from the arm rather than as the one line containing it,
    /// so a reflow that wraps the arm across two lines is not a failure. The
    /// fact being pinned is which value is forwarded, not how it wrapped.
    #[test]
    fn the_stop_request_forwards_its_own_payload_rather_than_a_literal() {
        const SOURCE: &str = include_str!("main.rs");

        let at = SOURCE
            .find("HlcUpdate::StopCharging(stop)")
            .expect("main.rs has no `HlcUpdate::StopCharging` arm");
        let arm: String = SOURCE[at..]
            .chars()
            .take(200)
            .filter(|c| !c.is_whitespace())
            .collect();

        assert!(
            arm.contains("hlc.stop_charging(*stop)"),
            "the arm must forward the payload, got `{arm}`"
        );
    }

    /// The isolation status and the cable check verdict are two separate
    /// commands that the C++ sends adjacently at three of its four status sites
    /// (`EvseManager.cpp:2025-2026`, and `:2011`/`:2015` followed by `:2287` or
    /// `:2539`). The core keeps them in separate variants carrying separate
    /// types, so neither can be built into the other; this pins the last step,
    /// that the status variant reaches `update_isolation_status` and not
    /// `cable_check_finished` beside it.
    ///
    /// Asserted against the source for the same reason the stop request is:
    /// the arm calls a method on a generated publisher. A mutation replacing
    /// the whole arm with `hlc.cable_check_finished(true)` passed every other
    /// test in this file, which is what this test exists to catch.
    #[test]
    fn the_isolation_status_arm_sends_the_status_command_and_not_the_verdict() {
        const SOURCE: &str = include_str!("main.rs");

        let at = SOURCE
            .find("HlcUpdate::IsolationStatus(status)")
            .expect("main.rs has no `HlcUpdate::IsolationStatus` arm");
        let arm: String = SOURCE[at..]
            .chars()
            .take(200)
            .filter(|c| !c.is_whitespace())
            .collect();

        assert!(
            arm.contains("hlc.update_isolation_status(isolation_status_enum(*status))"),
            "the arm must map the payload onto the status command, got `{arm}`"
        );
    }

    /// `EvseManager.cpp:420-425` is the one high level update whose absent
    /// requirement the C++ names out loud: `if (r_hlc.empty()) { EVLOG_warning
    /// << "HLC module not connected, cannot send error!"; return; }`. A fault
    /// the vehicle was never told about is indistinguishable from one it was,
    /// so the boundary says so rather than reporting success in silence.
    ///
    /// Asserted against the source for the same reason the stop request is:
    /// the arm sits inside a match on a generated publisher slot.
    #[test]
    fn an_absent_stack_is_named_when_an_error_cannot_be_sent() {
        const SOURCE: &str = include_str!("main.rs");

        let at = SOURCE
            .find("HlcUpdate::SendError(error) => hlc.send_error")
            .expect("main.rs has no `HlcUpdate::SendError` arm");
        // Whitespace is stripped **before** the window is applied, so the
        // budget counts source that matters rather than indentation. It was the
        // other way round and a new execution arm between the error send and
        // the absent slot arm pushed the latter out of a raw character window,
        // failing the test without either arm changing.
        let tail: String = SOURCE[at..]
            .chars()
            .filter(|c| !c.is_whitespace())
            .take(6000)
            .collect();

        assert!(
            tail.contains("matches!(update,HlcUpdate::SendError(_))"),
            "the absent slot arm must single out the error send"
        );
        assert!(
            tail.contains(r#"log::warn!("HLCmodulenotconnected,cannotsenderror!")"#),
            "the absent slot arm must say so"
        );
    }

    /// The verdict tables, against a second transcription of the same contract.
    ///
    /// `authorization_status_of` and `authorization_status_enum` are a pure
    /// transcription of `types/authorization.yaml`, and a transcription is
    /// exactly the shape where a single wrong pair reads as right: a verdict
    /// mapped to a neighbouring value compiles, round trips through nothing,
    /// and reaches the vehicle as the wrong answer.
    ///
    /// The match below is written from the interface rather than copied from
    /// the production table, so the two disagreeing is a failure. It is
    /// exhaustive, so a value added to the core enum stops this compiling
    /// rather than passing.
    ///
    /// What is not asserted anywhere, because the type system already forbids
    /// it, is the argument order of `authorization_response`. The two
    /// converters return distinct types, so transposing the call does not
    /// compile.
    #[test]
    fn every_verdict_reaches_the_wire_as_the_value_the_interface_names() {
        use types::authorization::AuthorizationStatus as Wire;
        use RsEvseManager::core::auth::AuthorizationStatus as Verdict;

        const EVERY_VERDICT: &[Verdict] = &[
            Verdict::Accepted,
            Verdict::Blocked,
            Verdict::ConcurrentTx,
            Verdict::Expired,
            Verdict::Invalid,
            Verdict::NoCredit,
            Verdict::NotAllowedTypeEvse,
            Verdict::NotAtThisLocation,
            Verdict::NotAtThisTime,
            Verdict::Unknown,
            Verdict::PinRequired,
            Verdict::Timeout,
        ];

        fn expected(verdict: Verdict) -> Wire {
            match verdict {
                Verdict::Accepted => Wire::Accepted,
                Verdict::Blocked => Wire::Blocked,
                Verdict::ConcurrentTx => Wire::ConcurrentTx,
                Verdict::Expired => Wire::Expired,
                Verdict::Invalid => Wire::Invalid,
                Verdict::NoCredit => Wire::NoCredit,
                Verdict::NotAllowedTypeEvse => Wire::NotAllowedTypeEVSE,
                Verdict::NotAtThisLocation => Wire::NotAtThisLocation,
                Verdict::NotAtThisTime => Wire::NotAtThisTime,
                Verdict::Unknown => Wire::Unknown,
                Verdict::PinRequired => Wire::PinRequired,
                Verdict::Timeout => Wire::Timeout,
            }
        }

        for verdict in EVERY_VERDICT {
            let wire = authorization_status_enum(*verdict);
            assert_eq!(
                wire,
                expected(*verdict),
                "{verdict:?} reaches the wire wrong"
            );
            assert_eq!(
                authorization_status_of(wire),
                *verdict,
                "{verdict:?} does not survive the round trip"
            );
        }
    }

    /// The certificate half of the same answer, on the same terms.
    #[test]
    fn every_certificate_status_reaches_the_wire_as_the_value_the_interface_names() {
        use types::authorization::CertificateStatus as Wire;
        use RsEvseManager::core::auth::CertificateStatus as Verdict;

        const EVERY_STATUS: &[Verdict] = &[
            Verdict::Accepted,
            Verdict::SignatureError,
            Verdict::CertificateExpired,
            Verdict::CertificateRevoked,
            Verdict::NoCertificateAvailable,
            Verdict::CertChainError,
            Verdict::ContractCancelled,
        ];

        fn expected(status: Verdict) -> Wire {
            match status {
                Verdict::Accepted => Wire::Accepted,
                Verdict::SignatureError => Wire::SignatureError,
                Verdict::CertificateExpired => Wire::CertificateExpired,
                Verdict::CertificateRevoked => Wire::CertificateRevoked,
                Verdict::NoCertificateAvailable => Wire::NoCertificateAvailable,
                Verdict::CertChainError => Wire::CertChainError,
                Verdict::ContractCancelled => Wire::ContractCancelled,
            }
        }

        for status in EVERY_STATUS {
            let wire = certificate_status_enum(*status);
            assert_eq!(wire, expected(*status), "{status:?} reaches the wire wrong");
            assert_eq!(
                certificate_status_of(wire),
                *status,
                "{status:?} does not survive the round trip"
            );
        }
    }

    /// The autocharge token, filled by name against the C++ constants.
    ///
    /// Four of the eight fields of `ProvidedIdToken` are optional and three of
    /// the four the C++ sets are constants, so a wrong constant here offers
    /// `Auth` a token of the wrong kind and nothing else notices.
    #[test]
    fn the_autocharge_token_carries_the_three_constants_the_cpp_sets() {
        let payload =
            provided_token_payload(&RsEvseManager::core::hlc::ProvidedToken::Autocharge {
                id_token: "VID:AABBCCDDEEFF".into(),
                connectors: vec![3],
            })
            .expect("the autocharge arm builds its own token and cannot fail");

        assert_eq!(
            payload.authorization_type,
            types::authorization::AuthorizationType::Autocharge,
            "`EvseManager.cpp:44`"
        );
        assert_eq!(
            payload.id_token.r#type,
            types::authorization::IdTokenType::MacAddress,
            "`EvseManager.cpp:46`"
        );
        assert_eq!(payload.id_token.value, "VID:AABBCCDDEEFF");
        assert_eq!(payload.connectors, Some(vec![3]), "`EvseManager.cpp:47`");
    }

    /// The relayed contract token, which must arrive unchanged but for the one
    /// field the C++ replaces (`EvseManager.cpp:1033-1034`).
    #[test]
    fn a_relayed_contract_token_keeps_everything_but_its_connector_list() {
        let presented = types::authorization::ProvidedIdToken {
            authorization_type: types::authorization::AuthorizationType::PlugAndCharge,
            certificate: Some("-----BEGIN CERTIFICATE-----".into()),
            connectors: Some(vec![99]),
            id_token: types::authorization::IdToken {
                additional_info: None,
                r#type: types::authorization::IdTokenType::eMAID,
                value: "CONTRACT".into(),
            },
            iso_15118_certificate_hash_data: None,
            parent_id_token: None,
            prevalidated: Some(true),
            request_id: Some(7),
        };
        let carried = ::everestrs::serde_json::to_value(&presented).expect("serializable");

        let payload =
            provided_token_payload(&RsEvseManager::core::hlc::ProvidedToken::PlugAndCharge {
                token: RsEvseManager::core::hlc::OpaqueToken::new(carried),
                connectors: vec![1],
            })
            .expect("the payload is this boundary's own serialization");

        assert_eq!(
            payload.connectors,
            Some(vec![1]),
            "the connector list is the one field replaced"
        );
        assert_eq!(
            payload,
            types::authorization::ProvidedIdToken {
                connectors: Some(vec![1]),
                ..presented
            },
            "nothing else may change, the contract certificate least of all"
        );
    }

    /// The session setup call has two adjacent booleans in it, and the three
    /// orders in play disagree: the C++ call reads `(payment_options,
    /// supported_certificate_service, central_contract_validation_allowed)`,
    /// the interface declares that same order, and the generated Rust takes
    /// them alphabetically, `(central_contract_validation_allowed,
    /// payment_options, supported_certificate_service)`. A positional port
    /// swaps the two booleans and compiles.
    ///
    /// Asserted against the source for the same reason the stop request is:
    /// the arm calls a method on a generated publisher.
    #[test]
    fn the_session_setup_arm_names_each_argument_it_passes() {
        const SOURCE: &str = include_str!("main.rs");

        let at = SOURCE
            .find("HlcUpdate::SessionSetup(setup)")
            .expect("main.rs has no `HlcUpdate::SessionSetup` arm");
        let arm = &SOURCE[at..at + 600];

        for named in [
            "setup.central_contract_validation_allowed",
            "setup.supported_certificate_service",
        ] {
            assert!(
                arm.contains(named),
                "the arm must pass `{named}` by name, got `{arm}`"
            );
        }

        // `fake_dc_enabled` sits between two booleans, all three are read off
        // the same struct, and the generated signature takes them in an order
        // that is neither the interface's nor the C++ call's. So a
        // transposition compiles and reads plausibly. Pin its position rather
        // than its presence: it must be the argument immediately after
        // `central_contract_validation_allowed`.
        let compact: String = arm.chars().filter(|c| !c.is_whitespace()).collect();
        assert!(
            compact.contains("setup.central_contract_validation_allowed,setup.fake_dc,"),
            "`fake_dc_enabled` must be the second argument, got `{arm}`"
        );
    }

    /// A command handler that does nothing compiles and reads plausibly, and
    /// this one has nothing to answer with, so no caller can tell. The
    /// mutation that deletes the `send` and leaves `Ok(())` passed every other
    /// test in this file.
    ///
    /// Asserted against the source because the handler takes a `&Context`,
    /// which borrows a `ModulePublisher` that needs a live framework runtime.
    #[test]
    fn the_plug_and_charge_handler_posts_the_command_it_was_given() {
        const SOURCE: &str = include_str!("main.rs");

        let at = SOURCE
            .find("plug_and_charge_configuration: types::evse_manager::PlugAndChargeConfiguration,")
            .expect("main.rs has no `set_plug_and_charge_configuration` handler");
        let body = &SOURCE[at..at + 300];

        assert!(
            body.contains("Command::SetPlugAndChargeConfiguration"),
            "the handler must post the command, got `{body}`"
        );
        assert!(
            body.contains("plug_and_charge_from(&plug_and_charge_configuration)"),
            "the handler must post what it was given, got `{body}`"
        );
    }

    /// `set_der_available` answers its caller synchronously, so the whole
    /// decision is in this file: refuse when high level communication is not
    /// enabled, otherwise post the declaration and accept.
    ///
    /// Asserted against the source for the same reason as the handler above:
    /// the handler takes a `&Context`, which borrows a `ModulePublisher` that
    /// needs a live framework runtime. The three mutations this catches are a
    /// handler that accepts without posting, one that posts the wrong value,
    /// and one that answers `Accepted` on a port with no stack.
    #[test]
    fn the_der_availability_handler_refuses_without_a_stack_and_posts_otherwise() {
        const SOURCE: &str = include_str!("main.rs");

        let body = dropped_hlc_facts::body_of("set_der_available");

        assert!(
            body.contains("SetDerAvailableResult::NoHlc"),
            "the handler must refuse a port with no stack, got `{body}`"
        );
        // The gate `is_hlc_enabled()` reads is now the existence of the
        // configuration: `HlcConfig::for_deployment` answers `None` for a
        // deployment the flag would have been false on, so the pattern that
        // rejects both the unfilled `OnceLock` and the absent stack is the
        // whole guard. It read a `!config.enabled` field until that field was
        // deleted along with the thirty guards it fed.
        assert!(
            body.contains("let Some(Some(_)) = self.hlc.get()"),
            "the refusal must read the same gate `is_hlc_enabled()` does, got `{body}`"
        );
        assert!(
            body.contains("Command::SetDerAvailable(available)"),
            "the handler must post what it was given, got `{body}`"
        );
        assert!(
            body.contains("SetDerAvailableResult::Accepted"),
            "an enabled port accepts, got `{body}`"
        );
        // The order matters: an accept that precedes the guard answers every
        // caller the same way, which is the defect `update_allowed_energy_transfer_modes`
        // has in the C++.
        let refuse = body.find("NoHlc").expect("the refusal is present");
        let accept = body.find("Accepted").expect("the acceptance is present");
        assert!(refuse < accept, "the guard comes first, got `{body}`");

        // The stub this replaced asked `hlc_wired()`, which is only the
        // requirement being connected and not the C++ gate. It is gone, and no
        // other caller wanted it.
        // Split so the census does not find its own needle: this file is the
        // haystack.
        assert!(
            !SOURCE.contains(concat!("fn hlc_", "wired")),
            "the wrong gate must not come back"
        );
    }

    /// `handle_get_evse` fills both connector fields from configuration
    /// (`evse/evse_managerImpl.cpp:428-432`), so both answer before a session
    /// starts. Asserted against the source for the same reason as the two
    /// handlers above. The mutations this catches are a swapped charge mode,
    /// which reports a DC port as AC to the device model OCPP builds from it,
    /// and an `hlc_capable` written as a literal rather than read from the gate.
    #[test]
    fn the_connector_reports_its_topology_and_its_stack() {
        let body = dropped_hlc_facts::body_of("get_evse");

        assert!(
            body.contains("ChargeMode::Ac => types::evse_manager::ChargeMode::AC"),
            "an AC deployment must report AC, got `{body}`"
        );
        assert!(
            body.contains("ChargeMode::Dc => types::evse_manager::ChargeMode::DC"),
            "a DC deployment must report DC, got `{body}`"
        );
        // The same gate `set_der_available` reads, not the requirement being
        // wired: a connector whose stack is configured off reports false, as
        // `is_hlc_enabled()` does.
        assert!(
            body.contains("hlc_capable: matches!(self.hlc.get(), Some(Some(_)))"),
            "the stack answer must read the gate, got `{body}`"
        );
    }

    #[test]
    fn each_payment_option_carries_its_own_spelling() {
        assert_eq!(
            payment_option_enum(PaymentOption::Contract),
            types::iso15118::PaymentOption::Contract
        );
        assert_eq!(
            payment_option_enum(PaymentOption::ExternalPayment),
            types::iso15118::PaymentOption::ExternalPayment
        );
    }

    /// The wire type carries three optional booleans in alphabetical order,
    /// and two of them mean opposite halves of the same feature. Each has to
    /// reach its own field.
    #[test]
    fn each_plug_and_charge_field_reaches_its_own() {
        let wire = |central, certificate, pnc| {
            plug_and_charge_from(&types::evse_manager::PlugAndChargeConfiguration {
                central_contract_validation_allowed: central,
                contract_certificate_installation_enabled: certificate,
                pnc_enabled: pnc,
            })
        };

        assert_eq!(
            wire(None, None, None),
            PlugAndChargeConfiguration::default()
        );
        assert_eq!(
            wire(Some(true), None, None),
            PlugAndChargeConfiguration {
                central_validation_allowed: Some(true),
                ..PlugAndChargeConfiguration::default()
            }
        );
        assert_eq!(
            wire(None, Some(true), None),
            PlugAndChargeConfiguration {
                certificate_installation_enabled: Some(true),
                ..PlugAndChargeConfiguration::default()
            }
        );
        assert_eq!(
            wire(None, None, Some(true)),
            PlugAndChargeConfiguration {
                enabled: Some(true),
                ..PlugAndChargeConfiguration::default()
            }
        );
    }

    /// A `false` on the wire has to survive as `Some(false)` and not collapse
    /// into "absent", because absent means "leave this one alone".
    #[test]
    fn a_false_on_the_wire_is_not_the_same_as_an_absent_field() {
        assert_eq!(
            plug_and_charge_from(&types::evse_manager::PlugAndChargeConfiguration {
                central_contract_validation_allowed: Some(false),
                contract_certificate_installation_enabled: Some(false),
                pnc_enabled: Some(false),
            }),
            PlugAndChargeConfiguration {
                enabled: Some(false),
                central_validation_allowed: Some(false),
                certificate_installation_enabled: Some(false),
            }
        );
    }

    /// The four `uk_random_delay` command handlers, and which core command
    /// each one must post.
    ///
    /// Same problem and same answer as `ported_hlc_facts` below: a subscriber
    /// callback takes a `&Context` that borrows a `ModulePublisher` needing a
    /// live framework runtime, so no test can call one. Compiling proves
    /// nothing here, because all four bodies differ only in which variant they
    /// name and every variant is in scope: `set_duration_s` posting
    /// `RandomDelayEnable` compiles, reads plausibly and leaves all four gates
    /// green. The mutation sweep found exactly that, so the routing is
    /// asserted against the source.
    mod random_delay_commands {
        /// `(handler, the command expression its body must contain)`.
        const ROUTED: &[(&str, &str)] = &[
            ("enable", "Event::Command(Command::RandomDelayEnable)"),
            ("disable", "Event::Command(Command::RandomDelayDisable)"),
            ("cancel", "Event::Command(Command::RandomDelayCancel)"),
            (
                "set_duration_s",
                "Event::Command(Command::RandomDelaySetDuration(value))",
            ),
        ];

        fn without_whitespace(text: &str) -> String {
            text.chars().filter(|c| !c.is_whitespace()).collect()
        }

        #[test]
        fn each_command_handler_posts_the_command_the_table_names() {
            for (handler, expected) in ROUTED {
                let body = without_whitespace(super::dropped_hlc_facts::body_of(handler));
                assert!(
                    body.contains(&without_whitespace(expected)),
                    "{handler} does not post {expected}"
                );
            }
        }

        /// A handler that posts two commands, or one that posts a command
        /// another handler owns, is the other half of the same mistake: the
        /// table above only says a body contains the right expression.
        #[test]
        fn no_command_handler_posts_a_command_another_one_owns() {
            for (handler, _) in ROUTED {
                let body = without_whitespace(super::dropped_hlc_facts::body_of(handler));
                let posted: Vec<&str> = ROUTED
                    .iter()
                    .filter(|(_, command)| body.contains(&without_whitespace(command)))
                    .map(|(name, _)| *name)
                    .collect();
                assert_eq!(posted, vec![*handler], "{handler} posts the wrong set");
            }
        }
    }

    /// The five commands whose interface returns a decision only the core can
    /// make, and which therefore have to wait for it.
    ///
    /// The defect these pin: intake used to answer at once, so `reserve`
    /// reported success for reservations the core had refused and
    /// `enable_disable` reported the requested state rather than the arbitrated
    /// one. Answering at intake still compiles and still publishes a plausible
    /// `true`; it is wrong only against a core the caller never consulted,
    /// which is what makes it worth a table rather than a comment.
    ///
    /// `force_unlock` is deliberately not here. The C++ answers it from whether
    /// a connector lock is wired (`evse/evse_managerImpl.cpp:491-500`), which
    /// is a boundary fact, and the boundary answers it; see the module below.
    mod awaited_commands {
        /// `(handler, the awaiting expression its body must contain)`.
        const AWAITED: &[(&str, &str)] = &[
            ("pause_charging", "self.send_awaiting(Command::PauseCharging)"),
            (
                "resume_charging",
                "self.send_awaiting(Command::ResumeCharging)",
            ),
            (
                "stop_transaction",
                "self.send_awaiting(Command::StopTransaction {",
            ),
            (
                "reserve",
                "self.send_awaiting(Command::Reserve { reservation_id })",
            ),
            (
                "enable_disable",
                "self.send_awaiting(Command::EnableDisable {",
            ),
        ];

        fn without_whitespace(text: &str) -> String {
            text.chars().filter(|c| !c.is_whitespace()).collect()
        }

        #[test]
        fn each_handler_waits_for_the_command_the_table_names() {
            for (handler, expected) in AWAITED {
                let body = without_whitespace(super::dropped_hlc_facts::body_of(handler));
                assert!(
                    body.contains(&without_whitespace(expected)),
                    "{handler} does not wait for {expected}"
                );
            }
        }

        /// None of them answers a constant.
        ///
        /// The check above is satisfied by a body that waits for the verdict
        /// and then ignores it, which is the defect wearing the fix's clothes.
        /// `Ok(true)` was the literal text of all but one of these bodies
        /// before, so it is the exact thing that must not come back.
        #[test]
        fn no_handler_answers_a_constant() {
            for (handler, _) in AWAITED {
                let body = without_whitespace(super::dropped_hlc_facts::body_of(handler));
                assert!(
                    !body.contains("Ok(true)") && !body.contains("Ok(false)"),
                    "{handler} answers a constant rather than the core"
                );
            }
        }

        /// And none of them waits for a command another one owns.
        #[test]
        fn no_handler_waits_for_a_command_another_one_owns() {
            for (handler, _) in AWAITED {
                let body = without_whitespace(super::dropped_hlc_facts::body_of(handler));
                let awaited: Vec<&str> = AWAITED
                    .iter()
                    .filter(|(_, command)| body.contains(&without_whitespace(command)))
                    .map(|(name, _)| *name)
                    .collect();
                assert_eq!(awaited, vec![*handler], "{handler} waits for the wrong set");
            }
        }
    }

    /// `force_unlock` answers whether there was a lock to open.
    ///
    /// `evse_managerImpl::handle_force_unlock` does nothing and returns `false`
    /// when `r_connector_lock` is empty, and OCPP 1.6 renders that `false` as
    /// `UnlockStatus::NotSupported`. This port answered `true` unconditionally,
    /// so a fixed cable deployment reported an unlock no hardware performed;
    /// `ocpp16` `test_unlock_connector_no_charging_fixed_cable` caught it when
    /// the module was substituted into that suite.
    ///
    /// Pinned against the source for the reason `dropped_hlc_facts` gives: the
    /// handler takes a `&Context` borrowing a `ModulePublisher`, so no test can
    /// call it, and `Intake` needs a whole `Settings` to construct.
    mod force_unlock_reports_the_lock {
        fn without_whitespace(text: &str) -> String {
            text.chars().filter(|c| !c.is_whitespace()).collect()
        }

        /// The slot count has to reach `Wiring`, or the guard below reads a
        /// field nothing fills and declines on every deployment.
        #[test]
        fn the_connector_lock_slot_count_reaches_the_wiring() {
            let body = without_whitespace(super::dropped_hlc_facts::body_of("wiring_of"));
            assert!(
                body.contains(&without_whitespace(
                    "connector_lock: !publishers.connector_lock_slots.is_empty(),"
                )),
                "wiring_of does not fill connector_lock from the slot count: {body}"
            );
        }

        /// And the handler has to consult it. Both halves are needed: a filled
        /// field nobody reads and a guard over a field nobody fills each leave
        /// the original answer in place.
        #[test]
        fn the_handler_declines_when_no_lock_is_wired() {
            let body = without_whitespace(super::dropped_hlc_facts::body_of("force_unlock"));
            assert!(
                body.contains(&without_whitespace(
                    "if !self.connector_lock_wired() { return Ok(false); }"
                )),
                "force_unlock does not decline without a lock: {body}"
            );
        }

        /// The command must not be posted on the declining path either: the
        /// C++ cancels no transaction and touches no board support when there
        /// is no lock, so an unlock that reaches the core is a second defect
        /// wearing the first one's fix.
        #[test]
        fn the_declining_path_posts_no_command() {
            let body = super::dropped_hlc_facts::body_of("force_unlock");
            let guard = body
                .find("return Ok(false);")
                .expect("force_unlock has no declining path");
            assert!(
                !body[..guard].contains("Event::Command("),
                "force_unlock posts a command before it declines: {body}"
            );
        }
    }

    /// The inverse of `dropped_hlc_facts`: the facts that **are** ported, and
    /// which core event each one becomes.
    ///
    /// A subscriber callback takes a `&Context`, which borrows a
    /// `ModulePublisher` that needs a live framework runtime, so no test can
    /// call one. The reachability audit proves each event has a producer; it
    /// cannot prove the producer builds the right variant, and a boolean wired
    /// the wrong way round compiles and reads plausibly. So the table is
    /// asserted against the source, the same way the dropped ledger below is.
    ///
    /// Porting a fact means adding its row here and deleting its row there.
    mod ported_hlc_facts {
        const SOURCE: &str = include_str!("main.rs");

        /// `(callback, the event expression its body must contain)`.
        const PORTED: &[(&str, &str)] = &[
            (
                "on_ac_close_contactor",
                "Event::Hlc(HlcEvent::AllowCloseContactor(true))",
            ),
            (
                "on_ac_open_contactor",
                "Event::Hlc(HlcEvent::AllowCloseContactor(false))",
            ),
            (
                "on_dc_open_contactor",
                "Event::Hlc(HlcEvent::OpenContactorDc)",
            ),
            ("on_dlink_error", "Event::Hlc(HlcEvent::DataLinkError)"),
            ("on_dlink_pause", "Event::Hlc(HlcEvent::DataLinkPause)"),
            (
                "on_dlink_terminate",
                "Event::Hlc(HlcEvent::DataLinkTerminate)",
            ),
            (
                "on_v_2_g_setup_finished",
                "Event::Hlc(HlcEvent::SetupFinished)",
            ),
            // The one fact that is not an `HlcEvent`: it reaches no state
            // machine, only the session transcript. See
            // `core::event::Event::V2gMessage`.
            (
                "on_v_2_g_messages",
                "Event::V2gMessage(Box::new(V2gMessage { id: v2g_message_id(&value.id), xml: value.xml.unwrap_or_default(), json: value.v_2_g_json.unwrap_or_default(), exi_hex: value.exi.unwrap_or_default(), exi_base64: value.exi_base_64.unwrap_or_default(), }))",
            ),
            (
                "on_dc_ev_target_voltage_current",
                "Event::Hlc(HlcEvent::DcEvTarget { voltage_v: value.dc_ev_target_voltage, current_a: value.dc_ev_target_current, })",
            ),
            (
                "on_d_20_dc_dynamic_charge_mode",
                "Event::Hlc(HlcEvent::DcDynamicChargeMode( DynamicModeRequest { max_charge_power_w: value.max_charge_power, min_charge_power_w: value.min_charge_power, max_charge_current_a: value.max_charge_current, max_voltage_v: value.max_voltage, min_voltage_v: value.min_voltage, max_discharge_power_w: value.max_discharge_power, min_discharge_power_w: value.min_discharge_power, max_discharge_current_a: value.max_discharge_current, }, ))",
            ),
            (
                "on_dc_ev_maximum_limits",
                "Event::Hlc(HlcEvent::DcEvMaximumLimits(EvMaximumLimits { maximum_current_a: value.dc_ev_maximum_current_limit, maximum_voltage_v: value.dc_ev_maximum_voltage_limit, }))",
            ),
            (
                "on_dc_ev_status",
                "Event::Hlc(HlcEvent::StateOfCharge { percent: value.dc_ev_ress_soc, })",
            ),
            (
                "on_hlc_session_failed",
                "HlcEvent::SessionFailed(hlc_session_failure_from(&value))",
            ),
            (
                "on_state",
                "HlcEvent::MatchingStarted(matching_started_from(&value))",
            ),
            // The same callback's second fact. Two rows for one body, which is
            // what the table's containment check allows and what says both
            // events are still produced.
            (
                "on_state",
                "Event::Hlc(HlcEvent::SlacMatched(slac_matched_from(&value)))",
            ),
            (
                "on_request_error_routine",
                "Event::Hlc(HlcEvent::SlacErrorRoutine)",
            ),
            (
                "on_dlink_ready",
                "Event::Hlc(HlcEvent::DataLinkReady(value))",
            ),
            (
                "on_ev_mac_address",
                "Event::Hlc(HlcEvent::VehicleMacAddress(value))",
            ),
            (
                "on_selected_protocol",
                "Event::Hlc(HlcEvent::SelectedProtocol(value))",
            ),
            (
                "on_require_auth_eim",
                "Event::Hlc(HlcEvent::RequireAuthEim)",
            ),
            (
                "on_require_auth_pnc",
                "Event::Hlc(HlcEvent::RequireAuthPlugAndCharge { token: OpaqueToken::new(payload), })",
            ),
            (
                "on_start_cable_check",
                "Event::Hlc(HlcEvent::RequiresCableCheck)",
            ),
            (
                "on_start_pre_charge",
                "Event::Hlc(HlcEvent::PreChargeStarted)",
            ),
            (
                "on_selected_service_parameters",
                "Event::Hlc(HlcEvent::SelectedService( selected_service_from(&value.energy_transfer), ))",
            ),
            (
                "on_sae_bidi_mode_active",
                "Event::Hlc(HlcEvent::SaeBidiModeActive)",
            ),
        ];

        /// Whitespace and the formatter's trailing commas removed, because the
        /// expression is reflowed and the fact being pinned is which variant is
        /// built, not how the line wrapped.
        fn without_whitespace(text: &str) -> String {
            let dense: String = text.chars().filter(|c| !c.is_whitespace()).collect();
            dense.replace(",)", ")")
        }

        #[test]
        fn every_ported_fact_becomes_the_event_the_table_names() {
            for (callback, expected) in PORTED {
                let body = without_whitespace(super::dropped_hlc_facts::body_of(callback));
                assert!(
                    body.contains(&without_whitespace(expected)),
                    "`{callback}` must build `{expected}`, body is `{body}`"
                );
            }
        }

        /// A callback cannot be in both tables: one drops the fact and the
        /// other carries it.
        #[test]
        fn no_callback_is_both_ported_and_dropped() {
            for (callback, _) in PORTED {
                assert!(
                    !super::dropped_hlc_facts::unported_callbacks().contains(callback),
                    "`{callback}` is in both tables"
                );
            }
        }

        /// Every `HlcEvent` variant the module produces at the boundary is
        /// named by the table, so a tenth producer cannot appear unpinned.
        #[test]
        fn the_table_accounts_for_every_boundary_producer() {
            // Production source only. `PORTED` above quotes the same prefix
            // with a real variant name after it, so a census over the whole
            // file counts the table's own rows and satisfies itself: deleting
            // a producer would leave the census unchanged.
            let production = SOURCE
                .split_once("\n#[cfg(test)]\nmod tests {")
                .expect("the test module marker moved")
                .0;
            let mut named: Vec<&str> = production
                .match_indices("Event::Hlc(HlcEvent::")
                .filter_map(|(at, _)| {
                    let from = at + "Event::Hlc(HlcEvent::".len();
                    let rest = &production[from..];
                    let end = rest.find(|c: char| !c.is_alphanumeric())?;
                    (end > 0).then(|| &rest[..end])
                })
                .collect();
            named.sort_unstable();
            named.dedup();

            assert_eq!(
                named,
                vec![
                    "AllowCloseContactor",
                    "CurrentDemandFinished",
                    "CurrentDemandStarted",
                    "DataLinkError",
                    "DataLinkPause",
                    "DataLinkReady",
                    "DataLinkTerminate",
                    "DcDynamicChargeMode",
                    "DcEvMaximumLimits",
                    "DcEvTarget",
                    "MatchingStarted",
                    "ModeSelected",
                    "OpenContactorDc",
                    "PreChargeStarted",
                    "RequireAuthEim",
                    "RequireAuthPlugAndCharge",
                    "RequiresCableCheck",
                    "SaeBidiModeActive",
                    "SelectedProtocol",
                    "SelectedService",
                    "SessionFailed",
                    "SessionSetup",
                    "SetupFinished",
                    "SlacErrorRoutine",
                    "SlacMatched",
                    "StateOfCharge",
                    "StopFromEv",
                    "VehicleMacAddress",
                ],
                "a boundary producer appeared or vanished"
            );
        }
    }

    /// Pins the classification of the unported `on_*` arms.
    ///
    /// The arms are a parity ledger, and an empty one is indistinguishable
    /// from a deliberate no-op to the compiler, to a reviewer and to every
    /// other test in this crate. So the ledger is asserted against the source
    /// itself: an arm whose interface variable has a live `subscribe_<name>`
    /// handler in `modules/EVSE/EvseManager/EvseManager.cpp` carries a drop
    /// line at the level its arrival rate warrants, an arm whose counterpart
    /// does no work carries nothing, and no arm escapes the ledger.
    ///
    /// Porting one of these means deleting its row here in the same change.
    mod dropped_hlc_facts {
        const SOURCE: &str = include_str!("main.rs");

        const DROP_PREFIX: &str = "unported HLC fact dropped: ";

        /// `(callback, interface variable, level)`. Every entry has a live
        /// `subscribe_<variable>` in `EvseManager.cpp` whose body does work.
        /// `debug` marks a fact carried in the charge loop, which arrives
        /// roughly every 250 ms to 1 s; `warn` marks one arriving once or a
        /// few times per session.
        const WITH_COUNTERPART: &[(&str, &str, &str)] = &[
            ("on_ac_eamount", "ac_eamount", "warn"),
            (
                "on_ac_ev_dynamic_control_mode",
                "ac_ev_dynamic_control_mode",
                "debug",
            ),
            ("on_ac_ev_max_current", "ac_ev_max_current", "warn"),
            ("on_ac_ev_max_voltage", "ac_ev_max_voltage", "warn"),
            ("on_ac_ev_min_current", "ac_ev_min_current", "warn"),
            ("on_ac_ev_power_limits", "ac_ev_power_limits", "warn"),
            ("on_ac_ev_present_powers", "ac_ev_present_powers", "debug"),
            ("on_dc_bulk_soc", "dc_bulk_soc", "warn"),
            ("on_dc_ev_energy_capacity", "dc_ev_energy_capacity", "warn"),
            ("on_dc_ev_energy_request", "dc_ev_energy_request", "debug"),
            ("on_dc_ev_remaining_time", "dc_ev_remaining_time", "debug"),
            ("on_dc_full_soc", "dc_full_soc", "warn"),
            ("on_departure_time", "departure_time", "warn"),
        ];

        /// No `subscribe_<variable>` anywhere in `modules/EVSE/EvseManager/`.
        /// The first three are named in that file's own "unused vars of HLC
        /// for now" list; the rest appear nowhere in the module at all. Not
        /// parity gaps, so nothing to report.
        const WITHOUT_COUNTERPART: &[&str] = &[
            "on_selected_payment_option",
            "on_dc_bulk_charging_complete",
            "on_dc_charging_complete",
            "on_dc_ev_present_voltage",
            "on_display_parameters",
            "on_ev_app_protocol",
            "on_meter_info_requested",
            "on_supported_app_protocols_secc",
        ];

        /// The body of `fn <name>`, brace matched, trimmed.
        pub(super) fn body_of(name: &str) -> &'static str {
            let signature = format!("fn {name}(");
            let start = SOURCE
                .find(&signature)
                .unwrap_or_else(|| panic!("main.rs has no `{signature}`"));
            let open = start
                + SOURCE[start..]
                    .find('{')
                    .unwrap_or_else(|| panic!("`{signature}` has no body"));
            let mut depth = 0usize;
            for (offset, character) in SOURCE[open..].char_indices() {
                match character {
                    '{' => depth += 1,
                    '}' => {
                        depth -= 1;
                        if depth == 0 {
                            return SOURCE[open + 1..open + offset].trim();
                        }
                    }
                    _ => {}
                }
            }
            panic!("unterminated body for `{signature}`");
        }

        /// Every `on_*` method declared at method indentation whose body is
        /// empty or is nothing but a drop line.
        pub(super) fn unported_callbacks() -> Vec<&'static str> {
            SOURCE
                .match_indices("\n    fn on_")
                .filter_map(|(at, _)| {
                    let from = at + "\n    fn ".len();
                    let name = &SOURCE[from..from + SOURCE[from..].find('(')?];
                    let body = body_of(name);
                    (body.is_empty() || body.contains(DROP_PREFIX)).then_some(name)
                })
                .collect()
        }

        #[test]
        fn a_dropped_fact_the_cpp_acts_on_names_itself_at_its_arrival_rate() {
            for (callback, variable, level) in WITH_COUNTERPART {
                assert_eq!(
                    body_of(callback),
                    format!(r#"log::{level}!("{DROP_PREFIX}{variable}");"#),
                    "`{callback}` drops a fact `EvseManager.cpp` acts on"
                );
            }
        }

        #[test]
        fn a_callback_with_no_counterpart_stays_silent() {
            for callback in WITHOUT_COUNTERPART {
                assert_eq!(
                    body_of(callback),
                    "",
                    "`{callback}` is not a parity gap and needs no line"
                );
            }
        }

        #[test]
        fn the_ledger_accounts_for_every_unported_callback() {
            let mut ledger: Vec<&str> = WITH_COUNTERPART
                .iter()
                .map(|(callback, _, _)| *callback)
                .chain(WITHOUT_COUNTERPART.iter().copied())
                .collect();
            ledger.sort_unstable();

            let mut found = unported_callbacks();
            found.sort_unstable();

            assert_eq!(
                found, ledger,
                "an unported callback was added or ported without updating the ledger"
            );
        }
    }

    // The doc comment for `mod dispatched_hlc_updates` used to sit here. That
    // module is further down the file and `mod session_transcript` was inserted
    // between the two, which reassigned thirty six lines about outbound HLC
    // dispatch to a module about filesystem I/O. The comment has been moved
    // back onto the module it describes rather than the modules reordered.
    /// The transcript's only I/O, exercised against a real filesystem.
    ///
    /// `core::session_log` is 30 tests deep on what a record should contain and
    /// cannot open a file. These tests are the other half: that the four
    /// actions it asks for do what it assumed, and that the failures it is
    /// designed to survive are survivable.
    mod session_transcript {
        use super::*;
        use RsEvseManager::core::config::LoggingSettings;
        use RsEvseManager::core::session_log::{Action, Payload, SessionLogger};

        /// A private directory for one test, removed on the way out.
        struct Scratch(std::path::PathBuf);

        impl Scratch {
            fn new(name: &str) -> Self {
                let path = std::env::temp_dir()
                    .join(format!("rs-evse-transcript-{name}-{}", std::process::id()));
                let _ = std::fs::remove_dir_all(&path);
                Self(path)
            }

            fn root(&self) -> &str {
                self.0.to_str().expect("a utf8 scratch path")
            }
        }

        impl Drop for Scratch {
            fn drop(&mut self) {
                let _ = std::fs::remove_dir_all(&self.0);
            }
        }

        /// A logger over a scratch root. There is no disabled logger to build:
        /// `SessionLogger::for_settings` answers `None` for a deployment with
        /// `session_logging` off, so `enable()` is gone with the field it set.
        fn logger_at(root: &str) -> SessionLogger {
            SessionLogger::for_settings(&LoggingSettings {
                session_logging: true,
                session_logging_path: root.to_string(),
                session_logging_xml: true,
                logfile_suffix: String::new(),
                dbg_hlc_auth_after_tstep: false,
            })
            .expect("logging is on")
        }

        fn perform_all(actions: &[Action]) -> Vec<String> {
            actions
                .iter()
                .filter_map(|action| perform_log_action(action).err())
                .map(|error| error.to_string())
                .collect()
        }

        #[test]
        fn a_session_writes_both_transcripts_and_renames_them_on_stop() {
            let scratch = Scratch::new("roundtrip");
            let mut logger = logger_at(scratch.root());

            let start = logger.start_session("2026-09-07T10:00:00.000Z", "sess-1");
            assert!(
                perform_all(&start.actions).is_empty(),
                "a writable root opens cleanly"
            );
            let dir = logger
                .session_dir()
                .expect("a started session names its directory")
                .to_string();

            let record = logger.evse(
                "2026-09-07T10:00:01.000Z",
                false,
                "Set PWM Off",
                Payload::default(),
            );
            assert!(perform_all(&record.actions).is_empty());

            // While the session runs, only the incomplete names exist. That is
            // what tells a reader the file is still being written.
            assert!(std::path::Path::new(&format!("{dir}/incomplete-eventlog.csv")).exists());
            assert!(!std::path::Path::new(&format!("{dir}/eventlog.csv")).exists());

            let stop = logger.stop_session("2026-09-07T10:00:02.000Z");
            assert!(perform_all(&stop.actions).is_empty());

            let csv = std::fs::read_to_string(format!("{dir}/eventlog.csv"))
                .expect("the finished csv is renamed into place");
            let html = std::fs::read_to_string(format!("{dir}/eventlog.html"))
                .expect("the finished html is renamed into place");
            assert!(!std::path::Path::new(&format!("{dir}/incomplete-eventlog.csv")).exists());

            // The bracket the C++ writes, and the record between it.
            assert!(csv.contains("\"Session logging started.\""), "{csv}");
            assert!(csv.contains("\"Set PWM Off\""), "{csv}");
            assert!(csv.contains("\"Session logging stopped.\""), "{csv}");
            assert!(html.starts_with("<html><head><title>"), "{html}");
            assert!(
                html.trim_end().ends_with("</table></body></html>"),
                "{html}"
            );
        }

        #[test]
        fn a_root_that_cannot_be_created_reports_and_stops_logging_without_propagating() {
            // The path the brief names: logging asked for, and the root not
            // creatable. Here the root's parent is a regular file, so
            // `create_dir_all` fails the way an unwritable mount does.
            let scratch = Scratch::new("blocked");
            std::fs::create_dir_all(&scratch.0).expect("a scratch dir");
            let blocker = scratch.0.join("file");
            std::fs::write(&blocker, b"not a directory").expect("a blocking file");
            let root = blocker.join("logs");

            let mut logger = logger_at(root.to_str().expect("utf8"));
            let start = logger.start_session("2026-09-07T10:00:00.000Z", "sess-1");

            let mut reported = 0;
            for action in &start.actions {
                if let Err(error) = perform_log_action(action) {
                    reported += 1;
                    logger.note_failure(action, &error.to_string());
                }
            }
            assert!(reported > 0, "an uncreatable root has to fail loudly");
            assert!(
                !logger.is_session_active(),
                "a transcript that cannot open leaves no session behind"
            );
            // And the next session tries again, so a transient fault is not a
            // permanent loss of logging. There is no `enabled` field left for
            // `note_failure` to clear, so a second start asks for the work
            // over again.
            assert!(!logger.start_session("2026-09-07T10:05:00.000Z", "sess-2")
                .actions
                .is_empty());
        }

        #[test]
        fn a_record_written_after_a_failed_open_still_produces_no_error() {
            let scratch = Scratch::new("degraded");
            let mut logger = logger_at(scratch.root());
            let start = logger.start_session("2026-09-07T10:00:00.000Z", "sess-1");
            // Never performed, so the directory does not exist. The logger is
            // told, which is the only thing that keeps it consistent with the
            // filesystem.
            for action in &start.actions {
                logger.note_failure(action, "simulated");
            }

            let record = logger.evse(
                "2026-09-07T10:00:01.000Z",
                false,
                "Set PWM F",
                Payload::default(),
            );

            assert!(
                record.actions.is_empty() && record.lines.is_empty(),
                "a degraded transcript asks for nothing, got {record:?}"
            );
        }

        #[test]
        fn an_append_to_a_missing_file_is_reported_and_the_session_survives() {
            // A record lost to a full disk must not end the session's
            // transcript: the C++ logs the write and carries on.
            let scratch = Scratch::new("append");
            let mut logger = logger_at(scratch.root());
            let start = logger.start_session("2026-09-07T10:00:00.000Z", "sess-1");
            perform_all(&start.actions);
            let dir = logger.session_dir().expect("a directory").to_string();
            std::fs::remove_file(format!("{dir}/incomplete-eventlog.csv")).expect("removable");

            let record = logger.evse(
                "2026-09-07T10:00:01.000Z",
                false,
                "Set PWM Off",
                Payload::default(),
            );
            let mut failed = false;
            for action in &record.actions {
                if let Err(error) = perform_log_action(action) {
                    failed = true;
                    logger.note_failure(action, &error.to_string());
                }
            }

            assert!(failed, "the missing csv has to be noticed");
            assert!(
                logger.is_session_active(),
                "a failed record does not end the transcript"
            );
        }

        #[test]
        fn a_suffix_that_escapes_the_root_writes_nothing_at_all() {
            // The guard is lexical and lives in `core`, so nothing reaches the
            // filesystem to be guarded against here. Asserted from this side
            // too, because this is the side that would have created the
            // directory.
            let scratch = Scratch::new("escape");
            let mut logger = logger_at(scratch.root());

            let start =
                logger.start_session("2026-09-07T10:00:00.000Z", "../../../../../../tmp/escaped");

            assert!(
                start.actions.is_empty(),
                "an escaping suffix asks for no filesystem work, got {:?}",
                start.actions
            );
            assert!(!logger.is_session_active());
            assert!(!scratch.0.exists(), "and creates nothing");
        }

        #[test]
        fn an_iso_message_id_is_named_by_its_wire_spelling() {
            // The direction of a record is decided from this string, so a
            // wrong name would put a vehicle message in the EVSE column.
            assert_eq!(
                v2g_message_id(&types::iso15118::V2gMessageId::SessionSetupReq),
                "SessionSetupReq"
            );
            assert_eq!(
                v2g_message_id(&types::iso15118::V2gMessageId::CurrentDemandRes),
                "CurrentDemandRes"
            );
        }

        /// The direction rule's default, stated rather than left implicit.
        ///
        /// `from_vehicle` asks whether the name contains `Res`, so an id that
        /// contains neither `Req` nor `Res` is filed on the car side. Every
        /// value the interface declares ends in one or the other
        /// (`types/iso15118.yaml:73-140`), so this is unreachable from the
        /// wire; it is pinned because it is what an unnamed id degrades to, and
        /// `v2g_message_id` degrades to the empty string.
        #[test]
        fn an_id_declaring_no_direction_is_filed_on_the_car_side() {
            use RsEvseManager::core::event::V2gMessage;
            assert!(V2gMessage::default().from_vehicle());
            assert!(V2gMessage {
                id: "SessionSetupReq".to_string(),
                ..Default::default()
            }
            .from_vehicle());
            assert!(!V2gMessage {
                id: "SessionSetupRes".to_string(),
                ..Default::default()
            }
            .from_vehicle());
        }

        #[test]
        fn opening_a_transcript_discards_whatever_was_there_before() {
            // `Truncate` has to truncate. Implemented as an append-or-create it
            // passed every test here, because each test starts from an empty
            // scratch directory where append and truncate are the same thing.
            //
            // The reachable collision is a restart, which is what the
            // `incomplete-` naming exists to mark in the first place: a port
            // killed mid-session leaves `incomplete-eventlog.csv` behind, never
            // renamed, and a fresh process with a configured `logfile_suffix`
            // -- the operator's own literal rather than the session uuid --
            // derives the same directory for the next session inside the same
            // timestamp resolution. The C++ opens with a plain
            // `ofstream::open` (`SessionLog.cpp:106-107`), which truncates.
            // Left as an append, the new session's records are welded onto the
            // dead session's transcript and the new header lands in the middle
            // of the file, so the HTML no longer parses as one document and the
            // CSV carries two sessions under one name.
            let scratch = Scratch::new("stale");
            const TS: &str = "2026-09-07T10:00:00.000Z";
            const SUFFIX: &str = "fixed-suffix";

            let mut killed = logger_at(scratch.root());
            let first = killed.start_session(TS, SUFFIX);
            assert!(perform_all(&first.actions).is_empty());
            let dir = killed.session_dir().expect("a directory").to_string();
            let record = killed.evse(
                TS,
                false,
                "STALE FROM THE SESSION THAT DIED",
                Payload::default(),
            );
            assert!(perform_all(&record.actions).is_empty());
            // Dropped without a stop, so nothing is renamed: exactly what a
            // kill leaves behind.
            drop(killed);

            let mut restarted = logger_at(scratch.root());
            let second = restarted.start_session(TS, SUFFIX);
            assert_eq!(
                restarted.session_dir(),
                Some(dir.as_str()),
                "the collision this test is about"
            );
            assert!(perform_all(&second.actions).is_empty());

            let csv = std::fs::read_to_string(format!("{dir}/incomplete-eventlog.csv"))
                .expect("a reopened csv");
            let html = std::fs::read_to_string(format!("{dir}/incomplete-eventlog.html"))
                .expect("a reopened html");
            assert!(
                !csv.contains("STALE FROM THE SESSION THAT DIED"),
                "a reopened transcript must not carry the dead session's records: {csv}"
            );
            assert!(
                html.starts_with("<html><head><title>"),
                "the header has to be the start of the file, not the middle: {html}"
            );
            assert_eq!(
                html.matches("<html>").count(),
                1,
                "one document per file: {html}"
            );
        }

        /// A partial write is not a lost record, and no test can prove it.
        ///
        /// `perform_log_action`'s `Append` arm uses `write_all` rather than
        /// `write`. Swapping the two survives every test in this crate, because
        /// forcing a short write on a regular file opened for append needs a
        /// full filesystem or a signal arriving mid-syscall, and neither is
        /// reachable from a test that has to pass on any host. `write_all` is
        /// still what the arm must use: a short write there would truncate a
        /// record silently, and the C++ flushes after every record
        /// (`SessionLog.cpp:233`, `:241`) for the same reason -- a transcript
        /// has to survive a kill. Pinned by the source ledger below rather than
        /// by behaviour.
        ///
        /// The same is true of `html_escape(now)` in `core::session_log`: the
        /// boundary formats `now` as RFC 3339, which contains no character that
        /// function rewrites, so escaping it is unobservable and deliberate.
        #[test]
        fn a_short_write_is_not_reachable_from_a_test() {
            // Stated as the property that makes the equivalence hold, so this
            // fails if the timestamp format ever grows a metacharacter.
            let now = EverestEffects::timestamp();
            assert!(
                !now.contains(['&', '<', '>', '"', '\'']),
                "an RFC 3339 timestamp carries no markup: {now}"
            );
        }
    }

    /// The session event payloads: the carry that makes them possible, and a
    /// ledger for the wiring no test can construct.
    ///
    /// This port published `SessionStarted`, `TransactionStarted` and
    /// `TransactionFinished` as bare events until now. Measured against the
    /// real OCPP suite that was one cause behind 240 of 282 regressions, and it
    /// degraded three different ways: legacy OCPP 1.6 reads
    /// `transaction_started.value()` and the module process terminates on
    /// `std::bad_optional_access`, `OCPP201` throws, `OCPPmulti` logs and skips
    /// so the transaction never starts.
    mod session_event_payloads {
        use super::*;

        /// A `ProvidedIdToken` with every field of every nested type filled,
        /// including both optional lists at their maximum shape.
        ///
        /// The point of filling all of them is the carry: `IdTag` holds the
        /// record as its own serialization rather than as a mirror in `core`,
        /// and this is what says the serialization is lossless. A mirror would
        /// have to be widened by hand for each of these, and a field it missed
        /// would reach a consumer as a plausible default.
        fn every_field_filled() -> types::authorization::ProvidedIdToken {
            types::authorization::ProvidedIdToken {
                authorization_type: types::authorization::AuthorizationType::PlugAndCharge,
                certificate: Some("-----BEGIN CERTIFICATE-----".to_owned()),
                connectors: Some(vec![1, 2]),
                id_token: types::authorization::IdToken {
                    additional_info: Some(vec![types::authorization::CustomIdToken {
                        r#type: "custom".to_owned(),
                        value: "extra".to_owned(),
                    }]),
                    r#type: types::authorization::IdTokenType::eMAID,
                    value: "DE-ABC-123".to_owned(),
                },
                iso_15118_certificate_hash_data: Some(vec![
                    types::iso15118::CertificateHashDataInfo {
                        hash_algorithm: types::iso15118::CertificateHashDataInfoAutoGenHashAlgorithm::SHA256,
                        issuer_key_hash: "key".to_owned(),
                        issuer_name_hash: "name".to_owned(),
                        responder_url: "http://ocsp.example".to_owned(),
                        serial_number: "0001".to_owned(),
                    },
                ]),
                parent_id_token: Some(types::authorization::IdToken {
                    additional_info: None,
                    r#type: types::authorization::IdTokenType::Central,
                    value: "GROUP".to_owned(),
                }),
                prevalidated: Some(true),
                request_id: Some(42),
            }
        }

        /// The whole record survives the trip through `core` and back.
        ///
        /// Every field here is read by someone: `OCPP201` and `OCPPmulti`
        /// render `id_token` into the OCPP `idToken`, `request_id` into
        /// `remoteStartId`, `parent_id_token` into `groupIdToken`, and read
        /// `authorization_type` to decide whether the trigger reason is
        /// `RemoteStart`. This is the assertion that no field arrives as a
        /// default a consumer cannot tell from the real thing.
        #[test]
        fn an_id_token_survives_the_carry_field_for_field() {
            let original = every_field_filled();

            let carried = id_tag_from(&original).expect("a token carries");
            let published = provided_id_token(&carried).expect("a carried token publishes");

            assert_eq!(
                ::everestrs::serde_json::to_value(&published).expect("serializable"),
                ::everestrs::serde_json::to_value(&original).expect("serializable"),
                "the carry is not lossless"
            );
        }

        /// The two facts the core decides on are read off the same record the
        /// payload serializes, so they cannot disagree with it.
        #[test]
        fn the_two_decided_facts_agree_with_the_carried_record() {
            let carried = id_tag_from(&every_field_filled()).expect("a token carries");

            assert_eq!(carried.value(), "DE-ABC-123");
            assert!(
                carried.is_plug_and_charge(),
                "a PlugAndCharge authorization is a contract"
            );

            let external = types::authorization::ProvidedIdToken {
                authorization_type: types::authorization::AuthorizationType::RFID,
                ..every_field_filled()
            };
            assert!(
                !id_tag_from(&external)
                    .expect("a token carries")
                    .is_plug_and_charge(),
                "an RFID authorization is not a contract"
            );
        }

        /// Every value of `types::authorization::AuthorizationType`, so the one
        /// that decides the contract answer is driven against all four that do
        /// not rather than against one of them.
        #[test]
        fn only_plug_and_charge_is_a_contract() {
            use types::authorization::AuthorizationType as Wire;
            for (wire, contract) in [
                (Wire::OCPP, false),
                (Wire::RFID, false),
                (Wire::Autocharge, false),
                (Wire::PlugAndCharge, true),
                (Wire::BankCard, false),
            ] {
                let token = types::authorization::ProvidedIdToken {
                    authorization_type: wire.clone(),
                    ..every_field_filled()
                };
                assert_eq!(
                    id_tag_from(&token)
                        .expect("a token carries")
                        .is_plug_and_charge(),
                    contract,
                    "{wire:?}"
                );
            }
        }

        /// The production half of this file only, for the reason
        /// `session_log_wiring::boundary` gives: a search over the whole file
        /// would find this table's own text and satisfy itself.
        fn boundary() -> &'static str {
            include_str!("main.rs")
                .split_once("\n#[cfg(test)]\nmod tests {")
                .expect("main.rs carries its tests at the end")
                .0
        }

        fn without_whitespace(text: &str) -> String {
            text.chars().filter(|c| !c.is_whitespace()).collect()
        }

        /// `(what it pins, the expression the source must contain)`.
        ///
        /// A ledger for the same structural reason `session_log_wiring` has
        /// one: `EverestEffects` holds a `ModulePublisher`, which needs a live
        /// framework runtime, so no test can construct one and the rendering
        /// half of this work is invisible to all four gates. `reachability.py`
        /// does not close it either - it proves an effect has a producer and
        /// says nothing about what the producer does with it. Every row below
        /// is a place where reverting to the `None` this task replaced compiles
        /// and passes everything else.
        const WIRED: &[(&str, &str)] = &[
            (
                "the transaction started payload reaches the wire",
                "transaction_started: self.transaction_started(report),",
            ),
            (
                "the transaction finished payload reaches the wire",
                "transaction_finished: self.transaction_finished(report),",
            ),
            (
                "the session started payload reaches the wire",
                "session_started: self.session_started(report),",
            ),
            // The three meter values. Each is required by its wire type, so a
            // revert here is not an absent field but a zeroed one: OCPP 1.6
            // bills `meterStart` from `energy_Wh_import.total` and would bill
            // every transaction from zero.
            (
                "the transaction start bills from the billing meter",
                "id_tag: provided_id_token(id_tag)?, meter_value: self.meter_value(),",
            ),
            (
                "the transaction finish bills from the billing meter",
                "types::evse_manager::TransactionFinished { meter_value: self.meter_value(),",
            ),
            (
                "the session start reports the billing meter",
                // Named with the field above it, because three more
                // announcements now read the same meter the same way and the
                // reading alone no longer identifies this site.
                "reason: start_session_reason(reason), meter_value: self.meter_value(),",
            ),
            // The three the C++ `signal_simple_event` subscriber stamps and
            // this port published as `None`. Each is required by its wire
            // type, so a revert zeroes a reading rather than omitting it: a
            // consumer that settles the session total off
            // `session_finished.meter_value` reads zero energy for every
            // session, and an OCPP `TransactionEvent(Updated)` built from
            // `charging_state_changed_event.meter_value` reports the charge
            // starting and pausing at zero.
            (
                "the authorization announcements report the billing meter",
                "authorization_event: self.authorization_event(report),",
            ),
            (
                "the charging state announcements report the billing meter",
                "charging_state_changed_event: self.charging_state_changed_event(report),",
            ),
            (
                "the session finish reports the billing meter",
                "session_finished: self.session_finished(report),",
            ),
            // And the reading each of the three renders, so a revert inside a
            // helper is caught as well as a revert at its call site.
            (
                "the authorization announcement's reading is the billing meter's",
                "types::evse_manager::AuthorizationEvent { meter_value: self.meter_value(), }",
            ),
            (
                "the charging state announcement's reading is the billing meter's",
                "types::evse_manager::ChargingStateChangedEvent { meter_value: self.meter_value(), }",
            ),
            (
                "the session finish reading is the billing meter's",
                "types::evse_manager::SessionFinished { meter_value: self.meter_value(), }",
            ),
            // The fourth field, which is not a meter reading: it is the set
            // the core decided. A revert here leaves a consumer unable to tell
            // a pause the operator asked for from a pause for want of energy,
            // which is the whole of what the field is for.
            (
                "the pause reason set reaches the wire",
                "charging_paused_evse: self.charging_paused_evse(report),",
            ),
            // The two per-slot meter records on the energy flow request, and
            // the slot each is filled from. Reverting either leaves an energy
            // manager reading a node that reports no measured usage at all;
            // reverting the slot names swaps the grid side record onto the
            // leaves, which is a plausible looking request describing the
            // wrong side of the meter.
            (
                "the energy usage records reach the wire",
                "energy_usage_root: usage.root.clone(), energy_usage_leaves: usage.leaves.clone(),",
            ),
            (
                "the grid side record is rotated on the way in",
                "POWERMETER_GRID_SIDE => { usage.root = Some(apply_phase_rotation( value.clone(), self.settings.misc.phase_rotation_grid_side, )); }",
            ),
            (
                "the car side record is carried verbatim",
                "POWERMETER_CAR_SIDE => usage.leaves = Some(value.clone()),",
            ),
            (
                "both slots are recorded above the billing gate",
                "self.note_energy_usage(context.name, &value); if !meter_feeds_the_core(context.name, self.car_side_meter_wired()) { return; }",
            ),
            (
                "the set is carried in the order the core decided",
                "reasons: reasons.iter().copied().map(pause_reason_wire).collect(),",
            ),
            // And the record the three of them read, written where the C++
            // writes it: in the billing meter subscription, before anything
            // else is told about the reading.
            (
                "the whole billing meter reading is kept",
                "*self.billing_meter.lock().unwrap_or_else(|poisoned| poisoned.into_inner()) \
                 = Some(value.clone());",
            ),
            // The three signed meter values, and the four sites that carry
            // them. The two payload rows pin the pairing rather than each
            // field alone: `evse/evse_managerImpl.cpp:279-280` puts the
            // opening reading on `start_signed_meter_value` and the closing
            // one on `signed_meter_value`, and swapping them publishes a well
            // formed record of the wrong energy.
            (
                "a new record starts unsigned",
                "self.forget_signed_meter_values();",
            ),
            (
                "the meter's signature for the open is kept",
                "if response.status == types::powermeter::TransactionRequestStatus::OK { \
                 self.note_start_signature(response.signed_meter_value); }",
            ),
            (
                "the meter's two signatures for the close are kept",
                "self.note_stop_signature(response.start_signed_meter_value, \
                 response.signed_meter_value,);",
            ),
            (
                "the transaction start payload carries its signature",
                "signed_meter_value: self.signed().start.clone(),",
            ),
            (
                "the transaction finish payload carries both, the right way round",
                "signed_meter_value: signed.stop.clone(), \
                 start_signed_meter_value: signed.start.clone(),",
            ),
            (
                "the stop reason reaches the wire",
                "reason: Some(stop_transaction_reason_wire(*reason)),",
            ),
            (
                "the stop request's own token reaches the core",
                "id_tag: request.id_tag.as_ref().and_then(id_tag_from),",
            ),
            (
                "an accepted authorization carries its whole identity",
                "let Some(token) = id_tag_from(&provided_token) else",
            ),
            // The fourth field of the verdict. A non evse specific reservation
            // never reaches `reserve`, so dropping this row leaves the module
            // with no source at all for the id its transaction event owes, and
            // it is another place where reverting to `None` compiles and passes
            // everything else.
            (
                "the reservation a verdict names reaches the core",
                "reservation_id: validation_result.reservation_id,",
            ),
        ];

        #[test]
        fn every_pinned_expression_is_present_in_the_boundary() {
            let source = without_whitespace(boundary());
            for (what, expected) in WIRED {
                assert!(
                    source.contains(without_whitespace(expected).as_str()),
                    "`{what}` is unpinned: `{expected}` is not in the production \
                     half of main.rs"
                );
            }
        }

        #[test]
        fn no_two_rows_pin_the_same_expression() {
            let mut seen: Vec<String> = WIRED
                .iter()
                .map(|(_, expected)| without_whitespace(expected))
                .collect();
            let before = seen.len();
            seen.sort();
            seen.dedup();
            assert_eq!(before, seen.len(), "a row is duplicated");
        }

        /// Every row pins one site rather than a family of them.
        ///
        /// `contains` is satisfied by any occurrence, so a row whose expression
        /// also appears elsewhere stops pinning the site it names: reverting
        /// that site leaves the row green. That is what happened to the session
        /// start's meter reading the moment three more announcements read the
        /// same meter the same way, and it is invisible to the check above,
        /// which compares the rows with each other and not with the source.
        #[test]
        fn every_row_pins_exactly_one_site() {
            let source = without_whitespace(boundary());
            for (what, expected) in WIRED {
                assert_eq!(
                    source
                        .matches(without_whitespace(expected).as_str())
                        .count(),
                    1,
                    "`{what}` does not pin exactly one site: `{expected}`"
                );
            }
        }
    }

    /// The pass-through variable group, pinned against the source text.
    ///
    /// Five of the eight variables `evse_manager` declares as pass-throughs are
    /// published at the boundary: four are republications of a record this
    /// module was handed, which `Intake` receives and publishes on the `evse`
    /// interface exactly as the C++ subscription callbacks that carry them do,
    /// and `evse_id` is configuration said once. The other three are decided by
    /// the core and rendered here from an `Effect`.
    ///
    /// A ledger for the same structural reason `session_event_payloads` has
    /// one: publishing needs a `ModulePublisher`, which needs a live framework
    /// runtime, so no test in this crate can construct one and every one of
    /// these publishes is invisible to all four gates. Reverting any row to the
    /// dropped body it replaced compiles and passes everything else, which is
    /// exactly how the group went unnoticed until the OCPP suite measured the
    /// MeterValues collapse.
    /// The three boundary sites that decide what a legal metrology meter
    /// signs, pinned as source text.
    ///
    /// A ledger rather than behaviour, for the reason `pass_through_variables`
    /// gives: `authorize_response` needs a live `Context` to call, and no test
    /// in this crate can construct one. The two mappings either side of it are
    /// driven for real by the tests above; this is the wiring between them, and
    /// without it the verdict's tariff could stop being read off the wire
    /// record with every other test still green.
    mod ocmf_billing_wiring {
        use super::*;

        /// The production half of this file only, for the reason
        /// `pass_through_variables::boundary` gives: a search over the whole
        /// file would find this table's own text and satisfy itself.
        fn boundary() -> &'static str {
            include_str!("main.rs")
                .split_once("\n#[cfg(test)]\nmod tests {")
                .expect("main.rs carries its tests at the end")
                .0
        }

        fn without_whitespace(text: &str) -> String {
            let dense: String = text.chars().filter(|c| !c.is_whitespace()).collect();
            dense.replace(",)", ")").replace(",}", "}")
        }

        /// `(what it decides, the expression that decides it)`.
        const WIRED: &[(&str, &str)] = &[
            // The identity's declared credential kind, read off the same wire
            // record `value` and `plug_and_charge` come from.
            (
                "the credential kind reaching the core",
                "id_token_type_of(&token.id_token.r#type),",
            ),
            // The verdict's own tariff, whole and in wire order. `core` picks
            // which message opens the transaction; nothing here does.
            (
                "the tariff reaching the core",
                r#"tariff: TariffMessages::new( validation_result .tariff_messages .into_iter() .map(|message| message.content) .collect() )"#,
            ),
            // The metering request built from the effect, both fields off the
            // one record.
            (
                "the metering request the meter is handed",
                "meter.start_transaction(transaction_request( &self.settings.evse_id, transaction_id, id_token.as_ref(), tariff_text.as_deref(), ))",
            ),
        ];

        #[test]
        fn each_billing_decision_is_wired_to_the_value_it_reads() {
            let source = without_whitespace(boundary());
            for (what, expression) in WIRED {
                assert!(
                    source.contains(&without_whitespace(expression)),
                    "{what} is not wired as `{expression}`"
                );
            }
        }

        #[test]
        fn no_other_site_names_the_tariff_messages_of_a_validation_result() {
            // One reader of the wire field, so a second route into the core
            // cannot arrive carrying a tariff `Auth` never recorded.
            assert_eq!(
                boundary().matches("tariff_messages").count(),
                1,
                "the verdict's tariff messages are read at exactly one site"
            );
        }
    }

    mod pass_through_variables {
        use super::*;

        /// The production half of this file only, for the reason
        /// `session_event_payloads::boundary` gives: a search over the whole
        /// file would find this table's own text and satisfy itself.
        fn boundary() -> &'static str {
            include_str!("main.rs")
                .split_once("\n#[cfg(test)]\nmod tests {")
                .expect("main.rs carries its tests at the end")
                .0
        }

        /// Trailing commas absorbed as well as whitespace, because `rustfmt`
        /// decides whether each of these calls fits on one line and adds one
        /// when it does not. What is being pinned is which variable is
        /// published from which value, not how the line wrapped.
        fn without_whitespace(text: &str) -> String {
            let dense: String = text.chars().filter(|c| !c.is_whitespace()).collect();
            dense.replace(",)", ")").replace(",}", "}")
        }

        /// `(the variable, the expression that puts it on the wire)`.
        const WIRED: &[(&str, &str)] = &[
            // The one the OCPP suite measured. All three consumers subscribe
            // it on `evse_manager`, and with nothing published
            // `TriggerMessage(meter_values)` had nothing to answer with.
            (
                "powermeter",
                r#"report_republish("powermeter", context.publisher.evse.powermeter(value.clone()),);"#,
            ),
            // Gated on the billing slot, the same gate the core's reading is
            // gated on, because the C++ subscribes only that slot.
            (
                "powermeter_public_key_ocmf",
                r#"report_republish("powermeter_public_key_ocmf", context.publisher.evse.powermeter_public_key_ocmf(value),);"#,
            ),
            (
                "hw_capabilities",
                r#"report_republish("hw_capabilities", context.publisher.evse.hw_capabilities(value),);"#,
            ),
            (
                "telemetry",
                r#"report_republish("telemetry", context.publisher.evse.telemetry(value));"#,
            ),
            // The only one of the five that is not a republication of a
            // received record: it is configuration, said once at the lifecycle
            // point `evse_managerImpl::ready` says it at.
            (
                "evse_id",
                r#"report_republish("evse_id", publishers.evse.evse_id(self.settings.evse_id.clone()),);"#,
            ),
            // The three the core decides. Their effects have producers that
            // `reachability.py` audits, so what is unpinned about them is only
            // the boundary arm that renders each one.
            (
                "car_manufacturer",
                "Effect::PublishCarManufacturer(manufacturer) => Self::outcome( publishers .evse .car_manufacturer(car_manufacturer_wire(*manufacturer)), )",
            ),
            (
                "ev_info",
                "Effect::PublishEvInfo(info) => { Self::outcome(publishers.evse.ev_info(ev_info_wire(info))) }",
            ),
            (
                "selected_protocol",
                "Effect::PublishSelectedProtocol(protocol) => { Self::outcome(publishers.evse.selected_protocol(protocol.clone())) }",
            ),
            // The billing gate on the key, which is the half a revert would
            // most plausibly drop: without it a deployment with both meters
            // wired publishes the grid side key beside the car side reading.
            (
                "the key comes from the meter that bills",
                "fn on_public_key_ocmf(&self, context: &Context, value: String) { if !meter_feeds_the_core(context.name, self.car_side_meter_wired()) { return; }",
            ),
            // The other two intake gates on the same interface, each pinned
            // where it is applied rather than only where it is defined. Both
            // are absences on the wire - a report or a fault that does not
            // arrive - so nothing downstream can assert them.
            (
                "the floors come from the car side meter",
                "fn on_capabilities(&self, context: &Context, value: types::powermeter::Capabilities) { if !meter_reports_the_floors(context.name) { return; }",
            ),
            (
                "the powermeter fault gate is the C++ subscription decision",
                "if !self.powermeter_faults_reach_the_module(context.name) { return; } self.send(error_event(ErrorSource::Powermeter, error, true));",
            ),
            // The identity on the limits stream. Two strings are in scope here
            // and they name different things, so the wrong one compiles and
            // publishes a plausible identifier: `mod->info.id` names this
            // module instance to the other modules
            // (`evse/evse_managerImpl.cpp:402`), while `evse_id` names the
            // charge point to a driver.
            (
                "the limits stream is named by the module instance",
                // No trailing comma: this table's own normalizer drops one
                // before a closing brace, so the haystack has none.
                "uuid: Some(self.node_id.clone())",
            ),
            // The board's stop reaches the same command the `stop_transaction`
            // call does, carrying both of the fields the C++ hands
            // `cancel_transaction`. A board event of its own compiles and no
            // power path acts on one, which is what this row exists to keep
            // from coming back.
            (
                "the board stop is the stop transaction command",
                "fn on_request_stop_transaction(&self, _context: &Context, value: types::evse_manager::StopTransactionRequest,) { self.send(Event::Command(Command::StopTransaction { reason: stop_transaction_reason_from(value.reason), id_tag: value.id_tag.as_ref().and_then(id_tag_from)",
            ),
        ];

        /// Anything on the group that is not published at all, and what it is
        /// waiting for. Empty, and kept so that the next such gap has a home
        /// and an assertion rather than only a comment.
        const NOT_CARRIED: &[(&str, &str)] = &[];

        #[test]
        fn every_pinned_expression_is_present_in_the_boundary() {
            let source = without_whitespace(boundary());
            for (variable, expected) in WIRED {
                assert!(
                    source.contains(without_whitespace(expected).as_str()),
                    "`{variable}` is unpinned: `{expected}` is not in the production \
                     half of main.rs"
                );
            }
        }

        #[test]
        fn no_two_rows_pin_the_same_expression() {
            let mut seen: Vec<String> = WIRED
                .iter()
                .map(|(_, expected)| without_whitespace(expected))
                .collect();
            let before = seen.len();
            seen.sort();
            seen.dedup();
            assert_eq!(before, seen.len(), "a row is duplicated");
        }

        /// The one row this table used to carry was `ChargingFinished`, a
        /// thirteenth `signal_simple_event` value with no `SessionEvent`
        /// variant, which cost the group one `selected_protocol` publication.
        /// It has a variant now, so the table is empty and the assertion is
        /// the inverse of the one it replaces: nothing on the group is
        /// unpublished, and the event that was missing is still there.
        #[test]
        fn nothing_on_the_group_is_unpublished() {
            assert!(NOT_CARRIED.is_empty(), "{NOT_CARRIED:?}");
            assert!(
                include_str!("core/session.rs").contains("ChargingFinished"),
                "`ChargingFinished` is not a session event; the group is short \
                 an announcement"
            );
        }

        /// Every variable the `evse_manager` interface declares that this
        /// module's own `evse` publisher can carry, accounted for.
        ///
        /// The census is over the interface rather than over this file, so a
        /// variable nobody publishes is a missing row and not an absence
        /// nothing reads. The eleven excluded below are published through an
        /// `Effect` the core produces and are audited by
        /// `reachability.py` there instead; `ev_info` is the one the interface
        /// declares, and it is published from `Effect::PublishEvInfo`.
        #[test]
        fn the_group_is_the_eight_the_verification_named() {
            let mut group: Vec<&str> = WIRED
                .iter()
                .map(|(variable, _)| *variable)
                .filter(|variable| !variable.starts_with("the "))
                .collect();
            group.sort_unstable();
            assert_eq!(
                group,
                vec![
                    "car_manufacturer",
                    "ev_info",
                    "evse_id",
                    "hw_capabilities",
                    "powermeter",
                    "powermeter_public_key_ocmf",
                    "selected_protocol",
                    "telemetry",
                ],
                "the pass-through group changed"
            );
        }
    }

    /// The transcript's boundary wiring, pinned against the source text.
    ///
    /// Every row here exists because a mutation of that exact expression
    /// survived all four gates. They survived for one structural reason:
    /// `EverestEffects` holds a `ModulePublisher`, which needs a live framework
    /// runtime, and the three session log config keys are read inside `fn main`.
    /// So no test can construct either, and **the whole boundary half of the
    /// transcript -- the config wiring, `logging_path` on the wire, the record
    /// dispatch, and the loud-and-local failure reporting -- is invisible to
    /// `test-boundary.sh` as well as to `test-core.sh`, which strips this file
    /// outright.** `reachability.py` does not close it either: it proves an
    /// effect has a producer and says nothing about what the producer does with
    /// it.
    ///
    /// That is the same hole `dispatched_hlc_updates` was written for, and the
    /// same remedy: assert the source. What it protects is not cosmetic. A
    /// `suffix_for` called with its two arguments transposed writes every
    /// transcript into a directory named after the literal `session_uuid`; a
    /// `note_failure` that is never called leaves the logger believing a
    /// transcript is open that is not, so every later record is addressed to a
    /// file that was never created and the operator sees nothing; a
    /// `logging_path` hard coded to `None` removes the one signal that tells an
    /// operator, without reading the log, whether logging actually started.
    ///
    /// Adding a row is how a new session log call site gets a gate.
    /// The hardware over voltage monitor's voltage stream.
    ///
    /// It reaches the software watchdog that shadows the monitor on the same
    /// thresholds. No test can call a subscriber callback, and this one was a
    /// no-op for the life of the port, so nothing but the source says it is
    /// wired at all.
    mod over_voltage_wiring {
        fn boundary() -> &'static str {
            include_str!("main.rs")
                .split_once("\n#[cfg(test)]\nmod tests {")
                .expect("main.rs carries its tests at the end")
                .0
        }

        /// The meter's DC voltage is taken from `dc` alone. The field beside
        /// it falls back to L1 and then to zero, and feeding either into the
        /// plausibility comparison would have an AC phase voltage, or a zero
        /// standing in for a missing reading, disagree with the instruments.
        #[test]
        fn the_meter_dc_voltage_is_taken_from_dc_alone() {
            let dense: String = boundary().chars().filter(|c| !c.is_whitespace()).collect();
            assert!(
                dense.contains("letdc_voltage_v=value.voltage_v.as_ref().and_then(|voltage|voltage.dc);"),
                "the meter's DC voltage is not taken from the DC field alone"
            );
        }

        #[test]
        fn the_over_voltage_stream_reaches_the_core() {
            let dense: String = boundary().chars().filter(|c| !c.is_whitespace()).collect();
            assert!(
                dense.contains("self.send(Event::OverVoltageMeasurement{voltage_v:value});"),
                "the over voltage measurement stream is not carried to the core"
            );
        }
    }

    /// The charging phase carried on every supply mode change.
    ///
    /// Four arms of a four way map, none of which any test can call: a pairing
    /// swapped here compiles, reads plausibly, and tells the power supply that
    /// a cable check is a charge or that a charge is internal testing. The core
    /// side is pinned by `each_charging_phase_rides_on_the_mode_change_it_opened`
    /// and the cable check sequence test; this pins the translation.
    mod supply_charging_phase_wiring {
        fn boundary() -> &'static str {
            include_str!("main.rs")
                .split_once("\n#[cfg(test)]\nmod tests {")
                .expect("main.rs carries its tests at the end")
                .0
        }

        /// Switching off reports `Other` deliberately, and differs from the
        /// C++ in doing so. See the intentional divergence list in
        /// `docs/architecture.md`: `types/power_supply_DC.yaml` names `Other`
        /// for exactly this call, while `powersupply_DC_off` sends whatever
        /// phase was running and only then resets it.
        #[test]
        fn switching_off_reports_no_phase() {
            let dense: String = boundary()
                .chars()
                .filter(|c| !c.is_whitespace() && *c != '{' && *c != '}')
                .collect();
            assert!(
                dense.contains(
                    "types::power_supply_DC::Mode::Off,types::power_supply_DC::ChargingPhase::Other"
                ),
                "the supply off no longer reports Other"
            );
        }

        #[test]
        fn every_phase_maps_to_its_own_counterpart() {
            // Braces absorbed as well as whitespace: `rustfmt` wraps an arm in
            // a block when it does not fit on one line, and what is pinned is
            // the pairing, not how the arm wrapped.
            let dense: String = boundary()
                .chars()
                .filter(|c| !c.is_whitespace() && *c != '{' && *c != '}')
                .collect();
            for phase in ["Other", "CableCheck", "PreCharge", "Charging"] {
                let pairing =
                    format!("ChargingPhase::{phase}=>types::power_supply_DC::ChargingPhase::{phase}");
                assert!(
                    dense.contains(&pairing),
                    "{phase} does not map to its own counterpart"
                );
            }
        }
    }

    /// The isolation monitor's self test verdict, which decides whether a
    /// cable check may proceed.
    ///
    /// Pinned for the reason `ported_hlc_facts` gives and which applies harder
    /// here: a subscriber callback cannot be called from a test, and a boolean
    /// wired the wrong way round compiles and reads plausibly. Inverted, every
    /// failed self test would pass the cable check and every passing one would
    /// fail it, which is the defect this wiring exists to remove.
    mod isolation_self_test_wiring {
        /// The production half of this file only: the row below quotes the
        /// expression it pins, so a search over the whole file would find the
        /// row itself and stay green with the wiring deleted.
        fn boundary() -> &'static str {
            include_str!("main.rs")
                .split_once("\n#[cfg(test)]\nmod tests {")
                .expect("main.rs carries its tests at the end")
                .0
        }

        /// The two earth readings are adjacent, same typed and optional, so a
        /// swap between them compiles and reads plausibly, and the generator
        /// spells them `l_1_e` and `l_2_e` rather than as the wire does. A
        /// swapped pair would still debounce and still raise, on the wrong
        /// conductor.
        #[test]
        fn each_earth_reading_is_filled_from_its_own_field() {
            let dense: String = boundary().chars().filter(|c| !c.is_whitespace()).collect();
            for (field, source) in [
                ("voltage_to_earth_l1e_v", "value.voltage_to_earth_l_1_e_v"),
                ("voltage_to_earth_l2e_v", "value.voltage_to_earth_l_2_e_v"),
            ] {
                assert!(
                    dense.contains(&format!("{field}:{source}")),
                    "{field} is not filled from its own wire field"
                );
            }
        }

        #[test]
        fn the_verdict_reaches_the_core_unnegated() {
            let dense: String = boundary().chars().filter(|c| !c.is_whitespace()).collect();
            assert!(
                dense.contains("self.send(Event::IsolationSelfTest(result));"),
                "the self test verdict is not carried to the core as it arrived"
            );
        }
    }

    mod session_log_wiring {
        use super::*;

        /// The production half of this file only.
        ///
        /// Split at the test module rather than searched whole, for the reason
        /// `dispatched_hlc_updates` censuses over the dispatch: every row below
        /// quotes a real expression, so a search over the whole file would find
        /// the table's own text and satisfy itself, and deleting the wiring
        /// would leave it green.
        fn boundary() -> &'static str {
            include_str!("main.rs")
                .split_once("\n#[cfg(test)]\nmod tests {")
                .expect("main.rs carries its tests at the end")
                .0
        }

        /// `(what it pins, the expression the source must contain)`.
        ///
        /// Written whitespace insensitively, so reformatting cannot break a row
        /// and cannot satisfy one either.
        const WIRED: &[(&str, &str)] = &[
            // All three configuration keys, read at the one place that builds
            // the logger, which is now the constructor itself:
            // `SessionLogger::for_settings` reads the root, the payload switch
            // and the feature key, and answers `None` for a deployment that did
            // not ask for the feature. `EvseManager.cpp:137-145` reads the same
            // three at the same point in the lifecycle. The three rows this
            // replaces pinned a root, a `set_xml_output` call and an
            // `if ... { enable() }`, which is the shape that let the logger be
            // built and left off.
            (
                "the logger is built from the logging settings and nothing else",
                "let session_log = SessionLogger::for_settings(&settings.logging);",
            ),
            // The boot diagnostic. The defect it replaces was silence, so a
            // logger that says nothing is the original bug back.
            (
                "the boot announcement is emitted",
                "if let Some(logger) = session_log.as_ref() { log::info!(\"{}\", logger.announcement()); }",
            ),
            // The deployment with no logger, which is the cell the transcript
            // suite never drove: every test in it builds a logger. The core
            // emits the records whatever the deployment is, which is the C++
            // shape - `SessionLog::evse` and friends are called
            // unconditionally and the object decides - so this side has to
            // drop them, and dropping them is what the absence means. Without
            // the early return the `as_mut()` would have to be unwrapped, and
            // an `expect` here would take the process down on a deployment
            // that simply did not ask for the feature.
            (
                "a deployment with no logger is told nothing",
                "let Some(logger) = held.as_mut() else { return EffectOutcome::Ok; };",
            ),
            // `evse/evse_managerImpl.cpp:170`: the configured suffix is the
            // first argument and the session identity the second. Both are
            // `&str`, so a transposition compiles and reads plausibly.
            (
                "suffix_for takes the config key first and the session second",
                "session_log::suffix_for(&self.settings.logging.logfile_suffix, session_uuid)",
            ),
            // `evse/evse_managerImpl.cpp:172-174`, the one signal an operator
            // can read off the wire instead of out of the log.
            (
                "logging_path reports the directory that actually opened",
                "logging_path: self.session_log_dir(),",
            ),
            (
                "the reported directory is the open session's, not the root",
                ".and_then(|logger| logger.session_dir()) .map(str::to_string)",
            ),
            // The three record origins reach the three methods that spell them.
            // `logger.evse` and `logger.car` take the same four arguments, so
            // transposing the two arms compiles and puts every vehicle message
            // in the EVSE column of every transcript.
            (
                "an EVSE record reaches evse",
                "session_log::Origin::Evse => logger.evse(&now, *iso15118, msg, payload),",
            ),
            (
                "a car record reaches car",
                "session_log::Origin::Car => logger.car(&now, *iso15118, msg, payload),",
            ),
            (
                "a system record reaches sys",
                "session_log::Origin::Sys => logger.sys(&now, msg),",
            ),
            // The car side meter's capability report reaches the core, which
            // records it and merges its floors into what the vehicle is
            // offered (`core::powermeter_limits`). Both fields are
            // `Option<f64>` and the two directions cross on the way to the
            // supply's limits, so a positional literal would swap the charging
            // and discharging directions silently and every arithmetic test in
            // the core would still pass.
            (
                "the powermeter capability report reaches the core by name",
                "Event::PowermeterCapabilities(PowermeterCapabilities { min_import_current_a: value.min_import_current_a, min_export_current_a: value.min_export_current_a, })",
            ),
            // The EVerest log line, the parallel output of `SessionLog.cpp:210-227`.
            (
                "each line reaches the EVerest log exactly once",
                "for line in &output.lines { log::info!(\"{}\", line.render()); }",
            ),
            // Loud and local. Every filesystem fault names the path and the OS
            // reason and reaches the logger, which is the only thing that keeps
            // the logger's idea of the filesystem honest.
            (
                "a failed action is reported to the logger with the OS reason",
                "for action in &output.actions { if let Err(error) = perform_log_action(action) { logger.note_failure(action, &error.to_string()); } }",
            ),
            // And nothing propagates. This is the assertion that no route
            // exists by which a logging fault can stop a charge.
            (
                "a record whose payload is absent still borrows four fields",
                "let owned = payload.as_deref().cloned().unwrap_or_default();",
            ),
            // A directory and every missing parent, which is what the C++
            // `create_directories` does (`SessionLog.cpp:61`, `:91`).
            (
                "a directory is created with its parents",
                "session_log::Action::CreateDir { path } => std::fs::create_dir_all(path),",
            ),
            (
                "an append never truncates and never short writes",
                ".append(true) .open(path) .and_then(|mut file| file.write_all(text.as_bytes())),",
            ),
            // `EvseManager.cpp:1877`. Named through serde and not through
            // `Debug`, so a wire name that ever stops matching the Rust
            // identifier is still spelled correctly. See
            // `an_iso_message_id_is_named_by_the_wire_and_not_by_debug` below
            // for why no behavioural test can hold this today.
            (
                "an ISO message id is named through serde",
                "match ::everestrs::serde_json::to_value(id) { Ok(::everestrs::serde_json::Value::String(name)) => name,",
            ),
            (
                "an unnameable id is reported and degrades to an empty string",
                "log::error!(\"cannot name an ISO 15118 message id: {other:?}\"); String::new()",
            ),
        ];

        fn without_whitespace(text: &str) -> String {
            text.chars().filter(|c| !c.is_whitespace()).collect()
        }

        #[test]
        fn every_pinned_expression_is_present_in_the_boundary() {
            let source = without_whitespace(boundary());
            for (what, expected) in WIRED {
                assert!(
                    source.contains(without_whitespace(expected).as_str()),
                    "`{what}` is unpinned: `{expected}` is not in the production \
                     half of main.rs"
                );
            }
        }

        /// Why naming an id by `Debug` cannot be caught by a test today.
        ///
        /// Replacing `serde_json::to_value` with `format!("{id:?}")` survives
        /// every gate, and it survives for a provable reason rather than a
        /// missing assertion: `everestrs-build` emits
        /// `types::iso15118::V2gMessageId` with **no `#[serde(rename)]` on any
        /// variant**, so for all 74 of them the wire name and the Rust
        /// identifier are the same string. No input distinguishes the two
        /// spellings, so no test can.
        ///
        /// It is still the wrong call, and the difference is not hypothetical.
        /// Other generated enums in the same file DO carry variant renames --
        /// `ac_rcd/DC` for `Dc`, `generic/VendorError` for `VendorError` -- so
        /// the rename mechanism is live in this generator, and the mangling
        /// recorded in `AGENTS.md` (`v2g_json` becoming `v_2_g_json`) is the
        /// same generator rewriting names it cannot use verbatim. The day one
        /// ISO message id needs a rename, `Debug` starts naming it wrongly, the
        /// direction rule reads that wrong name for `Res`, and every message of
        /// that type lands in the wrong column of every transcript.
        ///
        /// So the route is pinned in the ledger above, by source text, and the
        /// premise of the equivalence is pinned here: if a rename ever appears
        /// on this enum, this test fails and the equivalence argument is void.
        #[test]
        fn an_iso_message_id_is_named_by_the_wire_and_not_by_debug() {
            for id in [
                types::iso15118::V2gMessageId::SupportedAppProtocolReq,
                types::iso15118::V2gMessageId::SessionSetupRes,
                types::iso15118::V2gMessageId::AcDerSaeChargeParameterDiscoveryReq,
                types::iso15118::V2gMessageId::DcWeldingDetectionRes,
                types::iso15118::V2gMessageId::UnknownMessage,
            ] {
                assert_eq!(
                    v2g_message_id(&id),
                    format!("{id:?}"),
                    "a variant rename on V2gMessageId would void the equivalence \
                     argument for naming an id by Debug; the ledger pins the serde \
                     route, and this is its premise"
                );
            }
        }

        /// The rows are distinct, so a copied row cannot pad the ledger.
        #[test]
        fn no_two_rows_pin_the_same_expression() {
            let mut seen: Vec<String> = WIRED
                .iter()
                .map(|(_, expected)| without_whitespace(expected))
                .collect();
            let before = seen.len();
            seen.sort();
            seen.dedup();
            assert_eq!(before, seen.len(), "a row is duplicated");
        }

        /// `EffectOutcome::Ok`, unconditionally, is the whole safety argument.
        ///
        /// The C++ logs and continues at every one of its filesystem failure
        /// paths, and this is that made structural: there is no route by which a
        /// logging fault reaches the charge. Asserted as the ABSENCE of a
        /// failure return inside the transcript's own method, because a test
        /// cannot call it. A mutant that returned `EffectOutcome::Failed` from
        /// the failure branch survived all four gates.
        #[test]
        fn a_transcript_fault_never_reaches_the_core() {
            let body = boundary()
                .split_once("fn session_log(&self, request: &SessionLogEffect) -> EffectOutcome {")
                .expect("the transcript effect handler")
                .1;
            let body = body
                .split_once("\n    }\n")
                .expect("the end of that method")
                .0;

            assert!(
                !body.contains("EffectOutcome::Failed"),
                "a transcript fault must not become an effect failure: {body}"
            );
            assert!(
                body.trim_end().ends_with("EffectOutcome::Ok"),
                "the handler must end by answering Ok: {body}"
            );
        }
    }

    /// The outbound counterpart of `ported_hlc_facts`: every `HlcUpdate` the
    /// boundary dispatches, and the command on the generated publisher each one
    /// must reach.
    ///
    /// Asserted against the source for the same reason the inbound ledger is,
    /// and the reason is stronger here. An arm calls a method on a
    /// `ISO15118_chargerPublisher` borrowed out of a `ModulePublisher`, which
    /// needs a live framework runtime, so no test can construct one and there
    /// is no return value to compare: every arm in this dispatch is invisible
    /// to `test-core.sh`, which strips this file, and to the reachability audit,
    /// which proves an effect has a producer and says nothing about which
    /// command the producer ends up calling.
    ///
    /// What that leaves unguarded is the transposition. `interfaces/ISO15118_charger.yaml`
    /// declares 28 commands, several of them adjacent pairs over the same
    /// payload shape, and the C++ calls the members of those pairs on
    /// consecutive lines: `update_isolation_status` beside `cable_check_finished`
    /// (`EvseManager.cpp:2025-2026`), `update_dc_maximum_limits` beside
    /// `update_dc_minimum_limits` (`:1574`/`:1579`), `update_meter_info` beside
    /// `update_ac_present_power` (`:1165`/`:1168`). Sending one of a pair to the
    /// other's command compiles, reads plausibly, and reaches a real vehicle as
    /// the wrong announcement. Three of the four such arms already carried a
    /// hand written pin above, each added after a mutation survived every other
    /// test in this file; this table is those pins generalized to all of them,
    /// so a wrong publisher fails a gate rather than passing in silence.
    ///
    /// The DC pair is now dispatched in both directions, and unlike the other
    /// two it cannot transpose silently: `maximum_limits_payload` and
    /// `minimum_limits_payload` return the distinct generated types
    /// `DcEvseMaximumLimits` and `DcEvseMinimumLimits`, so handing one arm's
    /// payload to the other arm's command is a type error rather than a wrong
    /// announcement. Checked by mutation, not by reading: swapping the maximum
    /// arm's command fails to compile. Its row here pins the command name
    /// against a later refactor that gives the two a common payload.
    ///
    /// Dispatching a command means adding its row here and deleting its row
    /// from `UNDISPATCHED` below.
    /// The six `slac` commands this module sends, each pinned to its variant.
    ///
    /// The `Effect::SlacUpdate` dispatch is boundary wiring no test can reach,
    /// and it is the one dispatch here whose arms are interchangeable to the
    /// compiler: every command `interfaces/slac.yaml` declares but `reset`
    /// takes no argument and returns the same type, so sending `leave_bcd`
    /// where `enter_bcd` belongs compiles and matches nothing. Same ledger as
    /// `dispatched_hlc_updates`, at the size this dispatch needs.
    mod dispatched_slac_updates {
        const SOURCE: &str = include_str!("main.rs");

        /// `(variant, the call its arm must be)`.
        const DISPATCHED: &[(&str, &str)] = &[
            ("EnterBcd", "slac.enter_bcd()"),
            ("LeaveBcd", "slac.leave_bcd()"),
            ("Reset", "slac.reset(false)"),
            ("DlinkError", "slac.dlink_error()"),
            ("DlinkPause", "slac.dlink_pause()"),
            ("DlinkTerminate", "slac.dlink_terminate()"),
        ];

        /// Production source only, so the table cannot satisfy itself with its
        /// own rows. `dispatched_hlc_updates::production` splits here too.
        fn production() -> &'static str {
            SOURCE
                .split_once("\n#[cfg(test)]\nmod tests {")
                .expect("the test module marker moved")
                .0
        }

        #[test]
        fn every_variant_sends_the_command_its_row_names() {
            for (variant, call) in DISPATCHED {
                let arm = format!("SlacUpdate::{variant} => {call},");
                assert!(
                    production().contains(&arm),
                    "the `Effect::SlacUpdate` dispatch has no arm `{arm}`"
                );
            }
        }

        #[test]
        fn no_two_variants_send_the_same_command() {
            let mut commands: Vec<&str> = DISPATCHED.iter().map(|(_, call)| *call).collect();
            let named = commands.len();
            commands.sort_unstable();
            commands.dedup();
            assert_eq!(
                commands.len(),
                named,
                "two variants send one command: {commands:?}"
            );
        }

        #[test]
        fn the_ledger_names_every_variant() {
            // The dispatch matches `SlacUpdate` exhaustively, so a new variant
            // cannot compile without an arm. This is what makes it also need a
            // row. Counted off the enum rather than off a number written here.
            let body = include_str!("core/effect.rs")
                .split_once("pub enum SlacUpdate {")
                .expect("`SlacUpdate` moved")
                .1
                .split_once("\n}")
                .expect("unterminated `SlacUpdate`")
                .0;
            let variants = body
                .lines()
                .map(str::trim)
                .filter(|line| line.ends_with(',') && !line.starts_with("///"))
                .count();
            assert_eq!(
                variants,
                DISPATCHED.len(),
                "a `SlacUpdate` variant has no row"
            );
        }
    }

    mod dispatched_hlc_updates {
        const SOURCE: &str = include_str!("main.rs");

        /// `(variant, the call expression its arm must contain)`.
        ///
        /// Every command named here is declared in
        /// `interfaces/ISO15118_charger.yaml`, and no two rows name the same
        /// one; `each_command_is_reached_by_exactly_one_arm` holds both.
        const DISPATCHED: &[(&str, &str)] = &[
            (
                "AuthorizationResponse",
                "hlc.authorization_response( authorization_status_enum(response.status), certificate_status_enum(response.certificate), )",
            ),
            ("CableCheckFinished", "hlc.cable_check_finished(*ok)"),
            (
                "IsolationStatus",
                "hlc.update_isolation_status(isolation_status_enum(*status))",
            ),
            ("ContactorClosed", "hlc.ac_contactor_closed(*closed)"),
            ("StopCharging", "hlc.stop_charging(*stop)"),
            ("DlinkReady", "hlc.dlink_ready(*ready)"),
            (
                "PowerSupplyCapabilities",
                "hlc.set_powersupply_capabilities(power_supply_capabilities_payload(caps))",
            ),
            (
                "ChargingParameters",
                "hlc.set_charging_parameters(types::iso15118::SetupPhysicalValues { ac_nominal_voltage: values.ac_nominal_voltage_v, dc_current_regulation_tolerance: values.dc_current_regulation_tolerance_a, dc_peak_current_ripple: values.dc_peak_current_ripple_a, dc_energy_to_be_delivered: values.dc_energy_to_be_delivered_wh, })",
            ),
            (
                "DcMinimumLimits",
                "hlc.update_dc_minimum_limits(minimum_limits_payload(limits))",
            ),
            (
                "DcMaximumLimits",
                "hlc.update_dc_maximum_limits(maximum_limits_payload(limits))",
            ),
            (
                "AcMaximumLimits",
                "hlc.update_ac_maximum_limits(types::iso15118::AcEvseMaximumPower { charge_power, discharge_power, })",
            ),
            (
                "AcMinimumLimits",
                "hlc.update_ac_minimum_limits(types::iso15118::AcEvseMinimumPower { charge_power, discharge_power, })",
            ),
            (
                "AcParameters",
                "hlc.update_ac_parameters(ac_parameters_payload(parameters))",
            ),
            ("AcMaxCurrent", "hlc.update_ac_max_current(*amps)"),
            (
                "AcTargetPower",
                "hlc.update_ac_target_values(ac_target_values_payload(power))",
            ),
            (
                "AcPresentPower",
                "hlc.update_ac_present_power(power_payload(power))",
            ),
            (
                "MeterInfo",
                "hlc.update_meter_info(self.meter_value())",
            ),
            (
                "DcPresentValues",
                "hlc.update_dc_present_values(types::iso15118::DcEvsePresentVoltageCurrent { evse_present_voltage: *voltage_v, evse_present_current: Some(*current_a), })",
            ),
            ("PauseCharging", "hlc.pause_charging(*pause)"),
            ("SendError", "hlc.send_error(evse_error_enum(*error))"),
            ("ReceiptRequired", "hlc.receipt_is_required(*required)"),
            (
                "Setup",
                "hlc.setup( *debug_mode, types::iso15118::EVSEID { evse_id: evse_id.clone(), evse_id_din: Some(evse_id_din.clone()), }, sae_bidi_mode_enum(*sae_mode), )",
            ),
            ("BptSetup", "hlc.bpt_setup(bpt_setup_payload(setup))"),
            ("ResetError", "hlc.reset_error()"),
            (
                "TransferModes",
                "hlc.update_energy_transfer_modes( modes.iter().copied().map(transfer_mode_enum).collect(), )",
            ),
            // The second positional is `fake_dc_enabled`. It has no name at
            // the call site, so this row is what pins its position among the
            // three booleans: reading `setup.fake_dc` into either of the other
            // two slots compiles.
            (
                "SessionSetup",
                "hlc.session_setup( setup.central_contract_validation_allowed, setup.fake_dc, setup.payment_options.iter().copied().map(payment_option_enum).collect(), setup.supported_certificate_service, )",
            ),
        ];

        /// The three commands `interfaces/ISO15118_charger.yaml` declares that
        /// this dispatch does not send, and why each one is absent.
        ///
        /// The first two are unported deliberately and the reasoning is in
        /// `docs/architecture.md`. The last is a gap found while pinning this
        /// table and is recorded here rather than fixed, because giving it a
        /// producer is a behaviour change and this table is a coverage change.
        /// It is written up under "Outbound commands with no variant" in that
        /// same document.
        const UNDISPATCHED: &[(&str, &str)] = &[
            ("no_energy_pause_charging", "both producers are DC only (`Charger.cpp:346-362`, `:679-685`) and the DC limits channel they gate on is not ported"),
            ("update_supported_app_protocols", "nothing in tree originates a call; all four sites are IsoMux forwarding one it received"),
        ];

        /// Whitespace and the formatter's trailing commas removed, for the same
        /// reason `ported_hlc_facts::without_whitespace` removes them: the
        /// expression is reflowed by `rustfmt` and the fact being pinned is
        /// which command is called with which payload, not how the line
        /// wrapped. Trailing commas are absorbed before both a paren and a
        /// brace, because these arms fill struct literals as well as calls.
        fn without_whitespace(text: &str) -> String {
            let dense: String = text.chars().filter(|c| !c.is_whitespace()).collect();
            dense.replace(",)", ")").replace(",}", "}")
        }

        /// Production source only.
        ///
        /// Every anchor below is a string this module also quotes, so a search
        /// over the whole file finds the quotation and satisfies itself:
        /// deleting the thing being pinned would leave the search unchanged.
        /// `ported_hlc_facts` splits here for the same reason.
        fn production() -> &'static str {
            SOURCE
                .split_once("\n#[cfg(test)]\nmod tests {")
                .expect("the test module marker moved")
                .0
        }

        /// The command a table row calls, out of its `hlc.<command>(..)`.
        fn command_of(expected: &'static str) -> &'static str {
            expected
                .strip_prefix("hlc.")
                .and_then(|rest| rest.split_once('('))
                .expect("every row calls a method on `hlc`")
                .0
        }

        /// The arm list of the `Effect::HlcUpdate` dispatch, brace matched from
        /// the `match update {` that opens it.
        ///
        /// Scoped to the arms rather than read as a window from each variant,
        /// so the census below counts arms and nothing else. The `None` branch
        /// beside it mentions `HlcUpdate::SendError` in a `matches!`, which is
        /// not an arm, and this file's other tests quote variant names too.
        fn dispatch_arms() -> &'static str {
            const OPENS: &str = "Some(hlc) => Self::outcome(match update {";
            let source = production();
            let at = source
                .find(OPENS)
                .expect("main.rs has no `Effect::HlcUpdate` dispatch");
            let open = at + OPENS.len() - 1;
            let mut depth = 0usize;
            for (offset, character) in source[open..].char_indices() {
                match character {
                    '{' => depth += 1,
                    '}' => {
                        depth -= 1;
                        if depth == 0 {
                            return &source[open + 1..open + offset];
                        }
                    }
                    _ => {}
                }
            }
            panic!("unterminated `Effect::HlcUpdate` dispatch");
        }

        /// Every variant named by an arm of that dispatch, in source order.
        fn dispatched_variants() -> Vec<&'static str> {
            let arms = dispatch_arms();
            arms.match_indices("HlcUpdate::")
                .filter_map(|(at, _)| {
                    let from = at + "HlcUpdate::".len();
                    let rest = &arms[from..];
                    let end = rest.find(|c: char| !c.is_alphanumeric())?;
                    (end > 0).then(|| &rest[..end])
                })
                .collect()
        }

        /// The arm for one variant: from its pattern to the next arm's, so the
        /// whole body is in scope and not a fixed character window. The three
        /// hand written pins above each take a window instead, and one of them
        /// carries a comment about having already been broken by an unrelated
        /// arm growing past its budget.
        fn arm_of(variant: &str) -> &'static str {
            let arms = dispatch_arms();
            let pattern = format!("HlcUpdate::{variant}");
            let at = arms
                .find(&pattern)
                .unwrap_or_else(|| panic!("the dispatch has no `{pattern}` arm"));
            let body = &arms[at + pattern.len()..];
            let end = body.find("HlcUpdate::").unwrap_or(body.len());
            &arms[at..at + pattern.len() + end]
        }

        #[test]
        fn every_dispatched_update_reaches_the_command_the_table_names() {
            for (variant, expected) in DISPATCHED {
                let arm = without_whitespace(arm_of(variant));
                assert!(
                    arm.contains(&without_whitespace(expected)),
                    "`{variant}` must call `{expected}`, arm is `{arm}`"
                );
            }
        }

        /// The table accounts for every arm, so a twenty fourth publisher
        /// cannot appear unpinned.
        ///
        /// Censused over the dispatch itself rather than over the file, because
        /// the table quotes each variant name with a real command after it: a
        /// census over the whole file would count the table's own rows and
        /// satisfy itself, and deleting an arm would leave it unchanged.
        #[test]
        fn the_table_accounts_for_every_dispatched_arm() {
            let mut found = dispatched_variants();
            found.sort_unstable();
            let before = found.len();
            found.dedup();
            assert_eq!(
                before,
                found.len(),
                "a variant is dispatched by two arms, so one of them is dead"
            );

            let mut table: Vec<&str> = DISPATCHED.iter().map(|(variant, _)| *variant).collect();
            table.sort_unstable();

            assert_eq!(
                found, table,
                "an `HlcUpdate` arm was added or removed without updating the ledger"
            );
        }

        /// One arm per command, and one command per arm.
        ///
        /// This is the assertion the adjacent pairs need, and the AC limits
        /// pair is why it is not redundant with the table above.
        /// `core::effect` says of those two arms that "handing the maximum
        /// payload to the minimum command is a type error rather than a silent
        /// swap, which is a stronger pin than any test could be". The first
        /// half is true: `AcEvseMaximumPower` and `AcEvseMinimumPower` are
        /// field for field identical but distinct types, so passing one to the
        /// other's command does not compile.
        ///
        /// The second half does not follow. The mutation that matters moves the
        /// command and its wire type together, `update_ac_minimum_limits(
        /// AcEvseMinimumPower { .. })` becoming `update_ac_maximum_limits(
        /// AcEvseMaximumPower { .. })` inside the `AcMinimumLimits` arm. Both
        /// halves stay consistent, it compiles, and the vehicle is told the
        /// floor as though it were the ceiling. Nothing in the type system
        /// sees it. This test does: the retargeted command appears twice and
        /// its own appears not at all.
        #[test]
        fn each_command_is_reached_by_exactly_one_arm() {
            let arms = dispatch_arms();
            for (variant, expected) in DISPATCHED {
                let command = command_of(expected);
                let calls = arms.matches(&format!("hlc.{command}(")).count();
                assert_eq!(
                    calls, 1,
                    "`{command}` is called {calls} times in the dispatch, so `{variant}` \
                     either shares a command with another arm or has lost its own"
                );
            }
        }

        /// A command cannot be in both tables: one dispatches it and the other
        /// records why it is absent.
        #[test]
        fn no_command_is_both_dispatched_and_undispatched() {
            let dispatched: Vec<&str> = DISPATCHED
                .iter()
                .map(|(_, expected)| command_of(expected))
                .collect();

            for (command, _) in UNDISPATCHED {
                assert!(
                    !dispatched.contains(command),
                    "`{command}` is in both tables"
                );
                assert!(
                    !dispatch_arms().contains(&format!("hlc.{command}(")),
                    "`{command}` is recorded as undispatched but the dispatch calls it"
                );
            }
        }

        /// The commands `interfaces/ISO15118_charger.yaml` declares, read from
        /// the interface itself.
        ///
        /// Parsed with string operations rather than a yaml crate because the
        /// crate has no yaml dependency and this needs one shape out of one
        /// file: the keys indented two spaces under the top level `cmds:`.
        ///
        /// The block runs to the next key at column zero, which is `vars:`.
        /// Comments sit at column zero inside it, the first of them directly
        /// under `cmds:` itself, so they continue the block rather than ending
        /// it; taking only indented lines finds no commands at all.
        fn declared_commands() -> Vec<&'static str> {
            const INTERFACE: &str = include_str!("../../../../interfaces/ISO15118_charger.yaml");

            INTERFACE
                .split_once("\ncmds:\n")
                .expect("the interface has no top level `cmds:`")
                .1
                .lines()
                .take_while(|line| {
                    line.is_empty() || line.starts_with(' ') || line.starts_with('#')
                })
                .filter_map(|line| {
                    let name = line.strip_prefix("  ")?;
                    (!name.starts_with(' ')).then(|| name.trim_end().strip_suffix(':'))?
                })
                .collect()
        }

        /// The two tables together account for every command the interface
        /// declares, so a command cannot be forgotten by being in neither.
        ///
        /// Read from the interface rather than pinned as a count, so a command
        /// added there is a failure here and not a silent absence. A count
        /// would not do it: moving a row between the two tables leaves the sum
        /// unchanged, which is correct, but so does adding a twenty ninth
        /// command to the interface and to neither table, which is the case
        /// this exists to catch.
        #[test]
        fn the_two_tables_account_for_every_command_the_interface_declares() {
            let mut declared = declared_commands();
            declared.sort_unstable();
            assert!(
                declared.len() >= 28,
                "only {} commands parsed out of the interface, so the parse broke \
                 rather than the interface shrinking: {declared:?}",
                declared.len()
            );

            let mut accounted: Vec<&str> = DISPATCHED
                .iter()
                .map(|(_, expected)| command_of(expected))
                .chain(UNDISPATCHED.iter().map(|(command, _)| *command))
                .collect();
            accounted.sort_unstable();

            assert_eq!(
                accounted, declared,
                "`interfaces/ISO15118_charger.yaml` and the two tables disagree: a \
                 command was added to the interface and to neither table, or a table \
                 names one the interface does not declare"
            );
        }

        /// Exactly one variant is singled out in the absent slot branch.
        ///
        /// `an_absent_stack_is_named_when_an_error_cannot_be_sent` above pins
        /// that `SendError` is the one named. This pins that it is the only
        /// one, because the decision that the other twenty two updates reach an
        /// absent slot in silence is a decision, taken from
        /// `EvseManager.cpp:420-425` where the C++ names exactly one, and a
        /// second `matches!` added beside it would change what a deployment
        /// without an HLC stack logs on every charge loop.
        #[test]
        fn the_absent_slot_branch_singles_out_exactly_one_update() {
            const OPENS: &str = "None => {\n                    if matches!(update, HlcUpdate::";
            let source = production();
            let at = source
                .find(OPENS)
                .expect("the absent slot branch no longer opens on a `matches!`");
            let branch = &source[at..];
            let end = branch
                .find("Effect::PublishProvidedToken")
                .expect("the effect after the dispatch moved");
            assert_eq!(
                branch[..end].matches("HlcUpdate::").count(),
                1,
                "the absent slot branch names more than one update, so the silence \
                 the other arms keep there is no longer a single decision"
            );
        }

        /// The over voltage monitor's two thresholds reach `set_limits` in the
        /// order `interfaces/over_voltage_monitor.yaml` declares them:
        /// `emergency_over_voltage_limit_V` first, then
        /// `error_over_voltage_limit_V`.
        ///
        /// Pinned as source text because `everestrs` generates that command
        /// with positional `f64` arguments, so there is no name to fill and no
        /// type that would reject a swap. The two are not interchangeable:
        /// emergency trips immediately and error only after the monitor's
        /// configured duration, so swapping them converts an immediate trip
        /// into a delayed one at the lower threshold and delays the higher one
        /// that exists to catch the failure of the first. Nothing else in this
        /// tree would notice, which is why this is pinned here and not left to
        /// the effect table.
        #[test]
        fn the_over_voltage_thresholds_reach_set_limits_emergency_first() {
            assert!(
                without_whitespace(production()).contains(&without_whitespace(
                    "monitor.set_limits(thresholds.emergency_v(), thresholds.error_v())"
                )),
                "the `set_limits` argument order moved; emergency must be first"
            );
        }

        /// Every arm's result flows through `Self::outcome`.
        ///
        /// The arms return whatever the generated publisher returns, and
        /// `Self::outcome` is what turns a failed publish into a reported
        /// failure. Wrapping the match in a block that discards the result and
        /// answers `EffectOutcome::Ok` compiles, keeps every arm in this table
        /// exactly as it is, and swallows every publish error on the way to the
        /// vehicle. So the wrapper is pinned and not just the arms inside it.
        #[test]
        fn the_dispatch_reports_what_each_publish_returned() {
            assert!(
                production().contains("Some(hlc) => Self::outcome(match update {"),
                "the `HlcUpdate` dispatch no longer routes its arms through \
                 `Self::outcome`, so a failed publish may be reported as success"
            );
        }
    }
}
