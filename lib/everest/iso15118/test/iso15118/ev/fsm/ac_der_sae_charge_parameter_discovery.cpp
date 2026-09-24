// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <vector>

#include "helper.hpp"

#include <iso15118/ev/d20/state/ac_der_sae_charge_parameter_discovery.hpp>
#include <iso15118/ev/der_sae_control_validation.hpp>
#include <iso15118/message/ac_der_sae_charge_parameter_discovery.hpp>
#include <iso15118/message/common_types.hpp>
#include <iso15118/message/schedule_exchange.hpp>
#include <iso15118/message/type.hpp>
#include <iso15118/sae_modes.hpp>

using namespace iso15118;

namespace {

namespace dt = message_20::datatypes;
namespace dt_sae = dt::sae;

using dt::Processing;
using dt::ResponseCode;
using Fn = sae::DerBitMapFunctions;
using sae::sae_function_bit;
using State = ev::d20::state::AC_DER_SAE_ChargeParameterDiscovery;
using Primed = PrimedState<State>;
using CpdRequest = message_20::DER_SAE_AC_ChargeParameterDiscoveryRequest;
using CpdResponse = message_20::DER_SAE_AC_ChargeParameterDiscoveryResponse;

// 2001-09-09 in microseconds; a seconds-based time is six orders of magnitude below.
constexpr std::uint64_t MICROSECOND_EPOCH_FLOOR = 1'000'000'000'000'000ULL;

dt_sae::CurveDataPointsList make_points(std::initializer_list<float> x_values) {
    dt_sae::CurveDataPointsList points;
    for (const auto x : x_values) {
        points.push_back({dt::from_float(x), dt::from_float(1.0f)});
    }
    return points;
}

// Clean control enabling EnterService, VoltVar and VoltWatt.
CpdResponse make_response(const message_20::Header& header, Processing processing) {
    CpdResponse res{};
    res.header = header;
    res.response_code = ResponseCode::OK;
    auto& mode = res.transfer_mode;
    mode.processing = processing;
    mode.max_charge_power = dt::from_float(15000.0f);
    mode.min_charge_power = dt::from_float(500.0f);
    mode.nominal_frequency = dt::from_float(60.0f);
    mode.maximum_discharge_power = dt::from_float(11000.0f);

    auto& control = mode.der_control_cpd_res;
    control.enter_service_cpd_res.permit_service = true;
    control.reactive_power_support_cpd_res.volt_var.enable = true;
    control.reactive_power_support_cpd_res.volt_var.curve_data_points = make_points({92.0f, 108.0f});
    control.active_power_support_cpd_res.volt_watt.enable = true;
    control.active_power_support_cpd_res.volt_watt.curve_data_points = make_points({106.0f, 110.0f});
    return res;
}

CpdResponse make_response(Processing processing) {
    return make_response(SESSION_HEADER, processing);
}

CpdResponse make_invalid_response() {
    auto res = make_response(Processing::Finished);
    res.transfer_mode.der_control_cpd_res.active_power_support_cpd_res.volt_watt.curve_data_points =
        make_points({106.0f});
    return res;
}

ev::d20::SessionOptions sae_options(std::uint16_t cpd_rounds = 1, bool stop_on_invalid = false) {
    ev::d20::SessionOptions options{};
    options.cpd_rounds = cpd_rounds;
    options.der_stop_on_invalid_control = stop_on_invalid;
    return options;
}

const auto seed_ac_params = [](FsmStateHelper& helper) {
    ev::AcChargeParams p{};
    p.phase_count = 1;
    p.max_charge_power = 22000.0f;
    p.min_charge_power = 1000.0f;
    helper.set_ac_params(p);
};

Primed make_primed(const ev::feedback::Callbacks& callbacks, ev::d20::SessionOptions options = sae_options()) {
    return Primed{callbacks, dt::ServiceCategory::AC_DER_SAE, std::move(options), seed_ac_params};
}

std::optional<CpdRequest> take_request(Primed& primed) {
    return primed.take_requests().get<CpdRequest>();
}

// true if the state transitioned.
bool feed_response(Primed& primed, const CpdResponse& response) {
    primed.handle_response(response);
    return primed.feed(ev::d20::Event::V2GTP_MESSAGE).transitioned();
}

void require_awaiting_with(Primed& primed, bool transitioned, Processing expected) {
    REQUIRE_FALSE(transitioned);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::AC_DER_SAE_ChargeParameterDiscovery);
    const auto next = take_request(primed);
    REQUIRE(next.has_value());
    REQUIRE(next->transfer_mode.processing == expected);
}

void require_schedule_exchange(Primed& primed, bool transitioned) {
    REQUIRE(transitioned);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::ScheduleExchange);
    REQUIRE_FALSE(primed.ctx.is_session_stopped());
}

} // namespace

SCENARIO("ISO15118-20 EV AC_DER_SAE_ChargeParameterDiscovery emits the first request on enter") {
    const ev::feedback::Callbacks callbacks{};

    GIVEN("cpd_rounds 1") {
        auto primed = make_primed(callbacks);
        const auto request = take_request(primed);
        REQUIRE(request.has_value());
        const auto& mode = request->transfer_mode;

        THEN("The request carries the profile and SECC time in microseconds, and is Finished") {
            REQUIRE(request->header.session_id == SESSION_HEADER.session_id);
            REQUIRE(mode.processing == Processing::Finished);
            REQUIRE(mode.enabled_modes == 0);
            REQUIRE(mode.supported_modes == primed.ctx.sae_supported_modes());
            REQUIRE(mode.inverter_details.inverter_manufacturer == primed.ctx.sae_profile().inverter_manufacturer);
            REQUIRE(dt::from_RationalNumber(mode.max_charge_power) == Catch::Approx(22000.0f));
            REQUIRE(mode.update_time > MICROSECOND_EPOCH_FLOOR);
            REQUIRE(primed.ctx.cpd_rounds_sent() == 1);
        }
    }

    GIVEN("cpd_rounds 3") {
        auto primed = make_primed(callbacks, sae_options(3));
        const auto request = take_request(primed);
        REQUIRE(request.has_value());

        THEN("The first request is Ongoing") {
            REQUIRE(request->transfer_mode.processing == Processing::Ongoing);
        }
    }
}

SCENARIO("ISO15118-20 EV AC_DER_SAE_ChargeParameterDiscovery stamps EVUpdateTime in SECC time") {
    const ev::feedback::Callbacks callbacks{};
    const auto synchronized = [](FsmStateHelper& helper) {
        seed_ac_params(helper);
        helper.get_context().secc_clock().synchronize(SECC_REFERENCE_US);
    };
    const auto since = std::chrono::steady_clock::now();
    Primed primed{callbacks, dt::ServiceCategory::AC_DER_SAE, sae_options(), synchronized};

    const auto request = take_request(primed);
    REQUIRE(request.has_value());
    require_tracks_reference(request->transfer_mode.update_time, SECC_REFERENCE_US, since);

    (void)feed_response(primed, make_response(Processing::Finished));
    require_tracks_reference(primed.ctx.sae_settings_update_time(), SECC_REFERENCE_US, since);
}

SCENARIO("ISO15118-20 EV AC_DER_SAE_ChargeParameterDiscovery owns the termination of the rounds") {
    const ev::feedback::Callbacks callbacks{};

    GIVEN("cpd_rounds 3 and an SECC reporting Ongoing") {
        auto primed = make_primed(callbacks, sae_options(3));
        REQUIRE(take_request(primed)->transfer_mode.processing == Processing::Ongoing);

        THEN("The EV sends Ongoing, then Finished, then leaves on the SECC's Finished") {
            require_awaiting_with(primed, feed_response(primed, make_response(Processing::Ongoing)),
                                  Processing::Ongoing);
            require_awaiting_with(primed, feed_response(primed, make_response(Processing::Ongoing)),
                                  Processing::Finished);
            require_schedule_exchange(primed, feed_response(primed, make_response(Processing::Finished)));
        }
    }

    GIVEN("cpd_rounds 3 and an SECC reporting Finished early") {
        auto primed = make_primed(callbacks, sae_options(3));
        (void)take_request(primed);

        THEN("The EV warns [V2G20-3352] and goes to ScheduleExchange without another CPDReq [V2G20-3357]") {
            require_schedule_exchange(primed, feed_response(primed, make_response(Processing::Finished)));
            REQUIRE(primed.take_requests().types() == std::vector{message_20::Type::ScheduleExchangeReq});
            REQUIRE(primed.ctx.cpd_rounds_sent() == 1);
        }
    }

    GIVEN("cpd_rounds 1 and an SECC reporting Ongoing") {
        auto primed = make_primed(callbacks);
        (void)take_request(primed);

        THEN("The SECC holds the loop and the EV keeps sending Finished") {
            require_awaiting_with(primed, feed_response(primed, make_response(Processing::Ongoing)),
                                  Processing::Finished);
            require_awaiting_with(primed, feed_response(primed, make_response(Processing::Ongoing)),
                                  Processing::Finished);
            require_schedule_exchange(primed, feed_response(primed, make_response(Processing::Finished)));
        }
    }
}

SCENARIO("ISO15118-20 EV AC_DER_SAE_ChargeParameterDiscovery checks the DER control") {
    int limits_calls = 0;
    ev::DerControlProblems observed{"stale"};
    ev::feedback::Callbacks callbacks{};
    callbacks.sae_cpd_limits = [&](const dt_sae::DER_SAE_AC_CPDResEnergyTransferMode&,
                                   const ev::DerControlProblems& problems) {
        ++limits_calls;
        observed = problems;
    };

    GIVEN("Clean control") {
        auto primed = make_primed(callbacks);
        require_schedule_exchange(primed, feed_response(primed, make_response(Processing::Finished)));

        THEN("The payload is forwarded with no problems") {
            REQUIRE(limits_calls == 1);
            REQUIRE(observed.empty());
        }
    }

    GIVEN("Invalid control and der_stop_on_invalid_control false") {
        auto primed = make_primed(callbacks, sae_options(1, false));
        require_schedule_exchange(primed, feed_response(primed, make_invalid_response()));

        THEN("The session continues and the problems are forwarded") {
            REQUIRE(limits_calls == 1);
            REQUIRE_FALSE(observed.empty());
        }
    }

    GIVEN("Invalid control and der_stop_on_invalid_control true") {
        auto primed = make_primed(callbacks, sae_options(1, true));
        expect_stops_session(primed, make_invalid_response(), ev::d20::StateID::AC_DER_SAE_ChargeParameterDiscovery);

        THEN("The block that ends the session is forwarded with its problems") {
            REQUIRE(limits_calls == 1);
            REQUIRE_FALSE(observed.empty());
        }
    }
}

SCENARIO("ISO15118-20 EV AC_DER_SAE_ChargeParameterDiscovery records the enabled modes") {
    std::optional<std::uint32_t> reported_modes;
    int ac_limits_calls = 0;
    ev::feedback::Callbacks callbacks{};
    callbacks.der_enabled_modes = [&](std::uint32_t modes) { reported_modes = modes; };
    callbacks.ac_limits = [&](const dt::AC_CPDResEnergyTransferMode&) { ++ac_limits_calls; };

    // Supports VoltWatt and EnterService but not VoltVar, which the response also enables.
    auto options = sae_options(2);
    options.sae_profile.supported_modes = sae_function_bit(Fn::ChargeFunction) |
                                          sae_function_bit(Fn::DischargeFunction) | sae_function_bit(Fn::EnterService) |
                                          sae_function_bit(Fn::VoltWattFunction);
    auto primed = make_primed(callbacks, options);
    (void)take_request(primed);

    const auto expected = sae_function_bit(Fn::EnterService) | sae_function_bit(Fn::VoltWattFunction);
    const auto transitioned = feed_response(primed, make_response(Processing::Ongoing));

    THEN("The modes are derived and masked, and the feedback fires") {
        REQUIRE(primed.ctx.sae_enabled_modes() == expected);
        REQUIRE(reported_modes == expected);
        REQUIRE(ac_limits_calls == 1);
        REQUIRE(primed.ctx.sae_permit_service());
        REQUIRE(primed.ctx.sae_settings_update_time() > MICROSECOND_EPOCH_FLOOR);
    }

    THEN("The next request echoes them") {
        REQUIRE_FALSE(transitioned);
        const auto next = take_request(primed);
        REQUIRE(next.has_value());
        REQUIRE(next->transfer_mode.enabled_modes == expected);
    }
}

SCENARIO("ISO15118-20 EV AC_DER_SAE_ChargeParameterDiscovery records a denied PermitService without acting on it") {
    const ev::feedback::Callbacks callbacks{};
    auto options = sae_options();
    options.sae_profile.supported_modes |= sae_function_bit(Fn::EnterService);
    auto primed = make_primed(callbacks, options);
    REQUIRE(primed.ctx.sae_permit_service());

    auto response = make_response(Processing::Finished);
    response.transfer_mode.der_control_cpd_res.enter_service_cpd_res.permit_service = false;

    // [V2G20-3363]: recorded only; acting on it belongs to the ChargeLoop.
    require_schedule_exchange(primed, feed_response(primed, response));
    REQUIRE_FALSE(primed.ctx.sae_permit_service());
    REQUIRE_FALSE(sae::is_function_set(primed.ctx.sae_enabled_modes(), Fn::EnterService));
}

SCENARIO("ISO15118-20 EV AC_DER_SAE_ChargeParameterDiscovery rejects malformed responses") {
    const ev::feedback::Callbacks callbacks{};
    const auto make_fsm = [](FsmStateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.get_session().set_id(SESSION_HEADER.session_id);
        return fsm::v2::FSM<ev::d20::StateBase>{ctx.create_state<State>()};
    };
    const auto make_ok = [](const message_20::Header& header) { return make_response(header, Processing::Finished); };
    check_rejection_paths(callbacks, ev::d20::StateID::AC_DER_SAE_ChargeParameterDiscovery, make_fsm, make_ok,
                          message_20::ScheduleExchangeResponse{});
}
