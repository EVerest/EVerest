// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "helper.hpp"

#include <iso15118/ev/d20/state/power_delivery.hpp>
#include <iso15118/ev/params/ac.hpp>
#include <iso15118/message/ac_charge_loop.hpp>
#include <iso15118/message/authorization.hpp>
#include <iso15118/message/power_delivery.hpp>
#include <iso15118/message/session_stop.hpp>
#include <iso15118/message/type.hpp>

using namespace iso15118;

namespace {
using message_20::datatypes::Progress;
using message_20::datatypes::ResponseCode;
using message_20::datatypes::ServiceCategory;
using PowerDelivery = ev::d20::state::PowerDelivery;

message_20::PowerDeliveryResponse make_pd_res(const message_20::Header& header, ResponseCode code) {
    return message_20::PowerDeliveryResponse{header, code, std::nullopt};
}

// The PowerDeliveryReq a predecessor sends as it enters PowerDelivery with `progress`.
template <typename Seed>
DecodedRequests sent_entering(ServiceCategory service, Seed seed, PowerDelivery::ChargeProgress progress) {
    const ev::feedback::Callbacks callbacks{};
    FsmStateHelper helper{callbacks, DEFAULT_APP_PROTOCOLS, service};
    helper.get_context().get_session().set_id(SESSION_HEADER.session_id);
    seed(helper);
    const auto state = ev::d20::state::to_power_delivery(helper.get_context(), progress);
    REQUIRE(state->get_id() == ev::d20::StateID::PowerDelivery);
    state->enter();
    auto sent = take_all_requests(helper.get_message_exchange());
    REQUIRE(sent.types() == std::vector{message_20::Type::PowerDeliveryReq});
    return sent;
}
} // namespace

SCENARIO("ISO15118-20 EV PowerDelivery is entered with a Finished request carrying the chosen progress") {
    const auto requests = sent_entering(ServiceCategory::DC, no_seed, PowerDelivery::Start{});
    const auto request_message = requests.get<message_20::PowerDeliveryRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->header.session_id == SESSION_HEADER.session_id);
    REQUIRE(request_message->processing == message_20::datatypes::Processing::Finished);
    REQUIRE(request_message->charge_progress == Progress::Start);
    // [V2G20-1546]: in Dynamic mode too.
    REQUIRE(request_message->power_profile.has_value());
    REQUIRE(std::holds_alternative<message_20::datatypes::Dynamic_EVPPTControlMode>(
        request_message->power_profile->control_mode));
    REQUIRE_FALSE(request_message->channel_selection.has_value());
}

SCENARIO("ISO15118-20 EV PowerDelivery transitions to DC_ChargeLoop on OK response") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<PowerDelivery> primed{callbacks, no_seed, PowerDelivery::Start{}};

    primed.handle_response(make_pd_res(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::DC_ChargeLoop);
    REQUIRE(primed.ctx.is_session_stopped() == false);
}

SCENARIO("ISO15118-20 EV PowerDelivery transitions to DC_WeldingDetection on Stop") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<PowerDelivery> primed{callbacks, no_seed, PowerDelivery::Stop{ev::session::StopReason::EvStop}};

    primed.handle_response(make_pd_res(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::DC_WeldingDetection);
    REQUIRE(primed.ctx.is_session_stopped() == false);
}

SCENARIO("ISO15118-20 EV PowerDelivery transitions to DC_ChargeLoop on OK response for DC_BPT") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<PowerDelivery> primed{callbacks, message_20::datatypes::ServiceCategory::DC_BPT, no_seed,
                                      PowerDelivery::Start{}};

    primed.handle_response(make_pd_res(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::DC_ChargeLoop);
    REQUIRE(primed.ctx.is_session_stopped() == false);
}

SCENARIO("ISO15118-20 EV PowerDelivery transitions to DC_WeldingDetection on Stop for DC_BPT") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<PowerDelivery> primed{callbacks, message_20::datatypes::ServiceCategory::DC_BPT, no_seed,
                                      PowerDelivery::Stop{ev::session::StopReason::EvStop}};

    primed.handle_response(make_pd_res(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::DC_WeldingDetection);
    REQUIRE(primed.ctx.is_session_stopped() == false);
}

SCENARIO("ISO15118-20 EV PowerDelivery transitions to AC_ChargeLoop on OK response for AC") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<PowerDelivery> primed{callbacks, ServiceCategory::AC, no_seed, PowerDelivery::Start{}};

    primed.handle_response(make_pd_res(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::AC_ChargeLoop);
    REQUIRE(primed.ctx.is_session_stopped() == false);
}

SCENARIO("ISO15118-20 EV PowerDelivery transitions to SessionStop on Stop for AC") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<PowerDelivery> primed{callbacks, ServiceCategory::AC, no_seed,
                                      PowerDelivery::Stop{ev::session::StopReason::EvStop}};

    primed.handle_response(make_pd_res(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::SessionStop);
    REQUIRE(primed.ctx.is_session_stopped() == false);
}

// AC_BPT shares the AC charge loop; it must not fall onto the DC path.
SCENARIO("ISO15118-20 EV PowerDelivery transitions to AC_ChargeLoop on OK response for AC_BPT") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<PowerDelivery> primed{callbacks, ServiceCategory::AC_BPT, no_seed, PowerDelivery::Start{}};

    primed.handle_response(make_pd_res(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::AC_ChargeLoop);
    REQUIRE(primed.ctx.is_session_stopped() == false);
}

SCENARIO("ISO15118-20 EV PowerDelivery transitions to SessionStop on Stop for AC_BPT") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<PowerDelivery> primed{callbacks, ServiceCategory::AC_BPT, no_seed,
                                      PowerDelivery::Stop{ev::session::StopReason::EvStop}};

    primed.handle_response(make_pd_res(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::SessionStop);
    REQUIRE(primed.ctx.is_session_stopped() == false);
}

SCENARIO("ISO15118-20 EV PowerDelivery transitions to AC_DER_IEC_ChargeLoop on OK for AC_DER_IEC") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<PowerDelivery> primed{callbacks, ServiceCategory::AC_DER_IEC, no_seed, PowerDelivery::Start{}};

    primed.handle_response(make_pd_res(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::AC_DER_IEC_ChargeLoop);
    REQUIRE(primed.ctx.is_session_stopped() == false);
}

SCENARIO("ISO15118-20 EV PowerDelivery transitions to AC_DER_SAE_ChargeLoop on OK for AC_DER_SAE") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<PowerDelivery> primed{callbacks, ServiceCategory::AC_DER_SAE, no_seed, PowerDelivery::Start{}};

    primed.handle_response(make_pd_res(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::AC_DER_SAE_ChargeLoop);
    REQUIRE(primed.ctx.is_session_stopped() == false);
}

// ServiceSelection stops these first; PowerDelivery must not pick a loop for one either.
SCENARIO("ISO15118-20 EV PowerDelivery stops the session on Start for an unsupported service category") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<PowerDelivery> primed{callbacks, ServiceCategory::WPT, no_seed, PowerDelivery::Start{}};

    expect_stops_session(primed, make_pd_res(SESSION_HEADER, ResponseCode::OK), ev::d20::StateID::PowerDelivery);
}

SCENARIO("ISO15118-20 EV PowerDelivery transitions to SessionStop on Stop for AC_DER_SAE") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<PowerDelivery> primed{callbacks, ServiceCategory::AC_DER_SAE, no_seed,
                                      PowerDelivery::Stop{ev::session::StopReason::EvStop}};

    primed.handle_response(make_pd_res(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::SessionStop);
    REQUIRE(primed.ctx.is_session_stopped() == false);
}

SCENARIO("ISO15118-20 EV PowerDelivery carries an AC_DER_SAE pause into SessionStop") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<PowerDelivery> primed{callbacks, ServiceCategory::AC_DER_SAE, no_seed,
                                      PowerDelivery::Stop{ev::session::StopReason::EvsePause}};

    primed.handle_response(make_pd_res(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::SessionStop);
    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::SessionStopRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->charging_session == message_20::datatypes::ChargingSession::Pause);
}

SCENARIO("ISO15118-20 EV PowerDelivery accepts OK_PowerToleranceConfirmed") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<PowerDelivery> primed{callbacks, no_seed, PowerDelivery::Start{}};

    primed.handle_response(make_pd_res(SESSION_HEADER, ResponseCode::OK_PowerToleranceConfirmed));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::DC_ChargeLoop);
    REQUIRE(primed.ctx.is_session_stopped() == false);
}

SCENARIO("ISO15118-20 EV PowerDelivery stops the session on an accepted Standby it cannot drive") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<PowerDelivery> primed{callbacks, no_seed, PowerDelivery::Standby{}};

    primed.handle_response(make_pd_res(SESSION_HEADER, ResponseCode::WARNING_StandbyNotAllowed));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    // The response is accepted, but the EV drives no standby loop, so it emits nothing and there
    // is nothing left to advance the session. It stops here rather than parking silently. This
    // was already the end-to-end outcome via Session's consumed-but-idle catch-all; the state now
    // says so itself, which is what makes it visible without running a session.
    REQUIRE(result.transitioned() == false);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::PowerDelivery);
    REQUIRE(primed.ctx.is_session_stopped());
}

SCENARIO("ISO15118-20 EV PowerDelivery stops session on FAILED_ContactorError") {
    // State-specific rejection beyond the shared triple.
    const ev::feedback::Callbacks callbacks{};
    PrimedState<PowerDelivery> primed{callbacks, no_seed, PowerDelivery::Start{}};

    expect_stops_session(primed, make_pd_res(SESSION_HEADER, ResponseCode::FAILED_ContactorError),
                         ev::d20::StateID::PowerDelivery);
}

SCENARIO("ISO15118-20 EV PowerDelivery rejects malformed responses") {
    const ev::feedback::Callbacks callbacks{};
    const auto make_fsm = [](FsmStateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.get_session().set_id(SESSION_HEADER.session_id);
        return fsm::v2::FSM<ev::d20::StateBase>{ctx.create_state<PowerDelivery>(PowerDelivery::Start{})};
    };
    const auto make_ok = [](const message_20::Header& header) { return make_pd_res(header, ResponseCode::OK); };
    const auto wrong = message_20::AuthorizationResponse{SESSION_HEADER, ResponseCode::OK,
                                                         message_20::datatypes::Processing::Finished};
    check_rejection_paths(callbacks, ev::d20::StateID::PowerDelivery, make_fsm, make_ok, wrong);
}

namespace {
using message_20::datatypes::ControlMode;

const auto seed_scheduled_without_tuple = [](FsmStateHelper& helper) {
    ev::params::DcChargeParams params{};
    params.max_charge_power = 11000.0f;
    helper.set_dc_params(params);
    helper.get_context().set_selected_control_mode(ControlMode::Scheduled);
};

// With the tuple ScheduleExchange took from the SECC's offer.
const auto seed_scheduled = [](FsmStateHelper& helper) {
    seed_scheduled_without_tuple(helper);
    helper.get_context().set_schedule_tuple_id(std::uint8_t{3});
};
} // namespace

SCENARIO("ISO15118-20 EV PowerDelivery carries the selected ScheduleTupleID on Start in Scheduled mode") {
    const auto requests = sent_entering(ServiceCategory::DC, seed_scheduled, PowerDelivery::Start{});
    const auto request_message = requests.get<message_20::PowerDeliveryRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->power_profile.has_value());
    const auto& profile = request_message->power_profile.value();
    REQUIRE(std::holds_alternative<message_20::datatypes::Scheduled_EVPPTControlMode>(profile.control_mode));
    REQUIRE(std::get<message_20::datatypes::Scheduled_EVPPTControlMode>(profile.control_mode).selected_schedule == 3);
    REQUIRE(profile.entries.size() == 1);
    REQUIRE(message_20::datatypes::from_RationalNumber(profile.entries[0].power) == 11000.0f);
}

SCENARIO("ISO15118-20 EV PowerDelivery anchors the power profile in SECC time") {
    const auto synchronized = [](FsmStateHelper& helper) {
        seed_scheduled(helper);
        helper.get_context().secc_clock().synchronize(SECC_REFERENCE_US);
    };
    const auto since = std::chrono::steady_clock::now();
    const auto request_message = sent_entering(ServiceCategory::DC, synchronized, PowerDelivery::Start{})
                                     .get<message_20::PowerDeliveryRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->power_profile.has_value());
    // Table 103: microseconds of SECC time, the present.
    const auto anchor = request_message->power_profile->time_anchor;
    require_tracks_reference(anchor, SECC_REFERENCE_US, since);
    REQUIRE(anchor <= request_message->header.timestamp);
}

namespace {
// The profile a PowerDeliveryReq sent entering with `progress` states, in `mode`, with its one entry.
template <typename Seed>
message_20::datatypes::PowerScheduleEntry stated_entry(ServiceCategory service, Seed seed,
                                                       PowerDelivery::ChargeProgress progress, ControlMode mode) {
    const DecodedRequests requests = sent_entering(service, seed, progress);
    const auto request_message = requests.get<message_20::PowerDeliveryRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->power_profile.has_value());
    const auto& profile = request_message->power_profile.value();
    if (mode == ControlMode::Scheduled) {
        const auto* scheduled = std::get_if<message_20::datatypes::Scheduled_EVPPTControlMode>(&profile.control_mode);
        REQUIRE(scheduled != nullptr);
        REQUIRE(scheduled->selected_schedule == 3);
    } else {
        REQUIRE(std::holds_alternative<message_20::datatypes::Dynamic_EVPPTControlMode>(profile.control_mode));
    }
    REQUIRE(profile.entries.size() == 1);
    return profile.entries[0];
}

const auto seed_dynamic = [](FsmStateHelper& helper) {
    ev::params::DcChargeParams params{};
    params.max_charge_power = 11000.0f;
    helper.set_dc_params(params);
    helper.get_context().set_selected_control_mode(ControlMode::Dynamic);
};

} // namespace

SCENARIO("ISO15118-20 EV PowerDelivery predicts the maximum power on Start in Dynamic mode") {
    const auto entry = stated_entry(ServiceCategory::DC, seed_dynamic, PowerDelivery::Start{}, ControlMode::Dynamic);
    REQUIRE(message_20::datatypes::from_RationalNumber(entry.power) == 11000.0f);
}

// [V2G20-1546], [V2G20-1839]: leaving the loop is a zero power period in either control mode.
SCENARIO("ISO15118-20 EV PowerDelivery states a zero power profile on Stop and Standby") {
    const auto progress = GENERATE(as<PowerDelivery::ChargeProgress>{},
                                   PowerDelivery::Stop{ev::session::StopReason::EvStop}, PowerDelivery::Standby{});
    const auto scheduled = GENERATE(true, false);
    const auto entry = scheduled ? stated_entry(ServiceCategory::DC, seed_scheduled, progress, ControlMode::Scheduled)
                                 : stated_entry(ServiceCategory::DC, seed_dynamic, progress, ControlMode::Dynamic);
    REQUIRE(message_20::datatypes::from_RationalNumber(entry.power) == 0.0f);
    REQUIRE_FALSE(entry.power_l2.has_value());
    REQUIRE_FALSE(entry.power_l3.has_value());
}

SCENARIO("ISO15118-20 EV PowerDelivery anchors the zero power profile in SECC time") {
    const auto synchronized = [](FsmStateHelper& helper) {
        seed_scheduled(helper);
        helper.get_context().secc_clock().synchronize(SECC_REFERENCE_US);
    };
    const auto since = std::chrono::steady_clock::now();
    const auto request_message =
        sent_entering(ServiceCategory::DC, synchronized, PowerDelivery::Stop{ev::session::StopReason::EvStop})
            .get<message_20::PowerDeliveryRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->power_profile.has_value());
    require_tracks_reference(request_message->power_profile->time_anchor, SECC_REFERENCE_US, since);
}

SCENARIO("ISO15118-20 EV PowerDelivery omits the power profile when no ScheduleTupleID was selected") {
    const auto requests = sent_entering(ServiceCategory::DC, seed_scheduled_without_tuple, PowerDelivery::Start{});
    const auto request_message = requests.get<message_20::PowerDeliveryRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE_FALSE(request_message->power_profile.has_value());
}

namespace {
using message_20::datatypes::AcConnector;

// A three-line EV advertising 12 kW in total, on the connector the SECC offered. The total divides
// evenly by three so the assertions are exact through RationalNumber's two-significant-digit
// exponent quantisation.
auto seed_scheduled_ac(AcConnector connector) {
    return [connector](FsmStateHelper& helper) {
        ev::params::AcChargeParams params{};
        params.phase_count = 3;
        params.max_charge_power = 12000.0f;
        helper.set_ac_params(params);
        helper.get_context().set_selected_control_mode(ControlMode::Scheduled);
        helper.get_context().set_selected_ac_connector(connector);
        helper.get_context().set_schedule_tuple_id(std::uint8_t{3});
    };
}
} // namespace

SCENARIO("ISO15118-20 EV PowerDelivery states the Scheduled AC power profile for the connector") {
    const auto requests =
        sent_entering(ServiceCategory::AC, seed_scheduled_ac(AcConnector::ThreePhase), PowerDelivery::Start{});
    const auto request_message = requests.get<message_20::PowerDeliveryRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->power_profile.has_value());
    const auto& entry = request_message->power_profile.value().entries.at(0);

    // [V2G20-1820]: the profile states the same three-phase total AC_ChargeParameterDiscoveryReq
    // advertised, on the base element alone.
    REQUIRE(message_20::datatypes::from_RationalNumber(entry.power) == 12000.0f);
    REQUIRE_FALSE(entry.power_l2.has_value());
    REQUIRE_FALSE(entry.power_l3.has_value());
}

SCENARIO("ISO15118-20 EV PowerDelivery declares one line's share on a SinglePhase connector") {
    const auto requests =
        sent_entering(ServiceCategory::AC, seed_scheduled_ac(AcConnector::SinglePhase), PowerDelivery::Start{});
    const auto request_message = requests.get<message_20::PowerDeliveryRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->power_profile.has_value());
    const auto& entry = request_message->power_profile.value().entries.at(0);

    // The regression: a three-line EV on a one-line connector used to declare the full 12 kW here
    // while AC_ChargeParameterDiscoveryReq advertised 12000/3 on that same line.
    REQUIRE(message_20::datatypes::from_RationalNumber(entry.power) == 4000.0f);
    REQUIRE_FALSE(entry.power_l2.has_value());
    REQUIRE_FALSE(entry.power_l3.has_value());
}

SCENARIO("ISO15118-20 EV PowerDelivery states the AC profile for the connector in either control mode") {
    const auto mode = GENERATE(ControlMode::Scheduled, ControlMode::Dynamic);
    const auto seed = [mode](FsmStateHelper& helper) {
        seed_scheduled_ac(AcConnector::SinglePhase)(helper);
        helper.get_context().set_selected_control_mode(mode);
        helper.get_context().set_schedule_tuple_id(std::uint8_t{3});
    };

    const auto start = stated_entry(ServiceCategory::AC, seed, PowerDelivery::Start{}, mode);
    REQUIRE(message_20::datatypes::from_RationalNumber(start.power) == 4000.0f);
    REQUIRE_FALSE(start.power_l2.has_value());
    REQUIRE_FALSE(start.power_l3.has_value());

    const auto progress = GENERATE(as<PowerDelivery::ChargeProgress>{},
                                   PowerDelivery::Stop{ev::session::StopReason::EvStop}, PowerDelivery::Standby{});
    const auto zero = stated_entry(ServiceCategory::AC, seed, progress, mode);
    REQUIRE(message_20::datatypes::from_RationalNumber(zero.power) == 0.0f);
    REQUIRE_FALSE(zero.power_l2.has_value());
    REQUIRE_FALSE(zero.power_l3.has_value());
}

// A single-line EV on a ThreePhase connector states L2 and L3; at zero power all three are zero.
SCENARIO("ISO15118-20 EV PowerDelivery zeroes every line of a split AC profile on Stop") {
    const auto seed = [](FsmStateHelper& helper) {
        ev::params::AcChargeParams params{};
        params.phase_count = 1;
        params.max_charge_power = 7000.0f;
        helper.set_ac_params(params);
        helper.get_context().set_selected_control_mode(ControlMode::Scheduled);
        helper.get_context().set_selected_ac_connector(AcConnector::ThreePhase);
        helper.get_context().set_schedule_tuple_id(std::uint8_t{3});
    };
    const auto entry = stated_entry(ServiceCategory::AC, seed, PowerDelivery::Stop{ev::session::StopReason::EvStop},
                                    ControlMode::Scheduled);
    REQUIRE(message_20::datatypes::from_RationalNumber(entry.power) == 0.0f);
    REQUIRE(entry.power_l2.has_value());
    REQUIRE(message_20::datatypes::from_RationalNumber(*entry.power_l2) == 0.0f);
    REQUIRE(entry.power_l3.has_value());
    REQUIRE(message_20::datatypes::from_RationalNumber(*entry.power_l3) == 0.0f);
}

// [V2G20-1839], [V2G20-1197]: the PowerDelivery(Stop) ahead of SessionStop(Pause) applies a 0 W entry
// under the ScheduleTupleID Start named.
SCENARIO("ISO15118-20 EV pauses a Scheduled session behind a zero power profile") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<PowerDelivery> primed{callbacks, ServiceCategory::AC, seed_scheduled_ac(AcConnector::ThreePhase),
                                      entered_by([](FsmStateHelper& helper) {
                                          return ev::d20::state::to_power_delivery(helper.get_context(),
                                                                                   PowerDelivery::Start{});
                                      })};
    REQUIRE(primed.take_requests().get<message_20::PowerDeliveryRequest>()->charge_progress == Progress::Start);

    primed.handle_response(make_pd_res(SESSION_HEADER, ResponseCode::OK));
    REQUIRE(primed.feed(ev::d20::Event::V2GTP_MESSAGE).transitioned());
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::AC_ChargeLoop);

    primed.ctx.set_pause_charging_requested(true);
    message_20::AC_ChargeLoopResponse loop_res{};
    loop_res.header = SESSION_HEADER;
    loop_res.response_code = ResponseCode::OK;
    loop_res.control_mode = message_20::datatypes::Scheduled_AC_CLResControlMode{};
    primed.handle_response(loop_res);
    REQUIRE(primed.feed(ev::d20::Event::V2GTP_MESSAGE).transitioned());

    const auto stop = primed.take_requests().get<message_20::PowerDeliveryRequest>();
    REQUIRE(stop.has_value());
    REQUIRE(stop->charge_progress == Progress::Stop);
    REQUIRE(stop->power_profile.has_value());
    const auto* mode =
        std::get_if<message_20::datatypes::Scheduled_EVPPTControlMode>(&stop->power_profile->control_mode);
    REQUIRE(mode != nullptr);
    REQUIRE(mode->selected_schedule == 3);
    REQUIRE(stop->power_profile->entries.size() == 1);
    REQUIRE(message_20::datatypes::from_RationalNumber(stop->power_profile->entries[0].power) == 0.0f);

    const auto walk = walk_stop(primed);
    REQUIRE(walk.charging_session == message_20::datatypes::ChargingSession::Pause);
    REQUIRE(walk.paused);
}

SCENARIO("ISO15118-20 EV PowerDelivery signals an accepted DC-family Stop before welding detection") {
    const auto service = GENERATE(ServiceCategory::DC, ServiceCategory::AC, ServiceCategory::DC_BPT,
                                  ServiceCategory::AC_BPT, ServiceCategory::AC_DER_IEC, ServiceCategory::AC_DER_SAE,
                                  ServiceCategory::MCS, ServiceCategory::MCS_BPT);
    const auto progress = GENERATE(as<PowerDelivery::ChargeProgress>{}, PowerDelivery::Start{},
                                   PowerDelivery::Stop{ev::session::StopReason::EvStop},
                                   PowerDelivery::ScheduleRenegotiation{}, PowerDelivery::Standby{});
    int stopped = 0;
    ev::feedback::Callbacks callbacks{};
    FsmStateHelper* helper = nullptr;
    callbacks.power_delivery_stopped = [&] {
        ++stopped;
        REQUIRE(helper != nullptr);
        CHECK_FALSE(helper->get_message_exchange().has_request());
    };
    PrimedState<PowerDelivery> primed{callbacks, service, no_seed, progress};
    helper = &primed.helper;
    REQUIRE(stopped == 0);
    primed.handle_response(make_pd_res(SESSION_HEADER, ResponseCode::OK));
    primed.feed(ev::d20::Event::V2GTP_MESSAGE);
    const bool dc_family = service == ServiceCategory::DC or service == ServiceCategory::DC_BPT or
                           service == ServiceCategory::MCS or service == ServiceCategory::MCS_BPT;
    const bool dc_stop = std::holds_alternative<PowerDelivery::Stop>(progress) and dc_family;
    REQUIRE(stopped == (dc_stop ? 1 : 0));
    if (dc_stop) {
        CHECK(primed.take_requests().types() == std::vector{message_20::Type::DC_WeldingDetectionReq});
    }
}
