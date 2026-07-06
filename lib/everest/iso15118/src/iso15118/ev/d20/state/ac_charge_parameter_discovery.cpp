// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/state/ac_charge_parameter_discovery.hpp>
#include <iso15118/ev/d20/state/schedule_exchange.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>
#include <iso15118/message/ac_charge_parameter_discovery.hpp>

namespace iso15118::ev::d20::state {

namespace {

namespace dt = message_20::datatypes;

void fill_charge_limits(dt::AC_CPDReqEnergyTransferMode& mode, const AcChargeParams& p) {
    mode.max_charge_power = dt::from_float(p.max_charge_power);
    mode.min_charge_power = dt::from_float(p.min_charge_power);
    if (p.three_phase) {
        mode.max_charge_power_L2 = mode.max_charge_power;
        mode.max_charge_power_L3 = mode.max_charge_power;
        mode.min_charge_power_L2 = mode.min_charge_power;
        mode.min_charge_power_L3 = mode.min_charge_power;
    }
}

} // namespace

void AC_ChargeParameterDiscovery::enter() {
    m_ctx.log.enter_state("AC_ChargeParameterDiscovery");

    const auto p = m_ctx.get_ac_params();

    message_20::AC_ChargeParameterDiscoveryRequest req;
    setup_header(req.header, m_ctx.get_session());

    if (m_ctx.selected_service() == dt::ServiceCategory::AC_BPT) {
        dt::BPT_AC_CPDReqEnergyTransferMode mode{};
        fill_charge_limits(mode, p);
        mode.max_discharge_power = dt::from_float(p.max_discharge_power);
        mode.min_discharge_power = dt::from_float(p.min_discharge_power);
        if (p.three_phase) {
            mode.max_discharge_power_L2 = mode.max_discharge_power;
            mode.max_discharge_power_L3 = mode.max_discharge_power;
            mode.min_discharge_power_L2 = mode.min_discharge_power;
            mode.min_discharge_power_L3 = mode.min_discharge_power;
        }
        req.transfer_mode = mode;
    } else {
        dt::AC_CPDReqEnergyTransferMode mode{};
        fill_charge_limits(mode, p);
        req.transfer_mode = mode;
    }

    m_ctx.respond(req);
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
