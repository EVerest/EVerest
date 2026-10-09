// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/ev/d2/context.hpp>
#include <iso15118/ev/d2/states.hpp>

namespace iso15118::ev::d2::state {

// Polls AuthorizationReq until EVSEProcessing=Finished. The Session's Ongoing guard bounds the poll.
struct Authorization : public StateBase {
    Authorization(Context& ctx) : StateBase(ctx, StateID::Authorization) {
    }

    void enter() final;
    Result feed(Event) final;

private:
    // False when the PnC request could not be signed; the session stops instead of polling.
    bool send();
};

} // namespace iso15118::ev::d2::state
