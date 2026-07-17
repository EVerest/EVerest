// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "common_types.hpp"

namespace iso15118::message_din {

struct ServiceDiscoveryRequest {
    Header header;
    std::optional<std::string> service_scope;
    std::optional<datatypes::ServiceCategory> service_category;
};

namespace datatypes {

struct ServiceTag {
    uint16_t service_id{0};
    std::optional<std::string> service_name;
    ServiceCategory service_category{ServiceCategory::EVCharging};
    std::optional<std::string> service_scope;
};

struct Service {
    ServiceTag service_tag;
    bool free_service{false};
};

struct ChargeService {
    ServiceTag service_tag;
    bool free_service{false};
    SupportedEnergyTransferMode energy_transfer_type{SupportedEnergyTransferMode::DC_core};
};

} // namespace datatypes

struct ServiceDiscoveryResponse {
    Header header;
    datatypes::ResponseCode response_code;
    std::vector<datatypes::PaymentOption> payment_options;
    datatypes::ChargeService charge_service;
    std::optional<datatypes::Service> service_list;
};

} // namespace iso15118::message_din
