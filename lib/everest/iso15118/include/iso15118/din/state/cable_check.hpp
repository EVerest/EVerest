// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include <iso15118/message_din/cable_check.hpp>

#include "../states.hpp"

namespace iso15118::din::state {

struct CableCheck : public StateBase {
    CableCheck(Context& ctx) : StateBase(ctx, StateID::CableCheck) {
    }

    void enter() final;
    Result on_event(Event) final;
    Result on_request(const message_din::Variant& received) final;

private:
    Result process_request(const message_din::CableCheckRequest& req);

    bool cable_check_initiated{false};
    // Answered when C/D arrives, or with FAILED when the detection timeout expires ([V2G-DC-967]).
    std::optional<message_din::CableCheckRequest> pending_req{};
};

} // namespace iso15118::din::state
