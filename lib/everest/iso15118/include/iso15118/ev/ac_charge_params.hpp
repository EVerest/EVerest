// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>

namespace iso15118::ev {

/**
 * EV AC charge parameters. The limits are static for the session; present_active_power,
 * present_voltage, present_frequency and der_alarm_status are refreshed while it runs.
 */
struct AcChargeParams {
    // The EV's own line count, 1 or 3, not the charger's.
    uint8_t phase_count{3};

    // Totals across phase_count lines; \ref split_ac_limit divides them for the connector.
    float max_charge_power{0.0f};
    float min_charge_power{0.0f};
    float max_discharge_power{0.0f};
    float min_discharge_power{0.0f};

    float present_active_power{0.0f};

    // AC_DER_SAE only. Unset: not measured, and the charge loop uses the profile's nominal values.
    std::optional<float> present_voltage{};   // V
    std::optional<float> present_frequency{}; // Hz

    // AC_DER_SAE only: DERAlarmStatus bitmap, AMD1 Table M.9 (Table M.11 for Scheduled).
    std::uint32_t der_alarm_status{0};
};

} // namespace iso15118::ev
