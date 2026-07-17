// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/din/state/pre_charge.hpp>

#include <iso15118/din/state/power_delivery.hpp>
#include <iso15118/din/state/session_stop.hpp>

#include <iso15118/message/common_types.hpp>
#include <iso15118/message/dc_charge_loop.hpp>

#include <iso15118/detail/din/state/pre_charge.hpp>
#include <iso15118/detail/din/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::din::state {

namespace {
namespace m20dt = message_20::datatypes;
} // namespace

message_din::PreChargeResponse handle_request(const message_din::PreChargeRequest& req, float present_voltage,
                                              const dt::SessionId& session_id) {
    message_din::PreChargeResponse res;

    if (not validate_and_setup_header(res.header, session_id, req.header.session_id)) {
        return response_with_code(res, dt::ResponseCode::FAILED_UnknownSession);
    }

    res.dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_Ready;
    res.dc_evse_status.evse_isolation_status = dt::IsolationLevel::Valid;
    res.evse_present_voltage = present_voltage;

    return response_with_code(res, dt::ResponseCode::OK);
}

void PreCharge::enter() {
    m_ctx.log.enter_state("PreCharge");
}

Result PreCharge::feed(Event ev) {
    if (ev == Event::CONTROL_MESSAGE) {
        if (const auto* control_data = m_ctx.get_control_event<d20::PresentVoltageCurrent>()) {
            m_ctx.present_voltage = control_data->voltage;
        }
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    // An EV aborting sends SessionStopReq; hand it to SessionStop for a clean SessionStopRes.
    if (m_ctx.peek_request_type() == message_din::Type::SessionStopReq) {
        return m_ctx.create_state<SessionStop>();
    }

    // The EVCC decides when pre-charge has converged and then sends a PowerDeliveryReq. Defer it to the
    // PowerDelivery state without consuming the request (WAIT_FOR_PRECHARGE_POWERDELIVERY).
    if (m_ctx.peek_request_type() == message_din::Type::PowerDeliveryReq) {
        return m_ctx.create_state<PowerDelivery>();
    }

    const auto variant = m_ctx.pull_request();

    if (const auto req = variant->get_if<message_din::PreChargeRequest>()) {
        if (not pre_charge_initiated) {
            m_ctx.feedback.signal(session::feedback::Signal::PRE_CHARGE_STARTED);
            pre_charge_initiated = true;
        }

        // Report both the EV pre-charge target voltage and current so the power supply can follow the EV
        // (without the current the supply would stay at 0 A). Uses the dc_charge_loop_req scheduled path.
        {
            m20dt::Scheduled_DC_CLReqControlMode mode{};
            mode.target_voltage = m20dt::from_float(static_cast<float>(req->ev_target_voltage));
            mode.target_current = m20dt::from_float(static_cast<float>(req->ev_target_current));
            m_ctx.feedback.dc_charge_loop_req(session::feedback::DcReqControlMode{mode});
        }

        const auto res = handle_request(*req, m_ctx.present_voltage, m_ctx.get_session_id());
        m_ctx.respond(res);

        if (res.response_code >= dt::ResponseCode::FAILED) {
            m_ctx.session_stopped = true;
        }
        return {};
    }

    m_ctx.log("expected PreChargeReq! But code type id: %d", variant->get_type());
    message_din::PreChargeResponse res;
    setup_header(res.header, m_ctx.get_session_id());
    m_ctx.respond(response_with_code(res, dt::ResponseCode::FAILED_SequenceError));
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::din::state
