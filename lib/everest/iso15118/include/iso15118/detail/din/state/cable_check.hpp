// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include <iso15118/message_din/cable_check.hpp>

namespace iso15118::din::state {

namespace dt = message_din::datatypes;

message_din::CableCheckResponse handle_request(const message_din::CableCheckRequest& req, bool cable_check_done,
                                               bool cable_check_fault, const dt::SessionId& session_id,
                                               std::optional<dt::DcEvseStatusCode> error_status_code = std::nullopt);

} // namespace iso15118::din::state
