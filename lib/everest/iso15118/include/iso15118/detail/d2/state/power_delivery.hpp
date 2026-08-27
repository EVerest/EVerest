// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>

#include <iso15118/message_2/charge_parameter_discovery.hpp>
#include <iso15118/message_2/common_types.hpp>
#include <iso15118/message_2/power_delivery.hpp>

namespace iso15118::d2::state {

namespace dt = message_2::datatypes;

// Validates an EV ChargingProfile against the advertised SAScheduleList: every ProfileEntry max_power
// must stay within the advertised PMax [V2G2-224/225]. Returns true when the profile is acceptable.
bool charging_profile_within_limits(const dt::ChargingProfile& profile, const dt::SAScheduleList& sa_schedule_list,
                                    uint8_t advertised_sa_schedule_tuple_id);

// Builds the PowerDeliveryRes, validating the echoed SAScheduleTupleID against the advertised one, the
// presence of a ChargingProfile (AC Start) and its max_power entries against the advertised PMax
// (EvseV2G parity). When charger_stop is set, the response signals EVSENotification::StopCharging.
message_2::PowerDeliveryResponse handle_request(const message_2::PowerDeliveryRequest& req,
                                                const dt::SessionId& session_id, bool is_dc,
                                                uint8_t advertised_sa_schedule_tuple_id, bool cable_check_done,
                                                bool charger_stop, const dt::SAScheduleList& sa_schedule_list);

} // namespace iso15118::d2::state
