// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>

#include <iso15118/message/service_detail.hpp>

namespace iso15118::d20::ev::state {

namespace dt = message_20::datatypes;

namespace service_detail {

message_20::ServiceDetailRequest create_request(uint16_t service_id);

struct Result {
    bool valid{false};
    bool control_mode_found{false};
    uint16_t parameter_set_id{0};
    dt::ControlMode control_mode{dt::ControlMode::Dynamic};
    dt::MobilityNeedsMode mobility_needs_mode{dt::MobilityNeedsMode::ProvidedByEvcc};
};

// Reads the CONTROL_MODE / MobilityNeedsMode from the parameter set that matches the preferred control
// mode. Falls back to the first parameter set that carries a control mode if the preferred one is absent.
Result handle_response(const message_20::ServiceDetailResponse& res, dt::ControlMode preferred_mode);

} // namespace service_detail

} // namespace iso15118::d20::ev::state
