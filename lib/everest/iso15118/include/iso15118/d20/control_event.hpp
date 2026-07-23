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

// Latest meter reading pushed by the module (from types::powermeter::Powermeter). Consumed by the charge
// loop states to populate the MeterInfo element, which the SECC must send when it requests a signed
// MeteringReceipt ([V2G2-902]).
struct MeterInfo {
    std::string meter_id;
    uint64_t meter_reading_wh{0};
};

class AuthorizationResponse {
public:
    explicit AuthorizationResponse(bool authorized_) : authorized(authorized_) {
    }

    operator bool() const {
        return authorized;
    }

private:
    bool authorized;
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

using ControlEvent = std::variant<CableCheckFinished, PresentVoltageCurrent, MeterInfo, AuthorizationResponse,
                                  StopCharging, PauseCharging, DcTransferLimits, AcTransferLimits,
                                  UpdateDynamicModeParameters, ClosedContactor, AcTargetPower, AcPresentPower,
                                  EnergyServices, SupportedVASs, CertificateResponse, EvseError, CpStateChanged>;

} // namespace iso15118::d20
