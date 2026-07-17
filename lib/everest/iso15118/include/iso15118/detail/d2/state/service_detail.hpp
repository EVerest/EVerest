// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message_2/common_types.hpp>
#include <iso15118/message_2/service_detail.hpp>

namespace iso15118::d2::state {

namespace dt = message_2::datatypes;

message_2::ServiceDetailResponse handle_request(const message_2::ServiceDetailRequest& req,
                                                const dt::SessionId& session_id, uint16_t charge_service_id);

} // namespace iso15118::d2::state
