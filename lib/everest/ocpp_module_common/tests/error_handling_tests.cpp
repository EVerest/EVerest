// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <everest/ocpp_module_common/error_handling.hpp>

#include <filesystem>
#include <stdexcept>
#include <string_view>

namespace ocpp_module_common {

namespace {
std::filesystem::path test_data_path(std::string_view name) {
    return std::filesystem::path(TEST_DATA_DIR) / name;
}
} // namespace

TEST(LoadMrecErrorMapOverrides, OverridesAndAddsEntries) {
    const auto map = load_mrec_error_map_overrides(test_data_path("valid_overrides.json"));

    EXPECT_EQ(map.at("evse_manager/MREC4OverCurrentFailure"), "OVERRIDE_CX004");
    EXPECT_EQ(map.at("custom/MyNewError"), "CX999");
    EXPECT_EQ(map.at("connector_lock/MREC1ConnectorLockFailure"), "CX001");
}

TEST(LoadMrecErrorMapOverrides, EmptyObjectReturnsDefaults) {
    const auto map = load_mrec_error_map_overrides(test_data_path("empty_object.json"));
    EXPECT_EQ(map, MREC_ERROR_MAP);
}

class LoadMrecErrorMapOverridesThrowsTest : public ::testing::TestWithParam<std::string_view> {};

TEST_P(LoadMrecErrorMapOverridesThrowsTest, Throws) {
    EXPECT_THROW(load_mrec_error_map_overrides(test_data_path(GetParam())), std::runtime_error);
}

INSTANTIATE_TEST_SUITE_P(LoadMrecErrorMapOverrides, LoadMrecErrorMapOverridesThrowsTest,
                         ::testing::Values(std::string_view{"does_not_exist.json"}, std::string_view{"malformed.json"},
                                           std::string_view{"invalid_array.json"},
                                           std::string_view{"invalid_value_number.json"}));

} // namespace ocpp_module_common
