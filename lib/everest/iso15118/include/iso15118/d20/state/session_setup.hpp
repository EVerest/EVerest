// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#pragma once
#include <string>

#include "../states.hpp"

namespace iso15118::d20::state {

struct SessionSetup : public StateBase {
    SessionSetup(Context& ctx, bool skip_app_protocol_negotiation_) :
        StateBase(ctx, StateID::SessionSetup), skip_app_protocol_negotiation(skip_app_protocol_negotiation_) {
    }

    void enter() final;

    Result feed(Event) final;

private:
    std::string evse_id;
    bool skip_app_protocol_negotiation;
};

} // namespace iso15118::d20::state
