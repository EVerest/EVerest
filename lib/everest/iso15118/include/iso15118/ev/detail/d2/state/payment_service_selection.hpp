// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>

#include <iso15118/message_2/payment_service_selection.hpp>

namespace iso15118::ev::d2::state {

namespace dt = message_2::datatypes;

namespace payment_service_selection {

// Selects `payment_option` with the charge service. `add_certificate_service` appends the Certificate
// service (ServiceID 2) so the SECC accepts the following CertificateInstallationReq.
message_2::PaymentServiceSelectionRequest create_request(uint16_t charge_service_id, dt::PaymentOption payment_option,
                                                         bool add_certificate_service);

} // namespace payment_service_selection

} // namespace iso15118::ev::d2::state
