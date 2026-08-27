// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/din/config.hpp>
#include <iso15118/message_din/charge_parameter_discovery.hpp>

namespace iso15118::din::state {

namespace dt = message_din::datatypes;

// Builds a DC_EVSEChargeParameter response from the configured limits. processing_finished drives the
// EVSEProcessing Ongoing->Finished sequencing. Rejects AC / wrong energy transfer type
// (din_server.cpp handle_din_charge_parameter).
message_din::ChargeParameterDiscoveryResponse handle_request(const message_din::ChargeParameterDiscoveryRequest& req,
                                                             const SessionConfig& config, bool processing_finished,
                                                             const dt::SessionId& session_id);

} // namespace iso15118::din::state
