// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include <iso15118/din/config.hpp>
#include <iso15118/message_din/charge_parameter_discovery.hpp>

namespace iso15118::din::state {

namespace dt = message_din::datatypes;

// Builds a DC_EVSEChargeParameter response from the configured limits. processing_finished drives the
// EVSEProcessing Ongoing->Finished sequencing. Rejects AC / wrong energy transfer type
// (din_server.cpp handle_din_charge_parameter).
// error_status_code carries the module-reported EVSE error (send_error) into the response: [V2G-DC-638]
// has the SECC report EVSE_Ready only while no other requirement applies, so a fault belongs here too.
message_din::ChargeParameterDiscoveryResponse
handle_request(const message_din::ChargeParameterDiscoveryRequest& req, const SessionConfig& config,
               bool processing_finished, const dt::SessionId& session_id, bool charger_stop = false,
               std::optional<dt::DcEvseStatusCode> error_status_code = std::nullopt);

} // namespace iso15118::din::state
