// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <vector>

#include <iso15118/message_din/session_setup.hpp>

namespace iso15118::din::state {

namespace dt = message_din::datatypes;

message_din::SessionSetupResponse handle_request(const message_din::SessionSetupRequest& req,
                                                 const dt::SessionId& session_id, const std::vector<uint8_t>& evse_id,
                                                 bool new_session);

} // namespace iso15118::din::state
