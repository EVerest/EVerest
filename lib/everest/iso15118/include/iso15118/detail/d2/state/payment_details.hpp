// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message_2/common_types.hpp>
#include <iso15118/message_2/payment_details.hpp>

namespace iso15118::d2::state {

namespace dt = message_2::datatypes;

// Generate a fresh 16-byte GenChallenge (mirrors the d20 authorization_setup approach).
dt::GenChallenge generate_gen_challenge();

} // namespace iso15118::d2::state
