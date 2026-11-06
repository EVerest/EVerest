// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/d2/state/service_detail.hpp>

#include <algorithm>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d2/state/payment_service_selection.hpp>
#include <iso15118/ev/detail/d2/context_helper.hpp>
#include <iso15118/message_2/service_detail.hpp>

namespace iso15118::ev::d2::state {

void ServiceDetail::enter() {
    logf_debug("Enter state: ServiceDetail (ISO 15118-2)");
}

Result ServiceDetail::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return Result::ignored();
    }
    const auto variant = m_ctx.pull_response();
    const auto* res = expect_response<message_2::ServiceDetailResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return Result::stopping();
    }
    if (auto stop = stop_before_start(m_ctx)) {
        return std::move(*stop);
    }
    m_ctx.evse_info.certificate_installation_offered =
        res->service_id == message_2::datatypes::CERTIFICATE_SERVICE_ID and res->service_parameter_list.has_value() and
        std::any_of(res->service_parameter_list->begin(), res->service_parameter_list->end(),
                    [](const auto& set) { return set.parameter_set_id == 1; });
    if (not m_ctx.evse_info.certificate_installation_offered) {
        logf_warning("ServiceDetailResponse does not offer certificate installation (service_id %u)", res->service_id);
    }
    return to_payment_service_selection(m_ctx);
}

} // namespace iso15118::ev::d2::state
