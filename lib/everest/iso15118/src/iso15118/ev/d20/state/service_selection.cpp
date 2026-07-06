// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/state/dc_charge_parameter_discovery.hpp>
#include <iso15118/ev/d20/state/service_selection.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>
#include <iso15118/message/service_selection.hpp>

namespace iso15118::ev::d20::state {

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

    const auto* res = expect_response<message_20::ServiceSelectionResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return {};
    }

    return m_ctx.create_state<DC_ChargeParameterDiscovery>();
}

} // namespace iso15118::ev::d20::state
