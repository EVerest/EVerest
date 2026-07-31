// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message_din/contract_authentication.hpp>

namespace iso15118::din::ev::state {

namespace dt = message_din::datatypes;

namespace contract_authentication {

message_din::ContractAuthenticationRequest create_request();

enum class Action {
    Failed,
    Done,
    Retry,
};

struct Result {
    Action action{Action::Retry};
};

Result handle_response(const message_din::ContractAuthenticationResponse& res);

} // namespace contract_authentication

} // namespace iso15118::din::ev::state
