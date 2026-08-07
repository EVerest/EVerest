// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>
#include <string>

#include "common_types.hpp"

#include <everest/util/vector/fixed_vector.hpp>

namespace iso15118::message_2 {

namespace datatypes {

struct Parameter {
    std::string name;
    std::optional<bool> bool_value;
    std::optional<int8_t> byte_value;
    std::optional<int16_t> short_value;
    std::optional<int32_t> int_value;
    std::optional<PhysicalValue> physical_value;
    std::optional<std::string> string_value;
};

struct ParameterSet {
    int16_t parameter_set_id;
    everest::lib::util::fixed_vector<Parameter, 16> parameter;
};

using ServiceParameterList = everest::lib::util::fixed_vector<ParameterSet, 5>;

} // namespace datatypes

struct ServiceDetailRequest {
    Header header;
    uint16_t service_id;
};

struct ServiceDetailResponse {
    Header header;
    datatypes::ResponseCode response_code;
    uint16_t service_id;
    std::optional<datatypes::ServiceParameterList> service_parameter_list;
};

} // namespace iso15118::message_2
