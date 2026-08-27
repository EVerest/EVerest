// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include "../states.hpp"

namespace iso15118::d20::ev::state {

// AC (and AC_BPT) charge parameter discovery. Sends the transfer-mode request matching the selected
// energy service, stores the SECC-advertised AC limits and advances to ScheduleExchange.
struct AC_ChargeParameterDiscovery : public StateBase {
    AC_ChargeParameterDiscovery(Context& ctx) : StateBase(ctx, StateID::AC_ChargeParameterDiscovery) {
    }

    void enter() final;
    Result feed(Event) final;
};

} // namespace iso15118::d20::ev::state
