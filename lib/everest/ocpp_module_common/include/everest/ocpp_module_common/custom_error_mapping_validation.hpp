// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <everest/ocpp_module_common/custom_error_mapping.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <ocpp/v2/ocpp_types.hpp>
#include <utils/error.hpp>

namespace ocpp_module_common::custom_error_mapping {

/// \brief Number of connectors per EVSE id
using EvseTopology = std::map<std::int32_t, std::int32_t>;

/// \returns the error types declared in the "<namespace>.yaml" files of \p errors_dir, or nullopt if the directory
///          does not exist
std::optional<Everest::error::ErrorTypes> read_declared_error_types(const std::filesystem::path& errors_dir);

/// \returns the error types of the built-in MREC mapping
std::set<std::string> builtin_error_types();

/// \brief Every entry's error type must be declared; its sub_type is an open namespace and is not checked.
std::vector<Finding> validate_error_types(const CustomFileErrorMapping& mapping,
                                          const Everest::error::ErrorTypes& declared);

/// \returns the keys of the entries that replace a built-in entry. A "<type>#<sub_type>" entry refines rather than
///          replaces one and is not listed.
std::vector<std::string> replaced_builtin_entries(const CustomFileErrorMapping& mapping,
                                                  const std::set<std::string>& builtin);

/// \brief Placeholders, text lengths and the mapping's evse/connector combination.
std::vector<Finding> validate_values(const CustomFileErrorMapping& mapping);

/// \brief Every entry mapping must name an EVSE and connector that exist.
std::vector<Finding> validate_topology(const CustomFileErrorMapping& mapping, const EvseTopology& topology);

enum class DeviceModelLookup {
    Known,
    UnknownComponent,
    UnknownVariable,
};

using DeviceModelLookupFunction =
    std::function<DeviceModelLookup(const ocpp::v2::Component& component, const ocpp::v2::Variable& variable)>;

/// \brief Every entry naming an OCPP 2.x component or variable must name a combination the device model contains.
///        Without an entry mapping, any of the charging station, the EVSEs and the connectors may contain it.
/// \param strict report missing combinations as errors instead of warnings
std::vector<Finding> validate_device_model(const CustomFileErrorMapping& mapping,
                                           const DeviceModelLookupFunction& lookup, const EvseTopology& topology,
                                           bool strict);

/// \returns a description when \p entry reports \p error on another EVSE or connector than the raising module's
///          mapping. The raising module is only known once the error arrives, so this cannot be checked at load.
std::optional<std::string> mapping_override(const Entry& entry, const Everest::error::Error& error);

} // namespace ocpp_module_common::custom_error_mapping
