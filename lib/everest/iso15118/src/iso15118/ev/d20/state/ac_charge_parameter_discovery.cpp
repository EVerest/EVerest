// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/ac_phase_split.hpp>
#include <iso15118/ev/d20/discharge_sign.hpp>
#include <iso15118/ev/d20/service_family.hpp>
#include <iso15118/ev/d20/state/ac_charge_parameter_discovery.hpp>
#include <iso15118/ev/d20/state/schedule_exchange.hpp>
#include <iso15118/ev/d20/state/stop_before_start.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>
#include <iso15118/message/ac_charge_parameter_discovery.hpp>

namespace iso15118::ev::d20::state {

namespace dt = message_20::datatypes;

namespace {

void fill_charge_limits(dt::AC_CPDReqEnergyTransferMode& mode, const params::AcChargeParams& p,
                        dt::AcConnector connector) {
    emit_ac_limit(p.max_charge_power, p.phase_count, connector, mode.max_charge_power, mode.max_charge_power_L2,
                  mode.max_charge_power_L3);
    emit_ac_limit(p.min_charge_power, p.phase_count, connector, mode.min_charge_power, mode.min_charge_power_L2,
                  mode.min_charge_power_L3);
}

} // namespace

message_20::AC_ChargeParameterDiscoveryRequest
AC_ChargeParameterDiscovery::make_request(const params::AcChargeParams& p, dt::AcConnector connector,
                                          dt::ServiceCategory service) {
    message_20::AC_ChargeParameterDiscoveryRequest req;

    if (is_bpt(service)) {
        dt::BPT_AC_CPDReqEnergyTransferMode mode{};
        fill_charge_limits(mode, p, connector);

        emit_ac_discharge_limits(p, connector, mode);

        req.transfer_mode = mode;
    } else {
        dt::AC_CPDReqEnergyTransferMode mode{};
        fill_charge_limits(mode, p, connector);
        req.transfer_mode = mode;
    }

    return req;
}

void AC_ChargeParameterDiscovery::enter() {
    logf_debug("Enter state: AC_ChargeParameterDiscovery");
}

Result AC_ChargeParameterDiscovery::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return Result::ignored();
    }

    const auto variant = m_ctx.pull_response();

    const auto* res = expect_response<message_20::AC_ChargeParameterDiscoveryResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return Result::stopping();
    }

    if (auto stop = stop_before_start(m_ctx)) {
        return std::move(*stop);
    }

    if (m_ctx.is_bpt()) {
        const auto* mode = std::get_if<dt::BPT_AC_CPDResEnergyTransferMode>(&res->transfer_mode);
        if (mode == nullptr) {
            logf_error("AC_ChargeParameterDiscoveryResponse offers a non-BPT transfer mode the EV did not request");
            m_ctx.stop_session();
            return Result::stopping();
        }
        m_ctx.feedback.ac_bpt_limits(*mode);
        m_ctx.send_request(ScheduleExchange::make_request(m_ctx));
        return m_ctx.create_state<ScheduleExchange>();
    }

    const auto* mode = std::get_if<dt::AC_CPDResEnergyTransferMode>(&res->transfer_mode);
    if (mode == nullptr) {
        logf_error("AC_ChargeParameterDiscoveryResponse offers a BPT transfer mode the EV did not request");
        m_ctx.stop_session();
        return Result::stopping();
    }
    m_ctx.feedback.ac_limits(*mode);

    m_ctx.send_request(ScheduleExchange::make_request(m_ctx));
    return m_ctx.create_state<ScheduleExchange>();
}

} // namespace iso15118::ev::d20::state
