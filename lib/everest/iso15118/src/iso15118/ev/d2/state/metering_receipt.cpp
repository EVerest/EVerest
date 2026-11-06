// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/d2/state/metering_receipt.hpp>

#include <iso15118/detail/d2/crypto.hpp>
#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d2/state/power_delivery.hpp>
#include <iso15118/ev/detail/d2/context_helper.hpp>
#include <iso15118/ev/detail/d2/state/metering_receipt.hpp>

namespace iso15118::ev::d2::state {

namespace metering_receipt {

message_2::MeteringReceiptRequest create_request(const dt::SessionId& session_id, const dt::MeterInfo& meter_info,
                                                 uint8_t sa_schedule_tuple_id) {
    message_2::MeteringReceiptRequest req;
    req.header.session_id = session_id;
    req.session_id = session_id;
    req.meter_info = meter_info;
    req.sa_schedule_tuple_id = sa_schedule_tuple_id;
    return req;
}

} // namespace metering_receipt

Result to_metering_receipt(Context& ctx, const dt::MeterInfo& meter_info,
                           std::optional<session::StopReason> pending_stop) {
    const auto req = metering_receipt::create_request(ctx.get_session_id(), meter_info, ctx.sa_schedule_tuple_id());

    const auto& contract = ctx.pnc.contract;
    const auto key = contract ? iso15118::d2::crypto::PrivateKey{contract->key_pem, contract->key_password}
                              : iso15118::d2::crypto::PrivateKey{};
    auto signed_exi = iso15118::d2::crypto::serialize_signed(req, key);
    if (signed_exi.empty()) {
        // A stop pending at the loop response still goes out cleanly, without the receipt.
        if (pending_stop.has_value()) {
            logf_error("MeteringReceipt: failed to sign the MeteringReceiptReq; sending the pending stop without it");
            return Result{to_power_delivery(ctx, PowerDelivery::Stop{*pending_stop})};
        }
        logf_error("MeteringReceipt: failed to sign the MeteringReceiptReq; stopping the session");
        ctx.stop_session();
        return Result::stopping();
    }
    ctx.send_raw(std::move(signed_exi), message_2::Type::MeteringReceiptReq);
    return ctx.create_state<MeteringReceipt>(meter_info, pending_stop);
}

std::optional<Result> leave_charge_loop(Context& ctx, std::optional<session::StopReason> charger_stop,
                                        bool receipt_required, const std::optional<dt::MeterInfo>& meter_info) {
    const auto stop = charger_stop.has_value() ? charger_stop : session::requested_by_ev(ctx);
    if (receipt_required and ctx.pnc.contract_selected and meter_info.has_value()) {
        return to_metering_receipt(ctx, meter_info.value(), stop);
    }
    if (stop.has_value()) {
        return Result{to_power_delivery(ctx, PowerDelivery::Stop{*stop})};
    }
    return std::nullopt;
}

void MeteringReceipt::enter() {
    logf_debug("Enter state: MeteringReceipt (ISO 15118-2)");
}

Result MeteringReceipt::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return Result::ignored();
    }

    const auto variant = m_ctx.pull_response();
    if (expect_response<message_2::MeteringReceiptResponse>(m_ctx, *variant) == nullptr) {
        return Result::stopping();
    }

    // After the receipt, a stop pending from the loop or latched since goes to PowerDelivery(Stop)
    // [V2G2-793] [V2G2-519].
    if (const auto stop = m_pending_stop.has_value() ? m_pending_stop : session::requested_by_ev(m_ctx)) {
        return to_power_delivery(m_ctx, PowerDelivery::Stop{*stop});
    }
    return to_charge_loop(m_ctx);
}

} // namespace iso15118::ev::d2::state
