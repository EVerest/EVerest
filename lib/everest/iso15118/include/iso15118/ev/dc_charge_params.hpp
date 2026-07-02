// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

namespace iso15118::ev {

/**
 * EV DC charge parameters passed from the module (command thread) into the FSM.
 *
 * Static fields are supplied once before the session (via the Controller ctor);
 * the live fields (\ref present_soc, \ref present_voltage) are refreshed
 * mid-session. Held in an everest::lib::util::monitor and read as a snapshot from
 * FSM states via Context::get_dc_params().
 */
struct DcChargeParams {
    // Static: negotiated/advertised limits and targets.
    float max_charge_power{0.0f};
    float max_charge_current{0.0f};
    float max_voltage{0.0f};
    float min_voltage{0.0f};
    float energy_capacity{0.0f};
    float target_voltage{0.0f};
    float target_current{0.0f};

    // Live: refreshed by the module during the session.
    double present_soc{0.0};
    float present_voltage{0.0f};
};

} // namespace iso15118::ev
