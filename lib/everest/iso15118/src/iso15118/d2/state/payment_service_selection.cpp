// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/state/payment_service_selection.hpp>
#include <iso15118/d2/state/authorization.hpp>
#include <iso15118/message/d2/payment_service_selection.hpp>
#include <iso15118/detail/d2/context_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d2::state {

namespace dt = msg::data_types;

void PaymentServiceSelection::enter() {}

Result PaymentServiceSelection::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    const auto variant = m_ctx.pull_request();

    if (const auto req = variant->get_if<msg::PaymentServiceSelectionRequest>()) {
        msg::PaymentServiceSelectionResponse res;
        setup_header(res.header, m_ctx.session);
        response_with_code(res, dt::ResponseCode::OK);
        m_ctx.respond(res);
        return m_ctx.create_state<Authorization>();
    }

    const msg::Type req_type = variant->get_type();
    send_sequence_error(req_type, m_ctx);
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::d2::state
