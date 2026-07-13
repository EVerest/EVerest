// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "error_handling.hpp"

#include <everest/logging.hpp>
#include <nlohmann/json.hpp>

#include <fstream>
#include <stdexcept>

namespace module {

const MREC_ERROR_MAP_TYPE MREC_ERROR_MAP = {
    {"connector_lock/MREC1ConnectorLockFailure", "CX001"},
    {"evse_board_support/MREC2GroundFailure", "CX002"},
    {"evse_board_support/MREC3HighTemperature", "CX003"},
    {"evse_board_support/MREC4OverCurrentFailure", "CX004"},
    {"evse_board_support/MREC5OverVoltage", "CX005"},
    {"evse_board_support/MREC6UnderVoltage", "CX006"},
    {"evse_board_support/MREC8EmergencyStop", "CX008"},
    {"evse_board_support/MREC10InvalidVehicleMode", "CX010"},
    {"evse_board_support/MREC14PilotFault", "CX014"},
    {"evse_board_support/MREC15PowerLoss", "CX015"},
    {"evse_board_support/MREC17EVSEContactorFault", "CX017"},
    {"evse_board_support/MREC18CableOverTempDerate", "CX018"},
    {"evse_board_support/MREC19CableOverTempStop", "CX019"},
    {"evse_board_support/MREC20PartialInsertion", "CX020"},
    {"evse_board_support/MREC23ProximityFault", "CX023"},
    {"evse_board_support/MREC24ConnectorVoltageHigh", "CX024"},
    {"evse_board_support/MREC25BrokenLatch", "CX025"},
    {"evse_board_support/MREC26CutCable", "CX026"},
    {"evse_manager/MREC4OverCurrentFailure", "CX004"},
    {"ac_rcd/MREC2GroundFailure", "CX002"},
    {"evse_manager/MREC22ResistanceFault", "CX022"},
    {"evse_manager/MREC11CableCheckFault", "CX011"},
    {"evse_manager/MREC5OverVoltage", "CX005"},
};

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

    if (!error.origin.mapping.has_value()) {
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
    ocpp::v2::EventData event_data;
    event_data.eventId = event_id; // This can theoretically conflict with eventIds generated in libocpp (e.g.
                                   // for monitoring events), but the spec does not strictly forbid that
    event_data.timestamp = ocpp::DateTime(error.timestamp);
    event_data.trigger = ocpp::v2::EventTriggerEnum::Alerting;
    event_data.cause = std::nullopt; // TODO: use caused_by when available within error object
    event_data.actualValue = cleared ? "false" : "true";

    if (const auto it = error_map.find(error.type); it != error_map.end()) {
        event_data.techCode = it->second;
    } else {
        event_data.techCode = error.type;
    }

    if (!error.message.empty()) {
        event_data.techInfo = ocpp::CiString<500>(error.message, ocpp::StringTooLarge::Truncate);
    } else {
        event_data.techInfo = ocpp::CiString<500>(error.description, ocpp::StringTooLarge::Truncate);
    }
    event_data.cleared = cleared;
    event_data.transactionId = std::nullopt;        // TODO: Do we need to set this here?
    event_data.variableMonitoringId = std::nullopt; // We dont need to set this for HardwiredNotification
    event_data.eventNotificationType = ocpp::v2::EventNotificationEnum::HardWiredNotification;

    event_data.component = get_component_from_error(error);
    event_data.variable = {PROBLEM_VARIABLE_NAME}; // TODO: use type of error for mapping to variable?
    return event_data;
}

} // namespace module
