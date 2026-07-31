// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <cstdint>
#include <string>

#include <iso15118/message_2/session_setup.hpp>

namespace iso15118::d2::ev::state {

namespace dt = message_2::datatypes;

namespace session_setup {

message_2::SessionSetupRequest create_request(const std::array<uint8_t, 6>& evcc_mac);

struct Result {
    bool valid{false};
    bool old_session_joined{false};
    dt::SessionId session_id{};
    std::string evse_id;
};

Result handle_response(const message_2::SessionSetupResponse& res);

} // namespace session_setup

} // namespace iso15118::d2::ev::state
