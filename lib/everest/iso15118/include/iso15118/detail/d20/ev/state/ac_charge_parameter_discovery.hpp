// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/d20/ev/config.hpp>
#include <iso15118/message/ac_charge_parameter_discovery.hpp>

namespace iso15118::d20::ev::state {

namespace dt = message_20::datatypes;

namespace ac_charge_parameter_discovery {

// Build the AC transfer-mode request from the EV charge parameters.
message_20::AC_ChargeParameterDiscoveryRequest create_request(const AcEvChargeParameters& params);

// Build the bidirectional (BPT) AC transfer-mode request from the EV charge parameters.
message_20::AC_ChargeParameterDiscoveryRequest create_bpt_request(const AcEvChargeParameters& params);

struct Result {
    bool valid{false};
    // SECC-advertised AC limits (max charge/discharge power), stored in EvseInfo.
    AcMaximumLimits limits{};
};

Result handle_response(const message_20::AC_ChargeParameterDiscoveryResponse& res);

} // namespace ac_charge_parameter_discovery

} // namespace iso15118::d20::ev::state
