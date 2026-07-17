// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/state/metering_receipt.hpp>

#include <iso15118/d2/state/charging_status.hpp>
#include <iso15118/d2/state/current_demand.hpp>

#include <iso15118/detail/d2/crypto.hpp>
#include <iso15118/detail/d2/state/sequence_error.hpp>
#include <iso15118/detail/d2/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

#include <iso15118/message_2/metering_receipt.hpp>

namespace iso15118::d2::state {

namespace dt = message_2::datatypes;

namespace {
void set_evse_status(const Context& ctx, message_2::MeteringReceiptResponse& res) {
    if (ctx.dc_charging) {
        res.dc_evse_status = make_dc_evse_status(ctx, dt::DC_EVSEStatusCode::EVSE_Ready);
    } else {
        res.ac_evse_status = make_ac_evse_status();
    }
}
} // namespace

void MeteringReceipt::enter() {
    m_ctx.log.enter_state("MeteringReceipt");
}

Result MeteringReceipt::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    const auto variant = m_ctx.pull_request();

    const auto req = variant->get_if<message_2::MeteringReceiptRequest>();
    if (req == nullptr) {
        m_ctx.log("expected MeteringReceiptReq! But code type id: %d", variant->get_type());
        // [V2G2-539]: answer with the received-type response carrying FAILED_SequenceError, then close.
        respond_sequence_error(m_ctx, *variant);
        m_ctx.session_stopped = true;
        return {};
    }

    // The request must echo the assigned SessionID; a mismatch is answered with FAILED_UnknownSession.
    if (reject_unknown_session(m_ctx, *variant)) {
        return {};
    }

    message_2::MeteringReceiptResponse res;
    res.header.session_id = m_ctx.get_session_id();
    set_evse_status(m_ctx, res);

    // The signed body SessionID must equal the assigned session id [V2G2-909]; a mismatch means the
    // receipt is not bound to this session -> FAILED_UnknownSession + close.
    if (req->session_id != m_ctx.get_session_id()) {
        m_ctx.log("MeteringReceipt: body SessionID does not match the assigned session id");
        res.response_code = dt::ResponseCode::FAILED_UnknownSession;
        m_ctx.respond(res);
        m_ctx.session_stopped = true;
        return {};
    }

    // A MeteringReceiptReq is only in sequence after the SUT set ReceiptRequired=true (PnC, configured
    // via ev_receipt_required). Otherwise it is unexpected -> FAILED_SequenceError + close.
    const bool was_requested =
        m_ctx.session_config.receipt_required and m_ctx.contract_selected and not m_ctx.receipt_received;
    if (not was_requested) {
        m_ctx.log("MeteringReceipt: unexpected MeteringReceiptReq (ReceiptRequired was not set)");
        res.response_code = dt::ResponseCode::FAILED_SequenceError;
        m_ctx.respond(res);
        m_ctx.session_stopped = true;
        return {};
    }

    // Plug-and-Charge: the MeteringReceiptReq is signed with the contract certificate (the same leaf
    // captured in PaymentDetails). Verify it; a bad signature -> FAILED_MeteringSignatureNotValid + close.
    if (not crypto::verify_metering_receipt_signature(variant->get_exi_payload(), m_ctx.contract_leaf_der)) {
        m_ctx.log("PnC MeteringReceipt: signature verification failed");
        res.response_code = dt::ResponseCode::FAILED_MeteringSignatureNotValid;
        m_ctx.respond(res);
        m_ctx.session_stopped = true;
        return {};
    }

    m_ctx.receipt_received = true;
    res.response_code = dt::ResponseCode::OK;
    m_ctx.respond(res);

    // Resume the charge loop the MeteringReceipt was requested from.
    if (m_ctx.dc_charging) {
        return m_ctx.create_state<CurrentDemand>();
    }
    return m_ctx.create_state<ChargingStatus>();
}

} // namespace iso15118::d2::state
