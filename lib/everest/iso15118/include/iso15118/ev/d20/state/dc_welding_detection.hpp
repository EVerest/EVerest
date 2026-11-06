// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/ev/session/stop_reason.hpp>
#include <iso15118/ev/welding_detection.hpp>
#include <iso15118/message/dc_welding_detection.hpp>

#include "../states.hpp"

namespace iso15118::ev::d20::state {

struct DC_WeldingDetection : public StateBase {
public:
    DC_WeldingDetection(Context& ctx, session::StopReason stop_reason) :
        StateBase(ctx, StateID::DC_WeldingDetection), m_stop_reason(stop_reason) {
    }

    static message_20::DC_WeldingDetectionRequest make_request(message_20::datatypes::Processing processing);

    Result feed(Event) final;

private:
    session::StopReason m_stop_reason;
    ev::welding_detection::Progress progress;

    Result finish();
};

} // namespace iso15118::ev::d20::state
