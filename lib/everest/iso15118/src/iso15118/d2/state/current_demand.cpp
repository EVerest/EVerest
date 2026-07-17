// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/state/current_demand.hpp>

#include <iso15118/d2/state/metering_receipt.hpp>
#include <iso15118/d2/state/power_delivery.hpp>

#include <iso15118/message/common_types.hpp>
#include <iso15118/message/dc_charge_loop.hpp>

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

// Forward the EV setpoint (target V/I) and any per-loop maxima so the DC power supply follows the EV.
// Reuses the d20 dc_charge_loop_req path (Scheduled_DC_CLReqControlMode -> publish_dc_ev_target_voltage_current
// / publish_dc_ev_maximum_limits), mirroring EvseV2G iso_server.cpp:467-471.
void forward_ev_setpoint(const message_2::CurrentDemandRequest& req, const session::Feedback& feedback) {
    m20dt::Scheduled_DC_CLReqControlMode mode{};
    mode.target_voltage = m20dt::from_float(static_cast<float>(from_physical_value(req.ev_target_voltage)));
    mode.target_current = m20dt::from_float(static_cast<float>(from_physical_value(req.ev_target_current)));

    // The module publishes the maxima only when all three are present, so set them as a unit.
    if (req.ev_maximum_voltage_limit.has_value() and req.ev_maximum_current_limit.has_value() and
        req.ev_maximum_power_limit.has_value()) {
        mode.max_voltage = m20dt::from_float(static_cast<float>(from_physical_value(*req.ev_maximum_voltage_limit)));
        mode.max_charge_current =
            m20dt::from_float(static_cast<float>(from_physical_value(*req.ev_maximum_current_limit)));
        mode.max_charge_power = m20dt::from_float(static_cast<float>(from_physical_value(*req.ev_maximum_power_limit)));
    }

    feedback.dc_charge_loop_req(session::feedback::DcReqControlMode{mode});
}

} // namespace

message_2::CurrentDemandResponse handle_request(const message_2::CurrentDemandRequest& req,
                                                const dt::SessionId& session_id, const d2::SessionConfig& config,
                                                float present_voltage, float present_current,
                                                uint8_t sa_schedule_tuple_id, bool charger_stop, bool request_receipt,
                                                const std::optional<dt::MeterInfo>& meter_info) {
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

    const auto target_current = from_physical_value(req.ev_target_current);
    const auto target_voltage = from_physical_value(req.ev_target_voltage);
    res.evse_current_limit_achieved = target_current >= config.dc_max_current;
    res.evse_voltage_limit_achieved = target_voltage >= config.dc_max_voltage;
    res.evse_power_limit_achieved = (target_current * target_voltage) >= config.dc_max_power;

    res.evse_id = config.evse_id;
    res.sa_schedule_tuple_id = sa_schedule_tuple_id;
    // PnC: request a signed MeteringReceipt from the EV when configured (ev_receipt_required); EIM never
    // sets it ([V2G2-691]).
    res.receipt_required = request_receipt ? std::optional<bool>{true} : std::nullopt;

    // [V2G2-902]: when a MeteringReceipt is requested the EV signs the MeterInfo the SECC sent, so it must
    // be present. Include the latest meter reading forwarded by the module.
    if (request_receipt and meter_info.has_value()) {
        res.meter_info = meter_info;
    }

    return res;
}

void CurrentDemand::enter() {
    m_ctx.log.enter_state("CurrentDemand");
}

Result CurrentDemand::feed(Event ev) {
    if (ev == Event::CONTROL_MESSAGE) {
        if (const auto* control = m_ctx.get_control_event<d20::PresentVoltageCurrent>()) {
            m_ctx.present_voltage = control->voltage;
            m_ctx.present_current = control->current;
        } else if (const auto* meter = m_ctx.get_control_event<d20::MeterInfo>()) {
            dt::MeterInfo info{};
            info.meter_id = meter->meter_id;
            info.meter_reading = meter->meter_reading_wh;
            m_ctx.latest_meter_info = info;
        } else if (const auto* stop = m_ctx.get_control_event<d20::StopCharging>(); stop and static_cast<bool>(*stop)) {
            m_ctx.charger_stop_requested = true;
        }
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    // A MeteringReceiptReq (PnC, after ReceiptRequired) is handled by the MeteringReceipt state, which
    // returns to this loop afterwards.
    if (m_ctx.peek_request_type() == message_2::Type::MeteringReceiptReq) {
        return m_ctx.create_state<MeteringReceipt>();
    }
    // The charge loop ends when the EV sends PowerDeliveryReq(Stop); hand it to PowerDelivery.
    if (m_ctx.peek_request_type() != message_2::Type::CurrentDemandReq) {
        return m_ctx.create_state<PowerDelivery>();
    }

    const auto variant = m_ctx.pull_request();
    const auto req = variant->get<message_2::CurrentDemandRequest>();

    // The request must echo the assigned SessionID [V2G2-388]; a mismatch is answered with
    // CurrentDemandRes/FAILED_UnknownSession and terminates the session.
    if (reject_unknown_session(m_ctx, *variant)) {
        return {};
    }

    if (first_response) {
        m_ctx.feedback.signal(session::feedback::Signal::CHARGE_LOOP_STARTED);
        first_response = false;
    }

    forward_ev_setpoint(req, m_ctx.feedback);

    // Request a signed MeteringReceipt (PnC only, when configured and not yet received).
    const bool request_receipt =
        m_ctx.session_config.receipt_required and m_ctx.contract_selected and not m_ctx.receipt_received;
    auto res = handle_request(req, m_ctx.get_session_id(), m_ctx.session_config, m_ctx.present_voltage,
                              m_ctx.present_current, m_ctx.sa_schedule_tuple_id, m_ctx.charger_stop_requested,
                              request_receipt, m_ctx.latest_meter_info);
    // Stamp a module-reported EVSE error (Malfunction / UtilityInterruptEvent) into the status code so the
    // EV sees the fault mid-charge-loop (EmergencyShutdown additionally aborts, handled in the engine).
    apply_evse_error(m_ctx, res.dc_evse_status);
    m_ctx.respond(res);

    return {};
}

} // namespace iso15118::d2::state
