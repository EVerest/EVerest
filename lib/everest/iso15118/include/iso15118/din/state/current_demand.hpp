// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>
#include <tuple>

#include "../states.hpp"

namespace iso15118::din::state {

struct CurrentDemand : public StateBase {
    CurrentDemand(Context& ctx) : StateBase(ctx, StateID::CurrentDemand) {
    }

    void enter() final;
    Result feed(Event) final;

private:
    bool charge_loop_started{false};
    // Last EV setpoint (target voltage/current + the optional maximum V/I/P limits) forwarded via
    // dc_charge_loop_req; CurrentDemandReq arrives every 25-250 ms, so forward only on change
    // (EvseV2G publish_dc_ev_target_voltage_current parity).
    using EvSetpoint = std::tuple<double, double, std::optional<double>, std::optional<double>, std::optional<double>>;
    std::optional<EvSetpoint> last_forwarded_setpoint;
};

} // namespace iso15118::din::state
