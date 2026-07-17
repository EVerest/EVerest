// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/ev/state/charging_status.hpp>

#include <iso15118/d2/ev/state/power_delivery.hpp>
#include <iso15118/d2/ev/timeouts.hpp>

#include <iso15118/detail/d2/ev/state/charging_status.hpp>
#include <iso15118/detail/d2/ev/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

#include <iso15118/message/common_types.hpp>

namespace iso15118::d2::ev::state {

namespace charging_status {

message_2::ChargingStatusRequest create_request() {
    return {};
}

Result handle_response(const message_2::ChargingStatusResponse& res) {
    Result result;
    result.valid = (res.response_code < dt::ResponseCode::FAILED);
    if (not result.valid) {
        return result;
    }

    result.notification = res.ac_evse_status.notification;
    result.renegotiation_or_receipt =
        (res.ac_evse_status.notification == dt::EVSENotification::ReNegotiation) or res.receipt_required.value_or(false);
    result.evse_max_current = res.evse_max_current;
    result.sa_schedule_tuple_id = res.sa_schedule_tuple_id;
    return result;
}

d20::AcTargetPower compute_ac_target_power(const std::optional<dt::PhysicalValue>& evse_max_current,
                                           const std::optional<dt::PhysicalValue>& nominal_voltage) {
    d20::AcTargetPower target;
    if (not evse_max_current.has_value()) {
        return target;
    }

    const auto current = dt::from_physical_value(evse_max_current.value());
    const auto voltage = nominal_voltage.has_value() ? dt::from_physical_value(nominal_voltage.value()) : 230.0;
    target.target_active_power = message_20::datatypes::from_float(static_cast<float>(current * voltage));
    return target;
}

} // namespace charging_status

using namespace charging_status;

void ChargingStatus::enter() {
    m_ctx.log.enter_state("ChargingStatus");
}

void ChargingStatus::send(Event) {
    auto req = create_request();
    m_ctx.setup_header(req.header);
    m_ctx.send_request(req);
    m_ctx.start_timeout(d20::TimeoutType::SEQUENCE, MESSAGE_TIMEOUT_CHARGING_STATUS_MS);
}

d2::ev::Result ChargingStatus::feed(Event ev) {
    if (ev == Event::SEND_REQUEST) {
        send(ev);
        return {};
    }

    if (ev == Event::CONTROL_MESSAGE) {
        if (const auto* stop = m_ctx.get_control_event<StopCharging>(); stop and static_cast<bool>(*stop)) {
            stop_requested = true;
            stop_reason = dt::ChargingSession::Terminate;
        } else if (const auto* pause = m_ctx.get_control_event<PauseCharging>(); pause and static_cast<bool>(*pause)) {
            stop_requested = true;
            stop_reason = dt::ChargingSession::Pause;
        }
        return {};
    }

    if (ev == Event::TIMEOUT) {
        m_ctx.log("ChargingStatus message timeout reached, terminating session");
        m_ctx.session_stopped = true;
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    m_ctx.stop_timeout(d20::TimeoutType::SEQUENCE);

    const auto variant = m_ctx.pull_response();

    if (const auto res = variant->get_if<message_2::ChargingStatusResponse>()) {
        const auto result = handle_response(*res);

        if (not result.valid) {
            m_ctx.log("ChargingStatus failed, terminating session");
            m_ctx.session_stopped = true;
            return {};
        }

        if (first_response) {
            m_ctx.feedback.signal(session::ev::feedback::Signal::CHARGE_LOOP_STARTED);
            first_response = false;
        }

        // Publish the SECC target power on every ChargingStatusRes [flow spec §4.3].
        m_ctx.feedback.ac_evse_target_power(
            compute_ac_target_power(result.evse_max_current, m_ctx.evse_info.ac_nominal_voltage));

        // EVSE-driven stop via AC_EVSEStatus EVSENotification [flow spec §4.4].
        if (result.notification.has_value() and result.notification.value() == dt::EVSENotification::StopCharging) {
            m_ctx.feedback.stop_from_charger();
            m_ctx.pending_stop = dt::ChargingSession::Terminate;
            return m_ctx.create_state<PowerDelivery>(PowerDelivery::Phase::Stop);
        }

        // EVSE-requested schedule renegotiation: perform it. The EV re-enters PowerDelivery with
        // ChargeProgress=Renegotiate, which the SECC answers by returning to ChargeParameterDiscovery for
        // a new SAScheduleList; the session stays alive.
        if (result.notification.has_value() and result.notification.value() == dt::EVSENotification::ReNegotiation) {
            m_ctx.log("ChargingStatus received ReNegotiation; renegotiating via PowerDelivery(Renegotiate)");
            return m_ctx.create_state<PowerDelivery>(PowerDelivery::Phase::Renegotiate);
        }

        // ReceiptRequired: EIM has no MeteringReceipt path, so gracefully terminate via
        // PowerDelivery(Stop) -> SessionStop instead of continuing the loop.
        if (result.renegotiation_or_receipt) {
            m_ctx.log("ChargingStatus received ReceiptRequired (unsupported under EIM), "
                      "terminating session gracefully");
            m_ctx.pending_stop = dt::ChargingSession::Terminate;
            return m_ctx.create_state<PowerDelivery>(PowerDelivery::Phase::Stop);
        }

        // EV-initiated stop/pause (StopCharging/PauseCharging control event).
        if (stop_requested) {
            m_ctx.pending_stop = stop_reason;
            return m_ctx.create_state<PowerDelivery>(PowerDelivery::Phase::Stop);
        }

        send(ev);
        return {};
    }

    m_ctx.log("expected ChargingStatusRes! But code type id: %d", variant->get_type());
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::d2::ev::state
