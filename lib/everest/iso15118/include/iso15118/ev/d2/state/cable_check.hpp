// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/ev/d2/context.hpp>
#include <iso15118/ev/d2/states.hpp>

namespace iso15118::ev::d2::state {

// DC CableCheck poll until EVSEProcessing=Finished, then PreCharge. The Session's Ongoing guard bounds
// both the poll and the CP-state wait.
struct CableCheck : public StateBase {
    CableCheck(Context& ctx) : StateBase(ctx, StateID::CableCheck) {
    }

    void enter() final;
    Result feed(Event) final;

private:
    // [V2G2-847]: with CP-state feedback the first request waits for state C or D.
    bool request_sent{false};
};

} // namespace iso15118::ev::d2::state
