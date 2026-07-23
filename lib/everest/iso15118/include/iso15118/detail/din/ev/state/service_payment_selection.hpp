// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>

#include <iso15118/message_din/service_payment_selection.hpp>

namespace iso15118::din::ev::state {

namespace dt = message_din::datatypes;

namespace service_payment_selection {

message_din::ServicePaymentSelectionRequest create_request(uint16_t charge_service_id);

struct Result {
    bool valid{false};
};

Result handle_response(const message_din::ServicePaymentSelectionResponse& res);

} // namespace service_payment_selection

} // namespace iso15118::din::ev::state
