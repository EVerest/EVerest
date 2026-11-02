// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <string>

namespace ocpp::v16::keys {

/// \brief Render the built-in OCPP 1.6 key to OCPP 2.x Component/Variable mapping table as YAML.
///
/// Every known key is listed in valid_keys order: mapped keys under "mappings" with their target
/// (Actual, or maxLimit for keys mapped to VariableCharacteristics.maxLimit), keys without an
/// OCPP 2.x equivalent under "unmapped_keys".
///
/// The output is a reference document, not a custom mapping file: it does not validate against
/// get_ocpp16_custom_mapping_schema().
std::string default_mappings_to_yaml();

} // namespace ocpp::v16::keys
