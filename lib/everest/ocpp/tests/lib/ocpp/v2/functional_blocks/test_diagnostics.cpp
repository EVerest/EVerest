// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <atomic>
#include <optional>
#include <vector>

#include <component_state_manager_mock.hpp>
#include <connectivity_manager_mock.hpp>
#include <device_model_test_helper.hpp>
#include <evse_manager_fake.hpp>
#include <evse_security_mock.hpp>
#include <message_dispatcher_mock.hpp>
#include <mocks/database_handler_mock.hpp>

#include <ocpp/common/constants.hpp>
#include <ocpp/v2/connection_directory.hpp>
#include <ocpp/v2/connection_scope.hpp>
#include <ocpp/v2/ctrlr_component_variables.hpp>
#include <ocpp/v2/device_model.hpp>
#include <ocpp/v2/functional_blocks/authorization.hpp>
#include <ocpp/v2/functional_blocks/diagnostics.hpp>
#include <ocpp/v2/functional_blocks/functional_block_context.hpp>
#include <ocpp/v2/messages/Authorize.hpp>
#include <ocpp/v2/messages/ClearVariableMonitoring.hpp>
#include <ocpp/v2/messages/GetLog.hpp>
#include <ocpp/v2/messages/NotifyEvent.hpp>
#include <ocpp/v2/messages/SetVariableMonitoring.hpp>
#include <ocpp/v2/monitor_subscription.hpp>

using namespace ocpp;
using namespace ocpp::v2;
using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;

namespace {

class AuthorizationMock : public AuthorizationInterface {
public:
    MOCK_METHOD(void, handle_message, (const ocpp::EnhancedMessage<MessageType>&), (override));
    MOCK_METHOD(void, start_auth_cache_cleanup_thread, (), (override));
    MOCK_METHOD(AuthorizeResponse, authorize_req,
                (const IdToken, const std::optional<CiString<10000>>&,
                 const std::optional<std::vector<ocpp::v2::OCSPRequestData>>&),
                (override));
    MOCK_METHOD(void, trigger_authorization_cache_cleanup, (), (override));
    MOCK_METHOD(void, update_authorization_cache_size, (), (override));
    MOCK_METHOD(bool, is_auth_cache_ctrlr_enabled, (), (override));
    MOCK_METHOD(void, authorization_cache_insert_entry, (const std::string&, const IdTokenInfo&), (override));
    MOCK_METHOD(std::optional<AuthorizationCacheEntry>, authorization_cache_get_entry, (const std::string&),
                (override));
    MOCK_METHOD(void, authorization_cache_delete_entry, (const std::string&), (override));
    MOCK_METHOD(AuthorizeResponse, validate_token,
                (const IdToken, const std::optional<CiString<10000>>&,
                 const std::optional<std::vector<ocpp::v2::OCSPRequestData>>&),
                (override));
};

/// A connection view of a station with a primary connection and two secondaries.
class TwoSecondariesDirectory : public ConnectionDirectory {
public:
    [[nodiscard]] std::vector<ConnectionId> connection_ids() const override {
        return {ConnectionId::primary(), ConnectionId::secondary("EMS"), ConnectionId::secondary("MAINT")};
    }
    [[nodiscard]] bool may_set_variable(const ConnectionId&, const Component&, const Variable&) const override {
        return true;
    }
};

/// Application note section 5.5: a NotifyEvent only means something to the system that installed
/// the monitor it reports, so it goes to the connections subscribed to that monitor.
class NotifyEventRoutingTest : public ::testing::Test {
protected:
    DeviceModelTestHelper dm_helper;
    DeviceModel* dm{nullptr};

    NiceMock<MockMessageDispatcher> mock_dispatcher;
    NiceMock<ocpp::ConnectivityManagerMock> connectivity_manager;
    std::unique_ptr<EvseManagerFake> evse_manager;
    NiceMock<DatabaseHandlerMock> db_handler;
    ocpp::EvseSecurityMock evse_security;
    NiceMock<ComponentStateManagerMock> component_state_manager;
    std::atomic<OcppProtocolVersion> ocpp_version{OcppProtocolVersion::v201};

    NiceMock<AuthorizationMock> authorization;
    TwoSecondariesDirectory connections;
    RequestOriginRegistry request_origins;

    std::unique_ptr<FunctionalBlockContext> fb_context;
    std::unique_ptr<Diagnostics> diagnostics;

    /// The connection each dispatched NotifyEvent was pinned to, with its monitor ids.
    std::vector<std::pair<std::optional<ConnectionId>, std::vector<std::int32_t>>> notify_events;
    std::vector<json> dispatched_call_results;

    NotifyEventRoutingTest() :
        dm_helper(DEVICE_MODEL_DB_IN_MEMORY_PATH, MIGRATION_FILES_PATH, CONFIG_PATH, false, {"EMS", "MAINT"}) {
        dm = dm_helper.get_device_model();
        evse_manager = std::make_unique<EvseManagerFake>(1);

        fb_context = std::make_unique<FunctionalBlockContext>(mock_dispatcher, *dm, connectivity_manager, *evse_manager,
                                                              db_handler, evse_security, component_state_manager,
                                                              ocpp_version, connections, request_origins);

        ON_CALL(mock_dispatcher, dispatch_call(_, _)).WillByDefault(Invoke([this](const json& call, bool) {
            if (call.at(CALL_ACTION) != "NotifyEvent") {
                return;
            }
            std::vector<std::int32_t> monitor_ids;
            for (const auto& event : call.at(CALL_PAYLOAD).at("eventData")) {
                monitor_ids.push_back(event.value("variableMonitoringId", -1));
            }
            this->notify_events.emplace_back(ConnectionTargetScope::current(), monitor_ids);
        }));
        ON_CALL(mock_dispatcher, dispatch_call_result(_)).WillByDefault(Invoke([this](const json& call_result) {
            this->dispatched_call_results.push_back(call_result);
        }));

        diagnostics = std::make_unique<Diagnostics>(
            *fb_context, authorization, [](const GetLogRequest&) { return GetLogResponse(); }, std::nullopt,
            std::nullopt);
    }

    static EventData event(const EventNotificationEnum notification, const std::optional<std::int32_t> monitor_id) {
        EventData data;
        data.eventId = monitor_id.value_or(0);
        data.timestamp = DateTime();
        data.trigger = EventTriggerEnum::Alerting;
        data.actualValue = "1";
        data.component = Component{"EVSE"};
        data.variable = Variable{"AvailabilityState"};
        data.eventNotificationType = notification;
        data.variableMonitoringId = monitor_id;
        return data;
    }

    void subscribe(const ConnectionId& connection, const std::set<std::int32_t>& ids) {
        const auto component = ConnectionComponentVariables::monitoring_ctrlr(connection);
        ASSERT_EQ(dm->set_value(component, ConnectionComponentVariables::SubscribedMonitors, AttributeEnum::Actual,
                                to_monitor_id_list(ids), "test", true),
                  SetVariableStatusEnum::Accepted);
    }

    std::set<std::int32_t> subscription_of(const ConnectionId& connection) {
        const auto value =
            dm->get_optional_value<std::string>({ConnectionComponentVariables::monitoring_ctrlr(connection),
                                                 ConnectionComponentVariables::SubscribedMonitors});
        return parse_monitor_id_list(value.value_or(""));
    }

    /// Delivers a SetVariableMonitoring as if it had arrived on \p connection, returning the ids
    /// of the monitors it created.
    std::vector<std::int32_t> set_monitor_from(const ConnectionId& connection, const Variable& variable) {
        SetVariableMonitoringRequest request;
        SetMonitoringData data;
        data.component = ControllerComponents::OCPPCommCtrlr;
        data.variable = variable;
        data.type = MonitorEnum::UpperThreshold;
        data.value = 100.0F;
        data.severity = 5;
        request.setMonitoringData = {data};

        ocpp::EnhancedMessage<MessageType> message;
        message.messageType = MessageType::SetVariableMonitoring;
        message.message = ocpp::Call<SetVariableMonitoringRequest>(request);
        message.call_message = message.message;
        message.message_size = message.message.dump().size();

        dispatched_call_results.clear();
        {
            const ConnectionScope scope(connection);
            diagnostics->handle_message(message);
        }

        std::vector<std::int32_t> ids;
        for (const auto& result : dispatched_call_results.at(0).at(CALLRESULT_PAYLOAD).at("setMonitoringResult")) {
            if (result.at("status") == "Accepted") {
                ids.push_back(result.at("id"));
            }
        }
        return ids;
    }
};

TEST_F(NotifyEventRoutingTest, AHardwiredNotificationGoesToEveryConnection) {
    diagnostics->notify_event_req({event(EventNotificationEnum::HardWiredNotification, std::nullopt)});

    ASSERT_EQ(notify_events.size(), 3);
    EXPECT_EQ(notify_events.at(0).first, ConnectionId::primary());
    EXPECT_EQ(notify_events.at(1).first, ConnectionId::secondary("EMS"));
    EXPECT_EQ(notify_events.at(2).first, ConnectionId::secondary("MAINT"));
}

TEST_F(NotifyEventRoutingTest, AMonitorNobodyIsSubscribedToIsTheCsmsBusiness) {
    // Which is how a preconfigured monitor reaches the CSMS without anything having to write it
    // into a list at startup.
    diagnostics->notify_event_req({event(EventNotificationEnum::PreconfiguredMonitor, 2)});

    ASSERT_EQ(notify_events.size(), 1);
    EXPECT_EQ(notify_events.at(0).first, ConnectionId::primary());
}

TEST_F(NotifyEventRoutingTest, AMonitorGoesOnlyToTheConnectionsSubscribedToIt) {
    subscribe(ConnectionId::secondary("MAINT"), {10});

    diagnostics->notify_event_req({event(EventNotificationEnum::CustomMonitor, 10)});

    ASSERT_EQ(notify_events.size(), 1);
    EXPECT_EQ(notify_events.at(0).first, ConnectionId::secondary("MAINT"));
}

TEST_F(NotifyEventRoutingTest, TwoConnectionsCanBeSubscribedToTheSameMonitor) {
    // The section 5.5 example: the CSMS adds MAINT's monitor 10 to its own list.
    subscribe(ConnectionId::primary(), {2, 10, 11});
    subscribe(ConnectionId::secondary("MAINT"), {10, 2});

    diagnostics->notify_event_req({event(EventNotificationEnum::CustomMonitor, 10)});

    ASSERT_EQ(notify_events.size(), 2);
    EXPECT_EQ(notify_events.at(0).first, ConnectionId::primary());
    EXPECT_EQ(notify_events.at(1).first, ConnectionId::secondary("MAINT"));
}

TEST_F(NotifyEventRoutingTest, OneBatchOfEventsIsSplitPerConnection) {
    subscribe(ConnectionId::primary(), {11});
    subscribe(ConnectionId::secondary("EMS"), {20});

    diagnostics->notify_event_req({event(EventNotificationEnum::CustomMonitor, 11),
                                   event(EventNotificationEnum::CustomMonitor, 20),
                                   event(EventNotificationEnum::HardWiredMonitor, 1)});

    ASSERT_EQ(notify_events.size(), 3);

    // Each connection is sent one NotifyEvent carrying only what it is subscribed to, plus the
    // hardwired one everybody gets.
    EXPECT_EQ(notify_events.at(0).first, ConnectionId::primary());
    EXPECT_EQ(notify_events.at(0).second, (std::vector<std::int32_t>{11, 1}));

    EXPECT_EQ(notify_events.at(1).first, ConnectionId::secondary("EMS"));
    EXPECT_EQ(notify_events.at(1).second, (std::vector<std::int32_t>{20, 1}));

    EXPECT_EQ(notify_events.at(2).first, ConnectionId::secondary("MAINT"));
    EXPECT_EQ(notify_events.at(2).second, (std::vector<std::int32_t>{1}));
}

TEST_F(NotifyEventRoutingTest, InstallingAMonitorSubscribesTheConnectionThatInstalledIt) {
    const auto ids = set_monitor_from(ConnectionId::secondary("EMS"), Variable{"HeartbeatInterval"});
    ASSERT_EQ(ids.size(), 1);

    EXPECT_EQ(subscription_of(ConnectionId::secondary("EMS")), std::set<std::int32_t>{ids.at(0)});
    EXPECT_TRUE(subscription_of(ConnectionId::primary()).empty());
    EXPECT_TRUE(subscription_of(ConnectionId::secondary("MAINT")).empty());

    notify_events.clear();
    diagnostics->notify_event_req({event(EventNotificationEnum::CustomMonitor, ids.at(0))});

    ASSERT_EQ(notify_events.size(), 1);
    EXPECT_EQ(notify_events.at(0).first, ConnectionId::secondary("EMS"));
}

TEST_F(NotifyEventRoutingTest, ClearingAMonitorUnsubscribesEveryone) {
    const auto ids = set_monitor_from(ConnectionId::secondary("EMS"), Variable{"HeartbeatInterval"});
    ASSERT_EQ(ids.size(), 1);
    subscribe(ConnectionId::primary(), {ids.at(0)});

    ClearVariableMonitoringRequest request;
    request.id = {ids.at(0)};
    ocpp::EnhancedMessage<MessageType> message;
    message.messageType = MessageType::ClearVariableMonitoring;
    message.message = ocpp::Call<ClearVariableMonitoringRequest>(request);
    diagnostics->handle_message(message);

    EXPECT_TRUE(subscription_of(ConnectionId::primary()).empty());
    EXPECT_TRUE(subscription_of(ConnectionId::secondary("EMS")).empty());
}

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

} // namespace
