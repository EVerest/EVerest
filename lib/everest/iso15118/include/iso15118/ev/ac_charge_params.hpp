// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>

namespace iso15118::ev {

/**
 * EV AC charge parameters passed from the module (command thread) into the FSM.
 *
 * Static fields are supplied once before the session (via the Controller ctor);
 * the live field (\ref present_active_power) is refreshed mid-session. Held in an
 * everest::lib::util::monitor and read as a snapshot from FSM states via
 * Context::get_ac_params().
 */
struct AcChargeParams {
    // The EV's own line count, 1 or 3. Decides how the totals below are divided; it is not the
    // charger's, which arrives separately as the selected connector.
    uint8_t phase_count{3};

    // Static: advertised limits, as totals across the EV's own phase_count lines. They are split
    // per line at emission time (\ref split_ac_limit), because ISO 15118-20 reads the base element
    // as a sum or as L1 depending on the selected connector and on whether peers are present.
    float max_charge_power{0.0f};
    float min_charge_power{0.0f};
    float max_discharge_power{0.0f};
    float min_discharge_power{0.0f};

    // Live: refreshed by the module during the session.
    float present_active_power{0.0f};
};

} // namespace iso15118::ev
