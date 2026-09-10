// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "everest_api_types/evse_board_support/codec.hpp"
#include "everest_api_types/utilities/codec.hpp"
#include "nlohmann/json.hpp"
#include <gtest/gtest.h>

using namespace everest::lib::API::V1_0::types::evse_board_support;

namespace {

constexpr std::string_view capabilities_without_cp_state_E = R"({
    "max_current_A_import": 724.638,
    "min_current_A_import": 0,
    "max_phase_count_import": 3,
    "min_phase_count_import": 3,
    "max_current_A_export": 0,
    "min_current_A_export": 0,
    "max_phase_count_export": 0,
    "min_phase_count_export": 0,
    "supports_changing_phases_during_charging": false,
    "connector_type": "IEC62196Type2Cable"
})";

} // namespace

TEST(evse_board_support, hardware_capabilities_without_cp_state_E_defaults_to_false) {
    HardwareCapabilities result;
    ASSERT_TRUE(everest::lib::API::deserialize(std::string(capabilities_without_cp_state_E), result));

    EXPECT_FALSE(result.supports_cp_state_E);
    EXPECT_FLOAT_EQ(result.max_current_A_import, 724.638f);
    EXPECT_EQ(result.max_phase_count_import, 3);
    EXPECT_FALSE(result.supports_changing_phases_during_charging);
    EXPECT_EQ(result.connector_type, Connector_type::IEC62196Type2Cable);
    EXPECT_FALSE(result.max_plug_temperature_C.has_value());
}

TEST(evse_board_support, hardware_capabilities_with_cp_state_E_is_read) {
    auto payload = nlohmann::json::parse(capabilities_without_cp_state_E);
    payload["supports_cp_state_E"] = true;

    HardwareCapabilities result;
    ASSERT_TRUE(everest::lib::API::deserialize(payload.dump(), result));

    EXPECT_TRUE(result.supports_cp_state_E);
}

TEST(evse_board_support, hardware_capabilities_serialization_emits_cp_state_E) {
    HardwareCapabilities capabilities{};
    capabilities.supports_cp_state_E = true;
    capabilities.connector_type = Connector_type::IEC62196Type2Socket;

    auto serialized = nlohmann::json::parse(serialize(capabilities));
    EXPECT_EQ(serialized.at("supports_cp_state_E"), true);
    EXPECT_EQ(serialized.at("connector_type"), "IEC62196Type2Socket");
}
