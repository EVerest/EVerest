// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include <iso15118/message_din/welding_detection.hpp>

#include "../states.hpp"

namespace iso15118::din::state {

struct WeldingDetection : public StateBase {
    WeldingDetection(Context& ctx) : StateBase(ctx, StateID::WeldingDetection) {
    }

    void enter() final;
    Result feed(Event) final;

private:
    // Handle a WeldingDetectionReq that passed the CP State B gate and stage the response.
    void process_request(const message_din::WeldingDetectionRequest& req);

    // Arm the V2G_SECC_WeldingDetection supervision timer once, on the first WeldingDetectionReq.
    bool welding_started{false};
    // Request parked while waiting for CP State B ([V2G-DC-988]): answered when B arrives, or with
    // FAILED when V2G_SECC_CPState_Detection_Timeout expires ([V2G-DC-556]).
    std::optional<message_din::WeldingDetectionRequest> pending_req{};
};

} // namespace iso15118::din::state
