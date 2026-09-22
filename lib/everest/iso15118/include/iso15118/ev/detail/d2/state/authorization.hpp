// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message_2/authorization.hpp>

namespace iso15118::ev::d2::state {

namespace dt = message_2::datatypes;

namespace authorization {

// Unsigned request with id and GenChallenge omitted: the EIM request, and every session's re-poll
// while EVSEProcessing is Ongoing.
message_2::AuthorizationRequest create_request();

// Plug & Charge: the request echoes the GenChallenge from PaymentDetailsRes and is signed with the
// contract key [V2G2-684].
message_2::AuthorizationRequest create_pnc_request(const dt::GenChallenge& gen_challenge);

} // namespace authorization

} // namespace iso15118::ev::d2::state
