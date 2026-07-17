// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <string>

#include <iso15118/message/session_setup.hpp>

namespace iso15118::d20::ev::state {

namespace dt = message_20::datatypes;

namespace session_setup {

message_20::SessionSetupRequest create_request(const std::string& evcc_id);

struct Result {
    bool valid{false};
    bool new_session{false};
    dt::SessionId session_id{};
    std::string evse_id;
};

Result handle_response(const message_20::SessionSetupResponse& res);

} // namespace session_setup

} // namespace iso15118::d20::ev::state
