// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

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
    // Static: advertised limits, as three-phase totals. Per-phase L2/L3 fields are
    // never emitted; repeating the total on each phase would overstate the limit.
    float max_charge_power{0.0f};
    float min_charge_power{0.0f};
    float max_discharge_power{0.0f};
    float min_discharge_power{0.0f};

    // Live: refreshed by the module during the session.
    float present_active_power{0.0f};
};

} // namespace iso15118::ev
