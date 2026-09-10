// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>

#include <iso15118/message_2/service_discovery.hpp>

namespace iso15118::ev::d2::state {

namespace dt = message_2::datatypes;

namespace service_discovery {

// service_scope/service_category omitted: request all offered services.
message_2::ServiceDiscoveryRequest create_request();

struct Result {
    bool valid{false};
    bool mode_supported{false};
    // ExternalPayment (EIM) in the payment_option_list.
    bool eim_offered{false};
    // Contract (Plug & Charge) in the payment_option_list.
    bool contract_offered{false};
    // Certificate service (ServiceID 2) in the service_list.
    bool certificate_service_offered{false};
    uint16_t charge_service_id{0};
};

Result handle_response(const message_2::ServiceDiscoveryResponse& res, dt::EnergyTransferMode requested_mode);

} // namespace service_discovery

} // namespace iso15118::ev::d2::state
