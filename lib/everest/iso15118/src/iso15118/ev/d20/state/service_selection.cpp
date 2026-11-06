// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/state/ac_charge_parameter_discovery.hpp>
#include <iso15118/ev/d20/state/ac_der_iec_charge_parameter_discovery.hpp>
#include <iso15118/ev/d20/state/ac_der_sae_charge_parameter_discovery.hpp>
#include <iso15118/ev/d20/state/dc_charge_parameter_discovery.hpp>
#include <iso15118/ev/d20/state/service_selection.hpp>
#include <iso15118/ev/d20/state/stop_before_start.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>
#include <iso15118/message/service_selection.hpp>

namespace iso15118::ev::d20::state {

Result to_charge_parameter_discovery(Context& ctx) {
    using ServiceCategory = message_20::datatypes::ServiceCategory;
    switch (ctx.selected_service()) {
    case ServiceCategory::AC:
    case ServiceCategory::AC_BPT:
        ctx.send_request(
            AC_ChargeParameterDiscovery::make_request(ctx.get_ac_params(), ctx.ac_connector(), ctx.selected_service()));
        return ctx.create_state<AC_ChargeParameterDiscovery>();
    case ServiceCategory::AC_DER_IEC:
        ctx.send_request(AC_DER_IEC_ChargeParameterDiscovery::make_request(ctx.get_ac_params(), ctx.ac_connector()));
        return ctx.create_state<AC_DER_IEC_ChargeParameterDiscovery>();
    case ServiceCategory::AC_DER_SAE:
        ctx.send_request(AC_DER_SAE_ChargeParameterDiscovery::make_first_request(ctx));
        return ctx.create_state<AC_DER_SAE_ChargeParameterDiscovery>();
    case ServiceCategory::DC:
    case ServiceCategory::DC_BPT:
    case ServiceCategory::MCS:
    case ServiceCategory::MCS_BPT:
        ctx.send_request(DC_ChargeParameterDiscovery::make_request(ctx.get_dc_params(), ctx.selected_service()));
        return ctx.create_state<DC_ChargeParameterDiscovery>();
    case ServiceCategory::WPT:
    case ServiceCategory::DC_ACDP:
    case ServiceCategory::DC_ACDP_BPT:
    case ServiceCategory::Internet:
    case ServiceCategory::ParkingStatus:
        break;
    }

    logf_error("selected service category is not supported by the EV");
    ctx.stop_session();
    return Result::stopping();
}

message_20::ServiceSelectionRequest ServiceSelection::make_request(message_20::datatypes::ServiceCategory service,
                                                                   uint16_t parameter_set_id) {
    message_20::ServiceSelectionRequest req;
    req.selected_energy_transfer_service = {service, parameter_set_id};
    return req;
}

void ServiceSelection::enter() {
    logf_debug("Enter state: ServiceSelection");
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

    if (auto stop = stop_before_start(m_ctx)) {
        return std::move(*stop);
    }

    return to_charge_parameter_discovery(m_ctx);
}

} // namespace iso15118::ev::d20::state
