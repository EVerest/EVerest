// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

#include <cbv2g/din/din_msgDefDatatypes.h>

namespace iso15118::din::msg {

template <typename InType, typename OutType> void convert(const InType&, OutType&);

namespace data_types {

enum class ResponseCode : uint8_t {
    OK,
    OK_NewSessionEstablished,
    OK_OldSessionJoined,
    OK_CertificateExpiresSoon,
    FAILED,
    FAILED_SequenceError,
    FAILED_ServiceIDInvalid,
    FAILED_UnknownSession,
    FAILED_ServiceSelectionInvalid,
    FAILED_PaymentSelectionInvalid,
    FAILED_CertificateExpired,
    FAILED_SignatureError,
    FAILED_NoCertificateAvailable,
    FAILED_CertChainError,
    FAILED_ChallengeInvalid,
    FAILED_ContractCanceled,
    FAILED_WrongChargeParameter,
    FAILED_PowerDeliveryNotApplied,
    FAILED_TariffSelectionInvalid,
    FAILED_ChargingProfileInvalid,
    FAILED_EVSEPresentVoltageToLow,
    FAILED_MeteringSignatureNotValid,
    FAILED_WrongEnergyTransferType
};

constexpr auto SESSION_ID_LENGTH = 8;
using SESSION_ID = std::array<uint8_t, SESSION_ID_LENGTH>; // hexBinary, max length 8

using ServiceScope = std::string; // MaxLength: 32
enum class ServiceCategory : uint8_t {
    EVCharging,
    Internet,
    ContractCertificate,
    OtherCustom,
};

using ServiceID = uint16_t;
using ServiceName = std::string; // MaxLength: 32;

struct ServiceTag {
    ServiceID id;
    ServiceCategory category;
    std::optional<ServiceName> name{std::nullopt};
    std::optional<ServiceScope> scope{std::nullopt};
};

struct Service {
    ServiceTag service_tag;
    bool free_service;
};

enum class FaultCode {
    ParsingError,
    NoTLSRootCertificatAvailable,
    UnknownError,
};

enum class PaymentOption : uint8_t {
    Contract,
    ExternalPayment,
};

enum class EvseProcessing : uint8_t {
    Finished,
    Ongoing,
};

using GenChallenge = std::string;
using IDREF = std::string;

using SAScheduleTupleID = int16_t; // [1-255]

enum class DcEvErrorCode : uint8_t {
    NO_ERROR,
    FAILED_RESSTemperatureInhibit,
    FAILED_EVShiftPosition,
    FAILED_ChargerConnectorLockFault,
    FAILED_EVRESSMalfunction,
    FAILED_ChargingCurrentdifferential,
    FAILED_ChargingVoltageOutOfRange,
    Reserved_A,
    Reserved_B,
    Reserved_C,
    FAILED_ChargingSystemIncompatibility,
    NoData,
};

using PercentValue = uint8_t; // [0 - 100]

// struct DcEvStatus {
//     bool ev_ready;
//     std::optional<bool> ev_cabin_conditioning;
//     std::optional<bool> ev_ress_conditioning;
//     DcEvErrorCode ev_error_code;
//     PercentValue ev_ress_soc;
// };

enum class EvseNotification : uint8_t {
    None,
    StopCharging,
    ReNegotiation,
};

enum class DcEvseStatusCode : uint8_t {
    EVSE_NotReady,
    EVSE_Ready,
    EVSE_Shutdown,
    EVSE_UtilityInterruptEvent,
    EVSE_IsolationMonitoringActive,
    EVSE_EmergencyShutdown,
    EVSE_Malfunction,
    Reserved_8,
    Reserved_9,
    Reserved_A,
    Reserved_B,
    Reserved_C
};

enum class IsolationLevel : uint8_t {
    Invalid,
    Valid,
    Warning,
    Fault,
};

struct DcEvStatus {
    bool ev_ready;
    std::optional<bool> ev_cabin_conditioning;
    std::optional<bool> ev_ress_conditioning;
    DcEvErrorCode ev_error_code;
    PercentValue ev_ress_soc;
};

struct EvseStatus {
    uint16_t notification_max_delay{0};
    EvseNotification evse_notification{EvseNotification::None};
};

struct AcEvseStatus : EvseStatus {
    bool rcd;
    bool power_switch_closed;
};

struct DcEvseStatus : EvseStatus {
    std::optional<IsolationLevel> evse_isolation_status;
    DcEvseStatusCode evse_status_code;
};

enum class UnitSymbol : uint8_t {
    h,
    m,
    s,
    A,
    Ah,
    V,
    VA,
    W,
    Ws,
    Wh
};

struct PhysicalValue {
    int16_t value{0};
    int8_t multiplier{0}; // [-3 - 3]
    std::optional<UnitSymbol> unit{std::nullopt};
};

struct Notification {
    FaultCode fault_code;
    std::optional<std::string> fault_msg{std::nullopt};
};

float from_PhysicalValue(const PhysicalValue& in);
PhysicalValue from_float(const float in, const data_types::UnitSymbol unit);

} // namespace data_types

struct Header {
    data_types::SESSION_ID session_id;
    std::optional<data_types::Notification> notification{std::nullopt};
    // TODO: Missing xml signature
};

void convert(const struct din_MessageHeaderType& in, Header& out);
void convert(const Header& in, struct din_MessageHeaderType& out);

} // namespace iso15118::din::msg
