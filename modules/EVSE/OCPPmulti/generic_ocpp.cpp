// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "generic_ocpp.hpp"
#include <conversions.hpp>
#include <device_model/composed_device_model_storage.hpp>
#include <error_handling.hpp>
#include <everest/conversions/ocpp/ocpp_conversions.hpp>
#include <everest/external_energy_limits/external_energy_limits.hpp>
#include <everest/logging.hpp>
#include <generated/types/ocpp.hpp>
#include <ocpp/v2/ocpp_types.hpp>
#include <ocpp/v2/utils.hpp>

#include <ld-ev.hpp>
#include <optional>

namespace fs = std::filesystem;

namespace {

std::optional<ocpp::v2::IdToken> get_authorised_id_token(const types::evse_manager::SessionEvent& session_event) {
    using namespace module::conversions;

    std::optional<ocpp::v2::IdToken> result;

    const auto event_type = session_event.event;
    if (event_type == types::evse_manager::SessionEventEnum::SessionStarted) {
        if (!session_event.session_started.has_value()) {
            throw std::runtime_error("SessionEvent SessionStarted does not contain session_started context");
        }
        const auto session_started = session_event.session_started.value();
        if (session_started.id_tag.has_value()) {
            result = to_ocpp_id_token(session_started.id_tag.value().id_token);
        }
    } else if (event_type == types::evse_manager::SessionEventEnum::TransactionStarted) {
        if (!session_event.transaction_started.has_value()) {
            throw std::runtime_error("SessionEvent TransactionStarted does not contain transaction_started context");
        }
        const auto transaction_started = session_event.transaction_started.value();
        result = to_ocpp_id_token(transaction_started.id_tag.id_token);
    }

    return result;
}

std::int32_t get_connector_id_from_error(const Everest::error::Error& error) {
    if (error.origin.mapping.has_value() and error.origin.mapping.value().connector.has_value()) {
        return error.origin.mapping.value().connector.value();
    }
    return 1;
}

types::powermeter::Powermeter get_meter_value(const types::evse_manager::SessionEvent& session_event) {
    types::powermeter::Powermeter result;

    const auto event_type = session_event.event;
    if (event_type == types::evse_manager::SessionEventEnum::SessionStarted) {
        if (!session_event.session_started.has_value()) {
            throw std::runtime_error("SessionEvent SessionStarted does not contain session_started context");
        }
        result = session_event.session_started.value().meter_value;
    } else if (event_type == types::evse_manager::SessionEventEnum::SessionFinished) {
        if (!session_event.session_finished.has_value()) {
            throw std::runtime_error("SessionEvent SessionFinished does not contain session_finished context");
        }
        result = session_event.session_finished.value().meter_value;
    } else if (event_type == types::evse_manager::SessionEventEnum::TransactionStarted) {
        if (!session_event.transaction_started.has_value()) {
            throw std::runtime_error("SessionEvent TransactionStarted does not contain transaction_started context");
        }
        result = session_event.transaction_started.value().meter_value;
    } else if (event_type == types::evse_manager::SessionEventEnum::TransactionFinished) {
        if (!session_event.transaction_finished.has_value()) {
            throw std::runtime_error("SessionEvent TransactionFinished does not contain transaction_finished context");
        }
        result = session_event.transaction_finished.value().meter_value;
    } else if (event_type == types::evse_manager::SessionEventEnum::ChargingStarted or
               event_type == types::evse_manager::SessionEventEnum::ChargingPausedEV or
               event_type == types::evse_manager::SessionEventEnum::ChargingPausedEVSE) {
        if (!session_event.charging_state_changed_event.has_value()) {
            throw std::runtime_error("SessionEvent does not contain charging_state_changed_event context");
        }
        result = session_event.charging_state_changed_event.value().meter_value;
    } else if (event_type == types::evse_manager::SessionEventEnum::Authorized or
               event_type == types::evse_manager::SessionEventEnum::Deauthorized) {
        if (!session_event.authorization_event.has_value()) {
            throw std::runtime_error(
                "SessionEvent Authorized or Deauthorized does not contain authorization_event context");
        }
        result = session_event.authorization_event.value().meter_value;
    } else {
        throw std::runtime_error("Could not retrieve meter value from SessionEvent");
    }

    return result;
}

std::optional<types::units_signed::SignedMeterValue>
get_signed_meter_value(const types::evse_manager::SessionEvent& session_event) {
    std::optional<types::units_signed::SignedMeterValue> result;

    const auto event_type = session_event.event;
    if (event_type == types::evse_manager::SessionEventEnum::SessionStarted) {
        if (!session_event.session_started.has_value()) {
            throw std::runtime_error("SessionEvent SessionStarted does not contain session_started context");
        }
        result = session_event.session_started.value().signed_meter_value;
    } else if (event_type == types::evse_manager::SessionEventEnum::TransactionStarted) {
        if (!session_event.transaction_started.has_value()) {
            throw std::runtime_error("SessionEvent TransactionStarted does not contain transaction_started context");
        }
        result = session_event.transaction_started.value().signed_meter_value;
    } else if (event_type == types::evse_manager::SessionEventEnum::TransactionFinished) {
        if (!session_event.transaction_finished.has_value()) {
            throw std::runtime_error("SessionEvent TransactionFinished does not contain transaction_finished context");
        }
        result = session_event.transaction_finished.value().signed_meter_value;
    }
    return result;
}

module::TxEvent get_tx_event(const ocpp::v2::ReasonEnum reason) {
    auto result{module::TxEvent::NONE};

    switch (reason) {
    case ocpp::v2::ReasonEnum::DeAuthorized:
    case ocpp::v2::ReasonEnum::Remote:
    case ocpp::v2::ReasonEnum::Local:
    case ocpp::v2::ReasonEnum::MasterPass:
    case ocpp::v2::ReasonEnum::StoppedByEV:
    case ocpp::v2::ReasonEnum::ReqEnergyTransferRejected:
        result = module::TxEvent::DEAUTHORIZED;
        break;
    case ocpp::v2::ReasonEnum::EVDisconnected:
        result = module::TxEvent::EV_DISCONNECTED;
        break;
    case ocpp::v2::ReasonEnum::ImmediateReset:
        result = module::TxEvent::IMMEDIATE_RESET;
        break;
    // FIXME(kai): these reasons definitely do not all map to NONE
    case ocpp::v2::ReasonEnum::EmergencyStop:
    case ocpp::v2::ReasonEnum::EnergyLimitReached:
    case ocpp::v2::ReasonEnum::GroundFault:
    case ocpp::v2::ReasonEnum::LocalOutOfCredit:
    case ocpp::v2::ReasonEnum::Other:
    case ocpp::v2::ReasonEnum::OvercurrentFault:
    case ocpp::v2::ReasonEnum::PowerLoss:
    case ocpp::v2::ReasonEnum::PowerQuality:
    case ocpp::v2::ReasonEnum::Reboot:
    case ocpp::v2::ReasonEnum::SOCLimitReached:
    case ocpp::v2::ReasonEnum::TimeLimitReached:
    case ocpp::v2::ReasonEnum::Timeout:
    default:
        break;
    }
    return result;
}

std::set<module::TxStartStopPoint> get_tx_start_stop_points(const std::string& tx_start_stop_point_csl) {
    using namespace module;

    std::set<TxStartStopPoint> tx_start_stop_points;
    std::vector<std::string> csv;
    std::string str;
    std::stringstream ss(tx_start_stop_point_csl);
    while (std::getline(ss, str, ',')) {
        csv.push_back(str);
    }

    for (const auto& tx_start_stop_point : csv) {
        if (tx_start_stop_point == "ParkingBayOccupancy") {
            tx_start_stop_points.insert(TxStartStopPoint::ParkingBayOccupancy);
        } else if (tx_start_stop_point == "EVConnected") {
            tx_start_stop_points.insert(TxStartStopPoint::EVConnected);
        } else if (tx_start_stop_point == "Authorized") {
            tx_start_stop_points.insert(TxStartStopPoint::Authorized);
        } else if (tx_start_stop_point == "PowerPathClosed") {
            tx_start_stop_points.insert(TxStartStopPoint::PowerPathClosed);
        } else if (tx_start_stop_point == "EnergyTransfer") {
            tx_start_stop_points.insert(TxStartStopPoint::EnergyTransfer);
        } else if (tx_start_stop_point == "DataSigned") {
            tx_start_stop_points.insert(TxStartStopPoint::DataSigned);
        } else {
            // default to PowerPathClosed for now
            tx_start_stop_points.insert(TxStartStopPoint::PowerPathClosed);
        }
    }
    return tx_start_stop_points;
}

ocpp::v2::ChargingRateUnitEnum get_unit_or_default(const std::string& unit_string) {
    try {
        return ocpp::v2::conversions::string_to_charging_rate_unit_enum(unit_string);
    } catch (const std::out_of_range& e) {
        EVLOG_warning << "RequestCompositeScheduleUnit configured incorrectly with: " << unit_string
                      << ". Defaulting to using Amps.";
        return ocpp::v2::ChargingRateUnitEnum::A;
    }
}

std::string ocpp_protocol_version_to_string(const ocpp::OcppProtocolVersion ocpp_protocol_version) {
    switch (ocpp_protocol_version) {
    case ocpp::OcppProtocolVersion::v16:
        return "1.6";
    case ocpp::OcppProtocolVersion::v201:
        return "2.0.1";
    case ocpp::OcppProtocolVersion::v21:
        return "2.1";
    case ocpp::OcppProtocolVersion::Unknown:
    default:
        break;
    }
    return "Unknown";
}

ocpp::v2::TriggerReasonEnum stop_reason_to_trigger_reason_enum(const ocpp::v2::ReasonEnum& stop_reason) {
    switch (stop_reason) {
    case ocpp::v2::ReasonEnum::DeAuthorized:
        return ocpp::v2::TriggerReasonEnum::Deauthorized;
    case ocpp::v2::ReasonEnum::EmergencyStop:
        return ocpp::v2::TriggerReasonEnum::AbnormalCondition;
    case ocpp::v2::ReasonEnum::EnergyLimitReached:
        return ocpp::v2::TriggerReasonEnum::EnergyLimitReached;
    case ocpp::v2::ReasonEnum::EVDisconnected:
        return ocpp::v2::TriggerReasonEnum::EVCommunicationLost;
    case ocpp::v2::ReasonEnum::GroundFault:
        return ocpp::v2::TriggerReasonEnum::AbnormalCondition;
    case ocpp::v2::ReasonEnum::ImmediateReset:
        return ocpp::v2::TriggerReasonEnum::ResetCommand;
    case ocpp::v2::ReasonEnum::Local:
        return ocpp::v2::TriggerReasonEnum::StopAuthorized;
    case ocpp::v2::ReasonEnum::LocalOutOfCredit:
        return ocpp::v2::TriggerReasonEnum::ChargingStateChanged;
    case ocpp::v2::ReasonEnum::MasterPass:
        return ocpp::v2::TriggerReasonEnum::StopAuthorized;
    case ocpp::v2::ReasonEnum::PowerQuality:
        return ocpp::v2::TriggerReasonEnum::AbnormalCondition;
    case ocpp::v2::ReasonEnum::Reboot:
        return ocpp::v2::TriggerReasonEnum::ResetCommand;
    case ocpp::v2::ReasonEnum::Remote:
        return ocpp::v2::TriggerReasonEnum::RemoteStop;
    case ocpp::v2::ReasonEnum::SOCLimitReached:
        return ocpp::v2::TriggerReasonEnum::EnergyLimitReached;
    case ocpp::v2::ReasonEnum::StoppedByEV:
        return ocpp::v2::TriggerReasonEnum::StopAuthorized;
    case ocpp::v2::ReasonEnum::TimeLimitReached:
        return ocpp::v2::TriggerReasonEnum::TimeLimitReached;
    case ocpp::v2::ReasonEnum::Timeout:
        return ocpp::v2::TriggerReasonEnum::EVConnectTimeout;
    case ocpp::v2::ReasonEnum::Other:
    case ocpp::v2::ReasonEnum::ReqEnergyTransferRejected:
    case ocpp::v2::ReasonEnum::OvercurrentFault:
    case ocpp::v2::ReasonEnum::PowerLoss:
    default:
        return ocpp::v2::TriggerReasonEnum::AbnormalCondition;
    }
}

types::ocpp::KeyValue to_everest(const ocpp::v16::KeyValue& key_value) {
    types::ocpp::KeyValue _key_value;
    _key_value.key = key_value.key.get();
    _key_value.read_only = key_value.readonly;
    if (key_value.value.has_value()) {
        _key_value.value = key_value.value.value().get();
    }
    return _key_value;
}

types::ocpp::ConfigurationStatus to_everest(const ocpp::v16::ConfigurationStatus status) {
    switch (status) {
    case ocpp::v16::ConfigurationStatus::Accepted:
        return types::ocpp::ConfigurationStatus::Accepted;
    case ocpp::v16::ConfigurationStatus::Rejected:
        return types::ocpp::ConfigurationStatus::Rejected;
    case ocpp::v16::ConfigurationStatus::RebootRequired:
        return types::ocpp::ConfigurationStatus::RebootRequired;
    case ocpp::v16::ConfigurationStatus::NotSupported:
        return types::ocpp::ConfigurationStatus::NotSupported;
    default:
        EVLOG_warning << "Could not convert to ConfigurationStatus";
        return types::ocpp::ConfigurationStatus::Rejected;
    }
}

types::ocpp::GetConfigurationResponse to_everest(const ocpp::v16::GetConfigurationResponse& response) {
    types::ocpp::GetConfigurationResponse _response;
    std::vector<types::ocpp::KeyValue> configuration_keys;
    std::vector<std::string> unknown_keys;

    if (response.configurationKey.has_value()) {
        for (const auto& item : response.configurationKey.value()) {
            configuration_keys.push_back(to_everest(item));
        }
    }

    if (response.unknownKey.has_value()) {
        for (const auto& item : response.unknownKey.value()) {
            unknown_keys.push_back(item.get());
        }
    }

    _response.configuration_keys = configuration_keys;
    _response.unknown_keys = unknown_keys;
    return _response;
}

void update_evcc_id_token(ocpp::v2::IdToken& id_token, const std::string& evcc_id,
                          const ocpp::OcppProtocolVersion ocpp_protocol_version) {
    if (ocpp_protocol_version == ocpp::OcppProtocolVersion::v21) {
        auto info_vector = id_token.additionalInfo.has_value() ? id_token.additionalInfo.value()
                                                               : std::vector<ocpp::v2::AdditionalInfo>{};
        if (!id_token.additionalInfo.has_value() or
            (id_token.additionalInfo.has_value() and
             std::find_if(id_token.additionalInfo->cbegin(), id_token.additionalInfo->cend(),
                          [evcc_id](const ocpp::v2::AdditionalInfo& info) {
                              return info.additionalIdToken.get() == evcc_id;
                          }) == id_token.additionalInfo->cend())) {
            ocpp::v2::AdditionalInfo info;
            info.additionalIdToken = evcc_id;
            info.type = "EVCCID";
            info_vector.push_back(info);
            id_token.additionalInfo = info_vector;
        }
    }
}

constexpr const auto SQL_CORE_MIGRATIONS = "core_migrations";

// OCPP 2.0.1 specific configuration variable names
constexpr const auto CENTRAL_CONTRACT_VALIDATION_ALLOWED_VAR_NAME = "CentralContractValidationAllowed";
constexpr const auto CONTRACT_CERTIFICATE_INSTALLATION_ENABLED_VAR_NAME = "ContractCertificateInstallationEnabled";
constexpr const auto EV_CONNECTION_TIMEOUT_VAR_NAME = "EVConnectionTimeOut";
constexpr const auto MASTER_PASS_GROUP_ID_VAR_NAME = "MasterPassGroupId";
constexpr const auto PNC_ENABLED_VAR_NAME = "PnCEnabled";
constexpr const auto SETPOINT_PRIORITY_VAR_NAME = "SetpointPriority";
constexpr const auto SETPOINT_SOURCE = "OCPP";
constexpr const auto TX_START_POINT_VAR_NAME = "TxStartPoint";
constexpr const auto TX_STOP_POINT_VAR_NAME = "TxStopPoint";

constexpr std::int32_t LOWEST_SETPOINT_PRIORITY = 1000;
constexpr std::int32_t HIGHEST_SETPOINT_PRIORITY = 0;

} // namespace

namespace ocpp_multi {

// ----------------------------------------------------------------------------
// GenericOcpp

types::authorization::ValidationResult
GenericOcpp::handle_validate_token(const types::authorization::ProvidedIdToken& provided_token) {
    using namespace module::conversions;

    types::authorization::ValidationResult validation_result;

    if (m_started) {
        try {
            const auto id_token = to_ocpp_id_token(provided_token.id_token);
            std::optional<ocpp::CiString<10000>> certificate_opt;
            if (provided_token.certificate.has_value()) {
                certificate_opt.emplace(provided_token.certificate.value());
            }
            std::optional<std::vector<ocpp::v2::OCSPRequestData>> ocsp_request_data_opt;
            if (provided_token.iso15118CertificateHashData.has_value()) {
                ocsp_request_data_opt =
                    to_ocpp_ocsp_request_data_vector(provided_token.iso15118CertificateHashData.value());
            }

            // request response
            const auto response = m_charge_point.validate_token(id_token, certificate_opt, ocsp_request_data_opt);
            validation_result = to_everest_validation_result(response);

            // Publish tariff message on the session_cost interface
            if (!validation_result.tariff_messages.empty()) {
                types::session_cost::TariffMessage tariff_message;
                tariff_message.messages = validation_result.tariff_messages;
                tariff_message.identifier_id = provided_token.id_token.value;
                tariff_message.identifier_type = types::display_message::IdentifierType::IdToken;
                m_provides.session_cost.publish_tariff_message(tariff_message);
            }
        } catch (const ocpp::StringConversionException& e) {
            EVLOG_warning << "Error converting id token to validate: " << e.what();
            validation_result.authorization_status = types::authorization::AuthorizationStatus::Unknown;
        } catch (const std::exception& e) {
            EVLOG_warning << "Unknown error during validation of id token: " << e.what();
            validation_result.authorization_status = types::authorization::AuthorizationStatus::Unknown;
        }
    } else {
        EVLOG_warning << "ChargePoint not initialized, cannot handle validate token command";
        validation_result.authorization_status = types::authorization::AuthorizationStatus::Unknown;
    }

    return validation_result;
}

types::ocpp::DataTransferResponse GenericOcpp::handle_data_transfer(const types::ocpp::DataTransferRequest& request) {
    using namespace module::conversions;

    types::ocpp::DataTransferResponse response{};

    if (m_started) {
        ocpp::v2::DataTransferRequest ocpp_request = to_ocpp_data_transfer_request(request);
        auto ocpp_response = m_charge_point.data_transfer_req(ocpp_request);

        if (ocpp_response.has_value()) {
            response = to_everest_data_transfer_response(ocpp_response.value());
        } else {
            response.status = types::ocpp::DataTransferStatus::Offline;
        }
    } else {
        EVLOG_warning << "ChargePoint not initialized, cannot data transfer command";
        response.status = types::ocpp::DataTransferStatus::Offline;
    }

    return response;
}

bool GenericOcpp::handle_stop() {
    // Disconnects the websocket connection and stops the OCPP communication.
    // No OCPP messages will be stored and sent after a restart.

    bool result{false};
    if (m_started) {
        std::lock_guard lock(m_chargepoint_state_mutex);
        charging_schedules_timer_stop();
        m_charge_point.stop();
        result = true;
    } else {
        EVLOG_warning << "ChargePoint not initialized, cannot handle stop command";
    }
    return result;
}

bool GenericOcpp::handle_restart() {
    // Connects the websocket and enables OCPP communication after a previous
    // stop call.

    bool result{false};
    if (m_started) {
        std::lock_guard lock(m_chargepoint_state_mutex);
        charging_schedules_timer_start();
        m_charge_point.start(ocpp::v2::BootReasonEnum::ApplicationReset, true);
        result = true;
    } else {
        EVLOG_warning << "ChargePoint not initialized, cannot handle restart command";
    }
    return result;
}

void GenericOcpp::handle_security_event(types::ocpp::SecurityEvent& event) {
    if (m_started) {
        std::optional<ocpp::DateTime> timestamp;
        if (event.timestamp.has_value()) {
            timestamp = ocpp_conversions::to_ocpp_datetime_or_now(event.timestamp.value());
        }
        m_charge_point.on_security_event(event.type, event.info, event.critical, timestamp);
    } else {
        EVLOG_warning << "ChargePoint not yet initialized. Cannot handle security event.";
    }
}

std::vector<types::ocpp::GetVariableResult>
GenericOcpp::handle_get_variables(std::vector<types::ocpp::GetVariableRequest>& requests) {
    using namespace module::conversions;

    std::vector<types::ocpp::GetVariableResult> results;

    if (m_started) {
        const auto _requests = to_ocpp_get_variable_data_vector(requests);
        const auto response = m_charge_point.get_variables(_requests);
        results = to_everest_get_variable_result_vector(response);
    } else {
        EVLOG_warning << "ChargePoint not yet initialized. Cannot handle get variables request.";
        for (const auto& req : requests) {
            types::ocpp::GetVariableResult result;
            result.status = types::ocpp::GetVariableStatusEnumType::Rejected;
            result.component_variable.component = req.component_variable.component;
            result.component_variable.variable = req.component_variable.variable;
            result.attribute_type = req.attribute_type;
            results.push_back(result);
        }
    }

    return results;
}

std::vector<types::ocpp::SetVariableResult>
GenericOcpp::handle_set_variables(std::vector<types::ocpp::SetVariableRequest>& requests, std::string& source) {
    using namespace module::conversions;

    std::vector<types::ocpp::SetVariableResult> results;

    if (m_started) {
        const auto _requests = to_ocpp_set_variable_data_vector(requests);
        const auto response_map = m_charge_point.set_variables(_requests, source);
        std::vector<ocpp::v2::SetVariableResult> response(response_map.size());
        for (const auto& [set_variable_data, set_variable_result] : response_map) {
            response.push_back(set_variable_result);
        }
        results = to_everest_set_variable_result_vector(response);
    } else {
        EVLOG_warning << "ChargePoint not yet initialized. Cannot handle set variables request.";
        for (const auto& req : requests) {
            types::ocpp::SetVariableResult result;
            result.status = types::ocpp::SetVariableStatusEnumType::Rejected;
            result.component_variable.component = req.component_variable.component;
            result.component_variable.variable = req.component_variable.variable;
            result.attribute_type = req.attribute_type;
            results.push_back(result);
        }
    }

    return results;
}

types::ocpp::ChangeAvailabilityResponse
GenericOcpp::handle_change_availability(types::ocpp::ChangeAvailabilityRequest& request) {
    using namespace module::conversions;
    using ChangeAvailabilityStatusEnum = ocpp::v2::ChangeAvailabilityStatusEnum;

    ocpp::v2::ChangeAvailabilityResponse result;
    result.status = ChangeAvailabilityStatusEnum::Rejected;

    if (m_started) {
        const auto ocpp_request = to_ocpp_change_availability_request(request);
        m_charge_point.on_change_availability(ocpp_request);
    } else {
        EVLOG_warning << "ChargePoint not initialized, cannot handle change availability command";
    }

    return to_everest_change_availability_response(result);
}

void GenericOcpp::handle_monitor_variables(std::vector<types::ocpp::ComponentVariable>& component_variables) {
    using namespace module::conversions;

    if (m_started) {
        std::lock_guard lock(m_monitor_list_mutex);

        if (m_monitor_list.empty()) {
            // register a handler
            m_charge_point.register_variable_listener(
                [this](auto&, const auto& component, const auto& variable, auto&, auto&, auto&,
                       const std::string& value) { cb_variable_changed(component, variable, value); });
        }

        // add variables to monitor list
        for (const auto& cv : component_variables) {
            // failures to insert are likely to be the same variable being
            // requested again
            (void)m_monitor_list.emplace(to_ocpp_component(cv.component), to_ocpp_variable(cv.variable));
        }
    } else {
        EVLOG_warning << "ChargePoint not initialized, cannot handle monitor variables command";
    }
}

types::ocpp::GetConfigurationResponse GenericOcpp::handle_get_configuration_key(Array& keys) {
    types::ocpp::GetConfigurationResponse response;

    if (m_started) {
        ocpp::v16::GetConfigurationRequest request;
        request.key = std::vector<ocpp::CiString<50>>();
        for (const auto& key : keys) {
            request.key.value().push_back(key);
        }
        const auto ret = m_charge_point.get_configuration_key(request);
        response = to_everest(ret);
    } else {
        EVLOG_warning << "ChargePoint not initialized, cannot handle get configuration key command";
        types::ocpp::GetConfigurationResponse response;
        for (const auto& key : keys) {
            response.unknown_keys.push_back(key);
        }
    }

    return response;
}

types::ocpp::ConfigurationStatus GenericOcpp::handle_set_configuration_key(std::string& key, std::string& value) {
    auto result = types::ocpp::ConfigurationStatus::Rejected;
    if (m_started) {
        const auto ret = m_charge_point.set_configuration_key(key, value);
        result = to_everest(ret);
    } else {
        EVLOG_warning << "ChargePoint not initialized, cannot handle set configuration key command";
    }

    return result;
}

void GenericOcpp::handle_monitor_configuration_keys(Array& keys) {
    using namespace module::conversions;

    if (m_started) {
        std::lock_guard lock(m_monitor_list_mutex);

        if (m_monitor_list_v16.empty()) {
            // register a handler
            m_charge_point.register_variable_listener(
                [this](auto&, const auto&, const auto& variable, auto&, auto&, auto&, const std::string& value) {
                    cb_variable_changed_v16(variable, value);
                });
        }

        // add variables to monitor list
        for (const auto& key : keys) {
            // failures to insert are likely to be the same variable being
            // requested again
            (void)m_monitor_list_v16.emplace(ocpp::v2::Component{}, ocpp::v2::Variable{key});
        }
    } else {
        EVLOG_warning << "ChargePoint not initialized, cannot handle monitor configuration keys command";
    }
}

void GenericOcpp::handle_security_event(std::string& type, std::string& info) {
    m_charge_point.on_security_event(type, info, std::nullopt, std::nullopt);
}

// ----------------------------------------------------------------------------
// startup

void GenericOcpp::init() {
    // was originally in ready()
    const auto log_path = m_config.getMessageLogPath();
    if (!fs::exists(log_path)) {
        try {
            fs::create_directory(log_path);
        } catch (const fs::filesystem_error& ex) {
            EVLOG_AND_THROW(ex);
        }
    }

    init_check_energy_sink();
    init_evse_maps();
    init_subscribe();
    init_evse_subscribe();
}

void GenericOcpp::ready(const ConfigServiceClient& client) {
    using namespace module::conversions;

    wait_all_ready();
    auto v2callbacks = ready_ocppv2_callbacks();
    std::map<std::int32_t, std::int32_t> evse_connector_structure = get_connector_structure();

    // initialise libocpp device model
    auto libocpp_device_model_storage = std::make_shared<ocpp::v2::DeviceModelStorageSqlite>(
        device_model_database_path(), device_model_database_migration_path(), device_model_config_path());

    // initialise everest device model
    m_everest_device_model_storage = std::make_shared<module::device_model::EverestDeviceModelStorage>(
        m_requires.evse_manager, m_requires.extensions_15118, m_evse_hardware_capabilities_map,
        m_evse_supported_energy_transfer_modes, m_evse_service_renegotiation_supported,
        everest_device_model_database_path(), device_model_database_migration_path(), client);

    // initialise composed device model, this will be provided to the ChargePoint constructor
    auto composed_device_model_storage = std::make_unique<module::device_model::ComposedDeviceModelStorage>();

    // register both device model storages
    composed_device_model_storage->register_device_model_storage("OCPP", std::move(libocpp_device_model_storage));
    composed_device_model_storage->register_device_model_storage("EVEREST", m_everest_device_model_storage);

    const auto sql_init_path = (m_info.paths.share / SQL_CORE_MIGRATIONS).string();
    const auto ocpp_share_path = m_info.paths.share.string();

    m_charge_point.init(std::move(evse_connector_structure), std::move(composed_device_model_storage), ocpp_share_path,
                        m_config.getCoreDatabasePath(), sql_init_path, m_config.getMessageLogPath(),
                        std::move(v2callbacks));

    // publish charging schedules at least once on startup
    cb_charging_schedules_timer();
    charging_schedules_timer_start();

    ready_module_configuration();
    ready_transaction_handler();

    const auto boot_reason = to_ocpp_boot_reason(m_requires.system.call_get_boot_reason());
    m_charge_point.set_message_queue_resume_delay(std::chrono::seconds(m_config.getMessageQueueResumeDelay()));
    // we can now initialise the charge point's state machine. It reads the connector availability from the internal
    // database and potentially triggers enable/disable callbacks at the evse.
    m_charge_point.start(boot_reason, false);
    m_started = true;

    // Signal to EVSEs to start their internal state machines
    for (const auto& evse : m_requires.evse_manager) {
        evse->call_external_ready_to_start_charging();
    }

    // wait for potential events from the evses in order to start OCPP with the correct initial state (e.g. EV might
    // be plugged in at startup)
    std::this_thread::sleep_for(std::chrono::milliseconds(m_config.getDelayOcppStart()));
    // start OCPP connection
    m_charge_point.connect_websocket();

    // process any queued events
    ready_event_queue();
}

void GenericOcpp::init_check_energy_sink() {
    // ensure all evse_energy_sink(s) that are connected have an evse id mapping
    for (const auto& evse_sink : m_requires.evse_energy_sink) {
        if (not evse_sink->get_mapping().has_value()) {
            EVLOG_critical << "Please configure an evse mapping in your configuration file for the connected "
                              "r_evse_energy_sink with module_id: "
                           << evse_sink->module_id;
            throw std::runtime_error("At least one connected evse_energy_sink misses a mapping to an evse.");
        }
    }
}

void GenericOcpp::init_evse_maps() {
    const auto n_managers = m_requires.evse_manager.size();

    {
        auto ready_handle = m_evse_ready_map.handle();
        for (std::size_t evse_id = 1; evse_id <= n_managers; evse_id++) {
            (*ready_handle)[evse_id] = false;
        }
    }
    {
        auto soc_handle = m_evse_soc_map.handle();
        for (std::size_t evse_id = 1; evse_id <= n_managers; evse_id++) {
            (*soc_handle)[evse_id] = std::nullopt;
        }
    }
    {
        auto evse_evcc_id_handle = m_evse_evcc_id.handle();
        for (std::size_t evse_id = 1; evse_id <= n_managers; evse_id++) {
            (*evse_evcc_id_handle)[evse_id] = "";
        }
    }

    for (std::size_t evse_id = 1; evse_id <= n_managers; evse_id++) {
        m_evse_hardware_capabilities_map[evse_id] = types::evse_board_support::HardwareCapabilities{};
        m_evse_supported_energy_transfer_modes[evse_id] = {};
        m_evse_service_renegotiation_supported[evse_id] = false;
    }
}

void GenericOcpp::init_subscribe() {
    m_requires.system.subscribe_firmware_update_status([this](auto arg) { cb_firmware_update_status(arg); });
    m_requires.system.subscribe_log_status([this](auto arg) { cb_log_status(arg); });

    if (!m_requires.reservation.empty() && m_requires.reservation.at(0) != nullptr) {
        m_requires.reservation.at(0)->subscribe_reservation_update([this](auto arg) { cb_reservation_update(arg); });
    }
}

void GenericOcpp::init_evse_subscribe() {
    using namespace module;

    int evse_id = 0;
    for (const auto& evse : m_requires.evse_manager) {
        // IDs start at 1
        evse_id++;

        evse->subscribe_waiting_for_external_ready(
            [this, evse_id](bool ready) { cb_waiting_for_external_ready(evse_id, ready); });
        evse->subscribe_ready([this, evse_id](bool ready) { cb_ready(evse_id, ready); });
        evse->subscribe_hw_capabilities(
            [this, evse_id](const auto& hw_capabilities) { cb_hw_capabilities(evse_id, hw_capabilities); });
        evse->subscribe_supported_energy_transfer_modes([this, evse_id](const auto& supported_energy_transfer_modes) {
            cb_supported_energy_transfer_modes(evse_id, supported_energy_transfer_modes);
        });
        evse->subscribe_session_event(
            [this, evse_id](const auto& session_event) { cb_session_event(evse_id, session_event); });
        evse->subscribe_powermeter([this, evse_id](const auto& power_meter) { cb_powermeter(evse_id, power_meter); });
        evse->subscribe_ev_info([this, evse_id](const auto& ev_info) { cb_ev_info(evse_id, ev_info); });
        auto fault_handler = [this, evse_id](const auto& error) { cb_fault_handler(evse_id, error); };
        auto fault_cleared_handler = [this, evse_id](const auto& error) { cb_fault_cleared_handler(evse_id, error); };
        // A permanent fault from the evse requirement indicates that the evse should move to faulted state
        evse->subscribe_error(EVSE_MANAGER_INOPERATIVE_ERROR, fault_handler, fault_cleared_handler);
    }

    std::int32_t extensions_id = 0;
    for (const auto& extension : m_requires.extensions_15118) {
        extension->subscribe_iso15118_certificate_request([this, extensions_id](const auto& certificate_request) {
            cb_iso15118_certificate_request(extensions_id, certificate_request);
        });
        extension->subscribe_charging_needs(
            [this, extensions_id](const auto& charging_needs) { cb_charging_needs(extensions_id, charging_needs); });
        extension->subscribe_service_renegotiation_supported(
            [this, extensions_id](bool service_renegotiation_supported) {
                cb_service_renegotiation_supported(extensions_id, service_renegotiation_supported);
            });

        extensions_id++;
    }
}

void GenericOcpp::ready_event_queue() {
    using namespace module;
    using namespace module::conversions;

    // process event queue
    for (auto& [evse_id, evse_event_queue] : m_event_queue) {
        while (!evse_event_queue.empty()) {
            auto queued_event = evse_event_queue.front();
            if (std::holds_alternative<types::evse_manager::SessionEvent>(queued_event)) {
                const auto session_event = std::get<types::evse_manager::SessionEvent>(queued_event);
                EVLOG_info << "Processing queued event for evse_id: " << evse_id << ", event: " << session_event.event;
                process_session_event(evse_id, session_event);
            } else if (std::holds_alternative<Everest::error::Error>(queued_event)) {
                const auto& error = std::get<Everest::error::Error>(queued_event);
                EVLOG_info << "Processing queued error event for evse_id: " << evse_id << ": " << error.type;
                bool is_active = error.state == Everest::error::State::Active;
                const auto event_data = get_event_data(error, !is_active, m_event_id_counter++);
                m_charge_point.on_event({event_data});

                // We do only report inoperative errors as faults
                if (error.type == EVSE_MANAGER_INOPERATIVE_ERROR) {
                    if (is_active) {
                        m_charge_point.on_faulted(evse_id, get_connector_id_from_error(error));
                    } else {
                        m_charge_point.on_fault_cleared(evse_id, get_connector_id_from_error(error));
                    }
                }
            } else if (std::holds_alternative<ocpp::v2::MeterValue>(queued_event)) {
                const auto meter_value = std::get<ocpp::v2::MeterValue>(queued_event);
                EVLOG_info << "Processing queued meter value for evse_id: " << evse_id;
                m_charge_point.on_meter_value(evse_id, meter_value);
            } else if (std::holds_alternative<types::system::FirmwareUpdateStatus>(queued_event)) {
                const auto fw_update_status = std::get<types::system::FirmwareUpdateStatus>(queued_event);
                EVLOG_info << "Processing queued firmware update status";
                m_charge_point.on_firmware_update_status_notification(
                    fw_update_status.request_id, to_ocpp_firmware_status_enum(fw_update_status.firmware_update_status));
            } else if (std::holds_alternative<types::system::LogStatus>(queued_event)) {
                const auto log_status = std::get<types::system::LogStatus>(queued_event);
                EVLOG_info << "Processing queued log status";
                m_charge_point.on_log_status_notification(to_ocpp_upload_logs_status_enum(log_status.log_status),
                                                          log_status.request_id);
            } else {
                EVLOG_warning << "Unknown event type in queue for evse_id: " << evse_id;
            }
            evse_event_queue.pop();
        }
    }

    m_event_queue.clear();
}

void GenericOcpp::ready_module_configuration() {
    const auto ev_connection_timeout =
        m_charge_point.get_int32(ocpp::v2::ControllerComponents::TxCtrlr,
                                 ocpp::v2::Variable{EV_CONNECTION_TIMEOUT_VAR_NAME}, ocpp::v2::AttributeEnum::Actual);
    if (ev_connection_timeout) {
        m_requires.auth.call_set_connection_timeout(ev_connection_timeout.value());
    }

    const auto master_pass_group_id =
        m_charge_point.get_string(ocpp::v2::ControllerComponents::AuthCtrlr,
                                  ocpp::v2::Variable{MASTER_PASS_GROUP_ID_VAR_NAME}, ocpp::v2::AttributeEnum::Actual);
    if (master_pass_group_id) {
        m_requires.auth.call_set_master_pass_group_id(master_pass_group_id.value());
    }

    types::evse_manager::PlugAndChargeConfiguration pnc_config;

    const auto iso15118_pnc_enabled =
        m_charge_point.get_bool(ocpp::v2::ControllerComponents::ISO15118Ctrlr, ocpp::v2::Variable{PNC_ENABLED_VAR_NAME},
                                ocpp::v2::AttributeEnum::Actual);
    pnc_config.pnc_enabled = iso15118_pnc_enabled;

    const auto central_contract_validation_allowed = m_charge_point.get_bool(
        ocpp::v2::ControllerComponents::ISO15118Ctrlr, ocpp::v2::Variable{CENTRAL_CONTRACT_VALIDATION_ALLOWED_VAR_NAME},
        ocpp::v2::AttributeEnum::Actual);
    pnc_config.central_contract_validation_allowed = central_contract_validation_allowed;

    const auto contract_certificate_installation_enabled = m_charge_point.get_bool(
        ocpp::v2::ControllerComponents::ISO15118Ctrlr,
        ocpp::v2::Variable{CONTRACT_CERTIFICATE_INSTALLATION_ENABLED_VAR_NAME}, ocpp::v2::AttributeEnum::Actual);
    pnc_config.contract_certificate_installation_enabled = contract_certificate_installation_enabled;

    for (const auto& evse_manager : m_requires.evse_manager) {
        evse_manager->call_set_plug_and_charge_configuration(pnc_config);
    }
}

ocpp::v2::Callbacks GenericOcpp::ready_ocppv2_callbacks() {
    ocpp::v2::Callbacks callbacks;
    callbacks.is_reset_allowed_callback = [this](const auto& evse_id, auto) { return cb_is_reset_allowed(evse_id); };
    callbacks.reset_callback = [this](const auto& evse_id, auto type) { cb_reset(evse_id, type); };
    callbacks.connector_effective_operative_status_changed_callback = [this](auto evse_id, auto connector_id,
                                                                             auto new_status) {
        cb_connector_effective_operative_status(evse_id, connector_id, new_status);
    };
    callbacks.remote_start_transaction_callback = [this](const auto& request, const bool authorize_remote_start) {
        return cb_remote_start_transaction(request, authorize_remote_start);
    };
    callbacks.stop_transaction_callback = [this](auto evse_id, auto stop_reason) {
        return cb_stop_transaction(evse_id, stop_reason);
    };
    callbacks.pause_charging_callback = [this](auto evse_id) { cb_pause_charging(evse_id); };
    callbacks.unlock_connector_callback = [this](auto evse_id, auto connector_id) {
        return cb_unlock_connector(evse_id, connector_id);
    };
    callbacks.get_log_request_callback = [this](const auto& request) { return cb_get_log_request(request); };
    callbacks.is_reservation_for_token_callback = [this](auto evse_id, const auto& idToken, const auto& groupIdToken) {
        return cb_is_reservation_for_token(evse_id, idToken, groupIdToken);
    };
    callbacks.update_firmware_request_callback = [this](const auto& request) {
        return cb_update_firmware_request(request);
    };
    callbacks.variable_changed_callback = [this](const auto& set_variable_data) {
        cb_variable_changed(set_variable_data);
    };
    callbacks.validate_network_profile_callback = [this](auto /* configuration_slot */,
                                                         const auto& network_connection_profile) {
        return cb_validate_network_profile(network_connection_profile);
    };
    callbacks.configure_network_connection_profile_callback = [this](auto /* configuration_slot */,
                                                                     const auto& /* network_connection_profile */) {
        return cb_configure_network_connection_profile();
    };
    callbacks.all_connectors_unavailable_callback = [this]() { cb_all_connectors_unavailable(); };
    callbacks.transaction_event_callback = [this](const auto& transaction_event) {
        cb_transaction_event(transaction_event);
    };
    callbacks.transaction_event_response_callback = [this](const auto& transaction_event,
                                                           const auto& transaction_event_response) {
        cb_transaction_event_response(transaction_event, transaction_event_response);
    };
    callbacks.boot_notification_callback = [this](const auto& boot_notification_response) {
        cb_boot_notification(boot_notification_response);
    };
    callbacks.set_display_message_callback = [this](const auto& messages) { return cb_set_display_message(messages); };
    callbacks.get_display_message_callback = [this](const auto& request) { return cb_get_display_message(request); };
    callbacks.clear_display_message_callback = [this](const auto& request) {
        return cb_clear_display_message(request);
    };
    callbacks.set_running_cost_callback = [this](const auto& running_cost, auto number_of_decimals,
                                                 const auto& currency_code) {
        cb_set_running_cost(running_cost, number_of_decimals, currency_code);
    };
    callbacks.tariff_message_callback = [this](const auto& message) { cb_tariff_message(message); };
    callbacks.default_price_callback = [this](const auto& messages) { cb_default_price(messages); };
    if (!m_requires.data_transfer.empty()) {
        callbacks.data_transfer_callback = [this](const auto& request) { return cb_data_transfer(request); };
    }
    callbacks.connection_state_changed_callback =
        [this](auto is_connected, auto /*configuration_slot*/, const auto& /*network_connection_profile*/,
               auto protocol_version) { cb_connection_state_changed(is_connected, protocol_version); };
    callbacks.security_event_callback = [this](const auto& event_type, const auto& tech_info) {
        cb_security_event(event_type, tech_info);
    };

    // this callback publishes the schedules within EVerest and applies the schedules for the individual
    // evse_energy_sink
    callbacks.set_charging_profiles_callback = [this]() { cb_charging_schedules_timer(); };
    callbacks.time_sync_callback = [this](const auto& current_time) { cb_time_sync(current_time); };
    callbacks.reserve_now_callback = [this](const auto& request) { return cb_reserve_now(request); };
    callbacks.cancel_reservation_callback = [this](auto reservation_id) {
        return cb_cancel_reservation(reservation_id);
    };
    callbacks.update_allowed_energy_transfer_modes_callback = [this](const auto& allowed_energy_transfer_modes,
                                                                     const auto& transaction_id) {
        return cb_update_allowed_energy_transfer_modes(allowed_energy_transfer_modes, transaction_id);
    };
    callbacks.ocpp_messages_callback = [this](const auto& message, auto direction) {
        cb_ocpp_messages(message, direction);
    };
    return callbacks;
}

void GenericOcpp::ready_transaction_handler() {
    std::set<module::TxStartStopPoint> tx_start_points;
    std::set<module::TxStartStopPoint> tx_stop_points;

    const auto tx_start_point_request_value = m_charge_point.get_string(
        ocpp::v2::Component{"TxCtrlr"}, ocpp::v2::Variable{TX_START_POINT_VAR_NAME}, ocpp::v2::AttributeEnum::Actual);
    if (tx_start_point_request_value) {
        auto tx_start_point_csl =
            tx_start_point_request_value.value(); // contains comma seperated list of TxStartPoints
        tx_start_points = get_tx_start_stop_points(tx_start_point_csl);
        EVLOG_info << "TxStartPoints from device model: " << tx_start_point_csl;
    }

    if (tx_start_points.empty()) {
        tx_start_points = {module::TxStartStopPoint::PowerPathClosed};
    }

    const auto tx_stop_point_request_value = m_charge_point.get_string(
        ocpp::v2::Component{"TxCtrlr"}, ocpp::v2::Variable{TX_STOP_POINT_VAR_NAME}, ocpp::v2::AttributeEnum::Actual);
    if (tx_stop_point_request_value) {
        auto tx_stop_point_csl = tx_stop_point_request_value.value(); // contains comma seperated list of TxStartPoints
        tx_stop_points = get_tx_start_stop_points(tx_stop_point_csl);
        EVLOG_info << "TxStopPoints from device model: " << tx_stop_point_csl;
    }

    if (tx_stop_points.empty()) {
        tx_stop_points = {module::TxStartStopPoint::EVConnected, module::TxStartStopPoint::Authorized};
    }

    m_transaction_handler =
        std::make_unique<module::TransactionHandler>(m_requires.evse_manager.size(), tx_start_points, tx_stop_points);
}

// ----------------------------------------------------------------------------
// callbacks

void GenericOcpp::cb_all_connectors_unavailable() {
    EVLOG_info << "All connectors unavailable, proceed with firmware installation";
    m_requires.system.call_allow_firmware_installation();
}

void GenericOcpp::cb_boot_notification(const ocpp::v2::BootNotificationResponse& boot_notification_response) {
    using namespace module::conversions;

    const auto everest_boot_notification_response = to_everest_boot_notification_response(boot_notification_response);
    m_provides.ocpp_generic.publish_boot_notification_response(everest_boot_notification_response);
}

bool GenericOcpp::cb_cancel_reservation(std::int32_t reservation_id) {
    EVLOG_debug << "Received cancel reservation request for reservation id " << reservation_id;

    bool result{false};
    if (!m_requires.reservation.empty() && m_requires.reservation.at(0) != nullptr) {
        result = m_requires.reservation.at(0)->call_cancel_reservation(reservation_id);
    }
    return result;
}

void GenericOcpp::cb_charging_needs(std::int32_t extensions_id, const types::iso15118::ChargingNeeds& charging_needs) {
    using namespace module::conversions;

    if (m_started) {
        const auto& mapping = m_requires.extensions_15118.at(extensions_id)->get_mapping();
        if (mapping.has_value()) {
            try {
                ocpp::v2::NotifyEVChargingNeedsRequest charge_needs;
                charge_needs.chargingNeeds = to_ocpp_charging_needs(charging_needs);
                charge_needs.evseId = mapping.value().evse;

                m_charge_point.on_ev_charging_needs(charge_needs);
            } catch (const std::out_of_range& e) {
                EVLOG_warning << "Could not convert iso15118 ChargingNeeds to OCPP NotifyEVChargingNeedsRequest: "
                              << e.what();
            }
        } else {
            EVLOG_warning << "ISO15118 Extension interface mapping not set! Not sending 'ChargingNeeds'!";
        }
    } else {
        EVLOG_info << "ISO15118 charging_needs received before OCPP was initialised, ignoring";
    }
}

void GenericOcpp::cb_charging_schedules_timer() {
    // this callback publishes the schedules within EVerest and applies the schedules for the individual
    // evse_energy_sink
    const auto composite_schedule_unit = get_unit_or_default(m_config.getRequestCompositeScheduleUnit());
    const auto composite_schedules = m_charge_point.get_all_composite_schedules(
        m_config.getRequestCompositeScheduleDurationS(), composite_schedule_unit);
    publish_charging_schedules(composite_schedules);
    set_external_limits(composite_schedules);
}

ocpp::v2::ClearDisplayMessageResponse
GenericOcpp::cb_clear_display_message(const ocpp::v2::ClearDisplayMessageRequest& request) {
    using namespace module::conversions;

    ocpp::v2::ClearDisplayMessageResponse result;

    if (m_requires.display_message.empty()) {
        result.status = ocpp::v2::ClearMessageStatusEnum::Unknown;
    } else {
        types::display_message::ClearDisplayMessageResponse response =
            m_requires.display_message.at(0)->call_clear_display_message(
                to_everest_clear_display_message_request(request));
        result = to_ocpp_clear_display_message_response(response);
    }

    return result;
}

std::future<ocpp::ConfigNetworkResult> GenericOcpp::cb_configure_network_connection_profile() {
    std::promise<ocpp::ConfigNetworkResult> promise;
    std::future<ocpp::ConfigNetworkResult> future = promise.get_future();
    ocpp::ConfigNetworkResult result;
    result.success = true;
    promise.set_value(result);
    return future;
}

void GenericOcpp::cb_connector_effective_operative_status(std::int32_t evse_id, std::int32_t connector_id,
                                                          ocpp::v2::OperationalStatusEnum new_status) {
    auto& evse = m_requires.evse_manager.at(evse_id - 1);

    if (new_status == ocpp::v2::OperationalStatusEnum::Operative) {
        if (evse->call_enable_disable(connector_id, {types::evse_manager::Enable_source::CSMS,
                                                     types::evse_manager::Enable_state::Enable, 5000})) {
            m_charge_point.on_enabled(evse_id, connector_id);
        }
    } else {
        if (evse->call_enable_disable(connector_id, {types::evse_manager::Enable_source::CSMS,
                                                     types::evse_manager::Enable_state::Disable, 5000})) {
            m_charge_point.on_unavailable(evse_id, connector_id);
        }
    }
}

void GenericOcpp::cb_connection_state_changed(bool is_connected, ocpp::OcppProtocolVersion protocol_version) {
    if (is_connected) {
        m_ocpp_protocol_version = protocol_version;
    } else {
        m_ocpp_protocol_version = ocpp::OcppProtocolVersion::Unknown;
    }
    m_provides.ocpp_generic.publish_is_connected(is_connected);
}

ocpp::v2::DataTransferResponse GenericOcpp::cb_data_transfer(const ocpp::v2::DataTransferRequest& request) {
    using namespace module::conversions;

    types::ocpp::DataTransferRequest data_transfer_request = to_everest_data_transfer_request(request);
    types::ocpp::DataTransferResponse data_transfer_response =
        m_requires.data_transfer.at(0)->call_data_transfer(data_transfer_request);
    ocpp::v2::DataTransferResponse response = to_ocpp_data_transfer_response(data_transfer_response);
    return response;
}

void GenericOcpp::cb_default_price(const std::vector<ocpp::DisplayMessageContent>& messages) {
    const types::session_cost::DefaultPrice p = ocpp_conversions::to_everest_default_price(messages);
    m_provides.session_cost.publish_default_price(p);
}

void GenericOcpp::cb_error_cleared_handler(const Everest::error::Error& error) {
    using namespace module;

    // handled by specific evse_manager error handler
    if (error.type != EVSE_MANAGER_INOPERATIVE_ERROR) {
        if (m_started) {
            const auto event_data = get_event_data(error, true, m_event_id_counter++);
            m_charge_point.on_event({event_data});
        } else {
            std::scoped_lock lock(m_session_event_mutex);
            m_event_queue[get_component_from_error(error).evse.value_or(ocpp::v2::EVSE{0}).id].emplace(error);
        }
    }
}

void GenericOcpp::cb_error_handler(const Everest::error::Error& error) {
    using namespace module;

    // handled by specific evse_manager error handler
    if (error.type != EVSE_MANAGER_INOPERATIVE_ERROR) {
        if (m_started) {
            const auto event_data = get_event_data(error, false, m_event_id_counter++);
            m_charge_point.on_event({event_data});
        } else {
            std::scoped_lock lock(m_session_event_mutex);
            m_event_queue[get_component_from_error(error).evse.value_or(ocpp::v2::EVSE{0}).id].emplace(error);
        }
    }
}

void GenericOcpp::cb_ev_info(std::int32_t evse_id, const types::evse_manager::EVInfo& ev_info) {
    if (m_started) {
        if (ev_info.soc.has_value()) {
            m_evse_soc_map.handle()->at(evse_id) = ev_info.soc;
        }
        if (ev_info.evcc_id.has_value()) {
            m_evse_evcc_id.handle()->at(evse_id) = ev_info.evcc_id.value();
            m_everest_device_model_storage->update_connected_ev_vehicle_id(evse_id, ev_info.evcc_id.value());
        }
    } else {
        EVLOG_info << "EV Info received from evse_manager before DM was instantiated, ignoring...";
        EVLOG_info << "EV Info will be retrieved later";
    }
}

void GenericOcpp::cb_fault_cleared_handler(std::int32_t evse_id, const Everest::error::Error& error) {
    using namespace module;

    if (m_started) {
        const auto event_data = get_event_data(error, true, m_event_id_counter++);
        m_charge_point.on_event({event_data});
        m_charge_point.on_fault_cleared(evse_id, get_connector_id_from_error(error));
    } else {
        std::scoped_lock lock(m_session_event_mutex);
        m_event_queue[get_component_from_error(error).evse.value_or(ocpp::v2::EVSE{1}).id].emplace(error);
    }
}

void GenericOcpp::cb_fault_handler(std::int32_t evse_id, const Everest::error::Error& error) {
    using namespace module;

    if (m_started) {
        const auto event_data = get_event_data(error, false, m_event_id_counter++);
        m_charge_point.on_event({event_data});
        m_charge_point.on_faulted(evse_id, get_connector_id_from_error(error));
    } else {
        std::scoped_lock lock(m_session_event_mutex);
        m_event_queue[get_component_from_error(error).evse.value_or(ocpp::v2::EVSE{1}).id].emplace(error);
    }
}

void GenericOcpp::cb_firmware_update_status(types::system::FirmwareUpdateStatus status) {
    using namespace module::conversions;

    if (m_started) {
        m_charge_point.on_firmware_update_status_notification(
            status.request_id, to_ocpp_firmware_status_enum(status.firmware_update_status));
    } else {
        std::scoped_lock lock(m_session_event_mutex);
        m_event_queue[0].emplace(status);
    }
}

std::vector<ocpp::DisplayMessage>
GenericOcpp::cb_get_display_message(const ocpp::v2::GetDisplayMessagesRequest& request) {
    using namespace module::conversions;

    std::vector<ocpp::DisplayMessage> result;

    if (!m_requires.display_message.empty()) {
        types::display_message::GetDisplayMessageRequest get_request;

        types::display_message::GetDisplayMessageResponse response =
            m_requires.display_message.at(0)->call_get_display_messages(to_everest_display_message_request(request));

        if (response.messages.has_value() && !response.messages.value().empty()) {
            for (const auto& message : response.messages.value()) {
                result.push_back(ocpp_conversions::to_ocpp_display_message(message));
            }
        }
    }
    return result;
}

ocpp::v2::GetLogResponse GenericOcpp::cb_get_log_request(const ocpp::v2::GetLogRequest& request) {
    using namespace module::conversions;

    auto req = to_everest_upload_logs_request(request);
    if (req.retries.has_value()) {
        req.retries = req.retries.value() + 1;
    }
    const auto response = m_requires.system.call_upload_logs(req);
    return to_ocpp_get_log_response(response);
}

void GenericOcpp::cb_hw_capabilities(std::int32_t evse_id,
                                     const types::evse_board_support::HardwareCapabilities& hw_capabilities) {
    m_evse_hardware_capabilities_map[evse_id] = hw_capabilities;
}

ocpp::ReservationCheckStatus
GenericOcpp::cb_is_reservation_for_token(std::int32_t evse_id, const ocpp::CiString<255>& idToken,
                                         const std::optional<ocpp::CiString<255>>& groupIdToken) {
    auto reservation_status = types::reservation::ReservationCheckStatus::NotReserved;

    if (!m_requires.reservation.empty() && m_requires.reservation.at(0) != nullptr) {
        types::reservation::ReservationCheck reservation_check_request;
        reservation_check_request.evse_id = evse_id;
        reservation_check_request.id_token = idToken.get();
        if (groupIdToken.has_value()) {
            reservation_check_request.group_id_token = groupIdToken.value().get();
        }

        reservation_status = m_requires.reservation.at(0)->call_exists_reservation(reservation_check_request);
    }
    return ocpp_conversions::to_ocpp_reservation_check_status(reservation_status);
}

bool GenericOcpp::cb_is_reset_allowed(const std::optional<std::int32_t>& evse_id) {
    // Reset of EVSE is currently not supported

    bool result{false};
    if (!evse_id.has_value()) {
        // not EVSE reset
        try {
            result = m_requires.system.call_is_reset_allowed(types::system::ResetType::NotSpecified);
        } catch (std::out_of_range& e) {
            EVLOG_warning << "Could not convert OCPP ResetEnum to EVerest ResetType while executing "
                             "is_reset_allowed_callback.";
        }
    }
    return result;
}

void GenericOcpp::cb_iso15118_certificate_request(std::int32_t extensions_id,
                                                  const types::iso15118::RequestExiStreamSchema& certificate_request) {
    using namespace module::conversions;

    if (m_started) {
        auto ocpp_response = m_charge_point.on_get_15118_ev_certificate_request(
            to_ocpp_get_15118_certificate_request(certificate_request));
        EVLOG_debug << "Received response from get_15118_ev_certificate_request: " << ocpp_response;
        // transform response, inject action, send to associated EvseManager
        types::iso15118::ResponseExiStreamStatus everest_response;
        everest_response.status = to_everest_iso15118_status(ocpp_response.status);
        everest_response.certificate_action = certificate_request.certificate_action;
        if (not ocpp_response.exiResponse.get().empty()) {
            // since exi_response is an optional in the EVerest type we only set it when not empty
            everest_response.exi_response = ocpp_response.exiResponse.get();
        }

        m_requires.extensions_15118.at(extensions_id)->call_set_get_certificate_response(everest_response);
    } else {
        EVLOG_info << "ISO15118 certificate_request received before OCPP was initialised, ignoring";
    }
}

void GenericOcpp::cb_log_status(types::system::LogStatus status) {
    using namespace module::conversions;

    if (m_started) {
        m_charge_point.on_log_status_notification(to_ocpp_upload_logs_status_enum(status.log_status),
                                                  status.request_id);
    } else {
        std::scoped_lock lock(m_session_event_mutex);
        m_event_queue[0].emplace(status);
    }
}

void GenericOcpp::cb_ocpp_messages(const std::string& message, ocpp::MessageDirection direction) {
    switch (direction) {
    case ocpp::MessageDirection::CSMSToChargingStation: {
        types::ocpp::Message ocpp_message;
        ocpp_message.message = message;
        ocpp_message.version = ocpp_protocol_version_to_string(m_ocpp_protocol_version);
        ocpp_message.direction = types::ocpp::MessageDirection::CSMSToChargingStation;
        m_provides.ocpp_generic.publish_ocpp_message(ocpp_message);
        break;
    }
    case ocpp::MessageDirection::ChargingStationToCSMS: {
        types::ocpp::Message ocpp_message;
        ocpp_message.message = message;
        ocpp_message.version = ocpp_protocol_version_to_string(m_ocpp_protocol_version);
        ocpp_message.direction = types::ocpp::MessageDirection::ChargingStationToCSMS;
        m_provides.ocpp_generic.publish_ocpp_message(ocpp_message);
        break;
    }
    default:
        // unknown message direction (ignored)
        break;
    }
}

void GenericOcpp::cb_pause_charging(std::int32_t evse_id) {
    if (evse_id > 0 && evse_id <= m_requires.evse_manager.size()) {
        m_requires.evse_manager.at(evse_id - 1)->call_pause_charging();
    }
}

void GenericOcpp::cb_powermeter(std::int32_t evse_id, const types::powermeter::Powermeter& power_meter) {
    using namespace module::conversions;

    ocpp::v2::MeterValue meter_value =
        to_ocpp_meter_value(power_meter, ocpp::v2::ReadingContextEnum::Sample_Periodic, power_meter.signed_meter_value);
    if (m_started) {
        auto evse_soc_map_handle = m_evse_soc_map.handle();
        if (evse_soc_map_handle->at(evse_id).has_value()) {
            auto sampled_soc_value =
                to_ocpp_sampled_value(ocpp::v2::ReadingContextEnum::Sample_Periodic, ocpp::v2::MeasurandEnum::SoC,
                                      "Percent", std::nullopt, ocpp::v2::LocationEnum::EV);
            sampled_soc_value.value = evse_soc_map_handle->at(evse_id).value();
            meter_value.sampledValue.push_back(sampled_soc_value);
        }
        m_charge_point.on_meter_value(evse_id, meter_value);
        const auto total_power_active_import = ocpp::v2::utils::get_total_power_active_import(meter_value);
        if (total_power_active_import.has_value()) {
            m_everest_device_model_storage->update_power(evse_id, total_power_active_import.value());
        }
    } else {
        std::scoped_lock lock(m_session_event_mutex);
        m_event_queue[evse_id].emplace(meter_value);
    }
}

void GenericOcpp::cb_ready(std::int32_t evse_id, bool ready) {
    if (ready) {
        EVLOG_info << "EVSE " << evse_id << " ready.";
        m_evse_ready_map.handle()->at(evse_id) = true;
        m_evse_ready_map.notify_one();
    }
}

ocpp::v2::RequestStartStopStatusEnum
GenericOcpp::cb_remote_start_transaction(const ocpp::v2::RequestStartTransactionRequest& request,
                                         bool authorize_remote_start) {
    using namespace module::conversions;

    types::authorization::ProvidedIdToken provided_token;
    provided_token.id_token = to_everest_id_token(request.idToken);
    provided_token.authorization_type = types::authorization::AuthorizationType::OCPP;
    provided_token.prevalidated = !authorize_remote_start;
    provided_token.request_id = request.remoteStartId;

    if (request.groupIdToken.has_value()) {
        provided_token.parent_id_token = to_everest_id_token(request.groupIdToken.value());
    }

    if (request.evseId.has_value()) {
        provided_token.connectors = std::vector<std::int32_t>{request.evseId.value()};
    }
    m_provides.auth_provider.publish_provided_token(provided_token);
    return ocpp::v2::RequestStartStopStatusEnum::Accepted;
}

void GenericOcpp::cb_reservation_update(types::reservation::ReservationUpdateStatus status) {
    using namespace module::conversions;

    if (status.reservation_status == types::reservation::Reservation_status::Expired ||
        status.reservation_status == types::reservation::Reservation_status::Removed) {
        EVLOG_debug << "Received reservation status update for reservation " << status.reservation_id << ": "
                    << (status.reservation_status == types::reservation::Reservation_status::Expired ? "Expired"
                                                                                                     : "Removed");
        try {
            m_charge_point.on_reservation_status(status.reservation_id,
                                                 to_ocpp_reservation_update_status_enum(status.reservation_status));
        } catch (const std::out_of_range& e) {
        }
    }
}

ocpp::v2::ReserveNowStatusEnum GenericOcpp::cb_reserve_now(const ocpp::v2::ReserveNowRequest& request) {
    using namespace module::conversions;

    auto result = ocpp::v2::ReserveNowStatusEnum::Rejected;

    if (m_requires.reservation.empty() || m_requires.reservation.at(0) == nullptr) {
        EVLOG_info << "Reservation rejected because the interface r_reservation is a nullptr";
    } else {
        types::reservation::Reservation reservation;
        reservation.reservation_id = request.id;
        reservation.expiry_time = request.expiryDateTime.to_rfc3339();
        reservation.id_token = request.idToken.idToken;
        reservation.evse_id = request.evseId;
        if (request.groupIdToken.has_value()) {
            reservation.parent_id_token = request.groupIdToken.value().idToken;
        }
        if (request.connectorType.has_value()) {
            reservation.connector_type =
                types::evse_manager::string_to_connector_type_enum(request.connectorType.value());
        }
        const auto revervation_result = m_requires.reservation.at(0)->call_reserve_now(reservation);
        result = to_ocpp_reservation_status(revervation_result);
    }
    return result;
}

void GenericOcpp::cb_reset(const std::optional<const std::int32_t>& evse_id, ocpp::v2::ResetEnum type) {
    if (evse_id.has_value()) {
        EVLOG_warning << "Reset of EVSE is currently not supported";
    } else {
        bool scheduled = type == ocpp::v2::ResetEnum::OnIdle;

        // small delay before stopping the charge point to make sure all responses are received
        std::this_thread::sleep_for(std::chrono::seconds(m_config.getResetStopDelay()));
        try {
            m_requires.system.call_reset(types::system::ResetType::NotSpecified, scheduled);
        } catch (std::out_of_range& e) {
            EVLOG_warning << "Could not convert OCPP ResetEnum to EVerest ResetType while executing reset_callack. No "
                             "reset will be executed.";
        }
    }
}

void GenericOcpp::cb_security_event(const ocpp::CiString<50>& event_type,
                                    const std::optional<ocpp::CiString<255>>& tech_info) {
    types::ocpp::SecurityEvent event;
    event.type = event_type.get();
    EVLOG_info << "Security Event in OCPP occurred: " << event.type;
    if (tech_info.has_value()) {
        event.info = tech_info.value().get();
    }
    m_provides.ocpp_generic.publish_security_event(event);
}

void GenericOcpp::cb_service_renegotiation_supported(std::int32_t extensions_id, bool service_renegotiation_supported) {
    const auto& mapping = m_requires.extensions_15118.at(extensions_id)->get_mapping();
    if (mapping.has_value()) {
        m_evse_service_renegotiation_supported[mapping->evse] = service_renegotiation_supported;
    } else {
        EVLOG_warning << "ISO15118 Extension interface mapping not set! Not retrieving 'Service "
                         "Renegotiation Supported'!";
    }
}

void GenericOcpp::cb_session_event(std::int32_t evse_id, types::evse_manager::SessionEvent session_event) {
    if (m_started) {
        process_session_event(evse_id, session_event);
    } else {
        EVLOG_info << "OCPP not fully initialised, but received a session event on evse_id: " << evse_id
                   << " that will be queued up: " << session_event.event;
        std::scoped_lock lock(m_session_event_mutex);
        m_event_queue[evse_id].emplace(session_event);
    }
}

ocpp::v2::SetDisplayMessageResponse
GenericOcpp::cb_set_display_message(const std::vector<ocpp::DisplayMessage>& messages) {
    using namespace module::conversions;

    ocpp::v2::SetDisplayMessageResponse response;
    if (m_requires.display_message.empty()) {
        response.status = ocpp::v2::DisplayMessageStatusEnum::Rejected;
        return response;
    }

    std::vector<types::display_message::DisplayMessage> display_messages;
    for (const ocpp::DisplayMessage& message : messages) {
        const types::display_message::DisplayMessage m = ocpp_conversions::to_everest_display_message(message);
        display_messages.push_back(m);
    }

    const types::display_message::SetDisplayMessageResponse display_message_response =
        m_requires.display_message.at(0)->call_set_display_message(display_messages);
    response = to_ocpp_set_display_message_response(display_message_response);

    return response;
}

void GenericOcpp::cb_set_running_cost(const ocpp::RunningCost& running_cost, std::uint32_t number_of_decimals,
                                      const std::optional<std::string>& currency_code) {
    std::optional<types::money::CurrencyCode> currency;
    if (currency_code.has_value()) {
        try {
            currency = types::money::string_to_currency_code(currency_code.value());
        } catch (const std::out_of_range& e) {
            // If conversion fails, we just don't add the currency code. But we want to see it in the
            // logging.
            EVLOG_error << e.what();
        }
    }
    const types::session_cost::SessionCost cost =
        ocpp_conversions::create_session_cost(running_cost, number_of_decimals, currency);
    m_provides.session_cost.publish_session_cost(cost);
}

ocpp::v2::RequestStartStopStatusEnum GenericOcpp::cb_stop_transaction(std::int32_t evse_id,
                                                                      ocpp::v2::ReasonEnum stop_reason) {
    using namespace module::conversions;

    auto result = ocpp::v2::RequestStartStopStatusEnum::Rejected;

    if (evse_id > 0 && evse_id <= m_requires.evse_manager.size()) {
        types::evse_manager::StopTransactionRequest req;
        req.reason = to_everest_stop_transaction_reason(stop_reason);
        result = m_requires.evse_manager.at(evse_id - 1)->call_stop_transaction(req)
                     ? ocpp::v2::RequestStartStopStatusEnum::Accepted
                     : ocpp::v2::RequestStartStopStatusEnum::Rejected;
    }
    return result;
}

void GenericOcpp::cb_supported_energy_transfer_modes(
    std::int32_t evse_id, const std::vector<types::iso15118::EnergyTransferMode>& supported_energy_transfer_modes) {
    m_evse_supported_energy_transfer_modes[evse_id] = supported_energy_transfer_modes;
}

void GenericOcpp::cb_tariff_message(const ocpp::TariffMessage& message) {
    const types::session_cost::TariffMessage m = ocpp_conversions::to_everest_tariff_message(message);
    m_provides.session_cost.publish_tariff_message(m);
}

void GenericOcpp::cb_time_sync(const ocpp::DateTime& current_time) {
    m_requires.system.call_set_system_time(current_time.to_rfc3339());
}

void GenericOcpp::cb_transaction_event(const ocpp::v2::TransactionEventRequest& transaction_event) {
    using namespace module::conversions;

    const auto ocpp_transaction_event = to_everest_ocpp_transaction_event(transaction_event);
    m_provides.ocpp_generic.publish_ocpp_transaction_event(ocpp_transaction_event);
}

void GenericOcpp::cb_transaction_event_response(const ocpp::v2::TransactionEventRequest& transaction_event,
                                                const ocpp::v2::TransactionEventResponse& transaction_event_response) {
    using namespace module::conversions;

    auto ocpp_transaction_event = to_everest_ocpp_transaction_event(transaction_event);
    auto ocpp_transaction_event_response = to_everest_transaction_event_response(transaction_event_response);
    ocpp_transaction_event_response.original_transaction_event = ocpp_transaction_event;
    m_provides.ocpp_generic.publish_ocpp_transaction_event_response(ocpp_transaction_event_response);
    if (transaction_event_response.idTokenInfo.has_value() and transaction_event.evse.has_value()) {
        types::authorization::ValidationResultUpdate result_update;
        result_update.validation_result = to_everest_validation_result(transaction_event_response.idTokenInfo.value());
        result_update.connector_id = transaction_event.evse->id;
        m_provides.auth_validator.publish_validate_result_update(result_update);
    }
}

ocpp::v2::UnlockConnectorResponse GenericOcpp::cb_unlock_connector(std::int32_t evse_id, std::int32_t connector_id) {
    // FIXME: This needs to properly handle different connectors
    ocpp::v2::UnlockConnectorResponse response;
    if (evse_id > 0 && evse_id <= m_requires.evse_manager.size()) {
        if (m_requires.evse_manager.at(evse_id - 1)->call_force_unlock(connector_id)) {
            response.status = ocpp::v2::UnlockStatusEnum::Unlocked;
        } else {
            response.status = ocpp::v2::UnlockStatusEnum::UnlockFailed;
        }
    } else {
        response.status = ocpp::v2::UnlockStatusEnum::UnknownConnector;
    }
    return response;
}

bool GenericOcpp::cb_update_allowed_energy_transfer_modes(
    const std::vector<ocpp::v2::EnergyTransferModeEnum>& allowed_energy_transfer_modes,
    const ocpp::CiString<36>& transaction_id) {
    using namespace module::conversions;

    bool result{false};
    const auto evse_id = m_transaction_handler->get_evse_id(transaction_id);
    if (evse_id > 0 && evse_id <= m_requires.evse_manager.size()) {
        auto& evse = m_requires.evse_manager.at(evse_id - 1); // evse_id starts at 1 if valid

        if (evse != nullptr) {
            result = evse->call_update_allowed_energy_transfer_modes(
                         to_everest_allowed_energy_transfer_modes(allowed_energy_transfer_modes)) ==
                     types::evse_manager::UpdateAllowedEnergyTransferModesResult::Accepted;
        }
    }
    return result;
}

ocpp::v2::UpdateFirmwareResponse
GenericOcpp::cb_update_firmware_request(const ocpp::v2::UpdateFirmwareRequest& request) {
    using namespace module::conversions;

    auto req = to_everest_firmware_update_request(request);
    if (req.retries.has_value()) {
        req.retries = req.retries.value() + 1;
    }
    const auto response = m_requires.system.call_update_firmware(req);
    return to_ocpp_update_firmware_response(response);
}

ocpp::v2::SetNetworkProfileStatusEnum
GenericOcpp::cb_validate_network_profile(const ocpp::v2::NetworkConnectionProfile& network_connection_profile) {
    const auto ws_uri = ocpp::uri(network_connection_profile.ocppCsmsUrl.get());
    return (ws_uri.get_valid()) ? ocpp::v2::SetNetworkProfileStatusEnum::Accepted
                                : ocpp::v2::SetNetworkProfileStatusEnum::Rejected;
    // TODO(piet): Add further validation of the NetworkConnectionProfile
}

void GenericOcpp::cb_variable_changed_v16(const ocpp::v2::Variable& variable, const std::string& value) {
    using namespace module::conversions;

    MonitorListEntry entry{{}, variable};
    bool publish;
    {
        std::lock_guard lock(m_monitor_list_mutex);
        const auto it = m_monitor_list.find(entry);
        publish = it != m_monitor_list.end();
    }
    if (publish) {
        // monitor entry exists - publish
        // TODO(james-ctc): need the read-only info
        ocpp::v16::KeyValue key_value{variable.name, false, value};
        m_provides.ocpp16.publish_configuration_key(to_everest(key_value));
    }
}

void GenericOcpp::cb_variable_changed(const ocpp::v2::Component& component, const ocpp::v2::Variable& variable,
                                      const std::string& value) {
    using namespace module::conversions;

    MonitorListEntry entry{component, variable};
    bool publish;
    {
        std::lock_guard lock(m_monitor_list_mutex);
        const auto it = m_monitor_list.find(entry);
        publish = it != m_monitor_list.end();
    }
    if (publish) {
        // monitor entry exists - publish
        types::ocpp::EventData event_data;
        event_data.component_variable.component = to_everest_component(component);
        event_data.component_variable.variable = to_everest_variable(variable);
        event_data.event_id = 0;
        event_data.timestamp = ocpp::DateTime();
        event_data.trigger = types::ocpp::EventTriggerEnum::Delta;
        event_data.actual_value = value;
        event_data.event_notification_type = types::ocpp::EventNotificationType::CustomMonitor;
        m_provides.ocpp_generic.publish_event_data(event_data);
    }
}

void GenericOcpp::cb_variable_changed(const ocpp::v2::SetVariableData& set_variable_data) {
    using namespace ocpp::v2;
    const auto& component = set_variable_data.component;
    const auto& name = set_variable_data.variable.name.get();

    if (component == ControllerComponents::TxCtrlr) {
        if (name == EV_CONNECTION_TIMEOUT_VAR_NAME) {
            try {
                auto ev_connection_timeout = std::stoi(set_variable_data.attributeValue.get());
                m_requires.auth.call_set_connection_timeout(ev_connection_timeout);
            } catch (const std::exception& e) {
                EVLOG_error << "Could not parse EVConnectionTimeOut and did not set it in Auth module, error: "
                            << e.what();
            }
        } else if (name == TX_STOP_POINT_VAR_NAME) {
            const auto tx_stop_points = get_tx_start_stop_points(set_variable_data.attributeValue.get());
            if (tx_stop_points.empty()) {
                EVLOG_warning << "Could not set TxStartPoints";
            } else {
                m_transaction_handler->set_tx_stop_points(tx_stop_points);
            }
        } else if (name == TX_START_POINT_VAR_NAME) {
            const auto tx_start_points = get_tx_start_stop_points(set_variable_data.attributeValue.get());
            if (tx_start_points.empty()) {
                EVLOG_warning << "Could not set TxStartPoints";
            } else {
                m_transaction_handler->set_tx_start_points(tx_start_points);
            }
        }
    } else if (component == ControllerComponents::AuthCtrlr) {
        if (name == MASTER_PASS_GROUP_ID_VAR_NAME) {
            m_requires.auth.call_set_master_pass_group_id(set_variable_data.attributeValue.get());
        }
    } else if (component == ControllerComponents::ISO15118Ctrlr) {
        if (name == PNC_ENABLED_VAR_NAME) {
            types::evse_manager::PlugAndChargeConfiguration pnc_config;
            pnc_config.pnc_enabled = ocpp::conversions::string_to_bool(set_variable_data.attributeValue.get());
            for (const auto& evse_manager : m_requires.evse_manager) {
                evse_manager->call_set_plug_and_charge_configuration(pnc_config);
            }
        } else if (name == CENTRAL_CONTRACT_VALIDATION_ALLOWED_VAR_NAME) {
            types::evse_manager::PlugAndChargeConfiguration pnc_config;
            pnc_config.central_contract_validation_allowed =
                ocpp::conversions::string_to_bool(set_variable_data.attributeValue.get());
            for (const auto& evse_manager : m_requires.evse_manager) {
                evse_manager->call_set_plug_and_charge_configuration(pnc_config);
            }
        } else if (name == CONTRACT_CERTIFICATE_INSTALLATION_ENABLED_VAR_NAME) {
            types::evse_manager::PlugAndChargeConfiguration pnc_config;
            pnc_config.contract_certificate_installation_enabled =
                ocpp::conversions::string_to_bool(set_variable_data.attributeValue.get());
            for (const auto& evse_manager : m_requires.evse_manager) {
                evse_manager->call_set_plug_and_charge_configuration(pnc_config);
            }
        }
    }
}

void GenericOcpp::cb_waiting_for_external_ready(std::int32_t evse_id, bool ready) {
    if (ready) {
        m_evse_ready_map.handle()->at(evse_id) = true;
        m_evse_ready_map.notify_one();
    }
}

// ----------------------------------------------------------------------------
// general

void GenericOcpp::charging_schedules_timer_start() {
    const auto interval = m_config.getCompositeScheduleIntervalS();
    if (interval > 0) {
        m_charging_schedules_timer.interval([this]() { cb_charging_schedules_timer(); },
                                            std::chrono::seconds(interval));
    }
}

void GenericOcpp::charging_schedules_timer_stop() {
    m_charging_schedules_timer.stop();
}

std::optional<types::energy::ScheduleReqEntry>
GenericOcpp::create_limits_entry(const std::string& timestamp, const ocpp::v2::EnhancedChargingSchedulePeriod& period,
                                 ocpp::v2::ChargingRateUnitEnum unit) {
    std::optional<types::energy::ScheduleReqEntry> result;

    if (period.limit.has_value()) {
        types::energy::ScheduleReqEntry entry;
        entry.timestamp = timestamp;

        const auto source_ext_limit = m_info.id + "/OCPP_set_external_limits";

        types::energy::LimitsReq limits_req;
        if (unit == ocpp::v2::ChargingRateUnitEnum::A) {
            limits_req.ac_max_current_A = {period.limit.value(), source_ext_limit};
            if (period.numberPhases.has_value()) {
                limits_req.ac_max_phase_count = {period.numberPhases.value(), source_ext_limit};
            }
        } else {
            limits_req.total_power_W = {period.limit.value(), source_ext_limit};
        }

        entry.limits_to_leaves = limits_req;
        result = std::move(entry);
    }

    return result;
};

std::optional<types::energy::ScheduleSetpointEntry>
GenericOcpp::create_setpoint_entry(std::int32_t setpoint_priority, const std::string& timestamp,
                                   const ocpp::v2::EnhancedChargingSchedulePeriod& period,
                                   ocpp::v2::ChargingRateUnitEnum unit) {
    std::optional<types::energy::ScheduleSetpointEntry> result;
    const bool has_basic_setpoint = period.setpoint.has_value();
    const bool has_freq_table = period.v2xFreqWattCurve.has_value() && !period.v2xFreqWattCurve->empty();

    if (has_basic_setpoint || has_freq_table) {
        types::energy::ScheduleSetpointEntry entry;
        types::energy::SetpointType setpoint;
        setpoint.source = SETPOINT_SOURCE;
        setpoint.priority = setpoint_priority;
        entry.timestamp = timestamp;

        if (has_basic_setpoint) {
            if (unit == ocpp::v2::ChargingRateUnitEnum::A) {
                setpoint.ac_current_A = period.setpoint;
            } else {
                setpoint.total_power_W = period.setpoint;
            }
        }

        if (has_freq_table) {
            std::vector<types::energy::FrequencyWattPoint> frequency_table;
            for (const auto& point : period.v2xFreqWattCurve.value()) {
                types::energy::FrequencyWattPoint freq_point{};
                freq_point.frequency_Hz = point.frequency;
                freq_point.total_power_W = point.power;
                frequency_table.push_back(freq_point);
            }
            setpoint.frequency_table = std::move(frequency_table);
        }

        entry.setpoint = std::move(setpoint);
        result = std::move(entry);
    }

    return result;
}

std::filesystem::path GenericOcpp::device_model_config_path() const {
    auto path = fs::path(m_config.getDeviceModelConfigPath());
    if (path.is_relative()) {
        path = m_info.paths.share / path;
    }
    return path;
}

std::filesystem::path GenericOcpp::device_model_database_path() const {
    auto path = fs::path(m_config.getDeviceModelDatabasePath());
    if (path.is_relative()) {
        path = m_info.paths.share / path;
    }
    return path;
}

std::filesystem::path GenericOcpp::device_model_database_migration_path() const {
    auto path = fs::path(m_config.getDeviceModelDatabaseMigrationPath());
    if (path.is_relative()) {
        path = m_info.paths.share / path;
    }
    return path;
}

std::filesystem::path GenericOcpp::everest_device_model_database_path() const {
    auto path = fs::path(m_config.getEverestDeviceModelDatabasePath());
    if (path.is_relative()) {
        path = m_info.paths.share / path;
    }
    return path;
}

std::map<std::int32_t, std::int32_t> GenericOcpp::get_connector_structure() {
    std::map<std::int32_t, std::int32_t> evse_connector_structure;
    std::int32_t evse_id = 1;
    for (const auto& evse : m_requires.evse_manager) {
        auto _evse = evse->call_get_evse();
        std::int32_t num_connectors = _evse.connectors.size();

        if (_evse.id != evse_id) {
            throw std::runtime_error("Configured evse_id(s) must start with 1 counting upwards");
        }
        if (num_connectors > 0) {
            std::int32_t connector_id = 1;
            for (const auto& connector : _evse.connectors) {
                if (connector.id != connector_id) {
                    throw std::runtime_error("Configured connector_id(s) must start with 1 counting upwards");
                }
                connector_id++;
            }
        } else {
            num_connectors = 1;
        }

        evse_connector_structure[evse_id] = num_connectors;
        evse_id++;
    }
    return evse_connector_structure;
}

void GenericOcpp::process_authorised(std::int32_t evse_id, std::int32_t connector_id,
                                     const types::evse_manager::SessionEvent& session_event) {
    // currently handled as part of SessionStarted and TransactionStarted events
}

void GenericOcpp::process_deauthorised(std::int32_t evse_id, std::int32_t connector_id,
                                       const types::evse_manager::SessionEvent& session_event) {
    auto transaction_data = m_transaction_handler->get_transaction_data(evse_id);
    if (transaction_data != nullptr) {
        transaction_data->trigger_reason = ocpp::v2::TriggerReasonEnum::StopAuthorized;
    }
    const auto tx_event_effect = m_transaction_handler->submit_event(evse_id, module::TxEvent::DEAUTHORIZED);
    process_tx_event_effect(evse_id, tx_event_effect, session_event);
}

void GenericOcpp::process_charging_paused_ev(std::int32_t evse_id, std::int32_t connector_id,
                                             const types::evse_manager::SessionEvent& session_event) {
    auto transaction_data = m_transaction_handler->get_transaction_data(evse_id);
    if (transaction_data != nullptr) {
        transaction_data->charging_state = ocpp::v2::ChargingStateEnum::SuspendedEV;
        transaction_data->trigger_reason = ocpp::v2::TriggerReasonEnum::ChargingStateChanged;
        transaction_data->stop_reason = ocpp::v2::ReasonEnum::StoppedByEV;
    }
    const auto tx_event_effect = m_transaction_handler->submit_event(evse_id, module::TxEvent::ENERGY_TRANSFER_STOPPED);
    process_tx_event_effect(evse_id, tx_event_effect, session_event);
    m_charge_point.on_charging_state_changed(evse_id, ocpp::v2::ChargingStateEnum::SuspendedEV,
                                             ocpp::v2::TriggerReasonEnum::ChargingStateChanged);
}

void GenericOcpp::process_charging_paused_evse(std::int32_t evse_id, std::int32_t connector_id,
                                               const types::evse_manager::SessionEvent& session_event) {
    auto transaction_data = m_transaction_handler->get_transaction_data(evse_id);
    auto trigger_reason = ocpp::v2::TriggerReasonEnum::ChargingStateChanged;
    if (transaction_data != nullptr) {
        transaction_data->charging_state = ocpp::v2::ChargingStateEnum::SuspendedEVSE;
        if (transaction_data->stop_reason == ocpp::v2::ReasonEnum::Remote) {
            trigger_reason = ocpp::v2::TriggerReasonEnum::RemoteStop;
            transaction_data->trigger_reason = ocpp::v2::TriggerReasonEnum::ChargingStateChanged;
        }
    }
    const auto tx_event_effect = m_transaction_handler->submit_event(evse_id, module::TxEvent::ENERGY_TRANSFER_STOPPED);
    process_tx_event_effect(evse_id, tx_event_effect, session_event);
    m_charge_point.on_charging_state_changed(evse_id, ocpp::v2::ChargingStateEnum::SuspendedEVSE, trigger_reason);
}

void GenericOcpp::process_charging_started(std::int32_t evse_id, std::int32_t connector_id,
                                           const types::evse_manager::SessionEvent& session_event) {
    auto transaction_data = m_transaction_handler->get_transaction_data(evse_id);
    if (transaction_data != nullptr) {
        transaction_data->trigger_reason = ocpp::v2::TriggerReasonEnum::ChargingStateChanged;
        transaction_data->charging_state = ocpp::v2::ChargingStateEnum::Charging;
    }
    const auto tx_event_effect = m_transaction_handler->submit_event(evse_id, module::TxEvent::ENERGY_TRANSFER_STARTED);
    process_tx_event_effect(evse_id, tx_event_effect, session_event);
    m_charge_point.on_charging_state_changed(evse_id, ocpp::v2::ChargingStateEnum::Charging,
                                             ocpp::v2::TriggerReasonEnum::ChargingStateChanged);
}

void GenericOcpp::process_disabled(std::int32_t evse_id, std::int32_t connector_id,
                                   const types::evse_manager::SessionEvent& session_event) {
    m_charge_point.on_unavailable(evse_id, connector_id);
}

void GenericOcpp::process_enabled(std::int32_t evse_id, std::int32_t connector_id,
                                  const types::evse_manager::SessionEvent& session_event) {
    m_charge_point.on_enabled(evse_id, connector_id);
}

void GenericOcpp::process_reservation_end(std::int32_t evse_id, std::int32_t connector_id) {
    m_charge_point.on_reservation_cleared(evse_id, connector_id);
}

void GenericOcpp::process_reserved(std::int32_t evse_id, std::int32_t connector_id) {
    m_charge_point.on_reserved(evse_id, connector_id);
}

void GenericOcpp::process_session_event(std::int32_t evse_id, const types::evse_manager::SessionEvent& session_event) {
    const auto connector_id = session_event.connector_id.value_or(1);
    std::lock_guard<std::mutex> lg(m_session_event_mutex);
    switch (session_event.event) {
    case types::evse_manager::SessionEventEnum::SessionStarted: {
        process_session_started(evse_id, connector_id, session_event);
        break;
    }
    case types::evse_manager::SessionEventEnum::SessionFinished: {
        process_session_finished(evse_id, connector_id, session_event);
        break;
    }
    case types::evse_manager::SessionEventEnum::TransactionStarted: {
        process_transaction_started(evse_id, connector_id, session_event);
        break;
    }
    case types::evse_manager::SessionEventEnum::TransactionFinished: {
        process_transaction_finished(evse_id, connector_id, session_event);
        break;
    }
    case types::evse_manager::SessionEventEnum::SessionResumed:
        process_session_resumed(evse_id, connector_id, session_event);
        break;
    case types::evse_manager::SessionEventEnum::ChargingStarted: {
        process_charging_started(evse_id, connector_id, session_event);
        break;
    }
    case types::evse_manager::SessionEventEnum::ChargingPausedEV: {
        process_charging_paused_ev(evse_id, connector_id, session_event);
        break;
    }
    case types::evse_manager::SessionEventEnum::ChargingPausedEVSE: {
        process_charging_paused_evse(evse_id, connector_id, session_event);
        break;
    }
    case types::evse_manager::SessionEventEnum::Disabled: {
        process_disabled(evse_id, connector_id, session_event);
        break;
    }
    case types::evse_manager::SessionEventEnum::Enabled: {
        process_enabled(evse_id, connector_id, session_event);
        break;
    }
    case types::evse_manager::SessionEventEnum::Authorized: {
        process_authorised(evse_id, connector_id, session_event);
        break;
    }
    case types::evse_manager::SessionEventEnum::Deauthorized: {
        process_deauthorised(evse_id, connector_id, session_event);
        break;
    }
    case types::evse_manager::SessionEventEnum::ReservationStart: {
        process_reserved(evse_id, connector_id);
        break;
    }
    case types::evse_manager::SessionEventEnum::ReservationEnd: {
        process_reservation_end(evse_id, connector_id);
        break;
    }
    // explicitly ignore the following session events for now
    // TODO(kai): implement
    case types::evse_manager::SessionEventEnum::AuthRequired:
    case types::evse_manager::SessionEventEnum::PrepareCharging:
    case types::evse_manager::SessionEventEnum::StoppingCharging:
    case types::evse_manager::SessionEventEnum::ChargingFinished:
    case types::evse_manager::SessionEventEnum::PluginTimeout:
    case types::evse_manager::SessionEventEnum::SwitchingPhases:
        break;
    }

    // process authorised event which will inititate a TransactionEvent(Updated) message in case the token has not
    // yet been authorised by the CSMS
    auto authorized_id_token = get_authorised_id_token(session_event);
    if (authorized_id_token.has_value()) {
        {
            auto evse_evcc_id_handle = m_evse_evcc_id.handle();
            if (!evse_evcc_id_handle->at(evse_id).empty()) {
                update_evcc_id_token(authorized_id_token.value(), evse_evcc_id_handle->at(evse_id),
                                     m_ocpp_protocol_version);
            }
        }
        m_charge_point.on_authorized(evse_id, connector_id, authorized_id_token.value());
    }
}

void GenericOcpp::process_session_finished(std::int32_t evse_id, std::int32_t connector_id,
                                           const types::evse_manager::SessionEvent& session_event) {
    m_evse_soc_map.handle()->at(evse_id).reset();
    auto transaction_data = m_transaction_handler->get_transaction_data(evse_id);
    if (transaction_data != nullptr) {
        transaction_data->charging_state = ocpp::v2::ChargingStateEnum::Idle;
        transaction_data->stop_reason = ocpp::v2::ReasonEnum::EVDisconnected;
        transaction_data->trigger_reason = ocpp::v2::TriggerReasonEnum::EVCommunicationLost;
    }
    const auto tx_event_effect = m_transaction_handler->submit_event(evse_id, module::TxEvent::EV_DISCONNECTED);
    m_evse_evcc_id.handle()->at(evse_id) = "";
    process_tx_event_effect(evse_id, tx_event_effect, session_event);
    m_charge_point.on_session_finished(evse_id, connector_id);
    m_everest_device_model_storage->update_connected_ev_available(evse_id, false);
}

void GenericOcpp::process_session_resumed(std::int32_t evse_id, std::int32_t connector_id,
                                          const types::evse_manager::SessionEvent& session_event) {
    // resume transaction with data we get from the session event
    // currently, the SessionResumed event only occurs after a power outage followed by
    // a TransactionFinished event. We have to add the transaction data again to be able to
    // properly process the transaction finished event.
    // Currently, sending a TransactionEvent(Ended) after a power loss is only supported if
    // the configuration variable InternalCtrlr::ResumeTransactionsOnBoot is set to true.
    // If this is not the case, libocpp will not be able to process a TransactionFinished event
    // after a power loss, because it does internally not restore transaction data on boot.
    const auto timestamp = ocpp_conversions::to_ocpp_datetime_or_now(session_event.timestamp);
    auto transaction_data = std::make_shared<module::TransactionData>(connector_id, session_event.uuid, timestamp,
                                                                      ocpp::v2::TriggerReasonEnum::TxResumed,
                                                                      ocpp::v2::ChargingStateEnum::Idle);
    transaction_data->started = true;
    m_transaction_handler->add_transaction_data(evse_id, transaction_data);
}

void GenericOcpp::process_session_started(std::int32_t evse_id, std::int32_t connector_id,
                                          const types::evse_manager::SessionEvent& session_event) {
    using namespace module::conversions;

    if (session_event.session_started.has_value()) {
        const auto session_started = session_event.session_started.value();

        std::optional<ocpp::v2::IdToken> id_token = std::nullopt;
        std::optional<ocpp::v2::IdToken> group_id_token = std::nullopt;
        std::optional<std::int32_t> remote_start_id = std::nullopt;
        auto charging_state = ocpp::v2::ChargingStateEnum::Idle;
        auto trigger_reason = ocpp::v2::TriggerReasonEnum::Authorized;
        auto tx_event = module::TxEvent::AUTHORIZED;
        if (session_started.reason == types::evse_manager::StartSessionReason::EVConnected) {
            tx_event = module::TxEvent::EV_CONNECTED;
            trigger_reason = ocpp::v2::TriggerReasonEnum::CablePluggedIn;
            charging_state = ocpp::v2::ChargingStateEnum::EVConnected;
        } else if (!session_started.id_tag.has_value()) {
            EVLOG_warning << "Session started with reason Authorized, but no id_tag provided as part of the "
                             "session event";
        } else {
            id_token = to_ocpp_id_token(session_started.id_tag.value().id_token);
            auto evse_evcc_id_handle = m_evse_evcc_id.handle();
            if (!evse_evcc_id_handle->at(evse_id).empty()) {
                update_evcc_id_token(id_token.value(), evse_evcc_id_handle->at(evse_id), m_ocpp_protocol_version);
            }
            remote_start_id = session_started.id_tag.value().request_id;
            if (session_started.id_tag.value().parent_id_token.has_value()) {
                group_id_token = to_ocpp_id_token(session_started.id_tag.value().parent_id_token.value());
            }
            if (session_started.id_tag.value().authorization_type == types::authorization::AuthorizationType::OCPP) {
                trigger_reason = ocpp::v2::TriggerReasonEnum::RemoteStart;
            }
        }
        const auto timestamp = ocpp_conversions::to_ocpp_datetime_or_now(session_event.timestamp);
        const auto reservation_id = session_started.reservation_id;

        // this is always the first transaction related interaction, so we create TransactionData here
        auto transaction_data = std::make_shared<module::TransactionData>(connector_id, session_event.uuid, timestamp,
                                                                          trigger_reason, charging_state);
        transaction_data->id_token = id_token;
        transaction_data->group_id_token = group_id_token;
        transaction_data->remote_start_id = remote_start_id;
        transaction_data->reservation_id = reservation_id;
        m_transaction_handler->add_transaction_data(evse_id, transaction_data);

        const auto tx_event_effect = m_transaction_handler->submit_event(evse_id, tx_event);
        process_tx_event_effect(evse_id, tx_event_effect, session_event);
        if (session_started.reason == types::evse_manager::StartSessionReason::EVConnected) {
            m_charge_point.on_session_started(evse_id, connector_id);
        }
        if (tx_event == module::TxEvent::EV_CONNECTED) {
            m_everest_device_model_storage->update_connected_ev_available(evse_id, true);
        }
    } else {
        throw std::runtime_error("SessionEvent SessionStarted does not contain session_started context");
    }
}

void GenericOcpp::process_transaction_finished(std::int32_t evse_id, std::int32_t connector_id,
                                               const types::evse_manager::SessionEvent& session_event) {
    using namespace module::conversions;

    if (session_event.transaction_finished.has_value()) {
        const auto transaction_finished = session_event.transaction_finished.value();
        auto tx_event = module::TxEvent::NONE;
        auto reason = ocpp::v2::ReasonEnum::Other;
        if (transaction_finished.reason.has_value()) {
            reason = to_ocpp_reason(transaction_finished.reason.value());
            tx_event = get_tx_event(reason);
        }
        auto transaction_data = m_transaction_handler->get_transaction_data(evse_id);
        if (transaction_data != nullptr) {
            std::optional<ocpp::v2::IdToken> id_token = std::nullopt;
            if (transaction_finished.id_tag.has_value()) {
                id_token = to_ocpp_id_token(transaction_finished.id_tag.value().id_token);
                auto evse_evcc_id_handle = m_evse_evcc_id.handle();
                if (!evse_evcc_id_handle->at(evse_id).empty()) {
                    update_evcc_id_token(id_token.value(), evse_evcc_id_handle->at(evse_id), m_ocpp_protocol_version);
                }
            }

            // this is required to report the correct charging_state within a TransactionEvent(Ended) message
            auto charging_state = transaction_data->charging_state;
            if (reason == ocpp::v2::ReasonEnum::EVDisconnected) {
                charging_state = ocpp::v2::ChargingStateEnum::Idle;
            } else if (reason == ocpp::v2::ReasonEnum::ImmediateReset &&
                       charging_state != ocpp::v2::ChargingStateEnum::Idle) {
                charging_state = ocpp::v2::ChargingStateEnum::EVConnected;
            } else if (tx_event == module::TxEvent::DEAUTHORIZED) {
                charging_state = ocpp::v2::ChargingStateEnum::EVConnected;
            }
            transaction_data->trigger_reason = stop_reason_to_trigger_reason_enum(reason);
            transaction_data->stop_reason = reason;
            transaction_data->id_token = id_token;
            transaction_data->charging_state = charging_state;
        }

        // tx_event could be DEAUTHORIZED or EV_DISCONNECTED
        const auto tx_event_effect = m_transaction_handler->submit_event(evse_id, tx_event);
        process_tx_event_effect(evse_id, tx_event_effect, session_event);

        if (tx_event == module::TxEvent::DEAUTHORIZED) {
            if (reason == ocpp::v2::ReasonEnum::Remote) {
                m_charge_point.on_charging_state_changed(evse_id, ocpp::v2::ChargingStateEnum::EVConnected,
                                                         ocpp::v2::TriggerReasonEnum::RemoteStop);
            } else {
                m_charge_point.on_charging_state_changed(evse_id, ocpp::v2::ChargingStateEnum::EVConnected,
                                                         ocpp::v2::TriggerReasonEnum::StopAuthorized);
            }
        } else {
            // TODO(piet): If StopTxOnEVSideDisconnect is false, authorization shall still be present. This cannot only
            // be handled within this module, but probably also within EvseManager and Auth

            // authorization is always withdrawn in case of TransactionFinished, so in case we haven't updated the
            // transaction handler yet, we have to do it
            // now
            const auto tx_event_effect = m_transaction_handler->submit_event(evse_id, module::TxEvent::DEAUTHORIZED);
            process_tx_event_effect(evse_id, tx_event_effect, session_event);
        }
    } else {
        throw std::runtime_error("SessionEvent TransactionFinished does not contain transaction_finished context");
    }
}

void GenericOcpp::process_transaction_started(std::int32_t evse_id, std::int32_t connector_id,
                                              const types::evse_manager::SessionEvent& session_event) {
    using namespace module::conversions;

    if (session_event.transaction_started.has_value()) {
        auto transaction_data = m_transaction_handler->get_transaction_data(evse_id);
        if (transaction_data == nullptr) {
            EVLOG_warning
                << "Could not update transaction data because no transaction data is present. This might happen "
                   "in case a TxStopPoint is already active when a TransactionStarted event occurs (e.g. "
                   "TxStopPoint is EnergyTransfer or ParkingBayOccupied)";
            m_charge_point.on_session_started(evse_id, connector_id);
            auto tx_event_effect = m_transaction_handler->submit_event(evse_id, module::TxEvent::AUTHORIZED);
            process_tx_event_effect(evse_id, tx_event_effect, session_event);
            tx_event_effect = m_transaction_handler->submit_event(evse_id, module::TxEvent::EV_CONNECTED);
            process_tx_event_effect(evse_id, tx_event_effect, session_event);
            m_everest_device_model_storage->update_connected_ev_available(evse_id, true);
        } else {
            // at this point we dont know if the TransactionStarted event was triggered because of an Authorization or
            // EV Plug in event. We assume cable has been plugged in first and then authorized and update if other order
            // was applied
            auto tx_event = module::TxEvent::AUTHORIZED;
            auto trigger_reason = ocpp::v2::TriggerReasonEnum::Authorized;
            const auto transaction_started = session_event.transaction_started.value();
            if (transaction_started.reservation_id.has_value()) {
                transaction_data->reservation_id = transaction_started.reservation_id;
            }
            transaction_data->remote_start_id = transaction_started.id_tag.request_id;
            auto id_token = to_ocpp_id_token(transaction_started.id_tag.id_token);
            auto evse_evcc_id_handle = m_evse_evcc_id.handle();
            if (!evse_evcc_id_handle->at(evse_id).empty()) {
                update_evcc_id_token(id_token, evse_evcc_id_handle->at(evse_id), m_ocpp_protocol_version);
            }
            transaction_data->id_token = id_token;

            std::optional<ocpp::v2::IdToken> group_id_token = std::nullopt;
            if (transaction_started.id_tag.parent_id_token.has_value()) {
                transaction_data->group_id_token = to_ocpp_id_token(transaction_started.id_tag.parent_id_token.value());
            }

            // if session started reason was Authorized, Transaction is started because of EV plug in event
            if (transaction_data->trigger_reason == ocpp::v2::TriggerReasonEnum::Authorized or
                transaction_data->trigger_reason == ocpp::v2::TriggerReasonEnum::RemoteStart) {
                trigger_reason = ocpp::v2::TriggerReasonEnum::CablePluggedIn;
                transaction_data->charging_state = ocpp::v2::ChargingStateEnum::EVConnected;
                m_charge_point.on_session_started(evse_id, connector_id);
                tx_event = module::TxEvent::EV_CONNECTED;
            }

            if (transaction_started.id_tag.authorization_type == types::authorization::AuthorizationType::OCPP) {
                trigger_reason = ocpp::v2::TriggerReasonEnum::RemoteStart;
            }

            transaction_data->trigger_reason = trigger_reason;
            const auto tx_event_effect = m_transaction_handler->submit_event(evse_id, tx_event);
            process_tx_event_effect(evse_id, tx_event_effect, session_event);
            if (tx_event == module::TxEvent::EV_CONNECTED) {
                m_everest_device_model_storage->update_connected_ev_available(evse_id, true);
            }
        }
    } else {
        throw std::runtime_error("SessionEvent TransactionStarted does not contain session_started context");
    }
}

void GenericOcpp::process_tx_event_effect(std::int32_t evse_id, const module::TxEventEffect tx_event_effect,
                                          const types::evse_manager::SessionEvent& session_event) {
    using namespace module::conversions;

    if (tx_event_effect != module::TxEventEffect::NONE) {
        const auto transaction_data = m_transaction_handler->get_transaction_data(evse_id);
        if (transaction_data == nullptr) {
            throw std::runtime_error("Could not start transaction because no tranasaction data is present");
        }
        transaction_data->timestamp = ocpp_conversions::to_ocpp_datetime_or_now(session_event.timestamp);

        if (tx_event_effect == module::TxEventEffect::START_TRANSACTION) {
            transaction_data->started = true;
            transaction_data->meter_value =
                to_ocpp_meter_value(get_meter_value(session_event), ocpp::v2::ReadingContextEnum::Transaction_Begin,
                                    get_signed_meter_value(session_event));
            m_charge_point.on_transaction_started(evse_id, transaction_data->connector_id, transaction_data->session_id,
                                                  transaction_data->timestamp, transaction_data->trigger_reason,
                                                  transaction_data->meter_value, transaction_data->id_token,
                                                  transaction_data->group_id_token, transaction_data->reservation_id,
                                                  transaction_data->remote_start_id, transaction_data->charging_state);
        } else if (tx_event_effect == module::TxEventEffect::STOP_TRANSACTION) {
            transaction_data->meter_value =
                to_ocpp_meter_value(get_meter_value(session_event), ocpp::v2::ReadingContextEnum::Transaction_End,
                                    get_signed_meter_value(session_event));
            m_charge_point.on_transaction_finished(evse_id, transaction_data->timestamp, transaction_data->meter_value,
                                                   transaction_data->stop_reason, transaction_data->trigger_reason,
                                                   transaction_data->id_token, std::nullopt,
                                                   transaction_data->charging_state);
            m_transaction_handler->reset_transaction_data(evse_id);
        }
    }
}

void GenericOcpp::publish_charging_schedules(
    const std::vector<ocpp::v2::EnhancedCompositeSchedule>& composite_schedules) {
    using namespace module::conversions;

    const auto everest_schedules = to_everest_charging_schedules(composite_schedules);
    m_provides.ocpp_generic.publish_charging_schedules(everest_schedules);
}

void GenericOcpp::set_external_limits(const std::vector<ocpp::v2::EnhancedCompositeSchedule>& composite_schedules) {
    const auto start_time = ocpp::DateTime();

    auto to_timestamp = [&](int seconds_offset) {
        return ocpp::DateTime(start_time.to_time_point() + std::chrono::seconds(seconds_offset)).to_rfc3339();
    };

    std::int32_t setpoint_priority = LOWEST_SETPOINT_PRIORITY;
    const auto resp =
        m_charge_point.get_string(ocpp::v2::ControllerComponents::SmartChargingCtrlr,
                                  ocpp::v2::Variable{SETPOINT_PRIORITY_VAR_NAME}, ocpp::v2::AttributeEnum::Actual);

    if (resp) {
        setpoint_priority = resp.value() == "CSMS" ? HIGHEST_SETPOINT_PRIORITY : LOWEST_SETPOINT_PRIORITY;
    }

    for (const auto& composite_schedule : composite_schedules) {
        auto evse_id = composite_schedule.evseId;
        if (not external_energy_limits::is_evse_sink_configured(m_requires.evse_energy_sink, evse_id)) {
            EVLOG_warning << "Can not apply external limits! No evse energy sink configured for evse_id: " << evse_id;
            continue;
        }

        types::energy::ExternalLimits limits;
        std::vector<types::energy::ScheduleReqEntry> schedule_import;
        std::vector<types::energy::ScheduleSetpointEntry> schedule_setpoints;

        const auto& unit = composite_schedule.chargingRateUnit;

        for (const auto& period : composite_schedule.chargingSchedulePeriod) {
            const auto timestamp = to_timestamp(period.startPeriod);

            if (auto setpoint_entry = create_setpoint_entry(setpoint_priority, timestamp, period, unit)) {
                schedule_setpoints.push_back(*setpoint_entry);
            }

            if (auto limits_entry = create_limits_entry(timestamp, period, unit)) {
                schedule_import.push_back(*limits_entry);
            }
        }

        limits.schedule_import = std::move(schedule_import);
        limits.schedule_setpoints = std::move(schedule_setpoints);

        auto& evse_sink = external_energy_limits::get_evse_sink_by_evse_id(m_requires.evse_energy_sink, evse_id);
        evse_sink.call_set_external_limits(limits);
    }
}

void GenericOcpp::wait_all_ready() {
    auto ready_handle = m_evse_ready_map.handle();
    ready_handle.wait([this, &ready_handle]() {
        for (const auto& [evse, ready] : *ready_handle) {
            if (!ready) {
                return false;
            }
        }
        EVLOG_info << "All EVSE ready. Starting OCPP2.X service";
        return true;
    });
}

} // namespace ocpp_multi
