// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/message_din/power_delivery.hpp>
#include <iso15118/message_din/variant.hpp>

#include "helper.hpp"

using namespace iso15118;
using namespace iso15118::message_din;

SCENARIO("Se/Deserialize DIN power delivery messages") {

    const datatypes::SessionId session_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

    GIVEN("Serialize and deserialize power_delivery_req") {
        PowerDeliveryRequest req;
        req.header.session_id = session_id;
        req.ready_to_charge_state = true;
        auto& param = req.dc_ev_power_delivery_parameter.emplace();
        param.dc_ev_status.ev_ready = true;
        param.dc_ev_status.ev_ress_soc = 60;
        param.bulk_charging_complete = false;
        param.charging_complete = false;

        const auto bytes = serialize_helper(req);

        THEN("It round-trips through the Variant") {
            const io::StreamInputView view{bytes.data(), bytes.size()};
            message_din::Variant variant(view);

            REQUIRE(variant.get_type() == Type::PowerDeliveryReq);
            const auto& msg = variant.get<PowerDeliveryRequest>();
            REQUIRE(msg.header.session_id == session_id);
            REQUIRE(msg.ready_to_charge_state == true);
            REQUIRE(msg.dc_ev_power_delivery_parameter.has_value());
            const auto& p = msg.dc_ev_power_delivery_parameter.value();
            REQUIRE(p.dc_ev_status.ev_ress_soc == 60);
            REQUIRE(p.bulk_charging_complete.has_value());
            REQUIRE(p.bulk_charging_complete.value() == false);
            REQUIRE(p.charging_complete == false);
        }
    }

    GIVEN("Serialize and deserialize power_delivery_res (DC EVSE status)") {
        PowerDeliveryResponse res;
        res.header.session_id = session_id;
        res.response_code = datatypes::ResponseCode::OK;
        auto& status = res.dc_evse_status.emplace();
        status.evse_status_code = datatypes::DcEvseStatusCode::EVSE_Ready;

        const auto bytes = serialize_helper(res);

        THEN("It round-trips through the Variant") {
            const io::StreamInputView view{bytes.data(), bytes.size()};
            message_din::Variant variant(view);

            REQUIRE(variant.get_type() == Type::PowerDeliveryRes);
            const auto& msg = variant.get<PowerDeliveryResponse>();
            REQUIRE(msg.header.session_id == session_id);
            REQUIRE(msg.response_code == datatypes::ResponseCode::OK);
            REQUIRE(msg.dc_evse_status.has_value());
            REQUIRE(msg.dc_evse_status.value().evse_status_code == datatypes::DcEvseStatusCode::EVSE_Ready);
            REQUIRE(msg.ac_evse_status.has_value() == false);
        }
    }
}
