// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#include "entrypoint/json_codec.hpp"
#include "entrypoint/API.hpp"
#include "entrypoint/codec.hpp"

#include "nlohmann/json.hpp"

#include <iostream>

namespace everest::lib::API::V1_0::types::entrypoint {

void to_json(json& j, ModuleAssociations const& k) noexcept {
    j = json{
        {"required_modules", k.required_modules},
    };
}

void from_json(const json& j, ModuleAssociations& k) {
    k.required_modules = j.at("required_modules").get<std::vector<std::string>>();
}

void to_json(json& j, ApiParameter const& k) noexcept {
    j = json{
        {"type", k.type},
        {"module_id", k.module_id},
        {"version", k.version},
    };
    if (k.associated_module) {
        j["associated_module"] = k.associated_module.value();
    }
}

void from_json(const json& j, ApiParameter& k) {
    k.type = j.at("type");
    k.module_id = j.at("module_id");
    k.version = j.at("version");
    if (j.contains("associated_module")) {
        k.associated_module.emplace(j.at("associated_module").get<ModuleAssociations>());
    }
}

void to_json(json& j, ApiDiscoverResponse const& k) noexcept {
    j = json::array();
    for (auto val : k.apis) {
        j.push_back(val);
    }
}

void from_json(const json& j, ApiDiscoverResponse& k) {
    k.apis.clear();
    for (auto val : j) {
        k.apis.push_back(val);
    }
}

void to_json(json& j, ModuleParameter const& k) noexcept {
    j = json{
        {"name", k.name},
        {"module_id", k.module_id},
    };
}

void from_json(const json& j, ModuleParameter& k) {
    k.name = j.at("name");
    k.module_id = j.at("module_id");
}

void to_json(json& j, QueryEVerestConfigurationResponse const& k) noexcept {
    j = json{
        {"modules", k.modules},
        {"version", k.version},
    };
}

void from_json(const json& j, QueryEVerestConfigurationResponse& k) {
    k.modules.clear();
    for (auto val : j.at("modules")) {
        k.modules.push_back(val);
    }
    k.version = j.at("version");
}

void to_json(json& j, EVerestVersion const& k) noexcept {
    j = json{
        {"name", k.name},
        {"version", k.version},
        {"git_version", k.git_version},
    };
}

void from_json(const json& j, EVerestVersion& k) {
    k.name = j.at("name");
    k.version = j.at("version");
    k.git_version = j.at("git_version");
}

} // namespace everest::lib::API::V1_0::types::entrypoint
