// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message_2/authorization.hpp>

namespace iso15118::ev::d2::state {

namespace dt = message_2::datatypes;

namespace authorization {

// EIM: id and gen_challenge omitted.
message_2::AuthorizationRequest create_request();

// Plug & Charge: the request echoes the GenChallenge from PaymentDetailsRes and is signed with the
// contract key [V2G2-684].
message_2::AuthorizationRequest create_pnc_request(const dt::GenChallenge& gen_challenge);

} // namespace authorization

} // namespace iso15118::ev::d2::state
