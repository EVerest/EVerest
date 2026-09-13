// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/d2/state/current_demand.hpp>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d2/state/metering_receipt.hpp>
#include <iso15118/ev/d2/state/power_delivery.hpp>
#include <iso15118/ev/detail/d2/context_helper.hpp>
#include <iso15118/ev/detail/d2/state/current_demand.hpp>
#include <iso15118/ev/detail/d2/state/state_helper.hpp>

namespace iso15118::ev::d2::state {

namespace current_demand {

message_2::CurrentDemandRequest create_request(const DcChargeParams& params) {
    message_2::CurrentDemandRequest req;
    req.dc_ev_status = make_dc_ev_status(params, true);
    req.ev_target_current = dt::to_physical_value(params.target_current, dt::Unit::A);
    req.ev_target_voltage = dt::to_physical_value(params.target_voltage, dt::Unit::V);
    req.ev_maximum_current_limit = dt::to_physical_value(params.max_charge_current, dt::Unit::A);
    if (params.max_charge_power > 0.0f) {
        req.ev_maximum_power_limit = dt::to_physical_value(params.max_charge_power, dt::Unit::W);
    }
    // Table 55 [V2G2-258]: TRUE means full charge (100 % SOC) reached.
    req.charging_complete = (params.present_soc >= 100.0);
    return req;
}

Result handle_response(const message_2::CurrentDemandResponse& res) {
    Result result;
    result.notification = res.dc_evse_status.notification;

    const auto status_code = res.dc_evse_status.status_code;
    const bool status_stop = (status_code == dt::DC_EVSEStatusCode::EVSE_Shutdown) or
                             (status_code == dt::DC_EVSEStatusCode::EVSE_EmergencyShutdown) or
                             (status_code == dt::DC_EVSEStatusCode::EVSE_Malfunction) or
                             (status_code == dt::DC_EVSEStatusCode::EVSE_NotReady);
    result.charger_requested_stop = status_stop or (result.notification == dt::EVSENotification::StopCharging);
    result.receipt_required = res.receipt_required.value_or(false);
    return result;
}

std::optional<session::feedback::DcMaximumLimits> evse_present_limits(const message_2::CurrentDemandResponse& res) {
    if (not res.evse_maximum_voltage_limit.has_value() and not res.evse_maximum_current_limit.has_value() and
        not res.evse_maximum_power_limit.has_value()) {
        return std::nullopt;
    }
    session::feedback::DcMaximumLimits limits;
    if (res.evse_maximum_voltage_limit.has_value()) {
        limits.voltage = static_cast<float>(dt::from_physical_value(res.evse_maximum_voltage_limit.value()));
    }
    if (res.evse_maximum_current_limit.has_value()) {
        limits.current = static_cast<float>(dt::from_physical_value(res.evse_maximum_current_limit.value()));
    }
    if (res.evse_maximum_power_limit.has_value()) {
        limits.power = static_cast<float>(dt::from_physical_value(res.evse_maximum_power_limit.value()));
    }
    return limits;
}

} // namespace current_demand

void CurrentDemand::enter() {
    logf_debug("Enter state: CurrentDemand (ISO 15118-2)");
    m_ctx.send_request(current_demand::create_request(m_ctx.get_dc_params()));
}

Result CurrentDemand::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return Result::ignored();
    }

    const auto variant = m_ctx.pull_response();
    const auto* res = expect_response<message_2::CurrentDemandResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return Result::stopping();
    }

    const auto result = current_demand::handle_response(*res);

    if (const auto limits = current_demand::evse_present_limits(*res)) {
        m_ctx.feedback.dc_evse_present_limits(*limits);
    }

    if (result.charger_requested_stop) {
        m_ctx.feedback.stop_from_charger();
        m_ctx.set_stop_charging_requested(true);
        return m_ctx.create_state<PowerDelivery>(PowerDelivery::Phase::Stop);
    }

    // ISO 15118-2 has no Pause notification; a renegotiation is not supported, so end cleanly.
    if (result.notification == dt::EVSENotification::ReNegotiation) {
        logf_warning("CurrentDemand received ReNegotiation, which is not supported; stopping the session");
        m_ctx.set_stop_charging_requested(true);
        return m_ctx.create_state<PowerDelivery>(PowerDelivery::Phase::Stop);
    }

    if (m_ctx.is_stop_charging_requested() or m_ctx.is_pause_charging_requested()) {
        return m_ctx.create_state<PowerDelivery>(PowerDelivery::Phase::Stop);
    }

    if (result.receipt_required) {
        if (m_ctx.pnc.contract_selected and res->meter_info.has_value()) {
            return m_ctx.create_state<MeteringReceipt>(res->meter_info.value(), res->sa_schedule_tuple_id);
        }
        // Without a contract there is nothing to sign; the loop continues.
        if (not receipt_warned) {
            receipt_warned = true;
            logf_warning("CurrentDemand received ReceiptRequired without a Contract session or MeterInfo; "
                         "ignoring it for this session");
        }
    }

    m_ctx.send_request(current_demand::create_request(m_ctx.get_dc_params()));
    return Result::awaiting();
}

} // namespace iso15118::ev::d2::state
