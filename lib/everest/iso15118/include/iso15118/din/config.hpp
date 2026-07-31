// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include <iso15118/message_din/common_types.hpp>

namespace iso15118::din {

namespace dt = message_din::datatypes;

// SECC-side, session-scoped configuration for a DIN SPEC 70121 charging session. Holds the static EVSE
// information the response messages need (EVSEID, DC limits, offered charge service). Runtime quantities
// (present voltage/current, isolation status, processing flags) live in the din::Context instead, as they
// are driven by control events during the session.
struct SessionConfig {
    // EVSEID as hexBinary (DIN uses a byte string, MaxLength 32).
    std::vector<uint8_t> evse_id;

    // Offered ChargeService.
    uint16_t charge_service_id{1};
    bool free_service{true};
    dt::SupportedEnergyTransferMode energy_transfer_mode{dt::SupportedEnergyTransferMode::DC_extended};

    // DC EVSE limits advertised in ChargeParameterDiscoveryRes and reported in CurrentDemandRes.
    double evse_maximum_current_limit{0.0};
    std::optional<double> evse_maximum_power_limit{std::nullopt};
    double evse_maximum_voltage_limit{0.0};
    double evse_minimum_current_limit{0.0};
    double evse_minimum_voltage_limit{0.0};
    double evse_peak_current_ripple{0.0};
    std::optional<double> evse_energy_to_be_delivered{std::nullopt};
};

} // namespace iso15118::din
