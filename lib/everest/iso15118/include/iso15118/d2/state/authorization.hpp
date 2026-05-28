// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once
#include <string>

#include "../states.hpp"

namespace iso15118::d2::state {

struct Authorization : public StateBase {
    Authorization(Context& ctx) : StateBase(ctx, StateID::Authorization) {
    }

    void enter() final;

    Result feed(Event) final;

private:
    bool authorization_pending{true};
    bool authorized{false};
    bool first_req_msg{true};
    bool timeout_ongoing_reached{false};
};

} // namespace iso15118::d2::state
