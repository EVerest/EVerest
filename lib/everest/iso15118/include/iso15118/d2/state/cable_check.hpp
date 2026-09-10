// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include <iso15118/message_2/cable_check.hpp>

#include "../states.hpp"

namespace iso15118::d2::state {

struct CableCheck : public StateBase {
    CableCheck(Context& ctx) : StateBase(ctx, StateID::CableCheck) {
    }

    void enter() final;
    Result on_event(Event) final;
    Result on_request(const message_2::Variant& received) final;
    void leave() final;

private:
    // Triggers the isolation test (initial check only) and stages the response.
    Result process_request(const message_2::CableCheckRequest& req);

    bool cable_check_initiated{false};
    // Answered when C/D arrives, or FAILED when V2G_SECC_Msg_Performance_Time expires.
    std::optional<message_2::CableCheckRequest> pending_req{};
};

} // namespace iso15118::d2::state
