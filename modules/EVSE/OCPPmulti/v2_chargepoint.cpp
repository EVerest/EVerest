// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "v2_chargepoint.hpp"

#include <everest/conversions/ocpp/ocpp_conversions.hpp>
#include <everest/ocpp_module_common/conversions.hpp>
#include <everest/ocpp_module_common/device_model/composed_device_model_storage.hpp>
#include <everest/ocpp_module_common/error_handling.hpp>
#include <ocpp/v2/device_model_helpers.hpp>

namespace {

constexpr const auto CENTRAL_CONTRACT_VALIDATION_ALLOWED_VAR_NAME = "CentralContractValidationAllowed";
constexpr const auto CONTRACT_CERTIFICATE_INSTALLATION_ENABLED_VAR_NAME = "ContractCertificateInstallationEnabled";
constexpr const auto EV_CONNECTION_TIMEOUT_VAR_NAME = "EVConnectionTimeOut";
constexpr const auto MASTER_PASS_GROUP_ID_VAR_NAME = "MasterPassGroupId";
constexpr const auto PNC_ENABLED_VAR_NAME = "PnCEnabled";
constexpr const auto SETPOINT_PRIORITY_VAR_NAME = "SetpointPriority";
constexpr const auto TX_START_POINT_VAR_NAME = "TxStartPoint";
constexpr const auto TX_STOP_POINT_VAR_NAME = "TxStopPoint";

auto convert(const ocpp::v2::MessageContent& value) {
    ocpp::DisplayMessageContent result{};

    result.message = value.content;
    result.language = value.language;
    result.message_format = value.format;

    return result;
}

auto convert(ocpp::v2::ResetEnum value) {
    using ResetType = ocpp_multi::GenericChargePointCallbacks::ResetType;
    ResetType result{};
    switch (value) {
    case ocpp::v2::ResetEnum::Immediate:
        result = ResetType::Immediate;
        break;
    case ocpp::v2::ResetEnum::ImmediateAndResume:
        result = ResetType::ImmediateAndResume;
        break;
    default:
    case ocpp::v2::ResetEnum::OnIdle:
        result = ResetType::OnIdle;
        break;
    }
    return result;
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

} // namespace

namespace ocpp_multi {

// ----------------------------------------------------------------------------
// OCPP v2 ChargePoint

void ChargePointV2::check_configured(const std::string_view& fn) {
    if (m_charge_point == nullptr) {
        std::string msg{"ChargePointV2 not configured: "};
        msg += fn;
        throw NotConfigured(msg);
    }
}

void ChargePointV2::cb_default_price(const std::vector<ocpp::DisplayMessageContent>& messages) {
    const auto prices = ocpp_conversions::to_everest_default_price(messages);
    m_callbacks_ptr->cb_default_price(prices);
}

ocpp::v2::GetLogResponse ChargePointV2::cb_get_log_request(const ocpp::v2::GetLogRequest& request) {
    auto req = module::conversions::to_everest_upload_logs_request(request);
    return m_callbacks_ptr->cb_get_log_request(req);
}

bool ChargePointV2::cb_is_reset_allowed(const std::optional<const std::int32_t>& evse_id,
                                        const ocpp::v2::ResetEnum& type) {
    return m_callbacks_ptr->cb_is_reset_allowed(evse_id, convert(type));
}

ocpp::v2::RequestStartStopStatusEnum
ChargePointV2::cb_remote_start_transaction(const ocpp::v2::RequestStartTransactionRequest& request,
                                           bool authorize_remote_start) {
    ocpp_multi::GenericChargePointCallbacks::IdToken token;
    token.token = request.idToken;
    token.prevalidated = !authorize_remote_start;
    token.group_id_token = request.groupIdToken;
    token.evse_id = request.evseId;
    token.request_id = request.remoteStartId;
    m_callbacks_ptr->cb_provide_token(token);
    return ocpp::v2::RequestStartStopStatusEnum::Accepted;
}

void ChargePointV2::cb_reset(const std::optional<const std::int32_t>& evse_id, const ocpp::v2::ResetEnum& type) {
    m_callbacks_ptr->cb_reset(evse_id, convert(type));
}

ocpp::v2::RequestStartStopStatusEnum ChargePointV2::cb_stop_transaction(std::int32_t evse_id,
                                                                        const ocpp::v2::ReasonEnum& stop_reason) {
    const auto reason = module::conversions::to_everest_stop_transaction_reason(stop_reason);
    return m_callbacks_ptr->cb_stop_transaction(evse_id, reason);
}

void ChargePointV2::cb_tariff_message(const ocpp::TariffMessage& message) {
    const auto msg = ocpp_conversions::to_everest_tariff_message(message);
    m_callbacks_ptr->cb_tariff_message(msg);
}

void ChargePointV2::cb_variable_listener(
    const std::unordered_map<std::int64_t, ocpp::v2::VariableMonitoringMeta>& monitors,
    const ocpp::v2::Component& component, const ocpp::v2::Variable& variable,
    const ocpp::v2::VariableCharacteristics& characteristics, const ocpp::v2::VariableAttribute& attribute,
    const std::string& value_previous, const std::string& value_current) {
    // copy under lock, invoke outside
    const listener_t listener = *m_variable_listener.handle();
    if (listener != nullptr) {
        listener(component, variable, value_current);
    }
}

namespace {
// device model read via the virtual get_variables (same underlying read as ChargePoint::request_value<T>)
template <typename T>
std::optional<T> get_device_model_value(ocpp::v2::ChargePointInterface& charge_point,
                                        const ocpp::v2::Component& component_id, const ocpp::v2::Variable& variable_id,
                                        ocpp::v2::AttributeEnum attribute_enum) {
    ocpp::v2::GetVariableData data;
    data.component = component_id;
    data.variable = variable_id;
    data.attributeType = attribute_enum;
    const auto results = charge_point.get_variables({data});
    std::optional<T> res;
    if (!results.empty() && results.front().attributeStatus == ocpp::v2::GetVariableStatusEnum::Accepted &&
        results.front().attributeValue.has_value()) {
        try {
            res = ocpp::v2::to_specific_type<T>(results.front().attributeValue.value().get());
        } catch (const std::exception&) {
            // not convertible: treat as absent (matches request_value<T> behaviour)
        }
    }
    return res;
}
} // namespace

std::optional<bool> ChargePointV2::get_bool(const ocpp::v2::Component& component_id,
                                            const ocpp::v2::Variable& variable_id,
                                            ocpp::v2::AttributeEnum attribute_enum) {
    check_configured("get_bool");
    return get_device_model_value<bool>(*m_charge_point, component_id, variable_id, attribute_enum);
}

std::optional<std::int32_t> ChargePointV2::get_int32(const ocpp::v2::Component& component_id,
                                                     const ocpp::v2::Variable& variable_id,
                                                     ocpp::v2::AttributeEnum attribute_enum) {
    check_configured("get_int32");
    return get_device_model_value<std::int32_t>(*m_charge_point, component_id, variable_id, attribute_enum);
}
std::optional<std::string> ChargePointV2::get_string(const ocpp::v2::Component& component_id,
                                                     const ocpp::v2::Variable& variable_id,
                                                     ocpp::v2::AttributeEnum attribute_enum) {
    check_configured("get_string");
    return get_device_model_value<std::string>(*m_charge_point, component_id, variable_id, attribute_enum);
}

void ChargePointV2::process_tx_event_effect(std::int32_t evse_id, module::TxEventEffect tx_event_effect,
                                            const types::evse_manager::SessionEvent& session_event) {
    using namespace module::conversions;

    if (tx_event_effect != module::TxEventEffect::NONE) {
        const auto transaction_data = m_callbacks_ptr->transaction_data(evse_id);
        if (transaction_data == nullptr) {
            throw std::runtime_error("Could not start transaction because no tranasaction data is present");
        }
        transaction_data->timestamp = ocpp_conversions::to_ocpp_datetime_or_now(session_event.timestamp);

        if (tx_event_effect == module::TxEventEffect::START_TRANSACTION) {
            transaction_data->started = true;
            transaction_data->meter_value =
                to_ocpp_meter_value(get_meter_value(session_event), ocpp::v2::ReadingContextEnum::Transaction_Begin,
                                    get_signed_meter_value(session_event));
            m_charge_point->on_transaction_started(
                evse_id, transaction_data->connector_id, transaction_data->session_id, transaction_data->timestamp,
                transaction_data->trigger_reason, transaction_data->meter_value, transaction_data->id_token,
                transaction_data->group_id_token, transaction_data->reservation_id, transaction_data->remote_start_id,
                transaction_data->charging_state);
        } else if (tx_event_effect == module::TxEventEffect::STOP_TRANSACTION) {
            transaction_data->meter_value =
                to_ocpp_meter_value(get_meter_value(session_event), ocpp::v2::ReadingContextEnum::Transaction_End,
                                    get_signed_meter_value(session_event));
            std::optional<ocpp::v2::SignedMeterValue> start_signed_meter_value;
            if (session_event.transaction_finished.has_value() &&
                session_event.transaction_finished.value().start_signed_meter_value.has_value()) {
                start_signed_meter_value = to_ocpp_signed_meter_value(
                    session_event.transaction_finished.value().start_signed_meter_value.value());
            }
            // stop_reason was set by the session event handlers (e.g. StoppedByEV, EVDisconnected, Remote)
            m_charge_point->on_transaction_finished(evse_id, transaction_data->timestamp, transaction_data->meter_value,
                                                    transaction_data->stop_reason, transaction_data->trigger_reason,
                                                    transaction_data->id_token, std::nullopt,
                                                    transaction_data->charging_state, start_signed_meter_value);
            m_callbacks_ptr->transaction_reset(evse_id);
        }
    }
}

ocpp::v2::Callbacks ChargePointV2::configure_callbacks() {
    ocpp::v2::Callbacks callbacks;

    // indirectly supported
    callbacks.default_price_callback = [this](auto&&... args) { cb_default_price(args...); };
    callbacks.get_log_request_callback = [this](auto&&... args) { return cb_get_log_request(args...); };
    callbacks.is_reset_allowed_callback = [this](auto&&... args) { return cb_is_reset_allowed(args...); };
    callbacks.remote_start_transaction_callback = [this](auto&&... args) {
        return cb_remote_start_transaction(args...);
    };
    callbacks.reset_callback = [this](auto&&... args) { cb_reset(args...); };
    callbacks.stop_transaction_callback = [this](auto&&... args) { return cb_stop_transaction(args...); };
    callbacks.tariff_message_callback = [this](auto&&... args) { cb_tariff_message(args...); };

    // directly supported
    callbacks.connector_effective_operative_status_changed_callback = [this](auto&&... args) {
        m_callbacks_ptr->cb_connector_effective_operative_status(args...);
    };
    callbacks.pause_charging_callback = [this](auto&&... args) { m_callbacks_ptr->cb_pause_charging(args...); };
    callbacks.unlock_connector_callback = [this](auto&&... args) {
        return m_callbacks_ptr->cb_unlock_connector(args...);
    };
    callbacks.is_reservation_for_token_callback = [this](auto evse_id, const auto& idToken, const auto& groupIdToken) {
        return m_callbacks_ptr->cb_is_reservation_for_token(evse_id, idToken, groupIdToken);
    };
    callbacks.update_firmware_request_callback = [this](auto&&... args) {
        return m_callbacks_ptr->cb_update_firmware_request(args...);
    };
    callbacks.variable_changed_callback = [this](const auto& set_variable_data) {
        m_callbacks_ptr->cb_variable_set(set_variable_data);
    };
    callbacks.validate_network_profile_callback = [this](auto /* configuration_slot */,
                                                         const auto& network_connection_profile) {
        return m_callbacks_ptr->cb_validate_network_profile(network_connection_profile);
    };
    callbacks.configure_network_connection_profile_callback = [this](auto /* configuration_slot */,
                                                                     const auto& /* network_connection_profile */) {
        return m_callbacks_ptr->cb_configure_network_connection_profile();
    };
    callbacks.all_connectors_unavailable_callback = [this]() { m_callbacks_ptr->cb_all_connectors_unavailable(); };
    callbacks.transaction_event_callback = [this](auto&&... args) { m_callbacks_ptr->cb_transaction_event(args...); };
    callbacks.transaction_event_response_callback = [this](auto&&... args) {
        m_callbacks_ptr->cb_transaction_event_response(args...);
    };
    callbacks.boot_notification_callback = [this](auto&&... args) { m_callbacks_ptr->cb_boot_notification(args...); };
    callbacks.set_display_message_callback = [this](auto&&... args) {
        return m_callbacks_ptr->cb_set_display_message(args...);
    };
    callbacks.get_display_message_callback = [this](auto&&... args) {
        return m_callbacks_ptr->cb_get_display_message(args...);
    };
    callbacks.clear_display_message_callback = [this](auto&&... args) {
        return m_callbacks_ptr->cb_clear_display_message(args...);
    };
    callbacks.set_running_cost_callback = [this](auto&&... args) { m_callbacks_ptr->cb_set_running_cost(args...); };
    callbacks.data_transfer_callback = [this](auto&&... args) { return m_callbacks_ptr->cb_data_transfer(args...); };
    callbacks.connection_state_changed_callback =
        [this](auto is_connected, auto /*configuration_slot*/, const auto& /*network_connection_profile*/,
               auto protocol_version) { m_callbacks_ptr->cb_connection_state_changed(is_connected, protocol_version); };
    callbacks.security_event_callback = [this](const auto& event_type, const auto& tech_info) {
        m_callbacks_ptr->cb_security_event(event_type, tech_info);
    };

    // this callback publishes the schedules within EVerest and applies the schedules for the individual
    // evse_energy_sink
    callbacks.set_charging_profiles_callback = [this]() { m_callbacks_ptr->cb_set_charging_profiles(); };
    callbacks.time_sync_callback = [this](auto&&... args) { m_callbacks_ptr->cb_time_sync(args...); };
    callbacks.reserve_now_callback = [this](auto&&... args) { return m_callbacks_ptr->cb_reserve_now(args...); };
    callbacks.cancel_reservation_callback = [this](auto&&... args) {
        return m_callbacks_ptr->cb_cancel_reservation(args...);
    };
    callbacks.update_allowed_energy_transfer_modes_callback = [this](auto&&... args) {
        return m_callbacks_ptr->cb_update_allowed_energy_transfer_modes(args...);
    };
    callbacks.ocpp_messages_callback = [this](auto&&... args) { m_callbacks_ptr->cb_ocpp_messages(args...); };
    return callbacks;
}

void ChargePointV2::init(init_args_t& args) {
    // initialise libocpp device model
    auto libocpp_device_model_storage = std::make_shared<ocpp::v2::DeviceModelStorageSqlite>(
        args.v2_device_model_database_path, args.v2_device_model_database_migration_path,
        args.v2_device_model_config_path);

    // initialise composed device model, this will be provided to the ChargePoint constructor
    auto composed_device_model_storage = std::make_unique<module::device_model::ComposedDeviceModelStorage>();

    // register both device model storages
    // note - this processing causes a slight delay, scope for performance tuning
    composed_device_model_storage->register_device_model_storage("OCPP", std::move(libocpp_device_model_storage));
    composed_device_model_storage->register_device_model_storage("EVEREST", args.everest_device_model);

    const auto ocpp_share_path = args.share_path / "OCPP201";
    const auto sql_init_path = ocpp_share_path / SQL_CORE_MIGRATIONS;

    // clang-format off
    m_charge_point =
        std::make_unique<ocpp::v2::ChargePoint>(
            args.evse_connector_structure,
            std::move(composed_device_model_storage),
            ocpp_share_path,
            args.v2_core_database_path,
            sql_init_path,
            args.message_log_path,
            m_evse_security,
            configure_callbacks()
        );
    // clang-format on
}

void ChargePointV2::connect_websocket() {
    check_configured("connect_websocket");
    m_charge_point->connect_websocket();
}
void ChargePointV2::disconnect_websocket() {
    check_configured("disconnect_websocket");
    m_charge_point->disconnect_websocket();
}
void ChargePointV2::set_message_queue_resume_delay(std::chrono::seconds delay) {
    check_configured("set_message_queue_resume_delay");
    m_charge_point->set_message_queue_resume_delay(delay);
}
bool ChargePointV2::restart() {
    check_configured("restart");
    m_charge_point->start(ocpp::v2::BootReasonEnum::ApplicationReset, true);
    return true;
}
void ChargePointV2::start(ocpp::v2::BootReasonEnum bootreason, const std::set<std::string>& resuming_session_ids,
                          bool start_connecting) {
    check_configured("start");
    // resuming_session_ids is not used for OCPP2.x: resumed transactions are restored via
    // InternalCtrlr::ResumeTransactionsOnBoot and on_event_session_resumed()
    m_charge_point->start(bootreason, start_connecting);
}
void ChargePointV2::stop() {
    check_configured("stop");
    m_charge_point->stop();
}

std::optional<ocpp::v2::DataTransferResponse>
ChargePointV2::data_transfer_req(const ocpp::v2::DataTransferRequest& request) {
    check_configured("data_transfer_req");
    return m_charge_point->data_transfer_req(request);
}

std::optional<bool> ChargePointV2::get_central_contract_validation_allowed() {
    return get_bool(ocpp::v2::ControllerComponents::ISO15118Ctrlr, {CENTRAL_CONTRACT_VALIDATION_ALLOWED_VAR_NAME},
                    ocpp::v2::AttributeEnum::Actual);
}
std::optional<bool> ChargePointV2::get_contract_certificate_installation_enabled() {
    return get_bool(ocpp::v2::ControllerComponents::ISO15118Ctrlr, {CONTRACT_CERTIFICATE_INSTALLATION_ENABLED_VAR_NAME},
                    ocpp::v2::AttributeEnum::Actual);
}
std::optional<bool> ChargePointV2::get_pnc_enabled() {
    return get_bool(ocpp::v2::ControllerComponents::ISO15118Ctrlr, {PNC_ENABLED_VAR_NAME},
                    ocpp::v2::AttributeEnum::Actual);
}
std::optional<std::int32_t> ChargePointV2::get_ev_connection_timeout() {
    return get_int32(ocpp::v2::ControllerComponents::TxCtrlr, {EV_CONNECTION_TIMEOUT_VAR_NAME},
                     ocpp::v2::AttributeEnum::Actual);
}
std::optional<std::string> ChargePointV2::get_setpoint_priority() {
    return get_string(ocpp::v2::ControllerComponents::SmartChargingCtrlr, {SETPOINT_PRIORITY_VAR_NAME},
                      ocpp::v2::AttributeEnum::Actual);
}
std::optional<std::string> ChargePointV2::get_master_pass_group_id() {
    return get_string(ocpp::v2::ControllerComponents::AuthCtrlr, {MASTER_PASS_GROUP_ID_VAR_NAME},
                      ocpp::v2::AttributeEnum::Actual);
}
std::optional<std::string> ChargePointV2::get_tx_start_point() {
    return get_string(ocpp::v2::ControllerComponents::SmartChargingCtrlr, {TX_START_POINT_VAR_NAME},
                      ocpp::v2::AttributeEnum::Actual);
}
std::optional<std::string> ChargePointV2::get_tx_stop_point() {
    return get_string(ocpp::v2::ControllerComponents::SmartChargingCtrlr, {TX_STOP_POINT_VAR_NAME},
                      ocpp::v2::AttributeEnum::Actual);
}

std::vector<ocpp::v2::EnhancedCompositeSchedule>
ChargePointV2::get_all_composite_schedules(std::int32_t duration_s, ocpp::v2::ChargingRateUnitEnum unit) {
    check_configured("get_all_composite_schedules");
    return m_charge_point->get_all_composite_schedules(duration_s, unit);
}
std::vector<ocpp::v2::GetVariableResult>
ChargePointV2::get_variables(const std::vector<ocpp::v2::GetVariableData>& get_variable_data_vector) {
    check_configured("get_variables");
    return m_charge_point->get_variables(get_variable_data_vector);
}

void ChargePointV2::on_authorized(std::int32_t evse_id, std::int32_t connector_id, const ocpp::v2::IdToken& id_token) {
    check_configured("on_authorized");
    m_charge_point->on_authorized(evse_id, connector_id, id_token);
}
ocpp::v2::ChangeAvailabilityResponse
ChargePointV2::on_change_availability(const ocpp::v2::ChangeAvailabilityRequest& request) {
    check_configured("on_change_availability");
    return m_charge_point->on_change_availability(request);
}
bool ChargePointV2::on_charging_state_changed(std::uint32_t evse_id, ocpp::v2::ChargingStateEnum charging_state,
                                              ocpp::v2::TriggerReasonEnum trigger_reason) {
    check_configured("on_charging_state_changed");
    return m_charge_point->on_charging_state_changed(evse_id, charging_state, trigger_reason);
}
void ChargePointV2::on_enabled(std::int32_t evse_id, std::int32_t connector_id) {
    check_configured("on_enabled");
    m_charge_point->on_enabled(evse_id, connector_id);
}
void ChargePointV2::on_ev_charging_needs(const ocpp::v2::NotifyEVChargingNeedsRequest& request) {
    check_configured("on_ev_charging_needs");
    m_charge_point->on_ev_charging_needs(request);
}
void ChargePointV2::on_event(const EventInfo& event) {
    check_configured("on_event");
    if (event.error) {
        // TODO(james-ctc): needs tidying up. MREC error map is in generic_ocpp
        auto event_data = module::get_event_data(event.error.value(), event.event_cleared, event.event_id, {});
        std::string updated;
        m_callbacks_ptr->map_error(event.error->type, updated);
        event_data.techCode = std::move(updated);
        m_charge_point->on_event({event_data});
    }
}
void ChargePointV2::on_event_authorised(std::int32_t evse_id, std::int32_t connector_id,
                                        const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_event_authorised");
    // currently handled as part of SessionStarted and TransactionStarted events
}
void ChargePointV2::on_event_deauthorised(std::int32_t evse_id, std::int32_t connector_id,
                                          const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_event_deauthorised");
    auto transaction_data = m_callbacks_ptr->transaction_data(evse_id);
    if (transaction_data != nullptr) {
        transaction_data->trigger_reason = ocpp::v2::TriggerReasonEnum::StopAuthorized;
    }
    const auto tx_event_effect = m_callbacks_ptr->transaction_event(evse_id, module::TxEvent::DEAUTHORIZED);
    process_tx_event_effect(evse_id, tx_event_effect, session_event);
}
void ChargePointV2::on_event_charging_paused_ev(std::int32_t evse_id, std::int32_t connector_id,
                                                const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_event_charging_paused_ev");
    auto transaction_data = m_callbacks_ptr->transaction_data(evse_id);
    if (transaction_data != nullptr) {
        transaction_data->charging_state = ocpp::v2::ChargingStateEnum::SuspendedEV;
        transaction_data->trigger_reason = ocpp::v2::TriggerReasonEnum::ChargingStateChanged;
        transaction_data->stop_reason = ocpp::v2::ReasonEnum::StoppedByEV;
    }
    const auto tx_event_effect = m_callbacks_ptr->transaction_event(evse_id, module::TxEvent::ENERGY_TRANSFER_STOPPED);
    process_tx_event_effect(evse_id, tx_event_effect, session_event);
    m_charge_point->on_charging_state_changed(evse_id, ocpp::v2::ChargingStateEnum::SuspendedEV,
                                              ocpp::v2::TriggerReasonEnum::ChargingStateChanged);
}
void ChargePointV2::on_event_charging_paused_evse(std::int32_t evse_id, std::int32_t connector_id,
                                                  const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_event_charging_paused_evse");
    auto transaction_data = m_callbacks_ptr->transaction_data(evse_id);
    auto trigger_reason = ocpp::v2::TriggerReasonEnum::ChargingStateChanged;
    if (transaction_data != nullptr) {
        transaction_data->charging_state = ocpp::v2::ChargingStateEnum::SuspendedEVSE;
        if (transaction_data->stop_reason == ocpp::v2::ReasonEnum::Remote) {
            trigger_reason = ocpp::v2::TriggerReasonEnum::RemoteStop;
            transaction_data->trigger_reason = ocpp::v2::TriggerReasonEnum::ChargingStateChanged;
        }
    }
    const auto tx_event_effect = m_callbacks_ptr->transaction_event(evse_id, module::TxEvent::ENERGY_TRANSFER_STOPPED);
    process_tx_event_effect(evse_id, tx_event_effect, session_event);
    m_charge_point->on_charging_state_changed(evse_id, ocpp::v2::ChargingStateEnum::SuspendedEVSE, trigger_reason);
}
void ChargePointV2::on_event_charging_started(std::int32_t evse_id, std::int32_t connector_id,
                                              const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_event_charging_started");
    auto transaction_data = m_callbacks_ptr->transaction_data(evse_id);
    if (transaction_data != nullptr) {
        transaction_data->trigger_reason = ocpp::v2::TriggerReasonEnum::ChargingStateChanged;
        transaction_data->charging_state = ocpp::v2::ChargingStateEnum::Charging;
    }
    const auto tx_event_effect = m_callbacks_ptr->transaction_event(evse_id, module::TxEvent::ENERGY_TRANSFER_STARTED);
    process_tx_event_effect(evse_id, tx_event_effect, session_event);
    m_charge_point->on_charging_state_changed(evse_id, ocpp::v2::ChargingStateEnum::Charging,
                                              ocpp::v2::TriggerReasonEnum::ChargingStateChanged);
}
void ChargePointV2::on_event_disabled(std::int32_t evse_id, std::int32_t connector_id,
                                      const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_event_disabled");
    m_charge_point->on_unavailable(evse_id, connector_id);
}
void ChargePointV2::on_event_enabled(std::int32_t evse_id, std::int32_t connector_id,
                                     const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_event_enabled");
    m_charge_point->on_enabled(evse_id, connector_id);
}
void ChargePointV2::on_event_plugin_timeout(std::int32_t evse_id, std::int32_t connector_id,
                                            const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_event_plugin_timeout");
}
void ChargePointV2::on_event_reservation_end(std::int32_t evse_id, std::int32_t connector_id,
                                             const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_event_reservation_end");
    m_charge_point->on_reservation_cleared(evse_id, connector_id);
}
void ChargePointV2::on_event_reservation_start(std::int32_t evse_id, std::int32_t connector_id,
                                               const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_event_reservation_start");
    m_charge_point->on_reserved(evse_id, connector_id);
}
void ChargePointV2::on_event_session_finished(std::int32_t evse_id, std::int32_t connector_id,
                                              const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_event_session_finished");
    auto transaction_data = m_callbacks_ptr->transaction_data(evse_id);
    if (transaction_data != nullptr) {
        transaction_data->charging_state = ocpp::v2::ChargingStateEnum::Idle;
        transaction_data->stop_reason = ocpp::v2::ReasonEnum::EVDisconnected;
        transaction_data->trigger_reason = ocpp::v2::TriggerReasonEnum::EVCommunicationLost;
    }
    const auto tx_event_effect = m_callbacks_ptr->transaction_event(evse_id, module::TxEvent::EV_DISCONNECTED);
    process_tx_event_effect(evse_id, tx_event_effect, session_event);
    m_charge_point->on_session_finished(evse_id, connector_id);
}
void ChargePointV2::on_event_session_resumed(std::int32_t evse_id, std::int32_t connector_id,
                                             const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_event_session_resumed");
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
    m_callbacks_ptr->transaction_add(evse_id, transaction_data);
}
GenericChargePointInterface::SessionResult
ChargePointV2::on_event_session_started(std::int32_t evse_id, std::int32_t connector_id,
                                        const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_event_session_started");

    SessionResult result{false};

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
            m_callbacks_ptr->update_evcc_id_token(evse_id, id_token.value());
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
        m_callbacks_ptr->transaction_add(evse_id, transaction_data);

        const auto tx_event_effect = m_callbacks_ptr->transaction_event(evse_id, tx_event);
        process_tx_event_effect(evse_id, tx_event_effect, session_event);
        if (session_started.reason == types::evse_manager::StartSessionReason::EVConnected) {
            m_charge_point->on_session_started(evse_id, connector_id);
        }
        result = tx_event == module::TxEvent::EV_CONNECTED;
    } else {
        throw std::runtime_error("SessionEvent SessionStarted does not contain session_started context");
    }

    return result;
}
void ChargePointV2::on_event_switching_phases(std::int32_t evse_id, std::int32_t connector_id,
                                              const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_event_switching_phases");
}
void ChargePointV2::on_event_transaction_finished(std::int32_t evse_id, std::int32_t connector_id,
                                                  const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_event_transaction_finished");
    using namespace module::conversions;

    if (session_event.transaction_finished.has_value()) {
        const auto transaction_finished = session_event.transaction_finished.value();
        auto tx_event = module::TxEvent::NONE;
        auto reason = ocpp::v2::ReasonEnum::Other;
        if (transaction_finished.reason.has_value()) {
            reason = to_ocpp_reason(transaction_finished.reason.value());
            tx_event = get_tx_event(reason);
        }
        auto transaction_data = m_callbacks_ptr->transaction_data(evse_id);
        if (transaction_data != nullptr) {
            std::optional<ocpp::v2::IdToken> id_token = std::nullopt;
            if (transaction_finished.id_tag.has_value()) {
                id_token = to_ocpp_id_token(transaction_finished.id_tag.value().id_token);
                m_callbacks_ptr->update_evcc_id_token(evse_id, id_token.value());
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
        const auto tx_event_effect = m_callbacks_ptr->transaction_event(evse_id, tx_event);
        process_tx_event_effect(evse_id, tx_event_effect, session_event);

        if (tx_event == module::TxEvent::DEAUTHORIZED) {
            if (reason == ocpp::v2::ReasonEnum::Remote) {
                m_charge_point->on_charging_state_changed(evse_id, ocpp::v2::ChargingStateEnum::EVConnected,
                                                          ocpp::v2::TriggerReasonEnum::RemoteStop);
            } else {
                m_charge_point->on_charging_state_changed(evse_id, ocpp::v2::ChargingStateEnum::EVConnected,
                                                          ocpp::v2::TriggerReasonEnum::StopAuthorized);
            }
        } else {
            // TODO(piet): If StopTxOnEVSideDisconnect is false, authorization shall still be present. This cannot only
            // be handled within this module, but probably also within EvseManager and Auth

            // authorization is always withdrawn in case of TransactionFinished, so in case we haven't updated the
            // transaction handler yet, we have to do it
            // now
            const auto tx_event_effect = m_callbacks_ptr->transaction_event(evse_id, module::TxEvent::DEAUTHORIZED);
            process_tx_event_effect(evse_id, tx_event_effect, session_event);
        }
    } else {
        throw std::runtime_error("SessionEvent TransactionFinished does not contain transaction_finished context");
    }
}
GenericChargePointInterface::SessionResult
ChargePointV2::on_event_transaction_started(std::int32_t evse_id, std::int32_t connector_id,
                                            const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_event_transaction_started");
    using namespace module::conversions;

    SessionResult result{false};

    if (session_event.transaction_started.has_value()) {
        auto transaction_data = m_callbacks_ptr->transaction_data(evse_id);
        if (transaction_data == nullptr) {
            EVLOG_warning
                << "Could not update transaction data because no transaction data is present. This might happen "
                   "in case a TxStopPoint is already active when a TransactionStarted event occurs (e.g. "
                   "TxStopPoint is EnergyTransfer or ParkingBayOccupied)";
            m_charge_point->on_session_started(evse_id, connector_id);
            auto tx_event_effect = m_callbacks_ptr->transaction_event(evse_id, module::TxEvent::AUTHORIZED);
            process_tx_event_effect(evse_id, tx_event_effect, session_event);
            tx_event_effect = m_callbacks_ptr->transaction_event(evse_id, module::TxEvent::EV_CONNECTED);
            process_tx_event_effect(evse_id, tx_event_effect, session_event);
            result = true;
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
            m_callbacks_ptr->update_evcc_id_token(evse_id, id_token);
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
                m_charge_point->on_session_started(evse_id, connector_id);
                tx_event = module::TxEvent::EV_CONNECTED;
            }

            if (transaction_started.id_tag.authorization_type == types::authorization::AuthorizationType::OCPP) {
                trigger_reason = ocpp::v2::TriggerReasonEnum::RemoteStart;
            }

            transaction_data->trigger_reason = trigger_reason;
            const auto tx_event_effect = m_callbacks_ptr->transaction_event(evse_id, tx_event);
            process_tx_event_effect(evse_id, tx_event_effect, session_event);
            result = tx_event == module::TxEvent::EV_CONNECTED;
        }
    } else {
        throw std::runtime_error("SessionEvent TransactionStarted does not contain transaction_started context");
    }

    return result;
}
void ChargePointV2::on_fault_cleared(std::int32_t evse_id, std::int32_t connector_id) {
    check_configured("on_fault_cleared");
    m_charge_point->on_fault_cleared(evse_id, connector_id);
}
void ChargePointV2::on_faulted(std::int32_t evse_id, std::int32_t connector_id) {
    check_configured("on_faulted");
    m_charge_point->on_faulted(evse_id, connector_id);
}
void ChargePointV2::on_firmware_update_status_notification(std::int32_t request_id,
                                                           ocpp::v2::FirmwareStatusEnum firmware_update_status,
                                                           bool disable_connectors_during_install) {
    check_configured("on_firmware_update_status_notification");
    m_charge_point->on_firmware_update_status_notification(request_id, firmware_update_status,
                                                           disable_connectors_during_install);
}
void ChargePointV2::on_get_15118_ev_certificate_request(std::int32_t extensions_id,
                                                        const ocpp::v2::Get15118EVCertificateRequest& request) {
    check_configured("on_get_15118_ev_certificate_request");
    const auto response = m_charge_point->on_get_15118_ev_certificate_request(request);
    m_callbacks_ptr->cb_get_15118_ev_certificate_response(extensions_id, response, request.action);
}
void ChargePointV2::on_log_status_notification(ocpp::v2::UploadLogStatusEnum status, std::int32_t requestId) {
    check_configured("on_log_status_notification");
    m_charge_point->on_log_status_notification(status, requestId);
}
void ChargePointV2::on_meter_value(std::int32_t evse_id, std::optional<float> soc,
                                   const types::powermeter::Powermeter& power_meter) {
    check_configured("on_meter_value");
    ocpp::v2::MeterValue meter_value = module::conversions::to_ocpp_meter_value(
        power_meter, ocpp::v2::ReadingContextEnum::Sample_Periodic, power_meter.signed_meter_value);
    if (soc) {
        auto sampled_soc_value = module::conversions::to_ocpp_sampled_value(
            ocpp::v2::ReadingContextEnum::Sample_Periodic, ocpp::v2::MeasurandEnum::SoC, "Percent", std::nullopt,
            ocpp::v2::LocationEnum::EV);
        sampled_soc_value.value = soc.value();
        meter_value.sampledValue.push_back(sampled_soc_value);
    }
    m_charge_point->on_meter_value(evse_id, meter_value);
}

void ChargePointV2::on_reservation_status(std::int32_t reservation_id, ocpp::v2::ReservationUpdateStatusEnum status) {
    check_configured("on_reservation_status");
    m_charge_point->on_reservation_status(reservation_id, status);
}
void ChargePointV2::on_reservation_cleared(std::int32_t evse_id, std::int32_t connector_id) {
    check_configured("on_reservation_cleared");
    m_charge_point->on_reservation_cleared(evse_id, connector_id);
}
void ChargePointV2::on_reserved(std::int32_t evse_id, std::int32_t connector_id) {
    check_configured("on_reserved");
    m_charge_point->on_reserved(evse_id, connector_id);
}
void ChargePointV2::on_security_event(const ocpp::CiString<50>& event_type,
                                      const std::optional<ocpp::CiString<255>>& tech_info,
                                      const std::optional<bool>& critical,
                                      const std::optional<ocpp::DateTime>& timestamp) {
    check_configured("on_security_event");
    m_charge_point->on_security_event(event_type, tech_info, critical, timestamp);
}
void ChargePointV2::on_session_finished(std::int32_t evse_id, std::int32_t connector_id,
                                        const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_session_finished");
    m_charge_point->on_session_finished(evse_id, connector_id);
}
void ChargePointV2::on_session_started(std::int32_t evse_id, std::int32_t connector_id,
                                       const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_session_started");
    m_charge_point->on_session_started(evse_id, connector_id);
}
void ChargePointV2::on_transaction_finished(std::int32_t evse_id, const std::string& session_id,
                                            const ocpp::DateTime& timestamp, const ocpp::v2::MeterValue& meter_stop,
                                            types::evse_manager::StopTransactionReason reason,
                                            ocpp::v2::TriggerReasonEnum trigger_reason,
                                            const std::optional<ocpp::v2::IdToken>& id_token,
                                            const std::optional<std::string>& signed_meter_value,
                                            ocpp::v2::ChargingStateEnum charging_state) {
    check_configured("on_transaction_finished");
    const auto v2_reason = module::conversions::to_ocpp_reason(reason);
    m_charge_point->on_transaction_finished(evse_id, timestamp, meter_stop, v2_reason, trigger_reason, id_token,
                                            signed_meter_value, charging_state);
}
void ChargePointV2::on_transaction_started(
    std::int32_t evse_id, std::int32_t connector_id, const std::string& session_id, const ocpp::DateTime& timestamp,
    ocpp::v2::TriggerReasonEnum trigger_reason, const ocpp::v2::MeterValue& meter_start,
    const std::optional<ocpp::v2::IdToken>& id_token, const std::optional<ocpp::v2::IdToken>& group_id_token,
    const std::optional<std::int32_t>& reservation_id, const std::optional<std::int32_t>& remote_start_id,
    ocpp::v2::ChargingStateEnum charging_state) {
    check_configured("on_transaction_started");
    m_charge_point->on_transaction_started(evse_id, connector_id, session_id, timestamp, trigger_reason, meter_start,
                                           id_token, group_id_token, reservation_id, remote_start_id, charging_state);
}
void ChargePointV2::on_unavailable(std::int32_t evse_id, std::int32_t connector_id) {
    check_configured("on_unavailable");
    m_charge_point->on_unavailable(evse_id, connector_id);
}

void ChargePointV2::register_variable_listener(const ocpp::v2::Component& component, const ocpp::v2::Variable& variable,
                                               listener_t listener) {
    check_configured("register_variable_listener");
    bool registered = false;
    {
        auto handle = m_variable_listener.handle();
        if (*handle == nullptr && listener != nullptr) {
            *handle = std::move(listener);
            registered = true;
        }
    }
    if (registered) {
        // register outside the lock: libocpp may fire the callback synchronously
        m_charge_point->register_variable_listener([this](auto&&... args) { cb_variable_listener(args...); });
    }
}

std::vector<ocpp::v2::SetVariableResult>
ChargePointV2::set_variables(const std::vector<ocpp::v2::SetVariableData>& set_variable_data_vector,
                             const std::string& source) {
    check_configured("set_variables");
    const auto res = m_charge_point->set_variables(set_variable_data_vector, source);
    std::vector<ocpp::v2::SetVariableResult> result;
    result.reserve(res.size());
    for (const auto& item : res) {
        result.push_back(item.second);
    }
    return result;
}

bool ChargePointV2::set_powermeter_public_key(std::int32_t connector, const std::string& public_key_pem) {
    return false;
}

ocpp::v2::AuthorizeResponse ChargePointV2::validate_token(const types::authorization::ProvidedIdToken& provided_token) {
    check_configured("validate_token");
    using namespace module::conversions;

    ocpp::v2::AuthorizeResponse response;

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
        response = m_charge_point->validate_token(id_token, certificate_opt, ocsp_request_data_opt);

        // Publish tariff message on the session_cost interface
        const auto validation_result = module::conversions::to_everest_validation_result(response);
        if (!validation_result.tariff_messages.empty()) {
            types::session_cost::TariffMessage tariff_message;
            tariff_message.messages = validation_result.tariff_messages;
            tariff_message.identifier_id = provided_token.id_token.value;
            tariff_message.identifier_type = types::display_message::IdentifierType::IdToken;
            m_callbacks_ptr->cb_tariff_message(tariff_message);
        }
    } catch (const ocpp::StringConversionException& e) {
        EVLOG_warning << "Error converting id token to validate: " << e.what();
        response.idTokenInfo.status = ocpp::v2::AuthorizationStatusEnum::Unknown;
    } catch (const std::exception& e) {
        EVLOG_warning << "Unknown error during validation of id token: " << e.what();
        response.idTokenInfo.status = ocpp::v2::AuthorizationStatusEnum::Unknown;
    }

    return response;
}

} // namespace ocpp_multi
