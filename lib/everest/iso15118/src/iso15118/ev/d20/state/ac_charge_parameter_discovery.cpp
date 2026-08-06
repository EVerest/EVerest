// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/ac_phase_split.hpp>
#include <iso15118/ev/d20/state/ac_charge_parameter_discovery.hpp>
#include <iso15118/ev/d20/state/schedule_exchange.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>
#include <iso15118/message/ac_charge_parameter_discovery.hpp>

namespace iso15118::ev::d20::state {

namespace {

namespace dt = message_20::datatypes;

void assign(std::optional<dt::RationalNumber>& out, const std::optional<float>& value) {
    out = value.has_value() ? std::make_optional(dt::from_float(*value)) : std::nullopt;
}

void fill_charge_limits(dt::AC_CPDReqEnergyTransferMode& mode, const AcChargeParams& p, dt::AcConnector connector) {
    const auto max = split_ac_limit(p.max_charge_power, p.phase_count, connector);
    mode.max_charge_power = dt::from_float(max.base);
    assign(mode.max_charge_power_L2, max.l2);
    assign(mode.max_charge_power_L3, max.l3);

    const auto min = split_ac_limit(p.min_charge_power, p.phase_count, connector);
    mode.min_charge_power = dt::from_float(min.base);
    assign(mode.min_charge_power_L2, min.l2);
    assign(mode.min_charge_power_L3, min.l3);
}

} // namespace

void AC_ChargeParameterDiscovery::enter() {
    logf_debug("Enter state: AC_ChargeParameterDiscovery");

    const auto p = m_ctx.get_ac_params();
    // ServiceDetail always records the connector before this state runs. SinglePhase is the
    // reading under which the base element is never a sum, so it is the safe default.
    const auto connector = m_ctx.selected_ac_connector().value_or(dt::AcConnector::SinglePhase);

    message_20::AC_ChargeParameterDiscoveryRequest req;
    setup_header(req.header, m_ctx.get_session());

    if (m_ctx.selected_service() == dt::ServiceCategory::AC_BPT) {
        dt::BPT_AC_CPDReqEnergyTransferMode mode{};
        fill_charge_limits(mode, p, connector);

        const auto max_discharge = split_ac_limit(p.max_discharge_power, p.phase_count, connector);
        mode.max_discharge_power = dt::from_float(max_discharge.base);
        assign(mode.max_discharge_power_L2, max_discharge.l2);
        assign(mode.max_discharge_power_L3, max_discharge.l3);

        const auto min_discharge = split_ac_limit(p.min_discharge_power, p.phase_count, connector);
        mode.min_discharge_power = dt::from_float(min_discharge.base);
        assign(mode.min_discharge_power_L2, min_discharge.l2);
        assign(mode.min_discharge_power_L3, min_discharge.l3);

        req.transfer_mode = mode;
    } else {
        dt::AC_CPDReqEnergyTransferMode mode{};
        fill_charge_limits(mode, p, connector);
        req.transfer_mode = mode;
    }

    m_ctx.send_request(req);
}

Result AC_ChargeParameterDiscovery::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    const auto variant = m_ctx.pull_response();

    const auto* res = expect_response<message_20::AC_ChargeParameterDiscoveryResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return {};
    }

    if (m_ctx.selected_service() == dt::ServiceCategory::AC_BPT) {
        const auto* mode = std::get_if<dt::BPT_AC_CPDResEnergyTransferMode>(&res->transfer_mode);
        if (mode == nullptr) {
            logf_error("AC_ChargeParameterDiscoveryResponse offers a non-BPT transfer mode the EV did not request");
            m_ctx.stop_session();
            return {};
        }
        m_ctx.feedback.ac_bpt_limits(*mode);
        return m_ctx.create_state<ScheduleExchange>();
    }

    const auto* mode = std::get_if<dt::AC_CPDResEnergyTransferMode>(&res->transfer_mode);
    if (mode == nullptr) {
        logf_error("AC_ChargeParameterDiscoveryResponse offers a BPT transfer mode the EV did not request");
        m_ctx.stop_session();
        return {};
    }
    m_ctx.feedback.ac_limits(*mode);

    return m_ctx.create_state<ScheduleExchange>();
}

} // namespace iso15118::ev::d20::state
