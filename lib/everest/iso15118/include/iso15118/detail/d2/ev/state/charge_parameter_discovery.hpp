// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include <iso15118/d2/ev/config.hpp>
#include <iso15118/message_2/charge_parameter_discovery.hpp>
#include <iso15118/session/ev_feedback.hpp>

namespace iso15118::d2::ev::state {

namespace dt = message_2::datatypes;

namespace charge_parameter_discovery {

// Build the DC parameter request from the EV config.
message_2::ChargeParameterDiscoveryRequest create_dc_request(const EvSessionConfig& config,
                                                             const dt::DC_EVStatus& dc_ev_status);

// Build the AC parameter request from the EV config.
message_2::ChargeParameterDiscoveryRequest create_ac_request(const EvSessionConfig& config);

struct Result {
    bool valid{false};
    bool finished{false};

    // From the (first) SAScheduleTuple, if present.
    std::optional<uint8_t> sa_schedule_tuple_id{std::nullopt};
    std::optional<dt::PMaxScheduleEntry> selected_pmax_entry{std::nullopt};
    // Full PMaxSchedule of the selected SAScheduleTuple.
    everest::lib::util::fixed_vector<dt::PMaxScheduleEntry, 12> selected_pmax_schedule{};

    // SECC-advertised limits.
    std::optional<session::ev::feedback::DcMaximumLimits> dc_limits{std::nullopt};
    std::optional<dt::PhysicalValue> ac_nominal_voltage{std::nullopt};
    std::optional<dt::PhysicalValue> ac_max_current{std::nullopt};
};

Result handle_response(const message_2::ChargeParameterDiscoveryResponse& res);

} // namespace charge_parameter_discovery

} // namespace iso15118::d2::ev::state
