// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include <iso15118/message/dc_charge_loop.hpp>

namespace iso15118::d20::ev::state {

namespace dt = message_20::datatypes;

namespace dc_charge_loop {

// Inputs for the DC charge-loop request (see flow spec §3 DC_ChargeLoop). Unused fields are ignored
// by the selected variant builder.
struct RequestParams {
    dt::RationalNumber present_voltage{};
    dt::DisplayParameters display{};

    // Dynamic energy-request window.
    dt::RationalNumber target_energy_request{};
    dt::RationalNumber max_energy_request{};
    dt::RationalNumber min_energy_request{};

    // Charge limits.
    dt::RationalNumber max_charge_power{};
    dt::RationalNumber min_charge_power{};
    dt::RationalNumber max_charge_current{};
    dt::RationalNumber max_voltage{};
    dt::RationalNumber min_voltage{};

    // Scheduled set points.
    dt::RationalNumber target_current{};
    dt::RationalNumber target_voltage{};

    // BPT discharge limits.
    dt::RationalNumber max_discharge_power{};
    dt::RationalNumber min_discharge_power{};
    dt::RationalNumber max_discharge_current{};
    std::optional<dt::RationalNumber> max_v2x_energy_request{std::nullopt};
    std::optional<dt::RationalNumber> min_v2x_energy_request{std::nullopt};
};

message_20::DC_ChargeLoopRequest create_dynamic_request(const RequestParams& params, bool bpt);
message_20::DC_ChargeLoopRequest create_scheduled_request(const RequestParams& params, bool bpt);

struct Result {
    bool valid{false};
    std::optional<dt::EvseNotification> notification{std::nullopt};
    float present_voltage{0.0f};
    float present_current{0.0f};
};

Result handle_response(const message_20::DC_ChargeLoopResponse& res);

} // namespace dc_charge_loop

} // namespace iso15118::d20::ev::state
