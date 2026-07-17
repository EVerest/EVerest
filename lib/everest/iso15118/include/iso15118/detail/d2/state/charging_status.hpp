// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>

#include <iso15118/d2/config.hpp>
#include <iso15118/message_2/charging_status.hpp>
#include <iso15118/message_2/common_types.hpp>

namespace iso15118::d2::state {

namespace dt = message_2::datatypes;

message_2::ChargingStatusResponse handle_request(const message_2::ChargingStatusRequest& req,
                                                 const dt::SessionId& session_id, const d2::SessionConfig& config,
                                                 uint8_t sa_schedule_tuple_id, bool charger_stop,
                                                 bool request_receipt);

} // namespace iso15118::d2::state
