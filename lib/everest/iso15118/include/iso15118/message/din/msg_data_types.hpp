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

struct Notification {
    FaultCode fault_code;
    std::optional<std::string> fault_msg{std::nullopt};
};

} // namespace data_types

struct Header {
    data_types::SESSION_ID session_id;
    std::optional<data_types::Notification> notification{std::nullopt};
    // TODO: Missing xml signature
};

void convert(const struct din_MessageHeaderType& in, Header& out);
void convert(const Header& in, struct din_MessageHeaderType& out);

} // namespace iso15118::din::msg
