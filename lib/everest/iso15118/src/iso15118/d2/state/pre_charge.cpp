// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/state/pre_charge.hpp>

#include <iso15118/d2/state/session_stop.hpp>
#include <iso15118/detail/d2/state/power_delivery.hpp>

#include <iso15118/message/common_types.hpp>
#include <iso15118/message/dc_charge_loop.hpp>

#include <iso15118/detail/d2/state/pre_charge.hpp>
#include <iso15118/detail/d2/state/sequence_error.hpp>
#include <iso15118/detail/d2/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d2::state {

namespace {
// ISO 15118-2 defines no SECC-side PreCharge supervision timer (unlike DIN's
// V2G_SECC_PowerDelivery_Timer [V2G-DC-969]): the wait is bounded solely by V2G_SECC_Sequence_Timeout,
// and the -4 ATS (TC_SECC_DC_VTB_PowerDelivery_009) asserts no close happens before that expires.
namespace m20dt = message_20::datatypes;
} // namespace

message_2::PreChargeResponse handle_request([[maybe_unused]] const message_2::PreChargeRequest& req,
                                            const dt::SessionId& session_id, float present_voltage,
                                            std::optional<dt::DC_EVSEStatusCode> error_status_code, bool charger_stop) {
    message_2::PreChargeResponse res;
    res.header.session_id = session_id;
    res.response_code = dt::ResponseCode::OK;

    res.dc_evse_status.notification = charger_stop ? dt::EVSENotification::StopCharging : dt::EVSENotification::None;
    res.dc_evse_status.notification_max_delay = 0;
    res.dc_evse_status.isolation_status = dt::IsolationLevel::Valid;
    // EVSE_Shutdown on a stop, EVSE_Ready otherwise, and a module fault overrides both.
    res.dc_evse_status.status_code = error_status_code.value_or(charger_stop ? dt::DC_EVSEStatusCode::EVSE_Shutdown
                                                                             : dt::DC_EVSEStatusCode::EVSE_Ready);

    res.evse_present_voltage = dt::to_physical_value(present_voltage, dt::Unit::V);
    return res;
}

// Shared by both pre-charge nodes: they differ in which requests they accept, not in the answer.
namespace {
void answer_pre_charge(Context& ctx, const message_2::PreChargeRequest& req, PreChargeTarget& forwarded_target) {
    ctx.report_ev_status(req.dc_ev_status);

    const auto target =
        std::make_pair(dt::from_physical_value(req.ev_target_voltage), dt::from_physical_value(req.ev_target_current));
    if (forwarded_target != target) {
        m20dt::Scheduled_DC_CLReqControlMode mode{};
        mode.target_voltage = m20dt::from_float(static_cast<float>(target.first));
        mode.target_current = m20dt::from_float(static_cast<float>(target.second));
        ctx.feedback.dc_charge_loop_req(session::feedback::DcReqControlMode{mode});
        forwarded_target = target;
    }

    auto res = handle_request(req, ctx.get_session_id(), ctx.evse().present_voltage, ctx.error_status_code(),
                              ctx.evse().charger_stop_requested);
    apply_isolation_status(ctx, res.dc_evse_status);
    ctx.respond(res);
}
} // namespace

void PreChargeStart::enter() {
    logf_debug("Enter state: PreChargeStart");
}

Result PreChargeStart::on_event(Event ev) {
    if (ev == Event::CONTROL_MESSAGE) {
        if (const auto* control = m_ctx.get_control_event<d20::PresentVoltageCurrent>()) {
            m_ctx.set_present_values(control->voltage, control->current);
        }
        return {};
    }

    return {};
}

Result PreChargeStart::on_request(const message_2::Variant& received) {
    // [V2G2-584]: a PowerDeliveryReq is not in sequence until the EV has pre-charged at least once.
    const auto type = received.get_type();
    if (type == message_2::Type::PreChargeReq) {
        m_ctx.feedback.signal(session::feedback::Signal::PRE_CHARGE_STARTED);
        answer_pre_charge(m_ctx, received.get<message_2::PreChargeRequest>(), forwarded_target);
        return m_ctx.create_state<PreCharge>(forwarded_target);
    } else {
        logf_warning("Expected PreChargeReq! But got type id: %d", received.get_type());
        respond_sequence_error(m_ctx, received.get_type());
        return {};
    }
}

void PreCharge::enter() {
    logf_debug("Enter state: PreCharge");
}

Result PreCharge::on_event(Event ev) {
    if (ev == Event::CONTROL_MESSAGE) {
        if (const auto* control = m_ctx.get_control_event<d20::PresentVoltageCurrent>()) {
            m_ctx.set_present_values(control->voltage, control->current);
        }
        return {};
    }

    return {};
}

Result PreCharge::on_request(const message_2::Variant& received) {
    // [V2G2-587] widens the node to a PowerDeliveryReq; SessionStopReq is not in it.
    const auto type = received.get_type();
    if (type == message_2::Type::PreChargeReq) {
        answer_pre_charge(m_ctx, received.get<message_2::PreChargeRequest>(), forwarded_target);
        return {};
    } else if (type == message_2::Type::PowerDeliveryReq) {
        return process_dc_power_delivery(m_ctx, received.get<message_2::PowerDeliveryRequest>());
    } else {
        logf_warning("Expected PreChargeReq or PowerDeliveryReq! But got type id: %d", received.get_type());
        respond_sequence_error(m_ctx, received.get_type());
        return {};
    }
}

} // namespace iso15118::d2::state
