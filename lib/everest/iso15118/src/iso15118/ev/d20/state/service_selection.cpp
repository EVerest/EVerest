// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/state/ac_charge_parameter_discovery.hpp>
#include <iso15118/ev/d20/state/ac_der_iec_charge_parameter_discovery.hpp>
#include <iso15118/ev/d20/state/dc_charge_parameter_discovery.hpp>
#include <iso15118/ev/d20/state/service_selection.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>
#include <iso15118/message/service_selection.hpp>

namespace iso15118::ev::d20::state {

void ServiceSelection::enter() {
    logf_debug("Enter state: ServiceSelection");

    message_20::ServiceSelectionRequest req;
    setup_header(req.header, m_ctx.get_session());
    req.selected_energy_transfer_service = {m_ctx.selected_service(), m_parameter_set_id};
    m_ctx.send_request(req);
}

Result ServiceSelection::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return Result::ignored();
    }

    const auto variant = m_ctx.pull_response();

    const auto* res = expect_response<message_20::ServiceSelectionResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return Result::stopping();
    }

    // AC_DER_IEC is part of the AC family, so it must be matched before the
    // family test or it would take the plain AC parameter discovery.
    if (m_ctx.selected_service() == message_20::datatypes::ServiceCategory::AC_DER_IEC) {
        return m_ctx.create_state<AC_DER_IEC_ChargeParameterDiscovery>();
    }

    if (m_ctx.is_ac_family()) {
        return m_ctx.create_state<AC_ChargeParameterDiscovery>();
    }

    const auto service = m_ctx.selected_service();
    if (service == message_20::datatypes::ServiceCategory::DC or
        service == message_20::datatypes::ServiceCategory::DC_BPT) {
        return m_ctx.create_state<DC_ChargeParameterDiscovery>();
    }

    logf_error("selected service category is not supported by the EV");
    m_ctx.stop_session();
    return Result::stopping();
}

} // namespace iso15118::ev::d20::state
