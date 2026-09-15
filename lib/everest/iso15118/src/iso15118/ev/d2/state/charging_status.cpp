// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/d2/state/charging_status.hpp>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d2/state/metering_receipt.hpp>
#include <iso15118/ev/d2/state/power_delivery.hpp>
#include <iso15118/ev/detail/d2/context_helper.hpp>
#include <iso15118/ev/detail/d2/state/charging_status.hpp>
#include <iso15118/message/common_types.hpp>

namespace iso15118::ev::d2::state {

namespace charging_status {

message_2::ChargingStatusRequest create_request() {
    return {};
}

Result handle_response(const message_2::ChargingStatusResponse& res) {
    Result result;
    result.notification = res.ac_evse_status.notification;
    result.receipt_required = res.receipt_required.value_or(false);
    result.evse_max_current = res.evse_max_current;
    return result;
}

iso15118::d20::AcTargetPower compute_ac_target_power(const std::optional<dt::PhysicalValue>& evse_max_current,
                                                     const std::optional<dt::PhysicalValue>& nominal_voltage) {
    iso15118::d20::AcTargetPower target;
    if (not evse_max_current.has_value()) {
        return target;
    }
    const auto current = dt::from_physical_value(evse_max_current.value());
    const auto voltage = nominal_voltage.has_value() ? dt::from_physical_value(nominal_voltage.value()) : 230.0;
    target.target_active_power = message_20::datatypes::from_float(static_cast<float>(current * voltage));
    return target;
}

} // namespace charging_status

void ChargingStatus::enter() {
    logf_debug("Enter state: ChargingStatus (ISO 15118-2)");
    m_ctx.send_request(charging_status::create_request());
}

Result ChargingStatus::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return Result::ignored();
    }

    const auto variant = m_ctx.pull_response();
    const auto* res = expect_response<message_2::ChargingStatusResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return Result::stopping();
    }

    const auto result = charging_status::handle_response(*res);

    m_ctx.feedback.ac_target_power(
        charging_status::compute_ac_target_power(result.evse_max_current, m_ctx.evse_info.ac_nominal_voltage));

    if (result.notification == dt::EVSENotification::StopCharging) {
        m_ctx.feedback.stop_from_charger();
        m_ctx.set_stop_charging_requested(true);
        return m_ctx.create_state<PowerDelivery>(PowerDelivery::Phase::Stop);
    }

    // ISO 15118-2 has no Pause notification; a renegotiation is not supported, so end cleanly.
    if (result.notification == dt::EVSENotification::ReNegotiation) {
        logf_warning("ChargingStatus received ReNegotiation, which is not supported; stopping the session");
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
        if (not receipt_warned) {
            receipt_warned = true;
            logf_warning("ChargingStatus received ReceiptRequired without a Contract session or MeterInfo; "
                         "ignoring it for this session");
        }
    }

    m_ctx.send_request(charging_status::create_request());
    return Result::awaiting();
}

} // namespace iso15118::ev::d2::state
