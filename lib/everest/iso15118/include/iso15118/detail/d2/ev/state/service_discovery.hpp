// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>

#include <iso15118/message_2/service_discovery.hpp>

namespace iso15118::d2::ev::state {

namespace dt = message_2::datatypes;

namespace service_discovery {

message_2::ServiceDiscoveryRequest create_request();

struct Result {
    bool valid{false};
    bool mode_supported{false};
    // ExternalPayment (EIM) is offered in the payment_option_list.
    bool eim_offered{false};
    uint16_t charge_service_id{0};
};

// Verifies the ChargeService supports the EV-requested energy transfer mode.
Result handle_response(const message_2::ServiceDiscoveryResponse& res, dt::EnergyTransferMode requested_mode);

} // namespace service_discovery

} // namespace iso15118::d2::ev::state
