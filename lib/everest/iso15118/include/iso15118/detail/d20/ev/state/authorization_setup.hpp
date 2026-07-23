// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <vector>

#include <iso15118/message/authorization_setup.hpp>

namespace iso15118::d20::ev::state {

namespace dt = message_20::datatypes;

namespace authorization_setup {

message_20::AuthorizationSetupRequest create_request();

struct Result {
    bool valid{false};
    bool certificate_installation_service{false};
    std::vector<dt::Authorization> offered_auth_services;
};

Result handle_response(const message_20::AuthorizationSetupResponse& res);

} // namespace authorization_setup

} // namespace iso15118::d20::ev::state
