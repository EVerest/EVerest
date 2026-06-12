// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "v16_chargepoint.hpp"
#include "generic_chargepoint_interface.hpp"
#include "ocpp/common/types.hpp"
#include "ocpp/v16/ocpp_enums.hpp"
#include "ocpp/v16/types.hpp"
#include "ocpp/v2/messages/TransactionEvent.hpp"
#include "ocpp/v2/messages/UpdateFirmware.hpp"
#include "ocpp/v2/ocpp_enums.hpp"
#include "ocpp/v2/ocpp_types.hpp"

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

void create_empty_user_config(const fs::path& user_config_path) {
    if (fs::exists(user_config_path.parent_path())) {
        std::ofstream fs(user_config_path.c_str());
        auto user_config = json::object();
        fs << user_config << std::endl;
        fs.close();
    } else {
        EVLOG_AND_THROW(
            std::runtime_error(fmt::format("Provided UserConfigPath is invalid: {}", user_config_path.string())));
    }
}

auto convert(ocpp::v16::AuthorizationStatus value) {
    ocpp::v2::AuthorizationStatusEnum result{};
    switch (value) {
    case ocpp::v16::AuthorizationStatus::Accepted:
        result = ocpp::v2::AuthorizationStatusEnum::Accepted;
        break;
    case ocpp::v16::AuthorizationStatus::Blocked:
        result = ocpp::v2::AuthorizationStatusEnum::Blocked;
        break;
    case ocpp::v16::AuthorizationStatus::Expired:
        result = ocpp::v2::AuthorizationStatusEnum::Expired;
        break;
    case ocpp::v16::AuthorizationStatus::Invalid:
        result = ocpp::v2::AuthorizationStatusEnum::Invalid;
        break;
    case ocpp::v16::AuthorizationStatus::ConcurrentTx:
        result = ocpp::v2::AuthorizationStatusEnum::ConcurrentTx;
        break;
    default:
        result = ocpp::v2::AuthorizationStatusEnum::Unknown;
        break;
    }
    return result;
}

auto convert(const ocpp::v16::BootNotificationResponse& value) {
    ocpp::v2::BootNotificationResponse result{};
    result.currentTime = value.currentTime;
    result.interval = value.interval;
    switch (value.status) {
    case ocpp::v16::RegistrationStatus::Accepted:
        result.status = ocpp::v2::RegistrationStatusEnum::Accepted;
        break;
    case ocpp::v16::RegistrationStatus::Pending:
        result.status = ocpp::v2::RegistrationStatusEnum::Pending;
        break;
    case ocpp::v16::RegistrationStatus::Rejected:
    default:
        result.status = ocpp::v2::RegistrationStatusEnum::Rejected;
        break;
    }
    return result;
}

auto convert(ocpp::v2::BootReasonEnum value) {
    ocpp::v16::BootReasonEnum result{};
    switch (value) {
    case ocpp::v2::BootReasonEnum::ApplicationReset:
        result = ocpp::v16::BootReasonEnum::ApplicationReset;
        break;
    case ocpp::v2::BootReasonEnum::FirmwareUpdate:
        result = ocpp::v16::BootReasonEnum::FirmwareUpdate;
        break;
    case ocpp::v2::BootReasonEnum::LocalReset:
        result = ocpp::v16::BootReasonEnum::LocalReset;
        break;
    case ocpp::v2::BootReasonEnum::PowerUp:
        result = ocpp::v16::BootReasonEnum::PowerUp;
        break;
    case ocpp::v2::BootReasonEnum::RemoteReset:
        result = ocpp::v16::BootReasonEnum::RemoteReset;
        break;
    case ocpp::v2::BootReasonEnum::ScheduledReset:
        result = ocpp::v16::BootReasonEnum::ScheduledReset;
        break;
    case ocpp::v2::BootReasonEnum::Triggered:
        result = ocpp::v16::BootReasonEnum::Triggered;
        break;
    case ocpp::v2::BootReasonEnum::Watchdog:
        result = ocpp::v16::BootReasonEnum::Watchdog;
        break;
    case ocpp::v2::BootReasonEnum::Unknown:
    default:
        result = ocpp::v16::BootReasonEnum::Unknown;
        break;
    }
    return result;
}

auto convert(const ocpp::v16::DataTransferRequest& value) {
    ocpp::v2::DataTransferRequest result{};
    result.vendorId = value.vendorId;
    result.messageId = value.messageId;
    result.data = value.data;
    return result;
}

auto convert(const ocpp::v2::DataTransferResponse& value) {
    ocpp::v16::DataTransferResponse result{};
    switch (value.status) {
    case ocpp::v2::DataTransferStatusEnum::Accepted:
        result.status = ocpp::v16::DataTransferStatus::Accepted;
        break;
    case ocpp::v2::DataTransferStatusEnum::Rejected:
        result.status = ocpp::v16::DataTransferStatus::Rejected;
        break;
    case ocpp::v2::DataTransferStatusEnum::UnknownMessageId:
        result.status = ocpp::v16::DataTransferStatus::UnknownMessageId;
        break;
    case ocpp::v2::DataTransferStatusEnum::UnknownVendorId:
        result.status = ocpp::v16::DataTransferStatus::UnknownVendorId;
        break;
    }
    result.data = value.data;
    return result;
}

auto convert(const ocpp::v2::GetLogResponse& value) {
    ocpp::v16::GetLogResponse result{};

    switch (value.status) {
    case ocpp::v2::LogStatusEnum::Accepted:
        result.status = ocpp::v16::LogStatusEnumType::Accepted;
        break;
    case ocpp::v2::LogStatusEnum::AcceptedCanceled:
        result.status = ocpp::v16::LogStatusEnumType::AcceptedCanceled;
        break;
    case ocpp::v2::LogStatusEnum::Rejected:
    default:
        result.status = ocpp::v16::LogStatusEnumType::Rejected;
        break;
    }

    result.filename = value.filename;
    return result;
}

auto convert(ocpp::v16::LogEnumType value) {
    ocpp::v2::LogEnum result{};
    switch (value) {
    case ocpp::v16::LogEnumType::SecurityLog:
        result = ocpp::v2::LogEnum::SecurityLog;
        break;
    case ocpp::v16::LogEnumType::DiagnosticsLog:
    default:
        result = ocpp::v2::LogEnum::DiagnosticsLog;
        break;
    }
    return result;
}

auto convert(ocpp::v16::Reason value) {
    ocpp::v2::ReasonEnum result{};
    switch (value) {
    case ocpp::v16::Reason::EmergencyStop:
        result = ocpp::v2::ReasonEnum::EmergencyStop;
        break;
    case ocpp::v16::Reason::EVDisconnected:
        result = ocpp::v2::ReasonEnum::EVDisconnected;
        break;
    case ocpp::v16::Reason::HardReset:
    case ocpp::v16::Reason::SoftReset:
    case ocpp::v16::Reason::Reboot:
        result = ocpp::v2::ReasonEnum::Reboot;
        break;
    case ocpp::v16::Reason::Local:
        result = ocpp::v2::ReasonEnum::Local;
        break;
    case ocpp::v16::Reason::PowerLoss:
        result = ocpp::v2::ReasonEnum::PowerLoss;
        break;
    case ocpp::v16::Reason::Remote:
        result = ocpp::v2::ReasonEnum::Remote;
        break;
    case ocpp::v16::Reason::DeAuthorized:
        result = ocpp::v2::ReasonEnum::DeAuthorized;
        break;
    case ocpp::v16::Reason::UnlockCommand:
    case ocpp::v16::Reason::Other:
    default:
        result = ocpp::v2::ReasonEnum::Other;
        break;
    }
    return result;
}

auto convert(ocpp::v2::ReserveNowStatusEnum value) {
    ocpp::v16::ReservationStatus result{};
    switch (value) {
    case ocpp::v2::ReserveNowStatusEnum::Accepted:
        result = ocpp::v16::ReservationStatus::Accepted;
        break;
    case ocpp::v2::ReserveNowStatusEnum::Faulted:
        result = ocpp::v16::ReservationStatus::Faulted;
        break;
    case ocpp::v2::ReserveNowStatusEnum::Occupied:
        result = ocpp::v16::ReservationStatus::Occupied;
        break;
    case ocpp::v2::ReserveNowStatusEnum::Unavailable:
        result = ocpp::v16::ReservationStatus::Unavailable;
        break;
    case ocpp::v2::ReserveNowStatusEnum::Rejected:
    default:
        result = ocpp::v16::ReservationStatus::Rejected;
        break;
    }
    return result;
}

auto convert(ocpp::v16::ResetType value) {
    using ResetType = ocpp_multi::GenericChargePointCallbacks::ResetType;
    ResetType result{};
    switch (value) {
    case ocpp::v16::ResetType::Hard:
        result = ResetType::Hard;
        break;
    default:
    case ocpp::v16::ResetType::Soft:
        result = ResetType::Soft;
        break;
    }
    return result;
}

auto convert(const ocpp::v2::SetDisplayMessageResponse& value) {
    ocpp::v16::DataTransferResponse result{};

    switch (value.status) {
    case ocpp::v2::DisplayMessageStatusEnum::Accepted:
        result.status = ocpp::v16::DataTransferStatus::Accepted;
        break;
    case ocpp::v2::DisplayMessageStatusEnum::NotSupportedMessageFormat:
    case ocpp::v2::DisplayMessageStatusEnum::LanguageNotSupported:
    case ocpp::v2::DisplayMessageStatusEnum::NotSupportedPriority:
    case ocpp::v2::DisplayMessageStatusEnum::NotSupportedState:
    case ocpp::v2::DisplayMessageStatusEnum::UnknownTransaction:
    case ocpp::v2::DisplayMessageStatusEnum::Rejected:
    default:
        result.status = ocpp::v16::DataTransferStatus::Rejected;
        break;
    }
    if (value.statusInfo) {
        result.data = value.statusInfo->reasonCode;
    }
    return result;
}

auto convert(const ocpp::v2::UnlockConnectorResponse& value) {
    ocpp::v16::UnlockStatus result{};
    switch (value.status) {
    case ocpp::v2::UnlockStatusEnum::Unlocked:
        result = ocpp::v16::UnlockStatus::Unlocked;
        break;
    case ocpp::v2::UnlockStatusEnum::UnlockFailed:
        result = ocpp::v16::UnlockStatus::UnlockFailed;
        break;
    case ocpp::v2::UnlockStatusEnum::OngoingAuthorizedTransaction:
    case ocpp::v2::UnlockStatusEnum::UnknownConnector:
    default:
        result = ocpp::v16::UnlockStatus::NotSupported;
        break;
    }

    return result;
}

auto convert(const ocpp::v16::UpdateFirmwareRequest& value) {
    ocpp::v2::UpdateFirmwareRequest result{};
    ocpp::v2::Firmware firmware;
    firmware.location = value.location;
    firmware.retrieveDateTime = value.retrieveDate;
    result.requestId = -1;
    result.firmware = firmware;
    result.retries = value.retries;
    result.retryInterval = value.retryInterval;
    return result;
}

auto convert(const ocpp::v16::SignedUpdateFirmwareRequest& value) {
    ocpp::v2::UpdateFirmwareRequest request{};
    request.requestId = value.requestId;
    request.firmware.location = static_cast<std::string>(value.firmware.location);
    request.firmware.retrieveDateTime = value.firmware.retrieveDateTime;
    request.firmware.signingCertificate = value.firmware.signingCertificate;
    request.firmware.signature = value.firmware.signature;
    request.firmware.installDateTime = value.firmware.installDateTime;
    request.retries = value.retries;
    request.retryInterval = value.retryInterval;
    return request;
}

auto convert(const ocpp::v2::UpdateFirmwareResponse& value) {
    ocpp::v16::UpdateFirmwareStatusEnumType result{};
    switch (value.status) {
    case ocpp::v2::UpdateFirmwareStatusEnum::Accepted:
        result = ocpp::v16::UpdateFirmwareStatusEnumType::Accepted;
        break;
    case ocpp::v2::UpdateFirmwareStatusEnum::AcceptedCanceled:
        result = ocpp::v16::UpdateFirmwareStatusEnumType::AcceptedCanceled;
        break;
    case ocpp::v2::UpdateFirmwareStatusEnum::InvalidCertificate:
        result = ocpp::v16::UpdateFirmwareStatusEnumType::InvalidCertificate;
        break;
    case ocpp::v2::UpdateFirmwareStatusEnum::RevokedCertificate:
        result = ocpp::v16::UpdateFirmwareStatusEnumType::RevokedCertificate;
        break;
    case ocpp::v2::UpdateFirmwareStatusEnum::Rejected:
    default:
        result = ocpp::v16::UpdateFirmwareStatusEnumType::Rejected;
        break;
    }
    return result;
}

} // namespace

namespace ocpp_multi {

// ----------------------------------------------------------------------------
// OCPP 1.6 ChargePoint

void ChargePointV16::check_configured(const std::string_view& fn) {
    if (m_charge_point == nullptr) {
        std::string msg{"ChargePointV16 not configured: "};
        msg += fn;
        throw NotConfigured(msg);
    }
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

bool ChargePointV16::cb_disable_evse(std::int32_t connector) {
    bool result{false};
    if (connector > 0) {
        result = m_callbacks_ptr->cb_connector_effective_operative_status(-1, connector,
                                                                          ocpp::v2::OperationalStatusEnum::Inoperative);
    }
    return result;
}

bool ChargePointV16::cb_enable_evse(std::int32_t connector) {
    bool result{false};
    if (connector > 0) {
        result = m_callbacks_ptr->cb_connector_effective_operative_status(-1, connector,
                                                                          ocpp::v2::OperationalStatusEnum::Operative);
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

void ChargePointV16::cb_get_15118_ev_certificate_response(
    const std::int32_t connector, const ocpp::v2::Get15118EVCertificateResponse& certificate_response,
    const ocpp::v2::CertificateActionEnum& certificate_action) {
}

bool ChargePointV16::cb_is_reset_allowed(const ocpp::v16::ResetType& reset_type) {
    return m_callbacks_ptr->cb_is_reset_allowed(std::nullopt, convert(reset_type));
}

ocpp::ReservationCheckStatus ChargePointV16::cb_is_token_reserved_for_connector(const std::int32_t connector,
                                                                                const std::string& id_token) {
    const ocpp::CiString<255> token{id_token};
    return m_callbacks_ptr->cb_is_reservation_for_token(connector, token, std::nullopt);
}

void ChargePointV16::cb_provide_token(const std::string& id_token, std::vector<std::int32_t> referenced_connectors,
                                      bool prevalidated) {
    ocpp_multi::GenericChargePointCallbacks::IdToken token;
    token.token = {id_token, "Central"};
    token.prevalidated = prevalidated;
    token.connectors = std::move(referenced_connectors);
    m_callbacks_ptr->cb_provide_token(token);
}

ocpp::v16::ReservationStatus ChargePointV16::cb_reserve_now(std::int32_t reservation_id, std::int32_t connector,
                                                            ocpp::DateTime expiryDate, ocpp::CiString<20> idTag,
                                                            std::optional<ocpp::CiString<20>> parent_id) {
    ocpp::v2::ReserveNowRequest request;
    request.id = reservation_id;
    if (connector == 0) {
        request.evseId = std::nullopt;
    } else {
        request.evseId = connector;
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

bool ChargePointV16::cb_stop_transaction(std::int32_t connector, ocpp::v16::Reason reason) {
    const auto res = m_callbacks_ptr->cb_stop_transaction(connector, convert(reason));
    return res == ocpp::v2::RequestStartStopStatusEnum::Accepted;
}

ocpp::v16::DataTransferResponse ChargePointV16::cb_tariff_message(const ocpp::TariffMessage& message) {
    m_callbacks_ptr->cb_tariff_message(message);
    ocpp::v16::DataTransferResponse response;
    response.status = ocpp::v16::DataTransferStatus::Accepted;
    return response;
}

void ChargePointV16::cb_transaction_started(const std::int32_t connector, const std::string& session_id) {
    ocpp::v2::TransactionEventRequest event;
    event.eventType = ocpp::v2::TransactionEventEnum::Started;
    event.evse = {connector, 1};
    event.transactionInfo.transactionId = session_id;
    m_callbacks_ptr->cb_transaction_event(event);
}

void ChargePointV16::cb_transaction_stopped(const std::int32_t connector, const std::string& session_id,
                                            const std::int32_t transaction_id) {
    ocpp::v2::TransactionEventRequest event;
    event.eventType = ocpp::v2::TransactionEventEnum::Ended;
    event.evse = {connector, 1};
    event.transactionInfo.transactionId = session_id;
    event.seqNo = transaction_id; // TODO(james-ctc): this mapping is incorrect
    m_callbacks_ptr->cb_transaction_event(event);
}

void ChargePointV16::cb_transaction_updated(const std::int32_t connector, const std::string& session_id,
                                            const std::int32_t transaction_id,
                                            const ocpp::v16::IdTagInfo& id_tag_info) {
    ocpp::v2::TransactionEventRequest event;
    event.eventType = ocpp::v2::TransactionEventEnum::Updated;
    event.evse = {connector, 1};
    event.transactionInfo.transactionId = session_id;
    event.seqNo = transaction_id; // TODO(james-ctc): this mapping is incorrect

    ocpp::v2::TransactionEventResponse event_response;
    if (id_tag_info.parentIdTag) {
        ocpp::v2::IdTokenInfo token;
        token.status = convert(id_tag_info.status);
        token.cacheExpiryDateTime = id_tag_info.expiryDate;
        token.groupIdToken = {static_cast<std::string>(id_tag_info.parentIdTag.value()), "ISO14443"};
        event_response.idTokenInfo = token;
    }

    m_callbacks_ptr->cb_transaction_event_response(event, event_response);
}

ocpp::v16::UnlockStatus ChargePointV16::cb_unlock_connector(std::int32_t connector) {
    return (convert(m_callbacks_ptr->cb_unlock_connector(connector, 1)));
}

void ChargePointV16::cb_update_firmware(const ocpp::v16::UpdateFirmwareRequest msg) {
    const auto request = convert(msg);
    m_callbacks_ptr->cb_update_firmware_request(request);
}

ocpp::v16::GetLogResponse ChargePointV16::cb_upload_diagnostics(const ocpp::v16::GetDiagnosticsRequest& request) {
    ocpp::v2::GetLogRequest req;
    ocpp::v2::LogParameters params;

    params.remoteLocation = request.location;
    params.oldestTimestamp = request.startTime;
    params.latestTimestamp = request.stopTime;

    req.log = std::move(params);
    req.logType = ocpp::v2::LogEnum::DiagnosticsLog;
    req.retries = request.retries;
    req.retryInterval = request.retryInterval;

    return convert(m_callbacks_ptr->cb_get_log_request(req));
}

ocpp::v16::GetLogResponse ChargePointV16::cb_upload_logs(ocpp::v16::GetLogRequest request) {
    ocpp::v2::GetLogRequest req;
    ocpp::v2::LogParameters params;

    params.remoteLocation = static_cast<std::string>(request.log.remoteLocation);
    params.oldestTimestamp = request.log.oldestTimestamp;
    params.latestTimestamp = request.log.latestTimestamp;

    req.log = std::move(params);
    req.logType = convert(request.logType);
    req.retries = request.retries;
    req.retryInterval = request.retryInterval;

    return convert(m_callbacks_ptr->cb_get_log_request(req));
}

// ----------------------------------------------------------------------------
// setup/configuration

void ChargePointV16::configure_callbacks() {
    // directly supported
    m_charge_point->register_all_connectors_unavailable_callback(
        [this](auto&&... args) { return m_callbacks_ptr->cb_all_connectors_unavailable(args...); });
    m_charge_point->register_cancel_reservation_callback(
        [this](auto&&... args) { return m_callbacks_ptr->cb_cancel_reservation(args...); });
    m_charge_point->register_default_price_callback(
        [this](auto&&... args) { return m_callbacks_ptr->cb_tariff_message(args...); });
    m_charge_point->register_pause_charging_callback(
        [this](auto&&... args) { return m_callbacks_ptr->cb_pause_charging(args...); });
    m_charge_point->register_resume_charging_callback(
        [this](auto&&... args) { return m_callbacks_ptr->cb_resume_charging(args...); });
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
    m_charge_point->register_get_15118_ev_certificate_response_callback(
        [this](auto&&... args) { return cb_get_15118_ev_certificate_response(args...); });
    m_charge_point->register_transaction_started_callback(
        [this](auto&&... args) { return cb_transaction_started(args...); });
    m_charge_point->register_transaction_updated_callback(
        [this](auto&&... args) { return cb_transaction_updated(args...); });
    m_charge_point->register_transaction_stopped_callback(
        [this](auto&&... args) { return cb_transaction_stopped(args...); });
    m_charge_point->register_boot_notification_response_callback(
        [this](auto&&... args) { return cb_boot_notification_response(args...); });
    m_charge_point->register_session_cost_callback([this](auto&&... args) { return cb_session_cost(args...); });
    m_charge_point->register_tariff_message_callback([this](auto&&... args) { return cb_tariff_message(args...); });
    m_charge_point->register_set_display_message_callback(
        [this](auto&&... args) { return cb_set_display_message(args...); });
    m_charge_point->register_data_transfer_callback([this](auto&&... args) { return cb_data_transfer(args...); });
    m_charge_point->register_generic_configuration_key_changed_callback(
        [this](auto&&... args) { cb_generic_configuration_key_changed(args...); });
}

void ChargePointV16::configure_data_model(const config_info_t& config) {
    auto configured_config_path = fs::path(config.chargepoint_config_path);
    auto ocpp_share_path = fs::path(config.ocpp_share_path);

    // try to find the config file if it has been provided as a relative path
    if (!fs::exists(configured_config_path) && configured_config_path.is_relative()) {
        configured_config_path = ocpp_share_path / configured_config_path;
    }
    if (!fs::exists(configured_config_path)) {
        EVLOG_AND_THROW(
            Everest::EverestConfigError(fmt::format("OCPP config file is not available at given path: {} which was "
                                                    "resolved to: {}",
                                                    config.chargepoint_config_path, configured_config_path.string())));
    }
    const auto config_path = configured_config_path;
    EVLOG_info << "OCPP config: " << config_path.string();

    auto configured_user_config_path = fs::path(config.user_config_path);
    // try to find the user config file if it has been provided as a relative path
    if (!fs::exists(configured_user_config_path) && configured_user_config_path.is_relative()) {
        configured_user_config_path = ocpp_share_path / configured_user_config_path;
    }
    const auto user_config_path = configured_user_config_path;
    EVLOG_info << "OCPP user config: " << user_config_path.string();

    std::ifstream ifs(config_path.c_str());
    std::string config_file((std::istreambuf_iterator<char>(ifs)), (std::istreambuf_iterator<char>()));
    auto json_config = json::parse(config_file);
    json_config.at("Core").at("NumberOfConnectors") = config.numnber_of_connectors;

    if (fs::exists(user_config_path)) {
        std::ifstream ifs(user_config_path.c_str());
        std::string user_config_file((std::istreambuf_iterator<char>(ifs)), (std::istreambuf_iterator<char>()));

        try {
            const auto user_config = json::parse(user_config_file);
            EVLOG_info << "Augmenting chargepoint config with user_config entries";
            json_config.merge_patch(user_config);
        } catch (const json::parse_error& e) {
            EVLOG_error << "Error while parsing user config file.";
            EVLOG_AND_THROW(e);
        }
    } else {
        EVLOG_debug << "No user-config provided. Creating user config file";
        create_empty_user_config(user_config_path);
    }

    if (!fs::exists(config.message_log_path)) {
        try {
            fs::create_directory(config.message_log_path);
        } catch (const fs::filesystem_error& e) {
            EVLOG_AND_THROW(e);
        }
    }

    const auto charge_point_config_json = json_config.dump();
    m_charge_point_configuration = std::make_unique<ocpp::v16::ChargePointConfiguration>(
        charge_point_config_json, ocpp_share_path, user_config_path);
}

void ChargePointV16::init(init_args_t& args) {

    config_info_t config{
        args.v16_chargepoint_config_path,
        args.message_log_path,
        args.ocpp_share_path,
        args.sql_init_path,
        args.v16_user_config_path,
        static_cast<std::uint32_t>(args.evse_connector_structure.size()) // numnber_of_connectors;
    };

    configure_data_model(config);

    // clang-format off
    m_charge_point = std::make_unique<ocpp::v16::ChargePoint>(
        *m_charge_point_configuration,
        args.ocpp_share_path,
        args.core_database_path,
        args.sql_init_path,
        args.message_log_path,
        m_evse_security,
        std::nullopt,
        [this](auto &&...args){ m_callbacks_ptr->cb_ocpp_messages(args...);}
    );
    // clang-format on

    configure_callbacks();
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
void ChargePointV16::start(ocpp::v2::BootReasonEnum bootreason, bool start_connecting) {
    check_configured("start");
    // TODO(james-ctc): look into resuming sessions
    m_charge_point->start({}, convert(bootreason), {});
}
void ChargePointV16::stop() {
    check_configured("stop");
    m_charge_point->stop();
}

std::optional<ocpp::v2::DataTransferResponse>
ChargePointV16::data_transfer_req(const ocpp::v2::DataTransferRequest& request) {
    check_configured("data_transfer_req");
}

std::optional<bool> ChargePointV16::get_bool(const ocpp::v2::Component& component_id,
                                             const ocpp::v2::Variable& variable_id,
                                             ocpp::v2::AttributeEnum attribute_enum) {
    check_configured("get_bool");
}
std::optional<std::int32_t> ChargePointV16::get_int32(const ocpp::v2::Component& component_id,
                                                      const ocpp::v2::Variable& variable_id,
                                                      ocpp::v2::AttributeEnum attribute_enum) {
    check_configured("get_int32");
}
std::optional<std::string> ChargePointV16::get_string(const ocpp::v2::Component& component_id,
                                                      const ocpp::v2::Variable& variable_id,
                                                      ocpp::v2::AttributeEnum attribute_enum) {
    check_configured("get_string");
}

std::vector<ocpp::v2::EnhancedCompositeSchedule>
ChargePointV16::get_all_composite_schedules(std::int32_t duration_s, const ocpp::v2::ChargingRateUnitEnum& unit) {
    check_configured("get_all_composite_schedules");
}
std::vector<ocpp::v2::GetVariableResult>
ChargePointV16::get_variables(const std::vector<ocpp::v2::GetVariableData>& get_variable_data_vector) {
    check_configured("get_variables");
}

void ChargePointV16::on_authorized(std::int32_t evse_id, std::int32_t connector_id, const ocpp::v2::IdToken& id_token) {
    check_configured("on_authorized");
}
ocpp::v2::ChangeAvailabilityResponse
ChargePointV16::on_change_availability(const ocpp::v2::ChangeAvailabilityRequest& request) {
    check_configured("on_change_availability");
}
bool ChargePointV16::on_charging_state_changed(std::uint32_t evse_id, ocpp::v2::ChargingStateEnum charging_state,
                                               ocpp::v2::TriggerReasonEnum trigger_reason) {
    check_configured("on_charging_state_changed");
}
void ChargePointV16::on_enabled(std::int32_t evse_id, std::int32_t connector_id) {
    check_configured("on_enabled");
}
void ChargePointV16::on_ev_charging_needs(const ocpp::v2::NotifyEVChargingNeedsRequest& request) {
    check_configured("on_ev_charging_needs");
}
void ChargePointV16::on_event(const std::vector<ocpp::v2::EventData>& events) {
    check_configured("on_event");
}
void ChargePointV16::on_fault_cleared(std::int32_t evse_id, std::int32_t connector_id) {
    check_configured("on_fault_cleared");
}
void ChargePointV16::on_faulted(std::int32_t evse_id, std::int32_t connector_id) {
    check_configured("on_faulted");
}
void ChargePointV16::on_firmware_update_status_notification(
    std::int32_t request_id, const ocpp::v2::FirmwareStatusEnum& firmware_update_status) {
    check_configured("on_firmware_update_status_notification");
}
ocpp::v2::Get15118EVCertificateResponse
ChargePointV16::on_get_15118_ev_certificate_request(const ocpp::v2::Get15118EVCertificateRequest& request) {
    check_configured("on_get_15118_ev_certificate_request");
}
void ChargePointV16::on_log_status_notification(ocpp::v2::UploadLogStatusEnum status, std::int32_t requestId) {
    check_configured("on_log_status_notification");
}
void ChargePointV16::on_meter_value(std::int32_t evse_id, const ocpp::v2::MeterValue& meter_value) {
    check_configured("on_meter_value");
}
void ChargePointV16::on_reservation_status(std::int32_t reservation_id, ocpp::v2::ReservationUpdateStatusEnum status) {
    check_configured("on_reservation_status");
}
void ChargePointV16::on_reservation_cleared(std::int32_t evse_id, std::int32_t connector_id) {
    check_configured("on_reservation_cleared");
}
void ChargePointV16::on_reserved(std::int32_t evse_id, std::int32_t connector_id) {
    check_configured("on_reserved");
}
void ChargePointV16::on_security_event(const ocpp::CiString<50>& event_type,
                                       const std::optional<ocpp::CiString<255>>& tech_info,
                                       const std::optional<bool>& critical,
                                       const std::optional<ocpp::DateTime>& timestamp) {
    check_configured("on_security_event");
}
void ChargePointV16::on_session_finished(std::int32_t evse_id, std::int32_t connector_id) {
    check_configured("on_session_finished");
}
void ChargePointV16::on_session_started(std::int32_t evse_id, std::int32_t connector_id) {
    check_configured("on_session_started");
}
void ChargePointV16::on_transaction_finished(std::int32_t evse_id, const ocpp::DateTime& timestamp,
                                             const ocpp::v2::MeterValue& meter_stop, ocpp::v2::ReasonEnum reason,
                                             ocpp::v2::TriggerReasonEnum trigger_reason,
                                             const std::optional<ocpp::v2::IdToken>& id_token,
                                             const std::optional<std::string>& signed_meter_value,
                                             ocpp::v2::ChargingStateEnum charging_state) {
    check_configured("on_transaction_finished");
}
void ChargePointV16::on_transaction_started(
    std::int32_t evse_id, std::int32_t connector_id, const std::string& session_id, const ocpp::DateTime& timestamp,
    ocpp::v2::TriggerReasonEnum trigger_reason, const ocpp::v2::MeterValue& meter_start,
    const std::optional<ocpp::v2::IdToken>& id_token, const std::optional<ocpp::v2::IdToken>& group_id_token,
    const std::optional<std::int32_t>& reservation_id, const std::optional<std::int32_t>& remote_start_id,
    ocpp::v2::ChargingStateEnum charging_state) {
    check_configured("on_transaction_started");
}
void ChargePointV16::on_unavailable(std::int32_t evse_id, std::int32_t connector_id) {
    check_configured("on_unavailable");
}

void ChargePointV16::register_variable_listener(listener_t&& listener) {
    check_configured("register_variable_listener");
}
std::map<ocpp::v2::SetVariableData, ocpp::v2::SetVariableResult>
ChargePointV16::set_variables(const std::vector<ocpp::v2::SetVariableData>& set_variable_data_vector,
                              const std::string& source) {
    check_configured("set_variables");
}

ocpp::v2::AuthorizeResponse
ChargePointV16::validate_token(const ocpp::v2::IdToken& id_token,
                               const std::optional<ocpp::CiString<10000>>& certificate,
                               const std::optional<std::vector<ocpp::v2::OCSPRequestData>>& ocsp_request_data) {
    check_configured("validate_token");
}

} // namespace ocpp_multi
