// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/d20/state/ac_der_sae_charge_loop.hpp>
#include <iso15118/d20/state/session_stop.hpp>

#include <iso15118/d20/config.hpp>
#include <iso15118/message/ac_der_sae_charge_loop.hpp>
#include <iso15118/message/power_delivery.hpp>
#include <iso15118/message/session_setup.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

using namespace iso15118;

namespace dt = message_20::datatypes;
namespace dt_sae = message_20::datatypes::sae;
namespace sae = iso15118::sae;

using Scheduled_DER_Req = dt_sae::DER_Scheduled_AC_CLReqControlMode;
using Dynamic_DER_Req = dt_sae::DER_Dynamic_AC_CLReqControlMode;
using Scheduled_DER_Res = dt_sae::DER_Scheduled_AC_CLResControlMode;
using Dynamic_DER_Res = dt_sae::DER_Dynamic_AC_CLResControlMode;

using ChargeLoopResponse = message_20::DER_SAE_AC_ChargeLoopResponse;

namespace {

constexpr float NOMINAL_VOLTAGE_V = 230.0f;

// Copied from test/exi/cb/iso20/helper.hpp so the FSM tests do not reach across test directories.
template <typename Message> std::vector<std::uint8_t> serialize_helper(const Message& message) {
    std::uint8_t serialization_buffer[1024];
    io::StreamOutputView out({serialization_buffer, sizeof(serialization_buffer)});

    const auto size = message_20::serialize(message, out);

    return std::vector<std::uint8_t>(serialization_buffer, serialization_buffer + size);
}

constexpr std::uint32_t bit_of(sae::DerBitMapFunctions function) {
    return 1U << static_cast<std::uint32_t>(function);
}

constexpr std::uint32_t CHARGE_AND_DISCHARGE_ONLY =
    bit_of(sae::DerBitMapFunctions::ChargeFunction) | bit_of(sae::DerBitMapFunctions::DischargeFunction);
constexpr std::uint32_t WITH_ENTER_SERVICE = CHARGE_AND_DISCHARGE_ONLY | bit_of(sae::DerBitMapFunctions::EnterService);

// Two enableable functions far apart in the bitmap, so a cross wired mapping lands on neither.
constexpr std::uint32_t SECC_ENABLED_MODES =
    bit_of(sae::DerBitMapFunctions::EnterService) | bit_of(sae::DerBitMapFunctions::VoltVarFunction);

// Bit 25 carries no function in this document, so the SECC ignores it.
constexpr std::uint32_t UNUSED_BIT = 1U << 25U;

// The full formatted lines, so printing an adjacent bitmap argument instead of the intended one fails.
constexpr auto MISSING_TARGET_ACTIVE_POWER_LINE = "No target active power is available for the mandatory dynamic mode";
constexpr auto UNDECLARED_TARGET_ACTIVE_POWER_LINE =
    "EV did not declare the target active power function, sending the mandatory dynamic mode target active power "
    "anyway";
constexpr auto DER_ALARM_STATUS_LINE = "EV reports DERAlarmStatus 0x00000004";
constexpr auto ENABLED_MODES_MISMATCH_LINE = "EV EnabledModes 0x00000008 do not match the modes the SECC enabled "
                                             "0x00800008, missing: volt var, extra: none";
constexpr auto ENABLED_MODES_MISMATCH_NEEDLE = "do not match the modes the SECC enabled";

// Stands in for an older control set the charge parameter discovery would have recorded.
constexpr std::uint64_t STALE_UPDATE_TIME = 1;

// The logging callback is process global, so it has to be uninstalled before the buffer it writes into dies.
class LoggingCapture {
public:
    explicit LoggingCapture(std::vector<std::pair<LogLevel, std::string>>& sink) {
        io::set_logging_callback(
            [&sink](LogLevel level, std::string message) { sink.emplace_back(level, std::move(message)); });
    }
    LoggingCapture(const LoggingCapture&) = delete;
    LoggingCapture& operator=(const LoggingCapture&) = delete;
    ~LoggingCapture() {
        io::set_logging_callback([](LogLevel, std::string) {});
    }
};

size_t count_lines(const std::vector<std::pair<LogLevel, std::string>>& lines, LogLevel level, const char* needle) {
    return static_cast<size_t>(
        std::count_if(lines.begin(), lines.end(), [level, needle](const std::pair<LogLevel, std::string>& entry) {
            return entry.first == level and entry.second.find(needle) != std::string::npos;
        }));
}

d20::AcTransferLimits make_ac_limits() {
    d20::AcTransferLimits limits{};
    limits.charge_power = {{22, 3}, {10, 0}};
    limits.nominal_frequency = {50, 0};
    return limits;
}

d20::SaeDerTransferLimits make_sae_limits() {
    d20::SaeDerTransferLimits limits{};
    limits.max_discharge_power = {-11, 3};
    return limits;
}

d20::DerSaeSetupConfig make_permit_service_config() {
    auto der_control = d20::get_default_sae_der_control(NOMINAL_VOLTAGE_V);
    der_control.enter_service.permit_service = true;
    return d20::DerSaeSetupConfig{std::move(der_control), sae::RequiredDEROperatingMode::GridFollowing,
                                  sae::GridConnectionMode::GridConnected};
}

d20::EvseSetupConfig make_evse_setup() {
    d20::EvseSetupConfig setup{};
    setup.evse_id = "everest se";
    setup.supported_energy_services = {dt::ServiceCategory::AC_DER_SAE};
    setup.authorization_services = {dt::Authorization::EIM};
    setup.supported_vas_services = {};
    setup.enable_certificate_install_service = false;
    setup.dc_limits = {};
    setup.ac_limits = make_ac_limits();
    setup.der_iec_limits = std::nullopt;
    setup.der_sae_limits = make_sae_limits();
    setup.control_mobility_modes = {{dt::ControlMode::Scheduled, dt::MobilityNeedsMode::ProvidedByEvcc}};
    setup.der_sae_setup_config = make_permit_service_config();
    setup.powersupply_limits = {};
    return setup;
}

d20::Session make_session(dt::ControlMode control_mode, dt::MobilityNeedsMode mobility_needs_mode,
                          std::uint32_t ev_supported_modes) {
    const d20::SelectedServiceParameters service_parameters(dt::ServiceCategory::AC_DER_SAE,
                                                            dt::AcConnector::ThreePhase, control_mode,
                                                            mobility_needs_mode, dt::Pricing::NoPricing, 230);
    d20::Session session{service_parameters};
    session.set_ev_supported_sae_functions(ev_supported_modes);
    return session;
}

message_20::DER_SAE_AC_ChargeLoopRequest make_scheduled_request(const std::array<std::uint8_t, 8>& session_id) {
    message_20::DER_SAE_AC_ChargeLoopRequest req{};
    req.header.session_id = session_id;
    req.header.timestamp = 1691411798;
    req.meter_info_requested = false;

    auto& req_mode = req.control_mode.emplace<Scheduled_DER_Req>();
    req_mode.present_active_power = {11, 3};
    req_mode.present_voltage = {230, 0};
    req_mode.present_frequency = {50, 0};
    return req;
}

message_20::DER_SAE_AC_ChargeLoopRequest make_dynamic_request(const std::array<std::uint8_t, 8>& session_id) {
    message_20::DER_SAE_AC_ChargeLoopRequest req{};
    req.header.session_id = session_id;
    req.header.timestamp = 1691411798;
    req.meter_info_requested = false;

    auto& req_mode = req.control_mode.emplace<Dynamic_DER_Req>();
    req_mode.present_active_power = {11, 3};
    req_mode.max_charge_power = {11, 3};
    req_mode.min_charge_power = {4, 0};
    req_mode.present_reactive_power = {10, 0};
    req_mode.maximum_discharge_power = {-11, 3};
    req_mode.minimum_discharge_power = {-4, 0};
    req_mode.present_voltage = {230, 0};
    req_mode.present_frequency = {50, 0};
    return req;
}

message_20::PowerDeliveryRequest make_power_delivery_request(const std::array<std::uint8_t, 8>& session_id,
                                                             dt::Progress progress) {
    message_20::PowerDeliveryRequest req{};
    req.header.session_id = session_id;
    req.header.timestamp = 1691411799;
    req.processing = dt::Processing::Finished;
    req.charge_progress = progress;
    return req;
}

size_t count_signals(const std::vector<session::feedback::Signal>& signals, session::feedback::Signal wanted) {
    return static_cast<size_t>(std::count(signals.begin(), signals.end(), wanted));
}

template <typename Mode> bool holds_control_mode(const session::feedback::AcChargeLoopReq& entry) {
    if (not std::holds_alternative<session::feedback::AcReqControlMode>(entry)) {
        return false;
    }
    return std::holds_alternative<Mode>(std::get<session::feedback::AcReqControlMode>(entry));
}

} // namespace

SCENARIO("ISO15118-20 der sae ac charge loop state transitions") {

    // Declared before the state helper so the capture guard below is destroyed while this is still alive.
    std::vector<std::pair<LogLevel, std::string>> log_lines;

    const auto evse_setup = make_evse_setup();

    std::vector<session::feedback::Signal> signals;
    std::vector<session::feedback::AcChargeLoopReq> charge_loop_reqs;

    session::feedback::Callbacks callbacks{};
    callbacks.signal = [&signals](session::feedback::Signal signal) { signals.push_back(signal); };
    callbacks.ac_charge_loop_req = [&charge_loop_reqs](const session::feedback::AcChargeLoopReq& entry) {
        charge_loop_reqs.push_back(entry);
    };

    std::optional<d20::PauseContext> pause_ctx{std::nullopt};

    auto state_helper = FsmStateHelper(d20::SessionConfig(evse_setup), pause_ctx, callbacks);

    // Installed after FsmStateHelper, which installs a printing callback of its own.
    LoggingCapture capture{log_lines};

    auto ctx = state_helper.get_context();

    const auto update_time = ctx.session_config.der_sae_setup_config.value().der_control_update_time;
    REQUIRE(update_time != STALE_UPDATE_TIME);

    GIVEN("Bad case - unknown session") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_SAE_ChargeLoop>()};

        ctx.session =
            make_session(dt::ControlMode::Scheduled, dt::MobilityNeedsMode::ProvidedByEvcc, CHARGE_AND_DISCHARGE_ONLY);

        state_helper.handle_request(make_scheduled_request(d20::Session().get_id()));
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("ResponseCode: FAILED_UnknownSession and the session is stopped") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::AC_DER_SAE_ChargeLoop);
            REQUIRE(ctx.session_stopped == true);

            const auto res = ctx.get_response<ChargeLoopResponse>();
            REQUIRE(res.has_value());
            REQUIRE(res.value().response_code == dt::ResponseCode::FAILED_UnknownSession);
        }

        THEN("No charge loop feedback is dispatched for a rejected request") {
            REQUIRE(charge_loop_reqs.empty());
        }
    }

    GIVEN("Bad case - a scheduled request while dynamic mode was negotiated") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_SAE_ChargeLoop>()};

        ctx.session =
            make_session(dt::ControlMode::Dynamic, dt::MobilityNeedsMode::ProvidedByEvcc, CHARGE_AND_DISCHARGE_ONLY);
        ctx.session.record_der_control_sent(STALE_UPDATE_TIME);

        state_helper.handle_request(make_scheduled_request(ctx.session.get_id()));
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("ResponseCode: FAILED and the session is stopped") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::AC_DER_SAE_ChargeLoop);
            REQUIRE(ctx.session_stopped == true);

            const auto res = ctx.get_response<ChargeLoopResponse>();
            REQUIRE(res.has_value());
            REQUIRE(res.value().response_code == dt::ResponseCode::FAILED);
        }

        THEN("A rejected loop leaves the recorded control set at the older update time") {
            REQUIRE(ctx.session.der_control_changed_since_cpd(STALE_UPDATE_TIME) == false);
            REQUIRE(ctx.session.der_control_changed_since_cpd(update_time) == true);
        }

        THEN("No charge loop feedback is dispatched") {
            REQUIRE(charge_loop_reqs.empty());
        }
    }

    GIVEN("Bad case - an unexpected message type") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_SAE_ChargeLoop>()};

        ctx.session =
            make_session(dt::ControlMode::Scheduled, dt::MobilityNeedsMode::ProvidedByEvcc, CHARGE_AND_DISCHARGE_ONLY);

        message_20::SessionSetupRequest req{};
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;
        req.evccid = "WMIV1234567890ABCDEX";

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("ResponseCode: FAILED_SequenceError and the session is stopped") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::AC_DER_SAE_ChargeLoop);
            REQUIRE(ctx.session_stopped == true);

            const auto res = ctx.get_response<message_20::SessionSetupResponse>();
            REQUIRE(res.has_value());
            REQUIRE(res.value().response_code == dt::ResponseCode::FAILED_SequenceError);
        }
    }

    GIVEN("Good case - a scheduled request after the charge parameter discovery ran") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_SAE_ChargeLoop>()};

        ctx.session =
            make_session(dt::ControlMode::Scheduled, dt::MobilityNeedsMode::ProvidedByEvcc, WITH_ENTER_SERVICE);
        ctx.session.record_der_control_sent(update_time);

        d20::AcTargetPower target_power{};
        state_helper.set_active_control_event(target_power);
        fsm.feed(d20::Event::CONTROL_MESSAGE);
        d20::AcPresentPower present_power{};
        present_power.present_active_power = dt::RationalNumber{12, 3};
        state_helper.set_active_control_event(present_power);
        fsm.feed(d20::Event::CONTROL_MESSAGE);

        state_helper.handle_request(make_scheduled_request(ctx.session.get_id()));
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("ResponseCode: OK, the state is kept and the scheduled control mode is answered") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::AC_DER_SAE_ChargeLoop);
            REQUIRE(ctx.session_stopped == false);

            const auto res = ctx.get_response<ChargeLoopResponse>();
            REQUIRE(res.has_value());
            REQUIRE(res.value().response_code == dt::ResponseCode::OK);
            REQUIRE(std::holds_alternative<Scheduled_DER_Res>(res.value().control_mode));
        }

        THEN("The present active power taken from the control event is reported back") {
            const auto& res_mode =
                std::get<Scheduled_DER_Res>(ctx.get_response<ChargeLoopResponse>().value().control_mode);
            REQUIRE(dt::from_RationalNumber(res_mode.present_active_power.value()) == 12000.0f);
        }

        THEN("Only the enter service block of the DER control is sent") {
            const auto& der_control =
                std::get<Scheduled_DER_Res>(ctx.get_response<ChargeLoopResponse>().value().control_mode)
                    .der_control_cl_res;
            REQUIRE(der_control.enter_service_cl_res.permit_service == true);
            REQUIRE(der_control.voltage_trip.has_value() == false);
            REQUIRE(der_control.frequency_trip.has_value() == false);
            REQUIRE(der_control.reactive_power_support_cl_res.has_value() == false);
            REQUIRE(der_control.active_power_support_cl_res.has_value() == false);
        }

        THEN("The charge loop started signal is raised once") {
            REQUIRE(count_signals(signals, session::feedback::Signal::CHARGE_LOOP_STARTED) == 1);

            AND_THEN("It is not raised again on the following request") {
                state_helper.handle_request(make_scheduled_request(ctx.session.get_id()));
                fsm.feed(d20::Event::V2GTP_MESSAGE);

                REQUIRE(count_signals(signals, session::feedback::Signal::CHARGE_LOOP_STARTED) == 1);
            }
        }

        THEN("The control mode and the meter info request are dispatched") {
            REQUIRE(charge_loop_reqs.size() == 2);
            REQUIRE(holds_control_mode<Scheduled_DER_Req>(charge_loop_reqs.at(0)));
            REQUIRE(std::holds_alternative<session::feedback::MeterInfoRequested>(charge_loop_reqs.at(1)));
            REQUIRE(std::get<session::feedback::MeterInfoRequested>(charge_loop_reqs.at(1)) == false);

            AND_THEN("Display parameters are dispatched when the EV sends them") {
                auto req_with_display = make_scheduled_request(ctx.session.get_id());
                req_with_display.meter_info_requested = true;
                req_with_display.display_parameters.emplace().present_soc = 42;

                state_helper.handle_request(req_with_display);
                fsm.feed(d20::Event::V2GTP_MESSAGE);

                REQUIRE(charge_loop_reqs.size() == 5);
                REQUIRE(holds_control_mode<Scheduled_DER_Req>(charge_loop_reqs.at(2)));
                REQUIRE(std::get<session::feedback::MeterInfoRequested>(charge_loop_reqs.at(3)) == true);
                REQUIRE(std::holds_alternative<dt::DisplayParameters>(charge_loop_reqs.at(4)));
                REQUIRE(std::get<dt::DisplayParameters>(charge_loop_reqs.at(4)).present_soc.value_or(0) == 42);
            }
        }
    }

    GIVEN("Good case - a dynamic request") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_SAE_ChargeLoop>()};

        ctx.session = make_session(dt::ControlMode::Dynamic, dt::MobilityNeedsMode::ProvidedByEvcc, WITH_ENTER_SERVICE);
        ctx.session.record_der_control_sent(update_time);

        d20::AcTargetPower target_power{};
        target_power.target_active_power = dt::RationalNumber{10, 3};
        state_helper.set_active_control_event(target_power);
        fsm.feed(d20::Event::CONTROL_MESSAGE);

        state_helper.handle_request(make_dynamic_request(ctx.session.get_id()));
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("ResponseCode: OK and the dynamic control mode carries the target active power") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::AC_DER_SAE_ChargeLoop);

            const auto res = ctx.get_response<ChargeLoopResponse>();
            REQUIRE(res.has_value());
            REQUIRE(res.value().response_code == dt::ResponseCode::OK);
            REQUIRE(std::holds_alternative<Dynamic_DER_Res>(res.value().control_mode));

            const auto& res_mode = std::get<Dynamic_DER_Res>(res.value().control_mode);
            REQUIRE(dt::from_RationalNumber(res_mode.target_active_power) == 10000.0f);
        }

        THEN("The dynamic control mode is dispatched as feedback") {
            REQUIRE(charge_loop_reqs.size() == 2);
            REQUIRE(holds_control_mode<Dynamic_DER_Req>(charge_loop_reqs.at(0)));
        }
    }

    GIVEN("Good case - dynamic mode parameters from the charger") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_SAE_ChargeLoop>()};

        ctx.session = make_session(dt::ControlMode::Dynamic, dt::MobilityNeedsMode::ProvidedBySecc, WITH_ENTER_SERVICE);
        ctx.session.record_der_control_sent(update_time);

        d20::AcTargetPower target_power{};
        target_power.target_active_power = dt::RationalNumber{10, 3};
        state_helper.set_active_control_event(target_power);
        fsm.feed(d20::Event::CONTROL_MESSAGE);
        const d20::UpdateDynamicModeParameters dynamic_parameters{std::time(nullptr) + 40, 95, 80};
        state_helper.set_active_control_event(dynamic_parameters);
        fsm.feed(d20::Event::CONTROL_MESSAGE);

        state_helper.handle_request(make_dynamic_request(ctx.session.get_id()));
        fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("The parameters taken from the control event are answered") {
            const auto res = ctx.get_response<ChargeLoopResponse>();
            REQUIRE(res.has_value());
            REQUIRE(res.value().response_code == dt::ResponseCode::OK);

            const auto& res_mode = std::get<Dynamic_DER_Res>(res.value().control_mode);
            REQUIRE(res_mode.departure_time.value_or(0) >= 39);
            REQUIRE(res_mode.target_soc.value_or(0) == 95);
            REQUIRE(res_mode.minimum_soc.value_or(0) == 80);
            REQUIRE(res_mode.ack_max_delay.value_or(0) == 30);
        }
    }

    GIVEN("Good case - a charge loop entered with cached powers and parameters") {
        ctx.cache_ac_target_power = d20::AcTargetPower{};
        ctx.cache_ac_target_power.value().target_active_power = dt::RationalNumber{13, 3};
        ctx.cache_ac_present_power = d20::AcPresentPower{};
        ctx.cache_ac_present_power.value().present_active_power = dt::RationalNumber{14, 3};
        ctx.cache_dynamic_mode_parameters = d20::UpdateDynamicModeParameters{std::nullopt, 91, 81};

        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_SAE_ChargeLoop>()};

        ctx.session = make_session(dt::ControlMode::Dynamic, dt::MobilityNeedsMode::ProvidedBySecc, WITH_ENTER_SERVICE);
        ctx.session.record_der_control_sent(update_time);

        state_helper.handle_request(make_dynamic_request(ctx.session.get_id()));
        fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("The cache is restored on entry instead of starting from zero") {
            const auto res = ctx.get_response<ChargeLoopResponse>();
            REQUIRE(res.has_value());
            REQUIRE(res.value().response_code == dt::ResponseCode::OK);

            const auto& res_mode = std::get<Dynamic_DER_Res>(res.value().control_mode);
            REQUIRE(dt::from_RationalNumber(res_mode.target_active_power) == 13000.0f);
            REQUIRE(dt::from_RationalNumber(res_mode.present_active_power.value()) == 14000.0f);
            REQUIRE(res_mode.target_soc.value_or(0) == 91);
            REQUIRE(res_mode.minimum_soc.value_or(0) == 81);
        }
    }

    GIVEN("Good case - a dynamic request while the charger pauses") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_SAE_ChargeLoop>()};

        ctx.session = make_session(dt::ControlMode::Dynamic, dt::MobilityNeedsMode::ProvidedByEvcc, WITH_ENTER_SERVICE);
        ctx.session.record_der_control_sent(update_time);

        state_helper.set_active_control_event(d20::PauseCharging{true});
        fsm.feed(d20::Event::CONTROL_MESSAGE);
        d20::AcTargetPower target_power{};
        target_power.target_active_power = dt::RationalNumber{10, 3};
        state_helper.set_active_control_event(target_power);
        fsm.feed(d20::Event::CONTROL_MESSAGE);

        state_helper.handle_request(make_dynamic_request(ctx.session.get_id()));
        fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("A pause notification is sent") {
            const auto res = ctx.get_response<ChargeLoopResponse>();
            REQUIRE(res.has_value());
            REQUIRE(res.value().response_code == dt::ResponseCode::OK);
            REQUIRE(res.value().status.has_value());
            REQUIRE(res.value().status.value().notification == dt::EvseNotification::Pause);
            REQUIRE(res.value().status.value().notification_max_delay == 60);
        }
    }

    GIVEN("Good case - a dynamic request while the charger stops") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_SAE_ChargeLoop>()};

        ctx.session = make_session(dt::ControlMode::Dynamic, dt::MobilityNeedsMode::ProvidedByEvcc, WITH_ENTER_SERVICE);
        ctx.session.record_der_control_sent(update_time);

        state_helper.set_active_control_event(d20::StopCharging{true});
        fsm.feed(d20::Event::CONTROL_MESSAGE);
        d20::AcTargetPower target_power{};
        target_power.target_active_power = dt::RationalNumber{10, 3};
        state_helper.set_active_control_event(target_power);
        fsm.feed(d20::Event::CONTROL_MESSAGE);

        state_helper.handle_request(make_dynamic_request(ctx.session.get_id()));
        fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("A terminate notification is sent") {
            const auto res = ctx.get_response<ChargeLoopResponse>();
            REQUIRE(res.has_value());
            REQUIRE(res.value().response_code == dt::ResponseCode::OK);
            REQUIRE(res.value().status.has_value());
            REQUIRE(res.value().status.value().notification == dt::EvseNotification::Terminate);
            REQUIRE(res.value().status.value().notification_max_delay == 0);
        }
    }

    GIVEN("Good case - a non zero DERAlarmStatus") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_SAE_ChargeLoop>()};

        ctx.session =
            make_session(dt::ControlMode::Scheduled, dt::MobilityNeedsMode::ProvidedByEvcc, CHARGE_AND_DISCHARGE_ONLY);
        ctx.session.record_der_control_sent(update_time);

        auto req = make_scheduled_request(ctx.session.get_id());
        std::get<Scheduled_DER_Req>(req.control_mode).der_alarm_status = 0x00000004u;

        state_helper.handle_request(req);
        fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("The session survives and the alarm is warned about once") {
            const auto res = ctx.get_response<ChargeLoopResponse>();
            REQUIRE(res.has_value());
            REQUIRE(res.value().response_code == dt::ResponseCode::OK);
            REQUIRE(ctx.session_stopped == false);
            REQUIRE(count_lines(log_lines, LogLevel::Warning, DER_ALARM_STATUS_LINE) == 1);

            AND_THEN("An unchanged alarm is not warned about again") {
                state_helper.handle_request(req);
                fsm.feed(d20::Event::V2GTP_MESSAGE);
                REQUIRE(count_lines(log_lines, LogLevel::Warning, DER_ALARM_STATUS_LINE) == 1);

                AND_THEN("A cleared alarm is silent but re-arms the report") {
                    auto cleared_req = make_scheduled_request(ctx.session.get_id());
                    std::get<Scheduled_DER_Req>(cleared_req.control_mode).der_alarm_status = 0;
                    state_helper.handle_request(cleared_req);
                    fsm.feed(d20::Event::V2GTP_MESSAGE);
                    REQUIRE(count_lines(log_lines, LogLevel::Warning, DER_ALARM_STATUS_LINE) == 1);

                    AND_THEN("The recurring alarm is warned about again") {
                        state_helper.handle_request(req);
                        fsm.feed(d20::Event::V2GTP_MESSAGE);
                        REQUIRE(count_lines(log_lines, LogLevel::Warning, DER_ALARM_STATUS_LINE) == 2);
                    }
                }
            }
        }
    }

    GIVEN("Good case - EnabledModes that match the enabled modes exactly") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_SAE_ChargeLoop>()};

        ctx.session = make_session(dt::ControlMode::Scheduled, dt::MobilityNeedsMode::ProvidedByEvcc,
                                   CHARGE_AND_DISCHARGE_ONLY | SECC_ENABLED_MODES);
        ctx.session.record_der_control_sent(update_time);
        ctx.session.set_enabled_der_control_modes(SECC_ENABLED_MODES);

        auto req = make_scheduled_request(ctx.session.get_id());
        std::get<Scheduled_DER_Req>(req.control_mode).enabled_modes = CHARGE_AND_DISCHARGE_ONLY | SECC_ENABLED_MODES;

        state_helper.handle_request(req);
        fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Nothing is warned about, the inherent charge and discharge bits are outside the comparison") {
            const auto res = ctx.get_response<ChargeLoopResponse>();
            REQUIRE(res.has_value());
            REQUIRE(res.value().response_code == dt::ResponseCode::OK);
            REQUIRE(count_lines(log_lines, LogLevel::Warning, ENABLED_MODES_MISMATCH_NEEDLE) == 0);
        }
    }

    GIVEN("Good case - EnabledModes that omit an enabled function") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_SAE_ChargeLoop>()};

        ctx.session = make_session(dt::ControlMode::Scheduled, dt::MobilityNeedsMode::ProvidedByEvcc,
                                   CHARGE_AND_DISCHARGE_ONLY | SECC_ENABLED_MODES);
        ctx.session.record_der_control_sent(update_time);
        ctx.session.set_enabled_der_control_modes(SECC_ENABLED_MODES);

        auto req = make_scheduled_request(ctx.session.get_id());
        std::get<Scheduled_DER_Req>(req.control_mode).enabled_modes =
            CHARGE_AND_DISCHARGE_ONLY | bit_of(sae::DerBitMapFunctions::EnterService);

        state_helper.handle_request(req);
        fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("The missing function is named once and the session survives") {
            const auto res = ctx.get_response<ChargeLoopResponse>();
            REQUIRE(res.has_value());
            REQUIRE(res.value().response_code == dt::ResponseCode::OK);
            REQUIRE(ctx.session_stopped == false);
            REQUIRE(count_lines(log_lines, LogLevel::Warning, ENABLED_MODES_MISMATCH_LINE) == 1);

            AND_THEN("A second charge loop iteration does not report it again") {
                state_helper.handle_request(req);
                fsm.feed(d20::Event::V2GTP_MESSAGE);
                REQUIRE(count_lines(log_lines, LogLevel::Warning, ENABLED_MODES_MISMATCH_LINE) == 1);
                REQUIRE(count_lines(log_lines, LogLevel::Warning, ENABLED_MODES_MISMATCH_NEEDLE) == 1);
            }
        }
    }

    GIVEN("Good case - EnabledModes with a bit this document does not use") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_SAE_ChargeLoop>()};

        ctx.session = make_session(dt::ControlMode::Scheduled, dt::MobilityNeedsMode::ProvidedByEvcc,
                                   CHARGE_AND_DISCHARGE_ONLY | SECC_ENABLED_MODES);
        ctx.session.record_der_control_sent(update_time);
        ctx.session.set_enabled_der_control_modes(SECC_ENABLED_MODES);

        auto req = make_scheduled_request(ctx.session.get_id());
        std::get<Scheduled_DER_Req>(req.control_mode).enabled_modes =
            CHARGE_AND_DISCHARGE_ONLY | SECC_ENABLED_MODES | UNUSED_BIT;

        state_helper.handle_request(req);
        fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("The unused bit is ignored and nothing is warned about") {
            const auto res = ctx.get_response<ChargeLoopResponse>();
            REQUIRE(res.has_value());
            REQUIRE(res.value().response_code == dt::ResponseCode::OK);
            REQUIRE(count_lines(log_lines, LogLevel::Warning, ENABLED_MODES_MISMATCH_NEEDLE) == 0);
        }
    }

    GIVEN("Good case - a mid loop DER config change re-sends the control set") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_SAE_ChargeLoop>()};

        ctx.session = make_session(dt::ControlMode::Scheduled, dt::MobilityNeedsMode::ProvidedByEvcc,
                                   CHARGE_AND_DISCHARGE_ONLY | SECC_ENABLED_MODES);
        // A stale update time is what a config change mid session looks like to the charge loop. The EV still
        // echoes the discovery era enabled modes, so this iteration has nothing to complain about.
        ctx.session.record_der_control_sent(STALE_UPDATE_TIME);
        ctx.session.set_enabled_der_control_modes(SECC_ENABLED_MODES);

        auto req = make_scheduled_request(ctx.session.get_id());
        std::get<Scheduled_DER_Req>(req.control_mode).enabled_modes = CHARGE_AND_DISCHARGE_ONLY | SECC_ENABLED_MODES;

        state_helper.handle_request(req);
        fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("The stored enabled modes follow the control set the charge loop just sent") {
            const auto res = ctx.get_response<ChargeLoopResponse>();
            REQUIRE(res.has_value());
            REQUIRE(res.value().response_code == dt::ResponseCode::OK);

            // Only permit_service is enabled in the config, so the re-sent enabled modes are the enter service bit.
            REQUIRE(ctx.session.get_enabled_der_control_modes() == bit_of(sae::DerBitMapFunctions::EnterService));
            REQUIRE(count_lines(log_lines, LogLevel::Warning, ENABLED_MODES_MISMATCH_NEEDLE) == 0);
        }

        THEN("EnabledModes that match the new enabled modes raise no mismatch on the next iteration") {
            auto updated = make_scheduled_request(ctx.session.get_id());
            std::get<Scheduled_DER_Req>(updated.control_mode).enabled_modes =
                CHARGE_AND_DISCHARGE_ONLY | bit_of(sae::DerBitMapFunctions::EnterService);

            state_helper.handle_request(updated);
            fsm.feed(d20::Event::V2GTP_MESSAGE);

            REQUIRE(count_lines(log_lines, LogLevel::Warning, ENABLED_MODES_MISMATCH_NEEDLE) == 0);
        }
    }

    GIVEN("Bad case - a dynamic request without a target active power") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_SAE_ChargeLoop>()};

        ctx.session =
            make_session(dt::ControlMode::Dynamic, dt::MobilityNeedsMode::ProvidedByEvcc, CHARGE_AND_DISCHARGE_ONLY);
        ctx.session.record_der_control_sent(update_time);

        state_helper.handle_request(make_dynamic_request(ctx.session.get_id()));
        fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("ResponseCode: FAILED instead of a 0 W target and the session is stopped") {
            const auto res = ctx.get_response<ChargeLoopResponse>();
            REQUIRE(res.has_value());
            REQUIRE(res.value().response_code == dt::ResponseCode::FAILED);
            REQUIRE(ctx.session_stopped == true);
            REQUIRE(count_lines(log_lines, LogLevel::Error, MISSING_TARGET_ACTIVE_POWER_LINE) == 1);
        }

        THEN("No charge loop feedback is dispatched") {
            REQUIRE(charge_loop_reqs.empty());
        }

        THEN("A recovered target active power is answered") {
            d20::AcTargetPower target_power{};
            target_power.target_active_power = dt::RationalNumber{10, 3};
            state_helper.set_active_control_event(target_power);
            fsm.feed(d20::Event::CONTROL_MESSAGE);
            state_helper.handle_request(make_dynamic_request(ctx.session.get_id()));
            fsm.feed(d20::Event::V2GTP_MESSAGE);

            const auto res = ctx.get_response<ChargeLoopResponse>();
            REQUIRE(res.has_value());
            REQUIRE(res.value().response_code == dt::ResponseCode::OK);
            REQUIRE(dt::from_RationalNumber(std::get<Dynamic_DER_Res>(res.value().control_mode).target_active_power) ==
                    10000.0f);
        }
    }

    GIVEN("Good case - a dynamic request from an EV that did not declare the target active power function") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_SAE_ChargeLoop>()};

        ctx.session =
            make_session(dt::ControlMode::Dynamic, dt::MobilityNeedsMode::ProvidedByEvcc, CHARGE_AND_DISCHARGE_ONLY);
        ctx.session.record_der_control_sent(update_time);

        d20::AcTargetPower target_power{};
        target_power.target_active_power = dt::RationalNumber{10, 3};
        state_helper.set_active_control_event(target_power);
        fsm.feed(d20::Event::CONTROL_MESSAGE);

        state_helper.handle_request(make_dynamic_request(ctx.session.get_id()));
        fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("The undeclared target active power function is reported once") {
            REQUIRE(ctx.get_response<ChargeLoopResponse>().value().response_code == dt::ResponseCode::OK);
            REQUIRE(count_lines(log_lines, LogLevel::Debug, UNDECLARED_TARGET_ACTIVE_POWER_LINE) == 1);

            AND_THEN("It is not reported again on the following request") {
                state_helper.handle_request(make_dynamic_request(ctx.session.get_id()));
                fsm.feed(d20::Event::V2GTP_MESSAGE);
                REQUIRE(count_lines(log_lines, LogLevel::Debug, UNDECLARED_TARGET_ACTIVE_POWER_LINE) == 1);
            }
        }
    }

    // The mid-session DER control update has no production trigger yet: the charge parameter discovery marks
    // the control set as delivered and the setup config is never mutated afterwards. The shape below is therefore
    // driven by a session that never ran the charge parameter discovery.
    GIVEN("A session whose DER control set was never delivered") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_SAE_ChargeLoop>()};

        ctx.session =
            make_session(dt::ControlMode::Scheduled, dt::MobilityNeedsMode::ProvidedByEvcc, WITH_ENTER_SERVICE);
        REQUIRE(ctx.session.der_control_changed_since_cpd(update_time) == true);

        state_helper.handle_request(make_scheduled_request(ctx.session.get_id()));
        fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("All four optional DER control blocks are emitted") {
            const auto first = ctx.get_response<ChargeLoopResponse>();
            REQUIRE(first.has_value());
            REQUIRE(first.value().response_code == dt::ResponseCode::OK);

            const auto& first_der_control = std::get<Scheduled_DER_Res>(first.value().control_mode).der_control_cl_res;
            REQUIRE(first_der_control.voltage_trip.has_value() == true);
            REQUIRE(first_der_control.frequency_trip.has_value() == true);
            REQUIRE(first_der_control.reactive_power_support_cl_res.has_value() == true);
            REQUIRE(first_der_control.active_power_support_cl_res.has_value() == true);

            AND_THEN("The control set is recorded and the following request emits none of them") {
                REQUIRE(ctx.session.der_control_changed_since_cpd(update_time) == false);

                state_helper.handle_request(make_scheduled_request(ctx.session.get_id()));
                fsm.feed(d20::Event::V2GTP_MESSAGE);

                const auto& second_der_control =
                    std::get<Scheduled_DER_Res>(ctx.get_response<ChargeLoopResponse>().value().control_mode)
                        .der_control_cl_res;
                REQUIRE(second_der_control.voltage_trip.has_value() == false);
                REQUIRE(second_der_control.frequency_trip.has_value() == false);
                REQUIRE(second_der_control.reactive_power_support_cl_res.has_value() == false);
                REQUIRE(second_der_control.active_power_support_cl_res.has_value() == false);
            }
        }
    }

    GIVEN("The EV ends the charge loop with a power delivery request") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_SAE_ChargeLoop>()};

        ctx.session =
            make_session(dt::ControlMode::Scheduled, dt::MobilityNeedsMode::ProvidedByEvcc, WITH_ENTER_SERVICE);

        state_helper.handle_request(make_power_delivery_request(ctx.session.get_id(), dt::Progress::Stop));
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("The session stop state is entered directly") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::SessionStop);
            REQUIRE(ctx.session_stopped == false);

            const auto res = ctx.get_response<message_20::PowerDeliveryResponse>();
            REQUIRE(res.has_value());
            REQUIRE(res.value().response_code == dt::ResponseCode::OK);
        }

        THEN("The charge loop is finished and the contactor is opened") {
            REQUIRE(count_signals(signals, session::feedback::Signal::CHARGE_LOOP_FINISHED) == 1);
            REQUIRE(count_signals(signals, session::feedback::Signal::AC_OPEN_CONTACTOR) == 1);
        }
    }

    GIVEN("The charger requested a shutdown before the power delivery request") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_SAE_ChargeLoop>()};

        ctx.session =
            make_session(dt::ControlMode::Scheduled, dt::MobilityNeedsMode::ProvidedByEvcc, WITH_ENTER_SERVICE);
        ctx.request_shutdown();

        state_helper.handle_request(make_power_delivery_request(ctx.session.get_id(), dt::Progress::Start));
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("The EV is told to terminate and the session stop state is entered") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::SessionStop);

            const auto res = ctx.get_response<message_20::PowerDeliveryResponse>();
            REQUIRE(res.has_value());
            REQUIRE(res.value().response_code == dt::ResponseCode::OK);
            REQUIRE(res.value().status.has_value());
            REQUIRE(res.value().status.value().notification == dt::EvseNotification::Terminate);
            REQUIRE(res.value().status.value().notification_max_delay == 0);
        }

        THEN("The charge loop is finished and the contactor is opened") {
            REQUIRE(count_signals(signals, session::feedback::Signal::CHARGE_LOOP_FINISHED) == 1);
            REQUIRE(count_signals(signals, session::feedback::Signal::AC_OPEN_CONTACTOR) == 1);
        }
    }

    GIVEN("The EV keeps the power flowing with a power delivery request") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_SAE_ChargeLoop>()};

        ctx.session =
            make_session(dt::ControlMode::Scheduled, dt::MobilityNeedsMode::ProvidedByEvcc, WITH_ENTER_SERVICE);

        state_helper.handle_request(make_power_delivery_request(ctx.session.get_id(), dt::Progress::Start));
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("The charge loop is kept and no teardown signal is raised") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::AC_DER_SAE_ChargeLoop);
            REQUIRE(ctx.session_stopped == false);

            const auto res = ctx.get_response<message_20::PowerDeliveryResponse>();
            REQUIRE(res.has_value());
            REQUIRE(res.value().response_code == dt::ResponseCode::OK);
            REQUIRE(res.value().status.has_value() == false);

            REQUIRE(count_signals(signals, session::feedback::Signal::CHARGE_LOOP_FINISHED) == 0);
            REQUIRE(count_signals(signals, session::feedback::Signal::AC_OPEN_CONTACTOR) == 0);
        }
    }

    GIVEN("Bad case - a power delivery request for an unknown session") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_SAE_ChargeLoop>()};

        ctx.session =
            make_session(dt::ControlMode::Scheduled, dt::MobilityNeedsMode::ProvidedByEvcc, WITH_ENTER_SERVICE);

        state_helper.handle_request(make_power_delivery_request(d20::Session().get_id(), dt::Progress::Stop));
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("ResponseCode: FAILED_UnknownSession, the state is kept and the session is stopped") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::AC_DER_SAE_ChargeLoop);
            REQUIRE(ctx.session_stopped == true);

            const auto res = ctx.get_response<message_20::PowerDeliveryResponse>();
            REQUIRE(res.has_value());
            REQUIRE(res.value().response_code == dt::ResponseCode::FAILED_UnknownSession);
        }

        THEN("The contactor is not opened on a rejected request") {
            REQUIRE(count_signals(signals, session::feedback::Signal::CHARGE_LOOP_FINISHED) == 0);
            REQUIRE(count_signals(signals, session::feedback::Signal::AC_OPEN_CONTACTOR) == 0);
        }
    }

    // Byte pinning against the EXI fixtures is impossible here: the response header carries the randomly
    // generated session id and the current time. Every GIVEN above is already an implicit encode test because
    // respond() serializes, so the only thing this adds is decode coverage.
    GIVEN("A dynamic response the EXI codec has to carry") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_SAE_ChargeLoop>()};

        ctx.session = make_session(dt::ControlMode::Dynamic, dt::MobilityNeedsMode::ProvidedByEvcc, WITH_ENTER_SERVICE);
        ctx.session.record_der_control_sent(update_time);

        d20::AcTargetPower target_power{};
        target_power.target_active_power = dt::RationalNumber{10, 3};
        state_helper.set_active_control_event(target_power);
        fsm.feed(d20::Event::CONTROL_MESSAGE);

        state_helper.handle_request(make_dynamic_request(ctx.session.get_id()));
        fsm.feed(d20::Event::V2GTP_MESSAGE);

        const auto res = ctx.get_response<ChargeLoopResponse>();
        REQUIRE(res.has_value());

        const auto bytes = serialize_helper(res.value());
        const io::StreamInputView stream_view{bytes.data(), bytes.size()};
        message_20::Variant variant(io::v2gtp::PayloadType::Part20DerSae, stream_view);

        THEN("The round tripped response matches the one the state machine produced") {
            REQUIRE(variant.get_type() == message_20::Type::DER_SAE_AC_ChargeLoopRes);
            const auto& round_tripped = variant.get<ChargeLoopResponse>();

            REQUIRE(round_tripped.header.session_id == ctx.session.get_id());
            REQUIRE(round_tripped.header.timestamp == res.value().header.timestamp);
            REQUIRE(round_tripped.response_code == dt::ResponseCode::OK);
            REQUIRE(round_tripped.status.has_value() == false);
            REQUIRE(round_tripped.meter_info.has_value() == false);
            REQUIRE(round_tripped.receipt.has_value() == false);
            REQUIRE(round_tripped.target_frequency.has_value() == false);
            REQUIRE(std::holds_alternative<Dynamic_DER_Res>(round_tripped.control_mode));

            const auto& mode = std::get<Dynamic_DER_Res>(round_tripped.control_mode);
            REQUIRE(dt::from_RationalNumber(mode.target_active_power) == 10000.0f);
            REQUIRE(mode.der_control_cl_res.enter_service_cl_res.permit_service == true);
            REQUIRE(mode.der_control_cl_res.voltage_trip.has_value() == false);
            REQUIRE(mode.der_control_cl_res.frequency_trip.has_value() == false);
            REQUIRE(mode.departure_time.has_value() == false);
            REQUIRE(mode.target_soc.has_value() == false);
            REQUIRE(mode.minimum_soc.has_value() == false);
            REQUIRE(mode.ack_max_delay.has_value() == false);
            REQUIRE(dt::from_RationalNumber(mode.evse_maximum_charge_power.value()) == 22000.0f);
            REQUIRE(dt::from_RationalNumber(mode.evse_maximum_discharge_power.value()) == -11000.0f);
            REQUIRE(mode.required_der_operating_mode.has_value() == false);
        }
    }
}
