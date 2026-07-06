// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <algorithm>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/state/service_detail.hpp>
#include <iso15118/ev/d20/state/service_discovery.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>
#include <iso15118/message/service_discovery.hpp>

namespace iso15118::ev::d20::state {

void ServiceDiscovery::enter() {
    m_ctx.log.enter_state("ServiceDiscovery");

    message_20::ServiceDiscoveryRequest req;
    setup_header(req.header, m_ctx.get_session());
    req.supported_service_ids = message_20::datatypes::ServiceIdList{
        message_20::to_underlying_value(message_20::datatypes::ServiceCategory::DC)};
    m_ctx.respond(req);
}

Result ServiceDiscovery::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    const auto variant = m_ctx.pull_response();

    const auto* res = expect_response<message_20::ServiceDiscoveryResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return {};
    }

    const auto& services = res->energy_transfer_service_list;
    const auto dc_offered = std::any_of(services.begin(), services.end(), [](const auto& service) {
        return service.service_id == message_20::datatypes::ServiceCategory::DC;
    });
    if (not dc_offered) {
        logf_error("ServiceDiscoveryResponse does not offer the DC energy transfer service");
        m_ctx.stop_session();
        return {};
    }

    return m_ctx.create_state<ServiceDetail>();
}

} // namespace iso15118::ev::d20::state
