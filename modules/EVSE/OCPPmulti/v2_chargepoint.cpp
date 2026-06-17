// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "v2_chargepoint.hpp"
#include "generic_chargepoint_interface.hpp"
#include "ocpp/v2/ctrlr_component_variables.hpp"
#include "ocpp/v2/ocpp_enums.hpp"
#include "ocpp/v2/ocpp_types.hpp"
#include <conversions.hpp>
#include <device_model/composed_device_model_storage.hpp>

namespace {

constexpr const auto CENTRAL_CONTRACT_VALIDATION_ALLOWED_VAR_NAME = "CentralContractValidationAllowed";
constexpr const auto CONTRACT_CERTIFICATE_INSTALLATION_ENABLED_VAR_NAME = "ContractCertificateInstallationEnabled";
constexpr const auto EV_CONNECTION_TIMEOUT_VAR_NAME = "EVConnectionTimeOut";
constexpr const auto MASTER_PASS_GROUP_ID_VAR_NAME = "MasterPassGroupId";
constexpr const auto PNC_ENABLED_VAR_NAME = "PnCEnabled";
constexpr const auto SETPOINT_PRIORITY_VAR_NAME = "SetpointPriority";
constexpr const auto TX_START_POINT_VAR_NAME = "TxStartPoint";
constexpr const auto TX_STOP_POINT_VAR_NAME = "TxStopPoint";

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

} // namespace

namespace ocpp_multi {

// ----------------------------------------------------------------------------
// OCPP v2 ChargePoint

void ChargePointV2::cb_reset(const std::optional<const std::int32_t>& evse_id, const ocpp::v2::ResetEnum& type) {
    m_callbacks_ptr->cb_reset(evse_id, convert(type));
}

bool ChargePointV2::cb_is_reset_allowed(const std::optional<const std::int32_t>& evse_id,
                                        const ocpp::v2::ResetEnum& type) {
    return m_callbacks_ptr->cb_is_reset_allowed(evse_id, convert(type));
}

void ChargePointV2::check_configured(const std::string_view& fn) {
    if (m_charge_point == nullptr) {
        std::string msg{"ChargePointV2 not configured: "};
        msg += fn;
        throw NotConfigured(msg);
    }
}

ocpp::v2::RequestStartStopStatusEnum
ChargePointV2::cb_remote_start_transaction(const ocpp::v2::RequestStartTransactionRequest& request,
                                           bool authorize_remote_start) {
    ocpp_multi::GenericChargePointCallbacks::IdToken token;
    token.token = request.idToken;
    token.prevalidated = !authorize_remote_start;
    token.evse_id = request.evseId;
    token.request_id = request.remoteStartId;
    m_callbacks_ptr->cb_provide_token(token);
    return ocpp::v2::RequestStartStopStatusEnum::Accepted;
}

std::optional<bool> ChargePointV2::get_bool(const ocpp::v2::Component& component_id,
                                            const ocpp::v2::Variable& variable_id,
                                            ocpp::v2::AttributeEnum attribute_enum) {
    check_configured("get_bool");
    const auto result = m_charge_point->request_value<bool>(component_id, variable_id, attribute_enum);
    std::optional<bool> res;
    if (result.status == ocpp::v2::GetVariableStatusEnum::Accepted) {
        res = result.value;
    }
    return res;
}

std::optional<std::int32_t> ChargePointV2::get_int32(const ocpp::v2::Component& component_id,
                                                     const ocpp::v2::Variable& variable_id,
                                                     ocpp::v2::AttributeEnum attribute_enum) {
    check_configured("get_int32");
    const auto result = m_charge_point->request_value<std::int32_t>(component_id, variable_id, attribute_enum);
    std::optional<std::int32_t> res;
    if (result.status == ocpp::v2::GetVariableStatusEnum::Accepted) {
        res = result.value;
    }
    return res;
}
std::optional<std::string> ChargePointV2::get_string(const ocpp::v2::Component& component_id,
                                                     const ocpp::v2::Variable& variable_id,
                                                     ocpp::v2::AttributeEnum attribute_enum) {
    check_configured("get_string");
    const auto result = m_charge_point->request_value<std::string>(component_id, variable_id, attribute_enum);
    std::optional<std::string> res;
    if (result.status == ocpp::v2::GetVariableStatusEnum::Accepted) {
        res = result.value;
    }
    return res;
}

void ChargePointV2::cb_variable_listener(
    const std::unordered_map<std::int64_t, ocpp::v2::VariableMonitoringMeta>& monitors,
    const ocpp::v2::Component& component, const ocpp::v2::Variable& variable,
    const ocpp::v2::VariableCharacteristics& characteristics, const ocpp::v2::VariableAttribute& attribute,
    const std::string& value_previous, const std::string& value_current) {
    if (m_variable_listener != nullptr) {
        m_variable_listener(component, variable, value_current);
    }
}

ocpp::v2::Callbacks ChargePointV2::configure_callbacks() {
    ocpp::v2::Callbacks callbacks;

    // indirectly supported
    callbacks.reset_callback = [this](auto&&... args) { cb_reset(args...); };
    callbacks.is_reset_allowed_callback = [this](auto&&... args) { return cb_is_reset_allowed(args...); };
    callbacks.remote_start_transaction_callback = [this](auto&&... args) {
        return cb_remote_start_transaction(args...);
    };

    // directly supported
    callbacks.connector_effective_operative_status_changed_callback = [this](auto&&... args) {
        m_callbacks_ptr->cb_connector_effective_operative_status(args...);
    };
    callbacks.stop_transaction_callback = [this](auto&&... args) {
        return m_callbacks_ptr->cb_stop_transaction(args...);
    };
    callbacks.pause_charging_callback = [this](auto&&... args) { m_callbacks_ptr->cb_pause_charging(args...); };
    callbacks.unlock_connector_callback = [this](auto&&... args) {
        return m_callbacks_ptr->cb_unlock_connector(args...);
    };
    callbacks.get_log_request_callback = [this](const auto& request) {
        return m_callbacks_ptr->cb_get_log_request(request);
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
    callbacks.tariff_message_callback = [this](auto&&... args) { m_callbacks_ptr->cb_tariff_message(args...); };
    callbacks.default_price_callback = [this](auto&&... args) { m_callbacks_ptr->cb_default_price(args...); };
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
void ChargePointV2::start(ocpp::v2::BootReasonEnum bootreason, bool start_connecting) {
    check_configured("start");
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
void ChargePointV2::on_event(const std::vector<ocpp::v2::EventData>& events) {
    check_configured("on_event");
    m_charge_point->on_event(events);
}
void ChargePointV2::on_fault_cleared(std::int32_t evse_id, std::int32_t connector_id) {
    check_configured("on_fault_cleared");
    m_charge_point->on_fault_cleared(evse_id, connector_id);
}
void ChargePointV2::on_faulted(std::int32_t evse_id, std::int32_t connector_id) {
    check_configured("on_fault_cleared");
    m_charge_point->on_faulted(evse_id, connector_id);
}
void ChargePointV2::on_firmware_update_status_notification(std::int32_t request_id,
                                                           ocpp::v2::FirmwareStatusEnum firmware_update_status) {
    check_configured("on_firmware_update_status_notification");
    m_charge_point->on_firmware_update_status_notification(request_id, firmware_update_status);
}
ocpp::v2::Get15118EVCertificateResponse
ChargePointV2::on_get_15118_ev_certificate_request(std::int32_t extensions_id,
                                                   const ocpp::v2::Get15118EVCertificateRequest& request) {
    check_configured("on_get_15118_ev_certificate_request");
    return m_charge_point->on_get_15118_ev_certificate_request(request);
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
void ChargePointV2::on_transaction_finished(std::int32_t evse_id, const ocpp::DateTime& timestamp,
                                            const ocpp::v2::MeterValue& meter_stop, ocpp::v2::ReasonEnum reason,
                                            ocpp::v2::TriggerReasonEnum trigger_reason,
                                            const std::optional<ocpp::v2::IdToken>& id_token,
                                            const std::optional<std::string>& signed_meter_value,
                                            ocpp::v2::ChargingStateEnum charging_state) {
    check_configured("on_transaction_finished");
    m_charge_point->on_transaction_finished(evse_id, timestamp, meter_stop, reason, trigger_reason, id_token,
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

void ChargePointV2::register_variable_listener(const std::string& key, listener_t listener) {
    check_configured("register_variable_listener");
    if (m_variable_listener == nullptr && listener != nullptr) {
        m_variable_listener = std::move(listener);
        m_charge_point->register_variable_listener([this](auto&&... args) { cb_variable_listener(args...); });
    }
}
std::map<ocpp::v2::SetVariableData, ocpp::v2::SetVariableResult>
ChargePointV2::set_variables(const std::vector<ocpp::v2::SetVariableData>& set_variable_data_vector,
                             const std::string& source) {
    check_configured("set_variables");
    return m_charge_point->set_variables(set_variable_data_vector, source);
}

ocpp::v2::AuthorizeResponse ChargePointV2::validate_token(const types::authorization::ProvidedIdToken& provided_token) {
    check_configured("validate_token");
    using namespace module::conversions;

    ocpp::v2::AuthorizeResponse validation_result;

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
        validation_result = m_charge_point->validate_token(id_token, certificate_opt, ocsp_request_data_opt);
    } catch (const ocpp::StringConversionException& e) {
        EVLOG_warning << "Error converting id token to validate: " << e.what();
        validation_result.idTokenInfo.status = ocpp::v2::AuthorizationStatusEnum::Unknown;
    } catch (const std::exception& e) {
        EVLOG_warning << "Unknown error during validation of id token: " << e.what();
        validation_result.idTokenInfo.status = ocpp::v2::AuthorizationStatusEnum::Unknown;
    }

    return validation_result;
}

} // namespace ocpp_multi
