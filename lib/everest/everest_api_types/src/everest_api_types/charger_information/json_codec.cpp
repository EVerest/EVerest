// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "charger_information/json_codec.hpp"
#include "charger_information/API.hpp"
#include "nlohmann/json.hpp"
#include <optional>
#include <string>

namespace everest::lib::API::V1_0::types::charger_information {

namespace {

void set_optional(json& j, char const* key, std::optional<std::string> const& val) {
    if (val) {
        j[key] = val.value();
    }
}

void get_optional(json const& j, char const* key, std::optional<std::string>& val) {
    if (j.contains(key)) {
        val.emplace(j.at(key));
    }
}

} // namespace

void to_json(json& j, ChargerInformation const& k) noexcept {
    j = json{
        {"vendor", k.vendor},
        {"model", k.model},
    };
    set_optional(j, "chargepoint_serial", k.chargepoint_serial);
    set_optional(j, "chargebox_serial", k.chargebox_serial);
    set_optional(j, "friendly_name", k.friendly_name);
    set_optional(j, "manufacturer", k.manufacturer);
    set_optional(j, "manufacturer_url", k.manufacturer_url);
    set_optional(j, "model_url", k.model_url);
    set_optional(j, "model_number", k.model_number);
    set_optional(j, "model_revision", k.model_revision);
    set_optional(j, "board_revision", k.board_revision);
    set_optional(j, "firmware_version", k.firmware_version);
}

void from_json(const json& j, ChargerInformation& k) {
    k.vendor = j.at("vendor");
    k.model = j.at("model");
    get_optional(j, "chargepoint_serial", k.chargepoint_serial);
    get_optional(j, "chargebox_serial", k.chargebox_serial);
    get_optional(j, "friendly_name", k.friendly_name);
    get_optional(j, "manufacturer", k.manufacturer);
    get_optional(j, "manufacturer_url", k.manufacturer_url);
    get_optional(j, "model_url", k.model_url);
    get_optional(j, "model_number", k.model_number);
    get_optional(j, "model_revision", k.model_revision);
    get_optional(j, "board_revision", k.board_revision);
    get_optional(j, "firmware_version", k.firmware_version);
}

} // namespace everest::lib::API::V1_0::types::charger_information
