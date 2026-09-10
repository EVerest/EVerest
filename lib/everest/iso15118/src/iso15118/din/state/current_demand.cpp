// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/din/state/current_demand.hpp>

#include <iso15118/detail/din/state/power_delivery.hpp>
#include <iso15118/din/state/session_stop.hpp>

#include <iso15118/message/common_types.hpp>
#include <iso15118/message/dc_charge_loop.hpp>

#include <iso15118/detail/din/state/constants.hpp>
#include <iso15118/detail/din/state/current_demand.hpp>
#include <iso15118/detail/din/state/sequence_error.hpp>
#include <iso15118/detail/din/state/session_stop.hpp>
#include <iso15118/detail/din/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::din::state {

namespace m20dt = message_20::datatypes;

// Reuses the dc_charge_loop_req path so the DC power supply follows the EV.
m20dt::Scheduled_DC_CLReqControlMode build_ev_setpoint([[maybe_unused]] const message_din::CurrentDemandRequest& req) {
    m20dt::Scheduled_DC_CLReqControlMode mode{};
    mode.target_voltage = m20dt::from_float(static_cast<float>(req.ev_target_voltage));
    mode.target_current = m20dt::from_float(static_cast<float>(req.ev_target_current));
    if (req.ev_maximum_voltage_limit.has_value()) {
        mode.max_voltage = m20dt::from_float(static_cast<float>(req.ev_maximum_voltage_limit.value()));
    }
    if (req.ev_maximum_current_limit.has_value()) {
        mode.max_charge_current = m20dt::from_float(static_cast<float>(req.ev_maximum_current_limit.value()));
    }
    if (req.ev_maximum_power_limit.has_value()) {
        mode.max_charge_power = m20dt::from_float(static_cast<float>(req.ev_maximum_power_limit.value()));
    }
    return mode;
}

message_din::CurrentDemandResponse handle_request([[maybe_unused]] const message_din::CurrentDemandRequest& req,
                                                  const SessionConfig& config, float present_voltage,
                                                  float present_current, const dt::SessionId& session_id,
                                                  bool charger_stop,
                                                  std::optional<dt::DcEvseStatusCode> error_status_code) {
    message_din::CurrentDemandResponse res;
    setup_header(res.header, session_id);

    // Mandatory in CurrentDemandRes even on a FAILED_UnknownSession response, so populate them first.
    // A module-reported error overrides the status code so the EV sees the fault mid-charge-loop.
    res.dc_evse_status.evse_status_code = error_status_code.value_or(charger_stop ? dt::DcEvseStatusCode::EVSE_Shutdown
                                                                                  : dt::DcEvseStatusCode::EVSE_Ready);
    res.dc_evse_status.evse_isolation_status = dt::IsolationLevel::Valid;
    // [V2G-DC-500]: EVSENotification stays None for DC; a stop is signalled as EVSE_Shutdown above.
    res.dc_evse_status.evse_notification = dt::EvseNotification::None;

    res.evse_present_voltage = present_voltage;
    res.evse_present_current = present_current;

    res.evse_maximum_current_limit = config.evse_maximum_current_limit;
    res.evse_maximum_voltage_limit = config.evse_maximum_voltage_limit;
    res.evse_maximum_power_limit = config.evse_maximum_power_limit;

    // The three *LimitAchieved flags describe the EVSE's own output, not what the EV asked for, so all
    // three are derived from the present output against the advertised maxima. EVSEMaximumPowerLimit is
    // optional: with no power limit advertised there is none to reach.
    res.evse_current_limit_achieved = present_current >= config.evse_maximum_current_limit;
    res.evse_voltage_limit_achieved = present_voltage >= config.evse_maximum_voltage_limit;
    res.evse_power_limit_achieved =
        config.evse_maximum_power_limit.has_value() and
        static_cast<double>(present_voltage) * present_current >= config.evse_maximum_power_limit.value();
    return response_with_code(res, dt::ResponseCode::OK);
}

// Shared by both charge-loop nodes, which differ only in what else they accept.
namespace {
void answer_current_demand(Context& ctx, const message_din::CurrentDemandRequest& req, EvSetpoint& forwarded) {
    ctx.report_ev_status(req.dc_ev_status);

    session::feedback::DcEvChargeProgress progress{};
    if (req.remaining_time_to_full_soc.has_value()) {
        progress.remaining_time_to_full_soc = static_cast<float>(req.remaining_time_to_full_soc.value());
    }
    if (req.remaining_time_to_bulk_soc.has_value()) {
        progress.remaining_time_to_bulk_soc = static_cast<float>(req.remaining_time_to_bulk_soc.value());
    }
    progress.charging_complete = req.charging_complete;
    progress.bulk_charging_complete = req.bulk_charging_complete;
    ctx.report_charge_progress(progress);

    const EvSetpoint setpoint{std::make_tuple(req.ev_target_voltage, req.ev_target_current,
                                              req.ev_maximum_voltage_limit, req.ev_maximum_current_limit,
                                              req.ev_maximum_power_limit)};
    if (forwarded != setpoint) {
        ctx.feedback.dc_charge_loop_req(session::feedback::DcReqControlMode{build_ev_setpoint(req)});
        forwarded = setpoint;
    }

    // A graceful HLC shutdown reaches the EV the same way as an EVSE-initiated stop: EVSE_Shutdown.
    const bool charger_stop = ctx.evse().charger_stop_requested or ctx.shutdown_requested();

    auto res = handle_request(req, ctx.session_config, ctx.evse().present_voltage, ctx.evse().present_current,
                              ctx.get_session_id(), charger_stop, ctx.error_status_code());
    apply_isolation_status(ctx, res.dc_evse_status);
    ctx.respond(res);

    if (res.response_code >= dt::ResponseCode::FAILED) {
        ctx.session_stopped = true;
    }
}

Result charge_loop_on_event(Context& ctx, Event ev) {
    if (ev == Event::CONTROL_MESSAGE) {
        if (const auto* control_data = ctx.get_control_event<d20::PresentVoltageCurrent>()) {
            ctx.set_present_values(control_data->voltage, control_data->current);
        }
        return {};
    }

    return {};
}

// The deliberate deviation from [V2G-DC-462]/[V2G-DC-465], which admit no SessionStopReq in the charge
// loop -- see detail/din/state/constants.hpp for why we read that as a spec omission. Shared by both
// nodes so the deviation lives in one place.
// Returns true when it answered a SessionStopReq (process_session_stop never transitions -- it ends
// the session in place -- so the caller has nothing to propagate).
bool try_session_stop(Context& ctx, const message_din::Variant& received) {
#ifdef DIN_ACCEPT_SESSION_STOP_IN_CHARGE_LOOP
    if (const auto stop = received.get_if<message_din::SessionStopRequest>()) {
        process_session_stop(ctx, *stop);
        return true;
    }
#else
    (void)ctx;
    (void)received;
#endif
    return false;
}
} // namespace

void CurrentDemandStart::enter() {
    logf_debug("Enter state: CurrentDemandStart");
}

Result CurrentDemandStart::on_event(Event ev) {
    return charge_loop_on_event(m_ctx, ev);
}

Result CurrentDemandStart::on_request(const message_din::Variant& received) {
    if (const auto req = received.get_if<message_din::CurrentDemandRequest>()) {
        // Raised once, when charging actually starts: EvseManager then arms the over-voltage monitor.
        m_ctx.feedback.signal(session::feedback::Signal::CHARGE_LOOP_STARTED);

        answer_current_demand(m_ctx, *req, forwarded_setpoint);
        if (m_ctx.session_stopped) {
            return {};
        }
        // [V2G-DC-465]: a PowerDeliveryReq is in sequence from the next request on. The forwarded setpoint
        // goes with it so the change filter survives the hop.
        return m_ctx.create_state<CurrentDemand>(forwarded_setpoint);
    }

    if (try_session_stop(m_ctx, received)) {
        return {};
    }

    logf_warning("Expected CurrentDemandReq! But code type id: %d", received.get_type());
    respond_sequence_error(m_ctx, received);
    m_ctx.session_stopped = true;
    return {};
}

void CurrentDemand::enter() {
    logf_debug("Enter state: CurrentDemand");
}

Result CurrentDemand::on_event(Event ev) {
    return charge_loop_on_event(m_ctx, ev);
}

Result CurrentDemand::on_request(const message_din::Variant& received) {
    if (const auto req = received.get_if<message_din::CurrentDemandRequest>()) {
        answer_current_demand(m_ctx, *req, forwarded_setpoint);
        return {};
    }

    // [V2G-DC-465] admits a PowerDeliveryReq here; answering it is an action, not a state of its own.
    if (const auto req = received.get_if<message_din::PowerDeliveryRequest>()) {
        return process_power_delivery(m_ctx, *req);
    }

    if (try_session_stop(m_ctx, received)) {
        return {};
    }

    logf_warning("Expected CurrentDemandReq or PowerDeliveryReq! But code type id: %d", received.get_type());
    respond_sequence_error(m_ctx, received);
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::din::state
