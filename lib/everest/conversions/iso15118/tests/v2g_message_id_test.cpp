// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <gtest/gtest.h>

#include <everest/conversions/iso15118/v2g_message_id.hpp>

namespace {

using Id = types::iso15118::V2gMessageId;

TEST(V2gMessageId, NamesIso2Responses) {
    using Type = iso15118::message_2::Type;
    EXPECT_EQ(module::conversions::to_v2g_message_id(Type::SessionSetupRes), Id::SessionSetupRes);
    EXPECT_EQ(module::conversions::to_v2g_message_id(Type::PaymentServiceSelectionRes), Id::PaymentServiceSelectionRes);
    EXPECT_EQ(module::conversions::to_v2g_message_id(Type::AuthorizationRes), Id::AuthorizationRes);
    EXPECT_EQ(module::conversions::to_v2g_message_id(Type::PowerDeliveryRes), Id::PowerDeliveryRes);
    EXPECT_EQ(module::conversions::to_v2g_message_id(Type::WeldingDetectionRes), Id::WeldingDetectionRes);
    EXPECT_EQ(module::conversions::to_v2g_message_id(Type::CertificateInstallationRes), Id::CertificateInstallationRes);
    EXPECT_EQ(module::conversions::to_v2g_message_id(Type::SessionStopRes), Id::SessionStopRes);
}

TEST(V2gMessageId, PreservesIso20Names) {
    using Type = iso15118::message_20::Type;
    EXPECT_EQ(module::conversions::to_v2g_message_id(Type::PowerDeliveryRes), Id::PowerDeliveryRes);
    EXPECT_EQ(module::conversions::to_v2g_message_id(Type::DC_WeldingDetectionRes), Id::DcWeldingDetectionRes);
    EXPECT_EQ(module::conversions::to_v2g_message_id(Type::DER_SAE_AC_ChargeLoopRes), Id::AcDerSaeChargeLoopRes);
}

TEST(V2gMessageId, NamesIso20CertificateInstallation) {
    using Type = iso15118::message_20::Type;
    EXPECT_EQ(module::conversions::to_v2g_message_id(Type::CertificateInstallationReq), Id::CertificateInstallationReq);
    EXPECT_EQ(module::conversions::to_v2g_message_id(Type::CertificateInstallationRes), Id::CertificateInstallationRes);
}

TEST(V2gMessageId, NamesDinResponses) {
    using Type = iso15118::message_din::Type;
    EXPECT_EQ(module::conversions::to_v2g_message_id(Type::SessionSetupRes), Id::SessionSetupRes);
    EXPECT_EQ(module::conversions::to_v2g_message_id(Type::ServicePaymentSelectionRes), Id::ServicePaymentSelectionRes);
    EXPECT_EQ(module::conversions::to_v2g_message_id(Type::ContractAuthenticationRes), Id::ContractAuthenticationRes);
    EXPECT_EQ(module::conversions::to_v2g_message_id(Type::PowerDeliveryRes), Id::PowerDeliveryRes);
    EXPECT_EQ(module::conversions::to_v2g_message_id(Type::WeldingDetectionRes), Id::WeldingDetectionRes);
    EXPECT_EQ(module::conversions::to_v2g_message_id(Type::SessionStopRes), Id::SessionStopRes);
}

TEST(V2gMessageId, MissingTypesRemainUnknown) {
    EXPECT_EQ(module::conversions::to_v2g_message_id(iso15118::message_20::Type::None), Id::UnknownMessage);
    EXPECT_EQ(module::conversions::to_v2g_message_id(iso15118::message_2::Type::None), Id::UnknownMessage);
    EXPECT_EQ(module::conversions::to_v2g_message_id(iso15118::message_din::Type::None), Id::UnknownMessage);
}

} // namespace
