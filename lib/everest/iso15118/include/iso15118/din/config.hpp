// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include <iso15118/d20/control_event.hpp>
#include <iso15118/message_din/common_types.hpp>

namespace iso15118::din {

namespace dt = message_din::datatypes;

struct SessionConfig {
    // EVSEID as hexBinary (DIN uses a byte string, MaxLength 32).
    std::vector<uint8_t> evse_id;

    uint16_t charge_service_id{1};
    bool free_service{true};
    dt::SupportedEnergyTransferMode energy_transfer_mode{dt::SupportedEnergyTransferMode::DC_extended};

    // Two flavours (EvseV2G parity): the evse_capability_* values are the maximum the EVSE could ever
    // deliver, advertised in ChargeParameterDiscoveryRes and as the SAScheduleList PMax, while the
    // evse_maximum_* values are what energy management currently grants and go in CurrentDemandRes.
    double evse_capability_maximum_current_limit{0.0};
    std::optional<double> evse_capability_maximum_power_limit{std::nullopt};
    double evse_capability_maximum_voltage_limit{0.0};
    double evse_minimum_current_limit{0.0};
    double evse_minimum_voltage_limit{0.0};
    double evse_maximum_current_limit{0.0};
    std::optional<double> evse_maximum_power_limit{std::nullopt};
    double evse_maximum_voltage_limit{0.0};
    double evse_peak_current_ripple{0.0};
    // Only sent when the module reported them via set_charging_parameters.
    std::optional<double> evse_current_regulation_tolerance{std::nullopt};
    std::optional<double> evse_energy_to_be_delivered{std::nullopt};

    // IEC 61851-23:2023 CC.3.5.3: the ChargeParameterDiscoveryRes signals StopCharging and, unless the
    // EV is allowed to ignore it, the SECC stops before the cable check.
    d20::NoEnergyPauseMode no_energy_pause{d20::NoEnergyPauseMode::None};

    // In MILLISECONDS; 0 waits indefinitely. Only the EIM value applies -- DIN knows no other payment
    // option. EvseV2G never bounded this loop at all, so a DIN session there could sit in Ongoing until
    // the EV gave up.
    uint32_t auth_timeout_eim_ms{300000};
};

} // namespace iso15118::din
