// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/din/context.hpp>
#include <iso15118/message_din/common_types.hpp>
#include <iso15118/message_din/variant.hpp>

namespace iso15118::din::state {

namespace dt = message_din::datatypes;

// Builds the response corresponding to the *received* request type with all schema-mandatory fields
// populated, so cbexigen actually EXI-encodes it. An unknown request type stages nothing.
void respond_with_code(Context& ctx, const message_din::Variant& received, dt::ResponseCode code);

// [V2G-DC-666] (section 9.7.4.2.3): answer with the response corresponding to the *received* request
// carrying FAILED_SequenceError, perform an EVSE-initiated emergency shutdown and close the TCP
// connection ([V2G-DC-116]). Answering with the receiving state's own response type is a conformance
// violation (TC_SECC_VTB_ServiceDiscovery_002).
void respond_sequence_error(Context& ctx, const message_din::Variant& received);

// [V2G-DC-391]: every request after SessionSetup must echo the assigned SessionID. Returns true if
// the request was rejected -- the caller must then return without processing it, so no side effect
// (feedback signal, power-supply setpoint, CP gate) fires for an unknown session.
bool reject_unknown_session(Context& ctx, const message_din::Variant& received);

} // namespace iso15118::din::state
