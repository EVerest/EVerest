// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <stdexcept>
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

TEST(GenericOcppTester, inoperativeFaultUsesOcppEvseIdWhenEverestIdsDiffer) {
    // The fault handlers are subscribed per evse_manager position, which is the OCPP evse id. With
    // EVerest evse ids 2 and 3 (OCPP 1 and 2) the event must not be translated a second time as if
    // it carried an EVerest id: EVSE 2 would be dropped as not served, EVSE 3 reported as EVSE 1.
    using ::testing::_;

    stubs::ChargePointStub chargepoint;
    stubs::ConfigStub config;
    stubs::ModuleInterfaces interfaces;

    interfaces.add_charger_information("info");
    interfaces.add_data_transfer("data_transfer");
    interfaces.add_display_message("display");
    interfaces.add_evse_energy_sink("energy_node", 1);
    interfaces.add_evse_manager("evse_manager_1", 2);
    interfaces.add_evse_manager("evse_manager_2", 3);
    interfaces.add_extensions_15118("evsev2g");
    interfaces.add_reservation("reservation");

    chargepoint.load_store("default_store.json");

    EXPECT_CALL(chargepoint, init(_)).Times(1);
    EXPECT_CALL(chargepoint, get_all_composite_schedules(600, _)).Times(1);
    EXPECT_CALL(chargepoint, set_message_queue_resume_delay(std::chrono::seconds(config.MessageQueueResumeDelay)))
        .Times(1);
    EXPECT_CALL(chargepoint, start(_, _, false)).Times(1);
    EXPECT_CALL(chargepoint, connect_websocket()).Times(1);

    std::vector<std::int32_t> event_evse_ids;
    EXPECT_CALL(chargepoint, on_event(_)).Times(4).WillRepeatedly([&event_evse_ids](const auto& event) {
        event_evse_ids.push_back(event.evse_id);
    });
    EXPECT_CALL(chargepoint, on_faulted(1, _)).Times(1);
    EXPECT_CALL(chargepoint, on_fault_cleared(1, _)).Times(1);
    EXPECT_CALL(chargepoint, on_faulted(2, _)).Times(1);
    EXPECT_CALL(chargepoint, on_fault_cleared(2, _)).Times(1);

    stubs::GenericOcppTester ocpp(chargepoint, interfaces.get_module_info(), config, interfaces.get_provides(),
                                  interfaces.get_requires());
    ocpp.init();

    Everest::error::Error error;
    error.type = ocpp_module_common::EVSE_MANAGER_INOPERATIVE_ERROR;

    // queued before ready() on the first evse_manager (EVerest 2, OCPP 1)
    ocpp.cb_fault_handler(1, error);
    ocpp.cb_fault_cleared_handler(1, error);

    interfaces.publish_ready(0, true);
    interfaces.publish_ready(1, true);
    ocpp.ready(interfaces.get_config_service_client());

    // live after ready() on the second evse_manager (EVerest 3, OCPP 2)
    ocpp.cb_fault_handler(2, error);
    ocpp.cb_fault_cleared_handler(2, error);

    EXPECT_EQ(event_evse_ids, (std::vector<std::int32_t>{1, 1, 2, 2}));
}

TEST(GenericOcppTester, errorUsesOcppEvseIdWhenEverestIdsDiffer) {
    // Errors other than evse_manager/Inoperative carry the EVerest evse id of their mapping and are
    // translated to the OCPP evse id, queued and live. With EVerest evse ids 2 and 3 (OCPP 1 and 2) an
    // error mapped to EVerest EVSE 3 is reported on OCPP EVSE 2; one mapped to EVerest EVSE 1 is not
    // served by this instance and dropped.
    using ::testing::_;

    stubs::ChargePointStub chargepoint;
    stubs::ConfigStub config;
    stubs::ModuleInterfaces interfaces;

    interfaces.add_charger_information("info");
    interfaces.add_data_transfer("data_transfer");
    interfaces.add_display_message("display");
    interfaces.add_evse_energy_sink("energy_node", 1);
    interfaces.add_evse_manager("evse_manager_1", 2);
    interfaces.add_evse_manager("evse_manager_2", 3);
    interfaces.add_extensions_15118("evsev2g");
    interfaces.add_reservation("reservation");

    chargepoint.load_store("default_store.json");

    EXPECT_CALL(chargepoint, init(_)).Times(1);
    EXPECT_CALL(chargepoint, get_all_composite_schedules(600, _)).Times(1);
    EXPECT_CALL(chargepoint, set_message_queue_resume_delay(std::chrono::seconds(config.MessageQueueResumeDelay)))
        .Times(1);
    EXPECT_CALL(chargepoint, start(_, _, false)).Times(1);
    EXPECT_CALL(chargepoint, connect_websocket()).Times(1);

    std::vector<std::int32_t> event_evse_ids;
    EXPECT_CALL(chargepoint, on_event(_)).Times(4).WillRepeatedly([&event_evse_ids](const auto& event) {
        event_evse_ids.push_back(event.evse_id);
    });
    EXPECT_CALL(chargepoint, on_faulted(_, _)).Times(0);
    EXPECT_CALL(chargepoint, on_fault_cleared(_, _)).Times(0);

    stubs::GenericOcppTester ocpp(chargepoint, interfaces.get_module_info(), config, interfaces.get_provides(),
                                  interfaces.get_requires());
    ocpp.init();

    Everest::error::Error served;
    served.type = "evse_board_support/MREC2GroundFailure";
    served.origin.mapping = Mapping{3};
    Everest::error::Error not_served = served;
    not_served.origin.mapping = Mapping{1};

    // queued before ready()
    ocpp.cb_error_handler(served);
    ocpp.cb_error_cleared_handler(served);
    ocpp.cb_error_handler(not_served);

    interfaces.publish_ready(0, true);
    interfaces.publish_ready(1, true);
    ocpp.ready(interfaces.get_config_service_client());

    // live after ready()
    ocpp.cb_error_handler(served);
    ocpp.cb_error_cleared_handler(served);
    ocpp.cb_error_handler(not_served);

    EXPECT_EQ(event_evse_ids, (std::vector<std::int32_t>{2, 2, 2, 2}));
}

TEST(GenericOcppTester, duplicateEverestEvseIdIsRejected) {
    stubs::ChargePointStub chargepoint;
    stubs::ConfigStub config;
    stubs::ModuleInterfaces interfaces;

    interfaces.add_evse_manager("evse_manager_1", 2);
    interfaces.add_evse_manager("evse_manager_2", 2);

    stubs::GenericOcppTester ocpp(chargepoint, interfaces.get_module_info(), config, interfaces.get_provides(),
                                  interfaces.get_requires());
    ocpp.init();

    interfaces.publish_ready(0, true);
    interfaces.publish_ready(1, true);
    EXPECT_THROW(ocpp.ready(interfaces.get_config_service_client()), std::runtime_error);
}

// EVerest evse ids 2 and 3, i.e. OCPP evse ids 1 and 2
class GenericOcppEvseIdTester : public testing::Test {
protected:
    stubs::ChargePointStub chargepoint;
    stubs::ConfigStub config;
    stubs::ModuleInterfaces interfaces;
    ocpp_multi::GenericChargePointInterface::ConnectorStructure init_evse_connector_structure;
    ocpp_multi::GenericChargePointInterface::ConnectorStructureV16 init_connector_mapping;
    std::unique_ptr<stubs::GenericOcppTester> ocpp;

    void SetUp() override {
        using ::testing::_;

        interfaces.add_charger_information("info");
        interfaces.add_data_transfer("data_transfer");
        interfaces.add_display_message("display");
        interfaces.add_evse_energy_sink("energy_node_0", 0);
        interfaces.add_evse_energy_sink("energy_node_2", 2);
        interfaces.add_evse_manager("evse_manager_1", 2);
        interfaces.add_evse_manager("evse_manager_2", 3);
        interfaces.add_extensions_15118("evsev2g");
        interfaces.add_reservation("reservation");

        chargepoint.load_store("default_store.json");

        EXPECT_CALL(chargepoint, init(_)).Times(1).WillOnce([this](const auto& args) {
            init_evse_connector_structure = args.evse_connector_structure;
            init_connector_mapping = args.connector_mapping;
        });
        EXPECT_CALL(chargepoint, get_all_composite_schedules(600, _)).Times(1);
        EXPECT_CALL(chargepoint, set_message_queue_resume_delay(std::chrono::seconds(config.MessageQueueResumeDelay)))
            .Times(1);
        EXPECT_CALL(chargepoint, start(_, _, false)).Times(1);
        EXPECT_CALL(chargepoint, connect_websocket()).Times(1);

        ocpp = std::make_unique<stubs::GenericOcppTester>(chargepoint, interfaces.get_module_info(), config,
                                                          interfaces.get_provides(), interfaces.get_requires());
        ocpp->init();
        interfaces.publish_ready(0, true);
        interfaces.publish_ready(1, true);
        ocpp->ready(interfaces.get_config_service_client());
    }

    static ocpp::v2::EnhancedCompositeSchedule composite_schedule(std::int32_t evse_id) {
        ocpp::v2::EnhancedChargingSchedulePeriod period;
        period.startPeriod = 0;
        period.limit = 16.0F;
        ocpp::v2::EnhancedCompositeSchedule schedule;
        schedule.evseId = evse_id;
        schedule.chargingRateUnit = ocpp::v2::ChargingRateUnitEnum::A;
        schedule.chargingSchedulePeriod = {period};
        return schedule;
    }
};

TEST_F(GenericOcppEvseIdTester, connectorStructureUsesOcppEvseIds) {
    EXPECT_EQ(init_evse_connector_structure,
              (ocpp_multi::GenericChargePointInterface::ConnectorStructure{{1, 1}, {2, 1}}));
    EXPECT_EQ(init_connector_mapping,
              (ocpp_multi::GenericChargePointInterface::ConnectorStructureV16{{1, {{1, 1}}}, {2, {{1, 2}}}}));
}

TEST_F(GenericOcppEvseIdTester, externalLimitsReachSinkOfEverestEvse) {
    std::vector<int> called_sinks;
    for (const int sink : {0, 1}) {
        interfaces.subscribe_var(
            "external_energy_limits", "call_set_external_limits", sink,
            [&called_sinks, sink](const auto&, const auto&, const auto&) { called_sinks.push_back(sink); });
    }

    ocpp->set_external_limits({composite_schedule(0), composite_schedule(1), composite_schedule(2)});

    EXPECT_EQ(called_sinks, (std::vector<int>{0, 1}));
}

TEST_F(GenericOcppEvseIdTester, reserveNowUsesEverestEvseId) {
    std::optional<json> call_args;
    interfaces.subscribe_var("reservation", "call_reserve_now",
                             [&call_args](const auto&, const auto&, const auto& data) { call_args = data; });

    ocpp::v2::ReserveNowRequest request;
    request.id = 1;
    request.evseId = 1;
    ocpp->cb_reserve_now(request);

    ASSERT_TRUE(call_args.has_value());
    EXPECT_EQ(call_args->at("request").at("evse_id"), 2);
}

TEST_F(GenericOcppEvseIdTester, provideTokenUsesEverestEvseId) {
    std::optional<json> provided_token;
    interfaces.subscribe_var("auth_provider", "provided_token",
                             [&provided_token](const auto&, const auto&, const auto& data) { provided_token = data; });

    ocpp_multi::GenericChargePointCallbacks::IdToken id_token{};
    id_token.token.idToken = "TOKEN";
    id_token.token.type = "ISO14443";
    id_token.evse_id = 2;
    ocpp->cb_provide_token(id_token);

    ASSERT_TRUE(provided_token.has_value());
    EXPECT_EQ(provided_token->at("connectors"), json::array({3}));
}

TEST_F(GenericOcppEvseIdTester, changeAvailabilityUsesOcppEvseId) {
    using ::testing::Truly;

    EXPECT_CALL(chargepoint, on_change_availability(Truly([](const ocpp::v2::ChangeAvailabilityRequest& request) {
                    return request.evse.has_value() && request.evse->id == 2;
                })))
        .Times(1);

    types::ocpp::ChangeAvailabilityRequest request;
    request.operational_status = types::ocpp::OperationalStatusEnumType::Inoperative;
    request.evse = types::ocpp::EVSE{3};
    ocpp->handle_change_availability(request);
}

TEST_F(GenericOcppEvseIdTester, changeAvailabilityForUnservedEvseIsRejected) {
    using ::testing::_;

    EXPECT_CALL(chargepoint, on_change_availability(_)).Times(0);

    types::ocpp::ChangeAvailabilityRequest request;
    request.operational_status = types::ocpp::OperationalStatusEnumType::Inoperative;
    request.evse = types::ocpp::EVSE{1};
    ocpp->handle_change_availability(request);
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