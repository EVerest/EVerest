// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/message_din/charge_parameter_discovery.hpp>
#include <iso15118/message_din/variant.hpp>

#include "helper.hpp"

using namespace iso15118;
using namespace iso15118::message_din;

SCENARIO("Se/Deserialize DIN charge parameter discovery messages") {

    const datatypes::SessionId session_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

    GIVEN("Serialize and deserialize charge_parameter_discovery_req (DC)") {
        ChargeParameterDiscoveryRequest req;
        req.header.session_id = session_id;
        req.ev_requested_energy_transfer_type = datatypes::EnergyTransferMode::DC_extended;

        auto& dc = req.dc_ev_charge_parameter.emplace();
        dc.dc_ev_status.ev_ready = true;
        dc.dc_ev_status.ev_error_code = datatypes::DcEvErrorCode::NO_ERROR;
        dc.dc_ev_status.ev_ress_soc = 50;
        dc.ev_maximum_current_limit = 200.0;
        dc.ev_maximum_power_limit = 60000.0;
        dc.ev_maximum_voltage_limit = 500.0;
        dc.full_soc = 100;
        dc.bulk_soc = 80;

        const auto bytes = serialize_helper(req);

        THEN("It round-trips through the Variant") {
            const io::StreamInputView view{bytes.data(), bytes.size()};
            message_din::Variant variant(view);

            REQUIRE(variant.get_type() == Type::ChargeParameterDiscoveryReq);
            const auto& msg = variant.get<ChargeParameterDiscoveryRequest>();
            REQUIRE(msg.header.session_id == session_id);
            REQUIRE(msg.ev_requested_energy_transfer_type == datatypes::EnergyTransferMode::DC_extended);
            REQUIRE(msg.dc_ev_charge_parameter.has_value());
            const auto& d = msg.dc_ev_charge_parameter.value();
            REQUIRE(d.dc_ev_status.ev_ready == true);
            REQUIRE(d.dc_ev_status.ev_ress_soc == 50);
            REQUIRE(d.ev_maximum_current_limit == 200.0);
            REQUIRE(d.ev_maximum_power_limit.value() == 60000.0);
            REQUIRE(d.ev_maximum_voltage_limit == 500.0);
            REQUIRE(d.full_soc.value() == 100);
            REQUIRE(d.bulk_soc.value() == 80);
        }
    }

    GIVEN("Serialize and deserialize charge_parameter_discovery_res (DC)") {
        ChargeParameterDiscoveryResponse res;
        res.header.session_id = session_id;
        res.response_code = datatypes::ResponseCode::OK;
        res.evse_processing = datatypes::EvseProcessing::Ongoing;

        auto& dc = res.dc_evse_charge_parameter.emplace();
        dc.dc_evse_status.evse_status_code = datatypes::DcEvseStatusCode::EVSE_Ready;
        dc.dc_evse_status.notification_max_delay = 0;
        dc.dc_evse_status.evse_notification = datatypes::EvseNotification::None;
        dc.evse_maximum_current_limit = 300.0;
        dc.evse_maximum_power_limit = 60000.0;
        dc.evse_maximum_voltage_limit = 900.0;
        dc.evse_minimum_current_limit = 0.0;
        dc.evse_minimum_voltage_limit = 200.0;
        dc.evse_peak_current_ripple = 2.0;

        const auto bytes = serialize_helper(res);

        THEN("It round-trips through the Variant") {
            const io::StreamInputView view{bytes.data(), bytes.size()};
            message_din::Variant variant(view);

            REQUIRE(variant.get_type() == Type::ChargeParameterDiscoveryRes);
            const auto& msg = variant.get<ChargeParameterDiscoveryResponse>();
            REQUIRE(msg.header.session_id == session_id);
            REQUIRE(msg.response_code == datatypes::ResponseCode::OK);
            REQUIRE(msg.evse_processing == datatypes::EvseProcessing::Ongoing);
            REQUIRE(msg.dc_evse_charge_parameter.has_value());
            const auto& d = msg.dc_evse_charge_parameter.value();
            REQUIRE(d.dc_evse_status.evse_status_code == datatypes::DcEvseStatusCode::EVSE_Ready);
            REQUIRE(d.evse_maximum_current_limit == 300.0);
            REQUIRE(d.evse_maximum_power_limit.value() == 60000.0);
            REQUIRE(d.evse_maximum_voltage_limit == 900.0);
            REQUIRE(d.evse_minimum_voltage_limit == 200.0);
            REQUIRE(d.evse_peak_current_ripple == 2.0);
        }
    }
}
