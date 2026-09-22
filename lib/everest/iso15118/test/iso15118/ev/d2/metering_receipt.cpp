// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/d2/state/charging_status.hpp>
#include <iso15118/ev/d2/state/current_demand.hpp>
#include <iso15118/message_2/current_demand.hpp>

#include <iso15118/ev/d2/state/metering_receipt.hpp>
#include <iso15118/message_2/authorization.hpp>
#include <iso15118/message_2/charging_status.hpp>
#include <iso15118/message_2/metering_receipt.hpp>
#include <iso15118/message_2/power_delivery.hpp>

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
    pnc.contract.emplace().key_pem = make_test_ec_key_pem();
};

message_2::CurrentDemandResponse receipt_required() {
    auto res = d2_ok<message_2::CurrentDemandResponse>();
    res.dc_evse_status.status_code = dt::DC_EVSEStatusCode::EVSE_Ready;
    res.sa_schedule_tuple_id = 7;
    res.meter_info = make_meter_info();
    res.receipt_required = true;
    return res;
}

const auto by_current_demand = d2_entered_by(
    [](D2StateHelper& helper) { return d2_successor_of<ev::d2::state::CurrentDemand>(helper, receipt_required()); });

message_2::ChargingStatusResponse charging_status_receipt_required() {
    auto res = d2_ok<message_2::ChargingStatusResponse>();
    res.sa_schedule_tuple_id = 7;
    res.meter_info = make_meter_info();
    res.receipt_required = true;
    return res;
}

const auto by_charging_status = d2_entered_by([](D2StateHelper& helper) {
    return d2_successor_of<ev::d2::state::ChargingStatus>(helper, charging_status_receipt_required());
});

// An EV stop latched while the receipt is outstanding goes out right after MeteringReceiptRes.
template <typename Primed> void stops_after_response(Primed& primed) {
    REQUIRE(primed.take_requests().template get<message_2::MeteringReceiptRequest>().has_value());
    primed.ctx.set_stop_charging_requested(true);
    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::PowerDelivery);
    const auto request = primed.take_requests().template get<message_2::PowerDeliveryRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->charge_progress == dt::ChargeProgress::Stop);
    const auto walk = walk_stop(primed);
    REQUIRE(walk.charging_session == dt::ChargingSession::Terminate);
    REQUIRE_FALSE(walk.paused);
}

} // namespace

SCENARIO("ISO15118-2 EV MeteringReceipt's request signs the loop MeterInfo on the CurrentDemand transition") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::MeteringReceipt> primed{callbacks, seed_contract, by_current_demand};

    const auto request = primed.take_requests().get<message_2::MeteringReceiptRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->header.session_id == D2_SESSION_ID);
    REQUIRE(request->session_id == D2_SESSION_ID);
    REQUIRE(request->meter_info.meter_id == "METER1");
    REQUIRE(request->meter_info.meter_reading == 4321);
}

SCENARIO("ISO15118-2 EV MeteringReceipt names the SAScheduleTupleID ChargeParameterDiscovery selected") {
    const ev::feedback::Callbacks callbacks{};
    const auto seed = [](D2StateHelper& helper) {
        seed_contract(helper);
        helper.get_context().set_sa_schedule_tuple_id(4);
    };
    PrimedState<ev::d2::state::MeteringReceipt> primed{callbacks, seed, by_current_demand};

    const auto request = primed.take_requests().get<message_2::MeteringReceiptRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->sa_schedule_tuple_id == 4);
}

SCENARIO("ISO15118-2 EV MeteringReceipt returns to the charge loop it came from") {
    const ev::feedback::Callbacks callbacks{};

    GIVEN("A DC session") {
        PrimedState<ev::d2::state::MeteringReceipt> primed{
            callbacks, ev::params::EvSessionParams{}, false, seed_contract, make_meter_info(), std::nullopt};
        primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK));
        REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);
        REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::CurrentDemand);
    }

    GIVEN("An AC session") {
        PrimedState<ev::d2::state::MeteringReceipt> primed{callbacks,     ac_params(),       false,
                                                           seed_contract, make_meter_info(), std::nullopt};
        primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK));
        REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);
        REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::ChargingStatus);
    }
}

SCENARIO("ISO15118-2 EV MeteringReceipt stops on an EV stop latched during the receipt [V2G2-793] [V2G2-519]") {
    const ev::feedback::Callbacks callbacks{};

    GIVEN("A DC session") {
        PrimedState<ev::d2::state::MeteringReceipt> primed{callbacks, seed_contract, by_current_demand};
        stops_after_response(primed);
    }

    GIVEN("An AC session") {
        PrimedState<ev::d2::state::MeteringReceipt> primed{callbacks, ac_params(), false, seed_contract,
                                                           by_charging_status};
        stops_after_response(primed);
    }
}

SCENARIO("ISO15118-2 EV MeteringReceipt is not entered when the contract key is unusable") {
    const auto seed_bad_key = [](D2StateHelper& helper) {
        auto& pnc = helper.get_context().pnc;
        pnc.contract_selected = true;
        pnc.contract.emplace().key_pem = "not a key";
    };
    auto pred = D2Predecessor::make<ev::d2::state::CurrentDemand>(ev::params::EvSessionParams{}, false, seed_bad_key);
    pred.stops_without_request(receipt_required());
}

SCENARIO("ISO15118-2 EV MeteringReceipt that cannot be signed still sends the pending stop") {
    const auto seed_unusable_key = [](D2StateHelper& helper) {
        auto& pnc = helper.get_context().pnc;
        pnc.contract_selected = true;
        pnc.contract.emplace().key_pem = "not a key";
    };
    const auto stops_cleanly = [](D2Predecessor& pred, const auto& response) {
        pred.ctx.set_stop_charging_requested(true);
        const auto sent = pred.transitions_to(ev::d2::StateID::PowerDelivery, response);
        REQUIRE_FALSE(sent.template get<message_2::MeteringReceiptRequest>().has_value());
        const auto request = sent.template get<message_2::PowerDeliveryRequest>();
        REQUIRE(request.has_value());
        REQUIRE(request->charge_progress == dt::ChargeProgress::Stop);
        REQUIRE_FALSE(pred.ctx.is_session_stopped());
    };

    GIVEN("A DC session") {
        auto pred = D2Predecessor::make<ev::d2::state::CurrentDemand>({}, false, seed_unusable_key);
        stops_cleanly(pred, receipt_required());
    }

    GIVEN("An AC session") {
        auto pred = D2Predecessor::make<ev::d2::state::ChargingStatus>(ac_params(), false, seed_unusable_key);
        stops_cleanly(pred, charging_status_receipt_required());
    }
}

SCENARIO("ISO15118-2 EV MeteringReceipt rejects malformed responses") {
    const ev::feedback::Callbacks callbacks{};
    const auto make_fsm = [](D2StateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.set_session_id(D2_SESSION_ID);
        ctx.pnc.contract.emplace().key_pem = make_test_ec_key_pem();
        return fsm::v2::FSM<ev::d2::StateBase>{
            ctx.create_state<ev::d2::state::MeteringReceipt>(make_meter_info(), std::nullopt)};
    };
    const auto make_ok = [](const message_2::Header& header) { return make_response(header, dt::ResponseCode::OK); };
    message_2::AuthorizationResponse wrong;
    wrong.header = d2_header();
    wrong.response_code = dt::ResponseCode::OK;
    wrong.evse_processing = dt::EVSEProcessing::Finished;
    check_rejection_paths(callbacks, ev::d2::StateID::MeteringReceipt, make_fsm, make_ok, wrong);
}
