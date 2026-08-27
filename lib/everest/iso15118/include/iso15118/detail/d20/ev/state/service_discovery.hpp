// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <vector>

#include <iso15118/message/service_discovery.hpp>

namespace iso15118::d20::ev::state {

namespace dt = message_20::datatypes;

namespace service_discovery {

message_20::ServiceDiscoveryRequest create_request();

struct Result {
    bool valid{false};
    bool match_found{false};
    dt::ServiceCategory selected_service{};
};

// Intersects the EV's prioritized energy services with the SECC-offered services and picks the
// highest EV-priority match.
Result handle_response(const message_20::ServiceDiscoveryResponse& res,
                       const std::vector<dt::ServiceCategory>& ev_priority);

} // namespace service_discovery

} // namespace iso15118::d20::ev::state
