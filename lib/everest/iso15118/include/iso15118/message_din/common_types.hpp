// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <iso15118/message/shared_datatypes.hpp>

struct din_PhysicalValueType;

namespace iso15118::message_din {

template <typename InType, typename OutType> void convert(const InType&, OutType&);

template <typename T> constexpr auto to_underlying_value(T t) {
    return static_cast<std::underlying_type_t<T>>(t);
}

namespace datatypes {

// Datatypes shared verbatim with ISO 15118-2 (see iso15118::shared_datatypes).
using ::iso15118::shared_datatypes::SESSION_ID_LENGTH;
using ::iso15118::shared_datatypes::SessionId;

using EvccId = std::vector<uint8_t>; // hexBinary, MaxLength: 8 (MAC)
using EvseId = std::vector<uint8_t>; // hexBinary, MaxLength: 32

enum class ResponseCode {
    OK = 0,
    OK_NewSessionEstablished = 1,
    OK_OldSessionJoined = 2,
    OK_CertificateExpiresSoon = 3,
    FAILED = 4,
    FAILED_SequenceError = 5,
    FAILED_ServiceIDInvalid = 6,
    FAILED_UnknownSession = 7,
    FAILED_ServiceSelectionInvalid = 8,
    FAILED_PaymentSelectionInvalid = 9,
    FAILED_CertificateExpired = 10,
    FAILED_SignatureError = 11,
    FAILED_NoCertificateAvailable = 12,
    FAILED_CertChainError = 13,
    FAILED_ChallengeInvalid = 14,
    FAILED_ContractCanceled = 15,
    FAILED_WrongChargeParameter = 16,
    FAILED_PowerDeliveryNotApplied = 17,
    FAILED_TariffSelectionInvalid = 18,
    FAILED_ChargingProfileInvalid = 19,
    FAILED_EVSEPresentVoltageToLow = 20,
    FAILED_MeteringSignatureNotValid = 21,
    FAILED_WrongEnergyTransferType = 22,
};

enum class EvseProcessing {
    Finished = 0,
    Ongoing = 1,
};

using ::iso15118::shared_datatypes::PaymentOption;

using ::iso15118::shared_datatypes::ServiceCategory;

using ::iso15118::shared_datatypes::EnergyTransferMode;

enum class SupportedEnergyTransferMode {
    AC_single_phase_core = 0,
    AC_three_phase_core = 1,
    DC_core = 2,
    DC_extended = 3,
    DC_combo_core = 4,
    DC_dual = 5,
    AC_core1p_DC_extended = 6,
    AC_single_DC_core = 7,
    AC_single_phase_three_phase_core_DC_extended = 8,
    AC_core3p_DC_extended = 9,
};

enum class IsolationLevel {
    Invalid = 0,
    Valid = 1,
    Warning = 2,
    Fault = 3,
};

enum class DcEvseStatusCode {
    EVSE_NotReady = 0,
    EVSE_Ready = 1,
    EVSE_Shutdown = 2,
    EVSE_UtilityInterruptEvent = 3,
    EVSE_IsolationMonitoringActive = 4,
    EVSE_EmergencyShutdown = 5,
    EVSE_Malfunction = 6,
    Reserved_8 = 7,
    Reserved_9 = 8,
    Reserved_A = 9,
    Reserved_B = 10,
    Reserved_C = 11,
};

using ::iso15118::shared_datatypes::DcEvErrorCode;

using ::iso15118::shared_datatypes::EvseNotification;

enum class Unit {
    h = 0,
    m = 1,
    s = 2,
    A = 3,
    Ah = 4,
    V = 5,
    VA = 6,
    W = 7,
    W_s = 8,
    Wh = 9,
};

struct DcEvStatus {
    bool ev_ready{false};
    DcEvErrorCode ev_error_code{DcEvErrorCode::NO_ERROR};
    int8_t ev_ress_soc{0};
    std::optional<bool> ev_cabin_conditioning;
    std::optional<bool> ev_ress_conditioning;
};

struct DcEvseStatus {
    std::optional<IsolationLevel> evse_isolation_status;
    DcEvseStatusCode evse_status_code{DcEvseStatusCode::EVSE_NotReady};
    uint32_t notification_max_delay{0};
    EvseNotification evse_notification{EvseNotification::None};
};

struct AcEvseStatus {
    bool power_switch_closed{false};
    bool rcd{false};
    uint32_t notification_max_delay{0};
    EvseNotification evse_notification{EvseNotification::None};
};

} // namespace datatypes

// helpers to convert between a physical quantity (as double) and the codec PhysicalValueType.
// to_physical_value always sets Unit_isUsed=1, from_physical_value tolerates Unit_isUsed==0.
::din_PhysicalValueType to_physical_value(double value, datatypes::Unit unit);
double from_physical_value(const ::din_PhysicalValueType& in);

struct Header {
    datatypes::SessionId session_id{};
    // notification and signature skipped
};

template <typename cb_HeaderType> void convert(const cb_HeaderType& in, Header& out);

} // namespace iso15118::message_din
