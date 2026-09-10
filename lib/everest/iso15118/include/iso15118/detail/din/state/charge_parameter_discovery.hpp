// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include <iso15118/din/config.hpp>
#include <iso15118/message_din/charge_parameter_discovery.hpp>

namespace iso15118::din::state {

namespace dt = message_din::datatypes;

// processing_finished drives the EVSEProcessing Ongoing->Finished sequencing. error_status_code
// carries a module-reported fault, which [V2G-DC-638] lets displace EVSE_Ready.
message_din::ChargeParameterDiscoveryResponse
handle_request(const message_din::ChargeParameterDiscoveryRequest& req, const SessionConfig& config,
               bool processing_finished, const dt::SessionId& session_id, bool charger_stop = false,
               std::optional<dt::DcEvseStatusCode> error_status_code = std::nullopt);

} // namespace iso15118::din::state
