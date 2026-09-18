// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/d2/state/power_delivery.hpp>
#include <iso15118/message_2/authorization.hpp>
#include <iso15118/message_2/power_delivery.hpp>

using namespace iso15118;
namespace dt = message_2::datatypes;
using Phase = ev::d2::state::PowerDelivery::Phase;

namespace {

message_2::PowerDeliveryResponse make_response(const message_2::Header& header, dt::ResponseCode code) {
    message_2::PowerDeliveryResponse res;
    res.header = header;
    res.response_code = code;
    return res;
}

const auto seed_schedule = [](D2StateHelper& helper) {
    auto& info = helper.get_context().evse_info;
    info.sa_schedule_tuple_id = 7;
    info.selected_pmax_schedule.push_back(
        dt::PMaxScheduleEntry{0, std::nullopt, dt::to_physical_value(11000.0, dt::Unit::W)});
    info.selected_pmax_schedule.push_back(
        dt::PMaxScheduleEntry{1800, std::nullopt, dt::to_physical_value(7000.0, dt::Unit::W)});
};

} // namespace

SCENARIO("ISO15118-2 EV PowerDelivery(Start) carries the schedule-derived ChargingProfile") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::PowerDelivery> primed{callbacks, ev::EvSessionParams{}, false, seed_schedule,
                                                     Phase::Start};

    const auto request = primed.take_requests().get<message_2::PowerDeliveryRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->charge_progress == dt::ChargeProgress::Start);
    REQUIRE(request->sa_schedule_tuple_id == 7);
    REQUIRE(request->charging_profile.has_value());
    REQUIRE(request->charging_profile->profile_entry.size() == 2);
    REQUIRE(request->charging_profile->profile_entry.at(1).start == 1800);
    REQUIRE(request->dc_ev_power_delivery_parameter.has_value());
    REQUIRE(request->dc_ev_power_delivery_parameter->charging_complete == false);
    REQUIRE(request->dc_ev_power_delivery_parameter->dc_ev_status.ev_ready == true);
}

SCENARIO("ISO15118-2 EV PowerDelivery(Start) falls back to a flat profile without a schedule") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::PowerDelivery> primed{callbacks, d2_no_seed, Phase::Start};

    const auto request = primed.take_requests().get<message_2::PowerDeliveryRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->charging_profile->profile_entry.size() == 1);
    REQUIRE(dt::from_physical_value(request->charging_profile->profile_entry.front().max_power) == 60000.0);
}

SCENARIO("ISO15118-2 EV PowerDelivery reports ChargingComplete at full SoC") {
    const ev::feedback::Callbacks callbacks{};
    const auto seed_full = [](D2StateHelper& helper) {
        auto params = default_dc_params();
        params.present_soc = 100.0;
        helper.set_dc_params(params);
    };
    PrimedState<ev::d2::state::PowerDelivery> primed{callbacks, ev::EvSessionParams{}, false, seed_full, Phase::Start};

    const auto request = primed.take_requests().get<message_2::PowerDeliveryRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->dc_ev_power_delivery_parameter->charging_complete == true);
}

SCENARIO("ISO15118-2 EV PowerDelivery(Stop) announces no ChargingProfile") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::PowerDelivery> primed{callbacks, d2_no_seed, Phase::Stop};

    const auto request = primed.take_requests().get<message_2::PowerDeliveryRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->charge_progress == dt::ChargeProgress::Stop);
    REQUIRE_FALSE(request->charging_profile.has_value());
    REQUIRE(request->dc_ev_power_delivery_parameter->dc_ev_status.ev_ready == false);
}

SCENARIO("ISO15118-2 EV PowerDelivery(Start) enters the matching charge loop") {
    const ev::feedback::Callbacks callbacks{};

    GIVEN("A DC session") {
        PrimedState<ev::d2::state::PowerDelivery> primed{callbacks, d2_no_seed, Phase::Start};
        primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK));
        REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);
        REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::CurrentDemand);
    }

    GIVEN("An AC session") {
        PrimedState<ev::d2::state::PowerDelivery> primed{callbacks, ac_params(), false, d2_no_seed, Phase::Start};
        primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK));
        REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);
        REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::ChargingStatus);
    }
}

SCENARIO("ISO15118-2 EV PowerDelivery(Stop) tears the session down per mode") {
    const ev::feedback::Callbacks callbacks{};

    GIVEN("A DC session") {
        PrimedState<ev::d2::state::PowerDelivery> primed{callbacks, d2_no_seed, Phase::Stop};
        primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK));
        REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);
        REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::WeldingDetection);
    }

    GIVEN("An AC session") {
        PrimedState<ev::d2::state::PowerDelivery> primed{callbacks, ac_params(), false, d2_no_seed, Phase::Stop};
        primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK));
        REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);
        REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::SessionStop);
    }
}

SCENARIO("ISO15118-2 EV PowerDelivery(Renegotiate) returns to ChargeParameterDiscovery") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::PowerDelivery> primed{callbacks, d2_no_seed, Phase::Renegotiate};

    const auto request = primed.take_requests().get<message_2::PowerDeliveryRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->charge_progress == dt::ChargeProgress::Renegotiate);

    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::ChargeParameterDiscovery);
}

SCENARIO("ISO15118-2 EV PowerDelivery rejects malformed responses") {
    const ev::feedback::Callbacks callbacks{};
    const auto make_fsm = [](D2StateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.set_session_id(D2_SESSION_ID);
        return fsm::v2::FSM<ev::d2::StateBase>{ctx.create_state<ev::d2::state::PowerDelivery>(Phase::Start)};
    };
    const auto make_ok = [](const message_2::Header& header) { return make_response(header, dt::ResponseCode::OK); };
    message_2::AuthorizationResponse wrong;
    wrong.header = d2_header();
    wrong.response_code = dt::ResponseCode::OK;
    wrong.evse_processing = dt::EVSEProcessing::Finished;
    check_rejection_paths(callbacks, ev::d2::StateID::PowerDelivery, make_fsm, make_ok, wrong);
}
