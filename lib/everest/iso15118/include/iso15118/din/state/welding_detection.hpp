// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include <iso15118/message_din/session_stop.hpp>
#include <iso15118/message_din/welding_detection.hpp>

#include "../states.hpp"

namespace iso15118::din::state {

struct WeldingDetection : public StateBase {
    WeldingDetection(Context& ctx) : StateBase(ctx, StateID::WeldingDetection) {
    }

    void enter() final;
    Result on_event(Event) final;
    Result on_request(const message_din::Variant& received) final;

private:
    void process_request(const message_din::WeldingDetectionRequest& req);

    // Arm the V2G_SECC_WeldingDetection supervision timer once, on the first WeldingDetectionReq.
    bool welding_started{false};
    // Answered when B arrives, or with FAILED when the detection timeout expires ([V2G-DC-556]).
    std::optional<message_din::WeldingDetectionRequest> pending_req{};
    // A SessionStopReq takes the same gate, so it is parked in its own slot: whichever arrived is the
    // one answered when CP State B shows up.
    std::optional<message_din::SessionStopRequest> pending_stop{};
};

} // namespace iso15118::din::state
