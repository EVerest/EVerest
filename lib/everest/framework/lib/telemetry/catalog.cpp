// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#include <utils/telemetry/catalog.hpp>

#include <stdexcept>

#include <everest/logging.hpp>
#include <everest/utils/yaml_loader.hpp>
#include <fmt/format.h>

namespace everest::telemetry {

namespace {

constexpr auto REF_KEY = "$ref";

bool is_compatible(Kind kind, ValueType value_type) {
    switch (kind) {
    case Kind::Gauge:
    case Kind::Counter:
        return value_type == ValueType::Number or value_type == ValueType::Integer;
    case Kind::State:
        return value_type == ValueType::Boolean or value_type == ValueType::String;
    case Kind::Event:
        return value_type == ValueType::Object;
    }
    return false;
}

struct TypeReference {
    std::string file;
    std::string type_name;
};

std::optional<TypeReference> parse_type_reference(const std::string& ref) {
    const auto separator = ref.find("#/");
    if (ref.empty() or ref.front() != '/' or separator == std::string::npos or separator < 2) {
        return std::nullopt;
    }
    return TypeReference{ref.substr(1, separator - 1), ref.substr(separator + 2)};
}

} // namespace

std::variant<ElementDeclaration, std::string> parse_element_declaration(const std::string& name,
                                                                        const nlohmann::json& declaration) {
    if (name.empty()) {
        return std::string("empty element name");
    }
    if (not declaration.is_object()) {
        return std::string("declaration is not an object");
    }
    const auto kind_it = declaration.find("kind");
    const auto type_it = declaration.find("type");
    if (kind_it == declaration.end() or not kind_it->is_string() or type_it == declaration.end() or
        not type_it->is_string()) {
        return std::string("kind and type are required strings");
    }
    const auto kind = kind_from_string(kind_it->get<std::string>());
    if (not kind.has_value()) {
        return fmt::format("unknown kind '{}'", kind_it->get<std::string>());
    }
    const auto value_type = value_type_from_string(type_it->get<std::string>());
    if (not value_type.has_value()) {
        return fmt::format("unknown type '{}'", type_it->get<std::string>());
    }
    if (not is_compatible(kind.value(), value_type.value())) {
        return fmt::format("kind '{}' cannot have type '{}'", to_string(kind.value()), to_string(value_type.value()));
    }

    ElementDeclaration element;
    element.name = name;
    element.kind = kind.value();
    element.value_type = value_type.value();
    if (const auto it = declaration.find("description"); it != declaration.end() and it->is_string()) {
        element.description = it->get<std::string>();
    }
    if (const auto it = declaration.find("unit"); it != declaration.end()) {
        if (not it->is_string()) {
            return std::string("unit is not a string");
        }
        element.unit = it->get<std::string>();
    }
    if (const auto it = declaration.find("enum"); it != declaration.end()) {
        if (element.kind != Kind::State or element.value_type != ValueType::String or not it->is_array() or
            it->empty()) {
            return std::string("enum values are only allowed as a non-empty list on string states");
        }
        for (const auto& value : *it) {
            if (not value.is_string()) {
                return std::string("enum values must be strings");
            }
            element.enum_values.push_back(value.get<std::string>());
        }
    }
    if (const auto it = declaration.find(REF_KEY); it != declaration.end()) {
        if (not it->is_string()) {
            return std::string("$ref is not a string");
        }
        element.type_ref = it->get<std::string>();
    }
    return element;
}

ElementDeclarations parse_element_declarations(const nlohmann::json& section, std::vector<std::string>& errors) {
    ElementDeclarations elements;
    if (not section.is_object()) {
        return elements;
    }
    for (const auto& [name, declaration] : section.items()) {
        auto parsed = parse_element_declaration(name, declaration);
        if (const auto* error = std::get_if<std::string>(&parsed)) {
            errors.push_back(fmt::format("telemetry element '{}': {}", name, *error));
            continue;
        }
        elements.emplace(name, std::get<ElementDeclaration>(std::move(parsed)));
    }
    return elements;
}

EnumResolver make_types_dir_enum_resolver(const std::filesystem::path& types_dir) {
    return [types_dir](const std::string& ref) -> std::optional<std::vector<std::string>> {
        const auto reference = parse_type_reference(ref);
        if (not reference.has_value()) {
            return std::nullopt;
        }
        try {
            const auto type_file = Everest::load_yaml(types_dir / (reference->file + ".yaml"));
            const auto& type = type_file.at("types").at(reference->type_name);
            if (type.value("type", "") != "string" or not type.contains("enum")) {
                return std::nullopt;
            }
            return type.at("enum").get<std::vector<std::string>>();
        } catch (const std::exception& e) {
            EVLOG_debug << fmt::format("Could not resolve enum reference '{}': {}", ref, e.what());
            return std::nullopt;
        }
    };
}

TelemetryCatalog build_telemetry_catalog(const nlohmann::json& manifests,
                                         const std::map<std::string, std::string, std::less<>>& module_names,
                                         const MappingLookup& module_mapping, const EnumResolver& resolve_enum) {
    TelemetryCatalog catalog;
    for (const auto& [module_id, module_type] : module_names) {
        const auto manifest = manifests.find(module_type);
        if (manifest == manifests.end()) {
            continue;
        }
        std::vector<std::string> errors;
        auto elements = parse_element_declarations(manifest->value("telemetry", nlohmann::json::object()), errors);
        for (const auto& error : errors) {
            EVLOG_warning << fmt::format("Module '{}': {}", module_id, error);
        }
        if (elements.empty()) {
            continue;
        }

        for (auto& [name, element] : elements) {
            if (element.kind != Kind::State or not element.type_ref.has_value()) {
                continue;
            }
            const auto values = resolve_enum ? resolve_enum(element.type_ref.value()) : std::nullopt;
            if (values.has_value()) {
                element.enum_values = values.value();
            } else {
                EVLOG_warning << fmt::format("Telemetry element '{}' of module '{}': cannot resolve enum '{}'", name,
                                             module_id, element.type_ref.value());
            }
        }

        ProducerDeclaration producer;
        producer.module_type = module_type;
        producer.mapping = module_mapping ? module_mapping(module_id) : std::nullopt;
        producer.elements = std::move(elements);
        catalog.emplace(module_id, std::move(producer));
    }
    return catalog;
}

void to_json(nlohmann::json& j, const ElementDeclaration& element) {
    j = {{"kind", to_string(element.kind)},
         {"type", to_string(element.value_type)},
         {"description", element.description}};
    if (element.unit.has_value()) {
        j["unit"] = element.unit.value();
    }
    if (not element.enum_values.empty()) {
        j["enum"] = element.enum_values;
    }
    if (element.type_ref.has_value()) {
        j[REF_KEY] = element.type_ref.value();
    }
}

void to_json(nlohmann::json& j, const ProducerDeclaration& producer) {
    j = {{"module_type", producer.module_type}, {"elements", producer.elements}};
    if (producer.mapping.has_value()) {
        j["mapping"] = producer.mapping.value();
    }
}

void from_json(const nlohmann::json& j, ProducerDeclaration& producer) {
    producer.module_type = j.at("module_type").get<std::string>();
    producer.mapping.reset();
    if (const auto it = j.find("mapping"); it != j.end() and not it->is_null()) {
        producer.mapping = it->get<Mapping>();
    }
    std::vector<std::string> errors;
    producer.elements = parse_element_declarations(j.at("elements"), errors);
    if (not errors.empty()) {
        throw std::invalid_argument(errors.front());
    }
}

} // namespace everest::telemetry
