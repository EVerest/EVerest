// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <everest/ocpp_module_common/error_mapping.hpp>

#include <ocpp/v16/ocpp_enums.hpp>
#include <utils/config/types.hpp>

namespace ocpp_module_common::custom_error_mapping {

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

struct V2Identity {
    std::optional<std::string> tech_code;
    std::optional<std::string> tech_info;
    std::optional<std::string> component_name;
    std::optional<std::string> component_instance;
    std::optional<std::string> variable_name;
    std::optional<std::string> variable_instance;
    /// OCPP 2.1 severity (0-9) reported for the error; unset reports no severity
    std::optional<std::int32_t> severity;
};

struct Entry {
    ErrorKey key;
    std::optional<V16Identity> v16;
    std::optional<V2Identity> v2;
};

/// \brief Reports errors as the entries of a custom error mapping file describe them.
///
/// evse_manager/Inoperative is never handled: OCPP 1.6 reports a connector as Faulted only through the
/// built-in mapping of that error.
///
/// Converts an error from its entry alone: the fields the entry sets are reported, every other field
/// follows from the error itself. An error without an entry, or whose entry has no section for the
/// asked protocol version, is not handled, so \ref try_convert returns std::nullopt for it.
///
/// \code
/// const CustomFileErrorMapping mapping{load_error_mapping(path).error_mapping};
/// const auto info = mapping.try_convert(error);
/// \endcode
class CustomFileErrorMapping : public ErrorMappingV16, public ErrorMappingV2X {
public:
    CustomFileErrorMapping() = default;
    explicit CustomFileErrorMapping(std::map<ErrorKey, Entry> entries);

    std::optional<ocpp::v16::ErrorInfo> try_convert(const Everest::error::Error& error) const override;
    std::optional<ocpp::v2::EventData> try_convert(const Everest::error::Error& error, bool cleared,
                                                   std::int32_t event_id) const override;

    /// \returns the entry for \p type and \p sub_type, else the entry for \p type alone, else nullptr;
    ///          always nullptr for evse_manager/Inoperative
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
    /// set only when no finding is an error; immutable, so it can be shared by every reader without copies
    std::shared_ptr<const CustomFileErrorMapping> error_mapping;
    std::vector<Finding> findings;
};

/// \returns the error mapping schema (JSON schema draft-07)
const std::string& error_mapping_schema();

/// \brief Parses and validates the mapping file content against the schema. Duplicate keys and entries for
///        evse_manager/Inoperative are errors.
LoadResult parse_error_mapping(std::string_view content);

/// \brief Reads \p path and parses it with parse_error_mapping
LoadResult load_error_mapping(const std::filesystem::path& path);

} // namespace ocpp_module_common::custom_error_mapping
