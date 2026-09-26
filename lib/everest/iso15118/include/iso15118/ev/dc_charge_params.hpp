// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

namespace iso15118::ev {

/**
 * EV DC charge parameters. The limits and targets are static for the session; present_soc and
 * present_voltage are refreshed while it runs.
 */
struct DcChargeParams {
    float max_charge_power{0.0f};
    float max_charge_current{0.0f};
    float max_discharge_power{0.0f};
    float min_discharge_power{0.0f};
    float max_discharge_current{0.0f};
    float max_voltage{0.0f};
    float min_voltage{0.0f};
    float energy_capacity{0.0f};
    float target_voltage{0.0f};
    float target_current{0.0f};

    double present_soc{0.0};
    float present_voltage{0.0f};
};

} // namespace iso15118::ev
