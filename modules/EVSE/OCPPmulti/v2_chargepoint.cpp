// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "v2_chargepoint.hpp"
#include "generic_chargepoint_interface.hpp"
#include "ocpp/v2/ocpp_enums.hpp"

namespace {
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
    callbacks.set_charging_profiles_callback = [this]() { m_callbacks_ptr->cb_charging_schedules_timer(); };
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

    // TODO(james-ctc): how to share the database

    // clang-format off
    m_charge_point =
        std::make_unique<ocpp::v2::ChargePoint>(
            args.evse_connector_structure,
            std::move(args.device_model_storage_interface), // move is problematic
            args.ocpp_share_path,
            args.core_database_path,
            args.sql_init_path,
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

std::vector<ocpp::v2::EnhancedCompositeSchedule>
ChargePointV2::get_all_composite_schedules(std::int32_t duration_s, const ocpp::v2::ChargingRateUnitEnum& unit) {
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
                                                           const ocpp::v2::FirmwareStatusEnum& firmware_update_status) {
    check_configured("on_firmware_update_status_notification");
    m_charge_point->on_firmware_update_status_notification(request_id, firmware_update_status);
}
ocpp::v2::Get15118EVCertificateResponse
ChargePointV2::on_get_15118_ev_certificate_request(const ocpp::v2::Get15118EVCertificateRequest& request) {
    check_configured("on_get_15118_ev_certificate_request");
    return m_charge_point->on_get_15118_ev_certificate_request(request);
}
void ChargePointV2::on_log_status_notification(ocpp::v2::UploadLogStatusEnum status, std::int32_t requestId) {
    check_configured("on_log_status_notification");
    m_charge_point->on_log_status_notification(status, requestId);
}
void ChargePointV2::on_meter_value(std::int32_t evse_id, const ocpp::v2::MeterValue& meter_value) {
    check_configured("on_meter_value");
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
void ChargePointV2::on_session_finished(std::int32_t evse_id, std::int32_t connector_id) {
    check_configured("on_session_finished");
    m_charge_point->on_session_finished(evse_id, connector_id);
}
void ChargePointV2::on_session_started(std::int32_t evse_id, std::int32_t connector_id) {
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

void ChargePointV2::register_variable_listener(listener_t&& listener) {
    check_configured("register_variable_listener");
    m_charge_point->register_variable_listener(std::move(listener));
}
std::map<ocpp::v2::SetVariableData, ocpp::v2::SetVariableResult>
ChargePointV2::set_variables(const std::vector<ocpp::v2::SetVariableData>& set_variable_data_vector,
                             const std::string& source) {
    check_configured("set_variables");
    return m_charge_point->set_variables(set_variable_data_vector, source);
}

ocpp::v2::AuthorizeResponse
ChargePointV2::validate_token(const ocpp::v2::IdToken& id_token,
                              const std::optional<ocpp::CiString<10000>>& certificate,
                              const std::optional<std::vector<ocpp::v2::OCSPRequestData>>& ocsp_request_data) {
    check_configured("validate_token");
    return m_charge_point->validate_token(id_token, certificate, ocsp_request_data);
}

} // namespace ocpp_multi
