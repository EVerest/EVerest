// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <ocpp/v16/ocpp_enums.hpp>
#include <utils/config/types.hpp>

namespace ocpp_multi::error_mapping {

/// \brief Key of a mapping entry: an EVerest error type, optionally refined by a sub_type.
///        Written as "<namespace>/<type>" or "<namespace>/<type>#<sub_type>".
struct ErrorKey {
    std::string type;
    std::optional<std::string> sub_type;

    std::string to_string() const;
};

bool operator<(const ErrorKey& lhs, const ErrorKey& rhs);
bool operator==(const ErrorKey& lhs, const ErrorKey& rhs);

/// \returns the key, or nullopt when \p key is not "<namespace>/<type>[#<sub_type>]"
std::optional<ErrorKey> parse_error_key(std::string_view key);

struct V16Identity {
    std::optional<ocpp::v16::ChargePointErrorCode> error_code;
    std::optional<std::string> vendor_id;
    std::optional<std::string> vendor_error_code;
    std::optional<std::string> info;
};

/// \brief OCPP 2.1 severity (0-9) per EVerest severity level
struct SeverityEncoding {
    std::int32_t high;
    std::int32_t medium;
    std::int32_t low;
};

struct V2Identity {
    std::optional<std::string> tech_code;
    std::optional<std::string> tech_info;
    std::optional<std::string> component_name;
    std::optional<std::string> component_instance;
    std::optional<std::string> variable_name;
    std::optional<std::string> variable_instance;
    std::optional<SeverityEncoding> severity;
};

struct Entry {
    ErrorKey key;
    /// EVSE and connector the error is reported on, instead of the raising module's mapping
    std::optional<Mapping> tier_mapping;
    std::optional<V16Identity> v16;
    std::optional<V2Identity> v2;
};

class CustomErrorMapping {
public:
    CustomErrorMapping() = default;
    explicit CustomErrorMapping(std::map<ErrorKey, Entry> entries);

    /// \returns the entry for \p type and \p sub_type, else the entry for \p type alone, else nullptr
    const Entry* find(const std::string& type, const std::string& sub_type) const;
    const std::map<ErrorKey, Entry>& entries() const;

private:
    std::map<ErrorKey, Entry> m_entries;
};

struct Finding {
    enum class Level {
        Error,
        Warning,
    };
    Level level;
    /// key of the entry the finding is about; empty for the file as a whole
    std::string entry;
    /// JSON pointer to the offending value; empty when not applicable
    std::string pointer;
    std::string message;

    std::string to_string() const;
};

bool has_errors(const std::vector<Finding>& findings);

struct LoadResult {
    /// set only when no finding is an error
    std::optional<CustomErrorMapping> error_mapping;
    std::vector<Finding> findings;
};

/// \returns the error mapping schema (JSON schema draft-07)
const std::string& error_mapping_schema();

/// \brief Parses and validates the mapping file content against the schema. Duplicate keys are errors.
LoadResult parse_error_mapping(std::string_view content);

/// \brief Reads \p path and parses it with parse_error_mapping
LoadResult load_error_mapping(const std::filesystem::path& path);

} // namespace ocpp_multi::error_mapping
