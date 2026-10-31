// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <everest/conversions/iso15118/v2g_message_id.hpp>

#include <variant>

namespace module::conversions {

types::iso15118::V2gMessageId to_v2g_message_id(iso15118::message_20::Type type) {

    using Type = iso15118::message_20::Type;
    using Id = types::iso15118::V2gMessageId;

    switch (type) {
    case Type::None:
        return Id::UnknownMessage;
    case Type::SupportedAppProtocolReq:
        return Id::SupportedAppProtocolReq;
    case Type::SupportedAppProtocolRes:
        return Id::SupportedAppProtocolRes;
    case Type::SessionSetupReq:
        return Id::SessionSetupReq;
    case Type::SessionSetupRes:
        return Id::SessionSetupRes;
    case Type::AuthorizationSetupReq:
        return Id::AuthorizationSetupReq;
    case Type::AuthorizationSetupRes:
        return Id::AuthorizationSetupRes;
    case Type::AuthorizationReq:
        return Id::AuthorizationReq;
    case Type::AuthorizationRes:
        return Id::AuthorizationRes;
    case Type::CertificateInstallationReq:
        return Id::CertificateInstallationReq;
    case Type::CertificateInstallationRes:
        return Id::CertificateInstallationRes;
    case Type::ServiceDiscoveryReq:
        return Id::ServiceDiscoveryReq;
    case Type::ServiceDiscoveryRes:
        return Id::ServiceDiscoveryRes;
    case Type::ServiceDetailReq:
        return Id::ServiceDetailReq;
    case Type::ServiceDetailRes:
        return Id::ServiceDetailRes;
    case Type::ServiceSelectionReq:
        return Id::ServiceSelectionReq;
    case Type::ServiceSelectionRes:
        return Id::ServiceSelectionRes;
    case Type::DC_ChargeParameterDiscoveryReq:
        return Id::DcChargeParameterDiscoveryReq;
    case Type::DC_ChargeParameterDiscoveryRes:
        return Id::DcChargeParameterDiscoveryRes;
    case Type::ScheduleExchangeReq:
        return Id::ScheduleExchangeReq;
    case Type::ScheduleExchangeRes:
        return Id::ScheduleExchangeRes;
    case Type::DC_CableCheckReq:
        return Id::DcCableCheckReq;
    case Type::DC_CableCheckRes:
        return Id::DcCableCheckRes;
    case Type::DC_PreChargeReq:
        return Id::DcPreChargeReq;
    case Type::DC_PreChargeRes:
        return Id::DcPreChargeRes;
    case Type::PowerDeliveryReq:
        return Id::PowerDeliveryReq;
    case Type::PowerDeliveryRes:
        return Id::PowerDeliveryRes;
    case Type::DC_ChargeLoopReq:
        return Id::DcChargeLoopReq;
    case Type::DC_ChargeLoopRes:
        return Id::DcChargeLoopRes;
    case Type::DC_WeldingDetectionReq:
        return Id::DcWeldingDetectionReq;
    case Type::DC_WeldingDetectionRes:
        return Id::DcWeldingDetectionRes;
    case Type::SessionStopReq:
        return Id::SessionStopReq;
    case Type::SessionStopRes:
        return Id::SessionStopRes;
    case Type::AC_ChargeParameterDiscoveryReq:
        return Id::AcChargeParameterDiscoveryReq;
    case Type::AC_ChargeParameterDiscoveryRes:
        return Id::AcChargeParameterDiscoveryRes;
    case Type::AC_ChargeLoopReq:
        return Id::AcChargeLoopReq;
    case Type::AC_ChargeLoopRes:
        return Id::AcChargeLoopRes;
    case Type::DER_AC_ChargeParameterDiscoveryReq:
        return Id::AcDerChargeParameterDiscoveryReq;
    case Type::DER_AC_ChargeParameterDiscoveryRes:
        return Id::AcDerChargeParameterDiscoveryRes;
    case Type::DER_AC_ChargeLoopReq:
        return Id::AcDerChargeLoopReq;
    case Type::DER_AC_ChargeLoopRes:
        return Id::AcDerChargeLoopRes;
    case Type::DER_SAE_AC_ChargeParameterDiscoveryReq:
        return Id::AcDerSaeChargeParameterDiscoveryReq;
    case Type::DER_SAE_AC_ChargeParameterDiscoveryRes:
        return Id::AcDerSaeChargeParameterDiscoveryRes;
    case Type::DER_SAE_AC_ChargeLoopReq:
        return Id::AcDerSaeChargeLoopReq;
    case Type::DER_SAE_AC_ChargeLoopRes:
        return Id::AcDerSaeChargeLoopRes;
    }

    return Id::UnknownMessage;
}

// ISO 15118-2 message type -> V2gMessageId. The pre-20 names map 1:1 onto the shared interface enum.
types::iso15118::V2gMessageId to_v2g_message_id(iso15118::message_2::Type type) {

    using Type = iso15118::message_2::Type;
    using Id = types::iso15118::V2gMessageId;

    switch (type) {
    case Type::None:
        return Id::UnknownMessage;
    case Type::SessionSetupReq:
        return Id::SessionSetupReq;
    case Type::SessionSetupRes:
        return Id::SessionSetupRes;
    case Type::ServiceDiscoveryReq:
        return Id::ServiceDiscoveryReq;
    case Type::ServiceDiscoveryRes:
        return Id::ServiceDiscoveryRes;
    case Type::ServiceDetailReq:
        return Id::ServiceDetailReq;
    case Type::ServiceDetailRes:
        return Id::ServiceDetailRes;
    case Type::PaymentServiceSelectionReq:
        return Id::PaymentServiceSelectionReq;
    case Type::PaymentServiceSelectionRes:
        return Id::PaymentServiceSelectionRes;
    case Type::PaymentDetailsReq:
        return Id::PaymentDetailsReq;
    case Type::PaymentDetailsRes:
        return Id::PaymentDetailsRes;
    case Type::AuthorizationReq:
        return Id::AuthorizationReq;
    case Type::AuthorizationRes:
        return Id::AuthorizationRes;
    case Type::ChargeParameterDiscoveryReq:
        return Id::ChargeParameterDiscoveryReq;
    case Type::ChargeParameterDiscoveryRes:
        return Id::ChargeParameterDiscoveryRes;
    case Type::PowerDeliveryReq:
        return Id::PowerDeliveryReq;
    case Type::PowerDeliveryRes:
        return Id::PowerDeliveryRes;
    case Type::ChargingStatusReq:
        return Id::ChargingStatusReq;
    case Type::ChargingStatusRes:
        return Id::ChargingStatusRes;
    case Type::CableCheckReq:
        return Id::CableCheckReq;
    case Type::CableCheckRes:
        return Id::CableCheckRes;
    case Type::PreChargeReq:
        return Id::PreChargeReq;
    case Type::PreChargeRes:
        return Id::PreChargeRes;
    case Type::CurrentDemandReq:
        return Id::CurrentDemandReq;
    case Type::CurrentDemandRes:
        return Id::CurrentDemandRes;
    case Type::WeldingDetectionReq:
        return Id::WeldingDetectionReq;
    case Type::WeldingDetectionRes:
        return Id::WeldingDetectionRes;
    case Type::SessionStopReq:
        return Id::SessionStopReq;
    case Type::SessionStopRes:
        return Id::SessionStopRes;
    case Type::MeteringReceiptReq:
        return Id::MeteringReceiptReq;
    case Type::MeteringReceiptRes:
        return Id::MeteringReceiptRes;
    case Type::CertificateInstallationReq:
        return Id::CertificateInstallationReq;
    case Type::CertificateInstallationRes:
        return Id::CertificateInstallationRes;
    case Type::CertificateUpdateReq:
        return Id::CertificateUpdateReq;
    case Type::CertificateUpdateRes:
        return Id::CertificateUpdateRes;
    }

    return Id::UnknownMessage;
}

// DIN SPEC 70121 message type -> V2gMessageId.
types::iso15118::V2gMessageId to_v2g_message_id(iso15118::message_din::Type type) {

    using Type = iso15118::message_din::Type;
    using Id = types::iso15118::V2gMessageId;

    switch (type) {
    case Type::None:
        return Id::UnknownMessage;
    case Type::SessionSetupReq:
        return Id::SessionSetupReq;
    case Type::SessionSetupRes:
        return Id::SessionSetupRes;
    case Type::ServiceDiscoveryReq:
        return Id::ServiceDiscoveryReq;
    case Type::ServiceDiscoveryRes:
        return Id::ServiceDiscoveryRes;
    case Type::ServicePaymentSelectionReq:
        return Id::ServicePaymentSelectionReq;
    case Type::ServicePaymentSelectionRes:
        return Id::ServicePaymentSelectionRes;
    case Type::ContractAuthenticationReq:
        return Id::ContractAuthenticationReq;
    case Type::ContractAuthenticationRes:
        return Id::ContractAuthenticationRes;
    case Type::ChargeParameterDiscoveryReq:
        return Id::ChargeParameterDiscoveryReq;
    case Type::ChargeParameterDiscoveryRes:
        return Id::ChargeParameterDiscoveryRes;
    case Type::CableCheckReq:
        return Id::CableCheckReq;
    case Type::CableCheckRes:
        return Id::CableCheckRes;
    case Type::PreChargeReq:
        return Id::PreChargeReq;
    case Type::PreChargeRes:
        return Id::PreChargeRes;
    case Type::PowerDeliveryReq:
        return Id::PowerDeliveryReq;
    case Type::PowerDeliveryRes:
        return Id::PowerDeliveryRes;
    case Type::CurrentDemandReq:
        return Id::CurrentDemandReq;
    case Type::CurrentDemandRes:
        return Id::CurrentDemandRes;
    case Type::WeldingDetectionReq:
        return Id::WeldingDetectionReq;
    case Type::WeldingDetectionRes:
        return Id::WeldingDetectionRes;
    case Type::SessionStopReq:
        return Id::SessionStopReq;
    case Type::SessionStopRes:
        return Id::SessionStopRes;
    }

    return Id::UnknownMessage;
}

types::iso15118::V2gMessageId to_v2g_message_id(const iso15118::V2gMessageType& type) {
    return std::visit([](auto concrete) { return to_v2g_message_id(concrete); }, type);
}

} // namespace module::conversions
