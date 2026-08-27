// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/din/context.hpp>
#include <iso15118/message_din/common_types.hpp>
#include <iso15118/message_din/variant.hpp>

namespace iso15118::din::state {

namespace dt = message_din::datatypes;

// Builds the response message *corresponding to the received request type*, carrying the given
// ResponseCode, with all schema-mandatory fields populated (so cbexigen actually EXI-encodes it), and
// stages it via ctx.respond(). If the received message is not a known DIN request type, it logs and
// stages nothing (the caller still stops the session). Mirrors d2::state::respond_with_code.
void respond_with_code(Context& ctx, const message_din::Variant& received, dt::ResponseCode code);

// [V2G-DC-539] (DIN SPEC 70121 §9.5.2): when the SECC receives a valid but out-of-sequence request, it
// must answer with the *received-type* response carrying ResponseCode FAILED_SequenceError, and then
// terminate the session. Answering with the receiving state's own response type (as the per-state
// fallbacks used to) is a conformance violation (TC_SECC_VTB_ServiceDiscovery_002 expects a
// ContractAuthenticationRes when a ContractAuthenticationReq arrives in place of a ServiceDiscoveryReq).
void respond_sequence_error(Context& ctx, const message_din::Variant& received);

} // namespace iso15118::din::state
