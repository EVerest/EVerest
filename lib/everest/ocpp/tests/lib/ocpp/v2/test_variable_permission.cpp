// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <ocpp/v2/device_model_helpers.hpp>
#include <ocpp/v2/variable_permission.hpp>

namespace ocpp {
namespace v2 {

namespace {
Component component(const std::string& name, const std::optional<std::string>& instance = std::nullopt,
                    const std::optional<std::int32_t>& evse_id = std::nullopt,
                    const std::optional<std::int32_t>& connector_id = std::nullopt) {
    Component result;
    result.name = name;
    if (instance.has_value()) {
        result.instance = instance.value();
    }
    if (evse_id.has_value()) {
        EVSE evse;
        evse.id = evse_id.value();
        if (connector_id.has_value()) {
            evse.connectorId = connector_id.value();
        }
        result.evse = evse;
    }
    return result;
}

Variable variable(const std::string& name, const std::optional<std::string>& instance = std::nullopt) {
    Variable result;
    result.name = name;
    if (instance.has_value()) {
        result.instance = instance.value();
    }
    return result;
}
} // namespace

// --- the hierarchy notation ---------------------------------------------------------------------

TEST(ComponentVariableHierarchyTest, a_bare_component_name) {
    const auto parsed = parse_component_variable_hierarchy("SmartChargingCtrlr");

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->component.name, "SmartChargingCtrlr");
    EXPECT_FALSE(parsed->component.instance.has_value());
    EXPECT_FALSE(parsed->component.evse.has_value());
    // No variable named means the whole component.
    EXPECT_FALSE(parsed->variable.has_value());
}

TEST(ComponentVariableHierarchyTest, a_component_instance) {
    const auto parsed = parse_component_variable_hierarchy("NetworkConfiguration#3");

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->component.name, "NetworkConfiguration");
    ASSERT_TRUE(parsed->component.instance.has_value());
    EXPECT_EQ(parsed->component.instance.value(), "3");
    EXPECT_FALSE(parsed->variable.has_value());
}

TEST(ComponentVariableHierarchyTest, a_component_instance_and_a_variable) {
    const auto parsed = parse_component_variable_hierarchy("NetworkConfiguration#3&OcppCsmsUrl");

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->component.name, "NetworkConfiguration");
    EXPECT_EQ(parsed->component.instance.value(), "3");
    ASSERT_TRUE(parsed->variable.has_value());
    EXPECT_EQ(parsed->variable->name, "OcppCsmsUrl");
    EXPECT_FALSE(parsed->variable->instance.has_value());
}

TEST(ComponentVariableHierarchyTest, an_evse_scoped_component) {
    const auto parsed = parse_component_variable_hierarchy("1/EVSE&AvailabilityState");

    ASSERT_TRUE(parsed.has_value());
    ASSERT_TRUE(parsed->component.evse.has_value());
    EXPECT_EQ(parsed->component.evse->id, 1);
    EXPECT_FALSE(parsed->component.evse->connectorId.has_value());
    EXPECT_EQ(parsed->component.name, "EVSE");
    EXPECT_EQ(parsed->variable->name, "AvailabilityState");
}

TEST(ComponentVariableHierarchyTest, a_connector_scoped_component) {
    const auto parsed = parse_component_variable_hierarchy("1#2/Connector&AvailabilityState");

    ASSERT_TRUE(parsed.has_value());
    ASSERT_TRUE(parsed->component.evse.has_value());
    EXPECT_EQ(parsed->component.evse->id, 1);
    ASSERT_TRUE(parsed->component.evse->connectorId.has_value());
    EXPECT_EQ(parsed->component.evse->connectorId.value(), 2);
    EXPECT_EQ(parsed->component.name, "Connector");
}

TEST(ComponentVariableHierarchyTest, a_variable_instance) {
    const auto parsed = parse_component_variable_hierarchy("AlignedDataCtrlr&Measurands#EMS");

    ASSERT_TRUE(parsed.has_value());
    ASSERT_TRUE(parsed->variable.has_value());
    EXPECT_EQ(parsed->variable->name, "Measurands");
    ASSERT_TRUE(parsed->variable->instance.has_value());
    EXPECT_EQ(parsed->variable->instance.value(), "EMS");
}

TEST(ComponentVariableHierarchyTest, malformed_entries_are_rejected) {
    EXPECT_FALSE(parse_component_variable_hierarchy("").has_value());
    EXPECT_FALSE(parse_component_variable_hierarchy("#3").has_value());
    EXPECT_FALSE(parse_component_variable_hierarchy("Component#").has_value());
    EXPECT_FALSE(parse_component_variable_hierarchy("Component&").has_value());
    EXPECT_FALSE(parse_component_variable_hierarchy("Component&Variable#").has_value());
    EXPECT_FALSE(parse_component_variable_hierarchy("/Component").has_value());
    EXPECT_FALSE(parse_component_variable_hierarchy("notanumber/Component").has_value());
    EXPECT_FALSE(parse_component_variable_hierarchy("1#x/Component").has_value());
}

TEST(ComponentVariableHierarchyTest, round_trips) {
    for (const auto& entry :
         {"SmartChargingCtrlr", "NetworkConfiguration#3", "NetworkConfiguration#3&OcppCsmsUrl",
          "1/EVSE&AvailabilityState", "1#2/Connector&AvailabilityState", "AlignedDataCtrlr&Measurands#EMS"}) {
        const auto parsed = parse_component_variable_hierarchy(entry);
        ASSERT_TRUE(parsed.has_value()) << entry;
        EXPECT_EQ(to_component_variable_hierarchy(parsed->component, parsed->variable), entry);
    }
}

// --- the permission list --------------------------------------------------------------------------

TEST(VariablePermissionListTest, an_empty_list_allows_nothing) {
    // Default deny: by default a secondary system may change no device model variable at all.
    const auto list = VariablePermissionList::parse("");

    EXPECT_TRUE(list.empty());
    EXPECT_FALSE(list.allows(component("SmartChargingCtrlr"), variable("Anything")));
}

TEST(VariablePermissionListTest, a_component_entry_allows_all_its_variables) {
    // Application note section 3.6 first example.
    const auto list = VariablePermissionList::parse("SmartChargingCtrlr");

    EXPECT_TRUE(list.allows(component("SmartChargingCtrlr"), variable("Enabled")));
    EXPECT_TRUE(list.allows(component("SmartChargingCtrlr"), variable("SetpointPriority")));
    EXPECT_FALSE(list.allows(component("TxCtrlr"), variable("Enabled")));
}

TEST(VariablePermissionListTest, a_variable_entry_allows_only_that_variable) {
    // Application note section 3.6 second example.
    const auto list = VariablePermissionList::parse("NetworkConfiguration#3&OcppCsmsUrl");

    EXPECT_TRUE(list.allows(component("NetworkConfiguration", "3"), variable("OcppCsmsUrl")));
    EXPECT_FALSE(list.allows(component("NetworkConfiguration", "3"), variable("SecurityProfile")));
}

TEST(VariablePermissionListTest, an_instanced_component_entry_allows_all_its_variables) {
    // Application note section 3.6 third example.
    const auto list = VariablePermissionList::parse("NetworkConfiguration#3");

    EXPECT_TRUE(list.allows(component("NetworkConfiguration", "3"), variable("OcppCsmsUrl")));
    EXPECT_TRUE(list.allows(component("NetworkConfiguration", "3"), variable("SecurityProfile")));
}

TEST(VariablePermissionListTest, an_instance_is_matched_exactly) {
    const auto list = VariablePermissionList::parse("NetworkConfiguration#3");

    // A grant for one instance is not a grant for another, nor for the instance-less component.
    EXPECT_FALSE(list.allows(component("NetworkConfiguration", "4"), variable("OcppCsmsUrl")));
    EXPECT_FALSE(list.allows(component("NetworkConfiguration"), variable("OcppCsmsUrl")));
}

TEST(VariablePermissionListTest, omitting_an_instance_does_not_grant_every_instance) {
    // Otherwise being unspecific would silently widen a grant.
    const auto list = VariablePermissionList::parse("AlignedDataCtrlr");

    EXPECT_TRUE(list.allows(component("AlignedDataCtrlr"), variable("Interval")));
    EXPECT_FALSE(list.allows(component("AlignedDataCtrlr", "EMS"), variable("Interval")));
}

TEST(VariablePermissionListTest, an_evse_scope_is_matched_exactly) {
    const auto list = VariablePermissionList::parse("1/EVSE&AvailabilityState");

    EXPECT_TRUE(list.allows(component("EVSE", std::nullopt, 1), variable("AvailabilityState")));
    EXPECT_FALSE(list.allows(component("EVSE", std::nullopt, 2), variable("AvailabilityState")));
    EXPECT_FALSE(list.allows(component("EVSE"), variable("AvailabilityState")));
}

TEST(VariablePermissionListTest, several_entries) {
    const auto list = VariablePermissionList::parse(
        "AlignedDataCtrlr#DATACOLLECTION&Measurands, AlignedDataCtrlr#DATACOLLECTION&Interval");

    EXPECT_TRUE(list.allows(component("AlignedDataCtrlr", "DATACOLLECTION"), variable("Measurands")));
    EXPECT_TRUE(list.allows(component("AlignedDataCtrlr", "DATACOLLECTION"), variable("Interval")));
    EXPECT_FALSE(list.allows(component("AlignedDataCtrlr", "DATACOLLECTION"), variable("SignReadings")));
}

TEST(VariablePermissionListTest, a_malformed_entry_does_not_void_the_rest) {
    const auto list = VariablePermissionList::parse("#nonsense,SmartChargingCtrlr");

    EXPECT_TRUE(list.allows(component("SmartChargingCtrlr"), variable("Enabled")));
}

TEST(VariablePermissionListTest, component_names_are_matched_case_insensitively) {
    // As everywhere else in the device model.
    const auto list = VariablePermissionList::parse("smartchargingctrlr");

    EXPECT_TRUE(list.allows(component("SmartChargingCtrlr"), variable("Enabled")));
}

} // namespace v2
} // namespace ocpp
