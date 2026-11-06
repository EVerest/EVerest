// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>

#include <iso15118/ev/d2/context.hpp>
#include <iso15118/ev/d2/states.hpp>
#include <iso15118/ev/session/stop_reason.hpp>
#include <iso15118/message_2/common_types.hpp>

namespace iso15118::ev::d2::state {

// Plug & Charge: signs the MeterInfo of the charge-loop response that set ReceiptRequired, then goes to
// PowerDelivery(Stop) with `pending_stop` or an EV stop latched since, else returns to the loop it came
// from.
struct MeteringReceipt : public StateBase {
    MeteringReceipt(Context& ctx, message_2::datatypes::MeterInfo meter_info,
                    std::optional<session::StopReason> pending_stop) :
        StateBase(ctx, StateID::MeteringReceipt), m_meter_info(std::move(meter_info)), m_pending_stop(pending_stop) {
    }

    void enter() final;
    Result feed(Event) final;

private:
    message_2::datatypes::MeterInfo m_meter_info;
    std::optional<session::StopReason> m_pending_stop;
};

// Signs the MeteringReceiptReq over `meter_info` with the contract key, sends it and creates the state
// awaiting it. A request that cannot be signed goes to PowerDelivery(Stop) with `pending_stop`, or without
// one stops the session with nothing sent.
Result to_metering_receipt(Context& ctx, const message_2::datatypes::MeterInfo& meter_info,
                           std::optional<session::StopReason> pending_stop);

// At a charge-loop response, with `charger_stop` the stop the response asked for: a due receipt is signed
// before any stop, which follows it [V2G2-790] [V2G2-512]; without one a charger or EV stop goes to
// PowerDelivery(Stop) [V2G2-513] [V2G2-527]. Empty when the loop continues.
std::optional<Result> leave_charge_loop(Context& ctx, std::optional<session::StopReason> charger_stop,
                                        bool receipt_required,
                                        const std::optional<message_2::datatypes::MeterInfo>& meter_info);

} // namespace iso15118::ev::d2::state
