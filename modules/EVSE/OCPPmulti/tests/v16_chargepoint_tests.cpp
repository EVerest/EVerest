// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "stubs/v2_chargepoint_stub.hpp"

#include <ModuleAdapterStub.hpp>
#include <v16_chargepoint.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <filesystem>

namespace {
using namespace ocpp_multi;

std::string get_simplified_error_type(const std::string& error_type) {
    // this function should return everything after the first '/'
    // delimiter - if there is no delimiter or the delimiter is at
    // the end, it should return the input itself
    static constexpr auto TYPE_INTERFACE_DELIMITER = '/';

    auto input = std::istringstream(error_type);
    std::string tmp;

    // move right after the first delimiter
    std::getline(input, tmp, TYPE_INTERFACE_DELIMITER);

    if (!input) {
        // no delimiter found or delimiter at the end
        return error_type;
    }

    // get the rest of the input
    std::getline(input, tmp);

    return tmp;
};

std::string vendor_error_code_orig(const Everest::error::Error& error) {
    return get_simplified_error_type(error.type) + '/' + error.sub_type;
}

namespace fs = std::filesystem;

// ChargePointV16 on a real OCPP 1.6 stack backed by the device model; never started or connected
class ChargePointV16ConfigurationTest : public testing::Test {
protected:
    module::stub::QuietModuleAdapterStub m_adapter;
    Requirement m_requirement{"ocpp", 0};
    evse_securityIntf m_security{&m_adapter, m_requirement, "security", std::nullopt};
    testing::NiceMock<stubs::GenericChargePointCallbacksMock> m_callbacks;
    ChargePointV16 m_chargepoint{m_callbacks, m_security};
    fs::path m_work_dir;

    void SetUp() override {
        m_work_dir = fs::temp_directory_path() / "ocppmulti_v16_chargepoint_tests" /
                     testing::UnitTest::GetInstance()->current_test_info()->name();
        fs::remove_all(m_work_dir);
        fs::create_directories(m_work_dir / "share");
        fs::create_directory_symlink(fs::absolute(LIBOCPP_V16_CONFIG_DIR), m_work_dir / "share" / "OCPP");

        GenericChargePointInterface::init_args_t args;
        args.message_log_path = m_work_dir / "logs";
        args.share_path = m_work_dir / "share";
        args.v16_database_path = m_work_dir;
        args.v2_device_model_config_path = fs::absolute(LIBOCPP_COMPONENT_CONFIG_DIR);
        args.v2_device_model_database_migration_path = fs::absolute(LIBOCPP_DEVICE_MODEL_MIGRATIONS_DIR);
        args.v2_device_model_database_path = m_work_dir / "device_model_storage.db";
        // the shipped custom component configs define two EVSEs
        args.evse_connector_structure = {{1, 1}, {2, 1}};
        args.connector_mapping = {{1, {{1, 1}}}, {2, {{1, 2}}}};
        args.v16_ocpp16_network_config_slot = 1;
        args.v16_enable_legacy_config_migration = false;
        m_chargepoint.init(args);
    }

    void TearDown() override {
        fs::remove_all(m_work_dir);
    }

    ocpp::v2::SetVariableResult set_variable(const std::string& component, const std::string& variable,
                                             const std::string& value) {
        ocpp::v2::SetVariableData data;
        data.component.name = component;
        data.variable.name = variable;
        data.attributeValue = value;
        const auto outcomes = m_chargepoint.set_variables({data}, "test");
        EXPECT_EQ(outcomes.size(), 1u);
        return outcomes.empty() ? ocpp::v2::SetVariableResult{} : outcomes.front().result;
    }
};

TEST(ChargePointV16, defaultIsFault) {
    Everest::error::Error error;
    EXPECT_FALSE(ChargePointV16::default_is_fault(error));
}

TEST(ChargePointV16, defaultVendorErrorCode) {
    Everest::error::Error error;
    error.type = "";
    EXPECT_EQ(ChargePointV16::default_vendor_error_code(error), "/");
    error.sub_type = "12345";
    EXPECT_EQ(ChargePointV16::default_vendor_error_code(error), "/12345");
    error.type = "/";
    EXPECT_EQ(ChargePointV16::default_vendor_error_code(error), "/12345");
    error.type = "abcd/";
    EXPECT_EQ(ChargePointV16::default_vendor_error_code(error), "/12345");
    error.type = "abcd/def";
    EXPECT_EQ(ChargePointV16::default_vendor_error_code(error), "def/12345");
    error.sub_type = "";
    EXPECT_EQ(ChargePointV16::default_vendor_error_code(error), "def/");
    error.type = "abcd/def/ghi";
    error.sub_type = "apples";
    EXPECT_EQ(ChargePointV16::default_vendor_error_code(error), "def/ghi/apples");
}

TEST(ChargePointV16, defaultVendorErrorCodeOriginal) {
    Everest::error::Error error;
    error.type = "";
    EXPECT_EQ(vendor_error_code_orig(error), "/");
    error.sub_type = "12345";
    EXPECT_EQ(vendor_error_code_orig(error), "/12345");
    error.type = "/";
    EXPECT_EQ(vendor_error_code_orig(error), "/12345");
    error.type = "abcd/";
    EXPECT_EQ(vendor_error_code_orig(error), "/12345");
    error.type = "abcd/def";
    EXPECT_EQ(vendor_error_code_orig(error), "def/12345");
    error.sub_type = "";
    EXPECT_EQ(vendor_error_code_orig(error), "def/");
    error.type = "abcd/def/ghi";
    error.sub_type = "apples";
    EXPECT_EQ(ChargePointV16::default_vendor_error_code(error), "def/ghi/apples");
}

TEST(ChargePointV16, encodePauseReasonsEmpty) {
    EXPECT_EQ(ChargePointV16::encode_pause_reasons(std::nullopt), std::nullopt);

    types::evse_manager::ChargingPausedEVSEReasons reasons;
    EXPECT_TRUE(reasons.reasons.empty());
    EXPECT_EQ(ChargePointV16::encode_pause_reasons(reasons), std::nullopt);
}

TEST(ChargePointV16, encodePauseReasonsSingle) {
    types::evse_manager::ChargingPausedEVSEReasons reasons;

    reasons.reasons = {types::evse_manager::PauseChargingEVSEReasonEnum::NoEnergy};
    auto encoded = ChargePointV16::encode_pause_reasons(reasons);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(static_cast<std::string>(encoded.value()), "NoEnergy");

    reasons.reasons = {types::evse_manager::PauseChargingEVSEReasonEnum::Error};
    encoded = ChargePointV16::encode_pause_reasons(reasons);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(static_cast<std::string>(encoded.value()), "Error");

    reasons.reasons = {types::evse_manager::PauseChargingEVSEReasonEnum::UserPause};
    encoded = ChargePointV16::encode_pause_reasons(reasons);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(static_cast<std::string>(encoded.value()), "UserPause");
}

TEST(ChargePointV16, encodePauseReasonsDeduplicates) {
    types::evse_manager::ChargingPausedEVSEReasons reasons;
    reasons.reasons = {types::evse_manager::PauseChargingEVSEReasonEnum::UserPause,
                       types::evse_manager::PauseChargingEVSEReasonEnum::UserPause,
                       types::evse_manager::PauseChargingEVSEReasonEnum::Error};
    const auto encoded = ChargePointV16::encode_pause_reasons(reasons);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(static_cast<std::string>(encoded.value()), "Error,UserPause");
}

TEST(ChargePointV16, encodePauseReasonsSortedAlphabetically) {
    types::evse_manager::ChargingPausedEVSEReasons reasons;

    reasons.reasons = {types::evse_manager::PauseChargingEVSEReasonEnum::Error,
                       types::evse_manager::PauseChargingEVSEReasonEnum::NoEnergy,
                       types::evse_manager::PauseChargingEVSEReasonEnum::UserPause};
    const auto encoded = ChargePointV16::encode_pause_reasons(reasons);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(static_cast<std::string>(encoded.value()), "Error,NoEnergy,UserPause");

    reasons.reasons = {types::evse_manager::PauseChargingEVSEReasonEnum::UserPause,
                       types::evse_manager::PauseChargingEVSEReasonEnum::NoEnergy,
                       types::evse_manager::PauseChargingEVSEReasonEnum::Error};
    const auto reversed = ChargePointV16::encode_pause_reasons(reasons);
    ASSERT_TRUE(reversed.has_value());
    EXPECT_EQ(static_cast<std::string>(reversed.value()), "Error,NoEnergy,UserPause");
}

TEST(ChargePointV16, encodePauseReasonsFitsCiString) {
    types::evse_manager::ChargingPausedEVSEReasons reasons;
    reasons.reasons = {types::evse_manager::PauseChargingEVSEReasonEnum::UserPause,
                       types::evse_manager::PauseChargingEVSEReasonEnum::Error,
                       types::evse_manager::PauseChargingEVSEReasonEnum::NoEnergy};
    const auto encoded = ChargePointV16::encode_pause_reasons(reasons);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(static_cast<std::string>(encoded.value()).size(), 24u);
    EXPECT_EQ(static_cast<std::string>(encoded.value()), "Error,NoEnergy,UserPause");
}

TEST_F(ChargePointV16ConfigurationTest, monitoredKeyChangeStillReachesVariableSet) {
    std::optional<std::string> monitored_value;
    m_chargepoint.register_variable_listener({"ISO15118Ctrlr"}, {"PnCEnabled"},
                                             [&monitored_value](const ocpp::v2::Component&, const ocpp::v2::Variable&,
                                                                const std::string& value) { monitored_value = value; });

    EXPECT_CALL(m_callbacks, cb_variable_set(testing::Truly([](const ocpp::v2::SetVariableData& data) {
                    return data.component.name.get() == "ISO15118Ctrlr" && data.variable.name.get() == "PnCEnabled" &&
                           data.attributeValue.get() == "false";
                })));

    const auto result = set_variable("ISO15118Ctrlr", "PnCEnabled", "false");
    ASSERT_EQ(result.attributeStatus, ocpp::v2::SetVariableStatusEnum::Accepted);
    EXPECT_EQ(monitored_value, "false");
}

} // namespace
