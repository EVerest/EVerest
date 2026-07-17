// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message/authorization.hpp>

#include "../states.hpp"

namespace iso15118::d20::ev::state {

struct Authorization : public StateBase {
    Authorization(Context& ctx) : StateBase(ctx, StateID::Authorization) {
    }

    void enter() final;
    Result feed(Event) final;

private:
    // The request is built once and resent unaltered (except header timestamp) while evse_processing == Ongoing.
    message_20::AuthorizationRequest cached_request;
    bool first_request{true};
    bool ongoing_timeout_reached{false};

    void send(Event ev);
};

} // namespace iso15118::d20::ev::state
