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

// Builds the EV setpoint (target V/I) and any per-loop maxima for the module-facing dc_charge_loop_req
// feedback (Scheduled_DC_CLReqControlMode -> publish_dc_ev_target_voltage_current /
// publish_dc_ev_maximum_limits), mirroring EvseV2G iso_server.cpp:467-471.
m20dt::Scheduled_DC_CLReqControlMode build_ev_setpoint(const message_2::CurrentDemandRequest& req) {
    m20dt::Scheduled_DC_CLReqControlMode mode{};
    mode.target_voltage = m20dt::from_float(static_cast<float>(from_physical_value(req.ev_target_voltage)));
    mode.target_current = m20dt::from_float(static_cast<float>(from_physical_value(req.ev_target_current)));

    // Each EVMaximum*Limit is optional on its own, so forward whichever the EV sent instead of dropping
    // all three when one is missing (same rule as the DIN charge loop).
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

    // [Table 71] the three *LimitAchieved flags state that the EVSE "has reached" its current / voltage /
    // power limit: they describe the EVSE's own output, not the EV's requested target. (EvseV2G
    // iso_server.cpp compares EVTargetCurrent/EVTargetVoltage against the maxima instead, which reports
    // "the EV asked for at least my limit" -- not what the element means.)
    res.evse_current_limit_achieved = present_current >= config.dc_max_current;
    res.evse_voltage_limit_achieved = present_voltage >= config.dc_max_voltage;
    res.evse_power_limit_achieved = (present_voltage * present_current) >= config.dc_max_power;

    res.evse_id = config.evse_id;
    res.sa_schedule_tuple_id = sa_schedule_tuple_id;
    // PnC: request a signed MeteringReceipt from the EV when configured (ev_receipt_required); EIM never
    // sets it ([V2G2-691]).
    res.receipt_required = request_receipt ? std::optional<bool>{true} : std::nullopt;

    // MeterInfo is a Plug-and-Charge-only element of CurrentDemandRes, and not a receipt payload:
    // [Table 104] marks it "-" for BOTH peers in the Message Set "DC Charging EIM" and "O" for the SECC in
    // "DC Charging PnC" only, and [Table A.1] lists it under the DC PnC use case element (E4) alone. Per
    // [V2G2-666] an SECC "shall not support any parameter in a Message Set that is marked with an '-'", so
    // an EIM session gets no reading at all -- a deliberate deviation from EvseV2G, which sends MeterInfo
    // whenever the module supplied one (iso_server.cpp handle_iso_current_demand gates only
    // ReceiptRequired on the payment option). In PnC it is reported unconditionally: nothing ties it to
    // ReceiptRequired, the dependency runs the other way ([V2G2-902]: the EV echoes and signs the
    // MeterInfo of the response that requested the receipt, hence the caller withholds the request until a
    // reading exists). The AC counterpart differs -- ChargingStatusRes may carry MeterInfo in EIM too.
    if (pnc_selected) {
        res.meter_info = meter_info;
    }

    return res;
}

void CurrentDemand::enter() {
    logf_debug("Enter state: CurrentDemand");
}

Result CurrentDemand::feed(Event ev) {
    if (ev == Event::CONTROL_MESSAGE) {
        if (const auto* control = m_ctx.get_control_event<d20::PresentVoltageCurrent>()) {
            m_ctx.present_voltage = control->voltage;
            m_ctx.present_current = control->current;
        }
        // An EVSE-initiated stop (StopCharging) and the latest meter reading (MeterInfo) are latched on
        // the context by the engine, in any state.
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

    m_ctx.report_ev_status(req.dc_ev_status);

    // Remaining charging times and the completion flags the EV reports in every charge-loop request.
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

    if (first_response) {
        m_ctx.feedback.signal(session::feedback::Signal::CHARGE_LOOP_STARTED);
        first_response = false;
    }

    // Forward the EV setpoint on change only: the EV repeats it in every CurrentDemandReq, and each
    // forwarded setpoint makes the module republish (EvseV2G publish_dc_ev_target_voltage_current parity;
    // same rule as the DIN charge loop).
    const auto optional_value = [](const std::optional<dt::PhysicalValue>& v) -> std::optional<double> {
        return v.has_value() ? std::optional<double>{from_physical_value(*v)} : std::nullopt;
    };
    const EvSetpoint setpoint{from_physical_value(req.ev_target_voltage), from_physical_value(req.ev_target_current),
                              optional_value(req.ev_maximum_voltage_limit),
                              optional_value(req.ev_maximum_current_limit), optional_value(req.ev_maximum_power_limit)};
    if (last_forwarded_setpoint != setpoint) {
        m_ctx.feedback.dc_charge_loop_req(session::feedback::DcReqControlMode{build_ev_setpoint(req)});
        last_forwarded_setpoint = setpoint;
    }

    // Request a signed MeteringReceipt (PnC only, when configured and not yet received). Withheld until a
    // meter reading exists: [V2G2-902] has the EV echo and sign the MeterInfo of this very response, so a
    // ReceiptRequired without one leaves it nothing to sign.
    const bool request_receipt = m_ctx.session_config.receipt_required and m_ctx.contract_selected and
                                 not m_ctx.receipt_received and m_ctx.latest_meter_info.has_value();
    auto res = handle_request(m_ctx.get_session_id(), m_ctx.session_config, m_ctx.present_voltage,
                              m_ctx.present_current, m_ctx.sa_schedule_tuple_id, m_ctx.charger_stop_requested,
                              request_receipt, m_ctx.latest_meter_info, m_ctx.contract_selected);
    // Stamp a module-reported EVSE error (Malfunction / UtilityInterruptEvent) into the status code so the
    // EV sees the fault mid-charge-loop (EmergencyShutdown additionally aborts, handled in the engine).
    apply_evse_error(m_ctx, res.dc_evse_status);
    apply_isolation_status(m_ctx, res.dc_evse_status);
    m_ctx.respond(res);

    return {};
}

} // namespace iso15118::d2::state
