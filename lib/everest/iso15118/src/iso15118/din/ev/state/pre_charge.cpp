// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/din/ev/state/pre_charge.hpp>

#include <cmath>

#include <iso15118/din/ev/state/power_delivery.hpp>
#include <iso15118/din/ev/timeouts.hpp>

#include <iso15118/detail/din/ev/state/pre_charge.hpp>
#include <iso15118/detail/din/ev/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::din::ev::state {

namespace pre_charge {

message_din::PreChargeRequest create_request(const dt::DcEvStatus& dc_ev_status, double target_voltage,
                                             double target_current) {
    message_din::PreChargeRequest req;
    req.dc_ev_status = dc_ev_status;
    req.ev_target_voltage = target_voltage;
    req.ev_target_current = target_current;
    return req;
}

// Absolute cap applied on top of the +/- 10 % band so high target voltages still require a tight match.
constexpr double PRE_CHARGE_ABSOLUTE_TOLERANCE_V = 20.0;

Result handle_response(const message_din::PreChargeResponse& res, double target_voltage) {
    Result result;
    result.valid = (res.response_code < dt::ResponseCode::FAILED);
    if (not result.valid) {
        return result;
    }

    // NOTE: [V2G-DC-909] wants convergence decided on the EV's own inlet measurement, not
    // EVSEPresentVoltage. The EVCC does not yet plumb an inlet-voltage measurement into the FSM, so the
    // EVSE-reported voltage is used for now (deferred until that feed exists).
    const auto delta = std::fabs(res.evse_present_voltage - target_voltage);
    const auto tolerance = std::fabs(target_voltage) * 0.10;
    result.converged = (delta <= tolerance) and (delta <= PRE_CHARGE_ABSOLUTE_TOLERANCE_V);
    return result;
}

} // namespace pre_charge

using namespace pre_charge;

void PreCharge::enter() {
    m_ctx.log.enter_state("PreCharge");
}

float PreCharge::target_voltage() const {
    if (m_ctx.dc_cache.target_voltage.has_value()) {
        return m_ctx.dc_cache.target_voltage.value();
    }
    return static_cast<float>(m_ctx.session_config.dc.target_voltage);
}

void PreCharge::send(Event) {
    if (first_request) {
        m_ctx.start_timeout(d20::TimeoutType::ONGOING, TIMEOUT_PRE_CHARGE_MS);
        first_request = false;
    }

    // Follow Josev: pre-charge ramps voltage with a target current of 0 A.
    auto req = create_request(make_dc_ev_status(m_ctx, true), target_voltage(), 0.0);
    m_ctx.setup_header(req.header);
    m_ctx.send_request(req);
    m_ctx.start_timeout(d20::TimeoutType::SEQUENCE, MESSAGE_TIMEOUT_MS);
}

din::ev::Result PreCharge::feed(Event ev) {
    if (ev == Event::SEND_REQUEST) {
        send(ev);
        return {};
    }

    if (ev == Event::CONTROL_MESSAGE) {
        handle_dc_control_event(m_ctx);
        handle_stop_control_event(m_ctx);
        return {};
    }

    if (ev == Event::TIMEOUT) {
        const auto* timeout = m_ctx.get_active_timeout();
        if (timeout and *timeout == d20::TimeoutType::ONGOING) {
            m_ctx.log("PreCharge ongoing timeout reached, terminating session");
            ongoing_timeout_reached = true;
        } else {
            m_ctx.log("PreCharge message timeout reached, terminating session");
        }
        m_ctx.session_stopped = true;
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    m_ctx.stop_timeout(d20::TimeoutType::SEQUENCE);

    const auto variant = m_ctx.pull_response();

    if (const auto res = variant->get_if<message_din::PreChargeResponse>()) {
        const auto result = handle_response(*res, target_voltage());

        if (not result.valid) {
            m_ctx.log("PreCharge failed, terminating session");
            m_ctx.session_stopped = true;
            return {};
        }

        if (result.converged) {
            m_ctx.stop_timeout(d20::TimeoutType::ONGOING);

            // Publish dc_power_on immediately before PowerDeliveryReq(ready=true).
            m_ctx.feedback.dc_power_on();

            if (auto stop = stop_if_pending(m_ctx)) {
                return stop;
            }
            return m_ctx.create_state<PowerDelivery>(PowerDelivery::Phase::Start);
        }

        if (ongoing_timeout_reached) {
            m_ctx.session_stopped = true;
            return {};
        }

        send(ev);
        return {};
    }

    m_ctx.log("expected PreChargeRes! But code type id: %d", variant->get_type());
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::din::ev::state
