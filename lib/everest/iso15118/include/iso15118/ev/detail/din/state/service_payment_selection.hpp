// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>

#include <iso15118/message_din/service_payment_selection.hpp>

namespace iso15118::ev::din::state {

namespace service_payment_selection {

// EIM: ExternalPayment plus the single ChargeService the SECC offered.
message_din::ServicePaymentSelectionRequest create_request(uint16_t charge_service_id);

} // namespace service_payment_selection

} // namespace iso15118::ev::din::state
