// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>

#include <iso15118/message_din/service_discovery.hpp>

namespace iso15118::ev::din::state {

namespace dt = message_din::datatypes;

namespace service_discovery {

message_din::ServiceDiscoveryRequest create_request();

// The response code is validated by expect_response; only the offer is decided here.
struct Result {
    // The offered ChargeService carries the EV's requested energy transfer mode.
    bool charge_service_supported{false};
    // ExternalPayment (EIM) is offered.
    bool eim_offered{false};
    uint16_t charge_service_id{0};
};

Result handle_response(const message_din::ServiceDiscoveryResponse& res, dt::EnergyTransferMode requested);

} // namespace service_discovery

} // namespace iso15118::ev::din::state
