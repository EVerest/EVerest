// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <device_model_test_helper.hpp>

#include "component_state_manager_mock.hpp"
#include "connectivity_manager_mock.hpp"
#include "evse_manager_fake.hpp"
#include "evse_security_mock.hpp"
#include "message_dispatcher_mock.hpp"
#include "mocks/database_handler_mock.hpp"
#include <ocpp/common/constants.hpp>
#include <ocpp/v2/functional_blocks/authorization.hpp>
#include <ocpp/v2/functional_blocks/diagnostics.hpp>
#include <ocpp/v2/functional_blocks/functional_block_context.hpp>
#include <ocpp/v2/messages/Authorize.hpp>
#include <ocpp/v2/messages/GetMonitoringReport.hpp>
#include <ocpp/v2/messages/NotifyEvent.hpp>
#include <ocpp/v2/messages/SetVariableMonitoring.hpp>

using namespace ocpp::v2;
using ::testing::_;
using ::testing::Invoke;

namespace {

class AuthorizationMock : public AuthorizationInterface {
public:
    MOCK_METHOD(void, handle_message, (const ocpp::EnhancedMessage<MessageType>& message), (override));
    MOCK_METHOD(void, start_auth_cache_cleanup_thread, (), (override));
    MOCK_METHOD(AuthorizeResponse, authorize_req,
                (const IdToken id_token, const std::optional<ocpp::CiString<10000>>& certificate,
                 const std::optional<std::vector<OCSPRequestData>>& ocsp_request_data),
                (override));
    MOCK_METHOD(void, trigger_authorization_cache_cleanup, (), (override));
    MOCK_METHOD(void, update_authorization_cache_size, (), (override));
    MOCK_METHOD(bool, is_auth_cache_ctrlr_enabled, (), (override));
    MOCK_METHOD(void, authorization_cache_insert_entry,
                (const std::string& id_token_hash, const IdTokenInfo& id_token_info), (override));
    MOCK_METHOD(std::optional<AuthorizationCacheEntry>, authorization_cache_get_entry,
                (const std::string& id_token_hash), (override));
    MOCK_METHOD(void, authorization_cache_delete_entry, (const std::string& id_token_hash), (override));
    MOCK_METHOD(AuthorizeResponse, validate_token,
                (const IdToken id_token, const std::optional<ocpp::CiString<10000>>& certificate,
                 const std::optional<std::vector<OCSPRequestData>>& ocsp_request_data),
                (override));
};

EventData event_with_severity() {
    EventData event;
    event.eventId = 1;
    event.timestamp = ocpp::DateTime();
    event.trigger = EventTriggerEnum::Alerting;
    event.actualValue = "true";
    event.eventNotificationType = EventNotificationEnum::HardWiredNotification;
    event.component = {"EVSE"};
    event.variable = {"Problem"};
    event.severity = 3;
    return event;
}

class DiagnosticsTest : public ::testing::TestWithParam<ocpp::OcppProtocolVersion> {
protected:
    DeviceModelTestHelper dm_helper;
    ::testing::NiceMock<MockMessageDispatcher> mock_dispatcher;
    ::testing::NiceMock<ocpp::ConnectivityManagerMock> connectivity_manager;
    ::testing::NiceMock<DatabaseHandlerMock> database_handler;
    ocpp::EvseSecurityMock evse_security;
    EvseManagerFake evse_manager{1};
    ::testing::NiceMock<ComponentStateManagerMock> component_state_manager;
    std::atomic<ocpp::OcppProtocolVersion> ocpp_version{GetParam()};
    FunctionalBlockContext context{
        mock_dispatcher, *dm_helper.get_device_model(), connectivity_manager, evse_manager, database_handler,
        evse_security,   component_state_manager,       ocpp_version};
    ::testing::NiceMock<AuthorizationMock> authorization;
    Diagnostics diagnostics{context, authorization, nullptr, std::nullopt, std::nullopt};

    /// \returns the eventData of the NotifyEvent.req the diagnostics block dispatches for \p events
    json dispatched_event_data(const std::vector<EventData>& events) {
        json event_data;
        EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).WillOnce(Invoke([&event_data](const json& call, bool) {
            event_data = call[ocpp::CALL_PAYLOAD].at("eventData");
        }));
        diagnostics.notify_event_req(events);
        return event_data;
    }
};

using DiagnosticsSeverityOmittedTest = DiagnosticsTest;
using DiagnosticsSeverityReportedTest = DiagnosticsTest;

TEST_P(DiagnosticsSeverityOmittedTest, NotifyEventOmitsSeverity) {
    const auto event_data = dispatched_event_data({event_with_severity()});
    ASSERT_EQ(event_data.size(), 1U);
    EXPECT_FALSE(event_data.at(0).contains("severity"));
}

INSTANTIATE_TEST_SUITE_P(BeforeOcpp21, DiagnosticsSeverityOmittedTest,
                         ::testing::Values(ocpp::OcppProtocolVersion::v201, ocpp::OcppProtocolVersion::Unknown));

TEST_P(DiagnosticsSeverityReportedTest, NotifyEventReportsSeverity) {
    const auto event_data = dispatched_event_data({event_with_severity()});
    ASSERT_EQ(event_data.size(), 1U);
    EXPECT_EQ(event_data.at(0).at("severity"), 3);
}

INSTANTIATE_TEST_SUITE_P(Ocpp21, DiagnosticsSeverityReportedTest, ::testing::Values(ocpp::OcppProtocolVersion::v21));

using DiagnosticsItemsPerMessageTest = DiagnosticsTest;

void set_limit(DeviceModel& device_model, const RequiredComponentVariable& limit, const std::string& value) {
    ASSERT_EQ(
        device_model.set_value(limit.component, limit.variable.value(), AttributeEnum::Actual, value, "test", true),
        SetVariableStatusEnum::Accepted);
}

template <typename Request>
ocpp::EnhancedMessage<MessageType> make_message(const Request& request, const MessageType type) {
    ocpp::Call<Request> call(request);
    ocpp::EnhancedMessage<MessageType> enhanced_message;
    enhanced_message.messageType = type;
    enhanced_message.message = call;
    enhanced_message.call_message = enhanced_message.message;
    enhanced_message.message_size = enhanced_message.message.dump().size();
    return enhanced_message;
}

// N04.FR.09, with the error code of OCPP-J
TEST_P(DiagnosticsItemsPerMessageTest, SetVariableMonitoringOverItemsPerMessageIsAnsweredWithCallError) {
    set_limit(*dm_helper.get_device_model(), ControllerComponentVariables::ItemsPerMessageSetVariableMonitoring, "1");
    SetMonitoringData data;
    data.value = 1;
    data.type = MonitorEnum::Delta;
    data.severity = 5;
    data.component = ControllerComponentVariables::OrganizationName.component;
    data.variable = ControllerComponentVariables::OrganizationName.variable.value();
    SetVariableMonitoringRequest request;
    request.setMonitoringData = {data, data};
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).Times(0);
    EXPECT_CALL(mock_dispatcher, dispatch_call_error(_)).WillOnce([](const json& call_error) {
        EXPECT_EQ(call_error.at(2), "OccurrenceConstraintViolation");
    });

    diagnostics.handle_message(make_message(request, MessageType::SetVariableMonitoring));
}

// N02.FR.07, answered like B08.FR.17 with the error code of OCPP-J
TEST_P(DiagnosticsItemsPerMessageTest, GetMonitoringReportOverItemsPerMessageIsAnsweredWithCallError) {
    set_limit(*dm_helper.get_device_model(), ControllerComponentVariables::ItemsPerMessageGetReport, "1");
    ComponentVariable component_variable;
    component_variable.component = ControllerComponentVariables::OrganizationName.component;
    component_variable.variable = ControllerComponentVariables::OrganizationName.variable;
    GetMonitoringReportRequest request;
    request.requestId = 1;
    request.componentVariable = std::vector<ComponentVariable>{component_variable, component_variable};
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).Times(0);
    EXPECT_CALL(mock_dispatcher, dispatch_call_error(_)).WillOnce([](const json& call_error) {
        EXPECT_EQ(call_error.at(2), "OccurrenceConstraintViolation");
    });

    diagnostics.handle_message(make_message(request, MessageType::GetMonitoringReport));
}

INSTANTIATE_TEST_SUITE_P(Ocpp201, DiagnosticsItemsPerMessageTest, ::testing::Values(ocpp::OcppProtocolVersion::v201));

} // namespace
