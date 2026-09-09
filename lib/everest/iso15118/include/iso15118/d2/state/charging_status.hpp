// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include "../states.hpp"

namespace iso15118::d2::state {

// AC charging loop — handles ChargingStatusReq and PowerDeliveryReq{Stop}
struct ChargingStatus : public StateBase {
    ChargingStatus(Context& ctx, int16_t sa_schedule_tuple_id)
        : StateBase(ctx, StateID::ChargingStatus), selected_sa_id(sa_schedule_tuple_id) {}
    void enter() final;
    Result feed(Event) final;
private:
    int16_t selected_sa_id;
};

} // namespace iso15118::d2::state
