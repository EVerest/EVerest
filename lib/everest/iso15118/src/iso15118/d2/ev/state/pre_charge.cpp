// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/ev/state/pre_charge.hpp>

#include <cmath>

#include <iso15118/d2/ev/state/power_delivery.hpp>
#include <iso15118/d2/ev/timeouts.hpp>

#include <iso15118/detail/d2/ev/state/pre_charge.hpp>
#include <iso15118/detail/d2/ev/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d2::ev::state {

namespace pre_charge {

message_2::PreChargeRequest create_request(const dt::DC_EVStatus& dc_ev_status, float present_voltage,
                                           float target_voltage) {
    message_2::PreChargeRequest req;
    req.dc_ev_status = dc_ev_status;
    req.ev_target_voltage = dt::to_physical_value(target_voltage, dt::Unit::V);
    // The present-voltage field of the request is informational; the EVSE-reported one drives
    // convergence. Josev sends target current 0 during pre-charge.
    (void)present_voltage;
    req.ev_target_current = dt::to_physical_value(0.0f, dt::Unit::A);
    return req;
}

Result handle_response(const message_2::PreChargeResponse& res, float target_voltage) {
    Result result;
    result.valid = (res.response_code < dt::ResponseCode::FAILED);
    if (not result.valid) {
        return result;
    }

    const auto present_voltage = static_cast<float>(dt::from_physical_value(res.evse_present_voltage));
    // Require both the +/- 10 % band and an absolute cap so a large target does not admit a wide error.
    const bool within_band = (present_voltage > target_voltage * 0.9f) and (present_voltage < target_voltage * 1.1f);
    const bool within_abs = std::fabs(present_voltage - target_voltage) <= PRE_CHARGE_ABS_VOLTAGE_TOLERANCE_V;
    result.converged = within_band and within_abs;
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
    return m_ctx.session_config.dc_target_voltage;
}

void PreCharge::send(Event) {
    if (first_request) {
        m_ctx.start_timeout(d20::TimeoutType::ONGOING, TIMEOUT_PRE_CHARGE_MS);
        first_request = false;
    }

    auto req = create_request(make_dc_ev_status(m_ctx, true), m_ctx.dc_cache.present_voltage, target_voltage());
    m_ctx.setup_header(req.header);
    m_ctx.send_request(req);
    m_ctx.start_timeout(d20::TimeoutType::SEQUENCE, MESSAGE_TIMEOUT_MS);
}

d2::ev::Result PreCharge::feed(Event ev) {
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

    if (const auto res = variant->get_if<message_2::PreChargeResponse>()) {
        const auto result = handle_response(*res, target_voltage());

        if (not result.valid) {
            m_ctx.log("PreCharge failed, terminating session");
            m_ctx.session_stopped = true;
            return {};
        }

        if (result.converged) {
            m_ctx.stop_timeout(d20::TimeoutType::ONGOING);

            // Publish dc_power_on immediately before PowerDeliveryReq(Start) [flow spec §4.2].
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

} // namespace iso15118::d2::ev::state
