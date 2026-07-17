// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d20/ev/state/service_discovery.hpp>

#include <algorithm>

#include <iso15118/d20/ev/state/service_detail.hpp>
#include <iso15118/d20/ev/timeouts.hpp>

#include <iso15118/detail/d20/ev/state/service_discovery.hpp>
#include <iso15118/detail/d20/ev/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d20::ev::state {

namespace service_discovery {

message_20::ServiceDiscoveryRequest create_request() {
    return {}; // supported_service_ids omitted [flow spec §3, §6]
}

Result handle_response(const message_20::ServiceDiscoveryResponse& res,
                       const std::vector<dt::ServiceCategory>& ev_priority) {
    Result result;
    result.valid = (res.response_code < dt::ResponseCode::FAILED);
    if (not result.valid) {
        return result;
    }

    // Iterate the EV's prioritized list and pick the highest-priority service the SECC also offers.
    for (const auto ev_service : ev_priority) {
        const auto found = std::find_if(res.energy_transfer_service_list.begin(),
                                        res.energy_transfer_service_list.end(),
                                        [ev_service](const dt::Service& s) { return s.service_id == ev_service; });
        if (found != res.energy_transfer_service_list.end()) {
            result.match_found = true;
            result.selected_service = ev_service;
            break;
        }
    }

    return result;
}

} // namespace service_discovery

using namespace service_discovery;

void ServiceDiscovery::enter() {
    m_ctx.log.enter_state("ServiceDiscovery");
}

d20::ev::Result ServiceDiscovery::feed(Event ev) {
    if (ev == Event::SEND_REQUEST) {
        auto req = create_request();
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
        m_ctx.log("ServiceDiscovery message timeout reached, terminating session");
        m_ctx.session_stopped = true;
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    m_ctx.stop_timeout(d20::TimeoutType::SEQUENCE);

    const auto variant = m_ctx.pull_response();

    if (const auto res = variant->get_if<message_20::ServiceDiscoveryResponse>()) {
        const auto result = handle_response(*res, m_ctx.session_config.supported_energy_services);

        if (not result.valid) {
            m_ctx.log("ServiceDiscovery failed, terminating session");
            m_ctx.session_stopped = true;
            return {};
        }

        m_ctx.evse_info.offered_energy_services = res->energy_transfer_service_list;
        m_ctx.evse_info.offered_vas_services = res->vas_list;

        if (not result.match_found) {
            m_ctx.log("No matching energy service offered by the SECC, stopping session");
            m_ctx.pending_stop = dt::ChargingSession::Terminate;
            return m_ctx.create_state<SessionStop>();
        }

        m_ctx.evse_info.selected_energy_service = result.selected_service;

        if (auto stop = stop_if_pending(m_ctx)) {
            return stop;
        }
        return m_ctx.create_state<ServiceDetail>();
    }

    m_ctx.log("expected ServiceDiscoveryRes! But code type id: %d", variant->get_type());
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::d20::ev::state
