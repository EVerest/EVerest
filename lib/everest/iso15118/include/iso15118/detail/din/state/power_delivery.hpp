// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message_din/power_delivery.hpp>

namespace iso15118::din::state {

namespace dt = message_din::datatypes;

message_din::PowerDeliveryResponse handle_request(const message_din::PowerDeliveryRequest& req,
                                                  const dt::SessionId& session_id);

} // namespace iso15118::din::state
