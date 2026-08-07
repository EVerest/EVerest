// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/state/charging_status.hpp>

#include <iso15118/d2/state/metering_receipt.hpp>
#include <iso15118/d2/state/power_delivery.hpp>

#include <iso15118/detail/d2/state/charging_status.hpp>
#include <iso15118/detail/d2/state/sequence_error.hpp>
#include <iso15118/detail/d2/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d2::state {

message_2::ChargingStatusResponse handle_request([[maybe_unused]] const message_2::ChargingStatusRequest& req,
                                                 const dt::SessionId& session_id, const d2::SessionConfig& config,
                                                 uint8_t sa_schedule_tuple_id, bool charger_stop, bool request_receipt,
                                                 const std::optional<dt::MeterInfo>& meter_info) {
    message_2::ChargingStatusResponse res;
    res.header.session_id = session_id;
    res.response_code = dt::ResponseCode::OK;

    res.evse_id = config.evse_id;
    res.sa_schedule_tuple_id = sa_schedule_tuple_id;
    res.evse_max_current = dt::to_physical_value(config.ac_max_current, dt::Unit::A);

    res.ac_evse_status = make_ac_evse_status();
    res.ac_evse_status.notification = charger_stop ? dt::EVSENotification::StopCharging : dt::EVSENotification::None;

    // [V2G2-691] field always present. PnC: request a signed MeteringReceipt when configured; EIM false.
    res.receipt_required = request_receipt;

    // MeterInfo is an independent optional element, not a receipt payload: [Table 104] marks it "O" for
    // the SECC in the Message Sets "AC Charging EIM" *and* "AC Charging PnC", and [Table A.1] lists it for
    // the EIM use case elements (E1, E2, F0), so an EIM session is entitled to a meter reading too. The
    // only requirement on it, [V2G2-902], constrains its content ("exactly the same data which the meter
    // puts out"), not when it is sent; the dependency on ReceiptRequired runs the other way (the EV echoes
    // and signs the MeterInfo of the response that requested the receipt, hence the caller withholds the
    // request until a reading exists). So report every reading the module forwarded, EIM included
    // (EvseV2G parity, iso_server.cpp handle_iso_charging_status).
    res.meter_info = meter_info;

    return res;
}

void ChargingStatus::enter() {
    logf_debug("Enter state: ChargingStatus");
}

Result ChargingStatus::feed(Event ev) {
    if (ev == Event::CONTROL_MESSAGE) {
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
    if (m_ctx.peek_request_type() != message_2::Type::ChargingStatusReq) {
        return m_ctx.create_state<PowerDelivery>();
    }

    const auto variant = m_ctx.pull_request();
    const auto req = variant->get<message_2::ChargingStatusRequest>();

    // The request must echo the assigned SessionID [V2G2-388]; a mismatch is answered with
    // ChargingStatusRes/FAILED_UnknownSession and terminates the session.
    if (reject_unknown_session(m_ctx, *variant)) {
        return {};
    }

    // No CHARGE_LOOP_STARTED here: current_demand_started is a DC-only notification. EvseV2G raises it
    // from CurrentDemandReq alone (iso_server.cpp:2489) and never from ChargingStatusReq, and EvseManager
    // subscribes to it only in DC charge mode, where it switches the power supply into the charging phase
    // and arms the over-voltage monitor -- neither of which an AC session has.

    // Request a signed MeteringReceipt (PnC only, when configured and not yet received). Withheld until a
    // meter reading exists: [V2G2-902] has the EV echo and sign the MeterInfo of this very response, so a
    // ReceiptRequired without one leaves it nothing to sign.
    const bool request_receipt = m_ctx.session_config.receipt_required and m_ctx.contract_selected and
                                 not m_ctx.receipt_received and m_ctx.latest_meter_info.has_value();
    auto res = handle_request(req, m_ctx.get_session_id(), m_ctx.session_config, m_ctx.sa_schedule_tuple_id,
                              m_ctx.charger_stop_requested, request_receipt, m_ctx.latest_meter_info);
    // A module-reported RCD error is surfaced via the AC EVSE status RCD flag (mirrors EvseV2G send_error).
    res.ac_evse_status.rcd = (m_ctx.active_error == d20::EvseErrorCode::RCD);
    m_ctx.respond(res);

    return {};
}

} // namespace iso15118::d2::state
