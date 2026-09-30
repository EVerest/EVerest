// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include <iso15118/message_2/session_stop.hpp>
#include <iso15118/message_2/welding_detection.hpp>

#include "../states.hpp"

namespace iso15118::d2::state {

// [V2G2-601]: entered from PowerDeliveryRes(Stop), where the EV may weld-detect, restart the
// parameter exchange to charge again, or end the session.
//
// The CP State B gate lives here because it applies to the request that follows PowerDelivery(Stop)
// ([V2G2-913], [V2G2-920]..[V2G2-922]); once it has been passed, WeldingDetection below has no gate.
struct PostCharge : public StateBase {
    PostCharge(Context& ctx) : StateBase(ctx, StateID::PostCharge) {
    }

    void enter() final;
    Result on_event(Event) final;
    Result on_request(const message_2::Variant& received) final;
    void leave() final;

private:
    // Both apply the [V2G2-920] gate, which names WeldingDetectionReq and SessionStopReq alike.
    Result accept_welding_detection(const message_2::WeldingDetectionRequest& req);
    Result accept_session_stop(const message_2::SessionStopRequest& req);

    // At most one is ever set. Answered when B arrives, or FAILED when the performance time expires.
    std::optional<message_2::WeldingDetectionRequest> pending_weld{};
    std::optional<message_2::SessionStopRequest> pending_stop{};
};

// [V2G2-597] narrows the node: restarting the parameter exchange is no longer in sequence once
// welding detection has begun. Figure 104 draws both halves as one node; the requirements do not.
struct WeldingDetection : public StateBase {
    WeldingDetection(Context& ctx) : StateBase(ctx, StateID::WeldingDetection) {
    }

    void enter() final;
    Result on_event(Event) final;
    Result on_request(const message_2::Variant& received) final;
};

} // namespace iso15118::d2::state
