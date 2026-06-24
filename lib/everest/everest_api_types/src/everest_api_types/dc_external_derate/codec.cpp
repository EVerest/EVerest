// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "dc_external_derate/codec.hpp"
#include "dc_external_derate/API.hpp"
#include "dc_external_derate/json_codec.hpp"
#include "nlohmann/json.hpp"
#include "utilities/constants.hpp"
#include "utilities/json_codec_helpers.hpp"

namespace everest::lib::API::V1_0::types::dc_external_derate {

std::string serialize(ExternalDerating const& val) noexcept {
    return utilities::dump_json(val);
}

std::ostream& operator<<(std::ostream& os, ExternalDerating const& val) {
    os << serialize(val);
    return os;
}

template <> ExternalDerating deserialize(std::string_view val) {
    return utilities::parse_json<ExternalDerating>(val);
}

} // namespace everest::lib::API::V1_0::types::dc_external_derate
