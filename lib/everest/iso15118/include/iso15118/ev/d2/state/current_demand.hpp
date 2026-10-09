// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/ev/d2/context.hpp>
#include <iso15118/ev/d2/states.hpp>

namespace iso15118::ev::d2::state {

// DC CurrentDemand loop. Diverts to PowerDelivery(Stop) on an EV stop/pause or an EVSENotification
// StopCharging; a ReceiptRequired on a Contract session detours through MeteringReceipt.
struct CurrentDemand : public StateBase {
    CurrentDemand(Context& ctx) : StateBase(ctx, StateID::CurrentDemand) {
    }

    void enter() final;
    Result feed(Event) final;

private:
    // The SECC may set ReceiptRequired on every response; warn about an unusable one only once.
    bool receipt_warned{false};
};

} // namespace iso15118::ev::d2::state
