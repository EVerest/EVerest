// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

#include <iso15118/message/shared_datatypes.hpp>

namespace iso15118::message_2 {

template <typename InType, typename OutType> void convert(const InType&, OutType&);

template <typename T> constexpr auto to_underlying_value(T t) {
    return static_cast<std::underlying_type_t<T>>(t);
}

namespace datatypes {

// Datatypes shared verbatim with DIN SPEC 70121 (see iso15118::shared_datatypes).
using ::iso15118::shared_datatypes::SESSION_ID_LENGTH;
using ::iso15118::shared_datatypes::SessionId;

static constexpr auto EVCC_ID_LENGTH = 6;
using EvccId = std::array<uint8_t, EVCC_ID_LENGTH>;

static constexpr auto GEN_CHALLENGE_LENGTH = 16;
using GenChallenge = std::array<uint8_t, GEN_CHALLENGE_LENGTH>;

using MeterSignature = std::array<uint8_t, 64>;

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
    FAILED_MeteringSignatureNotValid = 20,
    FAILED_NoChargeServiceSelected = 21,
    FAILED_WrongEnergyTransferMode = 22,
    FAILED_ContactorError = 23,
    FAILED_CertificateNotAllowedAtThisEVSE = 24,
    FAILED_CertificateRevoked = 25,
};

enum class EVSEProcessing {
    Finished = 0,
    Ongoing = 1,
    Ongoing_WaitingForCustomerInteraction = 2,
};

using ::iso15118::shared_datatypes::PaymentOption;

using ::iso15118::shared_datatypes::EnergyTransferMode;

using ::iso15118::shared_datatypes::ServiceCategory;

enum class ChargingSession {
    Terminate = 0,
    Pause = 1,
};

enum class ChargeProgress {
    Start = 0,
    Stop = 1,
    Renegotiate = 2,
};

enum class Unit {
    h = 0,
    m = 1,
    s = 2,
    A = 3,
    V = 4,
    W = 5,
    Wh = 6,
};

using DC_EVErrorCode = ::iso15118::shared_datatypes::DcEvErrorCode;

enum class DC_EVSEStatusCode {
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

enum class IsolationLevel {
    Invalid = 0,
    Valid = 1,
    Warning = 2,
    Fault = 3,
    No_IMD = 4,
};

using EVSENotification = ::iso15118::shared_datatypes::EvseNotification;

enum class FaultCode {
    ParsingError = 0,
    NoTLSRootCertificatAvailable = 1,
    UnknownError = 2,
};

struct PhysicalValue {
    int16_t value{0};
    int8_t multiplier{0};
    Unit unit{Unit::A};
};

struct DC_EVStatus {
    bool ev_ready{false};
    DC_EVErrorCode ev_error_code{DC_EVErrorCode::NO_ERROR};
    int8_t ev_ress_soc{0};
};

struct DC_EVSEStatus {
    uint16_t notification_max_delay{0};
    EVSENotification notification{EVSENotification::None};
    std::optional<IsolationLevel> isolation_status;
    DC_EVSEStatusCode status_code{DC_EVSEStatusCode::EVSE_Ready};
};

struct AC_EVSEStatus {
    uint16_t notification_max_delay{0};
    EVSENotification notification{EVSENotification::None};
    bool rcd{false};
};

struct Notification {
    FaultCode fault_code{FaultCode::UnknownError};
    std::optional<std::string> fault_msg;
};

struct MeterInfo {
    std::string meter_id;
    std::optional<uint64_t> meter_reading;
    std::optional<MeterSignature> sig_meter_reading;
    std::optional<int16_t> meter_status;
    std::optional<int64_t> t_meter;
};

double from_physical_value(const PhysicalValue& in);
PhysicalValue to_physical_value(double value, Unit unit);

} // namespace datatypes

struct Header {
    datatypes::SessionId session_id{};
    std::optional<datatypes::Notification> notification;
    // Signature skipped: EIM only, tolerated on decode
};

template <typename cb_HeaderType> void convert(const cb_HeaderType& in, Header& out);
template <typename cb_HeaderType> void convert(const Header& in, cb_HeaderType& out);

template <typename cb_PhysicalValueType> void convert(const cb_PhysicalValueType& in, datatypes::PhysicalValue& out);
template <typename cb_PhysicalValueType> void convert(const datatypes::PhysicalValue& in, cb_PhysicalValueType& out);

} // namespace iso15118::message_2
