// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include "../states.hpp"

namespace iso15118::d2::state {

// ISO 15118-2 MeteringReceipt. Entered from the charge loop (CurrentDemand/ChargingStatus) when the EV
// sends a MeteringReceiptReq (typically after the SECC set ReceiptRequired). For Plug-and-Charge the
// request is signed with the contract certificate and the signature is verified here; the SECC answers
// MeteringReceiptRes and returns to the charge loop.
struct MeteringReceipt : public StateBase {
    MeteringReceipt(Context& ctx) : StateBase(ctx, StateID::MeteringReceipt) {
    }

    void enter() final;
    Result feed(Event) final;
};

} // namespace iso15118::d2::state
