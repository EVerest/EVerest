// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <cstdint>
#include <string>

#include <iso15118/message_din/session_setup.hpp>

namespace iso15118::din::ev::state {

namespace dt = message_din::datatypes;

namespace session_setup {

message_din::SessionSetupRequest create_request(const std::array<uint8_t, 6>& evcc_mac,
                                                const dt::SessionId& session_id);

struct Result {
    bool valid{false};
    bool new_session{false};
    dt::SessionId session_id{};
    std::string evse_id;
};

Result handle_response(const message_din::SessionSetupResponse& res);

} // namespace session_setup

} // namespace iso15118::din::ev::state
