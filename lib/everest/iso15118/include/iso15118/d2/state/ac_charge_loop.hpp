// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include "../states.hpp"

namespace iso15118::d2::state {

// [V2G2-576]: a PowerDeliveryReq is not in sequence until the EV has reported status once, which is
// the whole difference from AcChargeLoop below.
struct AcChargeLoopStart : public StateBase {
    AcChargeLoopStart(Context& ctx) : StateBase(ctx, StateID::AcChargeLoopStart) {
    }

    void enter() final;
    Result on_event(Event) final;
    Result on_request(const message_2::Variant& received) final;
};

// [V2G2-575]/[V2G2-580] widen the node to a PowerDeliveryReq. A MeteringReceiptReq is not accepted:
// setting ReceiptRequired hands over to MeteringReceipt [V2G2-577].
struct AcChargeLoop : public StateBase {
    AcChargeLoop(Context& ctx) : StateBase(ctx, StateID::AcChargeLoop) {
    }

    void enter() final;
    Result on_event(Event) final;
    Result on_request(const message_2::Variant& received) final;
};

} // namespace iso15118::d2::state
