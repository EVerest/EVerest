// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/ev/d2/context.hpp>
#include <iso15118/ev/d2/states.hpp>

namespace iso15118::ev::d2::state {

// AC ChargingStatus loop. Publishes ac_target_power on every response; same stop/receipt handling as
// CurrentDemand.
struct ChargingStatus : public StateBase {
    ChargingStatus(Context& ctx) : StateBase(ctx, StateID::ChargingStatus) {
    }

    void enter() final;
    Result feed(Event) final;

private:
    // The SECC may set ReceiptRequired on every response; warn about an unusable one only once.
    bool receipt_warned{false};
};

} // namespace iso15118::ev::d2::state
