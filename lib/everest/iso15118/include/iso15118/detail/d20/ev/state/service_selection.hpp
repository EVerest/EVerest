// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>

#include <iso15118/message/service_selection.hpp>

namespace iso15118::d20::ev::state {

namespace dt = message_20::datatypes;

namespace service_selection {

message_20::ServiceSelectionRequest create_request(dt::ServiceCategory energy_service, uint16_t parameter_set_id);

struct Result {
    bool valid{false};
};

Result handle_response(const message_20::ServiceSelectionResponse& res);

} // namespace service_selection

} // namespace iso15118::d20::ev::state
