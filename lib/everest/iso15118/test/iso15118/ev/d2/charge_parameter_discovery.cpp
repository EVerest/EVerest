// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <string>

#include "helper.hpp"

#include <iso15118/ev/d2/state/authorization.hpp>
#include <iso15118/ev/d2/state/charge_parameter_discovery.hpp>
#include <iso15118/io/logging.hpp>
#include <iso15118/message_2/authorization.hpp>
#include <iso15118/message_2/charge_parameter_discovery.hpp>
#include <iso15118/message_2/session_stop.hpp>

using namespace iso15118;
namespace dt = message_2::datatypes;

namespace {

dt::SAScheduleList make_schedule() {
    dt::SAScheduleList list;
    dt::SAScheduleTuple tuple;
    tuple.sa_schedule_tuple_id = 7;
    tuple.pmax_schedule.push_back(dt::PMaxScheduleEntry{0, std::nullopt, dt::to_physical_value(11000.0, dt::Unit::W)});
    list.push_back(tuple);
    return list;
}

message_2::ChargeParameterDiscoveryResponse make_response(const message_2::Header& header, dt::ResponseCode code,
                                                          dt::EVSEProcessing processing, bool with_schedule = true,
                                                          bool dc = true) {
    message_2::ChargeParameterDiscoveryResponse res;
    res.header = header;
    res.response_code = code;
    res.evse_processing = processing;
    if (with_schedule) {
        res.sa_schedule_list = make_schedule();
    }
    if (dc) {
        dt::DC_EVSEChargeParameter param{};
        param.evse_maximum_voltage_limit = dt::to_physical_value(900.0, dt::Unit::V);
        param.evse_maximum_current_limit = dt::to_physical_value(200.0, dt::Unit::A);
        param.evse_maximum_power_limit = dt::to_physical_value(150000.0, dt::Unit::W);
        res.dc_evse_charge_parameter = param;
    } else {
        dt::AC_EVSEChargeParameter param{};
        param.evse_nominal_voltage = dt::to_physical_value(230.0, dt::Unit::V);
        param.evse_max_current = dt::to_physical_value(32.0, dt::Unit::A);
        res.ac_evse_charge_parameter = param;
    }
    return res;
}

const auto by_authorization = d2_entered_by([](D2StateHelper& helper) {
    auto res = d2_ok<message_2::AuthorizationResponse>();
    res.evse_processing = dt::EVSEProcessing::Finished;
    return d2_successor_of<ev::d2::state::Authorization>(helper, res);
});

} // namespace

SCENARIO("ISO15118-2 EV ChargeParameterDiscovery's DC parameters go out on the Authorization transition") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::ChargeParameterDiscovery> primed{callbacks, d2_no_seed, by_authorization};

    const auto request = primed.take_requests().get<message_2::ChargeParameterDiscoveryRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->requested_energy_transfer_mode == dt::EnergyTransferMode::DC_extended);
    REQUIRE(request->max_entries_sa_schedule_tuple == 12);
    REQUIRE(request->dc_ev_charge_parameter.has_value());
    const auto& dc = request->dc_ev_charge_parameter.value();
    // [V2G2-478] mirror of DIN: not ready to take power at parameter discovery.
    REQUIRE(dc.dc_ev_status.ev_ready == false);
    REQUIRE(dc.dc_ev_status.ev_ress_soc == 50);
    REQUIRE(dt::from_physical_value(dc.ev_maximum_current_limit) == 150.0);
    REQUIRE(dt::from_physical_value(dc.ev_maximum_voltage_limit) == 900.0);
    REQUIRE(dc.ev_maximum_power_limit.has_value());
    REQUIRE_FALSE(dc.ev_energy_request.has_value());
    REQUIRE_FALSE(dc.full_soc.has_value());
}

SCENARIO("ISO15118-2 EV ChargeParameterDiscovery omits the power limit when the module reports none") {
    const ev::feedback::Callbacks callbacks{};
    const auto seed_no_power = [](D2StateHelper& helper) {
        auto params = default_dc_params();
        params.max_charge_power = 0.0f;
        helper.set_dc_params(params);
    };
    PrimedState<ev::d2::state::ChargeParameterDiscovery> primed{callbacks, seed_no_power, by_authorization};

    const auto request = primed.take_requests().get<message_2::ChargeParameterDiscoveryRequest>();
    REQUIRE(request.has_value());
    REQUIRE_FALSE(request->dc_ev_charge_parameter->ev_maximum_power_limit.has_value());
}

SCENARIO("ISO15118-2 EV ChargeParameterDiscovery's AC parameters go out on the Authorization transition") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::ChargeParameterDiscovery> primed{callbacks, ac_params(), false, d2_no_seed,
                                                                by_authorization};

    const auto request = primed.take_requests().get<message_2::ChargeParameterDiscoveryRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->requested_energy_transfer_mode == dt::EnergyTransferMode::AC_three_phase_core);
    REQUIRE(request->ac_ev_charge_parameter.has_value());
    const auto& ac = request->ac_ev_charge_parameter.value();
    REQUIRE(dt::from_physical_value(ac.ev_max_current) == 32.0);
    REQUIRE(dt::from_physical_value(ac.ev_min_current) == 10.0);
}

SCENARIO("ISO15118-2 EV ChargeParameterDiscovery resends on Ongoing") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::ChargeParameterDiscovery> primed{callbacks, d2_no_seed};

    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK, dt::EVSEProcessing::Ongoing, false));
    const auto result = primed.feed(ev::d2::Event::V2GTP_MESSAGE);

    REQUIRE(result.output == ev::d2::Disposition::Awaiting);
    REQUIRE(primed.take_requests().get<message_2::ChargeParameterDiscoveryRequest>().has_value());
}

SCENARIO("ISO15118-2 EV ChargeParameterDiscovery leaves an Ongoing poll on a latched stop request") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::ChargeParameterDiscovery> primed{callbacks, d2_no_seed};

    primed.ctx.set_stop_charging_requested(true);
    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK, dt::EVSEProcessing::Ongoing, false));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);

    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::SessionStop);
    REQUIRE(primed.take_requests().get<message_2::SessionStopRequest>().has_value());
}

SCENARIO("ISO15118-2 EV ChargeParameterDiscovery transitions to CableCheck on DC Finished") {
    bool power_ready = false;
    std::optional<ev::feedback::DcMaximumLimits> limits;
    ev::feedback::Callbacks callbacks{};
    callbacks.ev_power_ready = [&power_ready]() { power_ready = true; };
    callbacks.dc_evse_present_limits = [&limits](const ev::feedback::DcMaximumLimits& l) { limits = l; };
    PrimedState<ev::d2::state::ChargeParameterDiscovery> primed{callbacks, d2_no_seed};

    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK, dt::EVSEProcessing::Finished));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);

    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::CableCheck);
    REQUIRE(power_ready);
    REQUIRE(limits.has_value());
    REQUIRE(limits->voltage == 900.0f);
    REQUIRE(primed.ctx.sa_schedule_tuple_id() == 7);
    REQUIRE(primed.ctx.evse_info.selected_pmax_schedule.size() == 1);
}

SCENARIO("ISO15118-2 EV ChargeParameterDiscovery transitions to PowerDelivery on AC Finished") {
    std::optional<float> evse_max_current;
    ev::feedback::Callbacks callbacks{};
    callbacks.ac_evse_max_current = [&evse_max_current](float current) { evse_max_current = current; };
    PrimedState<ev::d2::state::ChargeParameterDiscovery> primed{callbacks, ac_params(), false, d2_no_seed};

    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK, dt::EVSEProcessing::Finished, true, false));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::PowerDelivery);
    REQUIRE(primed.ctx.evse_info.ac_nominal_voltage.has_value());
    REQUIRE(evse_max_current == 32.0f);
}

SCENARIO("ISO15118-2 EV ChargeParameterDiscovery stops on Finished without an SAScheduleList") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::ChargeParameterDiscovery> primed{callbacks, d2_no_seed};

    expect_stops_session(primed, make_response(d2_header(), dt::ResponseCode::OK, dt::EVSEProcessing::Finished, false),
                         ev::d2::StateID::ChargeParameterDiscovery);
}

SCENARIO("ISO15118-2 EV ChargeParameterDiscovery rejects malformed responses") {
    const ev::feedback::Callbacks callbacks{};
    const auto make_fsm = [](D2StateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.set_session_id(D2_SESSION_ID);
        return fsm::v2::FSM<ev::d2::StateBase>{ctx.create_state<ev::d2::state::ChargeParameterDiscovery>()};
    };
    const auto make_ok = [](const message_2::Header& header) {
        return make_response(header, dt::ResponseCode::OK, dt::EVSEProcessing::Finished);
    };
    message_2::AuthorizationResponse wrong;
    wrong.header = d2_header();
    wrong.response_code = dt::ResponseCode::OK;
    wrong.evse_processing = dt::EVSEProcessing::Finished;
    check_rejection_paths(callbacks, ev::d2::StateID::ChargeParameterDiscovery, make_fsm, make_ok, wrong);
}

namespace {

// start_charging asked for 20 kWh and to leave in an hour.
ev::params::EvSessionParams with_energy_request(ev::params::EvSessionParams params) {
    params.energy_request.energy_wh = 20000.0f;
    params.energy_request.departure = std::chrono::steady_clock::now() + std::chrono::hours(1);
    return params;
}

// The departure time went out as the seconds left at sending, a moment after the request was made.
void require_departure_within_the_hour(const std::optional<uint32_t>& departure_time) {
    REQUIRE(departure_time.has_value());
    REQUIRE(*departure_time <= 3600U);
    REQUIRE(*departure_time >= 3590U);
}

} // namespace

SCENARIO("ISO15118-2 EV AC ChargeParameterDiscovery states start_charging's energy and departure time") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::ChargeParameterDiscovery> primed{callbacks, with_energy_request(ac_params()), false,
                                                                d2_no_seed, by_authorization};

    const auto request = primed.take_requests().get<message_2::ChargeParameterDiscoveryRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->ac_ev_charge_parameter.has_value());
    const auto& ac = request->ac_ev_charge_parameter.value();
    REQUIRE(dt::from_physical_value(ac.e_amount) == 20000.0);
    require_departure_within_the_hour(ac.departure_time);
}

SCENARIO("ISO15118-2 EV AC ChargeParameterDiscovery falls back to the configured EAmount and omits the departure "
         "time") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::ChargeParameterDiscovery> primed{callbacks, ac_params(), false, d2_no_seed,
                                                                by_authorization};

    const auto request = primed.take_requests().get<message_2::ChargeParameterDiscoveryRequest>();
    REQUIRE(request.has_value());
    const auto& ac = request->ac_ev_charge_parameter.value();
    REQUIRE(dt::from_physical_value(ac.e_amount) == 60000.0);
    REQUIRE_FALSE(ac.departure_time.has_value());
}

SCENARIO("ISO15118-2 EV DC ChargeParameterDiscovery states start_charging's energy and departure time") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::ChargeParameterDiscovery> primed{
        callbacks, with_energy_request(ev::params::EvSessionParams{}), false, d2_no_seed, by_authorization};

    const auto request = primed.take_requests().get<message_2::ChargeParameterDiscoveryRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->dc_ev_charge_parameter.has_value());
    const auto& dc = request->dc_ev_charge_parameter.value();
    REQUIRE(dc.ev_energy_request.has_value());
    REQUIRE(dt::from_physical_value(dc.ev_energy_request.value()) == 20000.0);
    require_departure_within_the_hour(dc.departure_time);
}

SCENARIO("ISO15118-2 EV DC ChargeParameterDiscovery omits energy request and departure time when neither is stated") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::ChargeParameterDiscovery> primed{callbacks, d2_no_seed, by_authorization};

    const auto request = primed.take_requests().get<message_2::ChargeParameterDiscoveryRequest>();
    REQUIRE(request.has_value());
    const auto& dc = request->dc_ev_charge_parameter.value();
    REQUIRE_FALSE(dc.ev_energy_request.has_value());
    REQUIRE_FALSE(dc.departure_time.has_value());
}

SCENARIO("ISO15118-2 EV energy request clamps a departure time beyond the 32-bit field") {
    ev::params::EnergyRequest request;
    const auto now = std::chrono::steady_clock::now();
    request.departure = now + std::chrono::seconds{std::numeric_limits<std::uint32_t>::max()} + std::chrono::hours(1);
    REQUIRE(request.departure_in(now) == std::numeric_limits<std::uint32_t>::max());
}

namespace {

// Counts the energy-cap warnings libiso15118 logs while alive; the logging callback is process global.
class CapWarnings {
public:
    CapWarnings() {
        io::set_logging_callback([this](LogLevel level, const std::string& message) {
            if (level == LogLevel::Warning and message.find("capped") != std::string::npos) {
                ++count;
            }
        });
    }
    CapWarnings(const CapWarnings&) = delete;
    CapWarnings& operator=(const CapWarnings&) = delete;
    ~CapWarnings() {
        io::set_logging_callback([](LogLevel, const std::string&) {});
    }

    int count{0};
};

ev::params::EvSessionParams with_energy(ev::params::EvSessionParams params, float energy_wh) {
    params.energy_request.energy_wh = energy_wh;
    return params;
}

double sent_e_amount(const ev::params::EvSessionParams& params) {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::ChargeParameterDiscovery> primed{callbacks, params, false, d2_no_seed, by_authorization};
    const auto request = primed.take_requests().get<message_2::ChargeParameterDiscoveryRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->ac_ev_charge_parameter.has_value());
    return dt::from_physical_value(request->ac_ev_charge_parameter->e_amount);
}

double sent_ev_energy_request(const ev::params::EvSessionParams& params) {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::ChargeParameterDiscovery> primed{callbacks, params, false, d2_no_seed, by_authorization};
    const auto request = primed.take_requests().get<message_2::ChargeParameterDiscoveryRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->dc_ev_charge_parameter.has_value());
    REQUIRE(request->dc_ev_charge_parameter->ev_energy_request.has_value());
    return dt::from_physical_value(request->dc_ev_charge_parameter->ev_energy_request.value());
}

} // namespace

// [V2G2-832]: EAmount and EVEnergyRequest are at most 200000 Wh.
SCENARIO("ISO15118-2 EV AC ChargeParameterDiscovery caps EAmount at 200000 Wh") {
    REQUIRE(sent_e_amount(with_energy(ac_params(), 250000.0f)) == 200000.0);
    REQUIRE(sent_e_amount(with_energy(ac_params(), 200000.0f)) == 200000.0);

    auto fallback = ac_params();
    fallback.ac.e_amount = 250000.0f;
    REQUIRE(sent_e_amount(fallback) == 200000.0);

    // A stated EAmount replaces the fallback, which is then neither sent nor capped.
    const CapWarnings warnings;
    REQUIRE(sent_e_amount(with_energy(fallback, 100000.0f)) == 100000.0);
    REQUIRE(warnings.count == 0);
}

SCENARIO("ISO15118-2 EV DC ChargeParameterDiscovery leaves a NaN energy unstated") {
    const CapWarnings warnings;
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::ChargeParameterDiscovery> primed{
        callbacks, with_energy(ev::params::EvSessionParams{}, std::numeric_limits<float>::quiet_NaN()), false,
        d2_no_seed, by_authorization};
    const auto request = primed.take_requests().get<message_2::ChargeParameterDiscoveryRequest>();
    REQUIRE(request.has_value());
    REQUIRE_FALSE(request->dc_ev_charge_parameter->ev_energy_request.has_value());
    REQUIRE(warnings.count == 0);
}

SCENARIO("ISO15118-2 EV DC ChargeParameterDiscovery caps EVEnergyRequest at 200000 Wh") {
    REQUIRE(sent_ev_energy_request(with_energy(ev::params::EvSessionParams{}, 250000.0f)) == 200000.0);
    REQUIRE(sent_ev_energy_request(with_energy(ev::params::EvSessionParams{}, 200000.0f)) == 200000.0);
}

SCENARIO("ISO15118-2 EV ChargeParameterDiscovery warns once per session when it caps the energy") {
    const CapWarnings warnings;
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::ChargeParameterDiscovery> primed{
        callbacks, with_energy(ev::params::EvSessionParams{}, 250000.0f), false, d2_no_seed, by_authorization};
    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK, dt::EVSEProcessing::Ongoing, false));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).output == ev::d2::Disposition::Awaiting);
    REQUIRE(primed.take_requests().get<message_2::ChargeParameterDiscoveryRequest>().has_value());
    REQUIRE(warnings.count == 1);
}
