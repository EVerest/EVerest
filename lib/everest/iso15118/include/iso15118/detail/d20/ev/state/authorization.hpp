// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message/authorization.hpp>

namespace iso15118::d20::ev::state {

namespace dt = message_20::datatypes;

namespace authorization {

message_20::AuthorizationRequest create_request(dt::Authorization selected_auth_service);

enum class Action {
    Retry,  // evse_processing == Ongoing: resend the (cached) request
    Done,   // evse_processing == Finished: authorized, proceed
    Failed, // response_code >= FAILED
};

struct Result {
    Action action{Action::Failed};
};

Result handle_response(const message_20::AuthorizationResponse& res);

} // namespace authorization

} // namespace iso15118::d20::ev::state
