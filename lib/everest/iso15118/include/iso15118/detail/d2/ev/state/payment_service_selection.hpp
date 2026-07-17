// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>

#include <iso15118/message_2/payment_service_selection.hpp>

namespace iso15118::d2::ev::state {

namespace dt = message_2::datatypes;

namespace payment_service_selection {

// EIM only: ExternalPayment with the selected charge service (no parameter set id).
message_2::PaymentServiceSelectionRequest create_request(uint16_t charge_service_id);

struct Result {
    bool valid{false};
};

Result handle_response(const message_2::PaymentServiceSelectionResponse& res);

} // namespace payment_service_selection

} // namespace iso15118::d2::ev::state
