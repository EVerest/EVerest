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
    // Static: advertised limits.
    float max_charge_power{0.0f};
    float min_charge_power{0.0f};

    // Drives whether the optional L2/L3 phase limits are emitted.
    bool three_phase{false};

    // Live: refreshed by the module during the session.
    float present_active_power{0.0f};
};

} // namespace iso15118::ev
