// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#include "everest_api_types/utilities/codec.hpp"
#include "everest_api_types/generic/codec.hpp"

namespace everest::lib::API {

template <> bool deserialize(std::string_view data, bool& obj) {
    return everest::lib::API::V1_0::types::generic::adl_deserialize(data, obj);
}

template <> bool deserialize(std::string_view data, int& obj) {
    return everest::lib::API::V1_0::types::generic::adl_deserialize(data, obj);
}

template <> bool deserialize(std::string_view data, size_t& obj) {
    return everest::lib::API::V1_0::types::generic::adl_deserialize(data, obj);
}

template <> bool deserialize(std::string_view data, double& obj) {
    return everest::lib::API::V1_0::types::generic::adl_deserialize(data, obj);
}

template <> bool deserialize(std::string_view data, float& obj) {
    return everest::lib::API::V1_0::types::generic::adl_deserialize(data, obj);
}

template <> bool deserialize(std::string_view data, std::string& obj) {
    return everest::lib::API::V1_0::types::generic::adl_deserialize(data, obj);
}

} // namespace everest::lib::API
