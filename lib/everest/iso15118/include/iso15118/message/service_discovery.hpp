// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>
#include <vector>

#include "common_types.hpp"

#include <everest/util/vector/fixed_vector.hpp>

namespace iso15118::message_20 {

namespace datatypes {
using ServiceIdList = everest::lib::util::fixed_vector<std::uint16_t, 16>; // 16

struct Service {
    ServiceCategory service_id;
    bool free_service;
};
using ServiceList = everest::lib::util::fixed_vector<Service, 8>; // max: 8

struct VasService {
    uint16_t service_id;
    bool free_service;
};
using VasServiceList = everest::lib::util::fixed_vector<VasService, 8>; // max: 8

} // namespace datatypes

struct ServiceDiscoveryRequest {
    Header header;
    std::optional<datatypes::ServiceIdList> supported_service_ids;
};

struct ServiceDiscoveryResponse {

    Header header;
    datatypes::ResponseCode response_code;
    bool service_renegotiation_supported = false;
    datatypes::ServiceList energy_transfer_service_list = {{
        datatypes::ServiceCategory::AC, // service_id
        false                           // free_service
    }};
    std::optional<datatypes::VasServiceList> vas_list;
};

} // namespace iso15118::message_20
