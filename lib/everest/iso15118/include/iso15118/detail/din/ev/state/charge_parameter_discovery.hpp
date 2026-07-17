// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include <iso15118/message_din/charge_parameter_discovery.hpp>
#include <iso15118/session/ev_feedback.hpp>

namespace iso15118::din::ev::state {

namespace dt = message_din::datatypes;

namespace charge_parameter_discovery {

message_din::ChargeParameterDiscoveryRequest create_request(dt::EnergyTransferMode requested_energy_transfer_type,
                                                            const dt::DcEvChargeParameter& dc_ev_charge_parameter);

enum class Action {
    Failed,
    Done,
    Retry,
};

struct Result {
    Action action{Action::Retry};
    std::optional<session::ev::feedback::DcMaximumLimits> limits{std::nullopt};
    std::optional<dt::DcEvseChargeParameter> evse_parameter{std::nullopt};
    // Set when the finished response signals the SECC will not deliver energy (EVSENotification
    // StopCharging or a shutdown/malfunction status code). The EVCC must then go to SessionStop instead
    // of CableCheck (EvseV2G no-energy-pause path, din_server.cpp WAIT_FOR_SESSIONSTOP).
    bool evse_stopping{false};
};

Result handle_response(const message_din::ChargeParameterDiscoveryResponse& res);

} // namespace charge_parameter_discovery

} // namespace iso15118::din::ev::state
