// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef EVEREST_OCPP_MODULE_COMMON_ERROR_HANDLING_HPP
#define EVEREST_OCPP_MODULE_COMMON_ERROR_HANDLING_HPP

#include <ocpp/v2/ocpp_types.hpp>
#include <utils/error.hpp>

#include <filesystem>
#include <string>
#include <unordered_map>

namespace ocpp_module_common {
using MREC_ERROR_MAP_TYPE = std::unordered_map<std::string, std::string>;

/// @brief The default map for MREC_ERROR codes.
extern const MREC_ERROR_MAP_TYPE MREC_ERROR_MAP;
extern const std::string EVSE_MANAGER_INOPERATIVE_ERROR;
extern const std::string CHARGING_STATION_COMPONENT_NAME;
extern const std::string EVSE_COMPONENT_NAME;
extern const std::string CONNECTOR_COMPONENT_NAME;
extern const std::string PROBLEM_VARIABLE_NAME;

/// \brief Loads MREC error mappings from the JSON object at \p file.
/// \throws Throws std::runtime_error if the file cannot be opened, fails to
/// parse, or is not a JSON object of string values.
MREC_ERROR_MAP_TYPE load_mrec_error_map_overrides(const std::filesystem::path& file);

/// \brief Returns simplified mapping from error origin to OCPP component based on evse and connector ids
ocpp::v2::Component get_component_from_error(const Everest::error::Error& error);

/// \brief Derives the EventData from the given \p error, \p cleared and \p event_id parameters. The
/// techCode is looked up in \p error_map, falling back to \c error.type when no entry is present.
ocpp::v2::EventData get_event_data(const Everest::error::Error& error, bool cleared, int32_t event_id,
                                   const MREC_ERROR_MAP_TYPE& error_map);
} // namespace ocpp_module_common

#endif // EVEREST_OCPP_MODULE_COMMON_ERROR_HANDLING_HPP
