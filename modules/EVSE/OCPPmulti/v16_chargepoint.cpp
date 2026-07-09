// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "v16_chargepoint.hpp"

#include "charge_point_config_factory_v16.hpp"
#include "v16_conversions.hpp"
#include <everest/conversions/ocpp/ocpp_conversions.hpp>
#include <everest/ocpp_module_common/conversions.hpp>
#include <everest/ocpp_module_common/v16/conversions.hpp>
#include <everest/ocpp_module_common/v16/error_mapping.hpp>

#include <utility>

namespace conversions_v16 = ocpp_module_common::v16::conversions;

namespace {

// mapping between v1.6 and v2.x configuration keys
constexpr const auto CENTRAL_CONTRACT_VALIDATION_ALLOWED_CONFIG_KEY = "CentralContractValidationAllowed";
constexpr const auto CENTRAL_CONTRACT_VALIDATION_ALLOWED_COMPONENT = "ISO15118Ctrlr";
constexpr const auto CENTRAL_CONTRACT_VALIDATION_ALLOWED_VARIABLE = "CentralContractValidationAllowed";
constexpr const auto CONNECTION_TIMEOUT_CONFIG_KEY = "ConnectionTimeout";
constexpr const auto CONNECTION_TIMEOUT_COMPONENT = "TxCtrlr";
constexpr const auto CONNECTION_TIMEOUT_VARIABLE = "EVConnectionTimeOut";
constexpr const auto ISO15118_PNC_ENABLED_CONFIG_KEY = "ISO15118PnCEnabled";
constexpr const auto ISO15118_PNC_ENABLED_COMPONENT = "ISO15118Ctrlr";
constexpr const auto ISO15118_PNC_ENABLED_VARIABLE = "PnCEnabled";

constexpr const auto INOPERATIVE_ERROR_TYPE = "evse_manager/Inoperative";
constexpr const auto SWITCHING_PHASES_REASON = "SwitchingPhases";

template <typename T> std::optional<T> get(ocpp::v16::ChargePoint& charge_point, const std::string_view& variable) {
    std::optional<T> result;
    ocpp::v16::GetConfigurationRequest request;
    request.key = {ocpp::CiString<50>{std::string{variable}}};
    const auto response = charge_point.get_configuration_key(request);
    if (response.configurationKey) {
        for (const auto& key_value : response.configurationKey.value()) {
            if (static_cast<std::string>(key_value.key) == variable) {
                if (key_value.value) {
                    result = ocpp::v2::to_specific_type<T>(key_value.value.value());
                }
            }
        }
    }
    return result;
}

std::int32_t get_ocpp_connector_id(const ocpp_multi::GenericChargePointInterface::ConnectorStructureV16& mapping,
                                   const std::optional<ocpp::v2::EVSE>& evse) {
    std::int32_t result{0};

    // evse not present then use connector 0 - i.e. the whole charging station

    if (evse) {
        try {
            const auto evse_id = evse->id;
            const auto connector = evse->connectorId.value();
            result = mapping.at(evse_id).at(connector);
        } catch (const std::bad_optional_access&) {
            EVLOG_warning << "No connectorId provided for evse " << evse->id << "; cannot map to an OCPP1.6 connector";
            result = -1;
        } catch (const std::out_of_range&) {
            EVLOG_warning << "No OCPP1.6 connector mapping for evse " << evse->id << ", connector "
                          << (evse->connectorId ? std::to_string(evse->connectorId.value()) : "<none>");
            result = -1;
        }
    }

    return result;
}

inline std::int32_t ocpp_connector_id(const ocpp_multi::GenericChargePointInterface::ConnectorStructureV16& mapping,
                                      std::int32_t evse_id, std::optional<int32_t> connector_id) {
    const auto everest_connector_id = connector_id.value_or(1);
    return mapping.at(evse_id).at(everest_connector_id);
}

} // namespace

namespace ocpp_multi {

using namespace v16_conversions;

// ----------------------------------------------------------------------------
// OCPP 1.6 ChargePoint

void ChargePointV16::check_configured(const std::string_view& fn) {
    if (m_charge_point == nullptr) {
        std::string msg{"ChargePointV16 not configured: "};
        msg += fn;
        throw NotConfigured(msg);
    }
}

std::optional<ChargePointV16::evse_connector_t>
ChargePointV16::evse_from_ocpp_connector(std::int32_t ocpp_connector_id) const {
    const auto it = m_ocpp_connector_to_evse.find(ocpp_connector_id);
    if (it == m_ocpp_connector_to_evse.end()) {
        EVLOG_warning << "No EVSE mapping for OCPP1.6 connector " << ocpp_connector_id;
        return std::nullopt;
    }
    return it->second;
}

// ----------------------------------------------------------------------------
// callbacks from libocpp

void ChargePointV16::cb_boot_notification_response(
    const ocpp::v16::BootNotificationResponse& boot_notification_response) {
    const auto response = convert(boot_notification_response);
    m_callbacks_ptr->cb_boot_notification(response);
}

void ChargePointV16::cb_connection_state_changed(bool is_connected) {
    m_callbacks_ptr->cb_connection_state_changed(is_connected, ocpp::OcppProtocolVersion::v16);
}

ocpp::v16::DataTransferResponse ChargePointV16::cb_data_transfer(const ocpp::v16::DataTransferRequest& request) {
    const auto req = convert(request);
    return convert(m_callbacks_ptr->cb_data_transfer(req));
}

void ChargePointV16::cb_default_price(const ocpp::TariffMessage& message) {
    const auto default_price = ocpp_conversions::to_everest_default_price(message.message);
    m_callbacks_ptr->cb_default_price(default_price);
}

bool ChargePointV16::cb_disable_evse(std::int32_t ocpp_connector_id) {
    bool result{false};
    if (ocpp_connector_id > 0) {
        if (const auto evse = evse_from_ocpp_connector(ocpp_connector_id)) {
            // connector_id 0 addresses the whole EVSE
            result = m_callbacks_ptr->cb_connector_effective_operative_status(
                evse->evse_id, 0, ocpp::v2::OperationalStatusEnum::Inoperative);
        }
    }
    return result;
}

bool ChargePointV16::cb_enable_evse(std::int32_t ocpp_connector_id) {
    bool result{false};
    if (ocpp_connector_id > 0) {
        if (const auto evse = evse_from_ocpp_connector(ocpp_connector_id)) {
            // connector_id 0 addresses the whole EVSE
            result = m_callbacks_ptr->cb_connector_effective_operative_status(
                evse->evse_id, 0, ocpp::v2::OperationalStatusEnum::Operative);
        }
    }
    return result;
}

void ChargePointV16::cb_generic_configuration_key_changed(const ocpp::v16::KeyValue& key_value) {
    // convert to 2.x component/variable
    if (key_value.value) {
        ocpp::v2::SetVariableData data;
        bool send{false};
        if (key_value.key == CONNECTION_TIMEOUT_CONFIG_KEY) {
            data.component = {CONNECTION_TIMEOUT_COMPONENT};
            data.variable = {CONNECTION_TIMEOUT_VARIABLE};
            send = true;
        } else if (key_value.key == ISO15118_PNC_ENABLED_CONFIG_KEY) {
            data.component = {ISO15118_PNC_ENABLED_COMPONENT};
            data.variable = {ISO15118_PNC_ENABLED_VARIABLE};
            send = true;
        } else if (key_value.key == CENTRAL_CONTRACT_VALIDATION_ALLOWED_CONFIG_KEY) {
            data.component = {CENTRAL_CONTRACT_VALIDATION_ALLOWED_COMPONENT};
            data.variable = {CENTRAL_CONTRACT_VALIDATION_ALLOWED_VARIABLE};
            send = true;
        }

        if (send) {
            data.attributeValue = static_cast<std::string>(key_value.value.value());
            m_callbacks_ptr->cb_variable_set(data);
        }
    }
}

bool ChargePointV16::cb_is_reset_allowed(const ocpp::v16::ResetType& reset_type) {
    return m_callbacks_ptr->cb_is_reset_allowed(std::nullopt, convert(reset_type));
}

ocpp::ReservationCheckStatus ChargePointV16::cb_is_token_reserved_for_connector(const std::int32_t ocpp_connector_id,
                                                                                const std::string& id_token) {
    const auto evse = evse_from_ocpp_connector(ocpp_connector_id);
    if (!evse) {
        return ocpp::ReservationCheckStatus::NotReserved;
    }
    const ocpp::CiString<255> token{id_token};
    return m_callbacks_ptr->cb_is_reservation_for_token(evse->evse_id, token, std::nullopt);
}

void ChargePointV16::cb_provide_token(const std::string& id_token, std::vector<std::int32_t> referenced_connectors,
                                      bool prevalidated) {
    ocpp_multi::GenericChargePointCallbacks::IdToken token;
    token.token = {id_token, "Central"};
    token.prevalidated = prevalidated;
    // referenced_connectors holds OCPP1.6 connector ids; Auth expects evse ids
    std::vector<std::int32_t> evse_ids;
    for (const auto ocpp_connector_id : referenced_connectors) {
        if (const auto evse = evse_from_ocpp_connector(ocpp_connector_id)) {
            if (std::find(evse_ids.begin(), evse_ids.end(), evse->evse_id) == evse_ids.end()) {
                evse_ids.push_back(evse->evse_id);
            }
        }
    }
    token.connectors = std::move(evse_ids);
    m_callbacks_ptr->cb_provide_token(token);
}

ocpp::v16::ReservationStatus ChargePointV16::cb_reserve_now(std::int32_t reservation_id, std::int32_t ocpp_connector_id,
                                                            ocpp::DateTime expiryDate, ocpp::CiString<20> idTag,
                                                            std::optional<ocpp::CiString<20>> parent_id) {
    ocpp::v2::ReserveNowRequest request{};
    request.idToken = {idTag.get(), {}};
    request.id = reservation_id;
    if (ocpp_connector_id == 0) {
        // connector 0: reserve the whole charging station
        request.evseId = std::nullopt;
    } else if (const auto evse = evse_from_ocpp_connector(ocpp_connector_id)) {
        request.evseId = evse->evse_id;
    } else {
        return ocpp::v16::ReservationStatus::Rejected;
    }
    request.expiryDateTime = expiryDate;
    if (parent_id) {
        request.groupIdToken = {static_cast<std::string>(parent_id.value())};
    }
    return convert(m_callbacks_ptr->cb_reserve_now(request));
}

void ChargePointV16::cb_reset(const ocpp::v16::ResetType& reset_type) {
    m_callbacks_ptr->cb_reset(std::nullopt, convert(reset_type));
}

ocpp::v16::DataTransferResponse ChargePointV16::cb_session_cost(const ocpp::RunningCost& running_cost,
                                                                const std::uint32_t number_of_decimals) {
    m_callbacks_ptr->cb_set_running_cost(running_cost, number_of_decimals, std::nullopt);
    ocpp::v16::DataTransferResponse response;
    response.status = ocpp::v16::DataTransferStatus::Accepted;
    return response;
}

ocpp::v16::DataTransferResponse
ChargePointV16::cb_set_display_message(const std::vector<ocpp::DisplayMessage>& messages) {
    return convert(m_callbacks_ptr->cb_set_display_message(messages));
}

void ChargePointV16::cb_set_system_time(const std::string& system_time) {
    m_callbacks_ptr->cb_time_sync(ocpp::DateTime{system_time});
}

ocpp::v16::UpdateFirmwareStatusEnumType
ChargePointV16::cb_signed_update_firmware(const ocpp::v16::SignedUpdateFirmwareRequest msg) {
    return convert(m_callbacks_ptr->cb_update_firmware_request(convert(msg)));
}

bool ChargePointV16::cb_stop_transaction(std::int32_t ocpp_connector_id, ocpp::v16::Reason reason) {
    const auto evse = evse_from_ocpp_connector(ocpp_connector_id);
    if (!evse) {
        return false;
    }
    const auto stop_reason = conversions_v16::to_everest_stop_transaction_reason(reason);
    const auto res = m_callbacks_ptr->cb_stop_transaction(evse->evse_id, stop_reason);
    return res == ocpp::v2::RequestStartStopStatusEnum::Accepted;
}

ocpp::v16::DataTransferResponse ChargePointV16::cb_tariff_message(const ocpp::TariffMessage& message) {
    const auto msg = ocpp_conversions::to_everest_tariff_message(message);
    m_callbacks_ptr->cb_tariff_message(msg);
    ocpp::v16::DataTransferResponse response;
    response.status = ocpp::v16::DataTransferStatus::Accepted;
    return response;
}

void ChargePointV16::cb_transaction_started(const std::int32_t ocpp_connector_id, const std::string& session_id) {
    const auto evse = evse_from_ocpp_connector(ocpp_connector_id);
    if (!evse) {
        return;
    }
    ocpp::v2::TransactionEventRequest event;
    event.eventType = ocpp::v2::TransactionEventEnum::Started;
    event.evse = {evse->evse_id, evse->connector_id};
    event.transactionInfo.transactionId = session_id;
    // the numeric OCPP1.6 transaction id is not assigned yet at this point
    m_callbacks_ptr->cb_transaction_event(event, std::nullopt);
}

void ChargePointV16::cb_transaction_stopped(const std::int32_t ocpp_connector_id, const std::string& session_id,
                                            const std::int32_t transaction_id) {
    EVLOG_info << "Transaction stopped at OCPP1.6 connector: " << ocpp_connector_id << ", session_id: " << session_id;
    const auto evse = evse_from_ocpp_connector(ocpp_connector_id);
    if (!evse) {
        return;
    }
    ocpp::v2::TransactionEventRequest event;
    event.eventType = ocpp::v2::TransactionEventEnum::Ended;
    event.evse = {evse->evse_id, evse->connector_id};
    event.transactionInfo.transactionId = session_id;
    // event.seqNo = 0; seqNo does not exist in OCPP1.6
    m_callbacks_ptr->cb_transaction_event(event, std::to_string(transaction_id));
}

void ChargePointV16::cb_transaction_updated(const std::int32_t ocpp_connector_id, const std::string& session_id,
                                            const std::int32_t transaction_id,
                                            const ocpp::v16::IdTagInfo& id_tag_info) {
    const auto evse = evse_from_ocpp_connector(ocpp_connector_id);
    if (!evse) {
        return;
    }
    ocpp::v2::TransactionEventRequest event;
    event.eventType = ocpp::v2::TransactionEventEnum::Updated;
    event.evse = {evse->evse_id, evse->connector_id};
    event.transactionInfo.transactionId = session_id;
    // event.seqNo = 0; seqNo does not exist in OCPP1.6
    m_callbacks_ptr->cb_transaction_event(event, std::to_string(transaction_id));

    ocpp::v2::TransactionEventResponse event_response;
    if (id_tag_info.parentIdTag) {
        ocpp::v2::IdTokenInfo token;
        token.status = convert(id_tag_info.status);
        token.cacheExpiryDateTime = id_tag_info.expiryDate;
        token.groupIdToken = {static_cast<std::string>(id_tag_info.parentIdTag.value()), "ISO14443"};
        event_response.idTokenInfo = token;
    }

    m_callbacks_ptr->cb_transaction_event_response(event, event_response, std::to_string(transaction_id));
}

ocpp::v16::UnlockStatus ChargePointV16::cb_unlock_connector(std::int32_t ocpp_connector_id) {
    const auto evse = evse_from_ocpp_connector(ocpp_connector_id);
    if (!evse) {
        return ocpp::v16::UnlockStatus::NotSupported;
    }
    return (convert(m_callbacks_ptr->cb_unlock_connector(evse->evse_id, evse->connector_id)));
}

void ChargePointV16::cb_update_firmware(const ocpp::v16::UpdateFirmwareRequest msg) {
    const auto request = convert(msg);
    m_callbacks_ptr->cb_update_firmware_request(request);
}

ocpp::v16::GetLogResponse ChargePointV16::cb_upload_diagnostics(const ocpp::v16::GetDiagnosticsRequest& request) {
    types::system::UploadLogsRequest req{};

    req.location = request.location;
    req.retries = request.retries;
    req.retry_interval_s = request.retryInterval;
    req.oldest_timestamp = request.startTime;
    req.latest_timestamp = request.stopTime;

    return convert(m_callbacks_ptr->cb_get_log_request(req));
}

ocpp::v16::GetLogResponse ChargePointV16::cb_upload_logs(ocpp::v16::GetLogRequest request) {
    types::system::UploadLogsRequest req{};

    req.location = request.log.remoteLocation;
    req.retries = request.retries;
    req.retry_interval_s = request.retryInterval;
    req.oldest_timestamp = request.log.oldestTimestamp;
    req.latest_timestamp = request.log.latestTimestamp;
    req.type = ocpp::v16::conversions::log_enum_type_to_string(request.logType);
    req.request_id = request.requestId;

    return convert(m_callbacks_ptr->cb_get_log_request(req));
}

void ChargePointV16::cb_variable_listener(const ocpp::v16::KeyValue& key_value) {
    listener_t listener{nullptr};
    std::optional<std::pair<ocpp::v2::Component, ocpp::v2::Variable>> mapping;
    {
        auto monitors = m_variable_monitors.handle();
        listener = monitors->listener;
        const auto it = monitors->map.find(key_value.key);
        if (it != monitors->map.end()) {
            mapping = it->second;
        }
    }
    if (listener == nullptr) {
        EVLOG_warning << "cb_variable_listener: no listener configured";
    } else if (!mapping.has_value()) {
        EVLOG_warning << "cb_variable_listener: no mapping for " << key_value.key;
    } else if (key_value.value.has_value()) {
        // invoke outside the lock
        listener(mapping->first, mapping->second, key_value.value.value());
    }
}

ocpp::v16::ErrorInfo ChargePointV16::convert_error(const Everest::error::Error& error) {
    const auto& error_type = error.type;

    ocpp::v16::ErrorInfo result(error.uuid.uuid, ocpp::v16::ChargePointErrorCode::OtherError, false);
    result.timestamp = ocpp::DateTime(error.timestamp);
    bool incomplete{true};

    // MREC mapping
    const auto mrec_it =
        std::find_if(ocpp_module_common::v16::MREC_ERROR_MAP.begin(), ocpp_module_common::v16::MREC_ERROR_MAP.end(),
                     [error_type](const auto& entry) { return error_type.find(entry.first) != std::string::npos; });
    if (mrec_it != ocpp_module_common::v16::MREC_ERROR_MAP.end()) {
        // update the result
        result.error_code = mrec_it->second.first;
        result.vendor_id = ocpp_module_common::v16::CHARGE_X_MREC_VENDOR_ID;
        result.vendor_error_code = mrec_it->second.second;
        incomplete = false;
    }

    // OCPP mapping
    if (incomplete) {
        const auto ocpp_it =
            std::find_if(ocpp_module_common::v16::OCPP_ERROR_MAP.begin(), ocpp_module_common::v16::OCPP_ERROR_MAP.end(),
                         [error_type](const auto& entry) { return error_type.find(entry.first) != std::string::npos; });

        // is OCPP error
        if (ocpp_it != ocpp_module_common::v16::OCPP_ERROR_MAP.end()) {
            // update the result
            result.error_code = ocpp_it->second;
            incomplete = false;
        }
    }

    if (incomplete) {
        if (error_type == INOPERATIVE_ERROR_TYPE) {
            // update the result
            result.is_fault = true;
            result.info = "caused_by:" + error.message;
            result.vendor_id = error.vendor_id;
            result.vendor_error_code = error.description;
            incomplete = false;
        }
    }

    if (incomplete) {
        // default processing
        result.is_fault = default_is_fault(error);
        result.info = error.origin.to_string();
        result.vendor_id = error.message;
        result.vendor_error_code = default_vendor_error_code(error);
    }

    return result;
}

ocpp::v2::AuthorizeResponse ChargePointV16::validate_pnc(const types::authorization::ProvidedIdToken& provided_token) {
    std::string emaid = provided_token.id_token.value;
    std::optional<std::string> opt_certificate = provided_token.certificate;

    std::optional<std::vector<ocpp::v2::OCSPRequestData>> ocsp_request_data_opt;
    if (provided_token.iso15118CertificateHashData.has_value()) {
        ocsp_request_data_opt =
            module::conversions::to_ocpp_ocsp_request_data_vector(provided_token.iso15118CertificateHashData.value());
    }

    return m_charge_point->data_transfer_pnc_authorize(emaid, opt_certificate, ocsp_request_data_opt);
}

ocpp::v2::AuthorizeResponse
ChargePointV16::validate_standard(const types::authorization::ProvidedIdToken& provided_token) {
    ocpp::v2::AuthorizeResponse validation_result;

    const auto enhanced_id_tag_info =
        m_charge_point->authorize_id_token(ocpp::CiString<20>(provided_token.id_token.value));
    validation_result.idTokenInfo.status = convert(enhanced_id_tag_info.id_tag_info.status);
    validation_result.idTokenInfo.cacheExpiryDateTime = enhanced_id_tag_info.id_tag_info.expiryDate;

    if (enhanced_id_tag_info.id_tag_info.parentIdTag) {
        ocpp::v2::IdToken parent;
        parent.idToken = static_cast<std::string>(enhanced_id_tag_info.id_tag_info.parentIdTag.value());
        parent.type = "Central";
        validation_result.idTokenInfo.groupIdToken = std::move(parent);
    }

    if (enhanced_id_tag_info.tariff_message) {
        // this can be used as the TT field of the OCMF.
        const auto& messages = enhanced_id_tag_info.tariff_message->message;
        if (!messages.empty()) {
            ocpp::v2::MessageContent content;
            content.content = messages[0].message;
            content.format = ocpp::v2::MessageFormatEnum::ASCII;
            validation_result.idTokenInfo.personalMessage = std::move(content);
        }
    }

    return validation_result;
}

// ----------------------------------------------------------------------------
// setup/configuration

void ChargePointV16::configure_callbacks() {
    // directly supported
    m_charge_point->register_all_connectors_unavailable_callback(
        [this](auto&&... args) { return m_callbacks_ptr->cb_all_connectors_unavailable(args...); });
    m_charge_point->register_cancel_reservation_callback(
        [this](auto&&... args) { return m_callbacks_ptr->cb_cancel_reservation(args...); });
    m_charge_point->register_get_15118_ev_certificate_response_callback(
        [this](auto&&... args) { return m_callbacks_ptr->cb_get_15118_ev_certificate_response(args...); });
    m_charge_point->register_pause_charging_callback([this](std::int32_t ocpp_connector_id) {
        const auto evse = evse_from_ocpp_connector(ocpp_connector_id);
        return evse ? m_callbacks_ptr->cb_pause_charging(evse->evse_id) : false;
    });
    m_charge_point->register_resume_charging_callback([this](std::int32_t ocpp_connector_id) {
        const auto evse = evse_from_ocpp_connector(ocpp_connector_id);
        return evse ? m_callbacks_ptr->cb_resume_charging(evse->evse_id) : false;
    });
    m_charge_point->register_security_event_callback(
        [this](auto&&... args) { return m_callbacks_ptr->cb_security_event(args...); });
    m_charge_point->register_signal_set_charging_profiles_callback(
        [this](auto&&... args) { m_callbacks_ptr->cb_set_charging_profiles(args...); });

    // indirectly supported
    m_charge_point->register_stop_transaction_callback([this](auto&&... args) { return cb_stop_transaction(args...); });
    m_charge_point->register_unlock_connector_callback([this](auto&&... args) { return cb_unlock_connector(args...); });
    m_charge_point->register_reserve_now_callback([this](auto&&... args) { return cb_reserve_now(args...); });
    m_charge_point->register_upload_diagnostics_callback(
        [this](auto&&... args) { return cb_upload_diagnostics(args...); });
    m_charge_point->register_upload_logs_callback([this](auto&&... args) { return cb_upload_logs(args...); });
    m_charge_point->register_update_firmware_callback([this](auto&&... args) { return cb_update_firmware(args...); });
    m_charge_point->register_signed_update_firmware_callback(
        [this](auto&&... args) { return cb_signed_update_firmware(args...); });
    m_charge_point->register_provide_token_callback([this](auto&&... args) { cb_provide_token(args...); });
    m_charge_point->register_disable_evse_callback([this](auto&&... args) { return cb_disable_evse(args...); });
    m_charge_point->register_set_system_time_callback([this](auto&&... args) { return cb_set_system_time(args...); });
    m_charge_point->register_enable_evse_callback([this](auto&&... args) { return cb_enable_evse(args...); });
    m_charge_point->register_is_token_reserved_for_connector_callback(
        [this](auto&&... args) { return cb_is_token_reserved_for_connector(args...); });
    m_charge_point->register_is_reset_allowed_callback([this](auto&&... args) { return cb_is_reset_allowed(args...); });
    m_charge_point->register_reset_callback([this](auto&&... args) { return cb_reset(args...); });
    m_charge_point->register_connection_state_changed_callback(
        [this](auto&&... args) { return cb_connection_state_changed(args...); });
    m_charge_point->register_transaction_started_callback(
        [this](auto&&... args) { return cb_transaction_started(args...); });
    m_charge_point->register_transaction_updated_callback(
        [this](auto&&... args) { return cb_transaction_updated(args...); });
    m_charge_point->register_transaction_stopped_callback(
        [this](auto&&... args) { return cb_transaction_stopped(args...); });
    m_charge_point->register_boot_notification_response_callback(
        [this](auto&&... args) { return cb_boot_notification_response(args...); });
    m_charge_point->register_session_cost_callback([this](auto&&... args) { return cb_session_cost(args...); });
    m_charge_point->register_default_price_callback([this](auto&&... args) { cb_default_price(args...); });
    m_charge_point->register_tariff_message_callback([this](auto&&... args) { return cb_tariff_message(args...); });
    m_charge_point->register_set_display_message_callback(
        [this](auto&&... args) { return cb_set_display_message(args...); });
    m_charge_point->register_data_transfer_callback([this](auto&&... args) { return cb_data_transfer(args...); });
    m_charge_point->register_generic_configuration_key_changed_callback(
        [this](auto&&... args) { cb_generic_configuration_key_changed(args...); });
}

void ChargePointV16::configure_data_model(const config_info_t& config) {
    const auto ocpp_share_path = fs::path(config.ocpp_share_path);

    const module::config_factory_v16::Ocpp16DeviceModelParams params{
        config.device_model_database_path,           // DeviceModelDatabasePath
        config.device_model_database_migration_path, // DeviceModelDatabaseMigrationPath
        config.device_model_config_path,             // DeviceModelConfigPath
        config.device_model_config_mappings,         // DeviceModelConfigMappings
        config.ocpp16_network_config_slot,           // Ocpp16NetworkConfigSlot
        config.enable_legacy_config_migration,       // EnableLegacyConfigMigration
        config.chargepoint_config_path,              // ChargePointConfigPath
        config.user_config_path,                     // UserConfigPath
    };

    m_charge_point_configuration = module::config_factory_v16::create_charge_point_configuration(
        ocpp_share_path, params, static_cast<int32_t>(config.number_of_connectors));

    // The factory does not create the message-log directory; retain that here.
    if (!fs::exists(config.message_log_path)) {
        try {
            fs::create_directory(config.message_log_path);
        } catch (const fs::filesystem_error& e) {
            EVLOG_AND_THROW(e);
        }
    }
}

void ChargePointV16::init(init_args_t& args) {

    const auto ocpp_share_path = args.share_path / "OCPP";
    const auto sql_init_path = ocpp_share_path / SQL_CORE_MIGRATIONS;

    m_connector_mapping = std::move(args.connector_mapping);
    for (const auto& [evse_id, connectors] : m_connector_mapping) {
        for (const auto& [connector_id, ocpp_connector_id] : connectors) {
            m_ocpp_connector_to_evse[ocpp_connector_id] = {evse_id, connector_id};
        }
    }

    config_info_t config{
        args.v16_chargepoint_config_path,
        args.message_log_path,
        ocpp_share_path,
        sql_init_path,
        args.v16_user_config_path,
        static_cast<std::uint32_t>(args.evse_connector_structure.size()), // number of connectors;
        args.v2_device_model_database_path.string(),
        args.v2_device_model_database_migration_path.string(),
        args.v2_device_model_config_path.string(),
        args.v16_device_model_config_mappings,
        args.v16_ocpp16_network_config_slot,
        args.v16_enable_legacy_config_migration,
    };

    configure_data_model(config);

    // clang-format off
    m_charge_point = std::make_unique<ocpp::v16::ChargePoint>(
        *m_charge_point_configuration,
        ocpp_share_path,
        args.v16_database_path,
        sql_init_path,
        args.message_log_path,
        m_evse_security,
        std::nullopt,
        [this](auto &&...args){ m_callbacks_ptr->cb_ocpp_messages(args...);}
    );
    // clang-format on

    configure_callbacks();

    if (args.charger_info) {
        m_charge_point->update_chargepoint_information(
            args.charger_info->vendor, args.charger_info->model, args.charger_info->chargepoint_serial,
            args.charger_info->chargebox_serial, args.charger_info->firmware_version);
    }
}

// ----------------------------------------------------------------------------
// calls from the OCPP module

void ChargePointV16::connect_websocket() {
    check_configured("connect_websocket");
    m_charge_point->connect_websocket();
}
void ChargePointV16::disconnect_websocket() {
    check_configured("disconnect_websocket");
    m_charge_point->disconnect_websocket();
}
void ChargePointV16::set_message_queue_resume_delay(std::chrono::seconds delay) {
    check_configured("set_message_queue_resume_delay");
    m_charge_point->set_message_queue_resume_delay(delay);
}
bool ChargePointV16::restart() {
    check_configured("restart");
    return m_charge_point->restart();
}
void ChargePointV16::start(ocpp::v2::BootReasonEnum bootreason, const std::set<std::string>& resuming_session_ids,
                           bool start_connecting) {
    check_configured("start");
    m_charge_point->start({}, convert(bootreason), resuming_session_ids, start_connecting);
}
void ChargePointV16::stop() {
    check_configured("stop");
    m_charge_point->stop();
}

std::optional<ocpp::v2::DataTransferResponse>
ChargePointV16::data_transfer_req(const ocpp::v2::DataTransferRequest& request) {
    check_configured("data_transfer_req");
    const auto res = m_charge_point->data_transfer(request.vendorId, request.messageId, request.data);
    std::optional<ocpp::v2::DataTransferResponse> result;
    if (res) {
        ocpp::v2::DataTransferResponse response;
        response.status = convert(res.value().status);
        response.data = res.value().data;
        result = std::move(response);
    }
    return result;
}

std::optional<bool> ChargePointV16::get_central_contract_validation_allowed() {
    check_configured("get_central_contract_validation_allowed");
    return get<bool>(*m_charge_point, "CentralContractValidationAllowed");
}

std::optional<bool> ChargePointV16::get_contract_certificate_installation_enabled() {
    return {};
}

std::optional<bool> ChargePointV16::get_pnc_enabled() {
    check_configured("get_pnc_enabled");
    return get<bool>(*m_charge_point, "ISO15118PnCEnabled");
}

std::optional<std::int32_t> ChargePointV16::get_ev_connection_timeout() {
    check_configured("get_ev_connection_timeout");
    return get<std::int32_t>(*m_charge_point, "ConnectionTimeout");
}

std::optional<std::string> ChargePointV16::get_setpoint_priority() {
    return {};
}

std::optional<std::string> ChargePointV16::get_master_pass_group_id() {
    return {};
}

std::optional<std::string> ChargePointV16::get_tx_start_point() {
    return {};
}

std::optional<std::string> ChargePointV16::get_tx_stop_point() {
    return {};
}

std::vector<ocpp::v2::EnhancedCompositeSchedule>
ChargePointV16::get_all_composite_schedules(std::int32_t duration_s, ocpp::v2::ChargingRateUnitEnum unit) {
    check_configured("get_all_composite_schedules");
    const auto res = m_charge_point->get_all_enhanced_composite_charging_schedules(duration_s, convert(unit));
    std::vector<ocpp::v2::EnhancedCompositeSchedule> result;
    result.reserve(res.size());
    for (const auto& entry : res) {
        result.push_back(convert(entry.first, entry.second));
    }
    return result;
}

std::vector<ocpp::v2::GetVariableResult>
ChargePointV16::get_variables(const std::vector<ocpp::v2::GetVariableData>& get_variable_data_vector) {
    check_configured("get_variables");
    ocpp::v16::GetConfigurationRequest request;
    if (!get_variable_data_vector.empty()) {
        std::vector<ocpp::CiString<50>> keys;
        keys.reserve(get_variable_data_vector.size());
        for (const auto& item : get_variable_data_vector) {
            keys.push_back(item.variable.name);
        }
        request.key = std::move(keys);
    }

    const auto response = m_charge_point->get_configuration_key(request);

    std::vector<ocpp::v2::GetVariableResult> result;

    if (!get_variable_data_vector.empty()) {
        result.reserve(get_variable_data_vector.size());
        // maintain the order
        for (const auto& item : get_variable_data_vector) {
            result.push_back({ocpp::v2::GetVariableStatusEnum::UnknownVariable, {}, {item.variable.name}});
        }
    }

    // populate values - maintaining the order
    if (response.configurationKey) {
        for (const auto& item : response.configurationKey.value()) {
            auto it = std::find_if(result.begin(), result.end(),
                                   [item](const auto& entry) { return entry.variable.name == item.key; });
            if (it != result.end()) {
                if (item.value) {
                    it->attributeValue = item.value.value().get();
                }
                it->attributeStatus = ocpp::v2::GetVariableStatusEnum::Accepted;
                it->attributeType = ocpp::v2::AttributeEnum::Actual;
            }
        }
    }

    return result;
}

void ChargePointV16::on_authorized(std::int32_t evse_id, std::int32_t connector_id, const ocpp::v2::IdToken& id_token) {
    // not used in OCPP 1.6
}

ocpp::v2::ChangeAvailabilityResponse
ChargePointV16::on_change_availability(const ocpp::v2::ChangeAvailabilityRequest& request) {
    check_configured("on_change_availability");
    ocpp::v2::ChangeAvailabilityResponse result;
    result.status = ocpp::v2::ChangeAvailabilityStatusEnum::Rejected;
    ocpp::v16::ChangeAvailabilityRequest req;
    req.type = convert(request.operationalStatus);

    req.connectorId = get_ocpp_connector_id(m_connector_mapping, request.evse);

    if (req.connectorId >= 0) {
        const auto res = m_charge_point->on_change_availability(req);
        result.status = convert(res.status);
    } else {
        result.statusInfo = {"InvalidInput",
                             "Could not determine OCPP connector id from provided EVerest EVSE and Connector Ids."};
    }
    return result;
}

void ChargePointV16::on_enabled(std::int32_t evse_id, std::int32_t connector_id) {
    check_configured("on_enabled");
    m_charge_point->on_enabled(evse_id);
}

void ChargePointV16::on_ev_charging_needs(const ocpp::v2::NotifyEVChargingNeedsRequest& request) {
    // not used in OCPP 1.6
}

void ChargePointV16::on_event(const EventInfo& event) {
    check_configured("on_event");
    if (event.error) {
        if (event.event_cleared) {
            m_charge_point->on_error_cleared(event.evse_id, event.error->uuid.uuid);
        } else {
            const auto error_info = convert_error(event.error.value());
            m_charge_point->on_error(event.evse_id, error_info);
        }
    }
}

void ChargePointV16::on_event_authorised(std::int32_t evse_id, std::int32_t connector_id,
                                         const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_event_authorised");
}
void ChargePointV16::on_event_deauthorised(std::int32_t evse_id, std::int32_t connector_id,
                                           const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_event_deauthorised");
}
void ChargePointV16::on_event_disabled(std::int32_t evse_id, std::int32_t connector_id,
                                       const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_event_disabled");
    m_charge_point->on_disabled(evse_id);
}
void ChargePointV16::on_event_enabled(std::int32_t evse_id, std::int32_t connector_id,
                                      const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_event_enabled");
    m_charge_point->on_enabled(evse_id);
}
void ChargePointV16::on_event_charging_paused_ev(std::int32_t evse_id, std::int32_t connector_id,
                                                 const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_event_charging_paused_ev");
    const auto cid = ocpp_connector_id(m_connector_mapping, evse_id, session_event.connector_id);
    m_charge_point->on_suspend_charging_ev(cid);
}
void ChargePointV16::on_event_charging_paused_evse(std::int32_t evse_id, std::int32_t connector_id,
                                                   const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_event_charging_paused_evse");
    const auto cid = ocpp_connector_id(m_connector_mapping, evse_id, session_event.connector_id);
    m_charge_point->on_suspend_charging_evse(cid);
}
void ChargePointV16::on_event_charging_started(std::int32_t evse_id, std::int32_t connector_id,
                                               const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_event_charging_started");
    const auto cid = ocpp_connector_id(m_connector_mapping, evse_id, session_event.connector_id);
    m_charge_point->on_resume_charging(cid);
}
void ChargePointV16::on_event_plugin_timeout(std::int32_t evse_id, std::int32_t connector_id,
                                             const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_event_plugin_timeout");
    const auto cid = ocpp_connector_id(m_connector_mapping, evse_id, session_event.connector_id);
    m_charge_point->on_plugin_timeout(cid);
}
void ChargePointV16::on_event_reservation_end(std::int32_t evse_id, std::int32_t connector_id,
                                              const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_event_reservation_end");
    const auto cid = ocpp_connector_id(m_connector_mapping, evse_id, session_event.connector_id);
    m_charge_point->on_reservation_end(cid);
}
void ChargePointV16::on_event_reservation_start(std::int32_t evse_id, std::int32_t connector_id,
                                                const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_event_reservation_start");
    const auto cid = ocpp_connector_id(m_connector_mapping, evse_id, session_event.connector_id);
    m_charge_point->on_reservation_start(cid);
}
void ChargePointV16::on_event_session_finished(std::int32_t evse_id, std::int32_t connector_id,
                                               const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_event_session_finished");
    const auto cid = ocpp_connector_id(m_connector_mapping, evse_id, session_event.connector_id);
    m_charge_point->on_session_stopped(cid, session_event.uuid);
}
void ChargePointV16::on_event_session_resumed(std::int32_t evse_id, std::int32_t connector_id,
                                              const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_event_session_resumed");
}
GenericChargePointInterface::SessionResult
ChargePointV16::on_event_session_started(std::int32_t evse_id, std::int32_t connector_id,
                                         const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_event_session_started");
    if (!session_event.session_started.has_value()) {
        throw std::runtime_error("SessionEvent SessionStarted does not contain session_started context");
    }
    const auto session_started = session_event.session_started.value();
    const auto cid = ocpp_connector_id(m_connector_mapping, evse_id, session_event.connector_id);
    m_charge_point->on_session_started(cid, session_event.uuid,
                                       conversions_v16::to_ocpp_session_started_reason(session_started.reason),
                                       session_started.logging_path);
    return {};
}
void ChargePointV16::on_event_switching_phases(std::int32_t evse_id, std::int32_t connector_id,
                                               const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_event_switching_phases");
    const auto cid = ocpp_connector_id(m_connector_mapping, evse_id, session_event.connector_id);
    m_charge_point->on_suspend_charging_evse(cid, SWITCHING_PHASES_REASON);
}
void ChargePointV16::on_event_transaction_finished(std::int32_t evse_id, std::int32_t connector_id,
                                                   const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_event_transaction_finished");
    if (!session_event.transaction_finished.has_value()) {
        throw std::runtime_error("SessionEvent TransactionFinished does not contain transaction_finished context");
    }
    const auto transaction_finished = session_event.transaction_finished.value();
    const auto timestamp = ocpp_conversions::to_ocpp_datetime_or_now(session_event.timestamp);
    const auto energy_Wh_import = transaction_finished.meter_value.energy_Wh_import.total;
    const auto signed_meter_value = transaction_finished.signed_meter_value;

    auto reason = ocpp::v16::Reason::Other;
    if (transaction_finished.reason.has_value()) {
        reason = conversions_v16::to_ocpp_reason(transaction_finished.reason.value());
    }
    std::optional<ocpp::CiString<20>> id_tag_opt = std::nullopt;
    if (transaction_finished.id_tag.has_value()) {
        // we truncate potentially too large tokens
        id_tag_opt.emplace(
            ocpp::CiString<20>(transaction_finished.id_tag.value().id_token.value, ocpp::StringTooLarge::Truncate));
    }
    std::optional<std::string> signed_meter_data;
    if (signed_meter_value.has_value()) {
        // there is no specified way of transmitting signing method, encoding
        // method and public key this has to be negotiated beforehand or done in a
        // custom data transfer
        signed_meter_data.emplace(signed_meter_value.value().signed_meter_data);
    }
    std::optional<std::string> start_signed_meter_data;
    if (transaction_finished.start_signed_meter_value.has_value()) {
        start_signed_meter_data.emplace(transaction_finished.start_signed_meter_value.value().signed_meter_data);
    }
    const auto cid = ocpp_connector_id(m_connector_mapping, evse_id, session_event.connector_id);
    m_charge_point->on_transaction_stopped(cid, session_event.uuid, reason, timestamp, energy_Wh_import, id_tag_opt,
                                           signed_meter_data, start_signed_meter_data);
}

GenericChargePointInterface::SessionResult
ChargePointV16::on_event_transaction_started(std::int32_t evse_id, std::int32_t connector_id,
                                             const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_event_transaction_started");
    if (!session_event.transaction_started.has_value()) {
        throw std::runtime_error("SessionEvent TransactionStarted does not contain transaction_started context");
    }
    const auto transaction_started = session_event.transaction_started.value();

    const auto timestamp = ocpp_conversions::to_ocpp_datetime_or_now(session_event.timestamp);
    const auto energy_Wh_import = transaction_started.meter_value.energy_Wh_import.total;
    const auto session_id = session_event.uuid;
    const auto id_token = transaction_started.id_tag.id_token.value;
    const auto signed_meter_value = transaction_started.signed_meter_value;
    std::optional<int32_t> reservation_id_opt = std::nullopt;
    if (transaction_started.reservation_id) {
        reservation_id_opt.emplace(transaction_started.reservation_id.value());
    }
    std::optional<std::string> signed_meter_data;
    if (signed_meter_value.has_value()) {
        // there is no specified way of transmitting signing method, encoding
        // method and public key this has to be negotiated beforehand or done in a
        // custom data transfer
        signed_meter_data.emplace(signed_meter_value.value().signed_meter_data);
    }
    const auto cid = ocpp_connector_id(m_connector_mapping, evse_id, session_event.connector_id);
    m_charge_point->on_transaction_started(cid, session_event.uuid, id_token, energy_Wh_import, reservation_id_opt,
                                           timestamp, signed_meter_data);
    return {};
}

void ChargePointV16::on_fault_cleared(std::int32_t evse_id, std::int32_t connector_id) {
    // not used in OCPP 1.6
}

void ChargePointV16::on_faulted(std::int32_t evse_id, std::int32_t connector_id) {
    // not used in OCPP 1.6
}

void ChargePointV16::on_firmware_update_status_notification(std::int32_t request_id,
                                                            ocpp::v2::FirmwareStatusEnum firmware_update_status,
                                                            bool disable_connectors_during_install) {
    check_configured("on_firmware_update_status_notification");
    m_charge_point->on_firmware_update_status_notification(request_id, convert(firmware_update_status),
                                                           disable_connectors_during_install);
}

void ChargePointV16::on_get_15118_ev_certificate_request(std::int32_t extensions_id,
                                                         const ocpp::v2::Get15118EVCertificateRequest& request) {
    check_configured("on_get_15118_ev_certificate_request");
    m_charge_point->data_transfer_pnc_get_15118_ev_certificate(extensions_id, request.exiRequest,
                                                               request.iso15118SchemaVersion, request.action);
}

void ChargePointV16::on_log_status_notification(ocpp::v2::UploadLogStatusEnum status, std::int32_t requestId) {
    check_configured("on_log_status_notification");
    // the enum and strings for OCPP 1.6 are the same as OCPP 2.x
    m_charge_point->on_log_status_notification(requestId,
                                               ocpp::v2::conversions::upload_log_status_enum_to_string(status));
}

void ChargePointV16::on_meter_value(std::int32_t evse_id, std::optional<float> soc,
                                    const types::powermeter::Powermeter& power_meter) {
    check_configured("on_meter_value");
    ocpp::Measurement measurement;
    measurement.power_meter = conversions_v16::to_ocpp_power_meter(power_meter);
    if (soc) {
        // soc is present, so add this to the measurement
        measurement.soc_Percent = ocpp::StateOfCharge{soc.value()};
    }
    if (power_meter.temperatures.has_value()) {
        measurement.temperature_C = conversions_v16::to_ocpp_temperatures(power_meter.temperatures.value());
    }
    m_charge_point->on_meter_values(evse_id, measurement);
}

void ChargePointV16::on_reservation_status(std::int32_t reservation_id, ocpp::v2::ReservationUpdateStatusEnum status) {
    // not used in OCPP 1.6
}

void ChargePointV16::on_security_event(const ocpp::CiString<50>& event_type,
                                       const std::optional<ocpp::CiString<255>>& tech_info,
                                       const std::optional<bool>& critical,
                                       const std::optional<ocpp::DateTime>& timestamp) {
    check_configured("on_security_event");
    m_charge_point->on_security_event(event_type, tech_info, critical, timestamp);
}

void ChargePointV16::on_unavailable(std::int32_t evse_id, std::int32_t connector_id) {
    check_configured("on_unavailable");
    m_charge_point->on_disabled(evse_id);
}

void ChargePointV16::register_variable_listener(const ocpp::v2::Component& component,
                                                const ocpp::v2::Variable& variable, listener_t listener) {
    check_configured("register_variable_listener");
    const std::string key = variable.name;
    if (key.empty()) {
        return;
    }
    {
        auto monitors = m_variable_monitors.handle();
        if (monitors->listener == nullptr && listener != nullptr) {
            monitors->listener = std::move(listener);
        }
        monitors->map.insert_or_assign(key, std::make_pair(component, variable));
    }
    // register outside the lock: libocpp may fire the callback synchronously
    m_charge_point->register_configuration_key_changed_callback(
        key, [this](auto&&... args) { cb_variable_listener(args...); });
}

bool ChargePointV16::set_powermeter_public_key(std::int32_t connector, const std::string& public_key_pem) {
    check_configured("set_powermeter_public_key");
    return m_charge_point->set_powermeter_public_key(connector, public_key_pem);
}

std::vector<ocpp::v2::SetVariableResult>
ChargePointV16::set_variables(const std::vector<ocpp::v2::SetVariableData>& set_variable_data_vector,
                              const std::string& source) {
    check_configured("set_variables");

    std::vector<ocpp::v2::SetVariableResult> result;

    for (const auto& item : set_variable_data_vector) {
        ocpp::v2::SetVariableResult set_result;
        set_result.component = item.component;
        set_result.variable = item.variable;
        // set_result.attributeType = item.attributeType;
        // set_result.customData = item.customData;

        ocpp::CiString<500> value = static_cast<std::string>(item.attributeValue);
        const auto response = m_charge_point->set_configuration_key(item.variable.name, value);

        switch (response) {
        case ocpp::v16::ConfigurationStatus::Accepted:
            set_result.attributeStatus = ocpp::v2::SetVariableStatusEnum::Accepted;
            break;
        case ocpp::v16::ConfigurationStatus::RebootRequired:
            set_result.attributeStatus = ocpp::v2::SetVariableStatusEnum::RebootRequired;
            break;
        case ocpp::v16::ConfigurationStatus::NotSupported:
            // NotSupported in OCPP1.6 means that the configuration key is not known / not supported, so it's best
            // to go with UnknownVariable
            set_result.attributeStatus = ocpp::v2::SetVariableStatusEnum::UnknownVariable;
            break;
        case ocpp::v16::ConfigurationStatus::Rejected:
        default:
            set_result.attributeStatus = ocpp::v2::SetVariableStatusEnum::Rejected;
            break;
        }

        result.push_back(std::move(set_result));
    }

    return result;
}

ocpp::v2::AuthorizeResponse
ChargePointV16::validate_token(const types::authorization::ProvidedIdToken& provided_token) {
    check_configured("validate_token");

    ocpp::v2::AuthorizeResponse validation_result;

    try {
        if (provided_token.authorization_type == types::authorization::AuthorizationType::PlugAndCharge) {
            validation_result = validate_pnc(provided_token);
        } else {
            validation_result = validate_standard(provided_token);
        }
    } catch (const ocpp::StringConversionException& e) {
        EVLOG_warning << "Error converting id token to validate: " << e.what();
        validation_result.idTokenInfo.status = ocpp::v2::AuthorizationStatusEnum::Unknown;
    } catch (const std::exception& e) {
        EVLOG_warning << "Unknown error during validation of id token: " << e.what();
        validation_result.idTokenInfo.status = ocpp::v2::AuthorizationStatusEnum::Unknown;
    }

    return validation_result;
}

bool ChargePointV16::default_is_fault(const Everest::error::Error& error) {
    return false;
}

std::string ChargePointV16::default_vendor_error_code(const Everest::error::Error& error) {
    std::string result;

    // this function should return everything after the first '/'
    // delimiter - if there is no delimiter or the delimiter is at
    // the end, it should return the input itself

    auto npos = error.type.find('/');
    if (npos == std::string::npos) {
        result = error.type;
    } else {
        result = error.type.substr(npos + 1);
    }

    result.push_back('/');
    result += error.sub_type;
    return result;
}

} // namespace ocpp_multi
