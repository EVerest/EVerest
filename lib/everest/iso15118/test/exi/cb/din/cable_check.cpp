// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/message_din/cable_check.hpp>
#include <iso15118/message_din/variant.hpp>

#include "helper.hpp"

using namespace iso15118;
using namespace iso15118::message_din;

SCENARIO("Se/Deserialize DIN cable check messages") {

    const datatypes::SessionId session_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

    GIVEN("Serialize and deserialize cable_check_req") {
        CableCheckRequest req;
        req.header.session_id = session_id;
        req.dc_ev_status.ev_ready = true;
        req.dc_ev_status.ev_error_code = datatypes::DcEvErrorCode::NO_ERROR;
        req.dc_ev_status.ev_ress_soc = 42;

        const auto bytes = serialize_helper(req);

        THEN("It round-trips through the Variant") {
            const io::StreamInputView view{bytes.data(), bytes.size()};
            message_din::Variant variant(view);

            REQUIRE(variant.get_type() == Type::CableCheckReq);
            const auto& msg = variant.get<CableCheckRequest>();
            REQUIRE(msg.header.session_id == session_id);
            REQUIRE(msg.dc_ev_status.ev_ready == true);
            REQUIRE(msg.dc_ev_status.ev_ress_soc == 42);
        }
    }

    GIVEN("Serialize and deserialize cable_check_res") {
        CableCheckResponse res;
        res.header.session_id = session_id;
        res.response_code = datatypes::ResponseCode::OK;
        res.dc_evse_status.evse_isolation_status = datatypes::IsolationLevel::Valid;
        res.dc_evse_status.evse_status_code = datatypes::DcEvseStatusCode::EVSE_IsolationMonitoringActive;
        res.evse_processing = datatypes::EvseProcessing::Ongoing;

        const auto bytes = serialize_helper(res);

        THEN("It round-trips through the Variant") {
            const io::StreamInputView view{bytes.data(), bytes.size()};
            message_din::Variant variant(view);

            REQUIRE(variant.get_type() == Type::CableCheckRes);
            const auto& msg = variant.get<CableCheckResponse>();
            REQUIRE(msg.header.session_id == session_id);
            REQUIRE(msg.response_code == datatypes::ResponseCode::OK);
            REQUIRE(msg.dc_evse_status.evse_isolation_status.has_value());
            REQUIRE(msg.dc_evse_status.evse_isolation_status.value() == datatypes::IsolationLevel::Valid);
            REQUIRE(msg.dc_evse_status.evse_status_code == datatypes::DcEvseStatusCode::EVSE_IsolationMonitoringActive);
            REQUIRE(msg.evse_processing == datatypes::EvseProcessing::Ongoing);
        }
    }
}
