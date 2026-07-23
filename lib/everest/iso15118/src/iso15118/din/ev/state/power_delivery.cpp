// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/din/ev/state/power_delivery.hpp>

#include <iso15118/din/ev/state/current_demand.hpp>
#include <iso15118/din/ev/state/welding_detection.hpp>
#include <iso15118/din/ev/timeouts.hpp>

#include <iso15118/detail/din/ev/state/power_delivery.hpp>
#include <iso15118/detail/din/ev/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::din::ev::state {

namespace power_delivery {

message_din::PowerDeliveryRequest create_request(bool ready_to_charge, const dt::DcEvStatus& dc_ev_status,
                                                 bool charging_complete) {
    message_din::PowerDeliveryRequest req;
    req.ready_to_charge_state = ready_to_charge;
    dt::DcEvPowerDeliveryParameter param;
    param.dc_ev_status = dc_ev_status;
    param.charging_complete = charging_complete;
    req.dc_ev_power_delivery_parameter = param;
    return req;
}

Result handle_response(const message_din::PowerDeliveryResponse& res) {
    return {res.response_code < dt::ResponseCode::FAILED};
}

} // namespace power_delivery

using namespace power_delivery;

void PowerDelivery::enter() {
    m_ctx.log.enter_state("PowerDelivery");
}

din::ev::Result PowerDelivery::feed(Event ev) {
    if (ev == Event::SEND_REQUEST) {
        const bool ready = (m_phase == Phase::Start);
        // DIN Table 73: ChargingComplete means the EV is fully charged (100 % SOC) -- not merely
        // that charging is ending. Derive it from the reported SoC.
        auto req = create_request(ready, make_dc_ev_status(m_ctx, true), m_ctx.dc_cache.present_soc >= 100);
        m_ctx.setup_header(req.header);
        m_ctx.send_request(req);
        m_ctx.start_timeout(d20::TimeoutType::SEQUENCE, MESSAGE_TIMEOUT_MS);
        return {};
    }

    if (ev == Event::CONTROL_MESSAGE) {
        handle_dc_control_event(m_ctx);
        handle_stop_control_event(m_ctx);
        return {};
    }

    if (ev == Event::TIMEOUT) {
        m_ctx.log("PowerDelivery message timeout reached, terminating session");
        m_ctx.session_stopped = true;
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    m_ctx.stop_timeout(d20::TimeoutType::SEQUENCE);

    const auto variant = m_ctx.pull_response();

    if (const auto res = variant->get_if<message_din::PowerDeliveryResponse>()) {
        const auto result = handle_response(*res);

        if (not result.valid) {
            m_ctx.log("PowerDelivery failed, terminating session");
            m_ctx.session_stopped = true;
            return {};
        }

        if (m_phase == Phase::Start) {
            return m_ctx.create_state<CurrentDemand>();
        }
        return m_ctx.create_state<WeldingDetection>();
    }

    m_ctx.log("expected PowerDeliveryRes! But code type id: %d", variant->get_type());
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::din::ev::state
