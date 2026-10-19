// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>

#include <generic_ocpp.hpp>

#include "mrec_fixture.hpp"
#include "stubs/chargepoint_stub.hpp"
#include "stubs/config_stub.hpp"
#include "stubs/generic_ocpp_stub.hpp"
#include "stubs/interfaces_stub.hpp"

#include <everest/ocpp_module_common/error_handling.hpp>

namespace {
using namespace stubs;

TEST(GenericOcppTester, init) {
    using ::testing::_;
    using ::testing::InSequence;
    using ::testing::Return;

    stubs::ChargePointStub chargepoint;
    stubs::ConfigStub config;
    stubs::ModuleInterfaces interfaces;

    std::vector<json> received;
    interfaces.subscribe_var("evse_manager", "call_external_ready_to_start_charging",
                             [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

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

    // Chargepoint expected calls
    InSequence seq;
    EXPECT_CALL(chargepoint, init(_));
    EXPECT_CALL(chargepoint, get_all_composite_schedules(600, _));
    EXPECT_CALL(chargepoint, set_message_queue_resume_delay(std::chrono::seconds(config.MessageQueueResumeDelay)));
    EXPECT_CALL(chargepoint, start(_, _, false));
    EXPECT_CALL(chargepoint, connect_websocket());

    // GenericOcpp object
    stubs::GenericOcppTester ocpp(chargepoint, interfaces.get_module_info(), config, interfaces.get_provides(),
                                  interfaces.get_requires());

    interfaces.subscribe_global_all_errors(
        [&ocpp](const Everest::error::Error& arg) { ocpp.cb_error_handler(arg); },
        [&ocpp](const Everest::error::Error& arg) { ocpp.cb_error_cleared_handler(arg); });

    ocpp.init();

    // ocpp.ready() waits for the EVSE managers to be ready
    interfaces.publish_ready(0, true);
    interfaces.publish_ready(1, true);

    ocpp.ready(interfaces.get_config_service_client());

    ASSERT_EQ(received.size(), 2);
    EXPECT_EQ(received[0], json{});
    EXPECT_EQ(received[1], json{});
}

TEST(GenericOcppTester, queuedInoperativeFaultKeepsEvseId) {
    // An evse_manager/Inoperative raised before OCPP has started is queued and replayed in ready().
    // The EVSE is known from the per-requirement subscription; an evse_manager without a `mapping`
    // in the config must not be replayed as EVSE 0 (libocpp throws EvseOutOfRangeException for it,
    // which took the module down at start-up on the transient BSP CommunicationFault).
    using ::testing::_;

    stubs::ChargePointStub chargepoint;
    stubs::ConfigStub config;
    stubs::ModuleInterfaces interfaces;

    interfaces.add_charger_information("info");
    interfaces.add_data_transfer("data_transfer");
    interfaces.add_display_message("display");
    interfaces.add_evse_energy_sink("energy_node", 1);
    interfaces.add_evse_manager("evse_manager_1");
    interfaces.add_evse_manager("evse_manager_2");
    interfaces.add_extensions_15118("evsev2g");
    interfaces.add_reservation("reservation");

    chargepoint.load_store("default_store.json");

    EXPECT_CALL(chargepoint, init(_)).Times(1);
    EXPECT_CALL(chargepoint, get_all_composite_schedules(600, _)).Times(1);
    EXPECT_CALL(chargepoint, set_message_queue_resume_delay(std::chrono::seconds(config.MessageQueueResumeDelay)))
        .Times(1);
    EXPECT_CALL(chargepoint, start(_, _, false)).Times(1);
    EXPECT_CALL(chargepoint, connect_websocket()).Times(1);

    // both queued events reach the chargepoint with the EVSE of the subscription, never EVSE 0
    std::vector<std::int32_t> event_evse_ids;
    EXPECT_CALL(chargepoint, on_event(_)).Times(2).WillRepeatedly([&event_evse_ids](const auto& event) {
        event_evse_ids.push_back(event.evse_id);
    });
    EXPECT_CALL(chargepoint, on_faulted(2, _)).Times(1);
    EXPECT_CALL(chargepoint, on_fault_cleared(2, _)).Times(1);
    EXPECT_CALL(chargepoint, on_faulted(0, _)).Times(0);
    EXPECT_CALL(chargepoint, on_fault_cleared(0, _)).Times(0);

    stubs::GenericOcppTester ocpp(chargepoint, interfaces.get_module_info(), config, interfaces.get_provides(),
                                  interfaces.get_requires());
    ocpp.init();

    // raised and cleared before ready(): the origin carries no mapping
    Everest::error::Error error;
    error.type = ocpp_module_common::EVSE_MANAGER_INOPERATIVE_ERROR;
    ASSERT_FALSE(error.origin.mapping.has_value());
    ocpp.cb_fault_handler(2, error);
    ocpp.cb_fault_cleared_handler(2, error);

    interfaces.publish_ready(0, true);
    interfaces.publish_ready(1, true);
    ocpp.ready(interfaces.get_config_service_client());

    EXPECT_EQ(event_evse_ids, (std::vector<std::int32_t>{2, 2}));
}

TEST_F(GenericOcppProvidesTester, errorTypeNotRemapped) {
    // the error type must reach the chargepoint implementations unmodified: the v16
    // error-code map and the v2 techCode lookup are keyed on the full type
    using ::testing::_;

    std::optional<ocpp_multi::GenericChargePointInterface::EventInfo> event;
    EXPECT_CALL(chargepoint, on_event(_)).WillOnce([&event](const auto& arg) { event = arg; });

    Everest::error::Error error;
    error.type = "evse_board_support/MREC2GroundFailure";
    ocpp->cb_error_handler(error);

    ASSERT_TRUE(event.has_value());
    ASSERT_TRUE(event->error.has_value());
    EXPECT_EQ(event->error->type, "evse_board_support/MREC2GroundFailure");
    EXPECT_FALSE(event->event_cleared);
}

TEST_F(GenericOcppProvidesTester, mrecErrorsForwardedUnmodifiedOnRaiseAndClear) {
    // every MREC error reaches the chargepoint implementation unchanged, with the
    // cleared flag matching the direction; the protocol-specific mapping happens there
    using ::testing::_;

    std::vector<ocpp_multi::GenericChargePointInterface::EventInfo> events;
    EXPECT_CALL(chargepoint, on_event(_)).WillRepeatedly([&events](const auto& arg) { events.push_back(arg); });

    for (const auto& entry : mrec_fixture::ENTRIES) {
        SCOPED_TRACE(std::string(entry.type));
        events.clear();

        Everest::error::Error error;
        error.type = std::string(entry.type);
        error.sub_type = "some_sub_type";
        error.message = "sensor reports fault";
        ocpp->cb_error_handler(error);
        ocpp->cb_error_cleared_handler(error);

        ASSERT_EQ(events.size(), 2U);
        for (std::size_t i = 0; i < events.size(); ++i) {
            ASSERT_TRUE(events[i].error.has_value());
            EXPECT_EQ(events[i].error->type, entry.type);
            EXPECT_EQ(events[i].error->sub_type, "some_sub_type");
            EXPECT_EQ(events[i].error->message, "sensor reports fault");
            EXPECT_EQ(events[i].error->uuid, error.uuid);
            EXPECT_EQ(events[i].event_cleared, i == 1);
        }
    }
}

} // namespace
