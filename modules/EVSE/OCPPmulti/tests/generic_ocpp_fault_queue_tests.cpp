// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

// EvseManager Inoperative faults that arrive before ready() has finished are queued and replayed.
// A replayed fault must reach the same EVSE as a live one: the EVSE the fault was subscribed for,
// not the one in the error's origin mapping, which is optional.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>

#include <generic_ocpp.hpp>

#include "stubs/chargepoint_stub.hpp"
#include "stubs/config_stub.hpp"
#include "stubs/generic_ocpp_stub.hpp"
#include "stubs/interfaces_stub.hpp"

namespace {

using EventInfo = ocpp_multi::GenericChargePointInterface::EventInfo;
using ::testing::_;
using ::testing::Field;
using ::testing::InSequence;

Everest::error::Error make_inoperative(std::optional<Mapping> mapping) {
    Everest::error::Error error;
    error.type = "evse_manager/Inoperative";
    error.message = "EVSE is inoperative";
    error.origin = ImplementationIdentifier("evse_manager", "evse", mapping);
    return error;
}

// same setup as stubs::GenericOcppProvidesTester, but stops after init(): faults raised before
// start() are queued
class GenericOcppFaultQueue : public testing::Test {
protected:
    stubs::ChargePointStub chargepoint;
    stubs::ConfigStub config;
    std::unique_ptr<stubs::ModuleInterfaces> interfaces;
    std::unique_ptr<stubs::GenericOcppTester> ocpp;

    void SetUp() override {
        interfaces = std::make_unique<stubs::ModuleInterfaces>();
        ocpp = std::make_unique<stubs::GenericOcppTester>(chargepoint, interfaces->get_module_info(), config,
                                                          interfaces->get_provides(), interfaces->get_requires());
        interfaces->add_charger_information("info");
        interfaces->add_data_transfer("data_transfer");
        interfaces->add_display_message("display");
        interfaces->add_evse_energy_sink("energy_node", 1);
        interfaces->add_evse_manager("evse_manager_1");
        interfaces->add_evse_manager("evse_manager_2");
        interfaces->add_extensions_15118("evsev2g");
        interfaces->add_reservation("reservation");
        chargepoint.load_store("default_store.json");
        EXPECT_CALL(chargepoint, init(_)).Times(1);
        EXPECT_CALL(chargepoint, get_all_composite_schedules(600, _)).Times(1);
        EXPECT_CALL(chargepoint, set_message_queue_resume_delay(std::chrono::seconds(config.MessageQueueResumeDelay)))
            .Times(1);
        EXPECT_CALL(chargepoint, start(_, _, false)).Times(1);
        EXPECT_CALL(chargepoint, connect_websocket()).Times(1);
        ocpp->init();
    }

    void TearDown() override {
        interfaces.reset();
        ocpp.reset();
    }

    // replays the queue
    void start() {
        interfaces->publish_ready(0, true);
        interfaces->publish_ready(1, true);
        ocpp->ready(interfaces->get_config_service_client());
    }
};

TEST_F(GenericOcppFaultQueue, QueuedFaultWithoutOriginMappingUsesSubscribedEvse) {
    ocpp->cb_fault_handler(2, make_inoperative(std::nullopt));

    InSequence seq;
    EXPECT_CALL(chargepoint, on_event(Field(&EventInfo::event_cleared, false)));
    EXPECT_CALL(chargepoint, on_faulted(2, 1));
    start();
}

TEST_F(GenericOcppFaultQueue, QueuedFaultClearWithoutOriginMappingUsesSubscribedEvse) {
    const auto error = make_inoperative(std::nullopt);
    ocpp->cb_fault_handler(2, error);
    ocpp->cb_fault_cleared_handler(2, error);

    InSequence seq;
    EXPECT_CALL(chargepoint, on_event(Field(&EventInfo::event_cleared, false)));
    EXPECT_CALL(chargepoint, on_faulted(2, 1));
    EXPECT_CALL(chargepoint, on_event(Field(&EventInfo::event_cleared, true)));
    EXPECT_CALL(chargepoint, on_fault_cleared(2, 1));
    start();
}

TEST_F(GenericOcppFaultQueue, QueuedFaultWithMismatchedMappingUsesSubscribedEvse) {
    ocpp->cb_fault_handler(1, make_inoperative(Mapping(2, 1)));

    InSequence seq;
    EXPECT_CALL(chargepoint, on_event(_));
    EXPECT_CALL(chargepoint, on_faulted(1, 1));
    start();
}

TEST_F(GenericOcppFaultQueue, QueuedFaultWithMatchingMappingIsUnchanged) {
    ocpp->cb_fault_handler(1, make_inoperative(Mapping(1, 1)));

    InSequence seq;
    EXPECT_CALL(chargepoint, on_event(_));
    EXPECT_CALL(chargepoint, on_faulted(1, 1));
    start();
}

// the reported event itself keeps the EVSE from the origin, so the event content is the same
// whether the fault was queued or not
TEST_F(GenericOcppFaultQueue, QueuedFaultKeepsOriginInEvent) {
    ocpp->cb_fault_handler(2, make_inoperative(std::nullopt));

    InSequence seq;
    EXPECT_CALL(chargepoint, on_event(Field(&EventInfo::evse_id, 0)));
    EXPECT_CALL(chargepoint, on_faulted(_, _));
    start();
}

TEST_F(GenericOcppFaultQueue, LiveFaultWithoutOriginMappingUsesSubscribedEvse) {
    start();

    InSequence seq;
    EXPECT_CALL(chargepoint, on_event(Field(&EventInfo::evse_id, 0)));
    EXPECT_CALL(chargepoint, on_faulted(2, 1));
    ocpp->cb_fault_handler(2, make_inoperative(std::nullopt));
}

} // namespace
