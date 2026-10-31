// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message/authorization.hpp>

#include "../states.hpp"

namespace iso15118::ev::d20::state {

struct Authorization : public StateBase {
public:
    explicit Authorization(Context& ctx) : StateBase(ctx, StateID::Authorization) {
    }

    static message_20::AuthorizationRequest make_request();

    void enter() final;

    Result feed(Event) final;
};

} // namespace iso15118::ev::d20::state