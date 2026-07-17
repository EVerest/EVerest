// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include "../states.hpp"

namespace iso15118::d2::ev::state {

// EIM authorization. Sends an empty AuthorizationReq and polls until EVSEProcessing=Finished, guarded
// by the ongoing timeout.
struct Authorization : public StateBase {
    Authorization(Context& ctx) : StateBase(ctx, StateID::Authorization) {
    }

    void enter() final;
    Result feed(Event) final;

private:
    bool first_request{true};
    bool ongoing_timeout_reached{false};

    void send(Event ev);
};

} // namespace iso15118::d2::ev::state
