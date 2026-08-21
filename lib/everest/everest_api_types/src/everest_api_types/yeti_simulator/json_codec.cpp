// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "yeti_simulator/json_codec.hpp"
#include "nlohmann/json.hpp"
#include "yeti_simulator/API.hpp"
#include "yeti_simulator/codec.hpp"
#include <stdexcept>
#include <string>

namespace everest::lib::API::V1_0::types::yeti_simulator {

void to_json(json& j, Severity const& k) {
    switch (k) {
    case Severity::Low:
        j = "Low";
        return;
    case Severity::Medium:
        j = "Medium";
        return;
    case Severity::High:
        j = "High";
        return;
    }
    throw std::out_of_range("everest::lib::API::V1_0::types::yeti_simulator::Severity unknown enum value");
}

void from_json(json const& j, Severity& k) {
    std::string s = j;
    if (s == "Low") {
        k = Severity::Low;
        return;
    }
    if (s == "Medium") {
        k = Severity::Medium;
        return;
    }
    if (s == "High") {
        k = Severity::High;
        return;
    }

    throw std::out_of_range(
        "Provided string " + s +
        " could not be converted to enum of type everest::lib::API::V1_0::types::yeti_simulator::Severity");
}

void to_json(json& j, RaiseError const& k) noexcept {
    j = json{{"type", k.type}};
    if (k.sub_type) {
        j["sub_type"] = k.sub_type.value();
    }
    if (k.message) {
        j["message"] = k.message.value();
    }
    if (k.severity) {
        j["severity"] = k.severity.value();
    }
}

void from_json(json const& j, RaiseError& k) {
    k.type = j.at("type");

    if (j.contains("sub_type")) {
        k.sub_type.emplace(j.at("sub_type").get<std::string>());
    }
    if (j.contains("message")) {
        k.message.emplace(j.at("message").get<std::string>());
    }
    if (j.contains("severity")) {
        k.severity.emplace(j.at("severity").get<Severity>());
    }
}

void to_json(json& j, ClearError const& k) noexcept {
    j = json{{"type", k.type}};
    if (k.sub_type) {
        j["sub_type"] = k.sub_type.value();
    }
}

void from_json(json const& j, ClearError& k) {
    k.type = j.at("type");

    if (j.contains("sub_type")) {
        k.sub_type.emplace(j.at("sub_type").get<std::string>());
    }
}

} // namespace everest::lib::API::V1_0::types::yeti_simulator
