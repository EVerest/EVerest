// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#pragma once
#include <everest_api_types/entrypoint/API.hpp>
#include <optional>

namespace everest::lib::API::V1_0::types::entrypoint {

std::string serialize(ApiParameter val) noexcept;
std::string serialize(ApiDiscoverResponse val) noexcept;
std::string serialize(CommunicationParameters val) noexcept;

std::ostream& operator<<(std::ostream& os, ApiParameter const& val);
std::ostream& operator<<(std::ostream& os, ApiDiscoverResponse const& val);
std::ostream& operator<<(std::ostream& os, CommunicationParameters const& val);

#include <everest_api_types/utilities/deserialize_templates.inc>

} // namespace everest::lib::API::V1_0::types::entrypoint
