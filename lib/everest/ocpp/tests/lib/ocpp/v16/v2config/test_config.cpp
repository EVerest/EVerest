// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include "configuration_stub.hpp"
#include "ocpp/v16/known_keys.hpp"
#include "ocpp/v2/ocpp_enums.hpp"
#include "ocpp/v2/ocpp_types.hpp"
#include <ocpp/v16/charge_point_configuration_base.hpp>
#include <optional>

namespace {
using namespace ocpp::v16::stubs;

// run tests against V16 JSON and V2 database
// gtest_filter: Config/Configuration.*
INSTANTIATE_TEST_SUITE_P(Config, Configuration, testing::Values("sql", "json"));
INSTANTIATE_TEST_SUITE_P(Config, ConfigurationFull, testing::Values("sql", "json"));

TEST(ConnectorID, Extract) {
    using CPCB = ocpp::v16::ChargePointConfigurationBase;

    EXPECT_EQ(CPCB::extractConnectorIdFromMeterPublicKey(""), std::nullopt);
    EXPECT_EQ(CPCB::extractConnectorIdFromMeterPublicKey("1234"), std::nullopt);
    EXPECT_EQ(CPCB::extractConnectorIdFromMeterPublicKey("A"), std::nullopt);
    EXPECT_EQ(CPCB::extractConnectorIdFromMeterPublicKey("ABC"), std::nullopt);
    EXPECT_EQ(CPCB::extractConnectorIdFromMeterPublicKey("A1"), std::nullopt);
    EXPECT_EQ(CPCB::extractConnectorIdFromMeterPublicKey("A12"), std::nullopt);
    EXPECT_EQ(CPCB::extractConnectorIdFromMeterPublicKey("A123"), std::nullopt);
    EXPECT_EQ(CPCB::extractConnectorIdFromMeterPublicKey("MeterPublicKeyMeterPublicKey123"), std::nullopt);
    EXPECT_EQ(CPCB::extractConnectorIdFromMeterPublicKey("MeterPublicKey1"), 1);
    EXPECT_EQ(CPCB::extractConnectorIdFromMeterPublicKey("MeterPublicKey12"), 12);
    EXPECT_EQ(CPCB::extractConnectorIdFromMeterPublicKey("MeterPublicKey123"), 123);
}

TEST(ConnectorID, Build) {
    using CPCB = ocpp::v16::ChargePointConfigurationBase;

    EXPECT_EQ(CPCB::meterPublicKeyString(0), "MeterPublicKey0");
    EXPECT_EQ(CPCB::meterPublicKeyString(1), "MeterPublicKey1");
    EXPECT_EQ(CPCB::meterPublicKeyString(12), "MeterPublicKey12");
    EXPECT_EQ(CPCB::meterPublicKeyString(123), "MeterPublicKey123");
}

TEST(ConnectorID, PhaseRotation) {
    using CPCB = ocpp::v16::ChargePointConfigurationBase;
    const char* no_phase_rotation = "0.NotApplicable,1.Unknown,2.NotApplicable,3.Unknown";
    const char* valid_phase_rotation = "0.RST,1.RTS,2.SRT,3.STR,4.TRS,5.TSR";
    const char* valid_phase_rotation_extended = "8.RST,9.RTS,10.SRT,11.STR,12.TRS,13.TSR";

    EXPECT_TRUE(CPCB::isConnectorPhaseRotationValid(5, no_phase_rotation));
    EXPECT_FALSE(CPCB::isConnectorPhaseRotationValid(1, no_phase_rotation));

    EXPECT_TRUE(CPCB::isConnectorPhaseRotationValid(5, valid_phase_rotation));
    EXPECT_FALSE(CPCB::isConnectorPhaseRotationValid(3, valid_phase_rotation));

    EXPECT_TRUE(CPCB::isConnectorPhaseRotationValid(15, valid_phase_rotation_extended));
    EXPECT_FALSE(CPCB::isConnectorPhaseRotationValid(11, valid_phase_rotation_extended));

    EXPECT_TRUE(CPCB::isConnectorPhaseRotationValid(5, ""));

    // error cases
    EXPECT_FALSE(CPCB::isConnectorPhaseRotationValid(5, "123456"));
    EXPECT_FALSE(CPCB::isConnectorPhaseRotationValid(5, "abcdef"));
    EXPECT_FALSE(CPCB::isConnectorPhaseRotationValid(5, ".abcd"));
    EXPECT_FALSE(CPCB::isConnectorPhaseRotationValid(5, "abcd."));
    EXPECT_FALSE(CPCB::isConnectorPhaseRotationValid(5, "1."));
    EXPECT_FALSE(CPCB::isConnectorPhaseRotationValid(5, "11."));
    EXPECT_FALSE(CPCB::isConnectorPhaseRotationValid(5, "111."));
    EXPECT_FALSE(CPCB::isConnectorPhaseRotationValid(5, "1a.RST"));
}

using namespace ocpp;

TEST(V2Mapping, V16ToV2) {
    using namespace ocpp::v16::keys;
    using namespace ocpp::v2;

    auto res = convert_v2(valid_keys::CpoName);
    ASSERT_TRUE(res);
    auto component = std::get<Component>(res.value());
    auto variable = std::get<Variable>(res.value());
    EXPECT_EQ(component.name, "SecurityCtrlr");
    EXPECT_EQ(variable.name, "OrganizationName");

    res = convert_v2("CpoName");
    ASSERT_TRUE(res);
    component = std::get<Component>(res.value());
    variable = std::get<Variable>(res.value());
    EXPECT_EQ(component.name, "SecurityCtrlr");
    EXPECT_EQ(variable.name, "OrganizationName");
}

TEST(V2Mapping, V2ToV16) {
    using namespace ocpp::v16::keys;
    using namespace ocpp::v2;

    Component comp;
    comp.name = "SecurityCtrlr";
    Variable var;
    var.name = "OrganizationName";
    auto res = convert_v2(comp, var, ocpp::v2::AttributeEnum::Actual);
    EXPECT_EQ(res, "CpoName");

    // MaxChargingProfilesInstalled maps to VariableCharacteristics.maxLimit
    comp.name = "SmartChargingCtrlr";
    var.name = "Entries";
    var.instance = "ChargingProfiles";
    EXPECT_FALSE(convert_v2(comp, var, ocpp::v2::AttributeEnum::Actual));
    EXPECT_FALSE(convert_v2(comp, var, ocpp::v2::AttributeEnum::MaxSet));

    // Instead it is reachable via convert_v2_max_limit.
    const auto max_limit = convert_v2_max_limit(valid_keys::MaxChargingProfilesInstalled);
    ASSERT_TRUE(max_limit);
    EXPECT_EQ(max_limit->first.name, "SmartChargingCtrlr");
    EXPECT_EQ(max_limit->second.name, "Entries");
    EXPECT_EQ(max_limit->second.instance, "ChargingProfiles");
}

// Tests for get_all_key_value() with maxLimit keys.
// These keys map to VariableCharacteristics.maxLimit rather than VariableAttribute,
// so they are populated via a separate code path in get_all_key_value().
class MaxLimitGetAll : public ConfigurationBase {};

TEST_F(MaxLimitGetAll, MaxLimitKeysIncludedWhenSet) {
    ASSERT_TRUE(device_model);
    device_model->set("Core", "MeterValuesAlignedDataMaxLength", "10");
    device_model->set("Core", "MeterValuesSampledDataMaxLength", "20");
    device_model->set("Core", "StopTxnAlignedDataMaxLength", "30");
    device_model->set("Core", "StopTxnSampledDataMaxLength", "40");
    device_model->set("LocalAuthListManagement", "LocalAuthListMaxLength", "50");
    device_model->set("LocalAuthListManagement", "SendLocalListMaxLength", "60");
    device_model->set("SmartCharging", "MaxChargingProfilesInstalled", "70");

    const auto all = v2_config->get_all_key_value();
    const auto find = [&](const std::string& key) {
        return std::find_if(all.begin(), all.end(), [&](const auto& kv) { return kv.key == key.c_str(); });
    };

    struct Expected {
        const char* key;
        const char* value;
    };
    const Expected expected[] = {
        {"MeterValuesAlignedDataMaxLength", "10"}, {"MeterValuesSampledDataMaxLength", "20"},
        {"StopTxnAlignedDataMaxLength", "30"},     {"StopTxnSampledDataMaxLength", "40"},
        {"LocalAuthListMaxLength", "50"},          {"SendLocalListMaxLength", "60"},
        {"MaxChargingProfilesInstalled", "70"},
    };

    for (const auto& e : expected) {
        const auto it = find(e.key);
        ASSERT_NE(it, all.end()) << e.key << " missing from get_all_key_value()";
        EXPECT_EQ(it->value, e.value) << e.key;
        EXPECT_TRUE(it->readonly) << e.key << " should be readonly";
    }
}

TEST_F(MaxLimitGetAll, MaxLimitKeysAbsentWhenNotSet) {
    ASSERT_TRUE(device_model);
    // Do not configure any maxLimit keys — they should not appear in the result.
    const auto all = v2_config->get_all_key_value();
    const auto find = [&](const std::string& key) {
        return std::find_if(all.begin(), all.end(), [&](const auto& kv) { return kv.key == key.c_str(); });
    };

    // LocalAuthListMaxLength, SendLocalListMaxLength, MaxChargingProfilesInstalled are present because they are part of
    // the example config

    const char* keys[] = {
        "MeterValuesAlignedDataMaxLength",
        "MeterValuesSampledDataMaxLength",
        "StopTxnAlignedDataMaxLength",
        "StopTxnSampledDataMaxLength",
    };

    for (const auto* key : keys) {
        EXPECT_EQ(find(key), all.end()) << key << " should be absent when maxLimit is not set";
    }
}

} // namespace
