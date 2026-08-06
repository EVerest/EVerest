// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/ac_phase_split.hpp>
#include <iso15118/ev/d20/state/ac_der_iec_charge_parameter_discovery.hpp>
#include <iso15118/ev/d20/state/schedule_exchange.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>
#include <iso15118/message/ac_der_iec_charge_parameter_discovery.hpp>

namespace iso15118::ev::d20::state {

namespace dt = message_20::datatypes;

void AC_DER_IEC_ChargeParameterDiscovery::enter() {
    logf_debug("Enter state: AC_DER_IEC_ChargeParameterDiscovery");

    const auto p = m_ctx.get_ac_params();
    const auto connector = m_ctx.selected_ac_connector().value_or(dt::AcConnector::SinglePhase);

    const auto assign = [](std::optional<dt::RationalNumber>& out, const std::optional<float>& value) {
        out = value.has_value() ? std::make_optional(dt::from_float(*value)) : std::nullopt;
    };

    dt::DER_AC_CPDReqEnergyTransferMode mode{};

    const auto max = split_ac_limit(p.max_charge_power, p.phase_count, connector);
    mode.max_charge_power = dt::from_float(max.base);
    assign(mode.max_charge_power_L2, max.l2);
    assign(mode.max_charge_power_L3, max.l3);

    const auto min = split_ac_limit(p.min_charge_power, p.phase_count, connector);
    mode.min_charge_power = dt::from_float(min.base);
    assign(mode.min_charge_power_L2, min.l2);
    assign(mode.min_charge_power_L3, min.l3);

    const auto max_discharge = split_ac_limit(p.max_discharge_power, p.phase_count, connector);
    mode.max_discharge_power = dt::from_float(max_discharge.base);
    assign(mode.max_discharge_power_L2, max_discharge.l2);
    assign(mode.max_discharge_power_L3, max_discharge.l3);

    const auto min_discharge = split_ac_limit(p.min_discharge_power, p.phase_count, connector);
    mode.min_discharge_power = dt::from_float(min_discharge.base);
    assign(mode.min_discharge_power_L2, min_discharge.l2);
    assign(mode.min_discharge_power_L3, min_discharge.l3);
    mode.processing = dt::Processing::Finished;

    message_20::DER_AC_ChargeParameterDiscoveryRequest req;
    setup_header(req.header, m_ctx.get_session());
    req.transfer_mode = mode;
    m_ctx.send_request(req);
}

Result AC_DER_IEC_ChargeParameterDiscovery::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    const auto variant = m_ctx.pull_response();

    const auto* res = expect_response<message_20::DER_AC_ChargeParameterDiscoveryResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return {};
    }

    // ac_limits sees the AC base; der_curves carries the dictated DER functions.
    m_ctx.feedback.ac_limits(res->transfer_mode);
    m_ctx.feedback.der_curves(res->transfer_mode.der_control);

    return m_ctx.create_state<ScheduleExchange>();
}

} // namespace iso15118::ev::d20::state
