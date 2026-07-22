// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <everest/ocpp_module_common/conversions.hpp>
#include <generic_ocpp.hpp>
#include <optional>

#include "everest/logging.hpp"
#include "ocpp/v2/ocpp_types.hpp"
#include "stubs/generic_ocpp_stub.hpp"

namespace {
using namespace stubs;
using ::testing::_;
using ::testing::InSequence;
using ::testing::Return;

TEST(SteadyTimer, StartStop) {
    GTEST_SKIP() << "Unreliable - consider using a different timer";

    // currently unreliable
    Everest::SteadyTimer timer;
    EXPECT_FALSE(timer.is_running());
    timer.interval([]() { EVLOG_error << "timer expired"; }, std::chrono::seconds(500));
    EXPECT_TRUE(timer.is_running());
    timer.stop();
    EXPECT_FALSE(timer.is_running());
    timer.interval([]() { EVLOG_error << "timer expired"; }, std::chrono::seconds(500));
    EXPECT_TRUE(timer.is_running());
    timer.stop();
    EXPECT_FALSE(timer.is_running());
    timer.interval([]() { EVLOG_error << "timer expired"; }, std::chrono::seconds(500));
    EXPECT_TRUE(timer.is_running());
    timer.interval([]() { EVLOG_error << "timer expired"; }, std::chrono::seconds(500));
    EXPECT_TRUE(timer.is_running());
    timer.interval([]() { EVLOG_error << "timer expired"; }, std::chrono::seconds(500));
    EXPECT_TRUE(timer.is_running());
    timer.interval([]() { EVLOG_error << "timer expired"; }, std::chrono::seconds(500));
    EXPECT_TRUE(timer.is_running());
    timer.interval([]() { EVLOG_error << "timer expired"; }, std::chrono::seconds(500));
    EXPECT_TRUE(timer.is_running());
    timer.interval([]() { EVLOG_error << "timer expired"; }, std::chrono::seconds(500));
    EXPECT_TRUE(timer.is_running());
    timer.interval([]() { EVLOG_error << "timer expired"; }, std::chrono::seconds(500));
    EXPECT_TRUE(timer.is_running());
    timer.interval([]() { EVLOG_error << "timer expired"; }, std::chrono::seconds(500));
    EXPECT_TRUE(timer.is_running());
    timer.interval([]() { EVLOG_error << "timer expired"; }, std::chrono::seconds(500));
    EXPECT_TRUE(timer.is_running());
    timer.interval([]() { EVLOG_error << "timer expired"; }, std::chrono::seconds(500));
    EXPECT_TRUE(timer.is_running());
}

// ----------------------------------------------------------------------------
// Calls

TEST_F(GenericOcppProvidesTester, stopRestart) {
    GTEST_SKIP() << "Unreliable - consider using a different timer";

    // currently unreliable (see SteadyTimer.StartStop)

    InSequence seq;
    EXPECT_TRUE(ocpp->charging_schedules_timer_running());
    EXPECT_CALL(chargepoint, stop());
    auto result = ocpp->handle_stop();
    EXPECT_TRUE(result);
    EXPECT_FALSE(ocpp->charging_schedules_timer_running());

    EXPECT_CALL(chargepoint, restart()).WillOnce(Return(true));
    result = ocpp->handle_restart();
    EXPECT_TRUE(result);
    EXPECT_TRUE(ocpp->charging_schedules_timer_running());
    EXPECT_TRUE(ocpp->charging_schedules_timer_running());
    EXPECT_TRUE(ocpp->charging_schedules_timer_running());
    EXPECT_TRUE(ocpp->charging_schedules_timer_running());
    EXPECT_TRUE(ocpp->charging_schedules_timer_running());
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

    ocpp->handle_security_event(event);
}

TEST_F(GenericOcppProvidesTester, securityEventTooLarge) {
    using types::ocpp::SecurityEvent;
    SecurityEvent event{"CoverRemoved34567890123456789012345678901234567890A", "USB port", false};

    ocpp::CiString<50> type{event.type.substr(0, 50)};
    std::optional<ocpp::CiString<255>> info{event.info};
    EXPECT_CALL(chargepoint, on_security_event(type, info, event.critical, _));

    ocpp->handle_security_event(event);
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

    const auto result = ocpp->handle_get_variables(request);
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

    const auto result = ocpp->handle_get_variables(request);
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

    const auto result = ocpp->handle_get_variables(request);
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
    const std::vector<ocpp_multi::SetVariableOutcome> request_output;
    EXPECT_CALL(chargepoint, set_variables(request_input, source)).WillOnce(Return(request_output));

    const auto result = ocpp->handle_set_variables(request, source);
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
    std::vector<ocpp_multi::SetVariableOutcome> request_output;
    SetVariableData res_key{"Value", {"Component"}, {"Variable"}};
    ocpp::v2::SetVariableResult res_value{ocpp::v2::SetVariableStatusEnum::Accepted, res_key.component,
                                          res_key.variable};
    request_output.push_back({std::move(res_value), std::nullopt});
    ASSERT_EQ(request_input.size(), 1);
    ASSERT_EQ(request_output.size(), 1);
    EXPECT_CALL(chargepoint, set_variables(request_input, source)).WillOnce(Return(request_output));

    const auto result = ocpp->handle_set_variables(request, source);
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
    std::vector<ocpp_multi::SetVariableOutcome> request_output;
    SetVariableData res1_key{"Value1", {"Component1"}, {"Variable1"}};
    ocpp::v2::SetVariableResult res1_value{ocpp::v2::SetVariableStatusEnum::RebootRequired, res1_key.component,
                                           res1_key.variable, ocpp::v2::AttributeEnum::MinSet};
    SetVariableData res2_key{"Value2", {"Component2"}, {"Variable2"}};
    ocpp::v2::SetVariableResult res2_value{ocpp::v2::SetVariableStatusEnum::Accepted, res2_key.component,
                                           res2_key.variable};
    request_output.push_back({std::move(res1_value), std::nullopt});
    request_output.push_back({std::move(res2_value), std::nullopt});
    ASSERT_EQ(request_input.size(), 2);
    ASSERT_EQ(request_output.size(), 2);
    EXPECT_CALL(chargepoint, set_variables(request_input, source)).WillOnce(Return(request_output));

    const auto result = ocpp->handle_set_variables(request, source);
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

TEST_F(GenericOcppProvidesTester, setVariablesOutcomeMonitorValueDoesNotAffectReply) {
    // monitor_value is for internal monitor synthesis; the EVerest reply must unwrap
    // .result regardless of it
    using module::conversions::to_ocpp_set_variable_data_vector;
    using types::ocpp::SetVariableRequest;
    using types::ocpp::SetVariableStatusEnumType;

    std::vector<SetVariableRequest> request;
    SetVariableRequest req{{{"Component"}, {"Variable"}}, {"Value"}};
    request.push_back(req);

    const std::string source{"CSMS"};

    const auto request_input = to_ocpp_set_variable_data_vector(request);
    ocpp::v2::SetVariableResult res_value{ocpp::v2::SetVariableStatusEnum::Accepted, {"Component"}, {"Variable"}};
    std::vector<ocpp_multi::SetVariableOutcome> request_output;
    request_output.push_back({std::move(res_value), "Value"});
    EXPECT_CALL(chargepoint, set_variables(request_input, source)).WillOnce(Return(request_output));

    const auto result = ocpp->handle_set_variables(request, source);
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result[0].status, SetVariableStatusEnumType::Accepted);
    EXPECT_EQ(result[0].component_variable, req.component_variable);
}

TEST_F(GenericOcppProvidesTester, setVariablesSynthesizesMonitorEvent) {
    // a direct device-model write (v16 mode) fires monitors on the canonical CV;
    // outcomes without a monitor_value stay silent
    using module::conversions::to_ocpp_set_variable_data_vector;
    using ocpp::v2::Component;
    using ocpp::v2::Variable;
    using types::ocpp::ComponentVariable;
    using types::ocpp::SetVariableRequest;

    std::vector<json> received;
    interfaces->subscribe_var("ocpp_generic", "event_data",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    const Component component{"CustomCtrlr"};
    const Variable variable{"FreeVariable"};

    // monitor on the canonical CV (identity resolution via the stub default)
    EXPECT_CALL(chargepoint, resolve_to_canonical(component, variable)).Times(1);
    EXPECT_CALL(chargepoint, register_variable_listener(component, variable, _)).Times(1);
    std::vector<ComponentVariable> monitor_req{{{"CustomCtrlr"}, {"FreeVariable"}}};
    ocpp->handle_monitor_variables(monitor_req);

    const std::string source{"CSMS"};
    std::vector<SetVariableRequest> request;
    request.push_back({{{"CustomCtrlr"}, {"FreeVariable"}}, {"NewValue"}});
    const auto request_input = to_ocpp_set_variable_data_vector(request);

    ocpp::v2::SetVariableResult res{ocpp::v2::SetVariableStatusEnum::Accepted, component, variable};
    std::vector<ocpp_multi::SetVariableOutcome> direct_write_output;
    direct_write_output.push_back({res, "NewValue"});
    std::vector<ocpp_multi::SetVariableOutcome> key_backed_output;
    key_backed_output.push_back({res, std::nullopt});
    EXPECT_CALL(chargepoint, set_variables(request_input, source))
        .WillOnce(Return(direct_write_output))
        .WillOnce(Return(key_backed_output));

    ocpp->handle_set_variables(request, source);
    ASSERT_EQ(received.size(), 1);
    json expected =
        R"({"actual_value":"NewValue","component_variable":{"component":{"name":"CustomCtrlr"},"variable":{"name":"FreeVariable"}},"event_id":0,"event_notification_type":"CustomMonitor","timestamp":"x","trigger":"Alerting"})"_json;
    expected["timestamp"] = received[0]["timestamp"];
    EXPECT_EQ(received[0], expected);

    // no synthesis when the write went through the 1.6 key path
    ocpp->handle_set_variables(request, source);
    EXPECT_EQ(received.size(), 1);
}

TEST_F(GenericOcppProvidesTester, setVariablesSynthesizesMonitorEventRebootRequired) {
    // RebootRequired also counts as a committed direct write (e.g. ConnectionConfig CVs)
    using module::conversions::to_ocpp_set_variable_data_vector;
    using ocpp::v2::Component;
    using ocpp::v2::Variable;
    using types::ocpp::ComponentVariable;
    using types::ocpp::SetVariableRequest;

    std::vector<json> received;
    interfaces->subscribe_var("ocpp_generic", "event_data",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    const Component component{"InternalCtrlr"};
    const Variable variable{"NetworkConnectionProfiles"};

    EXPECT_CALL(chargepoint, register_variable_listener(component, variable, _)).Times(1);
    std::vector<ComponentVariable> monitor_req{{{"InternalCtrlr"}, {"NetworkConnectionProfiles"}}};
    ocpp->handle_monitor_variables(monitor_req);

    const std::string source{"CSMS"};
    std::vector<SetVariableRequest> request;
    request.push_back({{{"InternalCtrlr"}, {"NetworkConnectionProfiles"}}, {"NewValue"}});
    const auto request_input = to_ocpp_set_variable_data_vector(request);

    ocpp::v2::SetVariableResult res{ocpp::v2::SetVariableStatusEnum::RebootRequired, component, variable};
    std::vector<ocpp_multi::SetVariableOutcome> output;
    output.push_back({res, "NewValue"});
    EXPECT_CALL(chargepoint, set_variables(request_input, source)).WillOnce(Return(output));

    ocpp->handle_set_variables(request, source);
    ASSERT_EQ(received.size(), 1);
    EXPECT_EQ(received[0]["actual_value"], "NewValue");
    EXPECT_EQ(received[0]["component_variable"]["component"]["name"], "InternalCtrlr");
    EXPECT_EQ(received[0]["component_variable"]["variable"]["name"], "NetworkConnectionProfiles");
}

TEST_F(GenericOcppProvidesTester, setVariablesSynthesizedEventEchoesLegacyForm) {
    // Test if event is echoed for the legacy form if a monitor was registered with the legacy form
    // and was changed using the canonical form
    using module::conversions::to_ocpp_set_variable_data_vector;
    using ocpp::v2::Component;
    using ocpp::v2::Variable;
    using types::ocpp::ComponentVariable;
    using types::ocpp::SetVariableRequest;

    std::vector<json> received;
    interfaces->subscribe_var("ocpp_generic", "event_data",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    const Component canonical_component{"InternalCtrlr"};
    const Variable canonical_variable{"HeartbeatInterval"};

    EXPECT_CALL(chargepoint, resolve_to_canonical(Component{""}, Variable{"HeartBeatInterval"}))
        .WillOnce(Return(ocpp::v2::ComponentVariable{canonical_component, canonical_variable}));
    EXPECT_CALL(chargepoint, register_variable_listener(canonical_component, canonical_variable, _)).Times(1);
    std::vector<ComponentVariable> monitor_req{{{""}, {"HeartBeatInterval"}}};
    ocpp->handle_monitor_variables(monitor_req);

    const std::string source{"CSMS"};
    std::vector<SetVariableRequest> request;
    request.push_back({{{"InternalCtrlr"}, {"HeartbeatInterval"}}, {"77"}});
    const auto request_input = to_ocpp_set_variable_data_vector(request);

    ocpp::v2::SetVariableResult res{ocpp::v2::SetVariableStatusEnum::Accepted, canonical_component, canonical_variable};
    std::vector<ocpp_multi::SetVariableOutcome> output;
    output.push_back({res, "77"});
    EXPECT_CALL(chargepoint, set_variables(request_input, source)).WillOnce(Return(output));

    ocpp->handle_set_variables(request, source);
    ASSERT_EQ(received.size(), 1);
    EXPECT_EQ(received[0]["actual_value"], "77");
    EXPECT_EQ(received[0]["component_variable"]["component"]["name"], "");
    EXPECT_EQ(received[0]["component_variable"]["variable"]["name"], "HeartBeatInterval");
}

TEST_F(GenericOcppProvidesTester, setVariablesNoSynthesisForRejectedWrite) {
    using module::conversions::to_ocpp_set_variable_data_vector;
    using ocpp::v2::Component;
    using ocpp::v2::Variable;
    using types::ocpp::ComponentVariable;
    using types::ocpp::SetVariableRequest;

    std::vector<json> received;
    interfaces->subscribe_var("ocpp_generic", "event_data",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    const Component component{"CustomCtrlr"};
    const Variable variable{"FreeVariable"};

    std::vector<ComponentVariable> monitor_req{{{"CustomCtrlr"}, {"FreeVariable"}}};
    EXPECT_CALL(chargepoint, register_variable_listener(component, variable, _)).Times(1);
    ocpp->handle_monitor_variables(monitor_req);

    const std::string source{"CSMS"};
    std::vector<SetVariableRequest> request;
    request.push_back({{{"CustomCtrlr"}, {"FreeVariable"}}, {"NewValue"}});
    const auto request_input = to_ocpp_set_variable_data_vector(request);

    // a rejected write carries no monitor_value (the v16 router only sets it for committed writes)
    ocpp::v2::SetVariableResult res{ocpp::v2::SetVariableStatusEnum::Rejected, component, variable};
    std::vector<ocpp_multi::SetVariableOutcome> output;
    output.push_back({res, std::nullopt});
    EXPECT_CALL(chargepoint, set_variables(request_input, source)).WillOnce(Return(output));

    ocpp->handle_set_variables(request, source);
    EXPECT_TRUE(received.empty());
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

    const auto result = ocpp->handle_change_availability(request);
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

    const auto result = ocpp->handle_change_availability(request);
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

    EXPECT_CALL(chargepoint,
                register_variable_listener(ocpp::v2::Component{"Component1"}, ocpp::v2::Variable{"Variable1"}, _))
        .Times(1);
    EXPECT_CALL(chargepoint,
                register_variable_listener(ocpp::v2::Component{"Component2"}, ocpp::v2::Variable{"Variable2"}, _))
        .Times(1);

    EXPECT_TRUE(ocpp->get_monitor_list().empty());
    ocpp->handle_monitor_variables(req1);
    EXPECT_FALSE(ocpp->get_monitor_list().empty());
    EXPECT_EQ(ocpp->get_monitor_list().size(), 1);
    EXPECT_TRUE(contains(ocpp->get_monitor_list(), "Component1", "Variable1"));
    EXPECT_FALSE(contains(ocpp->get_monitor_list(), "Component2", "Variable2"));

    ocpp->handle_monitor_variables(req2);
    EXPECT_FALSE(ocpp->get_monitor_list().empty());
    EXPECT_EQ(ocpp->get_monitor_list().size(), 2);
    EXPECT_TRUE(contains(ocpp->get_monitor_list(), "Component1", "Variable1"));
    EXPECT_TRUE(contains(ocpp->get_monitor_list(), "Component2", "Variable2"));
}

TEST_F(GenericOcppProvidesTester, monitorVariablesLegacyFormStoredUnderCanonical) {
    // legacy key-only request resolves to the canonical CV; the listener is registered canonically
    using types::ocpp::ComponentVariable;

    const ocpp::v2::Component canonical_component{"InternalCtrlr"};
    const ocpp::v2::Variable canonical_variable{"HeartbeatInterval"};

    EXPECT_CALL(chargepoint, resolve_to_canonical(ocpp::v2::Component{""}, ocpp::v2::Variable{"HeartBeatInterval"}))
        .WillOnce(Return(ocpp::v2::ComponentVariable{canonical_component, canonical_variable}));
    EXPECT_CALL(chargepoint, register_variable_listener(canonical_component, canonical_variable, _)).Times(1);

    std::vector<ComponentVariable> req{{{""}, {"HeartBeatInterval"}}};
    ocpp->handle_monitor_variables(req);

    EXPECT_EQ(ocpp->get_monitor_list().size(), 1);
    EXPECT_TRUE(contains(ocpp->get_monitor_list(), "InternalCtrlr", "HeartbeatInterval"));
}

TEST_F(GenericOcppProvidesTester, monitorVariablesUnresolvableSkipped) {
    // unresolvable request is skipped (with a warning); no listener is registered
    using types::ocpp::ComponentVariable;

    EXPECT_CALL(chargepoint, resolve_to_canonical(ocpp::v2::Component{""}, ocpp::v2::Variable{"NoSuchKey"}))
        .WillOnce(Return(std::nullopt));
    EXPECT_CALL(chargepoint, register_variable_listener(_, _, _)).Times(0);

    std::vector<ComponentVariable> req{{{""}, {"NoSuchKey"}}};
    ocpp->handle_monitor_variables(req);

    EXPECT_TRUE(ocpp->get_monitor_list().empty());
}

TEST_F(GenericOcppProvidesTester, monitorVariablesFreeCvAccepted) {
    // a CV with no 1.6 key mapping resolves to itself (identity) and is accepted
    using types::ocpp::ComponentVariable;

    const ocpp::v2::Component component{"CustomCtrlr"};
    const ocpp::v2::Variable variable{"FreeVariable"};

    EXPECT_CALL(chargepoint, resolve_to_canonical(component, variable))
        .WillOnce(Return(ocpp::v2::ComponentVariable{component, variable}));
    EXPECT_CALL(chargepoint, register_variable_listener(component, variable, _)).Times(1);

    std::vector<ComponentVariable> req{{{"CustomCtrlr"}, {"FreeVariable"}}};
    ocpp->handle_monitor_variables(req);

    EXPECT_EQ(ocpp->get_monitor_list().size(), 1);
    EXPECT_TRUE(contains(ocpp->get_monitor_list(), "CustomCtrlr", "FreeVariable"));
}

TEST_F(GenericOcppProvidesTester, monitorVariablesDuplicateFormStoredOnce) {
    // the same requested form registered twice is stored once
    using types::ocpp::ComponentVariable;

    const ocpp::v2::Component component{"Component1"};
    const ocpp::v2::Variable variable{"Variable1"};

    EXPECT_CALL(chargepoint, resolve_to_canonical(component, variable)).Times(2);
    EXPECT_CALL(chargepoint, register_variable_listener(component, variable, _)).Times(2);

    std::vector<ComponentVariable> req{{{"Component1"}, {"Variable1"}}};
    ocpp->handle_monitor_variables(req);
    ocpp->handle_monitor_variables(req);

    EXPECT_EQ(ocpp->get_monitor_list().size(), 1);
}

// ----------------------------------------------------------------------------
// Vars

TEST_F(GenericOcppProvidesTester, publishOcppTransactionEvent) {
    // publish_ocpp_transaction_event() called from cb_transaction_event

    using ocpp::DateTime;
    using ocpp::v2::Transaction;
    using ocpp::v2::TransactionEventEnum;
    using ocpp::v2::TransactionEventRequest;
    using ocpp::v2::TriggerReasonEnum;

    TransactionEventRequest request;
    request.eventType = TransactionEventEnum::Updated;
    request.timestamp = DateTime{};
    request.triggerReason = TriggerReasonEnum::ChargingRateChanged;
    request.seqNo = 99587;
    request.transactionInfo = Transaction{"TransactionId"};
    // std::optional<CostDetails> costDetails;
    // std::optional<std::vector<MeterValue>> meterValue;
    // std::optional<bool> offline;
    // std::optional<std::int32_t> numberOfPhasesUsed;
    // std::optional<std::int32_t> cableMaxCurrent;
    // std::optional<std::int32_t> reservationId;
    // std::optional<PreconditioningStatusEnum> preconditioningStatus;
    // std::optional<bool> evseSleep;
    // std::optional<EVSE> evse;
    // std::optional<IdToken> idToken;
    // std::optional<CustomData> customData;

    std::vector<json> received;
    interfaces->subscribe_var("ocpp_generic", "ocpp_transaction_event",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    ocpp->cb_transaction_event(request, "TransactionId");
    ASSERT_EQ(received.size(), 1);
    EXPECT_EQ(received[0],
              R"({"session_id":"TransactionId","transaction_event":"Updated","transaction_id":"TransactionId"})"_json);

    // OCPP1.6: numeric transaction id differs from the session id
    ocpp->cb_transaction_event(request, "42");
    ASSERT_EQ(received.size(), 2);
    EXPECT_EQ(received[1],
              R"({"session_id":"TransactionId","transaction_event":"Updated","transaction_id":"42"})"_json);

    // OCPP1.6: transaction id not assigned yet (Started)
    ocpp->cb_transaction_event(request, std::nullopt);
    ASSERT_EQ(received.size(), 3);
    EXPECT_EQ(received[2], R"({"session_id":"TransactionId","transaction_event":"Updated"})"_json);
}

TEST_F(GenericOcppProvidesTester, publishOcppTransactionEventRespose) {
    // publish_ocpp_transaction_event_response() called from cb_transaction_event_response

    using ocpp::DateTime;
    using ocpp::v2::AuthorizationStatusEnum;
    using ocpp::v2::EVSE;
    using ocpp::v2::IdTokenInfo;
    using ocpp::v2::Transaction;
    using ocpp::v2::TransactionEventEnum;
    using ocpp::v2::TransactionEventRequest;
    using ocpp::v2::TransactionEventResponse;
    using ocpp::v2::TriggerReasonEnum;

    TransactionEventRequest transaction_event;
    transaction_event.eventType = TransactionEventEnum::Started;
    transaction_event.timestamp = DateTime();
    transaction_event.triggerReason = TriggerReasonEnum::CablePluggedIn;
    transaction_event.seqNo = 10;
    transaction_event.transactionInfo = Transaction{"transactionId"};
    // std::optional<CostDetails> costDetails;
    // std::optional<std::vector<MeterValue>> meterValue;
    // std::optional<bool> offline;
    // std::optional<std::int32_t> numberOfPhasesUsed;
    // std::optional<std::int32_t> cableMaxCurrent;
    // std::optional<std::int32_t> reservationId;
    // std::optional<PreconditioningStatusEnum> preconditioningStatus;
    // std::optional<bool> evseSleep;
    // std::optional<EVSE> evse;
    // std::optional<IdToken> idToken;
    // std::optional<CustomData> customData;

    TransactionEventResponse transaction_event_response{};
    // std::optional<float> totalCost;
    // std::optional<std::int32_t> chargingPriority;
    // std::optional<IdTokenInfo> idTokenInfo;
    // std::optional<TransactionLimit> transactionLimit;
    // std::optional<MessageContent> updatedPersonalMessage;
    // std::optional<std::vector<MessageContent>> updatedPersonalMessageExtra;
    // std::optional<CustomData> customData;

    std::vector<json> received;
    interfaces->subscribe_var("ocpp_generic", "ocpp_transaction_event_response",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    ocpp->cb_transaction_event_response(transaction_event, transaction_event_response, "transactionId");

    transaction_event.eventType = TransactionEventEnum::Updated;
    transaction_event.triggerReason = TriggerReasonEnum::ChargingStateChanged;
    transaction_event.evse = EVSE{1, 0};
    transaction_event_response.idTokenInfo = IdTokenInfo{AuthorizationStatusEnum::Accepted};
    // OCPP1.6: numeric transaction id differs from the session id
    ocpp->cb_transaction_event_response(transaction_event, transaction_event_response, "42");

    EXPECT_EQ(received.size(), 2);
    EXPECT_EQ(
        received[0],
        R"({"original_transaction_event":{"session_id":"transactionId","transaction_event":"Started","transaction_id":"transactionId"}})"_json);
    EXPECT_EQ(
        received[1],
        R"({"original_transaction_event":{"evse":{"connector_id":0,"id":1},"session_id":"transactionId","transaction_event":"Updated","transaction_id":"42"}})"_json);
}

TEST_F(GenericOcppProvidesTester, publishChargingSchedules) {
    // publish_charging_schedules() called from publish_charging_schedules
    // and cb_charging_schedules_timer

    using ocpp::DateTime;
    using ocpp::v2::ChargingRateUnitEnum;
    using ocpp::v2::EnhancedChargingSchedulePeriod;
    using ocpp::v2::EnhancedCompositeSchedule;

    std::vector<json> received;
    interfaces->subscribe_var("ocpp_generic", "charging_schedules",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    std::vector<EnhancedCompositeSchedule> composite_schedules;
    EnhancedCompositeSchedule schedule;
    schedule.evseId = 1;
    schedule.duration = 1500;
    schedule.scheduleStart = DateTime{"2026-06-05T13:37:36.409Z"};
    schedule.chargingRateUnit = ChargingRateUnitEnum::A;

    EnhancedChargingSchedulePeriod period;
    period.startPeriod = 0;
    period.limit = 16.;
    // std::optional<float> limit_L2;
    // std::optional<float> limit_L3;
    // std::optional<std::int32_t> numberPhases;
    // std::optional<std::int32_t> phaseToUse;
    // std::optional<float> dischargeLimit;
    // std::optional<float> dischargeLimit_L2;
    // std::optional<float> dischargeLimit_L3;
    // std::optional<float> setpoint;
    // std::optional<float> setpoint_L2;
    // std::optional<float> setpoint_L3;
    // std::optional<float> setpointReactive;
    // std::optional<float> setpointReactive_L2;
    // std::optional<float> setpointReactive_L3;
    // std::optional<bool> preconditioningRequest;
    // std::optional<bool> evseSleep;
    // std::optional<float> v2xBaseline;
    // std::optional<OperationModeEnum> operationMode;
    // std::optional<std::vector<V2XFreqWattPoint>> v2xFreqWattCurve;
    // std::optional<std::vector<V2XSignalWattPoint>> v2xSignalWattCurve;
    // std::optional<CustomData> customData;
    period.stackLevel = 8;

    schedule.chargingSchedulePeriod.push_back(period);
    period.startPeriod = 120;
    period.limit = 24.;
    schedule.chargingSchedulePeriod.push_back(period);

    composite_schedules.push_back(schedule);

    ocpp->publish_charging_schedules(composite_schedules);
    ASSERT_EQ(received.size(), 1);
    EXPECT_EQ(received[0],
              R"({"schedules":[
        {"charging_rate_unit":"A","charging_schedule_period":[
        {"limit":16.0,"stack_level":8,"start_period":0},{"limit":24.0,"stack_level":8,"start_period":120}],
        "duration":1500,"evse":1,"start_schedule":"2026-06-05T13:37:36.409Z"}]})"_json);

    ASSERT_EQ(config.RequestCompositeScheduleUnit, "A");
    composite_schedules[0].duration = config.RequestCompositeScheduleDurationS;
    composite_schedules[0].chargingSchedulePeriod[0].stackLevel = 16;
    composite_schedules[0].chargingSchedulePeriod[1].stackLevel = 20;
    EXPECT_CALL(chargepoint,
                get_all_composite_schedules(config.RequestCompositeScheduleDurationS, ChargingRateUnitEnum::A))
        .WillOnce(Return(composite_schedules));

    received.clear();
    ocpp->cb_set_charging_profiles();
    ASSERT_EQ(received.size(), 1);
    EXPECT_EQ(received[0],
              R"({"schedules":[
        {"charging_rate_unit":"A","charging_schedule_period":[
        {"limit":16.0,"stack_level":16,"start_period":0},{"limit":24.0,"stack_level":20,"start_period":120}],
        "duration":600,"evse":1,"start_schedule":"2026-06-05T13:37:36.409Z"}]})"_json);
}

TEST_F(GenericOcppProvidesTester, publishIsConnected) {
    // publish_is_connected() called from cb_connection_state_changed

    using ocpp::OcppProtocolVersion;

    std::vector<json> received;
    interfaces->subscribe_var("ocpp_generic", "is_connected",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    ocpp->cb_connection_state_changed(true, OcppProtocolVersion::v201);
    ocpp->cb_connection_state_changed(false, OcppProtocolVersion::v16);

    ASSERT_EQ(received.size(), 2);
    EXPECT_EQ(received[0], R"(true)"_json);
    EXPECT_EQ(received[1], R"(false)"_json);
}

TEST_F(GenericOcppProvidesTester, publishSecurityEvent) {
    // publish_security_event() called from cb_security_event

    std::vector<json> received;
    interfaces->subscribe_var("ocpp_generic", "security_event",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    ocpp->cb_security_event("Bad Actor", std::nullopt);
    ocpp->cb_security_event("Strange Actor", "Fuzzy");

    ASSERT_EQ(received.size(), 2);
    EXPECT_EQ(received[0], R"({"type":"Bad Actor"})"_json);
    EXPECT_EQ(received[1], R"({"info":"Fuzzy","type":"Strange Actor"})"_json);
}

TEST_F(GenericOcppProvidesTester, publishEventData) {
    // publish_event_data() called from cb_variable_monitor

    using ocpp::v2::Component;
    using ocpp::v2::Variable;
    using types::ocpp::ComponentVariable;

    std::vector<json> received;
    interfaces->subscribe_var("ocpp_generic", "event_data",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    ocpp->cb_variable_monitor(Component{"component"}, Variable{"variable"}, "value");
    EXPECT_TRUE(received.empty());

    // add monitor
    EXPECT_CALL(chargepoint, register_variable_listener(Component{"Component"}, Variable{"Variable"}, _)).Times(1);
    std::vector<ComponentVariable> req{{{"Component"}, {"Variable"}}};
    ocpp->handle_monitor_variables(req);
    ocpp->cb_variable_monitor(Component{"Component"}, Variable{"Variable"}, "value");

    ASSERT_EQ(received.size(), 1);
    // adjust the date and time
    json expected =
        R"({"actual_value":"value","component_variable":{"component":{"name":"Component"},"variable":{"name":"Variable"}},"event_id":0,"event_notification_type":"CustomMonitor","timestamp":"2026-06-05T14:38:58.511Z","trigger":"Alerting"})"_json;
    expected["timestamp"] = received[0]["timestamp"];
    EXPECT_EQ(received[0], expected);
}

TEST_F(GenericOcppProvidesTester, publishEventDataLegacyForm) {
    // a legacy (key-only) registration is echoed in its legacy shape

    using ocpp::v2::Component;
    using ocpp::v2::Variable;
    using types::ocpp::ComponentVariable;

    std::vector<json> received;
    interfaces->subscribe_var("ocpp_generic", "event_data",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    const Component canonical_component{"InternalCtrlr"};
    const Variable canonical_variable{"HeartbeatInterval"};

    EXPECT_CALL(chargepoint, resolve_to_canonical(Component{""}, Variable{"HeartBeatInterval"}))
        .WillOnce(Return(ocpp::v2::ComponentVariable{canonical_component, canonical_variable}));
    EXPECT_CALL(chargepoint, register_variable_listener(canonical_component, canonical_variable, _)).Times(1);

    std::vector<ComponentVariable> req{{{""}, {"HeartBeatInterval"}}};
    ocpp->handle_monitor_variables(req);

    ocpp->cb_variable_monitor(canonical_component, canonical_variable, "42");

    ASSERT_EQ(received.size(), 1);
    json expected =
        R"({"actual_value":"42","component_variable":{"component":{"name":""},"variable":{"name":"HeartBeatInterval"}},"event_id":0,"event_notification_type":"CustomMonitor","timestamp":"x","trigger":"Alerting"})"_json;
    expected["timestamp"] = received[0]["timestamp"];
    EXPECT_EQ(received[0], expected);
}

TEST_F(GenericOcppProvidesTester, publishEventDataBothFormsFire) {
    // canonical and legacy registrations colliding on one canonical CV each get their own echo

    using ocpp::v2::Component;
    using ocpp::v2::Variable;
    using types::ocpp::ComponentVariable;

    std::vector<json> received;
    interfaces->subscribe_var("ocpp_generic", "event_data",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    const Component canonical_component{"InternalCtrlr"};
    const Variable canonical_variable{"HeartbeatInterval"};

    EXPECT_CALL(chargepoint, resolve_to_canonical(canonical_component, canonical_variable))
        .WillOnce(Return(ocpp::v2::ComponentVariable{canonical_component, canonical_variable}));
    EXPECT_CALL(chargepoint, resolve_to_canonical(Component{""}, Variable{"HeartBeatInterval"}))
        .WillOnce(Return(ocpp::v2::ComponentVariable{canonical_component, canonical_variable}));
    EXPECT_CALL(chargepoint, register_variable_listener(canonical_component, canonical_variable, _)).Times(2);

    std::vector<ComponentVariable> canonical_req{{{"InternalCtrlr"}, {"HeartbeatInterval"}}};
    std::vector<ComponentVariable> legacy_req{{{""}, {"HeartBeatInterval"}}};
    ocpp->handle_monitor_variables(canonical_req);
    ocpp->handle_monitor_variables(legacy_req);
    EXPECT_EQ(ocpp->get_monitor_list().size(), 2);

    ocpp->cb_variable_monitor(canonical_component, canonical_variable, "42");

    ASSERT_EQ(received.size(), 2);
    json expected_canonical =
        R"({"actual_value":"42","component_variable":{"component":{"name":"InternalCtrlr"},"variable":{"name":"HeartbeatInterval"}},"event_id":0,"event_notification_type":"CustomMonitor","timestamp":"x","trigger":"Alerting"})"_json;
    json expected_legacy =
        R"({"actual_value":"42","component_variable":{"component":{"name":""},"variable":{"name":"HeartBeatInterval"}},"event_id":0,"event_notification_type":"CustomMonitor","timestamp":"x","trigger":"Alerting"})"_json;
    expected_canonical["timestamp"] = received[0]["timestamp"];
    expected_legacy["timestamp"] = received[1]["timestamp"];
    EXPECT_EQ(received[0], expected_canonical);
    EXPECT_EQ(received[1], expected_legacy);
}

TEST_F(GenericOcppProvidesTester, publishBootNotificationResponse) {
    // publish_boot_notification_response() called from cb_boot_notification

    using ocpp::DateTime;
    using ocpp::v2::BootNotificationResponse;
    using ocpp::v2::RegistrationStatusEnum;

    std::vector<json> received;
    interfaces->subscribe_var("ocpp_generic", "boot_notification_response",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    BootNotificationResponse response;
    response.currentTime = DateTime{"2026-06-05T14:51:48.876Z"};
    response.interval = 520;
    response.status = RegistrationStatusEnum::Pending;
    // std::optional<StatusInfo> statusInfo;
    // std::optional<CustomData> customData;

    ocpp->cb_boot_notification(response);

    ASSERT_EQ(received.size(), 1);
    EXPECT_EQ(received[0], R"({"current_time":"2026-06-05T14:51:48.876Z","interval":520,"status":"Pending"})"_json);
}

TEST_F(GenericOcppProvidesTester, publishOcppMessage) {
    // publish_ocpp_message() called from cb_ocpp_messages

    using ocpp::MessageDirection;
    using ocpp::OcppProtocolVersion;

    std::vector<json> received;
    interfaces->subscribe_var("ocpp_generic", "ocpp_message",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    ocpp->cb_connection_state_changed(true, OcppProtocolVersion::v16);
    ocpp->cb_ocpp_messages(R"({"message": 1})", MessageDirection::ChargingStationToCSMS);
    ocpp->cb_ocpp_messages(R"({"message": 2})", MessageDirection::CSMSToChargingStation);

    ASSERT_EQ(received.size(), 2);
    EXPECT_EQ(received[0],
              R"({"direction":"ChargingStationToCSMS","message":"{\"message\": 1}","version":"1.6"})"_json);
    EXPECT_EQ(received[1],
              R"({"direction":"CSMSToChargingStation","message":"{\"message\": 2}","version":"1.6"})"_json);
}

} // namespace
