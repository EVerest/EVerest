// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/state/dc_charge_loop.hpp>

#include <iso15118/detail/d2/state/power_delivery.hpp>

#include <iso15118/message/common_types.hpp>
#include <iso15118/message/dc_charge_loop.hpp>

#include <iso15118/d2/state/metering_receipt.hpp>
#include <iso15118/detail/d2/state/current_demand.hpp>
#include <iso15118/detail/d2/state/sequence_error.hpp>
#include <iso15118/detail/d2/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d2::state {

using dt::from_physical_value;
using dt::to_physical_value;
using dt::Unit;

namespace {

namespace m20dt = message_20::datatypes;

m20dt::Scheduled_DC_CLReqControlMode build_ev_setpoint(const message_2::CurrentDemandRequest& req) {
    m20dt::Scheduled_DC_CLReqControlMode mode{};
    mode.target_voltage = m20dt::from_float(static_cast<float>(from_physical_value(req.ev_target_voltage)));
    mode.target_current = m20dt::from_float(static_cast<float>(from_physical_value(req.ev_target_current)));

    // Each EVMaximum*Limit is optional on its own, so forward whichever the EV sent rather than
    // dropping all three when one is missing.
    if (req.ev_maximum_voltage_limit.has_value()) {
        mode.max_voltage = m20dt::from_float(static_cast<float>(from_physical_value(*req.ev_maximum_voltage_limit)));
    }
    if (req.ev_maximum_current_limit.has_value()) {
        mode.max_charge_current =
            m20dt::from_float(static_cast<float>(from_physical_value(*req.ev_maximum_current_limit)));
    }
    if (req.ev_maximum_power_limit.has_value()) {
        mode.max_charge_power = m20dt::from_float(static_cast<float>(from_physical_value(*req.ev_maximum_power_limit)));
    }
    return mode;
}

} // namespace

message_2::CurrentDemandResponse handle_request(const dt::SessionId& session_id, const d2::SessionConfig& config,
                                                float present_voltage, float present_current,
                                                uint8_t sa_schedule_tuple_id, bool charger_stop, bool request_receipt,
                                                const std::optional<dt::MeterInfo>& meter_info, bool pnc_selected) {
    message_2::CurrentDemandResponse res;
    res.header.session_id = session_id;
    res.response_code = dt::ResponseCode::OK;

    res.dc_evse_status.notification = charger_stop ? dt::EVSENotification::StopCharging : dt::EVSENotification::None;
    res.dc_evse_status.notification_max_delay = 0;
    res.dc_evse_status.isolation_status = dt::IsolationLevel::Valid;
    res.dc_evse_status.status_code =
        charger_stop ? dt::DC_EVSEStatusCode::EVSE_Shutdown : dt::DC_EVSEStatusCode::EVSE_Ready;

    res.evse_present_voltage = to_physical_value(present_voltage, Unit::V);
    res.evse_present_current = to_physical_value(present_current, Unit::A);

    res.evse_maximum_voltage_limit = to_physical_value(config.dc_max_voltage, Unit::V);
    res.evse_maximum_current_limit = to_physical_value(config.dc_max_current, Unit::A);
    res.evse_maximum_power_limit = to_physical_value(config.dc_max_power, Unit::W);

    // [Table 71]: the three *LimitAchieved flags describe the EVSE's own output, not the EV's requested
    // target. (EvseV2G compares the EV targets against the maxima instead, which is a different claim.)
    res.evse_current_limit_achieved = present_current >= config.dc_max_current;
    res.evse_voltage_limit_achieved = present_voltage >= config.dc_max_voltage;
    res.evse_power_limit_achieved = (present_voltage * present_current) >= config.dc_max_power;

    res.evse_id = config.evse_id;
    res.sa_schedule_tuple_id = sa_schedule_tuple_id;
    // PnC only, when configured; EIM never sets it ([V2G2-691]).
    res.receipt_required = request_receipt ? std::optional<bool>{true} : std::nullopt;

    // MeterInfo is a Plug-and-Charge-only element of CurrentDemandRes: [Table 104] marks it "-" for both
    // peers in "DC Charging EIM", and [V2G2-666] forbids supporting a parameter marked so. An EIM
    // session therefore gets no reading at all -- a deliberate deviation from EvseV2G, which sends it
    // whenever the module supplied one. The AC counterpart differs: ChargingStatusRes may carry it in EIM.
    if (pnc_selected) {
        res.meter_info = meter_info;
    }

    return res;
}

void DcChargeLoop::enter() {
    logf_debug("Enter state: CurrentDemand");
}

Result DcChargeLoop::on_event(Event ev) {
    if (ev == Event::CONTROL_MESSAGE) {
        if (const auto* control = m_ctx.get_control_event<d20::PresentVoltageCurrent>()) {
            m_ctx.set_present_values(control->voltage, control->current);
        }
        return {};
    }

    return {};
}

Result DcChargeLoop::on_request(const message_2::Variant& received) {
    // SessionStopReq is handled by StateBase::feed(). The receipt is not accepted here: setting
    // ReceiptRequired hands over to MeteringReceipt, whose node accepts it [V2G2-795].
    const auto type = received.get_type();
    if (type == message_2::Type::CurrentDemandReq) {
        return process_request(received.get<message_2::CurrentDemandRequest>());
    } else if (type == message_2::Type::PowerDeliveryReq) {
        return process_dc_power_delivery(m_ctx, received.get<message_2::PowerDeliveryRequest>());
    } else {
        logf_warning("Expected CurrentDemandReq or PowerDeliveryReq! But got type id: %d", received.get_type());
        respond_sequence_error(m_ctx, received.get_type());
        return {};
    }
}

Result DcChargeLoop::process_request(const message_2::CurrentDemandRequest& req) {
    m_ctx.report_ev_status(req.dc_ev_status);

    session::feedback::DcEvChargeProgress progress{};
    if (req.remaining_time_to_full_soc.has_value()) {
        progress.remaining_time_to_full_soc = static_cast<float>(from_physical_value(*req.remaining_time_to_full_soc));
    }
    if (req.remaining_time_to_bulk_soc.has_value()) {
        progress.remaining_time_to_bulk_soc = static_cast<float>(from_physical_value(*req.remaining_time_to_bulk_soc));
    }
    progress.charging_complete = req.charging_complete;
    progress.bulk_charging_complete = req.bulk_charging_complete;
    m_ctx.report_charge_progress(progress);

    if (not m_ctx.session().charge_loop_started) {
        m_ctx.feedback.signal(session::feedback::Signal::CHARGE_LOOP_STARTED);
        m_ctx.set_charge_loop_started();
    }

    // On change only: the EV repeats it in every request, and each forward makes the module republish.
    const auto optional_value = [](const std::optional<dt::PhysicalValue>& v) -> std::optional<double> {
        return v.has_value() ? std::optional<double>{from_physical_value(*v)} : std::nullopt;
    };
    const Context::DcSetpoint setpoint{
        from_physical_value(req.ev_target_voltage), from_physical_value(req.ev_target_current),
        optional_value(req.ev_maximum_voltage_limit), optional_value(req.ev_maximum_current_limit),
        optional_value(req.ev_maximum_power_limit)};
    m_ctx.report_dc_setpoint(setpoint, session::feedback::DcReqControlMode{build_ev_setpoint(req)});

    // Withheld until a meter reading exists: [V2G2-902] has the EV echo and sign the MeterInfo of this
    // very response, so a ReceiptRequired without one leaves it nothing to sign.
    const bool request_receipt = m_ctx.session_config.receipt_required and m_ctx.session().contract_selected and
                                 not m_ctx.session().receipt_received and m_ctx.evse().latest_meter_info.has_value();
    auto res = handle_request(m_ctx.get_session_id(), m_ctx.session_config, m_ctx.evse().present_voltage,
                              m_ctx.evse().present_current, m_ctx.session().sa_schedule_tuple_id,
                              m_ctx.evse().charger_stop_requested, request_receipt, m_ctx.evse().latest_meter_info,
                              m_ctx.session().contract_selected);
    // EmergencyShutdown additionally aborts, handled in the engine.
    apply_evse_error(m_ctx, res.dc_evse_status);
    apply_isolation_status(m_ctx, res.dc_evse_status);
    m_ctx.respond(res);

    if (request_receipt) {
        // [V2G2-795]: the receipt is now the only request in sequence, so hand over; it returns here [V2G2-593].
        return m_ctx.create_state<MeteringReceipt>(true);
    }

    return {};
}

} // namespace iso15118::d2::state
