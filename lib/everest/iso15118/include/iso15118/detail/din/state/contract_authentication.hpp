// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message_din/contract_authentication.hpp>

namespace iso15118::din::state {

namespace dt = message_din::datatypes;

// EIM: respond EVSEProcessing=Ongoing until authorized, then Finished (din_server.cpp
// handle_din_contract_authentication).
message_din::ContractAuthenticationResponse handle_request(bool authorized, const dt::SessionId& session_id,
                                                           bool rejected = false);

} // namespace iso15118::din::state
