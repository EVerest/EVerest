// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/state/dc_charge_parameter_discovery.hpp>
#include <iso15118/ev/d20/state/service_selection.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>
#include <iso15118/message/service_selection.hpp>

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

void ServiceSelection::enter() {
    m_ctx.log.enter_state("ServiceSelection");

    message_20::ServiceSelectionRequest req;
    setup_header(req.header, m_ctx.get_session());
    req.selected_energy_transfer_service = {message_20::datatypes::ServiceCategory::DC, m_parameter_set_id};
    m_ctx.respond(req);
}

Result ServiceSelection::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    const auto variant = m_ctx.pull_response();

    const auto res = variant->get_if<message_20::ServiceSelectionResponse>();
    if (res == nullptr) {
        logf_error("Expected ServiceSelectionResponse, got code type id: %d", static_cast<int>(variant->get_type()));
        m_ctx.stop_session(true);
        return {};
    }

    if (res->header.session_id != m_ctx.get_session().get_id()) {
        logf_error("ServiceSelectionResponse session_id does not match current session");
        m_ctx.stop_session(true);
        return {};
    }

    if (not check_response_code(res->response_code)) {
        logf_error("ServiceSelectionResponse rejected with response_code: %d", static_cast<int>(res->response_code));
        m_ctx.stop_session(true);
        return {};
    }

    return m_ctx.create_state<DC_ChargeParameterDiscovery>();
}

} // namespace iso15118::ev::d20::state
