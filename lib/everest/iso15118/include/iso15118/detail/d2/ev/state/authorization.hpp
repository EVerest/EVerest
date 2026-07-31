// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message_2/authorization.hpp>

namespace iso15118::d2::ev::state {

namespace dt = message_2::datatypes;

namespace authorization {

// Empty AuthorizationReq (no id, no gen_challenge): EIM only.
message_2::AuthorizationRequest create_request();

enum class Action {
    Retry,  // evse_processing == Ongoing: resend
    Done,   // evse_processing == Finished: authorized, proceed
    Failed, // response_code >= FAILED
};

struct Result {
    Action action{Action::Failed};
};

Result handle_response(const message_2::AuthorizationResponse& res);

} // namespace authorization

} // namespace iso15118::d2::ev::state
