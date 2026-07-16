// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <generic_ocpp.hpp>

#include "stubs/chargepoint_stub.hpp"
#include "stubs/config_stub.hpp"
#include "stubs/generic_ocpp_stub.hpp"
#include "stubs/interfaces_stub.hpp"

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

TEST_F(GenericOcppProvidesTester, errorTypeNotRemapped) {
    // the error type must reach the chargepoint implementations unmodified: the v16
    // error-code map and the v2 map_error() lookup are keyed on the full type
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

} // namespace
