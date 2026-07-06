// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/state/dc_charge_parameter_discovery.hpp>
#include <iso15118/ev/d20/state/schedule_exchange.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>
#include <iso15118/message/dc_charge_parameter_discovery.hpp>

namespace iso15118::ev::d20::state {

namespace {

namespace dt = message_20::datatypes;

void fill_charge_limits(dt::DC_CPDReqEnergyTransferMode& mode, const DcChargeParams& p) {
    mode.max_charge_power = dt::from_float(p.max_charge_power);
    mode.min_charge_power = dt::RationalNumber{0, 0};
    mode.max_charge_current = dt::from_float(p.max_charge_current);
    mode.min_charge_current = dt::RationalNumber{0, 0};
    mode.max_voltage = dt::from_float(p.max_voltage);
    mode.min_voltage = dt::from_float(p.min_voltage);
    mode.target_soc = std::nullopt;
}

} // namespace

void DC_ChargeParameterDiscovery::enter() {
    m_ctx.log.enter_state("DC_ChargeParameterDiscovery");

    const auto p = m_ctx.get_dc_params();

    message_20::DC_ChargeParameterDiscoveryRequest req;
    setup_header(req.header, m_ctx.get_session());

    if (m_ctx.selected_service() == dt::ServiceCategory::DC_BPT) {
        dt::BPT_DC_CPDReqEnergyTransferMode mode{};
        fill_charge_limits(mode, p);
        mode.max_discharge_power = dt::from_float(p.max_discharge_power);
        mode.min_discharge_power = dt::from_float(p.min_discharge_power);
        mode.max_discharge_current = dt::from_float(p.max_discharge_current);
        mode.min_discharge_current = dt::RationalNumber{0, 0};
        req.transfer_mode = mode;
    } else {
        dt::DC_CPDReqEnergyTransferMode mode{};
        fill_charge_limits(mode, p);
        req.transfer_mode = mode;
    }

    m_ctx.respond(req);
}

Result DC_ChargeParameterDiscovery::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    const auto variant = m_ctx.pull_response();

    const auto* res = expect_response<message_20::DC_ChargeParameterDiscoveryResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return {};
    }

    if (m_ctx.selected_service() == dt::ServiceCategory::DC_BPT) {
        const auto* mode = std::get_if<dt::BPT_DC_CPDResEnergyTransferMode>(&res->transfer_mode);
        if (mode == nullptr) {
            logf_error("DC_ChargeParameterDiscoveryResponse offers a non-BPT transfer mode the EV did not request");
            m_ctx.stop_session();
            return {};
        }
        m_ctx.feedback.dc_bpt_limits(*mode);
        return m_ctx.create_state<ScheduleExchange>();
    }

    const auto* mode = std::get_if<dt::DC_CPDResEnergyTransferMode>(&res->transfer_mode);
    if (mode == nullptr) {
        logf_error("DC_ChargeParameterDiscoveryResponse offers a BPT transfer mode the EV did not request");
        m_ctx.stop_session();
        return {};
    }

    return m_ctx.create_state<ScheduleExchange>();
}

} // namespace iso15118::ev::d20::state
