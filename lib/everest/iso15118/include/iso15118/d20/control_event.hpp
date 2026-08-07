// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <iso15118/d20/ac_powers.hpp>
#include <iso15118/d20/dynamic_mode_parameters.hpp>
#include <iso15118/d20/limits.hpp>

namespace iso15118::d20 {
class CableCheckFinished {
public:
    explicit CableCheckFinished(bool success_) : success(success_) {
    }

    operator bool() const {
        return success;
    }

private:
    bool success;
};

struct PresentVoltageCurrent {
    float voltage;
    float current;
};

// Latest meter reading pushed by the module (from types::powermeter::Powermeter), once per powermeter
// update. Latched on the session context and reported as the MeterInfo element of the charge-loop
// responses; the SECC must have sent one when it requests a signed MeteringReceipt ([V2G2-902]).
struct MeterInfo {
    std::string meter_id;
    uint64_t meter_reading_wh{0};
};

class AuthorizationResponse {
public:
    explicit AuthorizationResponse(bool authorized_, bool certificate_revoked_ = false) :
        authorized(authorized_), certificate_revoked(certificate_revoked_) {
    }

    operator bool() const {
        return authorized;
    }

    // ISO 15118-2 Plug-and-Charge: a rejection because the contract certificate is revoked is answered
    // with AuthorizationRes/FAILED_CertificateRevoked instead of a plain FAILED (EvseV2G parity). Only
    // meaningful when authorized is false; the other protocols ignore it.
    bool is_certificate_revoked() const {
        return certificate_revoked;
    }

private:
    bool authorized;
    bool certificate_revoked;
};

class StopCharging {
public:
    explicit StopCharging(bool stop_) : stop(stop_) {
    }

    operator bool() const {
        return stop;
    }

private:
    bool stop;
};

class PauseCharging {
public:
    explicit PauseCharging(bool pause_) : pause(pause_) {
    }

    operator bool() const {
        return pause;
    }

private:
    bool pause;
};

using EnergyServices = std::vector<message_20::datatypes::ServiceCategory>;

class ClosedContactor {
public:
    explicit ClosedContactor(bool closed_) : closed(closed_) {
    }

    operator bool() const {
        return closed;
    }

private:
    bool closed;
};

// TODO(SL): Define this globally for message and states
using SupportedVASs = std::vector<uint16_t>;

// ISO 15118-2 Plug-and-Charge CertificateInstallation relay: the module injects the raw
// CertificateInstallationRes EXI (base64, as delivered by the CSMS/CPS backend over the
// iso15118_extensions interface) back into the d2 SECC engine, which splices it onto the wire verbatim.
struct CertificateResponse {
    bool status_accepted{false};
    std::string exi_response_base64{};
};

// EVSE-side error reported by the module (mirrors types::iso15118::EvseError). Consumed by the SECC
// engines: Malfunction / UtilityInterruptEvent stamp the corresponding EVSEStatusCode into the DC charge
// responses, RCD sets the AC RCD flag, EmergencyShutdown aborts the session, and None clears an active
// error (reset). Contactor is informational (no wire effect), matching EvseV2G.
enum class EvseErrorCode : uint8_t {
    None,
    Contactor,
    RCD,
    UtilityInterruptEvent,
    Malfunction,
    EmergencyShutdown,
};

struct EvseError {
    EvseErrorCode code{EvseErrorCode::None};
};

// IEC 61851-1 CP state as measured by the EVSE (mirrors types::iso15118::CpState). Consumed by the
// SECC engines for the CP checks tied to the message sequence, e.g. DIN 70121 [V2G-DC-988]/
// [V2G-DC-556]: CP State B within V2G_SECC_CPState_Detection_Timeout after the request following
// PowerDelivery(off), otherwise FAILED response + oscillator off + TCP close.
enum class CpState : uint8_t {
    A,
    B,
    C,
    D,
    E,
    F,
};

struct CpStateChanged {
    CpState state{CpState::A};
};

// EVSE physical parameters pushed by the module (EvseManager's set_charging_parameters cmd, mirroring
// types::iso15118::SetupPhysicalValues). Consumed by the ISO 15118-2 and DIN SPEC 70121 SECC engines for
// the AC/DC EVSEChargeParameter elements of ChargeParameterDiscoveryRes; ISO 15118-20 carries the same
// information in its own limit structures and ignores this. Every field is optional on its own -- an
// absent one leaves the engine default in place.
struct PhysicalValues {
    std::optional<float> ac_nominal_voltage;
    std::optional<float> dc_current_regulation_tolerance;
    std::optional<float> dc_peak_current_ripple;
    std::optional<float> dc_energy_to_be_delivered;
};

// The charger has no energy available and asks the SECC to pause the session before charging starts
// (IEC 61851-23:2023 CC.3.5.3, EvseManager's no_energy_pause_charging cmd). Mirrors
// types::iso15118::NoEnergyPauseMode plus a None state for "no pause requested".
enum class NoEnergyPauseMode : uint8_t {
    None,
    // Pause before the cable check: the charger has no power at all for this session.
    BeforeCableCheck,
    // The charger can still run cable check and pre-charge, but must not start the charge loop.
    AfterCableCheckPreCharge,
    // Signal the pause to the EV but tolerate an EV that ignores it and charges anyway (against
    // IEC 61851-23:2023).
    AllowEvToIgnorePause,
};

struct NoEnergyPause {
    NoEnergyPauseMode mode{NoEnergyPauseMode::None};
};

// Isolation-monitoring result reported by the module (EvseManager's update_isolation_status cmd,
// mirroring types::iso15118::IsolationStatus). Consumed by the ISO 15118-2 and DIN SPEC 70121 SECC
// engines, which report it as DC_EVSEStatus.EVSEIsolationStatus in the DC responses that follow the
// cable check. ISO 15118-20 has no such element.
enum class IsolationStatus : uint8_t {
    Invalid,
    Valid,
    Warning,
    Fault,
    // No insulation monitoring device fitted, so the cable check was skipped. ISO 15118-2 has a
    // No_IMD enumerator for this; DIN SPEC 70121 does not and reports Valid instead.
    NoImd,
};

struct UpdateIsolationStatus {
    IsolationStatus status{IsolationStatus::Invalid};
};

using ControlEvent =
    std::variant<CableCheckFinished, PresentVoltageCurrent, MeterInfo, AuthorizationResponse, StopCharging,
                 PauseCharging, DcTransferLimits, AcTransferLimits, UpdateDynamicModeParameters, ClosedContactor,
                 AcTargetPower, AcPresentPower, EnergyServices, SupportedVASs, CertificateResponse, EvseError,
                 CpStateChanged, PhysicalValues, NoEnergyPause, UpdateIsolationStatus>;

} // namespace iso15118::d20
