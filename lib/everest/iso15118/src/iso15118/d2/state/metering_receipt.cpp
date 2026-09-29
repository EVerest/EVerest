// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/state/metering_receipt.hpp>

#include <iso15118/d2/state/ac_charge_loop.hpp>
#include <iso15118/d2/state/dc_charge_loop.hpp>
#include <iso15118/detail/d2/state/metering_receipt.hpp>
#include <iso15118/detail/d2/state/sequence_error.hpp>

#include <iso15118/detail/d2/crypto.hpp>
#include <iso15118/detail/d2/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

#include <iso15118/message_2/metering_receipt.hpp>

namespace iso15118::d2::state {

namespace dt = message_2::datatypes;

namespace {
// [Table 104] makes the RCD flag mandatory in every AC response, and the ChargingStatusRes on
// either side of the receipt carries it, so this must not be the one response that drops it.
message_2::MeteringReceiptResponse make_response(const Context& ctx, bool is_dc) {
    const bool charger_stop = ctx.evse().charger_stop_requested;

    message_2::MeteringReceiptResponse res;
    res.header.session_id = ctx.get_session_id();

    if (is_dc) {
        auto& status = res.dc_evse_status.emplace(make_dc_evse_status(
            ctx, charger_stop ? dt::DC_EVSEStatusCode::EVSE_Shutdown : dt::DC_EVSEStatusCode::EVSE_Ready));
        if (charger_stop) {
            status.notification = dt::EVSENotification::StopCharging;
        }
    } else {
        auto& status = res.ac_evse_status.emplace(make_ac_evse_status());
        status.rcd = ctx.rcd_error();
        if (charger_stop) {
            status.notification = dt::EVSENotification::StopCharging;
        }
    }

    return res;
}
} // namespace

void handle_metering_receipt(Context& m_ctx, const message_2::Variant& variant,
                             const message_2::MeteringReceiptRequest& request, bool is_dc) {
    auto res = make_response(m_ctx, is_dc);

    // [V2G2-909]: a mismatch means the receipt is not bound to this session.
    if (request.session_id != m_ctx.get_session_id()) {
        logf_warning("MeteringReceipt: body SessionID does not match the assigned session id");
        res.response_code = dt::ResponseCode::FAILED_UnknownSession;
        m_ctx.respond(res);
        m_ctx.session_stopped = true;
        return;
    }

    // "Was the receipt actually requested?" is not asked here: this runs only from the MeteringReceipt
    // state, which the loop enters exactly when it sent ReceiptRequired=TRUE. The old check re-derived
    // that from config and got it wrong, omitting the "a meter reading exists" term ([V2G2-902]).

    // PnC: signed with the contract certificate, the same leaf captured in PaymentDetails.
    if (not crypto::verify_metering_receipt_signature(variant.get_exi_payload(), m_ctx.session().contract_leaf_der)) {
        logf_warning("PnC MeteringReceipt: signature verification failed");
        res.response_code = dt::ResponseCode::FAILED_MeteringSignatureNotValid;
        m_ctx.respond(res);
        m_ctx.session_stopped = true;
        return;
    }

    m_ctx.set_receipt_received();
    res.response_code = dt::ResponseCode::OK;
    m_ctx.respond(res);
}

void MeteringReceipt::enter() {
    logf_debug("Enter state: MeteringReceipt");
}

Result MeteringReceipt::on_request(const message_2::Variant& received) {
    // The receipt is the only request in sequence here ([V2G2-577] / [V2G2-795]).
    const auto type = received.get_type();
    if (type == message_2::Type::MeteringReceiptReq) {
        handle_metering_receipt(m_ctx, received, received.get<message_2::MeteringReceiptRequest>(), dc);
        if (m_ctx.session_stopped) {
            return {};
        }
        // Back into the charge loop [V2G2-580] / [V2G2-797].
        if (dc) {
            return m_ctx.create_state<DcChargeLoop>();
        }
        return m_ctx.create_state<AcChargeLoop>();
    } else {
        // [V2G2-538]: the EV was told ReceiptRequired=TRUE, so continuing the loop instead of signing
        // is out of sequence.
        logf_warning("Expected MeteringReceiptReq! But got type id: %d", received.get_type());
        respond_sequence_error(m_ctx, received.get_type());
        return {};
    }
}

} // namespace iso15118::d2::state
