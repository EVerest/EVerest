// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>

#include <iso15118/message_din/service_payment_selection.hpp>

namespace iso15118::din::state {

namespace dt = message_din::datatypes;

message_din::ServicePaymentSelectionResponse handle_request(const message_din::ServicePaymentSelectionRequest& req,
                                                            uint16_t charge_service_id,
                                                            const dt::SessionId& session_id);

} // namespace iso15118::din::state
