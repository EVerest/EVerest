// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/d2/context.hpp>
#include <iso15118/message_2/common_types.hpp>
#include <iso15118/message_2/type.hpp>

namespace iso15118::d2::state {

namespace dt = message_2::datatypes;

// Builds the response corresponding to the *received* request type with all schema-mandatory fields
// populated, so cbexigen actually EXI-encodes it. An unknown request type stages nothing.
void respond_with_code(Context& ctx, message_2::Type received_type, dt::ResponseCode code);

// [V2G2-539]: answer with the received-type response carrying FAILED_SequenceError, then terminate.
// Stopping is not itself required of the SECC -- 8.8.1 only grants it the option -- but [V2G2-486]
// requires the EVCC to stop on any FAILED response, so lingering would hold state for a dead session.
void respond_sequence_error(Context& ctx, message_2::Type received_type);

// [V2G2-460]: every request except SessionSetupReq must echo the assigned SessionID; if it does not,
// answer FAILED_UnknownSession, terminate, and return true.
//
// Returns false in three cases: the SessionID matches; the request *is* a SessionSetupReq, the sole
// exemption; or no session is established yet, where there is nothing to compare against and Table
// 112 does not list FAILED_UnknownSession for SessionSetupRes at all.
bool reject_unknown_session(Context& ctx, message_2::Type received_type, const dt::SessionId& received_id);

} // namespace iso15118::d2::state
