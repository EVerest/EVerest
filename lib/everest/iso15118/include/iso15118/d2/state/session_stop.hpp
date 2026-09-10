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
    Result on_event(Event) final;
    Result on_request(const message_2::Variant& received) final;

private:
    // Applies the [V2G2-920] CP State B gate and either parks the request or answers it.
    Result accept_request(const message_2::SessionStopRequest& req);

    // Answered when B arrives, or with FAILED when V2G_SECC_Msg_Performance_Time expires [V2G2-922].
    std::optional<message_2::SessionStopRequest> pending_req{};
};

} // namespace iso15118::d2::state
