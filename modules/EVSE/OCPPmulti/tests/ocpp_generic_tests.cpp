// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <conversions.hpp>
#include <generic_ocpp.hpp>
#include <optional>

#include "generated/types/ocpp.hpp"
#include "ocpp/v2/ocpp_enums.hpp"
#include "ocpp/v2/ocpp_types.hpp"
#include "stubs/generic_ocpp_stub.hpp"

namespace {
using namespace stubs;
using ::testing::_;
using ::testing::InSequence;
using ::testing::Return;

TEST_F(GenericOcppProvidesTester, stopRestart) {
    InSequence seq;
    EXPECT_TRUE(ocpp.charging_schedules_timer_running());
    EXPECT_CALL(chargepoint, stop());
    auto result = ocpp.handle_stop();
    EXPECT_TRUE(result);
    EXPECT_FALSE(ocpp.charging_schedules_timer_running());

    EXPECT_CALL(chargepoint, start(ocpp::v2::BootReasonEnum::ApplicationReset, true));
    result = ocpp.handle_restart();
    EXPECT_TRUE(result);
    EXPECT_TRUE(ocpp.charging_schedules_timer_running());
}

TEST(GenericOcppProvides, stopRestartOffline) {
    // called before run - so should be rejected

    stubs::ChargePointStub chargepoint;
    stubs::ConfigStub config;
    stubs::ModuleInterfaces interfaces;

    // connect required interfaces
    interfaces.add_charger_information("info");
    interfaces.add_data_transfer("data_transfer");
    interfaces.add_display_message("display");
    interfaces.add_evse_energy_sink("energy_node", 1);
    interfaces.add_evse_manager("evse_manager_1");
    interfaces.add_evse_manager("evse_manager_2");
    interfaces.add_extensions_15118("evsev2g");
    interfaces.add_reservation("reservation");

    chargepoint.load_store("default_store.json");

    // GenericOcpp object
    stubs::GenericOcppTester ocpp(chargepoint, interfaces.get_module_info(), config, interfaces.get_provides(),
                                  interfaces.get_requires());

    ocpp.init();

    EXPECT_FALSE(ocpp.charging_schedules_timer_running());
    auto result = ocpp.handle_stop();
    EXPECT_FALSE(ocpp.charging_schedules_timer_running());
    EXPECT_FALSE(result);
    result = ocpp.handle_restart();
    EXPECT_FALSE(result);
    EXPECT_FALSE(ocpp.charging_schedules_timer_running());
}

TEST_F(GenericOcppProvidesTester, securityEvent) {
    using types::ocpp::SecurityEvent;
    SecurityEvent event{"Cover removed", "USB port", false};

    ocpp::CiString<50> type{event.type};
    std::optional<ocpp::CiString<255>> info{event.info};
    EXPECT_CALL(chargepoint, on_security_event(type, info, event.critical, _));

    ocpp.handle_security_event(event);
}

TEST_F(GenericOcppProvidesTester, getVariablesNone) {
    using module::conversions::to_ocpp_get_variable_data_vector;
    using types::ocpp::GetVariableRequest;
    using types::ocpp::GetVariableResult;

    // no variables
    std::vector<GetVariableRequest> request;

    const auto request_input = to_ocpp_get_variable_data_vector(request);
    const std::vector<ocpp::v2::GetVariableResult> request_output;
    EXPECT_CALL(chargepoint, get_variables(request_input)).WillOnce(Return(request_output));

    const auto result = ocpp.handle_get_variables(request);
    EXPECT_TRUE(result.empty());
}

TEST_F(GenericOcppProvidesTester, getVariablesOne) {
    using module::conversions::to_ocpp_get_variable_data_vector;
    using types::ocpp::GetVariableRequest;
    using types::ocpp::GetVariableResult;
    using types::ocpp::GetVariableStatusEnumType;

    std::vector<GetVariableRequest> request;
    const GetVariableRequest req{{{"Component"}, {"Variable"}}, std::nullopt};
    request.push_back(req);

    const auto request_input = to_ocpp_get_variable_data_vector(request);
    std::vector<ocpp::v2::GetVariableResult> request_output;
    const ocpp::v2::GetVariableResult res{ocpp::v2::GetVariableStatusEnum::Accepted,
                                          {"Component"},
                                          {"Variable"},
                                          std::nullopt,
                                          std::nullopt,
                                          "Value",
                                          std::nullopt};
    request_output.push_back(res);
    EXPECT_CALL(chargepoint, get_variables(request_input)).WillOnce(Return(request_output));

    const auto result = ocpp.handle_get_variables(request);
    EXPECT_FALSE(result.empty());
    ASSERT_EQ(result.size(), 1);
    const auto& entry = result[0];
    EXPECT_EQ(entry.status, GetVariableStatusEnumType::Accepted);
    EXPECT_EQ(entry.component_variable, req.component_variable);
    EXPECT_EQ(entry.value.value_or(""), res.attributeValue.value());
}

TEST_F(GenericOcppProvidesTester, getVariablesMany) {
    using module::conversions::to_ocpp_get_variable_data_vector;
    using types::ocpp::AttributeEnum;
    using types::ocpp::GetVariableRequest;
    using types::ocpp::GetVariableResult;
    using types::ocpp::GetVariableStatusEnumType;

    std::vector<GetVariableRequest> request;
    const GetVariableRequest req1{{{"Component1"}, {"Variable1"}}, AttributeEnum::Actual};
    const GetVariableRequest req2{{{"Component2"}, {"Variable2"}}, AttributeEnum::MaxSet};
    request.push_back(req1);
    request.push_back(req2);

    const auto request_input = to_ocpp_get_variable_data_vector(request);
    std::vector<ocpp::v2::GetVariableResult> request_output;
    const ocpp::v2::GetVariableResult res1{
        ocpp::v2::GetVariableStatusEnum::Accepted, {"Component1"}, {"Variable1"}, std::nullopt,
        ocpp::v2::AttributeEnum::Actual,           "Value1",       std::nullopt};
    const ocpp::v2::GetVariableResult res2{ocpp::v2::GetVariableStatusEnum::NotSupportedAttributeType,
                                           {"Component2"},
                                           {"Variable2"},
                                           std::nullopt,
                                           ocpp::v2::AttributeEnum::MaxSet,
                                           std::nullopt,
                                           std::nullopt};
    request_output.push_back(res1);
    request_output.push_back(res2);
    EXPECT_CALL(chargepoint, get_variables(request_input)).WillOnce(Return(request_output));

    const auto result = ocpp.handle_get_variables(request);
    EXPECT_FALSE(result.empty());
    ASSERT_EQ(result.size(), 2);
    const auto& entry1 = result[0];
    const auto& entry2 = result[1];

    EXPECT_EQ(entry1.status, GetVariableStatusEnumType::Accepted);
    EXPECT_EQ(entry1.component_variable, req1.component_variable);
    EXPECT_EQ(entry1.attribute_type.value_or(AttributeEnum::Target), AttributeEnum::Actual);
    EXPECT_EQ(entry1.value.value_or(""), res1.attributeValue.value());

    EXPECT_EQ(entry2.status, GetVariableStatusEnumType::NotSupportedAttributeType);
    EXPECT_EQ(entry2.component_variable, req2.component_variable);
    EXPECT_EQ(entry2.attribute_type.value_or(AttributeEnum::Target), AttributeEnum::MaxSet);
    EXPECT_EQ(entry2.value, res2.attributeValue);
}

TEST(GenericOcppProvides, getVariablesOffline) {
    // called before run - so should be rejected

    stubs::ChargePointStub chargepoint;
    stubs::ConfigStub config;
    stubs::ModuleInterfaces interfaces;

    // connect required interfaces
    interfaces.add_charger_information("info");
    interfaces.add_data_transfer("data_transfer");
    interfaces.add_display_message("display");
    interfaces.add_evse_energy_sink("energy_node", 1);
    interfaces.add_evse_manager("evse_manager_1");
    interfaces.add_evse_manager("evse_manager_2");
    interfaces.add_extensions_15118("evsev2g");
    interfaces.add_reservation("reservation");

    chargepoint.load_store("default_store.json");

    // GenericOcpp object
    stubs::GenericOcppTester ocpp(chargepoint, interfaces.get_module_info(), config, interfaces.get_provides(),
                                  interfaces.get_requires());

    ocpp.init();

    using module::conversions::to_ocpp_get_variable_data_vector;
    using types::ocpp::AttributeEnum;
    using types::ocpp::GetVariableRequest;
    using types::ocpp::GetVariableResult;
    using types::ocpp::GetVariableStatusEnumType;

    std::vector<GetVariableRequest> request;
    const GetVariableRequest req1{{{"Component1"}, {"Variable1"}}, AttributeEnum::Actual};
    const GetVariableRequest req2{{{"Component2"}, {"Variable2"}}, AttributeEnum::MaxSet};
    request.push_back(req1);
    request.push_back(req2);

    const auto request_input = to_ocpp_get_variable_data_vector(request);
    std::vector<ocpp::v2::GetVariableResult> request_output;
    const ocpp::v2::GetVariableResult res1{
        ocpp::v2::GetVariableStatusEnum::Accepted, {"Component1"}, {"Variable1"}, std::nullopt,
        ocpp::v2::AttributeEnum::Actual,           "Value1",       std::nullopt};

    EXPECT_CALL(chargepoint, get_variables(request_input)).Times(0);

    const auto result = ocpp.handle_get_variables(request);
    EXPECT_FALSE(result.empty());
    ASSERT_EQ(result.size(), 2);
    const auto& entry1 = result[0];
    const auto& entry2 = result[1];

    EXPECT_EQ(entry1.status, GetVariableStatusEnumType::Rejected);
    EXPECT_EQ(entry1.component_variable, req1.component_variable);
    EXPECT_EQ(entry1.attribute_type.value_or(AttributeEnum::Target), AttributeEnum::Actual);
    EXPECT_FALSE(entry1.value.has_value());

    EXPECT_EQ(entry2.status, GetVariableStatusEnumType::Rejected);
    EXPECT_EQ(entry2.component_variable, req2.component_variable);
    EXPECT_EQ(entry2.attribute_type.value_or(AttributeEnum::Target), AttributeEnum::MaxSet);
    EXPECT_FALSE(entry2.value.has_value());
}

TEST_F(GenericOcppProvidesTester, setVariablesNone) {
    using module::conversions::to_ocpp_set_variable_data_vector;
    using types::ocpp::SetVariableRequest;
    using types::ocpp::SetVariableResult;

    // no variables
    std::vector<SetVariableRequest> request;
    const std::string source{"HMI"};

    const auto request_input = to_ocpp_set_variable_data_vector(request);
    const std::map<ocpp::v2::SetVariableData, ocpp::v2::SetVariableResult> request_output;
    EXPECT_CALL(chargepoint, set_variables(request_input, source)).WillOnce(Return(request_output));

    const auto result = ocpp.handle_set_variables(request, source);
    EXPECT_TRUE(result.empty());
}

TEST_F(GenericOcppProvidesTester, setVariablesOne) {
    using module::conversions::to_ocpp_set_variable_data_vector;
    using ocpp::v2::SetVariableData;
    using types::ocpp::SetVariableRequest;
    using types::ocpp::SetVariableResult;
    using types::ocpp::SetVariableStatusEnumType;

    std::vector<SetVariableRequest> request;
    SetVariableRequest req{{{"Component"}, {"Variable"}}, {"Value"}};
    request.push_back(req);

    const std::string source{"HMI"};

    const auto request_input = to_ocpp_set_variable_data_vector(request);
    std::map<SetVariableData, ocpp::v2::SetVariableResult> request_output;
    SetVariableData res_key{"Value", {"Component"}, {"Variable"}};
    ocpp::v2::SetVariableResult res_value{ocpp::v2::SetVariableStatusEnum::Accepted, res_key.component,
                                          res_key.variable};
    request_output[res_key] = res_value;
    ASSERT_EQ(request_input.size(), 1);
    ASSERT_EQ(request_output.size(), 1);
    EXPECT_CALL(chargepoint, set_variables(request_input, source)).WillOnce(Return(request_output));

    const auto result = ocpp.handle_set_variables(request, source);
    EXPECT_FALSE(result.empty());
    ASSERT_EQ(result.size(), 1);
    const auto& entry = result[0];
    EXPECT_EQ(entry.status, SetVariableStatusEnumType::Accepted);
    EXPECT_EQ(entry.component_variable, req.component_variable);
    EXPECT_FALSE(entry.attribute_type.has_value());
}

TEST_F(GenericOcppProvidesTester, setVariablesMany) {
    using module::conversions::to_ocpp_set_variable_data_vector;
    using ocpp::v2::SetVariableData;
    using types::ocpp::AttributeEnum;
    using types::ocpp::SetVariableRequest;
    using types::ocpp::SetVariableResult;
    using types::ocpp::SetVariableStatusEnumType;

    std::vector<SetVariableRequest> request;
    SetVariableRequest req1{{{"Component1"}, {"Variable1"}}, {"Value1"}, AttributeEnum::MinSet};
    SetVariableRequest req2{{{"Component2"}, {"Variable2"}}, {"Value2"}};
    request.push_back(req1);
    request.push_back(req2);

    const std::string source{"HMI"};

    const auto request_input = to_ocpp_set_variable_data_vector(request);
    std::map<SetVariableData, ocpp::v2::SetVariableResult> request_output;
    SetVariableData res1_key{"Value1", {"Component1"}, {"Variable1"}};
    ocpp::v2::SetVariableResult res1_value{ocpp::v2::SetVariableStatusEnum::RebootRequired, res1_key.component,
                                           res1_key.variable, ocpp::v2::AttributeEnum::MinSet};
    SetVariableData res2_key{"Value2", {"Component2"}, {"Variable2"}};
    ocpp::v2::SetVariableResult res2_value{ocpp::v2::SetVariableStatusEnum::Accepted, res2_key.component,
                                           res2_key.variable};
    request_output[res1_key] = res1_value;
    request_output[res2_key] = res2_value;
    ASSERT_EQ(request_input.size(), 2);
    ASSERT_EQ(request_output.size(), 2);
    EXPECT_CALL(chargepoint, set_variables(request_input, source)).WillOnce(Return(request_output));

    const auto result = ocpp.handle_set_variables(request, source);
    EXPECT_FALSE(result.empty());
    ASSERT_EQ(result.size(), 2);
    const auto& entry1 = result[0];
    const auto& entry2 = result[1];

    EXPECT_EQ(entry1.status, SetVariableStatusEnumType::RebootRequired);
    EXPECT_EQ(entry1.component_variable, req1.component_variable);
    EXPECT_EQ(entry1.attribute_type.value_or(AttributeEnum::Target), AttributeEnum::MinSet);

    EXPECT_EQ(entry2.status, SetVariableStatusEnumType::Accepted);
    EXPECT_EQ(entry2.component_variable, req2.component_variable);
    EXPECT_FALSE(entry2.attribute_type.has_value());
}

TEST(GenericOcppProvides, setVariablesOffline) {
    // called before run - so should be rejected

    stubs::ChargePointStub chargepoint;
    stubs::ConfigStub config;
    stubs::ModuleInterfaces interfaces;

    // connect required interfaces
    interfaces.add_charger_information("info");
    interfaces.add_data_transfer("data_transfer");
    interfaces.add_display_message("display");
    interfaces.add_evse_energy_sink("energy_node", 1);
    interfaces.add_evse_manager("evse_manager_1");
    interfaces.add_evse_manager("evse_manager_2");
    interfaces.add_extensions_15118("evsev2g");
    interfaces.add_reservation("reservation");

    chargepoint.load_store("default_store.json");

    // GenericOcpp object
    stubs::GenericOcppTester ocpp(chargepoint, interfaces.get_module_info(), config, interfaces.get_provides(),
                                  interfaces.get_requires());

    ocpp.init();

    using module::conversions::to_ocpp_set_variable_data_vector;
    using ocpp::v2::SetVariableData;
    using types::ocpp::AttributeEnum;
    using types::ocpp::SetVariableRequest;
    using types::ocpp::SetVariableResult;
    using types::ocpp::SetVariableStatusEnumType;

    std::vector<SetVariableRequest> request;
    SetVariableRequest req1{{{"Component1"}, {"Variable1"}}, {"Value1"}, AttributeEnum::MinSet};
    SetVariableRequest req2{{{"Component2"}, {"Variable2"}}, {"Value2"}};
    request.push_back(req1);
    request.push_back(req2);

    const std::string source{"HMI"};

    const auto request_input = to_ocpp_set_variable_data_vector(request);
    EXPECT_CALL(chargepoint, set_variables(request_input, source)).Times(0);

    const auto result = ocpp.handle_set_variables(request, source);
    EXPECT_FALSE(result.empty());
    ASSERT_EQ(result.size(), 2);
    const auto& entry1 = result[0];
    const auto& entry2 = result[1];

    EXPECT_EQ(entry1.status, SetVariableStatusEnumType::Rejected);
    EXPECT_EQ(entry1.component_variable, req1.component_variable);
    EXPECT_EQ(entry1.attribute_type.value_or(AttributeEnum::Target), AttributeEnum::MinSet);

    EXPECT_EQ(entry2.status, SetVariableStatusEnumType::Rejected);
    EXPECT_EQ(entry2.component_variable, req2.component_variable);
    EXPECT_FALSE(entry2.attribute_type.has_value());
}

TEST_F(GenericOcppProvidesTester, changeAvailabilityInoperative) {
    using types::ocpp::ChangeAvailabilityRequest;
    using types::ocpp::ChangeAvailabilityResponse;
    using types::ocpp::ChangeAvailabilityStatusEnumType;
    using types::ocpp::EVSE;
    using types::ocpp::OperationalStatusEnumType;

    const EVSE evse{1, 1};
    const ChangeAvailabilityRequest request{OperationalStatusEnumType::Inoperative, evse};

    ocpp::v2::ChangeAvailabilityRequest request_input;
    request_input.operationalStatus = ocpp::v2::OperationalStatusEnum::Inoperative;
    request_input.evse = ocpp::v2::EVSE{1, 1};
    ocpp::v2::ChangeAvailabilityResponse request_output;
    request_output.status = ocpp::v2::ChangeAvailabilityStatusEnum::Accepted;
    EXPECT_CALL(chargepoint, on_change_availability(request_input)).WillOnce(Return(request_output));

    const auto result = ocpp.handle_change_availability(request);
    EXPECT_EQ(result.status, ChangeAvailabilityStatusEnumType::Accepted);
}

TEST_F(GenericOcppProvidesTester, changeAvailabilityRejected) {
    using types::ocpp::ChangeAvailabilityRequest;
    using types::ocpp::ChangeAvailabilityResponse;
    using types::ocpp::ChangeAvailabilityStatusEnumType;
    using types::ocpp::EVSE;
    using types::ocpp::OperationalStatusEnumType;

    const EVSE evse{1, 1};
    const ChangeAvailabilityRequest request{OperationalStatusEnumType::Operative, evse};

    ocpp::v2::ChangeAvailabilityRequest request_input;
    request_input.operationalStatus = ocpp::v2::OperationalStatusEnum::Operative;
    request_input.evse = ocpp::v2::EVSE{1, 1};
    ocpp::v2::ChangeAvailabilityResponse request_output;
    request_output.status = ocpp::v2::ChangeAvailabilityStatusEnum::Rejected;
    EXPECT_CALL(chargepoint, on_change_availability(request_input)).WillOnce(Return(request_output));

    const auto result = ocpp.handle_change_availability(request);
    EXPECT_EQ(result.status, ChangeAvailabilityStatusEnumType::Rejected);
}

bool contains(const ocpp_multi::GenericOcpp::MonitorList& list, const std::string& component,
              const std::string& variable) {
    using MonitorListEntry = ocpp_multi::GenericOcpp::MonitorListEntry;
    MonitorListEntry entry{{component}, {variable}};
    const auto it = list.find(entry);
    return it != list.end();
}

TEST_F(GenericOcppProvidesTester, monitorVariables) {
    using types::ocpp::ComponentVariable;

    std::vector<ComponentVariable> req1{{{"Component1"}, {"Variable1"}}};
    std::vector<ComponentVariable> req2{{{"Component2"}, {"Variable2"}}};

    EXPECT_CALL(chargepoint, register_variable_listener(_)).Times(1);

    EXPECT_TRUE(ocpp.get_monitor_list().empty());
    ocpp.handle_monitor_variables(req1);
    EXPECT_FALSE(ocpp.get_monitor_list().empty());
    EXPECT_EQ(ocpp.get_monitor_list().size(), 1);
    EXPECT_TRUE(contains(ocpp.get_monitor_list(), "Component1", "Variable1"));
    EXPECT_FALSE(contains(ocpp.get_monitor_list(), "Component2", "Variable2"));

    ocpp.handle_monitor_variables(req2);
    EXPECT_FALSE(ocpp.get_monitor_list().empty());
    EXPECT_EQ(ocpp.get_monitor_list().size(), 2);
    EXPECT_TRUE(contains(ocpp.get_monitor_list(), "Component1", "Variable1"));
    EXPECT_TRUE(contains(ocpp.get_monitor_list(), "Component2", "Variable2"));
}

} // namespace
