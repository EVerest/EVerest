// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include <iso15118/d20/ac_powers.hpp>
#include <iso15118/message/ac_charge_loop.hpp>

namespace iso15118::d20::ev::state {

namespace dt = message_20::datatypes;

namespace ac_charge_loop {

// Inputs for the AC charge-loop request (see flow spec §3 AC_ChargeLoop). Unused fields are ignored
// by the selected variant builder.
struct RequestParams {
    std::optional<uint32_t> departure_time{std::nullopt};
    dt::DisplayParameters display{};

    // Dynamic energy-request window.
    dt::RationalNumber target_energy_request{};
    dt::RationalNumber max_energy_request{};
    dt::RationalNumber min_energy_request{};

    // Charge limits and present powers.
    dt::RationalNumber max_charge_power{};
    dt::RationalNumber min_charge_power{};
    dt::RationalNumber present_active_power{};
    dt::RationalNumber present_reactive_power{};

    // BPT discharge limits.
    dt::RationalNumber max_discharge_power{};
    dt::RationalNumber min_discharge_power{};
    std::optional<dt::RationalNumber> max_v2x_energy_request{std::nullopt};
    std::optional<dt::RationalNumber> min_v2x_energy_request{std::nullopt};
};

message_20::AC_ChargeLoopRequest create_dynamic_request(const RequestParams& params, bool bpt);
message_20::AC_ChargeLoopRequest create_scheduled_request(const RequestParams& params, bool bpt);

struct Result {
    bool valid{false};
    std::optional<dt::EvseNotification> notification{std::nullopt};
    // Target power set points advertised by the SECC, forwarded via ac_evse_target_power on every Res.
    d20::AcTargetPower target{};
};

Result handle_response(const message_20::AC_ChargeLoopResponse& res);

} // namespace ac_charge_loop

} // namespace iso15118::d20::ev::state
