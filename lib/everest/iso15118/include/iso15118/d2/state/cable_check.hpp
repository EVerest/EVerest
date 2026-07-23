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
    Result feed(Event) final;

private:
    // Drive the cable check for a request that passed the CP State C/D gate: trigger the isolation
    // test (initial check only) and stage the response.
    Result process_request(const message_2::CableCheckRequest& req);

    bool cable_check_initiated{false};
    // Initial CableCheckReq parked while waiting for CP State C/D ([V2G2-916]..[V2G2-918]): answered
    // when C/D arrives, or FAILED when V2G_SECC_CPState_Detection_Timeout expires.
    std::optional<message_2::CableCheckRequest> pending_req{};
};

} // namespace iso15118::d2::state
