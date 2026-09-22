// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d20/state/ac_charge_loop.hpp>

#include <iso15118/message/ac_charge_loop.hpp>

#include <iso15118/d20/state/power_delivery.hpp>
#include <iso15118/d20/state/session_stop.hpp>
#include <iso15118/detail/d20/context_helper.hpp>
#include <iso15118/detail/d20/state/ac_charge_loop.hpp>
#include <iso15118/detail/d20/state/power_delivery.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d20::state {

// Secc performance timer for PowerDelivery is 1.5s.
// 100ms is resevered for polling timeout and sending the response.
constexpr uint32_t AC_OPEN_CONTACTOR_TIMEOUT = 1400;

namespace dt = message_20::datatypes;

using Scheduled_AC_Req = dt::Scheduled_AC_CLReqControlMode;
using Scheduled_BPT_AC_Req = dt::BPT_Scheduled_AC_CLReqControlMode;
using Dynamic_AC_Req = dt::Dynamic_AC_CLReqControlMode;
using Dynamic_BPT_AC_Req = dt::BPT_Dynamic_AC_CLReqControlMode;

using Scheduled_AC_Res = dt::Scheduled_AC_CLResControlMode;
using Scheduled_BPT_AC_Res = dt::BPT_Scheduled_AC_CLResControlMode;
using Dynamic_AC_Res = dt::Dynamic_AC_CLResControlMode;
using Dynamic_BPT_AC_Res = dt::BPT_Dynamic_AC_CLResControlMode;

template <typename Out> void convert(Out& out, const AcTargetPower& targets, const d20::AcPresentPower& present_power);

template <>
void convert(Scheduled_AC_Res& out, const AcTargetPower& targets, const d20::AcPresentPower& present_power) {
    out.target_active_power = targets.target_active_power;
    out.target_active_power_L2 = targets.target_active_power_L2;
    out.target_active_power_L3 = targets.target_active_power_L3;
    out.target_reactive_power = targets.target_reactive_power;
    out.target_reactive_power_L2 = targets.target_reactive_power_L2;
    out.target_reactive_power_L3 = targets.target_reactive_power_L3;
    out.present_active_power = present_power.present_active_power;
    out.present_active_power_L2 = present_power.present_active_power_L2;
    out.present_active_power_L3 = present_power.present_active_power_L3;
}

template <>
void convert(Scheduled_BPT_AC_Res& out, const AcTargetPower& targets, const d20::AcPresentPower& present_power) {
    convert(static_cast<Scheduled_AC_Res&>(out), targets, present_power);
}

template <> void convert(Dynamic_AC_Res& out, const AcTargetPower& targets, const d20::AcPresentPower& present_power) {
    out.target_active_power =
        targets.target_active_power.value_or(dt::RationalNumber{0, 0}); // 0kW if no value is available
    out.target_active_power_L2 = targets.target_active_power_L2;
    out.target_active_power_L3 = targets.target_active_power_L3;
    out.target_reactive_power = targets.target_reactive_power;
    out.target_reactive_power_L2 = targets.target_reactive_power_L2;
    out.target_reactive_power_L3 = targets.target_reactive_power_L3;
    out.present_active_power = present_power.present_active_power;
    out.present_active_power_L2 = present_power.present_active_power_L2;
    out.present_active_power_L3 = present_power.present_active_power_L3;
}

template <>
void convert(Dynamic_BPT_AC_Res& out, const AcTargetPower& targets, const d20::AcPresentPower& present_power) {
    convert(static_cast<Dynamic_AC_Res&>(out), targets, present_power);
}

// TODO(sl): Refactor with DcChargeLoop state
namespace {
template <typename T>
void set_dynamic_parameters_in_res(T& res_mode, const UpdateDynamicModeParameters& parameters,
                                   uint64_t header_timestamp) {
    res_mode.departure_time = departure_time_offset(parameters.departure_time, header_timestamp);
    res_mode.target_soc = parameters.target_soc;

    // [V2G20-1366]
    if (parameters.min_soc.has_value() and parameters.target_soc.has_value() and
        parameters.min_soc.value() <= parameters.target_soc.value()) {
        res_mode.minimum_soc = parameters.min_soc;
    }
    res_mode.ack_max_delay = 30; // TODO(sl) what to send here and define 30 seconds as const
}
} // namespace

message_20::AC_ChargeLoopResponse
handle_request(const message_20::AC_ChargeLoopRequest& req, const d20::Session& session, bool stop, bool pause,
               std::optional<float> target_frequency, const AcTargetPower& target_powers,
               const AcPresentPower& present_powers, const UpdateDynamicModeParameters& dynamic_parameters) {

    message_20::AC_ChargeLoopResponse res;

    if (not validate_and_setup_header(res.header, session, req.header.session_id)) {
        return response_with_code(res, dt::ResponseCode::FAILED_UnknownSession);
    }

    const auto& selected_services = session.get_selected_services();
    const auto selected_control_mode = selected_services.selected_control_mode;
    const auto selected_energy_service = selected_services.selected_energy_service;
    const auto selected_mobility_needs_mode = selected_services.selected_mobility_needs_mode;

    if (std::holds_alternative<Scheduled_AC_Req>(req.control_mode)) {

        // If the ev sends a false control mode or a false energy service other than the previous selected ones, then
        // the charger should terminate the session
        if (selected_control_mode != dt::ControlMode::Scheduled or selected_energy_service != dt::ServiceCategory::AC) {
            return response_with_code(res, dt::ResponseCode::FAILED);
        }

        auto& res_mode = res.control_mode.emplace<Scheduled_AC_Res>();
        convert(res_mode, target_powers, present_powers);

    } else if (std::holds_alternative<Scheduled_BPT_AC_Req>(req.control_mode)) {

        // If the ev sends a false control mode or a false energy service other than the previous selected ones, then
        // the charger should terminate the session
        if (selected_control_mode != dt::ControlMode::Scheduled or
            selected_energy_service != dt::ServiceCategory::AC_BPT) {
            return response_with_code(res, dt::ResponseCode::FAILED);
        }

        auto& res_mode = res.control_mode.emplace<Scheduled_BPT_AC_Res>();
        convert(res_mode, target_powers, present_powers);

    } else if (std::holds_alternative<Dynamic_AC_Req>(req.control_mode)) {

        // If the ev sends a false control mode or a false energy service other than the previous selected ones, then
        // the charger should terminate the session
        if (selected_control_mode != dt::ControlMode::Dynamic or selected_energy_service != dt::ServiceCategory::AC) {
            return response_with_code(res, dt::ResponseCode::FAILED);
        }

        auto& res_mode = res.control_mode.emplace<Dynamic_AC_Res>();
        convert(res_mode, target_powers, present_powers);

        if (selected_mobility_needs_mode == dt::MobilityNeedsMode::ProvidedBySecc) {
            set_dynamic_parameters_in_res(res_mode, dynamic_parameters, res.header.timestamp);
        }

    } else if (std::holds_alternative<Dynamic_BPT_AC_Req>(req.control_mode)) {

        // If the ev sends a false control mode or a false energy service other than the previous selected ones, then
        // the charger should terminate the session
        if (selected_control_mode != dt::ControlMode::Dynamic or
            selected_energy_service != dt::ServiceCategory::AC_BPT) {
            return response_with_code(res, dt::ResponseCode::FAILED);
        }

        auto& res_mode = res.control_mode.emplace<Dynamic_BPT_AC_Res>();
        convert(res_mode, target_powers, present_powers);

        if (selected_mobility_needs_mode == dt::MobilityNeedsMode::ProvidedBySecc) {
            set_dynamic_parameters_in_res(res_mode, dynamic_parameters, res.header.timestamp);
        }
    }

    if (target_frequency.has_value()) {
        res.target_frequency = dt::from_float(target_frequency.value());
    }

    // TODO(sl): Setting EvseStatus, MeterInfo, Receipt

    if (stop) {
        res.status = {0, dt::EvseNotification::Terminate};
    } else if (pause) {
        constexpr auto NotificationMaxDelay = 60; // [V2G20-3308]
        res.status = {NotificationMaxDelay, dt::EvseNotification::Pause};

        // TODO(SL): [V2G20-3318] Decrease notificationmaxdelay based on the reaming seconds if the ev did not perform a
        // pause
    }

    return response_with_code(res, dt::ResponseCode::OK);
}

void AC_ChargeLoop::enter() {
    logf_debug("Enter state: AC_ChargeLoop");
    dynamic_parameters = m_ctx.cache_dynamic_mode_parameters.value_or(UpdateDynamicModeParameters{});
    target_powers = m_ctx.cache_ac_target_power.value_or(AcTargetPower{});
    present_powers = m_ctx.cache_ac_present_power.value_or(AcPresentPower{});
}

Result AC_ChargeLoop::feed(Event ev) {

    if (ev == Event::CONTROL_MESSAGE) {
        if (const auto* control_data = m_ctx.get_control_event<StopCharging>()) {
            stop = *control_data;
        } else if (const auto* control_data = m_ctx.get_control_event<PauseCharging>()) {
            pause = *control_data;
        } else if (const auto* control_data = m_ctx.get_control_event<UpdateDynamicModeParameters>()) {
            dynamic_parameters = *control_data;
        } else if (const auto* control_data = m_ctx.get_control_event<AcTargetPower>()) {
            target_powers = *control_data;
        } else if (const auto* control_data = m_ctx.get_control_event<AcPresentPower>()) {
            present_powers = *control_data;
        } else if (const auto* control_data = m_ctx.get_control_event<ClosedContactor>()) {
            ac_connector_closed = *control_data;

            if (ac_connector_closed) {
                logf_warning(
                    "Got ClosedContactor event, but contactor is not opened. Waiting until the contactor is opened");
                return {};
            }

            const auto* active_timeout = m_ctx.get_active_timeout();
            if (active_timeout != nullptr and *active_timeout == d20::TimeoutType::CONTACTOR) {
                m_ctx.stop_timeout(d20::TimeoutType::CONTACTOR);
            }

            if (not previous_req.has_value()) {
                return {};
            }

            const auto& res = handle_request(previous_req.value(), m_ctx.session, false, false);
            m_ctx.respond(res);

            if (res.response_code >= dt::ResponseCode::FAILED) {
                m_ctx.session_stopped = true;
                return {};
            }
            m_ctx.feedback.signal(session::feedback::Signal::CHARGE_LOOP_FINISHED);
            return m_ctx.create_state<SessionStop>();
        }

        // Ignore control message
        return {};
    }

    if (ev == Event::TIMEOUT) {
        const auto* timeout = m_ctx.get_active_timeout();
        if (timeout != nullptr and *timeout == d20::TimeoutType::CONTACTOR) {
            logf_error("AC contactor is not opened within %ums, sending failure response code and stop the session",
                       AC_OPEN_CONTACTOR_TIMEOUT);
            const auto& res =
                handle_request(previous_req.value_or(message_20::PowerDeliveryRequest{}), m_ctx.session, true, false);
            m_ctx.respond(res);
            m_ctx.session_stopped = true;
        }
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    const auto variant = m_ctx.pull_request();

    if (const auto req = variant->get_if<message_20::PowerDeliveryRequest>()) {

        const auto shutdown_requested = m_ctx.shutdown_requested();

        // If the car wants to stop the session and the contactor is closed
        if (req->charge_progress == dt::Progress::Stop and ac_connector_closed) {
            previous_req = *req;
            // Open the AC contactor
            m_ctx.feedback.signal(session::feedback::Signal::AC_OPEN_CONTACTOR);
            m_ctx.feedback.signal(session::feedback::Signal::CHARGE_LOOP_FINISHED);

            m_ctx.start_timeout(d20::TimeoutType::CONTACTOR, AC_OPEN_CONTACTOR_TIMEOUT);

            logf_info("Waiting for contactor is opened"); // [V2G20-863]
            return {};
        }

        // The contactor is already opened
        const auto res = handle_request(*req, m_ctx.session, false, shutdown_requested);

        m_ctx.respond(res);

        if (res.response_code >= dt::ResponseCode::FAILED) {
            m_ctx.session_stopped = true;
            return {};
        }

        if (req->charge_progress == dt::Progress::Stop and not ac_connector_closed) {
            m_ctx.feedback.signal(session::feedback::Signal::CHARGE_LOOP_FINISHED);
            return m_ctx.create_state<SessionStop>();
        }
        return {};
    }

    if (const auto req = variant->get_if<message_20::AC_ChargeLoopRequest>()) {
        if (first_entry_in_charge_loop) {
            m_ctx.feedback.signal(session::feedback::Signal::CHARGE_LOOP_STARTED);
            first_entry_in_charge_loop = false;
        }

        const auto res = handle_request(*req, m_ctx.session, stop, pause, target_frequency, target_powers,
                                        present_powers, dynamic_parameters);

        m_ctx.respond(res);

        if (res.response_code >= dt::ResponseCode::FAILED) {
            m_ctx.session_stopped = true;
            return {};
        }

        if (const auto* mode = std::get_if<Scheduled_AC_Req>(&req->control_mode)) {
            m_ctx.feedback.ac_charge_loop_req(*mode);
        } else if (const auto* mode = std::get_if<Scheduled_BPT_AC_Req>(&req->control_mode)) {
            m_ctx.feedback.ac_charge_loop_req(*mode);
        }
        if (const auto* mode = std::get_if<Dynamic_AC_Req>(&req->control_mode)) {
            m_ctx.feedback.ac_charge_loop_req(*mode);
        }
        if (const auto* mode = std::get_if<Dynamic_BPT_AC_Req>(&req->control_mode)) {
            m_ctx.feedback.ac_charge_loop_req(*mode);
        }

        m_ctx.feedback.ac_charge_loop_req(req->meter_info_requested);
        if (req->display_parameters) {
            m_ctx.feedback.ac_charge_loop_req(*req->display_parameters);
        }

        return {};
    }

    logf_warning("Expected PowerDeliveryReq or AC_ChargeLoopRequest! But code type id: %d", variant->get_type());

    // Sequence Error
    const message_20::Type req_type = variant->get_type();
    send_sequence_error(req_type, m_ctx);

    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::d20::state
