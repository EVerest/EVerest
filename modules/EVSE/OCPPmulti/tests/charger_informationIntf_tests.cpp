// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

// cmds:
//   get_charger_information:

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <generic_ocpp.hpp>
#include <optional>

#include "stubs/generic_ocpp_stub.hpp"

namespace {
using namespace stubs;

TEST(GenericOcppTester, callGetChargerInformation) {
    // call_get_charger_information() used in
    // OCPP ready()

    using ::testing::_;

    stubs::ChargePointStub chargepoint;
    stubs::ConfigStub config;
    stubs::ModuleInterfaces interfaces;

    std::vector<json> received;
    interfaces.subscribe_var("charger_information", "call_get_charger_information", 0,
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

    ASSERT_EQ(received.size(), 1);
    EXPECT_EQ(received[0], json{});
}

} // namespace
