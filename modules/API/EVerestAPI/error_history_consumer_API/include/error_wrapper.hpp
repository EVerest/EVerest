// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#pragma once

#include <everest_api_types/error_history/API.hpp>
#include <utils/error.hpp>

namespace error_converter {

namespace API_types_ext = everest::lib::API::V1_0::types::error_history;

API_types_ext::ErrorObject framework_to_external_api(Everest::error::Error const& val);
API_types_ext::Severity framework_to_external_api(Everest::error::Severity const& val);
API_types_ext::Mapping framework_to_external_api(Mapping const& val);
API_types_ext::ImplementationIdentifier framework_to_external_api(ImplementationIdentifier const& val);
API_types_ext::State framework_to_external_api(Everest::error::State const& val);
} // namespace error_converter
