// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <everest/ocpp_module_common/error_handling.hpp>
#include <everest/ocpp_module_common/error_mapping.hpp>

#include <everest/logging.hpp>
#include <nlohmann/json.hpp>

#include <fstream>
#include <stdexcept>

namespace ocpp_module_common {

const MREC_ERROR_MAP_TYPE MREC_ERROR_MAP = MrecErrorMapping::tech_codes();

const std::string EVSE_MANAGER_INOPERATIVE_ERROR = "evse_manager/Inoperative";
const std::string CHARGING_STATION_COMPONENT_NAME = "ChargingStation";
const std::string EVSE_COMPONENT_NAME = "EVSE";
const std::string CONNECTOR_COMPONENT_NAME = "Connector";
const std::string PROBLEM_VARIABLE_NAME = "Problem";

MREC_ERROR_MAP_TYPE load_mrec_error_map_overrides(const std::filesystem::path& file) {
    std::ifstream stream(file);
    if (!stream.is_open()) {
        throw std::runtime_error("Could not open MREC error mapping file: " + file.string());
    }

    nlohmann::json data;
    try {
        stream >> data;
    } catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error("Failed to parse MREC error mapping file " + file.string() + ": " + e.what());
    }

    if (!data.is_object()) {
        throw std::runtime_error("MREC error mapping file must contain a JSON object: " + file.string());
    }

    MREC_ERROR_MAP_TYPE merged = MREC_ERROR_MAP;
    for (auto it = data.begin(); it != data.end(); ++it) {
        if (!it.value().is_string()) {
            throw std::runtime_error("MREC error mapping value for key '" + it.key() + "' must be a string");
        }
        merged[it.key()] = it.value().get<std::string>();
    }

    EVLOG_info << "Loaded " << data.size() << " MREC error mapping override(s) from " << file.string();
    return merged;
}

ocpp::v2::Component get_component_from_error(const Everest::error::Error& error) {
    ocpp::v2::Component component;

    // EVSE 0 is the charging station in the 3-tier mapping; OCPP 2.x EVSE ids start at 1
    if (!error.origin.mapping.has_value() || error.origin.mapping->evse < 1) {
        component.name = CHARGING_STATION_COMPONENT_NAME;
        return component;
    }

    const auto& mapping = error.origin.mapping.value();
    const auto evse_id = mapping.evse;

    if (!mapping.connector.has_value()) {
        ocpp::v2::EVSE evse;
        evse.id = evse_id;
        component.name = EVSE_COMPONENT_NAME;
        component.evse = evse;
        return component;
    }

    ocpp::v2::EVSE evse;
    evse.id = evse_id;
    evse.connectorId = mapping.connector.value();
    component.name = EVSE_COMPONENT_NAME;
    component.evse = evse;
    return component;
}

ocpp::v2::EventData get_event_data(const Everest::error::Error& error, const bool cleared, const int32_t event_id,
                                   const MREC_ERROR_MAP_TYPE& error_map) {
    return to_v2_event_data(error, cleared, event_id, MrecErrorMapping{error_map});
}

} // namespace ocpp_module_common
