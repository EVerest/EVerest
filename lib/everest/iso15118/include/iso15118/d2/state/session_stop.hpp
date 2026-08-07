// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include <iso15118/message_2/session_stop.hpp>

#include "../states.hpp"

namespace iso15118::d2::state {

struct SessionStop : public StateBase {
    SessionStop(Context& ctx) : StateBase(ctx, StateID::SessionStop) {
    }

    void enter() final;
    Result feed(Event) final;

private:
    // Handle a SessionStopReq that passed the CP State B gate and stage the response.
    void process_request(const message_2::SessionStopRequest& req);

    // Request parked while waiting for CP State B ([V2G2-920]..[V2G2-922]): answered when B arrives,
    // or with FAILED when V2G_SECC_CPState_Detection_Timeout expires.
    std::optional<message_2::SessionStopRequest> pending_req{};
};

} // namespace iso15118::d2::state
