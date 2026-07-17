// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>

#include <iso15118/message_2/payment_service_selection.hpp>

namespace iso15118::d2::ev::state {

namespace dt = message_2::datatypes;

namespace payment_service_selection {

// Selects `payment_option` (ExternalPayment for EIM, Contract for Plug-and-Charge) with the charge
// service. When `add_certificate_service` is set (Contract + a CertificateInstallation/Update is due and
// the SECC offers the Certificate service) the Certificate service (ServiceID 2) is added to the
// selected service list so the SECC accepts the subsequent CertificateInstallationReq.
message_2::PaymentServiceSelectionRequest create_request(uint16_t charge_service_id, dt::PaymentOption payment_option,
                                                         bool add_certificate_service);

struct Result {
    bool valid{false};
};

Result handle_response(const message_2::PaymentServiceSelectionResponse& res);

} // namespace payment_service_selection

} // namespace iso15118::d2::ev::state
