// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>

#include <iso15118/message_2/charge_parameter_discovery.hpp>
#include <iso15118/message_2/power_delivery.hpp>

namespace iso15118::d2::ev::state {

namespace dt = message_2::datatypes;

namespace power_delivery {

// Builds a ChargingProfile spanning the selected SAScheduleTuple's PMaxSchedule (one ProfileEntry per
// PMaxSchedule entry, echoing its start and PMax) so the EV never exceeds a later interval's PMax. The
// fallback power produces a single flat entry when the schedule is empty.
dt::ChargingProfile build_charging_profile(const everest::lib::util::fixed_vector<dt::PMaxScheduleEntry, 12>& schedule,
                                           float fallback_max_power_w);

message_2::PowerDeliveryRequest create_dc_request(dt::ChargeProgress charge_progress, uint8_t sa_schedule_tuple_id,
                                                  const dt::DC_EVStatus& dc_ev_status, bool charging_complete);

message_2::PowerDeliveryRequest create_ac_request(dt::ChargeProgress charge_progress, uint8_t sa_schedule_tuple_id,
                                                  std::optional<dt::ChargingProfile> charging_profile);

struct Result {
    bool valid{false};
};

Result handle_response(const message_2::PowerDeliveryResponse& res);

} // namespace power_delivery

} // namespace iso15118::d2::ev::state
