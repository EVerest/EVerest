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

private:
    Result answer(const message_20::DC_WeldingDetectionRequest& req);
    Result answer_pending_req();

    // Every request waits for values of the welding detection voltage source measured after it.
    std::optional<message_20::DC_WeldingDetectionRequest> pending_req{};
    bool welding_voltage_wait_expired{false};
};

} // namespace iso15118::d20::state
