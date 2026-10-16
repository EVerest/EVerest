// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include <iso15118/message/dc_welding_detection.hpp>

#include "../states.hpp"

namespace iso15118::d20::state {

struct DC_WeldingDetection : public StateBase {
    DC_WeldingDetection(Context& ctx) : StateBase(ctx, StateID::DC_WeldingDetection) {
    }

    void enter() final;

    Result feed(Event) final;
    void leave() final;

private:
    Result answer(const message_20::DC_WeldingDetectionRequest& req);

    float present_voltage{0};
    // Each DC_WeldingDetectionReq waits for present values received after it, so that the EVSEPresentVoltage
    // was measured after the request [IEC 61851-23:2023 CC.6.3]. Answered with the last values after
    // d20::TIMEOUT_PRESENT_VALUES.
    std::optional<message_20::DC_WeldingDetectionRequest> pending_req{};
    uint8_t present_values_outstanding{0};
};

} // namespace iso15118::d20::state
