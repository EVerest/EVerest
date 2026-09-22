// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/ev/din/context.hpp>
#include <iso15118/ev/din/states.hpp>
#include <iso15118/ev/session/stop_reason.hpp>
#include <iso15118/ev/welding_detection.hpp>

namespace iso15118::ev::din::state {

// Polls until voltage is safe; an exhausted bound reports failure and terminates the session.
struct WeldingDetection : public StateBase {
    WeldingDetection(Context& ctx, session::StopReason stop_reason) :
        StateBase(ctx, StateID::WeldingDetection), m_stop_reason(stop_reason) {
    }

    void enter() final;
    Result feed(Event) final;

private:
    session::StopReason m_stop_reason;
    ev::welding_detection::Progress progress;
};

// Sends the WeldingDetectionReq and creates the state awaiting it.
BasePointerType to_welding_detection(Context& ctx, session::StopReason stop_reason);

} // namespace iso15118::ev::din::state
