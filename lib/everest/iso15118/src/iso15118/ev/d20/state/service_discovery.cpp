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

namespace {

using ResponseCode = message_20::datatypes::ResponseCode;

bool check_response_code(ResponseCode response_code) {
    switch (response_code) {
    case ResponseCode::OK:
        return true;
    default:
        logf_warning("Unexpected response code received: %d", static_cast<int>(response_code));
        return iso15118::ev::d20::check_response_code(response_code);
    }
}

} // namespace

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

    const auto res = variant->get_if<message_20::ServiceDiscoveryResponse>();
    if (res == nullptr) {
        logf_error("Expected ServiceDiscoveryResponse, got code type id: %d", static_cast<int>(variant->get_type()));
        m_ctx.stop_session(true);
        return {};
    }

    if (res->header.session_id != m_ctx.get_session().get_id()) {
        logf_error("ServiceDiscoveryResponse session_id does not match current session");
        m_ctx.stop_session(true);
        return {};
    }

    if (not check_response_code(res->response_code)) {
        logf_error("ServiceDiscoveryResponse rejected with response_code: %d", static_cast<int>(res->response_code));
        m_ctx.stop_session(true);
        return {};
    }

    const auto& services = res->energy_transfer_service_list;
    const auto dc_offered = std::any_of(services.begin(), services.end(), [](const auto& service) {
        return service.service_id == message_20::datatypes::ServiceCategory::DC;
    });
    if (not dc_offered) {
        logf_error("ServiceDiscoveryResponse does not offer the DC energy transfer service");
        m_ctx.stop_session(true);
        return {};
    }

    return m_ctx.create_state<ServiceDetail>();
}

} // namespace iso15118::ev::d20::state
