// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <utils/config/deprecation.hpp>

#include <utility>

#include <fmt/format.h>

namespace everest::config {

namespace {
constexpr auto LABEL_WIDTH = 15;

std::optional<std::string> get_optional_string(const nlohmann::json& object, const std::string& key) {
    if (not object.contains(key)) {
        return std::nullopt;
    }
    return object.at(key).get<std::string>();
}

void append_field(std::string& text, const std::string_view label, const std::string& value) {
    text += fmt::format("\n  {:<{}} : {}", label, LABEL_WIDTH, value);
}

/// \brief Determines if \p entry_name was configured rather than taken from the manifest default
bool is_configured(const nlohmann::json& config_entry, const std::string& entry_name, const ConfigEntry& value,
                   const std::set<std::string>& defaulted_entries, bool origin_authoritative) {
    if (origin_authoritative) {
        return defaulted_entries.find(entry_name) == defaulted_entries.end();
    }
    // Configs loaded from storage no longer carry whether a value was configured or defaulted. Treating a value that
    // differs from the default as configured never warns about an untouched entry.
    if (not config_entry.contains("default")) {
        return true;
    }
    return nlohmann::json(value) != config_entry.at("default");
}
} // namespace

std::optional<DeprecationNotice> parse_deprecation(const nlohmann::json& owner, const std::string& fallback_component) {
    if (not owner.is_object() or not owner.contains("deprecated")) {
        return std::nullopt;
    }

    const auto& deprecated = owner.at("deprecated");

    DeprecationNotice notice;
    notice.component = deprecated.value("component", fallback_component);
    notice.deprecated_in = deprecated.at("deprecated_in").get<std::string>();
    notice.earliest_removal = deprecated.at("earliest_removal").get<std::string>();
    notice.migration_guide = get_optional_string(deprecated, "migration_guide");
    notice.note = get_optional_string(deprecated, "note");
    return notice;
}

std::string format_deprecation_notice(const DeprecationNotice& notice) {
    std::string text{notice.kind == DeprecationKind::Module ? "DEPRECATED MODULE" : "DEPRECATED CONFIG ENTRY"};

    append_field(text, "component", notice.component);
    if (notice.kind == DeprecationKind::Module) {
        append_field(text, "module id", notice.module_id);
    } else if (notice.implementation_id.has_value()) {
        append_field(text, "set in",
                     fmt::format("{}, implementation '{}'", notice.module_id, notice.implementation_id.value()));
    } else {
        append_field(text, "set in", fmt::format("{}, module config", notice.module_id));
    }
    append_field(text, "deprecated",
                 fmt::format("{}, earliest removal {}", notice.deprecated_in, notice.earliest_removal));
    if (notice.migration_guide.has_value()) {
        append_field(text, "migration guide", notice.migration_guide.value());
    }
    if (notice.note.has_value()) {
        append_field(text, "note", notice.note.value());
    }

    return text;
}

std::optional<DeprecationNotice> collect_module_deprecation(const nlohmann::json& manifest,
                                                            const std::string& module_id,
                                                            const std::string& module_name) {
    auto notice = parse_deprecation(manifest, module_name);
    if (not notice.has_value()) {
        return std::nullopt;
    }

    notice->kind = DeprecationKind::Module;
    notice->module_id = module_id;
    notice->module_name = module_name;
    return notice;
}

std::vector<DeprecationNotice> collect_config_deprecations(
    const nlohmann::json& config_map_schema, const std::vector<ConfigurationParameter>& configuration_parameters,
    const std::set<std::string>& defaulted_entries, bool origin_authoritative, const std::string& module_id,
    const std::string& module_name, const std::optional<std::string>& implementation_id) {
    std::vector<DeprecationNotice> notices;
    if (not config_map_schema.is_object()) {
        return notices;
    }

    for (const auto& parameter : configuration_parameters) {
        if (not config_map_schema.contains(parameter.name)) {
            continue;
        }

        const auto& config_entry = config_map_schema.at(parameter.name);
        auto notice = parse_deprecation(config_entry, fmt::format("{} config entry '{}'", module_name, parameter.name));
        if (not notice.has_value()) {
            continue;
        }

        const auto& deprecated = config_entry.at("deprecated");
        if (deprecated.contains("when") and deprecated.at("when") != nlohmann::json(parameter.value)) {
            continue;
        }
        if (not is_configured(config_entry, parameter.name, parameter.value, defaulted_entries, origin_authoritative)) {
            continue;
        }

        notice->kind = DeprecationKind::ConfigEntry;
        notice->module_id = module_id;
        notice->module_name = module_name;
        notice->implementation_id = implementation_id;
        notice->config_entry = parameter.name;
        notices.push_back(std::move(notice.value()));
    }

    return notices;
}

} // namespace everest::config
