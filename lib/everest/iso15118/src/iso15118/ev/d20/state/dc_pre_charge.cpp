// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <cmath>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/state/dc_pre_charge.hpp>
#include <iso15118/ev/d20/state/power_delivery.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>
#include <iso15118/message/dc_pre_charge.hpp>

namespace iso15118::ev::d20::state {

namespace {

using ResponseCode = message_20::datatypes::ResponseCode;

// ISO 15118 precharge-complete criterion: present voltage within tolerance of the
// target (absorbs converter voltage settling).
constexpr float PRECHARGE_VOLTAGE_TOLERANCE_V = 20.0f;

bool check_response_code(ResponseCode response_code) {
    switch (response_code) {
    case ResponseCode::OK:
        return true;
    case ResponseCode::FAILED_SequenceError:
    case ResponseCode::FAILED_UnknownSession:
        return false;
    default:
        logf_warning("Unexpected response code received: %d", static_cast<int>(response_code));
        return iso15118::ev::d20::check_response_code(response_code);
    }
}

message_20::DC_PreChargeRequest make_request(const SessionId& session, message_20::datatypes::Processing processing,
                                             const DcChargeParams& params) {
    message_20::DC_PreChargeRequest req;
    setup_header(req.header, session);
    req.processing = processing;
    req.present_voltage = message_20::datatypes::from_float(params.present_voltage);
    req.target_voltage = message_20::datatypes::from_float(params.target_voltage);
    return req;
}

} // namespace

void DC_PreCharge::enter() {
    const auto params = m_ctx.get_dc_params();
    m_ctx.respond(make_request(m_ctx.get_session(), message_20::datatypes::Processing::Ongoing, params));
}

Result DC_PreCharge::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    const auto variant = m_ctx.pull_response();

    if (const auto res = variant->get_if<message_20::DC_PreChargeResponse>()) {
        if (res->header.session_id != m_ctx.get_session().get_id()) {
            logf_error("DC_PreChargeResponse session_id does not match current session");
            m_ctx.stop_session(true);
            return {};
        }

        if (not check_response_code(res->response_code)) {
            logf_error("DC_PreChargeResponse rejected with response_code: %d", static_cast<int>(res->response_code));
            m_ctx.stop_session(true);
            return {};
        }

        const auto params = m_ctx.get_dc_params();
        const auto present_voltage = message_20::datatypes::from_RationalNumber(res->present_voltage);
        const auto gap = std::fabs(present_voltage - params.target_voltage);

        // target_voltage <= 0 means unwired/default params; treat the first OK as
        // in-tolerance so the flow still advances.
        const bool finished = params.target_voltage <= 0.0f or gap <= PRECHARGE_VOLTAGE_TOLERANCE_V;
        if (finished) {
            m_ctx.feedback.dc_power_on();
            return m_ctx.create_state<PowerDelivery>(message_20::datatypes::Progress::Start);
        }

        // Not in tolerance: resend one Ongoing request and stay (never respond + transition
        // in one pass). The SECC's PowerDelivery keeps answering precharge requests while its
        // converter ramps, so the EV converges here before transitioning; no Finished
        // precharge request is ever emitted.
        m_ctx.respond(make_request(m_ctx.get_session(), message_20::datatypes::Processing::Ongoing, params));
        return {};
    }

    logf_error("Expected DC_PreChargeResponse, got code type id: %d", static_cast<int>(variant->get_type()));
    m_ctx.stop_session(true);
    return {};
}

} // namespace iso15118::ev::d20::state
