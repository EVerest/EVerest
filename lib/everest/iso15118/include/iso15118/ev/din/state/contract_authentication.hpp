// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/ev/din/context.hpp>
#include <iso15118/ev/din/states.hpp>

namespace iso15118::ev::din::state {

// EIM authorization poll: resend ContractAuthenticationReq until EVSEProcessing=Finished. The session
// owns the ongoing guard.
struct ContractAuthentication : public StateBase {
    ContractAuthentication(Context& ctx) : StateBase(ctx, StateID::ContractAuthentication) {
    }

    void enter() final;
    Result feed(Event) final;
};

} // namespace iso15118::ev::din::state
