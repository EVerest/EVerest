// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/din/config.hpp>
#include <iso15118/message_din/service_discovery.hpp>

namespace iso15118::din::state {

namespace dt = message_din::datatypes;

message_din::ServiceDiscoveryResponse handle_request(const message_din::ServiceDiscoveryRequest& req,
                                                     const SessionConfig& config, const dt::SessionId& session_id);

} // namespace iso15118::din::state
