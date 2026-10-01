// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/ocpp_module_common/error_mapping.hpp>

#include <algorithm>

namespace ocpp_module_common {

namespace {

/// \returns the first entry of \p table whose key is contained in \p error_type
template <typename Table> auto find_contained_key(const Table& table, const std::string& error_type) {
    return std::find_if(table.begin(), table.end(),
                        [&error_type](const auto& entry) { return error_type.find(entry.first) != std::string::npos; });
}

} // namespace

ocpp::v16::ErrorInfo make_v16_error_info(const Everest::error::Error& error) {
    ocpp::v16::ErrorInfo result(error.uuid.uuid, ocpp::v16::ChargePointErrorCode::OtherError, false);
    result.timestamp = ocpp::DateTime(error.timestamp);
    return result;
}

ocpp::v2::EventData make_v2_event_data(const Everest::error::Error& error, const bool cleared,
                                       const std::int32_t event_id) {
    ocpp::v2::EventData event_data;
    event_data.eventId = event_id; // This can theoretically conflict with eventIds generated in libocpp (e.g.
                                   // for monitoring events), but the spec does not strictly forbid that
    event_data.timestamp = ocpp::DateTime(error.timestamp);
    event_data.trigger = ocpp::v2::EventTriggerEnum::Alerting;
    event_data.cause = std::nullopt; // TODO: use caused_by when available within error object

    event_data.actualValue = cleared ? "false" : "true";

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

const MrecErrorMapping::Table& MrecErrorMapping::entries() {
    using ocpp::v16::ChargePointErrorCode;
    // built on the first call and kept; a namespace scope table would race with the namespace scope
    // objects that copy it during their own initialisation
    static const Table table = {
        {"connector_lock/MREC1ConnectorLockFailure", {ChargePointErrorCode::ConnectorLockFailure, "CX001"}},
        {"evse_board_support/MREC2GroundFailure", {ChargePointErrorCode::GroundFailure, "CX002"}},
        {"evse_board_support/MREC3HighTemperature", {ChargePointErrorCode::HighTemperature, "CX003"}},
        {"evse_board_support/MREC4OverCurrentFailure", {ChargePointErrorCode::OverCurrentFailure, "CX004"}},
        {"evse_board_support/MREC5OverVoltage", {ChargePointErrorCode::OverVoltage, "CX005"}},
        {"evse_board_support/MREC6UnderVoltage", {ChargePointErrorCode::UnderVoltage, "CX006"}},
        {"evse_board_support/MREC8EmergencyStop", {ChargePointErrorCode::OtherError, "CX008"}},
        {"evse_board_support/MREC10InvalidVehicleMode", {ChargePointErrorCode::OtherError, "CX010"}},
        {"evse_board_support/MREC14PilotFault", {ChargePointErrorCode::OtherError, "CX014"}},
        {"evse_board_support/MREC15PowerLoss", {ChargePointErrorCode::OtherError, "CX015"}},
        {"evse_board_support/MREC17EVSEContactorFault", {ChargePointErrorCode::OtherError, "CX017"}},
        {"evse_board_support/MREC18CableOverTempDerate", {ChargePointErrorCode::OtherError, "CX018"}},
        {"evse_board_support/MREC19CableOverTempStop", {ChargePointErrorCode::OtherError, "CX019"}},
        {"evse_board_support/MREC20PartialInsertion", {ChargePointErrorCode::OtherError, "CX020"}},
        {"evse_board_support/MREC23ProximityFault", {ChargePointErrorCode::OtherError, "CX023"}},
        {"evse_board_support/MREC24ConnectorVoltageHigh", {ChargePointErrorCode::OtherError, "CX024"}},
        {"evse_board_support/MREC25BrokenLatch", {ChargePointErrorCode::OtherError, "CX025"}},
        {"evse_board_support/MREC26CutCable", {ChargePointErrorCode::OtherError, "CX026"}},
        {"evse_manager/MREC4OverCurrentFailure", {ChargePointErrorCode::OverCurrentFailure, "CX004"}},
        {"ac_rcd/MREC2GroundFailure", {ChargePointErrorCode::GroundFailure, "CX002"}},
        {"evse_manager/MREC22ResistanceFault", {ChargePointErrorCode::OtherError, "CX022"}},
        {"evse_manager/MREC11CableCheckFault", {ChargePointErrorCode::OtherError, "CX011"}},
        {"evse_manager/MREC5OverVoltage", {ChargePointErrorCode::OverVoltage, "CX005"}},
    };
    return table;
}

const std::string& MrecErrorMapping::vendor_id() {
    static const std::string id = "https://chargex.inl.gov";
    return id;
}

namespace {

MREC_ERROR_MAP_TYPE build_tech_codes(const MrecErrorMapping::Table& entries) {
    MREC_ERROR_MAP_TYPE result;
    for (const auto& [error_type, entry] : entries) {
        result.emplace(error_type, entry.second);
    }
    return result;
}

} // namespace

const MREC_ERROR_MAP_TYPE& MrecErrorMapping::tech_codes() {
    static const MREC_ERROR_MAP_TYPE codes = build_tech_codes(entries());
    return codes;
}

MrecErrorMapping::MrecErrorMapping() = default;

MrecErrorMapping::MrecErrorMapping(const MREC_ERROR_MAP_TYPE& tech_codes) : m_tech_codes(&tech_codes) {
}

std::optional<ocpp::v16::ErrorInfo> MrecErrorMapping::try_convert(const Everest::error::Error& error) const {
    const auto it = find_contained_key(entries(), error.type);
    if (it == entries().end()) {
        return std::nullopt;
    }

    const auto& [error_code, tech_code] = it->second;
    auto result = make_v16_error_info(error);
    result.error_code = error_code;
    result.vendor_id = vendor_id();
    result.vendor_error_code = ocpp::CiString<50>(tech_code, ocpp::StringTooLarge::Truncate);
    if (!error.message.empty()) {
        result.info = ocpp::CiString<50>(error.message, ocpp::StringTooLarge::Truncate);
    }
    return result;
}

std::optional<ocpp::v2::EventData> MrecErrorMapping::try_convert(const Everest::error::Error& error, const bool cleared,
                                                                 const std::int32_t event_id) const {
    std::optional<std::string> tech_code;
    if (m_tech_codes != nullptr) {
        if (const auto it = m_tech_codes->find(error.type); it != m_tech_codes->end()) {
            tech_code = it->second;
        }
    } else if (const auto it = entries().find(error.type); it != entries().end()) {
        // the error code of the entry describes OCPP 1.6 and is not needed here
        tech_code = it->second.second;
    }

    if (!tech_code.has_value()) {
        return std::nullopt;
    }

    auto event_data = make_v2_event_data(error, cleared, event_id);
    event_data.techCode = tech_code.value();
    return event_data;
}

const OcppErrorMappingV16::Table& OcppErrorMappingV16::entries() {
    // TODO: add other ChargePointErrorCode mappings
    // built on the first call and kept, as in MrecErrorMapping::entries
    static const Table table = {
        {"powermeter/CommunicationFault", ocpp::v16::ChargePointErrorCode::PowerMeterFailure},
    };
    return table;
}

std::optional<ocpp::v16::ErrorInfo> OcppErrorMappingV16::try_convert(const Everest::error::Error& error) const {
    const auto it = find_contained_key(entries(), error.type);
    if (it == entries().end()) {
        return std::nullopt;
    }

    auto result = make_v16_error_info(error);
    result.error_code = it->second;
    result.vendor_id = ocpp::CiString<255>(error.message, ocpp::StringTooLarge::Truncate);
    return result;
}

std::optional<ocpp::v16::ErrorInfo> InoperativeErrorMappingV16::try_convert(const Everest::error::Error& error) const {
    if (error.type != EVSE_MANAGER_INOPERATIVE_ERROR) {
        return std::nullopt;
    }

    auto result = make_v16_error_info(error);
    result.is_fault = true;
    result.info = ocpp::CiString<50>("caused_by:" + error.message, ocpp::StringTooLarge::Truncate);
    result.vendor_id = ocpp::CiString<255>(error.vendor_id, ocpp::StringTooLarge::Truncate);
    result.vendor_error_code = ocpp::CiString<50>(error.description, ocpp::StringTooLarge::Truncate);
    return result;
}

std::optional<ocpp::v16::ErrorInfo> DefaultErrorMappingV16::try_convert(const Everest::error::Error& error) const {
    auto result = make_v16_error_info(error);
    result.is_fault = is_fault(error);
    result.info = ocpp::CiString<50>(error.origin.to_string(), ocpp::StringTooLarge::Truncate);
    result.vendor_id = ocpp::CiString<255>(error.message, ocpp::StringTooLarge::Truncate);
    result.vendor_error_code = ocpp::CiString<50>(vendor_error_code(error), ocpp::StringTooLarge::Truncate);
    return result;
}

bool DefaultErrorMappingV16::is_fault(const Everest::error::Error& error) {
    return false;
}

std::string DefaultErrorMappingV16::vendor_error_code(const Everest::error::Error& error) {
    std::string result;

    const auto npos = error.type.find('/');
    if (npos == std::string::npos) {
        result = error.type;
    } else {
        result = error.type.substr(npos + 1);
    }

    result.push_back('/');
    result += error.sub_type;
    return result;
}

std::optional<ocpp::v2::EventData> DefaultErrorMappingV2X::try_convert(const Everest::error::Error& error,
                                                                       const bool cleared,
                                                                       const std::int32_t event_id) const {
    auto event_data = make_v2_event_data(error, cleared, event_id);
    event_data.techCode = error.type;
    return event_data;
}

} // namespace ocpp_module_common
