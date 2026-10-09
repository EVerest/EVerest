// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message_din/contract_authentication.hpp>

namespace iso15118::ev::din::state {

namespace contract_authentication {

message_din::ContractAuthenticationRequest create_request();

// True once the SECC reports EVSEProcessing=Finished; otherwise the poll repeats.
bool authorization_finished(const message_din::ContractAuthenticationResponse& res);

} // namespace contract_authentication

} // namespace iso15118::ev::din::state
