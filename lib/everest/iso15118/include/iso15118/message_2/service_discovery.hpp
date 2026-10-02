// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>
#include <string>

#include "common_types.hpp"

#include <everest/util/vector/fixed_vector.hpp>

namespace iso15118::message_2 {

namespace datatypes {

using PaymentOptionList = everest::lib::util::fixed_vector<PaymentOption, 2>;

using SupportedEnergyTransferModeList = everest::lib::util::fixed_vector<EnergyTransferMode, 6>;

struct ChargeService {
    uint16_t service_id;
    std::optional<std::string> service_name;
    ServiceCategory service_category;
    std::optional<std::string> service_scope;
    bool free_service;
    SupportedEnergyTransferModeList supported_energy_transfer_mode;
};

struct Service {
    uint16_t service_id;
    std::optional<std::string> service_name;
    ServiceCategory service_category;
    std::optional<std::string> service_scope;
    bool free_service;
};

using ServiceList = everest::lib::util::fixed_vector<Service, 8>;

} // namespace datatypes

struct ServiceDiscoveryRequest {
    Header header;
    std::optional<std::string> service_scope;
    std::optional<datatypes::ServiceCategory> service_category;
};

struct ServiceDiscoveryResponse {
    Header header;
    datatypes::ResponseCode response_code;
    datatypes::PaymentOptionList payment_option_list;
    datatypes::ChargeService charge_service;
    std::optional<datatypes::ServiceList> service_list;
};

} // namespace iso15118::message_2
