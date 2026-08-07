// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>
#include <string>

#include <iso15118/d2/config.hpp>
#include <iso15118/message_2/common_types.hpp>
#include <iso15118/message_2/current_demand.hpp>

namespace iso15118::d2::state {

namespace dt = message_2::datatypes;

// Builds the CurrentDemandRes. Takes no request: every element of the response comes from the SECC's own
// configuration and present output -- the SessionID is validated by the caller (reject_unknown_session),
// and the *LimitAchieved flags describe the EVSE, not the EV's target [Table 71].
message_2::CurrentDemandResponse handle_request(const dt::SessionId& session_id, const d2::SessionConfig& config,
                                                float present_voltage, float present_current,
                                                uint8_t sa_schedule_tuple_id, bool charger_stop, bool request_receipt,
                                                const std::optional<dt::MeterInfo>& meter_info = std::nullopt,
                                                bool pnc_selected = false);

} // namespace iso15118::d2::state
