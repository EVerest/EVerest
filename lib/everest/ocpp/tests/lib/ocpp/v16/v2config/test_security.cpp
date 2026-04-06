// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include "configuration_stub.hpp"

namespace {
using namespace ocpp::v16::stubs;

TEST_P(Configuration, DisableSecurityEventNotifications) {
    ASSERT_NE(get(), nullptr);

    // V16 gets a value from the schema file patches
    // initial values are from the JSON unit test config files
    EXPECT_FALSE(get()->getDisableSecurityEventNotifications());
    auto kv = get()->getDisableSecurityEventNotificationsKeyValue();
    EXPECT_EQ(kv.key, "DisableSecurityEventNotifications");
    EXPECT_EQ(kv.value, "false");
    EXPECT_FALSE(kv.readonly);

    get()->setDisableSecurityEventNotifications(true);
    EXPECT_TRUE(get()->getDisableSecurityEventNotifications());
    kv = get()->getDisableSecurityEventNotificationsKeyValue();
    EXPECT_EQ(kv.key, "DisableSecurityEventNotifications");
    EXPECT_EQ(kv.value, "true");
    EXPECT_FALSE(kv.readonly);
}

TEST_P(Configuration, SecurityProfile) {
    ASSERT_NE(get(), nullptr);
    // initial values are from the JSON unit test config files
    EXPECT_EQ(get()->getSecurityProfile(), 0);
    auto kv = get()->getSecurityProfileKeyValue();
    EXPECT_EQ(kv.key, "SecurityProfile");
    EXPECT_EQ(kv.value, "0");
    EXPECT_FALSE(kv.readonly);

    get()->setSecurityProfile(3);
    EXPECT_EQ(get()->getSecurityProfile(), 3);
    kv = get()->getSecurityProfileKeyValue();
    EXPECT_EQ(kv.key, "SecurityProfile");
    EXPECT_EQ(kv.value, "3");
    EXPECT_FALSE(kv.readonly);
}

TEST_P(Configuration, AuthorizationKey) {
    // notes: this one has some special code behind it

    ASSERT_NE(get(), nullptr);
    // initial values are from the JSON unit test config files

    // AuthorizationKey not set so we get the expected nullopt
    EXPECT_EQ(get()->getAuthorizationKey(), std::nullopt);

    // AuthorizationKey not set but a KeyValue is returned
    // rather than nullopt. kv.value is nullopt though
    auto kv = get()->getAuthorizationKeyKeyValue();
    ASSERT_TRUE(kv);
    EXPECT_EQ(kv.value().key, "AuthorizationKey");
    EXPECT_FALSE(kv.value().value.has_value());
    EXPECT_FALSE(kv.value().readonly);

    get()->setAuthorizationKey("01234567890123456789");
    // the correct key is returned
    EXPECT_EQ(get()->getAuthorizationKey(), "01234567890123456789");
    kv = get()->getAuthorizationKeyKeyValue();
    ASSERT_TRUE(kv);
    EXPECT_EQ(kv.value().key, "AuthorizationKey");
    // a dummy value is set to avoid leaking the key to the CSMS
    EXPECT_EQ(kv.value().value, "DummyAuthorizationKey");
    EXPECT_FALSE(kv.value().readonly);
}

TEST_P(Configuration, AuthorizationKeyHexDecoded) {
    // Per OCPP 1.6 spec errata E05, AuthorizationKey SHALL be hex-encoded.
    // Hex strings (even length, all hex chars) must be decoded consistently
    // regardless of whether the decoded bytes are printable.
    // See: https://github.com/EVerest/everest-core/issues/2034

    ASSERT_NE(get(), nullptr);

    // "746869735f69735f615f70617373776f72645f21" decodes to "this_is_a_password_!"
    // All decoded bytes are printable ASCII.
    get()->setAuthorizationKey("746869735f69735f615f70617373776f72645f21");
    EXPECT_EQ(get()->getAuthorizationKey(), "this_is_a_password_!");

    // "746869735f69735f615f70617373776f72645f01" decodes to "this_is_a_password_\x01"
    // The last byte (0x01) is non-printable. The old isHexNotation() rejected this
    // due to the printability check, storing the raw hex string instead of decoding.
    // Both keys use the same hex format and must be handled identically.
    std::string expected = std::string("this_is_a_password_") + '\x01';
    get()->setAuthorizationKey("746869735f69735f615f70617373776f72645f01");
    EXPECT_EQ(get()->getAuthorizationKey(), expected);
}

TEST_P(Configuration, AuthorizationKeyOddLengthNotHex) {
    // A string with odd length cannot be valid hex notation (each byte needs 2 chars)
    ASSERT_NE(get(), nullptr);
    get()->setAuthorizationKey("abcdef123");
    EXPECT_EQ(get()->getAuthorizationKey(), "abcdef123");
}

TEST_P(Configuration, CpoName) {
    ASSERT_NE(get(), nullptr);
    // initial values are from the JSON unit test config files
    EXPECT_EQ(get()->getCpoName(), std::nullopt);

    // CpoName not set but a KeyValue is returned
    // rather than nullopt. kv.value is nullopt though
    auto kv = get()->getCpoNameKeyValue();
    ASSERT_TRUE(kv);
    EXPECT_EQ(kv.value().key, "CpoName");
    EXPECT_FALSE(kv.value().value.has_value());
    EXPECT_FALSE(kv.value().readonly);

    get()->setCpoName("setCpoName");
    EXPECT_EQ(get()->getCpoName(), "setCpoName");
    kv = get()->getCpoNameKeyValue();
    ASSERT_TRUE(kv);
    EXPECT_EQ(kv.value().key, "CpoName");
    EXPECT_EQ(kv.value().value, "setCpoName");
    EXPECT_FALSE(kv.value().readonly);
}

TEST_P(Configuration, AdditionalRootCertificateCheck) {
    ASSERT_NE(get(), nullptr);
    EXPECT_EQ(get()->getAdditionalRootCertificateCheck(), std::nullopt);
    auto kv = get()->getAdditionalRootCertificateCheckKeyValue();
    ASSERT_FALSE(kv);
}

TEST_P(Configuration, CertificateSignedMaxChainSize) {
    ASSERT_NE(get(), nullptr);
    EXPECT_EQ(get()->getCertificateSignedMaxChainSize(), std::nullopt);
    auto kv = get()->getCertificateSignedMaxChainSizeKeyValue();
    ASSERT_FALSE(kv);
}

TEST_P(Configuration, CertificateStoreMaxLength) {
    ASSERT_NE(get(), nullptr);
    EXPECT_EQ(get()->getCertificateStoreMaxLength(), std::nullopt);
    auto kv = get()->getCertificateStoreMaxLengthKeyValue();
    ASSERT_FALSE(kv);
}

// -----------------------------------------------------------------------------
// Oneoff tests where there are differences between implementations
// Note: TEST_F and not TEST_P

TEST_F(Configuration, GetDisableSecurityEventNotificationsV16) {
    // this test should fail since DisableSecurityEventNotifications is not
    // in the JSON config file.
    // However there is a patch process that adds information from the
    // schema files
    // "Adding the following default values to the charge point configuration:"

    // TODO(james-ctc): the V2 implementation will need to consider
    // those additions when migrating data

    EXPECT_FALSE(v16_config->getDisableSecurityEventNotifications());
    auto kv = v16_config->getDisableSecurityEventNotificationsKeyValue();
    EXPECT_EQ(kv.key, "DisableSecurityEventNotifications");
    EXPECT_EQ(kv.value, "false");
    EXPECT_FALSE(kv.readonly);

    v16_config->setDisableSecurityEventNotifications(true);
    EXPECT_TRUE(v16_config->getDisableSecurityEventNotifications());
    kv = v16_config->getDisableSecurityEventNotificationsKeyValue();
    EXPECT_EQ(kv.key, "DisableSecurityEventNotifications");
    EXPECT_EQ(kv.value, "true");
    EXPECT_FALSE(kv.readonly);
}

TEST_F(Configuration, SetDisableSecurityEventNotificationsV2) {
    ASSERT_TRUE(device_model);
    device_model->clear("Security", "DisableSecurityEventNotifications");

    // correctly fails since the key doesn't exits
    EXPECT_ANY_THROW(v2_config->getDisableSecurityEventNotifications(););
    EXPECT_ANY_THROW(v2_config->getDisableSecurityEventNotificationsKeyValue(););

    // set an initial value
    device_model->set("Security", "DisableSecurityEventNotifications", "false");

    EXPECT_FALSE(v2_config->getDisableSecurityEventNotifications());
    auto kv = v2_config->getDisableSecurityEventNotificationsKeyValue();
    EXPECT_EQ(kv.key, "DisableSecurityEventNotifications");
    EXPECT_EQ(kv.value, "false");
    EXPECT_FALSE(kv.readonly);

    v2_config->setDisableSecurityEventNotifications(true);
    EXPECT_TRUE(v2_config->getDisableSecurityEventNotifications());
    kv = v2_config->getDisableSecurityEventNotificationsKeyValue();
    EXPECT_EQ(kv.key, "DisableSecurityEventNotifications");
    EXPECT_EQ(kv.value, "true");
    EXPECT_FALSE(kv.readonly);
}

TEST_F(Configuration, SetAdditionalRootCertificateCheckV2) {
    ASSERT_TRUE(device_model);
    // set an initial value
    device_model->set("Security", "AdditionalRootCertificateCheck", "");

    EXPECT_FALSE(v2_config->getAdditionalRootCertificateCheck().has_value());
    auto kv = v2_config->getAdditionalRootCertificateCheckKeyValue();
    ASSERT_TRUE(kv);
    EXPECT_EQ(kv.value().key, "AdditionalRootCertificateCheck");
    EXPECT_EQ(kv.value().value, "");
    EXPECT_TRUE(kv.value().readonly);

    device_model->set("Security", "AdditionalRootCertificateCheck", "false");

    EXPECT_TRUE(v2_config->getAdditionalRootCertificateCheck().has_value());
    EXPECT_FALSE(v2_config->getAdditionalRootCertificateCheck().value());

    kv = v2_config->getAdditionalRootCertificateCheckKeyValue();
    ASSERT_TRUE(kv);
    EXPECT_EQ(kv.value().key, "AdditionalRootCertificateCheck");
    EXPECT_EQ(kv.value().value, "false");
    EXPECT_TRUE(kv.value().readonly);
}

TEST_F(Configuration, GetCertificateSignedMaxChainSizeV2) {
    ASSERT_TRUE(device_model);
    // set an initial value
    device_model->set("Security", "CertificateSignedMaxChainSize", "5");

    EXPECT_EQ(v2_config->getCertificateSignedMaxChainSize(), 5);
    auto kv = v2_config->getCertificateSignedMaxChainSizeKeyValue();
    ASSERT_TRUE(kv);
    EXPECT_EQ(kv.value().key, "CertificateSignedMaxChainSize");
    EXPECT_EQ(kv.value().value, "5");
    EXPECT_TRUE(kv.value().readonly);
}

TEST_F(Configuration, GetCertificateStoreMaxLengthV2) {
    ASSERT_TRUE(device_model);
    // set an initial value
    device_model->set("Security", "CertificateStoreMaxLength", "512");

    EXPECT_EQ(v2_config->getCertificateStoreMaxLength(), 512);
    auto kv = v2_config->getCertificateStoreMaxLengthKeyValue();
    ASSERT_TRUE(kv);
    EXPECT_EQ(kv.value().key, "CertificateStoreMaxLength");
    EXPECT_EQ(kv.value().value, "512");
    EXPECT_TRUE(kv.value().readonly);
}

} // namespace
