// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <gtest/gtest.h>

#include "chargepoint_stub.hpp"
#include "config_stub.hpp"
#include "interfaces_stub.hpp"
#include <generic_ocpp.hpp>
#include <memory>

namespace stubs {

struct GenericOcppTester : public ocpp_multi::GenericOcpp {
    using ocpp_multi::GenericOcpp::cb_all_connectors_unavailable;
    using ocpp_multi::GenericOcpp::cb_boot_notification;
    using ocpp_multi::GenericOcpp::cb_cancel_reservation;
    using ocpp_multi::GenericOcpp::cb_charging_needs;
    using ocpp_multi::GenericOcpp::cb_clear_display_message;
    using ocpp_multi::GenericOcpp::cb_configure_network_connection_profile;
    using ocpp_multi::GenericOcpp::cb_connection_state_changed;
    using ocpp_multi::GenericOcpp::cb_connector_effective_operative_status;
    using ocpp_multi::GenericOcpp::cb_data_transfer;
    using ocpp_multi::GenericOcpp::cb_default_price;
    using ocpp_multi::GenericOcpp::cb_ev_info;
    using ocpp_multi::GenericOcpp::cb_fault_cleared_handler;
    using ocpp_multi::GenericOcpp::cb_fault_handler;
    using ocpp_multi::GenericOcpp::cb_firmware_update_status;
    using ocpp_multi::GenericOcpp::cb_get_15118_ev_certificate_response;
    using ocpp_multi::GenericOcpp::cb_get_display_message;
    using ocpp_multi::GenericOcpp::cb_get_log_request;
    using ocpp_multi::GenericOcpp::cb_hw_capabilities;
    using ocpp_multi::GenericOcpp::cb_is_reservation_for_token;
    using ocpp_multi::GenericOcpp::cb_is_reset_allowed;
    using ocpp_multi::GenericOcpp::cb_iso15118_certificate_request;
    using ocpp_multi::GenericOcpp::cb_log_status;
    using ocpp_multi::GenericOcpp::cb_ocpp_messages;
    using ocpp_multi::GenericOcpp::cb_pause_charging;
    using ocpp_multi::GenericOcpp::cb_powermeter;
    using ocpp_multi::GenericOcpp::cb_powermeter_public_key_ocmf;
    using ocpp_multi::GenericOcpp::cb_provide_token;
    using ocpp_multi::GenericOcpp::cb_ready;
    using ocpp_multi::GenericOcpp::cb_reservation_update;
    using ocpp_multi::GenericOcpp::cb_reserve_now;
    using ocpp_multi::GenericOcpp::cb_reset;
    using ocpp_multi::GenericOcpp::cb_resume_charging;
    using ocpp_multi::GenericOcpp::cb_security_event;
    using ocpp_multi::GenericOcpp::cb_service_renegotiation_supported;
    using ocpp_multi::GenericOcpp::cb_session_event;
    using ocpp_multi::GenericOcpp::cb_set_charging_profiles;
    using ocpp_multi::GenericOcpp::cb_set_display_message;
    using ocpp_multi::GenericOcpp::cb_set_running_cost;
    using ocpp_multi::GenericOcpp::cb_stop_transaction;
    using ocpp_multi::GenericOcpp::cb_supported_energy_transfer_modes;
    using ocpp_multi::GenericOcpp::cb_tariff_message;
    using ocpp_multi::GenericOcpp::cb_time_sync;
    using ocpp_multi::GenericOcpp::cb_transaction_event;
    using ocpp_multi::GenericOcpp::cb_transaction_event_response;
    using ocpp_multi::GenericOcpp::cb_unlock_connector;
    using ocpp_multi::GenericOcpp::cb_update_allowed_energy_transfer_modes;
    using ocpp_multi::GenericOcpp::cb_update_firmware_request;
    using ocpp_multi::GenericOcpp::cb_validate_network_profile;
    using ocpp_multi::GenericOcpp::cb_variable_monitor;
    using ocpp_multi::GenericOcpp::cb_variable_set;
    using ocpp_multi::GenericOcpp::cb_waiting_for_external_ready;
    using ocpp_multi::GenericOcpp::charging_schedules_timer_running;
    using ocpp_multi::GenericOcpp::charging_schedules_timer_start;
    using ocpp_multi::GenericOcpp::charging_schedules_timer_stop;
    using ocpp_multi::GenericOcpp::create_limits_entry;
    using ocpp_multi::GenericOcpp::create_setpoint_entry;
    using ocpp_multi::GenericOcpp::GenericOcpp;
    using ocpp_multi::GenericOcpp::get_connector_structure;
    using ocpp_multi::GenericOcpp::init_check_energy_sink;
    using ocpp_multi::GenericOcpp::init_error_handlers;
    using ocpp_multi::GenericOcpp::init_evse_maps;
    using ocpp_multi::GenericOcpp::init_evse_subscribe;
    using ocpp_multi::GenericOcpp::init_subscribe;
    using ocpp_multi::GenericOcpp::process_authorised;
    using ocpp_multi::GenericOcpp::process_charging_paused_ev;
    using ocpp_multi::GenericOcpp::process_charging_paused_evse;
    using ocpp_multi::GenericOcpp::process_charging_started;
    using ocpp_multi::GenericOcpp::process_deauthorised;
    using ocpp_multi::GenericOcpp::process_disabled;
    using ocpp_multi::GenericOcpp::process_enabled;
    using ocpp_multi::GenericOcpp::process_reservation_end;
    using ocpp_multi::GenericOcpp::process_reserved;
    using ocpp_multi::GenericOcpp::process_session_event;
    using ocpp_multi::GenericOcpp::process_session_finished;
    using ocpp_multi::GenericOcpp::process_session_resumed;
    using ocpp_multi::GenericOcpp::process_session_started;
    using ocpp_multi::GenericOcpp::process_transaction_finished;
    using ocpp_multi::GenericOcpp::process_transaction_started;
    using ocpp_multi::GenericOcpp::process_tx_event_effect;
    using ocpp_multi::GenericOcpp::publish_charging_schedules;
    using ocpp_multi::GenericOcpp::ready_event_queue;
    using ocpp_multi::GenericOcpp::ready_module_configuration;
    using ocpp_multi::GenericOcpp::ready_transaction_handler;
    using ocpp_multi::GenericOcpp::set_external_limits;
    using ocpp_multi::GenericOcpp::wait_all_ready;

    // access to variables
    using ocpp_multi::GenericOcpp::evse_evcc_id;
    using ocpp_multi::GenericOcpp::evse_hardware_capabilities_map;
    using ocpp_multi::GenericOcpp::evse_soc_map;
    using ocpp_multi::GenericOcpp::evse_supported_energy_transfer_modes;
};

// creates the OCPP object and performs initialisation before every test
class GenericOcppProvidesTester : public testing::Test {
protected:
    stubs::ChargePointStub chargepoint;
    stubs::ConfigStub config;
    std::unique_ptr<stubs::ModuleInterfaces> interfaces;
    std::unique_ptr<stubs::GenericOcppTester> ocpp;

    void SetUp() override {
        using ::testing::_;
        interfaces = std::make_unique<stubs::ModuleInterfaces>();
        ocpp = std::make_unique<stubs::GenericOcppTester>(chargepoint, interfaces->get_module_info(), config,
                                                          interfaces->get_provides(), interfaces->get_requires());
        // connect required interfaces
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
        // ocpp.ready() waits for the EVSE managers to be ready
        interfaces->publish_ready(0, true);
        interfaces->publish_ready(1, true);
        ocpp->ready(interfaces->get_config_service_client());
    }

    void TearDown() override {
        // consider removing generated/updated files/databases etc.
        interfaces.reset();
        ocpp.reset();
    }
};

struct GenericOcppRequiresTester : public GenericOcppProvidesTester {};

} // namespace stubs
