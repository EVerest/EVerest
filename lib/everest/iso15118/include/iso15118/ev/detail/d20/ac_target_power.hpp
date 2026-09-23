// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>
#include <type_traits>

#include <iso15118/d20/ac_powers.hpp>
#include <iso15118/message/ac_charge_loop.hpp>
#include <iso15118/message/common_types.hpp>

namespace iso15118::ev::d20 {

// The set point of an AC ChargeLoop response. The BPT and DER modes bind through their bases.
template <typename Mode>
iso15118::d20::AcTargetPower
make_ac_target_power(const Mode& mode, const std::optional<message_20::datatypes::RationalNumber>& target_frequency) {
    static_assert(std::is_base_of_v<message_20::datatypes::Dynamic_AC_CLResControlMode, Mode> or
                  std::is_base_of_v<message_20::datatypes::Scheduled_AC_CLResControlMode, Mode>);
    iso15118::d20::AcTargetPower target;
    target.target_active_power = mode.target_active_power;
    target.target_active_power_L2 = mode.target_active_power_L2;
    target.target_active_power_L3 = mode.target_active_power_L3;
    target.target_reactive_power = mode.target_reactive_power;
    target.target_reactive_power_L2 = mode.target_reactive_power_L2;
    target.target_reactive_power_L3 = mode.target_reactive_power_L3;
    target.target_frequency = target_frequency;
    return target;
}

} // namespace iso15118::ev::d20
