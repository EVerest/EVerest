// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>

#include <iso15118/d2/states.hpp>
#include <iso15118/message_2/charge_parameter_discovery.hpp>
#include <iso15118/message_2/common_types.hpp>
#include <iso15118/message_2/power_delivery.hpp>

namespace iso15118::d2::state {

namespace dt = message_2::datatypes;

// An action on a transition, not a wait state: it is reached from two nodes in each flow, so it
// cannot be a state either of them transitions into. The caller has matched the type against its own
// accepted set, and on AC has already satisfied the contactor gate, so neither function waits.
Result process_ac_power_delivery(Context& ctx, const message_2::PowerDeliveryRequest& req);
Result process_dc_power_delivery(Context& ctx, const message_2::PowerDeliveryRequest& req);

// Every ProfileEntry max_power must stay within the advertised PMax [V2G2-224/225].
bool charging_profile_within_limits(const dt::ChargingProfile& profile, const dt::SAScheduleList& sa_schedule_list,
                                    uint8_t advertised_sa_schedule_tuple_id);

// error_status_code becomes the DC EVSEStatusCode and makes a ChargeProgress=Start request fail with
// FAILED_PowerDeliveryNotApplied [V2G2-480]; rcd_error is the AC counterpart, which has no status code.
message_2::PowerDeliveryResponse
handle_request(const message_2::PowerDeliveryRequest& req, const dt::SessionId& session_id, bool is_dc,
               uint8_t advertised_sa_schedule_tuple_id, dt::IsolationLevel isolation_status, bool charger_stop,
               const dt::SAScheduleList& sa_schedule_list,
               std::optional<dt::DC_EVSEStatusCode> error_status_code = std::nullopt, bool rcd_error = false);

} // namespace iso15118::d2::state
