// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include "../states.hpp"

namespace iso15118::d2::state {

struct PowerDelivery : public StateBase {
    PowerDelivery(Context& ctx) : StateBase(ctx, StateID::PowerDelivery) {
    }
    // Called from ChargingStatus when EV sends PowerDeliveryReq to stop
    PowerDelivery(Context& ctx, int16_t sa_schedule_tuple_id) :
        StateBase(ctx, StateID::PowerDelivery), selected_sa_id(sa_schedule_tuple_id) {
    }
    void enter() final;
    Result feed(Event) final;

private:
    int16_t selected_sa_id{1};
};

} // namespace iso15118::d2::state
