// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>

#include <iso15118/message_din/service_discovery.hpp>

namespace iso15118::din::ev::state {

namespace dt = message_din::datatypes;

namespace service_discovery {

message_din::ServiceDiscoveryRequest create_request();

struct Result {
    bool valid{false};
    // The offered ChargeService supports the EV's requested energy transfer type.
    bool charge_service_supported{false};
    // ExternalPayment (EIM) is offered.
    bool eim_offered{false};
    uint16_t charge_service_id{0};
    dt::SupportedEnergyTransferMode offered_energy_transfer_mode{dt::SupportedEnergyTransferMode::DC_extended};
};

Result handle_response(const message_din::ServiceDiscoveryResponse& res, dt::EnergyTransferMode requested);

} // namespace service_discovery

} // namespace iso15118::din::ev::state
