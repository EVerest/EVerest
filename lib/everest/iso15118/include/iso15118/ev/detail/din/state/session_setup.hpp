// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <cstdint>

#include <iso15118/message_din/session_setup.hpp>

namespace iso15118::ev::din::state {

namespace session_setup {

// Leaves the header to Context::send_request(), which sets the resumed session id or all zeros.
message_din::SessionSetupRequest create_request(const std::array<uint8_t, 6>& evcc_mac);

} // namespace session_setup

} // namespace iso15118::ev::din::state
