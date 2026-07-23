// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d20/ev/state/power_delivery.hpp>

#include <iso15118/d20/ev/state/ac_charge_loop.hpp>
#include <iso15118/d20/ev/state/dc_charge_loop.hpp>
#include <iso15118/d20/ev/state/dc_welding_detection.hpp>
#include <iso15118/d20/ev/state/session_stop.hpp>
#include <iso15118/d20/ev/timeouts.hpp>

#include <iso15118/detail/d20/ev/state/power_delivery.hpp>
#include <iso15118/detail/d20/ev/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d20::ev::state {

namespace power_delivery {

message_20::PowerDeliveryRequest create_request(dt::Progress charge_progress,
                                                std::optional<dt::ChannelSelection> channel_selection) {
    message_20::PowerDeliveryRequest req;
    req.processing = dt::Processing::Finished;
    req.charge_progress = charge_progress;
    // power_profile omitted in dynamic mode (SECC does not require it) [flow spec §6].
    req.channel_selection = channel_selection;
    return req;
}

Result handle_response(const message_20::PowerDeliveryResponse& res) {
    return {res.response_code < dt::ResponseCode::FAILED};
}

} // namespace power_delivery

using namespace power_delivery;

void PowerDelivery::enter() {
    m_ctx.log.enter_state("PowerDelivery");
}

d20::ev::Result PowerDelivery::feed(Event ev) {
    if (ev == Event::SEND_REQUEST) {
        const auto progress = (m_phase == Phase::Start) ? dt::Progress::Start : dt::Progress::Stop;

        std::optional<dt::ChannelSelection> channel_selection{std::nullopt};
        if (is_bpt_service(m_ctx.evse_info.selected_energy_service)) {
            // Never Standby; charge channel for BPT [flow spec §PowerDelivery].
            channel_selection = dt::ChannelSelection::Charge;
        }

        auto req = create_request(progress, channel_selection);
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

    if (const auto res = variant->get_if<message_20::PowerDeliveryResponse>()) {
        const auto result = handle_response(*res);

        if (not result.valid) {
            m_ctx.log("PowerDelivery failed (response code >= FAILED), terminating session");
            m_ctx.session_stopped = true;
            return {};
        }

        const auto selected_service = m_ctx.evse_info.selected_energy_service;

        if (m_phase == Phase::Start) {
            if (is_dc_service(selected_service)) {
                return m_ctx.create_state<DC_ChargeLoop>();
            }
            // AC branch: proceed into the AC charging loop.
            return m_ctx.create_state<AC_ChargeLoop>();
        }

        // Phase::Stop
        if (is_dc_service(selected_service)) {
            return m_ctx.create_state<DC_WeldingDetection>();
        }
        return m_ctx.create_state<SessionStop>();
    }

    m_ctx.log("expected PowerDeliveryRes! But code type id: %d", variant->get_type());
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::d20::ev::state
