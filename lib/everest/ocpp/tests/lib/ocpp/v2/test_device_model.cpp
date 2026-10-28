// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <atomic>
#include <thread>

#include <device_model_test_helper.hpp>

#include <everest/util/async/monitor.hpp>

#include <ocpp/v2/ctrlr_component_variables.hpp>
#include <ocpp/v2/device_model.hpp>
#include <ocpp/v2/device_model_storage_sqlite.hpp>
#include <ocpp/v2/monitoring_updater.hpp>

namespace ocpp {
namespace v2 {

class DeviceModelTest : public ::testing::Test {
protected:
    DeviceModelTestHelper device_model_test_helper;
    DeviceModel* dm;
    const RequiredComponentVariable cv = ControllerComponentVariables::AlignedDataInterval;
    const std::map<std::int32_t, std::int32_t> evse_connector_structure{{1, 1}, {2, 1}};

    DeviceModelTest() : device_model_test_helper(), dm(device_model_test_helper.get_device_model()) {
    }
};

/// \brief Test if value can be changed. And test if setting a value of 0 is allowed for a value that is allowed to be
/// 0.
TEST_F(DeviceModelTest, test_set_value_and_allow_zero) {
    // default value is 900
    auto r = dm->get_value<int>(cv, ocpp::v2::AttributeEnum::Actual);
    ASSERT_EQ(r, 900);

    // try to set to 2, which is allowed because there is no minLimit
    auto sv_result = dm->set_value(cv.component, cv.variable.value(), ocpp::v2::AttributeEnum::Actual, "2", "test");
    ASSERT_EQ(sv_result, SetVariableStatusEnum::Accepted);

    // try to set to 0, which is allowed because 0 is an exception
    sv_result = dm->set_value(cv.component, cv.variable.value(), ocpp::v2::AttributeEnum::Actual, "0", "test");
    ASSERT_EQ(sv_result, SetVariableStatusEnum::Accepted);

    r = dm->get_value<int>(cv, ocpp::v2::AttributeEnum::Actual);
    ASSERT_EQ(r, 0);
}

/// \brief Test if value of 0 is allowed for ControllerComponentVariables::AlignedDataInterval although the minLimit is
/// set to 5
TEST_F(DeviceModelTest, test_min_limit_and_allow_zero) {
    // Set a min limit to 'Interval' of 'AlignedDataCtrlr'.
    VariableCharacteristics c;
    c.minLimit = 10;
    c.dataType = DataEnum::integer;
    c.supportsMonitoring = true;
    c.unit = "s";

    device_model_test_helper.update_variable_characteristics(c, "AlignedDataCtrlr", std::nullopt, std::nullopt,
                                                             std::nullopt, "Interval", std::nullopt);

    dm = device_model_test_helper.get_device_model();

    // default value is 900
    auto r = dm->get_value<int>(cv, ocpp::v2::AttributeEnum::Actual);
    ASSERT_EQ(r, 900);

    // try to set to 2, which is not allowed because minLimit of 10
    auto sv_result = dm->set_value(cv.component, cv.variable.value(), ocpp::v2::AttributeEnum::Actual, "2", "test");
    ASSERT_EQ(sv_result, SetVariableStatusEnum::Rejected);

    // try to set to 0, which is allowed because 0 is an exception
    sv_result = dm->set_value(cv.component, cv.variable.value(), ocpp::v2::AttributeEnum::Actual, "0", "test");
    ASSERT_EQ(sv_result, SetVariableStatusEnum::Accepted);

    r = dm->get_value<int>(cv, ocpp::v2::AttributeEnum::Actual);
    ASSERT_EQ(r, 0);
}

/// \brief SecurityProfile 0 must be settable although databases and component configs created from earlier schemas
/// still carry a minLimit of 1
TEST_F(DeviceModelTest, test_security_profile_zero_with_legacy_min_limit) {
    VariableCharacteristics c;
    c.minLimit = 1;
    c.maxLimit = 3;
    c.dataType = DataEnum::integer;
    c.supportsMonitoring = true;

    device_model_test_helper.update_variable_characteristics(c, "SecurityCtrlr", std::nullopt, std::nullopt,
                                                             std::nullopt, "SecurityProfile", std::nullopt);

    dm = device_model_test_helper.get_device_model();
    const auto& security_profile = ControllerComponentVariables::SecurityProfile;

    auto sv_result = dm->set_read_only_value(security_profile.component, security_profile.variable.value(),
                                             AttributeEnum::Actual, "0", "test");
    EXPECT_EQ(sv_result, SetVariableStatusEnum::Accepted);
    EXPECT_EQ(dm->get_value<int>(security_profile), 0);

    dm->set_active_security_profile(2, "test");
    EXPECT_EQ(dm->get_value<int>(security_profile), 2);
    dm->set_security_ctrl_security_profile(0, "test");
    EXPECT_EQ(dm->get_value<int>(security_profile), 0);
    dm->set_active_security_profile(2, "test");
    dm->set_active_security_profile(0, "test");
    EXPECT_EQ(dm->get_value<int>(security_profile), 0);

    sv_result = dm->set_read_only_value(security_profile.component, security_profile.variable.value(),
                                        AttributeEnum::Actual, "4", "test");
    EXPECT_EQ(sv_result, SetVariableStatusEnum::Rejected);
    EXPECT_EQ(dm->get_value<int>(security_profile), 0);
}

TEST_F(DeviceModelTest, test_component_as_key_in_map) {
    std::map<Component, std::int32_t> components_to_ints;

    Component base_comp;
    base_comp.name = "Foo";
    components_to_ints[base_comp] = 1;

    Component different_instance_comp;
    different_instance_comp.name = "Foo";
    different_instance_comp.instance = "bar";

    Component different_evse_comp;
    different_evse_comp.name = "Foo";
    EVSE evse0;
    evse0.id = 0;
    different_evse_comp.evse = evse0;

    Component different_evse_and_instance_comp;
    different_evse_and_instance_comp.name = "Foo";
    different_evse_and_instance_comp.evse = evse0;
    different_evse_and_instance_comp.instance = "bar";

    Component comp_with_custom_data;
    comp_with_custom_data.name = "Foo";
    comp_with_custom_data.customData = json::object({{"vendorId", "Baz"}});

    Component different_name_comp;
    different_name_comp.name = "Bar";

    EXPECT_EQ(components_to_ints.find(base_comp)->second, 1);
    EXPECT_EQ(components_to_ints.find(different_instance_comp), components_to_ints.end());
    EXPECT_EQ(components_to_ints.find(different_evse_comp), components_to_ints.end());
    EXPECT_EQ(components_to_ints.find(different_evse_and_instance_comp), components_to_ints.end());
    EXPECT_EQ(components_to_ints.find(comp_with_custom_data)->second, 1);
    EXPECT_EQ(components_to_ints.find(different_name_comp), components_to_ints.end());

    // OCPP 2.1 Part 2: ComponentType.name and .instance are identifierString, which is case-insensitive.
    Component lower_case_name_comp;
    lower_case_name_comp.name = "foo";

    Component upper_case_name_comp;
    upper_case_name_comp.name = "FOO";

    EXPECT_EQ(components_to_ints.count(lower_case_name_comp), 1U) << "\"foo\" should match \"Foo\"";
    EXPECT_EQ(components_to_ints.count(upper_case_name_comp), 1U) << "\"FOO\" should match \"Foo\"";
    EXPECT_EQ(base_comp, lower_case_name_comp);
    EXPECT_EQ(base_comp, upper_case_name_comp);

    components_to_ints[different_instance_comp] = 2;

    Component different_case_instance_comp;
    different_case_instance_comp.name = "Foo";
    different_case_instance_comp.instance = "BAR";

    EXPECT_EQ(components_to_ints.count(different_case_instance_comp), 1U) << "instance \"BAR\" should match \"bar\"";
    EXPECT_EQ(different_instance_comp, different_case_instance_comp);

    ASSERT_EQ(components_to_ints.count(lower_case_name_comp), 1U);
    EXPECT_EQ(components_to_ints.at(lower_case_name_comp), 1);
    ASSERT_EQ(components_to_ints.count(different_case_instance_comp), 1U);
    EXPECT_EQ(components_to_ints.at(different_case_instance_comp), 2);
}

TEST_F(DeviceModelTest, test_variable_as_key_in_map) {
    std::map<Variable, std::int32_t> variables_to_ints;

    Variable base_var;
    base_var.name = "Foo";
    variables_to_ints[base_var] = 1;

    Variable different_instance_var;
    different_instance_var.name = "Foo";
    different_instance_var.instance = "bar";

    Variable var_with_custom_data;
    var_with_custom_data.name = "Foo";
    var_with_custom_data.customData = json::object({{"vendorId", "Baz"}});

    Variable different_name_var;
    different_name_var.name = "Bar";

    EXPECT_EQ(variables_to_ints.find(base_var)->second, 1);
    EXPECT_EQ(variables_to_ints.find(different_instance_var), variables_to_ints.end());
    EXPECT_EQ(variables_to_ints.find(var_with_custom_data)->second, 1);
    EXPECT_EQ(variables_to_ints.find(different_name_var), variables_to_ints.end());

    // OCPP 2.1 Part 2: VariableType.name and .instance are identifierString, which is case-insensitive.
    Variable lower_case_name_var;
    lower_case_name_var.name = "foo";

    Variable upper_case_name_var;
    upper_case_name_var.name = "FOO";

    EXPECT_EQ(variables_to_ints.count(lower_case_name_var), 1U) << "\"foo\" should match \"Foo\"";
    EXPECT_EQ(variables_to_ints.count(upper_case_name_var), 1U) << "\"FOO\" should match \"Foo\"";
    EXPECT_EQ(base_var, lower_case_name_var);
    EXPECT_EQ(base_var, upper_case_name_var);

    variables_to_ints[different_instance_var] = 2;

    Variable different_case_instance_var;
    different_case_instance_var.name = "Foo";
    different_case_instance_var.instance = "BAR";

    EXPECT_EQ(variables_to_ints.count(different_case_instance_var), 1U) << "instance \"BAR\" should match \"bar\"";
    EXPECT_EQ(different_instance_var, different_case_instance_var);

    ASSERT_EQ(variables_to_ints.count(lower_case_name_var), 1U);
    EXPECT_EQ(variables_to_ints.at(lower_case_name_var), 1);
    ASSERT_EQ(variables_to_ints.count(different_case_instance_var), 1U);
    EXPECT_EQ(variables_to_ints.at(different_case_instance_var), 2);
}

TEST_F(DeviceModelTest, test_set_monitors) {
    std::vector<SetMonitoringData> requests;

    EVSE evse;
    evse.id = 1;
    evse.connectorId = 1;

    Component component1;
    component1.name = "Connector";
    component1.evse = evse;

    Component component2;
    component2.name = "AlignedDataCtrlr";

    Variable variable_comp1;
    variable_comp1.name = "SupplyPhases";
    Variable variable_comp2;
    variable_comp2.name = "Interval";

    std::vector<ComponentVariable> components = {
        {component1, variable_comp1},
        {component2, variable_comp2},
    };

    // Clear all existing monitors for a clean test state
    auto existing_monitors = dm->get_monitors({}, components);
    for (auto& result : existing_monitors) {
        std::vector<std::int32_t> ids;
        for (auto& monitor : result.variableMonitoring) {
            ids.push_back(monitor.id);
        }
        dm->clear_monitors(ids, true);
    }

    SetMonitoringData req_one;
    req_one.value = 0.0;
    req_one.type = MonitorEnum::PeriodicClockAligned;
    req_one.severity = 7;
    req_one.component = component1;
    req_one.variable = variable_comp1;
    SetMonitoringData req_two;
    req_two.value = 4.579;
    req_two.type = MonitorEnum::UpperThreshold;
    req_two.severity = 3;
    req_two.component = component2;
    req_two.variable = variable_comp2;

    requests.push_back(req_one);
    requests.push_back(req_two);

    auto results = dm->set_monitors(requests);
    ASSERT_EQ(results.size(), requests.size());

    ASSERT_EQ(results[0].status, SetMonitoringStatusEnum::Accepted);
    // Interval is not a float but an integer.
    ASSERT_EQ(results[1].status, SetMonitoringStatusEnum::Rejected);
}

TEST_F(DeviceModelTest, test_set_threshold_and_delta_monitors_on_integer_variable) {
    Component component;
    component.name = "AlignedDataCtrlr";

    Variable variable;
    variable.name = "Interval";

    // Clear all existing monitors for a clean test state
    auto existing_monitors = dm->get_monitors({}, {{component, variable}});
    for (auto& result : existing_monitors) {
        std::vector<std::int32_t> ids;
        for (auto& monitor : result.variableMonitoring) {
            ids.push_back(monitor.id);
        }
        dm->clear_monitors(ids, true);
    }

    SetMonitoringData upper_threshold;
    upper_threshold.value = 950.0;
    upper_threshold.type = MonitorEnum::UpperThreshold;
    upper_threshold.severity = 3;
    upper_threshold.component = component;
    upper_threshold.variable = variable;

    SetMonitoringData lower_threshold;
    lower_threshold.value = 10.0;
    lower_threshold.type = MonitorEnum::LowerThreshold;
    lower_threshold.severity = 3;
    lower_threshold.component = component;
    lower_threshold.variable = variable;

    SetMonitoringData delta;
    delta.value = 30.0;
    delta.type = MonitorEnum::Delta;
    delta.severity = 3;
    delta.component = component;
    delta.variable = variable;

    // Non-integral values remain invalid for integer-typed variables
    SetMonitoringData non_integral;
    non_integral.value = 4.579;
    non_integral.type = MonitorEnum::UpperThreshold;
    non_integral.severity = 4;
    non_integral.component = component;
    non_integral.variable = variable;

    auto results = dm->set_monitors({upper_threshold, lower_threshold, delta, non_integral});
    ASSERT_EQ(results.size(), 4);

    EXPECT_EQ(results[0].status, SetMonitoringStatusEnum::Accepted);
    EXPECT_EQ(results[1].status, SetMonitoringStatusEnum::Accepted);
    EXPECT_EQ(results[2].status, SetMonitoringStatusEnum::Accepted);
    EXPECT_EQ(results[3].status, SetMonitoringStatusEnum::Rejected);
}

TEST_F(DeviceModelTest, test_get_monitors) {
    // Set 'Interval' to not support monitoring.
    VariableCharacteristics c;
    c.minLimit = 10;
    c.dataType = DataEnum::integer;
    c.supportsMonitoring = false;
    c.unit = "s";

    device_model_test_helper.update_variable_characteristics(c, "AlignedDataCtrlr", std::nullopt, std::nullopt,
                                                             std::nullopt, "Interval", std::nullopt);

    dm = device_model_test_helper.get_device_model();

    std::vector<MonitoringCriterionEnum> criteria = {
        MonitoringCriterionEnum::DeltaMonitoring,
        MonitoringCriterionEnum::PeriodicMonitoring,
        MonitoringCriterionEnum::ThresholdMonitoring,
    };

    EVSE evse;
    evse.id = 1;
    evse.connectorId = 1;

    Component component1;
    component1.name = "Connector";
    component1.evse = evse;

    Component component2;
    component2.name = "AlignedDataCtrlr";

    Variable variable_comp1;
    variable_comp1.name = "SupplyPhases";
    Variable variable_comp2;
    variable_comp2.name = "Interval";

    std::vector<ComponentVariable> components = {
        {component1, variable_comp1},
        {component2, variable_comp2},
    };

    SetMonitoringData req_one;
    req_one.value = 0.0;
    req_one.type = MonitorEnum::PeriodicClockAligned;
    req_one.severity = 7;
    req_one.component = component1;
    req_one.variable = variable_comp1;
    SetMonitoringData req_two;
    req_two.value = 4.579;
    req_two.type = MonitorEnum::UpperThreshold;
    req_two.severity = 3;
    req_two.component = component2;
    req_two.variable = variable_comp2;

    std::vector<SetMonitoringData> requests;
    requests.push_back(req_one);
    requests.push_back(req_two);

    auto results_set = dm->set_monitors(requests);
    ASSERT_EQ(results_set.size(), requests.size());

    auto results_get = dm->get_monitors(criteria, components);
    ASSERT_EQ(results_get.size(), 1);

    ASSERT_EQ(results_get[0].variableMonitoring.size(), 1);
    auto monitor1 = results_get[0].variableMonitoring[0];

    ASSERT_EQ(monitor1.severity, 7);
    ASSERT_EQ(monitor1.type, MonitorEnum::PeriodicClockAligned);
}

TEST_F(DeviceModelTest, test_clear_monitors) {
    std::vector<MonitoringCriterionEnum> criteria = {
        MonitoringCriterionEnum::DeltaMonitoring,
        MonitoringCriterionEnum::PeriodicMonitoring,
        MonitoringCriterionEnum::ThresholdMonitoring,
    };

    EVSE evse;
    evse.id = 1;
    evse.connectorId = 1;

    Component component1;
    component1.name = "Connector";
    component1.evse = evse;
    Component component2;
    component2.name = "AlignedDataCtrlr";

    Variable variable_comp1;
    variable_comp1.name = "SupplyPhases";
    Variable variable_comp2;
    variable_comp2.name = "Interval";

    std::vector<ComponentVariable> components = {
        {component1, variable_comp1},
        {component2, variable_comp2},
    };

    SetMonitoringData req_one;
    req_one.value = 0.0;
    req_one.type = MonitorEnum::PeriodicClockAligned;
    req_one.severity = 7;
    req_one.component = component1;
    req_one.variable = variable_comp1;
    SetMonitoringData req_two;
    req_two.value = 4.579;
    req_two.type = MonitorEnum::UpperThreshold;
    req_two.severity = 3;
    req_two.component = component2;
    req_two.variable = variable_comp2;

    std::vector<SetMonitoringData> requests;
    requests.push_back(req_one);
    requests.push_back(req_two);

    auto results_set = dm->set_monitors(requests);

    // Insert some monitors that are hard-wired
    SetMonitoringData hardwired_one;
    hardwired_one.value = 0.0;
    hardwired_one.type = MonitorEnum::PeriodicClockAligned;
    hardwired_one.severity = 5;
    hardwired_one.component = component1;
    hardwired_one.variable = variable_comp1;
    SetMonitoringData hardwired_two;
    hardwired_two.value = 8.579;
    hardwired_two.type = MonitorEnum::UpperThreshold;
    hardwired_two.severity = 2;
    hardwired_two.component = component2;
    hardwired_two.variable = variable_comp2;

    std::vector<SetMonitoringData> requests2;
    requests2.push_back(hardwired_one);
    requests2.push_back(hardwired_two);

    auto set_result = dm->set_monitors(requests2, VariableMonitorType::HardWiredMonitor);
    std::vector<int> hardwired_monitor_ids;

    ASSERT_EQ(set_result.size(), 2);
    ASSERT_EQ(set_result[0].status, SetMonitoringStatusEnum::Accepted);
    ASSERT_EQ(set_result[1].status, SetMonitoringStatusEnum::Rejected);

    for (auto& res : set_result) {
        if (res.status == SetMonitoringStatusEnum::Accepted) {
            hardwired_monitor_ids.push_back(res.id.value());
        }
    }

    auto current_results = dm->get_monitors(criteria, components);

    // Delete all found IDs
    std::vector<int> to_delete;
    for (auto& result : current_results) {
        for (auto& monitor : result.variableMonitoring) {
            to_delete.push_back(monitor.id);
        }
    }

    auto clear_result = dm->clear_custom_monitors();
    ASSERT_EQ(clear_result, 1); // 1 custom should be deleted, since 1 is rejected

    auto results = dm->get_monitors(criteria, components);
    ASSERT_EQ(results.size(), 1);

    for (auto& result : results) {
        // Each have 1 hardwired monitor
        ASSERT_EQ(result.variableMonitoring.size(), 1);
    }

    // All must be rejected
    auto res_clear = dm->clear_monitors(hardwired_monitor_ids);

    for (auto& result : res_clear) {
        ASSERT_EQ(result.status, ClearMonitoringStatusEnum::Rejected);
    }

    // Clear all for next test iteration
    dm->clear_monitors(hardwired_monitor_ids, true);
}

/// \brief Tests check_integrity does not raise error for valid database
TEST_F(DeviceModelTest, test_check_integrity_valid) {
    EXPECT_NO_THROW(dm->check_integrity(evse_connector_structure));
}

/// \brief Tests check_integrity raises exception for invalid database
TEST_F(DeviceModelTest, test_check_integrity_invalid_missing_required_variable) {
    // Remove a required variable from the databsae.
    device_model_test_helper.remove_variable_from_db("DisplayMessageCtrlr", std::nullopt, std::nullopt, std::nullopt,
                                                     "DisplayMessages", std::nullopt);
    dm = device_model_test_helper.get_device_model();
    // Set the display message ctrlr to available.
    dm->set_value(ControllerComponentVariables::DisplayMessageCtrlrAvailable.component,
                  ControllerComponentVariables::DisplayMessageCtrlrAvailable.variable.value(), AttributeEnum::Actual,
                  "true", "test", true);
    // This should throw an exception as the display message ctrlr is available, but there is a required variable
    // missing.
    EXPECT_THROW(dm->check_integrity(evse_connector_structure), DeviceModelError);

    // Set display message ctrlr to unavailable.
    dm->set_value(ControllerComponentVariables::DisplayMessageCtrlrAvailable.component,
                  ControllerComponentVariables::DisplayMessageCtrlrAvailable.variable.value(), AttributeEnum::Actual,
                  "false", "test", true);

    // This should now pass.
    EXPECT_NO_THROW(dm->check_integrity(evse_connector_structure));

    // If we completely remove the variable.
    device_model_test_helper.remove_variable_from_db("DisplayMessageCtrlr", std::nullopt, std::nullopt, std::nullopt,
                                                     "Available", std::nullopt);
    dm = device_model_test_helper.get_device_model();
    // Then it should pass as well.
    EXPECT_NO_THROW(dm->check_integrity(evse_connector_structure));
}

TEST_F(DeviceModelTest, test_check_integrity_invalid_missing_required_variable_value) {
    // Remove a required variable value from the database
    device_model_test_helper.set_variable_attribute_value_null("SecurityCtrlr", std::nullopt, std::nullopt,
                                                               std::nullopt, "OrganizationName", std::nullopt,
                                                               AttributeEnum::Actual);
    dm = device_model_test_helper.get_device_model();
    // This should throw an exception.
    EXPECT_THROW(dm->check_integrity(evse_connector_structure), DeviceModelError);
}

TEST_F(DeviceModelTest, test_get_variable_unset_as_empty_string_is_accepted_and_empty) {
    const auto& organization_name = ControllerComponentVariables::OrganizationName;
    ASSERT_TRUE(device_model_test_helper.set_variable_attribute_value_null("SecurityCtrlr", std::nullopt, std::nullopt,
                                                                           std::nullopt, "OrganizationName",
                                                                           std::nullopt, AttributeEnum::Actual));
    dm = device_model_test_helper.get_device_model();

    std::string value = "stale";
    EXPECT_EQ(dm->get_variable(organization_name.component, organization_name.variable.value(), AttributeEnum::Actual,
                               value, false, UnsetValue::EmptyString),
              GetVariableStatusEnum::Accepted);
    EXPECT_EQ(value, "");

    // internal reads keep treating a missing value as absent
    EXPECT_EQ(dm->get_variable(organization_name.component, organization_name.variable.value(), AttributeEnum::Actual,
                               value, false, UnsetValue::NotSupported),
              GetVariableStatusEnum::NotSupportedAttributeType);
    EXPECT_FALSE(dm->get_optional_value<std::string>(organization_name).has_value());
}

TEST_F(DeviceModelTest, test_get_variable_unset_as_empty_string_unknown_attribute_type_is_not_supported) {
    const auto& organization_name = ControllerComponentVariables::OrganizationName;
    std::string value;
    EXPECT_EQ(dm->get_variable(organization_name.component, organization_name.variable.value(), AttributeEnum::Target,
                               value, false, UnsetValue::EmptyString),
              GetVariableStatusEnum::NotSupportedAttributeType);
}

TEST_F(DeviceModelTest, test_get_variable_unset_as_empty_string_write_only_without_value_is_rejected) {
    const auto& basic_auth_password = ControllerComponentVariables::BasicAuthPassword;
    ASSERT_TRUE(device_model_test_helper.set_variable_attribute_value_null("SecurityCtrlr", std::nullopt, std::nullopt,
                                                                           std::nullopt, "BasicAuthPassword",
                                                                           std::nullopt, AttributeEnum::Actual));
    dm = device_model_test_helper.get_device_model();

    std::string value;
    EXPECT_EQ(dm->get_variable(basic_auth_password.component, basic_auth_password.variable.value(),
                               AttributeEnum::Actual, value, false, UnsetValue::EmptyString),
              GetVariableStatusEnum::Rejected);
}

TEST_F(DeviceModelTest, test_get_variable_write_only_without_value_is_rejected_before_unset_check) {
    const auto& basic_auth_password = ControllerComponentVariables::BasicAuthPassword;
    ASSERT_TRUE(device_model_test_helper.set_variable_attribute_value_null("SecurityCtrlr", std::nullopt, std::nullopt,
                                                                           std::nullopt, "BasicAuthPassword",
                                                                           std::nullopt, AttributeEnum::Actual));
    dm = device_model_test_helper.get_device_model();

    std::string value;
    EXPECT_EQ(dm->get_variable(basic_auth_password.component, basic_auth_password.variable.value(),
                               AttributeEnum::Actual, value, false, UnsetValue::NotSupported),
              GetVariableStatusEnum::Rejected);
    EXPECT_EQ(dm->request_value<std::string>(basic_auth_password.component, basic_auth_password.variable.value(),
                                             AttributeEnum::Actual)
                  .status,
              GetVariableStatusEnum::Rejected);
    EXPECT_EQ(dm->get_variable(basic_auth_password.component, basic_auth_password.variable.value(),
                               AttributeEnum::Actual, value, true, UnsetValue::NotSupported),
              GetVariableStatusEnum::NotSupportedAttributeType);
}

TEST_F(DeviceModelTest, test_check_integrity_no_supported_versions_found) {
    // There are no known supported ocpp versions.
    dm->set_value(ControllerComponentVariables::SupportedOcppVersions.component,
                  ControllerComponentVariables::SupportedOcppVersions.variable.value(), AttributeEnum::Actual, "",
                  "test", true);

    // This should throw an exception.
    EXPECT_THROW(dm->check_integrity(evse_connector_structure), DeviceModelError);
}

TEST_F(DeviceModelTest, test_check_integrity_wrong_connector_structure) {
    std::map<std::int32_t, std::int32_t> evse_connector_structure{{1, 3}, {2, 1}, {3, 1}};
    EXPECT_THROW(dm->check_integrity(evse_connector_structure), DeviceModelError);

    evse_connector_structure = std::map<std::int32_t, std::int32_t>{{1, 3}, {2, 1}};
    EXPECT_THROW(dm->check_integrity(evse_connector_structure), DeviceModelError);

    evse_connector_structure = std::map<std::int32_t, std::int32_t>{{1, 1}};
    EXPECT_THROW(dm->check_integrity(evse_connector_structure), DeviceModelError);
}

TEST_F(DeviceModelTest, test_check_integrity_missing_required_evse_variable) {
    // Set required EVSE variable value to NULL.
    device_model_test_helper.set_variable_attribute_value_null("EVSE", std::nullopt, 1, std::nullopt, "SupplyPhases",
                                                               std::nullopt, AttributeEnum::Actual);
    dm = device_model_test_helper.get_device_model();
    // This should throw an exception.
    EXPECT_THROW(dm->check_integrity(evse_connector_structure), DeviceModelError);

    // Now remove the complete variable.
    device_model_test_helper.remove_variable_from_db("EVSE", std::nullopt, 1, std::nullopt, "SupplyPhases",
                                                     std::nullopt);
    dm = device_model_test_helper.get_device_model();
    // This should also throw an exception.
    EXPECT_THROW(dm->check_integrity(evse_connector_structure), DeviceModelError);
}

TEST_F(DeviceModelTest, test_check_integrity_missing_required_connector_variable) {
    // Set required EVSE variable value to NULL.
    device_model_test_helper.set_variable_attribute_value_null("Connector", std::nullopt, 1, 1, "ConnectorType",
                                                               std::nullopt, AttributeEnum::Actual);
    dm = device_model_test_helper.get_device_model();
    // This should throw an exception.
    EXPECT_THROW(dm->check_integrity(evse_connector_structure), DeviceModelError);

    // Now remove the complete variable.
    device_model_test_helper.remove_variable_from_db("Connector", std::nullopt, 1, 1, "ConnectorType", std::nullopt);
    dm = device_model_test_helper.get_device_model();
    // This should also throw an exception.
    EXPECT_THROW(dm->check_integrity(evse_connector_structure), DeviceModelError);
}

TEST_F(DeviceModelTest, test_check_integrity_missing_required_evse_power) {
    // Remove maxLimit of 'Power' variable of EVSE.
    VariableCharacteristics c;
    c.maxLimit = std::nullopt;
    c.dataType = DataEnum::decimal;
    c.supportsMonitoring = true;
    c.unit = "W";
    device_model_test_helper.update_variable_characteristics(c, "EVSE", std::nullopt, 1, std::nullopt, "Power",
                                                             std::nullopt);
    dm = device_model_test_helper.get_device_model();
    // This should throw an exception.
    EXPECT_THROW(dm->check_integrity(evse_connector_structure), DeviceModelError);

    // Now remove the complete variable.
    device_model_test_helper.remove_variable_from_db("EVSE", std::nullopt, 1, std::nullopt, "Power", std::nullopt);
    dm = device_model_test_helper.get_device_model();
    // This should also throw an exception.
    EXPECT_THROW(dm->check_integrity(evse_connector_structure), DeviceModelError);
}

namespace {
void clear_all_monitors(DeviceModel& dm, const Component& component, const Variable& variable) {
    for (const auto& result : dm.get_monitors({}, {{component, variable}})) {
        std::vector<std::int32_t> ids;
        for (const auto& monitor : result.variableMonitoring) {
            ids.push_back(monitor.id);
        }
        dm.clear_monitors(ids, true);
    }
}

SetMonitoringData make_monitor_request(const Component& component, const Variable& variable, const MonitorEnum type,
                                       const float value, const std::int32_t severity) {
    SetMonitoringData request;
    request.component = component;
    request.variable = variable;
    request.type = type;
    request.value = value;
    request.severity = severity;
    return request;
}
} // namespace

/// N04.FR.16: an id that belongs to a monitor on another variable is rejected, and that monitor stays where it is
TEST_F(DeviceModelTest, test_set_monitor_with_id_of_other_variable_is_rejected) {
    const auto& interval = ControllerComponentVariables::AlignedDataInterval;
    const auto& tx_ended_interval = ControllerComponentVariables::AlignedDataTxEndedInterval;
    clear_all_monitors(*dm, interval.component, interval.variable.value());
    clear_all_monitors(*dm, tx_ended_interval.component, tx_ended_interval.variable.value());

    const auto set_results = dm->set_monitors(
        {make_monitor_request(interval.component, interval.variable.value(), MonitorEnum::UpperThreshold, 1000.0F, 3)});
    ASSERT_EQ(set_results.size(), 1);
    ASSERT_EQ(set_results[0].status, SetMonitoringStatusEnum::Accepted);
    const auto monitor_id = set_results[0].id.value();

    auto move_request = make_monitor_request(tx_ended_interval.component, tx_ended_interval.variable.value(),
                                             MonitorEnum::UpperThreshold, 100.0F, 3);
    move_request.id = monitor_id;
    const auto move_results = dm->set_monitors({move_request});
    ASSERT_EQ(move_results.size(), 1);
    EXPECT_EQ(move_results[0].status, SetMonitoringStatusEnum::Rejected);

    const auto interval_meta = dm->get_variable_meta_data(interval.component, interval.variable.value());
    ASSERT_TRUE(interval_meta.has_value());
    ASSERT_EQ(interval_meta->monitors.count(monitor_id), 1);
    EXPECT_EQ(interval_meta->monitors.at(monitor_id).monitor.value, 1000.0F);
    const auto tx_meta = dm->get_variable_meta_data(tx_ended_interval.component, tx_ended_interval.variable.value());
    ASSERT_TRUE(tx_meta.has_value());
    EXPECT_TRUE(tx_meta->monitors.empty());

    // The same id on its own variable is still an update
    auto update_request =
        make_monitor_request(interval.component, interval.variable.value(), MonitorEnum::UpperThreshold, 1200.0F, 3);
    update_request.id = monitor_id;
    const auto update_results = dm->set_monitors({update_request});
    ASSERT_EQ(update_results.size(), 1);
    EXPECT_EQ(update_results[0].status, SetMonitoringStatusEnum::Accepted);
}

/// N07.FR.18: a numeric Delta monitor measures from the value of its last trigger
TEST_F(DeviceModelTest, test_numeric_delta_monitor_reference_follows_trigger) {
    const auto& interval = ControllerComponentVariables::AlignedDataInterval;
    const auto& enabled = ControllerComponentVariables::MonitoringCtrlrEnabled;
    clear_all_monitors(*dm, interval.component, interval.variable.value());
    ASSERT_EQ(dm->set_value(interval.component, interval.variable.value(), AttributeEnum::Actual, "900", "test"),
              SetVariableStatusEnum::Accepted);
    ASSERT_EQ(dm->set_value(enabled.component, enabled.variable.value(), AttributeEnum::Actual, "true", "test"),
              SetVariableStatusEnum::Accepted);

    everest::lib::util::monitor<std::vector<EventData>> sent_events;
    const auto sent = [&sent_events]() { return *sent_events.handle(); };
    MonitoringUpdater updater(
        *dm,
        [&sent_events](const std::vector<EventData>& events) {
            auto handle = sent_events.handle();
            handle->insert(handle->end(), events.begin(), events.end());
        },
        []() { return false; });
    updater.start_monitoring();

    const auto results = dm->set_monitors(
        {make_monitor_request(interval.component, interval.variable.value(), MonitorEnum::Delta, 50.0F, 3)});
    ASSERT_EQ(results.size(), 1);
    ASSERT_EQ(results[0].status, SetMonitoringStatusEnum::Accepted);
    const auto monitor_id = results[0].id.value();

    const auto reference = [&]() {
        return dm->get_variable_meta_data(interval.component, interval.variable.value())
            ->monitors.at(monitor_id)
            .reference_value;
    };
    EXPECT_EQ(reference(), "900");

    // 960 deviates 60 from 900: triggers, and becomes the reference once the event is sent
    ASSERT_EQ(dm->set_value(interval.component, interval.variable.value(), AttributeEnum::Actual, "960", "test"),
              SetVariableStatusEnum::Accepted);
    updater.process_triggered_monitors();
    ASSERT_EQ(sent().size(), 1);
    EXPECT_EQ(sent()[0].actualValue, "960");
    EXPECT_EQ(reference(), "960");

    // 1000 deviates only 40 from 960: no trigger, although it deviates 100 from the initial value
    ASSERT_EQ(dm->set_value(interval.component, interval.variable.value(), AttributeEnum::Actual, "1000", "test"),
              SetVariableStatusEnum::Accepted);
    EXPECT_EQ(reference(), "960");
    updater.process_triggered_monitors();
    EXPECT_EQ(sent().size(), 1);

    updater.stop_monitoring();
    clear_all_monitors(*dm, interval.component, interval.variable.value());
}

/// N07.FR.18: a Delta event dropped while offline does not move the reference, so the CSMS still learns of the change
TEST_F(DeviceModelTest, test_numeric_delta_monitor_reference_kept_when_event_dropped_offline) {
    const auto& interval = ControllerComponentVariables::AlignedDataInterval;
    const auto& enabled = ControllerComponentVariables::MonitoringCtrlrEnabled;
    const auto& offline_severity = ControllerComponentVariables::OfflineQueuingSeverity;
    clear_all_monitors(*dm, interval.component, interval.variable.value());
    ASSERT_EQ(dm->set_value(interval.component, interval.variable.value(), AttributeEnum::Actual, "900", "test"),
              SetVariableStatusEnum::Accepted);
    ASSERT_EQ(dm->set_value(enabled.component, enabled.variable.value(), AttributeEnum::Actual, "true", "test"),
              SetVariableStatusEnum::Accepted);
    ASSERT_EQ(dm->set_value(offline_severity.component, offline_severity.variable.value(), AttributeEnum::Actual, "2",
                            "test"),
              SetVariableStatusEnum::Accepted);

    everest::lib::util::monitor<std::vector<EventData>> sent_events;
    const auto sent = [&sent_events]() { return *sent_events.handle(); };
    std::atomic<bool> offline{false};
    MonitoringUpdater updater(
        *dm,
        [&sent_events](const std::vector<EventData>& events) {
            auto handle = sent_events.handle();
            handle->insert(handle->end(), events.begin(), events.end());
        },
        [&offline]() { return offline.load(); });
    updater.start_monitoring();

    const auto results = dm->set_monitors(
        {make_monitor_request(interval.component, interval.variable.value(), MonitorEnum::Delta, 50.0F, 5)});
    ASSERT_EQ(results.size(), 1);
    ASSERT_EQ(results[0].status, SetMonitoringStatusEnum::Accepted);
    const auto monitor_id = results[0].id.value();

    const auto reference = [&]() {
        return dm->get_variable_meta_data(interval.component, interval.variable.value())
            ->monitors.at(monitor_id)
            .reference_value;
    };

    // Severity 5 is above OfflineQueuingSeverity 2: the event is dropped and the reference stays
    offline = true;
    ASSERT_EQ(dm->set_value(interval.component, interval.variable.value(), AttributeEnum::Actual, "960", "test"),
              SetVariableStatusEnum::Accepted);
    updater.process_triggered_monitors();
    EXPECT_TRUE(sent().empty());
    EXPECT_EQ(reference(), "900");

    // 1000 deviates only 40 from 960, but 100 from the last value the CSMS was told about
    offline = false;
    ASSERT_EQ(dm->set_value(interval.component, interval.variable.value(), AttributeEnum::Actual, "1000", "test"),
              SetVariableStatusEnum::Accepted);
    updater.process_triggered_monitors();
    ASSERT_EQ(sent().size(), 1);
    EXPECT_EQ(sent()[0].actualValue, "1000");
    EXPECT_EQ(reference(), "1000");

    updater.stop_monitoring();
    clear_all_monitors(*dm, interval.component, interval.variable.value());
}

/// Variable changes evaluate monitors while concurrent processing runs deliver the events in the order they were
/// generated
TEST_F(DeviceModelTest, test_monitoring_updater_concurrent_changes_and_processing) {
    const auto& interval = ControllerComponentVariables::AlignedDataInterval;
    const auto& enabled = ControllerComponentVariables::MonitoringCtrlrEnabled;
    clear_all_monitors(*dm, interval.component, interval.variable.value());
    ASSERT_EQ(dm->set_value(enabled.component, enabled.variable.value(), AttributeEnum::Actual, "true", "test"),
              SetVariableStatusEnum::Accepted);

    everest::lib::util::monitor<std::vector<EventData>> sent_events;
    MonitoringUpdater updater(
        *dm,
        [&sent_events](const std::vector<EventData>& events) {
            // Widens the window between collecting and delivering events
            std::this_thread::sleep_for(std::chrono::microseconds(200));
            auto handle = sent_events.handle();
            handle->insert(handle->end(), events.begin(), events.end());
        },
        []() { return false; });
    updater.start_monitoring();

    const auto results = dm->set_monitors(
        {make_monitor_request(interval.component, interval.variable.value(), MonitorEnum::Delta, 1.0F, 3),
         make_monitor_request(interval.component, interval.variable.value(), MonitorEnum::UpperThreshold, 500.0F, 4)});
    ASSERT_EQ(results.size(), 2);
    ASSERT_EQ(results[0].status, SetMonitoringStatusEnum::Accepted);
    ASSERT_EQ(results[1].status, SetMonitoringStatusEnum::Accepted);

    std::atomic<bool> done{false};
    const auto process = [&]() {
        while (!done) {
            updater.process_triggered_monitors();
        }
    };
    std::thread processor(process);
    std::thread other_processor(process);
    for (int i = 0; i < 500; i++) {
        dm->set_value(interval.component, interval.variable.value(), AttributeEnum::Actual,
                      std::to_string(i % 2 == 0 ? 100 + i : 900 + i), "test");
    }
    done = true;
    processor.join();
    other_processor.join();
    updater.process_triggered_monitors();
    updater.stop_monitoring();

    const auto events = *sent_events.handle();
    ASSERT_FALSE(events.empty());
    for (std::size_t i = 1; i < events.size(); i++) {
        EXPECT_LT(events.at(i - 1).eventId, events.at(i).eventId);
    }
    clear_all_monitors(*dm, interval.component, interval.variable.value());
}

} // namespace v2
} // namespace ocpp
