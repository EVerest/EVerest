// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/d20/session.hpp>
#include <iso15118/message/session_setup.hpp>

namespace iso15118::d20::state {

msg::d20::SessionSetupResponse handle_request(const msg::d20::SessionSetupRequest& req, const d20::Session& session,
                                                const std::string& evse_id, bool new_session);
} // namespace iso15118::d20::state
