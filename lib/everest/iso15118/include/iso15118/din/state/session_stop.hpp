// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include <iso15118/message_din/session_stop.hpp>

#include "../states.hpp"

namespace iso15118::din::state {

struct SessionStop : public StateBase {
    SessionStop(Context& ctx) : StateBase(ctx, StateID::SessionStop) {
    }

    void enter() final;
    Result feed(Event) final;

private:
    // Handle a SessionStopReq that passed the CP State B gate and stage the response.
    void process_request(const message_din::SessionStopRequest& req);

    // Request parked while waiting for CP State B ([V2G-DC-988]): answered when B arrives, or with
    // FAILED when V2G_SECC_CPState_Detection_Timeout expires ([V2G-DC-556]).
    std::optional<message_din::SessionStopRequest> pending_req{};
};

} // namespace iso15118::din::state
