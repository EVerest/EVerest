// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <string>

#include <iso15118/message_2/common_types.hpp>
#include <iso15118/message_2/session_setup.hpp>

namespace iso15118::d2::state {

namespace dt = message_2::datatypes;

message_2::SessionSetupResponse handle_request(const message_2::SessionSetupRequest& req,
                                               const dt::SessionId& session_id, const std::string& evse_id,
                                               bool new_session);

} // namespace iso15118::d2::state
