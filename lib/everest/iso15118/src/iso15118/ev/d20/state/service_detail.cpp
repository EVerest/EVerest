// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/state/service_detail.hpp>
#include <iso15118/ev/d20/state/service_selection.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>
#include <iso15118/message/service_detail.hpp>

namespace iso15118::ev::d20::state {

void ServiceDetail::enter() {
    m_ctx.log.enter_state("ServiceDetail");

    message_20::ServiceDetailRequest req;
    setup_header(req.header, m_ctx.get_session());
    req.service = message_20::to_underlying_value(message_20::datatypes::ServiceCategory::DC);
    m_ctx.respond(req);
}

Result ServiceDetail::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    const auto variant = m_ctx.pull_response();

    const auto* res = expect_response<message_20::ServiceDetailResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return {};
    }

    if (res->service_parameter_list.empty()) {
        logf_error("ServiceDetailResponse carries no parameter sets");
        m_ctx.stop_session();
        return {};
    }

    const auto parameter_set_id = res->service_parameter_list.front().id;
    return m_ctx.create_state<ServiceSelection>(parameter_set_id);
}

} // namespace iso15118::ev::d20::state
