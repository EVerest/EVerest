// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/d2/config.hpp>
#include <iso15118/message_2/charge_parameter_discovery.hpp>
#include <iso15118/message_2/common_types.hpp>

namespace iso15118::d2::state {

namespace dt = message_2::datatypes;

// Builds the single advertised SAScheduleList (one tuple, id 1, one PMaxSchedule entry).
dt::SAScheduleList build_sa_schedule_list(const d2::SessionConfig& config, bool is_dc);

message_2::ChargeParameterDiscoveryResponse handle_request(const message_2::ChargeParameterDiscoveryRequest& req,
                                                           const dt::SessionId& session_id,
                                                           const d2::SessionConfig& config);

} // namespace iso15118::d2::state
