// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message/session_setup.hpp>

#include "../states.hpp"

namespace iso15118::ev::d20::state {

struct SessionSetup : public StateBase {
public:
    SessionSetup(Context& ctx) : StateBase(ctx, StateID::SessionSetup) {
    }

    static message_20::SessionSetupRequest make_request(const message_20::datatypes::Identifier& evcc_id);

    void enter() final;

    Result feed(Event) final;
};

} // namespace iso15118::ev::d20::state
