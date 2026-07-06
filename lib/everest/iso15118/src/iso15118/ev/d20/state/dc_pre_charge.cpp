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

// ISO 15118 precharge-complete criterion: present voltage within tolerance of the
// target (absorbs converter voltage settling).
constexpr float PRECHARGE_VOLTAGE_TOLERANCE_V = 20.0f;

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

    const auto* res = expect_response<message_20::DC_PreChargeResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return {};
    }

    const auto params = m_ctx.get_dc_params();
    const auto present_voltage = message_20::datatypes::from_RationalNumber(res->present_voltage);
    const auto gap = std::fabs(present_voltage - params.target_voltage);

    if (gap <= PRECHARGE_VOLTAGE_TOLERANCE_V) {
        m_ctx.feedback.dc_power_on();
        return m_ctx.create_state<PowerDelivery>(message_20::datatypes::Progress::Start);
    }

    // Not in tolerance: resend one Ongoing request and stay. The SECC keeps answering
    // precharge requests while its converter ramps, so the EV converges here before
    // transitioning; no Finished precharge request is ever emitted.
    m_ctx.respond(make_request(m_ctx.get_session(), message_20::datatypes::Processing::Ongoing, params));
    return {};
}

} // namespace iso15118::ev::d20::state
