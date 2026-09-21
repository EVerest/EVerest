// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message_2/metering_receipt.hpp>

#include "../states.hpp"

namespace iso15118::d2::state {

// Entered from the charge loop when the SECC set ReceiptRequired=TRUE, and the receipt is the only
// request in sequence from then on ([V2G2-577] AC / [V2G2-795] DC). Being in this state is therefore
// the proof that the receipt was requested, which the charge loop could not express on its own.
// \p is_dc selects the loop to return to and the EVSE status block ([V2G2-580] / [V2G2-797]).
struct MeteringReceipt : public StateBase {
    MeteringReceipt(Context& ctx, bool is_dc) : StateBase(ctx, StateID::MeteringReceipt), dc(is_dc) {
    }

    void enter() final;
    Result on_request(const message_2::Variant& received) final;

private:
    const bool dc;
};

} // namespace iso15118::d2::state
