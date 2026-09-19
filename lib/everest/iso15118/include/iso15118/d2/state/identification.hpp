// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message_2/payment_details.hpp>
#include <iso15118/message_2/variant.hpp>

#include "../states.hpp"

namespace iso15118::d2::state {

// [V2G2-551], the Plug-and-Charge branch. AuthorizationReq is not accepted: an EIM session goes from
// ServiceSelection straight to Authorization and never arrives here.
// The optional certificate exchange relays the raw request EXI to the module and waits. That wait is
// internal: no V2G request is in sequence until the backend replies, so any that arrives is an error.
struct Identification : public StateBase {
    Identification(Context& ctx) : StateBase(ctx, StateID::Identification) {
    }

    void enter() final;
    Result on_event(Event) final;
    Result on_request(const message_2::Variant& received) final;

private:
    Result forward_to_backend(const message_2::Variant& received);

    // Set once the request is with the backend, so it is never forwarded twice.
    bool request_forwarded{false};
};

} // namespace iso15118::d2::state
