// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
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
};

// Sends the AuthorizationReq, signed with the contract key on a Contract session, and creates the
// state awaiting it. A request that cannot be signed stops the session with nothing sent.
Result to_authorization(Context& ctx);

} // namespace iso15118::ev::d2::state
