// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "helper.hpp"

#include <iso15118/d20/ac_powers.hpp>
#include <iso15118/ev/d20/state/ac_der_sae_charge_loop.hpp>
#include <iso15118/ev/d20/state/power_delivery.hpp>
#include <iso15118/ev/der_sae_control_validation.hpp>
#include <iso15118/io/logging.hpp>
#include <iso15118/message/ac_der_sae_charge_loop.hpp>
#include <iso15118/message/authorization.hpp>
#include <iso15118/message/common_types.hpp>
#include <iso15118/message/power_delivery.hpp>
#include <iso15118/message/type.hpp>
#include <iso15118/sae_modes.hpp>

using namespace iso15118;

namespace {

namespace dt = message_20::datatypes;
namespace dt_sae = dt::sae;

using dt::ResponseCode;
using Fn = sae::DerBitMapFunctions;
using sae::sae_function_bit;
using State = ev::d20::state::AC_DER_SAE_ChargeLoop;
using Primed = PrimedState<State>;
using ClRequest = message_20::DER_SAE_AC_ChargeLoopRequest;
using ClResponse = message_20::DER_SAE_AC_ChargeLoopResponse;

// 2001-09-09 in microseconds; a seconds-based time is nine orders of magnitude below.
constexpr std::uint64_t MICROSECOND_EPOCH_FLOOR = 1'000'000'000'000'000ULL;
constexpr std::uint64_t CPD_UPDATE_TIME = 1234;

const auto DROOP_BIT = sae_function_bit(Fn::FrequencyDroopFunction);
const auto VOLT_WATT_BIT = sae_function_bit(Fn::VoltWattFunction);

// Clean Dynamic response: nothing repeated, service permitted.
ClResponse make_res(const message_20::Header& header, ResponseCode code,
                    std::optional<dt::EvseStatus> status = std::nullopt) {
    ClResponse res;
    res.header = header;
    res.response_code = code;
    res.status = status;
    dt_sae::DER_Dynamic_AC_CLResControlMode mode{};
    mode.der_control_cl_res.enter_service_cl_res.permit_service = true;
    res.control_mode = mode;
    return res;
}

ClResponse make_ok() {
    return make_res(SESSION_HEADER, ResponseCode::OK);
}

dt_sae::DERControlCLRes& control_of(ClResponse& res) {
    return std::get<dt_sae::DER_Dynamic_AC_CLResControlMode>(res.control_mode).der_control_cl_res;
}

// Enables frequency droop, and volt watt with a valid curve.
ClResponse make_droop_and_volt_watt() {
    auto res = make_ok();
    dt_sae::ActivePowerSupportCLRes active{};
    active.frequency_droop = dt_sae::FrequencyDroop{};
    active.frequency_droop->enable = true;
    dt_sae::VoltWatt volt_watt{};
    volt_watt.enable = true;
    volt_watt.curve_data_points.push_back({dt::from_float(100.0f), dt::from_float(100.0f)});
    volt_watt.curve_data_points.push_back({dt::from_float(110.0f), dt::from_float(20.0f)});
    active.volt_watt = volt_watt;
    control_of(res).active_power_support_cl_res = active;
    return res;
}

// An enabled volt watt curve with a single point is invalid.
ClResponse make_invalid() {
    auto res = make_droop_and_volt_watt();
    control_of(res).active_power_support_cl_res->volt_watt->curve_data_points.pop_back();
    return res;
}

// Discharge limits differ from the charge limits so a swap fails.
const auto seed_present_5000 = [](FsmStateHelper& helper) {
    ev::AcChargeParams p{};
    p.phase_count = 1;
    p.max_charge_power = 11000.0f;
    p.min_charge_power = 1000.0f;
    p.max_discharge_power = 9000.0f;
    p.min_discharge_power = 800.0f;
    p.present_active_power = 5000.0f;
    helper.set_ac_params(p);
    helper.get_context().set_sae_settings_update_time(CPD_UPDATE_TIME);
};

ev::d20::SessionOptions sae_options(std::uint32_t extra_supported = 0, bool stop_on_invalid = false) {
    ev::d20::SessionOptions options{};
    options.sae_profile.supported_modes |= extra_supported;
    options.der_stop_on_invalid_control = stop_on_invalid;
    return options;
}

template <typename Seed = decltype(seed_present_5000)>
Primed make_primed(const ev::feedback::Callbacks& callbacks, ev::d20::SessionOptions options = sae_options(),
                   Seed seed = seed_present_5000) {
    return Primed{callbacks, dt::ServiceCategory::AC_DER_SAE, std::move(options), seed};
}

dt_sae::DER_Dynamic_AC_CLReqControlMode take_mode(Primed& primed) {
    const auto request = primed.take_requests().get<ClRequest>();
    REQUIRE(request.has_value());
    REQUIRE(std::holds_alternative<dt_sae::DER_Dynamic_AC_CLReqControlMode>(request->control_mode));
    return std::get<dt_sae::DER_Dynamic_AC_CLReqControlMode>(request->control_mode);
}

// Feed a response the state must stay on, and return the request it sends next.
dt_sae::DER_Dynamic_AC_CLReqControlMode stay_on(Primed& primed, const ClResponse& res) {
    primed.handle_response(res);
    REQUIRE_FALSE(primed.feed(ev::d20::Event::V2GTP_MESSAGE).transitioned());
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::AC_DER_SAE_ChargeLoop);
    REQUIRE_FALSE(primed.ctx.is_session_stopped());
    return take_mode(primed);
}

void require_power_delivery_stop(Primed& primed, bool transitioned) {
    REQUIRE(transitioned);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::PowerDelivery);
    REQUIRE_FALSE(primed.ctx.is_session_stopped());
    const auto pd_request = primed.take_requests().get<message_20::PowerDeliveryRequest>();
    REQUIRE(pd_request.has_value());
    REQUIRE(pd_request->charge_progress == dt::Progress::Stop);
}

struct Forwarded {
    dt_sae::DER_Dynamic_AC_CLResControlMode mode;
    ev::DerControlProblems problems;
};

struct Observer {
    bool stop_fired = false;
    bool pause_fired = false;
    std::vector<Forwarded> controls;
    std::vector<iso15118::d20::AcTargetPower> targets;
    std::vector<std::uint32_t> enabled_modes;
    ev::feedback::Callbacks callbacks{};
    Observer() {
        callbacks.stop_from_charger = [this]() { stop_fired = true; };
        callbacks.pause_from_charger = [this]() { pause_fired = true; };
        callbacks.sae_der_control = [this](const dt_sae::DER_Dynamic_AC_CLResControlMode& mode,
                                           const ev::DerControlProblems& problems) {
            controls.push_back({mode, problems});
        };
        callbacks.der_enabled_modes = [this](std::uint32_t modes) { enabled_modes.push_back(modes); };
        callbacks.ac_target_power = [this](const iso15118::d20::AcTargetPower& target) { targets.push_back(target); };
    }
};

// Records log messages while alive, then restores a no-op callback.
class LogCounter {
public:
    LogCounter() {
        io::set_logging_callback(
            [this](LogLevel level, std::string message) { messages.emplace_back(level, std::move(message)); });
    }
    ~LogCounter() {
        io::set_logging_callback([](LogLevel, const std::string&) {});
    }
    LogCounter(const LogCounter&) = delete;
    LogCounter& operator=(const LogCounter&) = delete;

    std::size_t count(const std::string& needle, LogLevel level = LogLevel::Warning) const {
        std::size_t n = 0;
        for (const auto& [message_level, message] : messages) {
            n += (message_level == level and message.find(needle) != std::string::npos) ? 1 : 0;
        }
        return n;
    }

private:
    std::vector<std::pair<LogLevel, std::string>> messages;
};

} // namespace

SCENARIO("ISO15118-20 EV AC_DER_SAE_ChargeLoop emits a Dynamic request on enter") {
    const ev::feedback::Callbacks callbacks{};
    auto primed = make_primed(callbacks, sae_options(DROOP_BIT), [](FsmStateHelper& helper) {
        seed_present_5000(helper);
        helper.get_context().set_sae_enabled_modes(DROOP_BIT);
    });

    const auto request = primed.take_requests().get<ClRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->header.session_id == SESSION_HEADER.session_id);
    REQUIRE_FALSE(request->meter_info_requested);
    REQUIRE_FALSE(request->display_parameters.has_value());
    REQUIRE(std::holds_alternative<dt_sae::DER_Dynamic_AC_CLReqControlMode>(request->control_mode));
    const auto& mode = std::get<dt_sae::DER_Dynamic_AC_CLReqControlMode>(request->control_mode);

    REQUIRE(dt::from_RationalNumber(mode.max_charge_power) == Catch::Approx(11000.0f));
    REQUIRE(dt::from_RationalNumber(mode.min_charge_power) == Catch::Approx(1000.0f));
    REQUIRE(dt::from_RationalNumber(mode.present_active_power) == Catch::Approx(5000.0f));
    REQUIRE(dt::from_RationalNumber(mode.maximum_discharge_power) == Catch::Approx(9000.0f));
    REQUIRE(dt::from_RationalNumber(mode.minimum_discharge_power) == Catch::Approx(800.0f));
    REQUIRE(mode.enabled_modes == DROOP_BIT);
    // Nothing measured: the profile nominals.
    REQUIRE(dt::from_RationalNumber(mode.present_voltage) == Catch::Approx(230.0f));
    REQUIRE(dt::from_RationalNumber(mode.present_frequency) == Catch::Approx(50.0f));
    REQUIRE(mode.der_alarm_status == 0);
    REQUIRE(mode.der_operational_state == dt_sae::DEROperationalState::On);
    REQUIRE(mode.der_connection_status == dt_sae::DERConnectionStatus::Connected);
    REQUIRE(mode.update_time == CPD_UPDATE_TIME);
}

SCENARIO("ISO15118-20 EV AC_DER_SAE_ChargeLoop reports measured grid values from the live params") {
    const ev::feedback::Callbacks callbacks{};
    auto primed = make_primed(callbacks, sae_options(), [](FsmStateHelper& helper) {
        ev::AcChargeParams p{};
        p.phase_count = 1;
        p.present_voltage = 231.5f;
        p.present_frequency = 49.9f;
        helper.set_ac_params(p);
    });

    const auto first = take_mode(primed);
    REQUIRE(dt::from_RationalNumber(first.present_voltage) == Catch::Approx(231.5f));
    REQUIRE(dt::from_RationalNumber(first.present_frequency) == Catch::Approx(49.9f));
    REQUIRE(first.der_alarm_status == 0);

    {
        auto h = primed.helper.get_ac_params_monitor().handle();
        h->der_alarm_status = 0x5;
    }
    REQUIRE(stay_on(primed, make_ok()).der_alarm_status == 0x5);
}

SCENARIO("ISO15118-20 EV AC_DER_SAE_ChargeLoop stays and re-emits a request on every clean response") {
    Observer obs;
    auto primed = make_primed(obs.callbacks);
    take_mode(primed);

    for (int round = 0; round < 2; ++round) {
        stay_on(primed, make_ok());
    }
    REQUIRE_FALSE(obs.stop_fired);
    REQUIRE(obs.controls.size() == 2);
}

SCENARIO("ISO15118-20 EV AC_DER_SAE_ChargeLoop forwards the whole Dynamic mode") {
    Observer obs;
    auto primed = make_primed(obs.callbacks);
    take_mode(primed);

    auto res = make_ok();
    auto& mode = std::get<dt_sae::DER_Dynamic_AC_CLResControlMode>(res.control_mode);
    mode.target_active_power = dt::from_float(-3000.0f);
    mode.required_der_operating_mode = dt_sae::RequiredDEROperatingMode::GridForming;
    mode.grid_connection_mode = dt_sae::GridConnectionMode::GridIslanded;
    stay_on(primed, res);

    REQUIRE(obs.controls.size() == 1);
    const auto& forwarded = obs.controls.back().mode;
    REQUIRE(dt::from_RationalNumber(forwarded.target_active_power) == Catch::Approx(-3000.0f));
    REQUIRE(forwarded.required_der_operating_mode == dt_sae::RequiredDEROperatingMode::GridForming);
    REQUIRE(forwarded.grid_connection_mode == dt_sae::GridConnectionMode::GridIslanded);
}

SCENARIO("ISO15118-20 EV AC_DER_SAE_ChargeLoop reports the set point and target frequency through ac_target_power") {
    Observer obs;
    auto primed = make_primed(obs.callbacks);
    take_mode(primed);

    stay_on(primed, make_ok());
    REQUIRE(obs.targets.size() == 1);
    REQUIRE_FALSE(obs.targets.back().target_frequency.has_value());

    auto res = make_ok();
    res.target_frequency = dt::from_float(50.2f);
    auto& mode = std::get<dt_sae::DER_Dynamic_AC_CLResControlMode>(res.control_mode);
    mode.target_active_power = dt::from_float(-3000.0f);
    mode.target_reactive_power = dt::from_float(500.0f);
    stay_on(primed, res);

    REQUIRE(obs.targets.size() == 2);
    const auto& target = obs.targets.back();
    REQUIRE(target.target_active_power.has_value());
    REQUIRE(dt::from_RationalNumber(*target.target_active_power) == Catch::Approx(-3000.0f));
    REQUIRE(target.target_reactive_power.has_value());
    REQUIRE(dt::from_RationalNumber(*target.target_reactive_power) == Catch::Approx(500.0f));
    REQUIRE(target.target_frequency.has_value());
    REQUIRE(dt::from_RationalNumber(*target.target_frequency) == Catch::Approx(50.2f));
}

SCENARIO("ISO15118-20 EV AC_DER_SAE_ChargeLoop drives PowerDelivery(Stop) on Terminate") {
    Observer obs;
    auto primed = make_primed(obs.callbacks);

    primed.handle_response(
        make_res(SESSION_HEADER, ResponseCode::OK, dt::EvseStatus{0, dt::EvseNotification::Terminate}));
    require_power_delivery_stop(primed, primed.feed(ev::d20::Event::V2GTP_MESSAGE).transitioned());
    REQUIRE(obs.stop_fired);
    REQUIRE(obs.controls.empty());
    REQUIRE(primed.ctx.requested_stop_reason() == dt::ChargingSession::Terminate);
}

SCENARIO("ISO15118-20 EV AC_DER_SAE_ChargeLoop pauses through PowerDelivery(Stop) on Pause") {
    Observer obs;
    auto primed = make_primed(obs.callbacks);

    primed.handle_response(make_res(SESSION_HEADER, ResponseCode::OK, dt::EvseStatus{0, dt::EvseNotification::Pause}));
    require_power_delivery_stop(primed, primed.feed(ev::d20::Event::V2GTP_MESSAGE).transitioned());
    REQUIRE(obs.pause_fired);
    REQUIRE_FALSE(obs.stop_fired);
    REQUIRE(obs.controls.empty());
    REQUIRE(primed.ctx.requested_stop_reason() == dt::ChargingSession::Pause);
}

SCENARIO("ISO15118-20 EV AC_DER_SAE_ChargeLoop defers an EV stop or pause to the next response") {
    Observer obs;
    auto primed = make_primed(obs.callbacks);

    GIVEN("An EV stop request") {
        primed.ctx.set_stop_charging_requested(true);
        REQUIRE_FALSE(primed.feed(ev::d20::Event::CONTROL_MESSAGE).transitioned());
        REQUIRE_FALSE(primed.take_requests().get<message_20::PowerDeliveryRequest>().has_value());

        primed.handle_response(make_ok());
        require_power_delivery_stop(primed, primed.feed(ev::d20::Event::V2GTP_MESSAGE).transitioned());
        REQUIRE(primed.ctx.requested_stop_reason() == dt::ChargingSession::Terminate);
    }
    GIVEN("An EV pause request") {
        primed.ctx.set_pause_charging_requested(true);

        primed.handle_response(make_ok());
        require_power_delivery_stop(primed, primed.feed(ev::d20::Event::V2GTP_MESSAGE).transitioned());
        REQUIRE(primed.ctx.requested_stop_reason() == dt::ChargingSession::Pause);
    }
    REQUIRE_FALSE(obs.stop_fired);
    REQUIRE(obs.controls.empty());
}

SCENARIO("ISO15118-20 EV AC_DER_SAE_ChargeLoop stops AC DER while PermitService is withdrawn [V2G20-3366]") {
    Observer obs;
    auto primed = make_primed(obs.callbacks);
    take_mode(primed);

    auto denied = make_ok();
    control_of(denied).enter_service_cl_res.permit_service = false;
    const auto off = stay_on(primed, denied);

    REQUIRE_FALSE(primed.ctx.sae_permit_service());
    REQUIRE(off.der_operational_state == dt_sae::DEROperationalState::Off);
    REQUIRE(off.der_connection_status == dt_sae::DERConnectionStatus::Disconnected);
    REQUIRE(off.update_time > MICROSECOND_EPOCH_FLOOR);
    // Charging continues.
    REQUIRE(dt::from_RationalNumber(off.max_charge_power) == Catch::Approx(11000.0f));

    const auto still_off = stay_on(primed, denied);
    REQUIRE(still_off.der_operational_state == dt_sae::DEROperationalState::Off);
    REQUIRE(still_off.update_time == off.update_time);

    // A grant changes only the permit here, so the refresh is visible only from a known value.
    primed.ctx.set_sae_settings_update_time(CPD_UPDATE_TIME);
    const auto on = stay_on(primed, make_ok());
    REQUIRE(primed.ctx.sae_permit_service());
    REQUIRE(on.der_operational_state == dt_sae::DEROperationalState::On);
    REQUIRE(on.der_connection_status == dt_sae::DERConnectionStatus::Connected);
    REQUIRE(on.update_time > MICROSECOND_EPOCH_FLOOR);

    // The owner sees the permit of every round.
    REQUIRE(obs.controls.size() == 3);
    REQUIRE_FALSE(obs.controls[0].mode.der_control_cl_res.enter_service_cl_res.permit_service);
    REQUIRE_FALSE(obs.controls[1].mode.der_control_cl_res.enter_service_cl_res.permit_service);
    REQUIRE(obs.controls[2].mode.der_control_cl_res.enter_service_cl_res.permit_service);
}

SCENARIO("ISO15118-20 EV AC_DER_SAE_ChargeLoop recomputes the enabled modes from each response") {
    Observer obs;
    // Droop supported, volt watt not: the mask drops volt watt.
    auto primed = make_primed(obs.callbacks, sae_options(DROOP_BIT));
    take_mode(primed);

    REQUIRE(stay_on(primed, make_droop_and_volt_watt()).enabled_modes == DROOP_BIT);
    REQUIRE(primed.ctx.sae_enabled_modes() == DROOP_BIT);
    REQUIRE(obs.enabled_modes == std::vector<std::uint32_t>{DROOP_BIT});

    // The same dictate again: no feedback.
    stay_on(primed, make_droop_and_volt_watt());
    REQUIRE(obs.enabled_modes.size() == 1);

    // The dictate repeats nothing: cleared.
    REQUIRE(stay_on(primed, make_ok()).enabled_modes == 0);
    REQUIRE(primed.ctx.sae_enabled_modes() == 0);
    REQUIRE(obs.enabled_modes == std::vector<std::uint32_t>{DROOP_BIT, 0});
}

SCENARIO("ISO15118-20 EV AC_DER_SAE_ChargeLoop clears the CPD enabled modes when the dictate repeats nothing") {
    Observer obs;
    auto primed = make_primed(obs.callbacks, sae_options(DROOP_BIT | VOLT_WATT_BIT), [](FsmStateHelper& helper) {
        seed_present_5000(helper);
        helper.get_context().set_sae_enabled_modes(DROOP_BIT | VOLT_WATT_BIT);
    });
    REQUIRE(take_mode(primed).enabled_modes == (DROOP_BIT | VOLT_WATT_BIT));

    REQUIRE(stay_on(primed, make_ok()).enabled_modes == 0);
    REQUIRE(obs.enabled_modes == std::vector<std::uint32_t>{0});
}

SCENARIO("ISO15118-20 EV AC_DER_SAE_ChargeLoop continues on invalid control by default") {
    LogCounter logs;
    Observer obs;
    auto primed = make_primed(obs.callbacks);
    take_mode(primed);

    // Clean from the start: nothing to clear.
    stay_on(primed, make_ok());
    REQUIRE(logs.count("problems cleared", LogLevel::Info) == 0);

    stay_on(primed, make_invalid());
    REQUIRE(obs.controls.size() == 2);
    REQUIRE_FALSE(obs.controls.back().problems.empty());
    const auto per_problem = logs.count("SAE DER control: ");
    REQUIRE(per_problem == obs.controls.back().problems.size());
    REQUIRE(logs.count("Continuing despite") == 1);

    // The same problems again: forwarded, not warned.
    stay_on(primed, make_invalid());
    REQUIRE(obs.controls.size() == 3);
    REQUIRE(obs.controls.back().problems == obs.controls[1].problems);
    REQUIRE(logs.count("SAE DER control: ") == per_problem);
    REQUIRE(logs.count("Continuing despite") == 1);

    // Cleared: logged once.
    stay_on(primed, make_ok());
    REQUIRE(obs.controls.size() == 4);
    REQUIRE(obs.controls.back().problems.empty());
    REQUIRE(logs.count("problems cleared", LogLevel::Info) == 1);
    stay_on(primed, make_ok());
    REQUIRE(logs.count("problems cleared", LogLevel::Info) == 1);

    // Changed back: warned again.
    stay_on(primed, make_invalid());
    REQUIRE(logs.count("SAE DER control: ") == 2 * per_problem);
    REQUIRE(logs.count("Continuing despite") == 2);
}

SCENARIO("ISO15118-20 EV AC_DER_SAE_ChargeLoop stops on invalid control when configured to") {
    Observer obs;
    auto primed = make_primed(obs.callbacks, sae_options(0, true));

    expect_stops_session(primed, make_invalid(), ev::d20::StateID::AC_DER_SAE_ChargeLoop);
    // The block that ends the session still reaches the owner, with its problems.
    REQUIRE(obs.controls.size() == 1);
    REQUIRE_FALSE(obs.controls.front().problems.empty());
    REQUIRE(obs.targets.empty());
}

SCENARIO("ISO15118-20 EV AC_DER_SAE_ChargeLoop terminates before validating the control") {
    Observer obs;
    auto primed = make_primed(obs.callbacks, sae_options(0, true));

    auto res = make_invalid();
    res.status = dt::EvseStatus{0, dt::EvseNotification::Terminate};
    primed.handle_response(res);
    require_power_delivery_stop(primed, primed.feed(ev::d20::Event::V2GTP_MESSAGE).transitioned());
    REQUIRE(obs.stop_fired);
    REQUIRE(obs.controls.empty());
    REQUIRE(primed.ctx.requested_stop_reason() == dt::ChargingSession::Terminate);
}

SCENARIO("ISO15118-20 EV AC_DER_SAE_ChargeLoop pauses before validating the control") {
    Observer obs;
    auto primed = make_primed(obs.callbacks, sae_options(0, true));

    auto res = make_invalid();
    res.status = dt::EvseStatus{0, dt::EvseNotification::Pause};
    primed.handle_response(res);
    require_power_delivery_stop(primed, primed.feed(ev::d20::Event::V2GTP_MESSAGE).transitioned());
    REQUIRE(obs.pause_fired);
    REQUIRE_FALSE(obs.stop_fired);
    REQUIRE(obs.controls.empty());
    REQUIRE(primed.ctx.requested_stop_reason() == dt::ChargingSession::Pause);
}

SCENARIO("ISO15118-20 EV AC_DER_SAE_ChargeLoop echoes the update time and refreshes it only on a change") {
    const ev::feedback::Callbacks callbacks{};
    auto primed = make_primed(callbacks, sae_options(DROOP_BIT));
    REQUIRE(take_mode(primed).update_time == CPD_UPDATE_TIME);

    REQUIRE(stay_on(primed, make_ok()).update_time == CPD_UPDATE_TIME);

    const auto refreshed = stay_on(primed, make_droop_and_volt_watt()).update_time;
    REQUIRE(refreshed > MICROSECOND_EPOCH_FLOOR);
    REQUIRE(primed.ctx.sae_settings_update_time() == refreshed);

    REQUIRE(stay_on(primed, make_droop_and_volt_watt()).update_time == refreshed);
}

SCENARIO("ISO15118-20 EV AC_DER_SAE_ChargeLoop refreshes the update time in SECC time") {
    const ev::feedback::Callbacks callbacks{};
    const auto synchronized = [](FsmStateHelper& helper) {
        seed_present_5000(helper);
        helper.get_context().secc_clock().synchronize(SECC_REFERENCE_US);
    };
    const auto since = std::chrono::steady_clock::now();
    auto primed = make_primed(callbacks, sae_options(DROOP_BIT), synchronized);
    take_mode(primed);

    const auto refreshed = stay_on(primed, make_droop_and_volt_watt()).update_time;
    require_tracks_reference(refreshed, SECC_REFERENCE_US, since);
}

SCENARIO("ISO15118-20 EV AC_DER_SAE_ChargeLoop stops the session on a Scheduled response") {
    Observer obs;
    auto primed = make_primed(obs.callbacks);

    auto res = make_ok();
    res.control_mode = dt_sae::DER_Scheduled_AC_CLResControlMode{};
    expect_stops_session(primed, res, ev::d20::StateID::AC_DER_SAE_ChargeLoop);
    REQUIRE(obs.controls.empty());
}

SCENARIO("ISO15118-20 EV AC_DER_SAE_ChargeLoop rejects malformed responses") {
    const ev::feedback::Callbacks callbacks{};
    const auto make_fsm = [](FsmStateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.get_session().set_id(SESSION_HEADER.session_id);
        return fsm::v2::FSM<ev::d20::StateBase>{ctx.create_state<State>()};
    };
    const auto make_ok_with = [](const message_20::Header& header) { return make_res(header, ResponseCode::OK); };
    const auto wrong = message_20::AuthorizationResponse{SESSION_HEADER, ResponseCode::OK, dt::Processing::Finished};
    check_rejection_paths(callbacks, ev::d20::StateID::AC_DER_SAE_ChargeLoop, make_fsm, make_ok_with, wrong);
}
