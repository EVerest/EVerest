// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/ac_phase_split.hpp>
#include <iso15118/ev/d20/discharge_sign.hpp>
#include <iso15118/ev/d20/state/ac_der_iec_charge_parameter_discovery.hpp>
#include <iso15118/ev/d20/state/schedule_exchange.hpp>
#include <iso15118/ev/d20/state/stop_before_start.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>
#include <iso15118/message/ac_der_iec_charge_parameter_discovery.hpp>

namespace iso15118::ev::d20::state {

namespace dt = message_20::datatypes;

message_20::DER_AC_ChargeParameterDiscoveryRequest
AC_DER_IEC_ChargeParameterDiscovery::make_request(const params::AcChargeParams& p, dt::AcConnector connector) {
    dt::DER_AC_CPDReqEnergyTransferMode mode{};
    emit_ac_limit(p.max_charge_power, p.phase_count, connector, mode.max_charge_power, mode.max_charge_power_L2,
                  mode.max_charge_power_L3);
    emit_ac_limit(p.min_charge_power, p.phase_count, connector, mode.min_charge_power, mode.min_charge_power_L2,
                  mode.min_charge_power_L3);
    emit_ac_discharge_limits(p, connector, mode);
    mode.processing = dt::Processing::Finished;

    message_20::DER_AC_ChargeParameterDiscoveryRequest req;
    req.transfer_mode = mode;
    return req;
}

void AC_DER_IEC_ChargeParameterDiscovery::enter() {
    logf_debug("Enter state: AC_DER_IEC_ChargeParameterDiscovery");
}

Result AC_DER_IEC_ChargeParameterDiscovery::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return Result::ignored();
    }

    const auto variant = m_ctx.pull_response();

    const auto* res = expect_response<message_20::DER_AC_ChargeParameterDiscoveryResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return Result::stopping();
    }

    if (auto stop = stop_before_start(m_ctx)) {
        return std::move(*stop);
    }

    // ac_der_limits carries the whole DER response, discharge limits included; der_curves is the
    // narrower channel for the dictated DER functions alone.
    m_ctx.feedback.ac_der_limits(res->transfer_mode);
    m_ctx.feedback.der_curves(res->transfer_mode.der_control);

    m_ctx.send_request(ScheduleExchange::make_request(m_ctx));
    return m_ctx.create_state<ScheduleExchange>();
}

} // namespace iso15118::ev::d20::state
