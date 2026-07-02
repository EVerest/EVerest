// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include "../states.hpp"

namespace iso15118::ev::d20::state {

struct ScheduleExchange : public StateBase {
public:
    ScheduleExchange(Context& ctx) : StateBase(ctx, StateID::ScheduleExchange) {
    }

    void enter() final;

    Result feed(Event) final;
};

} // namespace iso15118::ev::d20::state
