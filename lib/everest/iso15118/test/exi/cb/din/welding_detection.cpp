// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/message_din/variant.hpp>
#include <iso15118/message_din/welding_detection.hpp>

#include "helper.hpp"

using namespace iso15118;
using namespace iso15118::message_din;

SCENARIO("Se/Deserialize DIN welding detection messages") {

    const datatypes::SessionId session_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

    GIVEN("Serialize and deserialize welding_detection_req") {
        WeldingDetectionRequest req;
        req.header.session_id = session_id;
        req.dc_ev_status.ev_ready = false;
        req.dc_ev_status.ev_ress_soc = 90;

        const auto bytes = serialize_helper(req);

        THEN("It round-trips through the Variant") {
            const io::StreamInputView view{bytes.data(), bytes.size()};
            message_din::Variant variant(view);

            REQUIRE(variant.get_type() == Type::WeldingDetectionReq);
            const auto& msg = variant.get<WeldingDetectionRequest>();
            REQUIRE(msg.header.session_id == session_id);
            REQUIRE(msg.dc_ev_status.ev_ready == false);
            REQUIRE(msg.dc_ev_status.ev_ress_soc == 90);
        }
    }

    GIVEN("Serialize and deserialize welding_detection_res") {
        WeldingDetectionResponse res;
        res.header.session_id = session_id;
        res.response_code = datatypes::ResponseCode::OK;
        res.dc_evse_status.evse_status_code = datatypes::DcEvseStatusCode::EVSE_Ready;
        res.evse_present_voltage = 10.0;

        const auto bytes = serialize_helper(res);

        THEN("It round-trips through the Variant") {
            const io::StreamInputView view{bytes.data(), bytes.size()};
            message_din::Variant variant(view);

            REQUIRE(variant.get_type() == Type::WeldingDetectionRes);
            const auto& msg = variant.get<WeldingDetectionResponse>();
            REQUIRE(msg.header.session_id == session_id);
            REQUIRE(msg.response_code == datatypes::ResponseCode::OK);
            REQUIRE(msg.evse_present_voltage == 10.0);
        }
    }
}
