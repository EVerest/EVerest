// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <cmath>

#include <iso15118/ev/d20/ac_phase_split.hpp>
#include <iso15118/ev/params/ac.hpp>
#include <iso15118/ev/params/dc.hpp>
#include <iso15118/message/common_types.hpp>

namespace iso15118::ev::d20 {

/**
 * The wire value of a limit on energy flowing from the EV to the EVSE. [V2G20-1035] makes that
 * direction negative, and AMD1 Table 94 bounds every EV discharge power and current, var injection
 * and excited discharge power by <=0. The EV keeps these limits as magnitudes until they are sent;
 * a setting given already negative goes out <=0 too.
 */
inline float discharge_on_wire(float magnitude) {
    return -std::fabs(magnitude);
}

// The AC max/min discharge power pair, split across the connector's lines.
template <typename Mode>
void emit_ac_discharge_limits(const params::AcChargeParams& params, message_20::datatypes::AcConnector connector,
                              Mode& mode) {
    emit_ac_limit(discharge_on_wire(params.max_discharge_power), params.phase_count, connector,
                  mode.max_discharge_power, mode.max_discharge_power_L2, mode.max_discharge_power_L3);
    emit_ac_limit(discharge_on_wire(params.min_discharge_power), params.phase_count, connector,
                  mode.min_discharge_power, mode.min_discharge_power_L2, mode.min_discharge_power_L3);
}

// The DC max/min discharge power and max discharge current.
template <typename Mode> void fill_dc_discharge_limits(const params::DcChargeParams& params, Mode& mode) {
    mode.max_discharge_power = message_20::datatypes::from_float(discharge_on_wire(params.max_discharge_power));
    mode.min_discharge_power = message_20::datatypes::from_float(discharge_on_wire(params.min_discharge_power));
    mode.max_discharge_current = message_20::datatypes::from_float(discharge_on_wire(params.max_discharge_current));
}

} // namespace iso15118::ev::d20
