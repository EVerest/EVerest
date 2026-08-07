// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/d2/context.hpp>
#include <iso15118/message_2/common_types.hpp>
#include <iso15118/message_2/variant.hpp>

namespace iso15118::d2::state {

namespace dt = message_2::datatypes;

// Builds the response message *corresponding to the received request type*, carrying the given
// ResponseCode, with all schema-mandatory fields populated (so cbexigen actually EXI-encodes it), and
// stages it via ctx.respond(). If the received message is not a known request type, it logs and stages
// nothing (the caller still stops the session). Used for both the out-of-sequence [V2G2-539] path
// (FAILED_SequenceError) and the unknown-session path (FAILED_UnknownSession).
void respond_with_code(Context& ctx, const message_2::Variant& received, dt::ResponseCode code);

// [V2G2-539]: when the SECC receives a valid but out-of-sequence request, it must answer with the
// received-type response carrying ResponseCode FAILED_SequenceError, and then terminate the session.
void respond_sequence_error(Context& ctx, const message_2::Variant& received);

// [V2G2-388]/[V2G2-390]: after SessionSetup every request must echo the assigned SessionID. If the
// received request carries a different SessionID, the SECC answers with the received-type response
// carrying FAILED_UnknownSession, terminates the session, and returns true. Returns false (no action)
// when the SessionID matches. SessionSetup itself must NOT call this (it assigns/joins the id).
bool reject_unknown_session(Context& ctx, const message_2::Variant& received);

} // namespace iso15118::d2::state
