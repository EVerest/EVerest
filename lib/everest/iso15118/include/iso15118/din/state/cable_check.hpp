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
    Result feed(Event) final;

private:
    // Handle a CableCheckReq that passed the CP State C/D gate and stage the response.
    Result process_request(const message_din::CableCheckRequest& req);

    bool cable_check_initiated{false};
    // Request parked while waiting for CP State C/D ([V2G-DC-967]): answered when C/D arrives, or with
    // FAILED when V2G_SECC_CPState_Detection_Timeout expires.
    std::optional<message_din::CableCheckRequest> pending_req{};
};

} // namespace iso15118::din::state
