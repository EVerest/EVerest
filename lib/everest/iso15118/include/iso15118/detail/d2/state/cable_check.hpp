// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include <iso15118/message_2/cable_check.hpp>
#include <iso15118/message_2/common_types.hpp>

namespace iso15118::d2::state {

namespace dt = message_2::datatypes;

message_2::CableCheckResponse handle_request(const message_2::CableCheckRequest& req, const dt::SessionId& session_id,
                                             bool cable_check_done, bool cable_check_fault = false,
                                             std::optional<dt::DC_EVSEStatusCode> error_status_code = std::nullopt);

} // namespace iso15118::d2::state
