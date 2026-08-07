// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>
#include <utility>

#include "../states.hpp"

namespace iso15118::din::state {

struct PreCharge : public StateBase {
    PreCharge(Context& ctx) : StateBase(ctx, StateID::PreCharge) {
    }

    void enter() final;
    Result feed(Event) final;

private:
    bool pre_charge_initiated{false};
    // Last EV target voltage/current forwarded via dc_charge_loop_req; PreChargeReq arrives every
    // 25-250 ms, so forward only on change (EvseV2G publish_dc_ev_target_voltage_current parity).
    std::optional<std::pair<double, double>> last_forwarded_target;
};

} // namespace iso15118::din::state
