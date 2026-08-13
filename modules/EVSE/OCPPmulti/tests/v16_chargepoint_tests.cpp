// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <v16_chargepoint.hpp>

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

} // namespace
