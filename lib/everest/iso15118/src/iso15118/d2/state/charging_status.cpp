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
                                                 uint8_t sa_schedule_tuple_id, bool charger_stop,
                                                 bool request_receipt) {
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

    return res;
}

void ChargingStatus::enter() {
    m_ctx.log.enter_state("ChargingStatus");
}

Result ChargingStatus::feed(Event ev) {
    if (ev == Event::CONTROL_MESSAGE) {
        if (const auto* stop = m_ctx.get_control_event<d20::StopCharging>(); stop and static_cast<bool>(*stop)) {
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

    if (first_response) {
        m_ctx.feedback.signal(session::feedback::Signal::CHARGE_LOOP_STARTED);
        first_response = false;
    }

    const bool request_receipt =
        m_ctx.session_config.receipt_required and m_ctx.contract_selected and not m_ctx.receipt_received;
    const auto res = handle_request(req, m_ctx.get_session_id(), m_ctx.session_config, m_ctx.sa_schedule_tuple_id,
                                    m_ctx.charger_stop_requested, request_receipt);
    m_ctx.respond(res);

    return {};
}

} // namespace iso15118::d2::state
