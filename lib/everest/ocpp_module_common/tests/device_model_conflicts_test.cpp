// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <stdexcept>

#include <everest/ocpp_module_common/device_model/device_model_conflicts.hpp>

namespace {

using ocpp_module_common::device_model::check_device_model_conflicts;

ocpp::v2::Component make_component(const std::string& name, const std::optional<std::int32_t>& evse_id = std::nullopt) {
    ocpp::v2::Component component;
    component.name = name;
    if (evse_id.has_value()) {
        ocpp::v2::EVSE evse;
        evse.id = evse_id.value();
        component.evse = evse;
    }
    return component;
}

ocpp::v2::Variable make_variable(const std::string& name) {
    ocpp::v2::Variable variable;
    variable.name = name;
    return variable;
}

ocpp::v2::ComponentRegistrationResult make_result_with_conflicts() {
    ocpp::v2::ComponentRegistrationResult result;
    result.rejected.push_back({make_component("EVSE", 1), make_variable("Power"),
                               ocpp::v2::RegistrationRejectionReason::AlreadyDefined,
                               ocpp::v2::VARIABLE_OWNER_COMPONENT_CONFIG});
    result.rejected.push_back({make_component("EVSE", 1), make_variable("Available"),
                               ocpp::v2::RegistrationRejectionReason::AlreadyDefined, std::string{"EXT:vendor"}});
    return result;
}

} // namespace

TEST(DeviceModelConflictsTest, rejected_definitions_are_conflicts) {
    const auto conflicts = check_device_model_conflicts({make_result_with_conflicts()}, false);

    ASSERT_EQ(conflicts.size(), 2);
    EXPECT_NE(conflicts.at(0).find("Power"), std::string::npos);
    EXPECT_NE(conflicts.at(0).find("component config"), std::string::npos);
    EXPECT_NE(conflicts.at(1).find("EXT:vendor"), std::string::npos);
}

TEST(DeviceModelConflictsTest, a_variable_without_attributes_is_not_a_conflict) {
    ocpp::v2::ComponentRegistrationResult result;
    result.rejected.push_back(
        {make_component("EVSE", 1), make_variable("Power"), ocpp::v2::RegistrationRejectionReason::NoAttributes, ""});

    EXPECT_NO_THROW(EXPECT_TRUE(check_device_model_conflicts({result}, true).empty()));
}

TEST(DeviceModelConflictsTest, conflicts_stop_the_start_only_when_configured) {
    EXPECT_NO_THROW(check_device_model_conflicts({make_result_with_conflicts()}, false));
    EXPECT_THROW(check_device_model_conflicts({make_result_with_conflicts()}, true), std::runtime_error);
    EXPECT_NO_THROW(check_device_model_conflicts({ocpp::v2::ComponentRegistrationResult{}}, true));
}
