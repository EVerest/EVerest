// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include <iso15118/message_din/charge_parameter_discovery.hpp>
#include <iso15118/session/feedback.hpp>

namespace iso15118::ev::din::state {

namespace dt = message_din::datatypes;

namespace charge_parameter_discovery {

message_din::ChargeParameterDiscoveryRequest create_request(dt::EnergyTransferMode requested_energy_transfer_type,
                                                            const dt::DcEvChargeParameter& dc_ev_charge_parameter);

// The response code is validated by expect_response; only EVSEProcessing and the offered parameters
// are decided here.
struct Result {
    // EVSEProcessing is Finished; otherwise the poll repeats.
    bool finished{false};
    std::optional<session::feedback::DcMaximumLimits> limits{std::nullopt};
    // A finished response with no energy (StopCharging or a shutdown status) goes to SessionStop.
    // EvseV2G rejects a CableCheckReq there with FAILED_SequenceError.
    bool evse_stopping{false};
};

Result handle_response(const message_din::ChargeParameterDiscoveryResponse& res);

} // namespace charge_parameter_discovery

} // namespace iso15118::ev::din::state
