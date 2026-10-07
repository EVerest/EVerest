// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/ocpp_module_common/custom_error_mapping.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>
#include <tuple>
#include <utility>

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>

namespace ocpp_module_common::custom_error_mapping {

namespace {

using nlohmann::json;

constexpr char SUB_TYPE_SEPARATOR = '#';
constexpr auto SCHEMA_KEY = "$schema";
constexpr auto COMBINATION_DETAIL_PREFIX = "[combination: ";
constexpr auto INVALID_KEY_MESSAGE = "invalid key, expected '<namespace>/<type>' or '<namespace>/<type>#<sub_type>'";

bool is_error_namespace(std::string_view s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isalnum(c) || c == '_'; });
}

bool is_error_name(std::string_view s) {
    return !s.empty() && std::isupper(static_cast<unsigned char>(s.front())) &&
           std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isalnum(c); });
}

std::string first_token(json::json_pointer ptr) {
    std::string token;
    while (!ptr.empty()) {
        token = ptr.back();
        ptr = ptr.parent_pointer();
    }
    return token;
}

std::size_t depth(json::json_pointer ptr) {
    std::size_t result = 0;
    while (!ptr.empty()) {
        ptr = ptr.parent_pointer();
        ++result;
    }
    return result;
}

Finding make_error(std::string entry, std::string pointer, std::string message) {
    return {Finding::Level::Error, std::move(entry), std::move(pointer), std::move(message)};
}

class SchemaErrorCollector : public nlohmann::json_schema::error_handler {
public:
    explicit SchemaErrorCollector(std::vector<Finding>& findings) : m_findings(findings) {
    }

    void error(const json::json_pointer& ptr, const json& instance, const std::string& message) override {
        if (message.rfind(COMBINATION_DETAIL_PREFIX, 0) == 0) {
            // details of a failed anyOf; the summary is reported on its own
            return;
        }
        if (ptr.empty() && instance.is_string()) {
            // propertyNames reports the offending key as the instance at the root
            m_findings.push_back(make_error(instance.get<std::string>(), "", INVALID_KEY_MESSAGE));
            return;
        }
        if (depth(ptr) == 1 && message.find("no subschema has succeeded") != std::string::npos) {
            m_findings.push_back(
                make_error(first_token(ptr), ptr.to_string(), "entry needs at least one of 'v16' or 'v2'"));
            return;
        }
        m_findings.push_back(make_error(first_token(ptr), ptr.to_string(), message));
    }

private:
    std::vector<Finding>& m_findings;
};

const nlohmann::json_schema::json_validator& schema_validator() {
    static const nlohmann::json_schema::json_validator validator{json::parse(error_mapping_schema())};
    return validator;
}

/// \brief Parses \p content and reports every key that appears twice in the same object
std::optional<json> parse_json(std::string_view content, std::vector<Finding>& findings) {
    struct Scope {
        std::set<std::string> keys;
        json::json_pointer pointer;
    };
    std::vector<Scope> scopes;
    std::string last_key;
    std::string entry;

    const json::parser_callback_t on_event = [&](int depth, json::parse_event_t event, json& parsed) {
        switch (event) {
        case json::parse_event_t::object_start:
            scopes.push_back({{}, scopes.empty() ? json::json_pointer{} : scopes.back().pointer / last_key});
            break;
        case json::parse_event_t::object_end:
            scopes.pop_back();
            break;
        case json::parse_event_t::key: {
            last_key = parsed.get<std::string>();
            if (depth == 1) {
                entry = last_key;
            }
            if (!scopes.back().keys.insert(last_key).second) {
                findings.push_back(make_error(entry, (scopes.back().pointer / last_key).to_string(),
                                              "duplicate key '" + last_key + "'"));
            }
            break;
        }
        case json::parse_event_t::array_start:
        case json::parse_event_t::array_end:
        case json::parse_event_t::value:
            break;
        }
        return true;
    };

    try {
        return json::parse(content.begin(), content.end(), on_event);
    } catch (const json::parse_error& e) {
        findings.push_back(make_error("", "", std::string("malformed JSON: ") + e.what()));
        return std::nullopt;
    }
}

std::optional<std::string> optional_string(const json& object, const char* name) {
    const auto it = object.find(name);
    if (it == object.end()) {
        return std::nullopt;
    }
    return it->get<std::string>();
}

V16Identity to_v16(const json& v16, const std::string& entry, std::vector<Finding>& findings) {
    V16Identity identity;
    if (const auto code = optional_string(v16, "error_code"); code.has_value()) {
        try {
            identity.error_code = ocpp::v16::conversions::string_to_charge_point_error_code(code.value());
        } catch (const std::exception&) {
            findings.push_back(make_error(entry, (json::json_pointer{} / entry / "v16" / "error_code").to_string(),
                                          "unknown OCPP 1.6 errorCode"));
        }
    }
    identity.vendor_id = optional_string(v16, "vendor_id");
    identity.vendor_error_code = optional_string(v16, "vendor_error_code");
    identity.info = optional_string(v16, "info");
    return identity;
}

V2Identity to_v2(const json& v2) {
    V2Identity identity;
    identity.tech_code = optional_string(v2, "tech_code");
    identity.tech_info = optional_string(v2, "tech_info");
    identity.component_name = optional_string(v2, "component_name");
    identity.component_instance = optional_string(v2, "component_instance");
    identity.variable_name = optional_string(v2, "variable_name");
    identity.variable_instance = optional_string(v2, "variable_instance");
    if (const auto it = v2.find("severity"); it != v2.end()) {
        identity.severity = it->get<std::int32_t>();
    }
    return identity;
}

std::optional<Entry> to_entry(const std::string& key, const json& value, std::vector<Finding>& findings) {
    auto error_key = parse_error_key(key);
    if (!error_key.has_value()) {
        findings.push_back(make_error(key, "", INVALID_KEY_MESSAGE));
        return std::nullopt;
    }
    if (error_key->type == EVSE_MANAGER_INOPERATIVE_ERROR) {
        findings.push_back(make_error(key, "",
                                      EVSE_MANAGER_INOPERATIVE_ERROR +
                                          " is reported as a fault by the built-in mapping and cannot be mapped"));
        return std::nullopt;
    }
    Entry entry{std::move(error_key.value()), std::nullopt, std::nullopt};
    if (const auto it = value.find("v16"); it != value.end()) {
        entry.v16 = to_v16(*it, key, findings);
    }
    if (const auto it = value.find("v2"); it != value.end()) {
        entry.v2 = to_v2(*it);
    }
    return entry;
}

bool has_file_errors(const std::vector<Finding>& findings) {
    return std::any_of(findings.begin(), findings.end(), [](const Finding& finding) {
        return finding.level == Finding::Level::Error && finding.entry.empty();
    });
}

bool has_entry_errors(const std::vector<Finding>& findings, const std::string& entry) {
    return std::any_of(findings.begin(), findings.end(), [&entry](const Finding& finding) {
        return finding.level == Finding::Level::Error && finding.entry == entry;
    });
}

} // namespace

std::string ErrorKey::to_string() const {
    return sub_type.has_value() ? type + SUB_TYPE_SEPARATOR + sub_type.value() : type;
}

bool operator<(const ErrorKey& lhs, const ErrorKey& rhs) {
    return std::tie(lhs.type, lhs.sub_type) < std::tie(rhs.type, rhs.sub_type);
}

bool operator==(const ErrorKey& lhs, const ErrorKey& rhs) {
    return std::tie(lhs.type, lhs.sub_type) == std::tie(rhs.type, rhs.sub_type);
}

std::optional<ErrorKey> parse_error_key(std::string_view key) {
    const auto separator = key.find(SUB_TYPE_SEPARATOR);
    const auto type = key.substr(0, separator);
    const auto slash = type.find('/');
    if (slash == std::string_view::npos || !is_error_namespace(type.substr(0, slash)) ||
        !is_error_name(type.substr(slash + 1))) {
        return std::nullopt;
    }
    if (separator == std::string_view::npos) {
        return ErrorKey{std::string(type), std::nullopt};
    }
    const auto sub_type = key.substr(separator + 1);
    if (sub_type.empty()) {
        return std::nullopt;
    }
    return ErrorKey{std::string(type), std::string(sub_type)};
}

CustomFileErrorMapping::CustomFileErrorMapping(std::map<ErrorKey, Entry> entries) : m_entries(std::move(entries)) {
}

const Entry* CustomFileErrorMapping::find(const std::string& type, const std::string& sub_type) const {
    if (type == EVSE_MANAGER_INOPERATIVE_ERROR) {
        return nullptr;
    }
    if (!sub_type.empty()) {
        if (const auto it = m_entries.find(ErrorKey{type, sub_type}); it != m_entries.end()) {
            return &it->second;
        }
    }
    const auto it = m_entries.find(ErrorKey{type, std::nullopt});
    return it != m_entries.end() ? &it->second : nullptr;
}

std::shared_ptr<const CustomFileErrorMapping>
CustomFileErrorMapping::without(const std::vector<Finding>& findings) const {
    auto entries = m_entries;
    for (auto it = entries.begin(); it != entries.end();) {
        it = has_entry_errors(findings, it->first.to_string()) ? entries.erase(it) : std::next(it);
    }
    return std::make_shared<const CustomFileErrorMapping>(std::move(entries));
}

const std::map<ErrorKey, Entry>& CustomFileErrorMapping::entries() const {
    return m_entries;
}

std::optional<ocpp::v16::ErrorInfo> CustomFileErrorMapping::try_convert(const Everest::error::Error& error) const {
    const auto* entry = find(error.type, error.sub_type);
    if (entry == nullptr || !entry->v16.has_value()) {
        return std::nullopt;
    }

    auto result = make_v16_error_info(error);
    const auto& v16 = entry->v16.value();
    if (v16.error_code.has_value()) {
        result.error_code = v16.error_code.value();
    }
    if (v16.vendor_id.has_value()) {
        result.vendor_id = ocpp::CiString<255>(v16.vendor_id.value(), ocpp::StringTooLarge::Truncate);
    }
    if (v16.vendor_error_code.has_value()) {
        result.vendor_error_code = ocpp::CiString<50>(v16.vendor_error_code.value(), ocpp::StringTooLarge::Truncate);
    }
    if (v16.info.has_value()) {
        result.info = ocpp::CiString<50>(v16.info.value(), ocpp::StringTooLarge::Truncate);
    }
    return result;
}

std::optional<ocpp::v2::EventData> CustomFileErrorMapping::try_convert(const Everest::error::Error& error,
                                                                       const bool cleared,
                                                                       const std::int32_t event_id) const {
    const auto* entry = find(error.type, error.sub_type);
    if (entry == nullptr || !entry->v2.has_value()) {
        return std::nullopt;
    }

    auto result = make_v2_event_data(error, cleared, event_id);
    const auto& v2 = entry->v2.value();
    if (v2.tech_code.has_value()) {
        result.techCode = ocpp::CiString<50>(v2.tech_code.value(), ocpp::StringTooLarge::Truncate);
    }
    if (v2.tech_info.has_value()) {
        result.techInfo = ocpp::CiString<500>(v2.tech_info.value(), ocpp::StringTooLarge::Truncate);
    }
    if (v2.component_name.has_value()) {
        result.component.name = ocpp::CiString<50>(v2.component_name.value(), ocpp::StringTooLarge::Truncate);
    }
    if (v2.component_instance.has_value()) {
        result.component.instance = ocpp::CiString<50>(v2.component_instance.value(), ocpp::StringTooLarge::Truncate);
    }
    if (v2.variable_name.has_value()) {
        result.variable.name = ocpp::CiString<50>(v2.variable_name.value(), ocpp::StringTooLarge::Truncate);
    }
    if (v2.variable_instance.has_value()) {
        result.variable.instance = ocpp::CiString<50>(v2.variable_instance.value(), ocpp::StringTooLarge::Truncate);
    }
    result.severity = v2.severity;
    return result;
}

std::string Finding::to_string() const {
    std::ostringstream out;
    out << (level == Level::Error ? "error" : "warning");
    if (!entry.empty()) {
        out << " in entry '" << entry << "'";
    }
    if (!pointer.empty()) {
        out << " at " << pointer;
    }
    out << ": " << message;
    return out.str();
}

bool has_errors(const std::vector<Finding>& findings) {
    return std::any_of(findings.begin(), findings.end(),
                       [](const Finding& finding) { return finding.level == Finding::Level::Error; });
}

LoadResult parse_error_mapping(std::string_view content) {
    LoadResult result;
    const auto document = parse_json(content, result.findings);
    if (!document.has_value()) {
        return result;
    }
    if (!document->is_object()) {
        result.findings.push_back(make_error("", "", "the error mapping must be a JSON object"));
        return result;
    }

    SchemaErrorCollector collector{result.findings};
    schema_validator().validate(document.value(), collector);
    if (has_file_errors(result.findings)) {
        return result;
    }

    std::map<ErrorKey, Entry> entries;
    for (const auto& [key, value] : document->items()) {
        if (key == SCHEMA_KEY || has_entry_errors(result.findings, key)) {
            continue;
        }
        auto entry = to_entry(key, value, result.findings);
        if (entry.has_value() && !has_entry_errors(result.findings, key)) {
            auto error_key = entry->key;
            entries.emplace(std::move(error_key), std::move(entry.value()));
        }
    }
    result.error_mapping = std::make_shared<const CustomFileErrorMapping>(std::move(entries));
    return result;
}

LoadResult load_error_mapping(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        LoadResult result;
        result.findings.push_back(make_error("", "", "cannot open error mapping file '" + path.string() + "'"));
        return result;
    }
    std::ostringstream content;
    content << file.rdbuf();
    if (file.bad()) {
        LoadResult result;
        result.findings.push_back(make_error("", "", "cannot read error mapping file '" + path.string() + "'"));
        return result;
    }
    return parse_error_mapping(content.str());
}

} // namespace ocpp_module_common::custom_error_mapping
