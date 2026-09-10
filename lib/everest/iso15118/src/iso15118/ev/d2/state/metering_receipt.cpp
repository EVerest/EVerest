// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/d2/state/metering_receipt.hpp>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d2/state/charging_status.hpp>
#include <iso15118/ev/d2/state/current_demand.hpp>
#include <iso15118/ev/detail/d2/context_helper.hpp>
#include <iso15118/ev/detail/d2/crypto.hpp>
#include <iso15118/ev/detail/d2/state/metering_receipt.hpp>

namespace iso15118::ev::d2::state {

namespace metering_receipt {

message_2::MeteringReceiptRequest create_request(const dt::SessionId& session_id, const dt::MeterInfo& meter_info,
                                                 std::optional<uint8_t> sa_schedule_tuple_id) {
    message_2::MeteringReceiptRequest req;
    req.header.session_id = session_id;
    req.session_id = session_id;
    req.meter_info = meter_info;
    req.sa_schedule_tuple_id = sa_schedule_tuple_id;
    return req;
}

} // namespace metering_receipt

void MeteringReceipt::enter() {
    logf_debug("Enter state: MeteringReceipt (ISO 15118-2)");
    const auto req = metering_receipt::create_request(m_ctx.get_session_id(), m_meter_info, m_sa_schedule_tuple_id);

    const crypto::PrivateKey key{m_ctx.pnc.contract_key_pem, m_ctx.pnc.contract_key_password};
    auto signed_exi = crypto::serialize_signed(req, key);
    if (signed_exi.empty()) {
        logf_error("MeteringReceipt: failed to sign the MeteringReceiptReq; stopping the session");
        m_ctx.stop_session();
        return;
    }
    m_ctx.send_raw(std::move(signed_exi), message_2::Type::MeteringReceiptReq);
}

Result MeteringReceipt::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return Result::ignored();
    }

    const auto variant = m_ctx.pull_response();
    if (expect_response<message_2::MeteringReceiptResponse>(m_ctx, *variant) == nullptr) {
        return Result::stopping();
    }

    if (is_dc_mode(m_ctx.params().energy_transfer_mode)) {
        return m_ctx.create_state<CurrentDemand>();
    }
    return m_ctx.create_state<ChargingStatus>();
}

} // namespace iso15118::ev::d2::state
