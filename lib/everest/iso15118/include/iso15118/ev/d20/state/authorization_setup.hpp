// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message/authorization_setup.hpp>

#include "../states.hpp"

namespace iso15118::ev::d20::state {

struct AuthorizationSetup : public StateBase {
public:
    AuthorizationSetup(Context& ctx) : StateBase(ctx, StateID::AuthorizationSetup) {
    }

    static message_20::AuthorizationSetupRequest make_request();

    void enter() final;

    Result feed(Event) final;
};

} // namespace iso15118::ev::d20::state
