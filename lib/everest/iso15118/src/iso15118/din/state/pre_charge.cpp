// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/din/state/pre_charge.hpp>

#include <iso15118/detail/din/state/constants.hpp>
#include <iso15118/detail/din/state/power_delivery.hpp>
#include <iso15118/din/state/session_stop.hpp>

#include <iso15118/message/common_types.hpp>
#include <iso15118/message/dc_charge_loop.hpp>

#include <iso15118/detail/din/state/pre_charge.hpp>
#include <iso15118/detail/din/state/sequence_error.hpp>
#include <iso15118/detail/din/state/session_stop.hpp>
#include <iso15118/detail/din/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::din::state {

namespace {
namespace m20dt = message_20::datatypes;
} // namespace

message_din::PreChargeResponse handle_request([[maybe_unused]] const message_din::PreChargeRequest& req,
                                              float present_voltage, const dt::SessionId& session_id,
                                              std::optional<dt::DcEvseStatusCode> error_status_code,
                                              bool charger_stop) {
    message_din::PreChargeResponse res;
    setup_header(res.header, session_id);

    // Mandatory in PreChargeRes even on a FAILED_UnknownSession response, so populate them first.
    // EVSE_Shutdown on a stop, EVSE_Ready otherwise, and a module fault overrides both.
    res.dc_evse_status.evse_status_code = error_status_code.value_or(charger_stop ? dt::DcEvseStatusCode::EVSE_Shutdown
                                                                                  : dt::DcEvseStatusCode::EVSE_Ready);
    res.dc_evse_status.evse_isolation_status = dt::IsolationLevel::Valid;
    res.evse_present_voltage = present_voltage;
    return response_with_code(res, dt::ResponseCode::OK);
}

// Shared by both pre-charge nodes, which differ only in what else they accept.
namespace {
void answer_pre_charge(Context& ctx, const message_din::PreChargeRequest& req, PreChargeTarget& forwarded) {
    ctx.report_ev_status(req.dc_ev_status);

    // Report both the EV pre-charge target voltage and current so the power supply can follow the EV
    // (without the current the supply would stay at 0 A). Uses the dc_charge_loop_req scheduled path.
    const auto target = std::make_pair(req.ev_target_voltage, req.ev_target_current);
    if (forwarded != target) {
        m20dt::Scheduled_DC_CLReqControlMode mode{};
        mode.target_voltage = m20dt::from_float(static_cast<float>(req.ev_target_voltage));
        mode.target_current = m20dt::from_float(static_cast<float>(req.ev_target_current));
        ctx.feedback.dc_charge_loop_req(session::feedback::DcReqControlMode{mode});
        forwarded = target;
    }

    auto res = handle_request(req, ctx.evse().present_voltage, ctx.get_session_id(), ctx.error_status_code(),
                              ctx.evse().charger_stop_requested);
    apply_isolation_status(ctx, res.dc_evse_status);
    ctx.respond(res);

    if (res.response_code >= dt::ResponseCode::FAILED) {
        ctx.session_stopped = true;
    }
}

// Latches the present voltage and, on the supervision timer, ends the session. Identical in both
// nodes: the [V2G-DC-969] window is armed in PreChargeStart and expires in either.
Result pre_charge_on_event(Context& ctx, Event ev) {
    if (ev == Event::CONTROL_MESSAGE) {
        if (const auto* control_data = ctx.get_control_event<d20::PresentVoltageCurrent>()) {
            ctx.set_present_voltage(control_data->voltage);
        }
        return {};
    }

    if (ev == Event::TIMEOUT) {
        const auto* timeout = ctx.get_active_timeout();
        if (timeout and *timeout == d20::TimeoutType::ONGOING) {
            logf_warning("PowerDelivery (pre-charge) timeout reached, terminating session");
            ctx.session_stopped = true;
        }
        return {};
    }

    return {};
}
} // namespace

void PreChargeStart::enter() {
    logf_debug("Enter state: PreChargeStart");
}

Result PreChargeStart::on_event(Event ev) {
    return pre_charge_on_event(m_ctx, ev);
}

Result PreChargeStart::on_request(const message_din::Variant& received) {
    if (const auto req = received.get_if<message_din::PreChargeRequest>()) {
        m_ctx.feedback.signal(session::feedback::Signal::PRE_CHARGE_STARTED);
        // [V2G-DC-969]: start the SECC PowerDelivery supervision timer on the first PreChargeReq.
        m_ctx.start_timeout(d20::TimeoutType::ONGOING, TIMEOUT_POWER_DELIVERY_MS);

        answer_pre_charge(m_ctx, *req, forwarded_target);
        if (m_ctx.session_stopped) {
            return {};
        }
        // [V2G-DC-458]: a PowerDeliveryReq is in sequence from the next request on. The forwarded target
        // goes with it so the change filter survives the hop.
        return m_ctx.create_state<PreCharge>(forwarded_target);
    }

    // [V2G-DC-455] admits a SessionStopReq here.
    if (const auto stop = received.get_if<message_din::SessionStopRequest>()) {
        return process_session_stop(m_ctx, *stop);
    }

    logf_warning("Expected PreChargeReq or SessionStopReq! But code type id: %d", received.get_type());
    respond_sequence_error(m_ctx, received);
    m_ctx.session_stopped = true;
    return {};
}

void PreCharge::enter() {
    logf_debug("Enter state: PreCharge");
}

Result PreCharge::on_event(Event ev) {
    return pre_charge_on_event(m_ctx, ev);
}

Result PreCharge::on_request(const message_din::Variant& received) {
    if (const auto req = received.get_if<message_din::PreChargeRequest>()) {
        answer_pre_charge(m_ctx, *req, forwarded_target);
        return {};
    }

    // [V2G-DC-458] admits a PowerDeliveryReq here; answering it is an action, not a state of its own.
    if (const auto req = received.get_if<message_din::PowerDeliveryRequest>()) {
        return process_power_delivery(m_ctx, *req);
    }

    // [V2G-DC-458] admits a SessionStopReq here too.
    if (const auto stop = received.get_if<message_din::SessionStopRequest>()) {
        return process_session_stop(m_ctx, *stop);
    }

    logf_warning("Expected PreChargeReq, PowerDeliveryReq or SessionStopReq! But code type id: %d",
                 received.get_type());
    respond_sequence_error(m_ctx, received);
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::din::state
