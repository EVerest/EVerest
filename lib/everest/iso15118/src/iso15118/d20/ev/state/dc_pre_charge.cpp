// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d20/ev/state/dc_pre_charge.hpp>

#include <cmath>

#include <iso15118/d20/ev/state/power_delivery.hpp>
#include <iso15118/d20/ev/timeouts.hpp>

#include <iso15118/detail/d20/ev/state/dc_pre_charge.hpp>
#include <iso15118/detail/d20/ev/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d20::ev::state {

namespace dc_pre_charge {

message_20::DC_PreChargeRequest create_request(dt::Processing processing, const dt::RationalNumber& present_voltage,
                                               const dt::RationalNumber& target_voltage) {
    message_20::DC_PreChargeRequest req;
    req.processing = processing;
    req.present_voltage = present_voltage;
    req.target_voltage = target_voltage;
    return req;
}

Result handle_response(const message_20::DC_PreChargeResponse& res, float target_voltage) {
    Result result;
    result.valid = (res.response_code < dt::ResponseCode::FAILED);
    if (not result.valid) {
        return result;
    }

    const auto present_voltage = dt::from_RationalNumber(res.present_voltage);
    const auto tolerance = std::fabs(target_voltage) * 0.10f;
    result.converged = std::fabs(present_voltage - target_voltage) <= tolerance;
    return result;
}

} // namespace dc_pre_charge

using namespace dc_pre_charge;

void DC_PreCharge::enter() {
    m_ctx.log.enter_state("DC_PreCharge");
}

float DC_PreCharge::target_voltage() const {
    if (m_ctx.dc_cache.target_voltage.has_value()) {
        return m_ctx.dc_cache.target_voltage.value();
    }
    return dt::from_RationalNumber(m_ctx.session_config.dc_charge_parameters.target_voltage);
}

void DC_PreCharge::send(Event) {
    if (first_request) {
        m_ctx.start_timeout(d20::TimeoutType::ONGOING, TIMEOUT_PRE_CHARGE_MS);
        first_request = false;
    }

    const auto processing = converged ? dt::Processing::Finished : dt::Processing::Ongoing;
    if (processing == dt::Processing::Finished) {
        sent_finished = true;
    }

    auto req = create_request(processing, dt::from_float(m_ctx.dc_cache.present_voltage),
                              dt::from_float(target_voltage()));
    m_ctx.setup_header(req.header);
    m_ctx.send_request(req);
    m_ctx.start_timeout(d20::TimeoutType::SEQUENCE, MESSAGE_TIMEOUT_MS);
}

d20::ev::Result DC_PreCharge::feed(Event ev) {
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
            m_ctx.log("DC_PreCharge ongoing timeout reached, terminating session");
            ongoing_timeout_reached = true;
        } else {
            m_ctx.log("DC_PreCharge message timeout reached, terminating session");
        }
        m_ctx.session_stopped = true;
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    m_ctx.stop_timeout(d20::TimeoutType::SEQUENCE);

    const auto variant = m_ctx.pull_response();

    if (const auto res = variant->get_if<message_20::DC_PreChargeResponse>()) {
        const auto result = handle_response(*res, target_voltage());

        if (not result.valid) {
            m_ctx.log("DC_PreCharge failed, terminating session");
            m_ctx.session_stopped = true;
            return {};
        }

        // Advance only when the current response is still converged AND we already sent a Finished
        // request in the previous exchange (mirrors Josev's precharge_finished latch).
        if (sent_finished and result.converged) {
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

        converged = result.converged;
        send(ev);
        return {};
    }

    m_ctx.log("expected DC_PreChargeRes! But code type id: %d", variant->get_type());
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::d20::ev::state
