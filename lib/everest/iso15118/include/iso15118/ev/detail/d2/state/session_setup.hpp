// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message_2/session_setup.hpp>

namespace iso15118::ev::d2::state {

namespace dt = message_2::datatypes;

namespace session_setup {

// Leaves the header to Context::send_request(), which sets the resumed session id or all zeros.
message_2::SessionSetupRequest create_request(const dt::EvccId& evcc_id);

} // namespace session_setup

} // namespace iso15118::ev::d2::state
