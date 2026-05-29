// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/d20/session.hpp>
#include <iso15118/message/d20/session_stop.hpp>

namespace iso15118::d20::state {

msg::d20::SessionStopResponse handle_request(const msg::d20::SessionStopRequest& req, const d20::Session& session);

} // namespace iso15118::d20::state
