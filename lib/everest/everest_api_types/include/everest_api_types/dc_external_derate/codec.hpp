// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#pragma once

#include "API.hpp"
#include <string>
#include <string_view>

namespace everest::lib::API::V1_0::types::dc_external_derate {

std::string serialize(ExternalDerating const& val) noexcept;

std::ostream& operator<<(std::ostream& os, ExternalDerating const& val);

#include <everest_api_types/utilities/deserialize_templates.inc>

} // namespace everest::lib::API::V1_0::types::dc_external_derate
