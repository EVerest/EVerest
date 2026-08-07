// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message_din/contract_authentication.hpp>

namespace iso15118::din::state {

namespace dt = message_din::datatypes;

// EIM: respond EVSEProcessing=Ongoing until authorized, then Finished (din_server.cpp
// handle_din_contract_authentication). A rejection, or the configured Ongoing window elapsing, ends the
// loop with FAILED instead.
message_din::ContractAuthenticationResponse handle_request(bool authorized, const dt::SessionId& session_id,
                                                           bool rejected = false, bool timeout_reached = false);

} // namespace iso15118::din::state
