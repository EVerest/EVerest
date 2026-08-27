// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/d20/ev/config.hpp>
#include <iso15118/message/dc_charge_parameter_discovery.hpp>
#include <iso15118/session/ev_feedback.hpp>

namespace iso15118::d20::ev::state {

namespace dt = message_20::datatypes;

namespace dc_charge_parameter_discovery {

// Build the unidirectional DC transfer-mode request from the EV charge parameters.
message_20::DC_ChargeParameterDiscoveryRequest create_request(const DcEvChargeParameters& params);

// Build the bidirectional (BPT) DC transfer-mode request from the EV charge parameters.
message_20::DC_ChargeParameterDiscoveryRequest create_bpt_request(const DcEvBptChargeParameters& params);

struct Result {
    bool valid{false};
    // SECC-advertised present limits (max power/current/voltage), forwarded via dc_evse_present_limits.
    session::ev::feedback::DcMaximumLimits limits{};
};

Result handle_response(const message_20::DC_ChargeParameterDiscoveryResponse& res);

} // namespace dc_charge_parameter_discovery

} // namespace iso15118::d20::ev::state
