// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message_2/common_types.hpp>
#include <iso15118/message_2/payment_service_selection.hpp>

namespace iso15118::d2::state {

namespace dt = message_2::datatypes;

message_2::PaymentServiceSelectionResponse handle_request(const message_2::PaymentServiceSelectionRequest& req,
                                                          const dt::SessionId& session_id, uint16_t charge_service_id,
                                                          bool pnc_enabled, bool cert_service_offered = false);

} // namespace iso15118::d2::state
