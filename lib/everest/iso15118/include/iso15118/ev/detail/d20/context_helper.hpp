// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/session.hpp>
#include <iso15118/message/common_types.hpp>

// Forward declaration
class Session;

namespace iso15118::ev::d20 {

bool validate_and_setup_header(message_20::Header&, const Session&, const decltype(message_20::Header::session_id)&);

void setup_header(message_20::Header&, const Session&);

bool check_response_code(message_20::datatypes::ResponseCode response_code);

} // namespace iso15118::ev::d20
