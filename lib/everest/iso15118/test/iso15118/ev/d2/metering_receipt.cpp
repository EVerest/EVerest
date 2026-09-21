// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/d2/state/metering_receipt.hpp>
#include <iso15118/message_2/authorization.hpp>
#include <iso15118/message_2/metering_receipt.hpp>

using namespace iso15118;
namespace dt = message_2::datatypes;

namespace {

dt::MeterInfo make_meter_info() {
    dt::MeterInfo info;
    info.meter_id = "METER1";
    info.meter_reading = 4321;
    info.t_meter = 1691411798;
    return info;
}

message_2::MeteringReceiptResponse make_response(const message_2::Header& header, dt::ResponseCode code) {
    message_2::MeteringReceiptResponse res;
    res.header = header;
    res.response_code = code;
    res.dc_evse_status = dt::DC_EVSEStatus{};
    return res;
}

const auto seed_contract = [](D2StateHelper& helper) {
    auto& pnc = helper.get_context().pnc;
    pnc.contract_selected = true;
    pnc.contract_key_pem = make_test_ec_key_pem();
};

} // namespace

SCENARIO("ISO15118-2 EV MeteringReceipt signs the loop MeterInfo on enter") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::MeteringReceipt> primed{callbacks,     ev::EvSessionParams{}, false,
                                                       seed_contract, make_meter_info(),     std::optional<uint8_t>{7}};

    const auto request = primed.take_requests().get<message_2::MeteringReceiptRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->header.session_id == D2_SESSION_ID);
    REQUIRE(request->session_id == D2_SESSION_ID);
    REQUIRE(request->meter_info.meter_id == "METER1");
    REQUIRE(request->meter_info.meter_reading == 4321);
    REQUIRE(request->sa_schedule_tuple_id == 7);
}

SCENARIO("ISO15118-2 EV MeteringReceipt returns to the charge loop it came from") {
    const ev::feedback::Callbacks callbacks{};

    GIVEN("A DC session") {
        PrimedState<ev::d2::state::MeteringReceipt> primed{
            callbacks, ev::EvSessionParams{}, false, seed_contract, make_meter_info(), std::optional<uint8_t>{7}};
        primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK));
        REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);
        REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::CurrentDemand);
    }

    GIVEN("An AC session") {
        PrimedState<ev::d2::state::MeteringReceipt> primed{callbacks,     ac_params(),       false,
                                                           seed_contract, make_meter_info(), std::optional<uint8_t>{7}};
        primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK));
        REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);
        REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::ChargingStatus);
    }
}

SCENARIO("ISO15118-2 EV MeteringReceipt stops the session when the contract key is unusable") {
    const ev::feedback::Callbacks callbacks{};
    const auto seed_bad_key = [](D2StateHelper& helper) { helper.get_context().pnc.contract_key_pem = "not a key"; };
    PrimedState<ev::d2::state::MeteringReceipt> primed{callbacks,    ev::EvSessionParams{}, false,
                                                       seed_bad_key, make_meter_info(),     std::optional<uint8_t>{7}};

    REQUIRE(primed.take_requests().empty());
    REQUIRE(primed.ctx.is_session_stopped() == true);
}

SCENARIO("ISO15118-2 EV MeteringReceipt rejects malformed responses") {
    const ev::feedback::Callbacks callbacks{};
    const auto make_fsm = [](D2StateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.set_session_id(D2_SESSION_ID);
        ctx.pnc.contract_key_pem = make_test_ec_key_pem();
        return fsm::v2::FSM<ev::d2::StateBase>{
            ctx.create_state<ev::d2::state::MeteringReceipt>(make_meter_info(), std::optional<uint8_t>{7})};
    };
    const auto make_ok = [](const message_2::Header& header) { return make_response(header, dt::ResponseCode::OK); };
    message_2::AuthorizationResponse wrong;
    wrong.header = d2_header();
    wrong.response_code = dt::ResponseCode::OK;
    wrong.evse_processing = dt::EVSEProcessing::Finished;
    check_rejection_paths(callbacks, ev::d2::StateID::MeteringReceipt, make_fsm, make_ok, wrong);
}
