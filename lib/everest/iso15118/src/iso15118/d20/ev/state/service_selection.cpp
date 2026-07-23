// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d20/ev/state/service_selection.hpp>

#include <iso15118/d20/ev/state/ac_charge_parameter_discovery.hpp>
#include <iso15118/d20/ev/state/dc_charge_parameter_discovery.hpp>
#include <iso15118/d20/ev/timeouts.hpp>

#include <iso15118/detail/d20/ev/state/service_selection.hpp>
#include <iso15118/detail/d20/ev/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d20::ev::state {

namespace service_selection {

message_20::ServiceSelectionRequest create_request(dt::ServiceCategory energy_service, uint16_t parameter_set_id) {
    message_20::ServiceSelectionRequest req;
    req.selected_energy_transfer_service = {energy_service, parameter_set_id};
    // selected_vas_list omitted [flow spec §6]
    return req;
}

Result handle_response(const message_20::ServiceSelectionResponse& res) {
    return {res.response_code < dt::ResponseCode::FAILED};
}

} // namespace service_selection

using namespace service_selection;

void ServiceSelection::enter() {
    m_ctx.log.enter_state("ServiceSelection");
}

d20::ev::Result ServiceSelection::feed(Event ev) {
    if (ev == Event::SEND_REQUEST) {
        auto req = create_request(m_ctx.evse_info.selected_energy_service, m_ctx.evse_info.selected_parameter_set_id);
        m_ctx.setup_header(req.header);
        m_ctx.send_request(req);
        m_ctx.start_timeout(d20::TimeoutType::SEQUENCE, MESSAGE_TIMEOUT_MS);
        return {};
    }

    if (ev == Event::CONTROL_MESSAGE) {
        handle_stop_control_event(m_ctx);
        return {};
    }

    if (ev == Event::TIMEOUT) {
        m_ctx.log("ServiceSelection message timeout reached, terminating session");
        m_ctx.session_stopped = true;
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    m_ctx.stop_timeout(d20::TimeoutType::SEQUENCE);

    const auto variant = m_ctx.pull_response();

    if (const auto res = variant->get_if<message_20::ServiceSelectionResponse>()) {
        const auto result = handle_response(*res);

        if (not result.valid) {
            m_ctx.log("ServiceSelection failed, terminating session");
            m_ctx.session_stopped = true;
            return {};
        }

        if (auto stop = stop_if_pending(m_ctx)) {
            return stop;
        }

        if (is_ac_service(m_ctx.evse_info.selected_energy_service)) {
            return m_ctx.create_state<AC_ChargeParameterDiscovery>();
        }
        return m_ctx.create_state<DC_ChargeParameterDiscovery>();
    }

    m_ctx.log("expected ServiceSelectionRes! But code type id: %d", variant->get_type());
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::d20::ev::state
