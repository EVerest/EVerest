// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "v2_chargepoint.hpp"

namespace ocpp_multi {

// ----------------------------------------------------------------------------
// OCPP v2 ChargePoint

void ChargePointV2::init(std::map<std::int32_t, std::int32_t>&& evse_connector_structure,
                         std::unique_ptr<ocpp::v2::DeviceModelStorageInterface>&& device_model_storage_interface,
                         const std::string& ocpp_share_path, const std::string& core_database_path,
                         const std::string& sql_init_path, const std::string& message_log_path,
                         ocpp::v2::Callbacks&& callbacks) {

    // TODO(james-ctc): how to share the database

    // clang-format off
    m_charge_point =
        std::make_unique<ocpp::v2::ChargePoint>(
            evse_connector_structure,
            std::move(device_model_storage_interface), // move is problematic
            ocpp_share_path,
            core_database_path,
            sql_init_path,
            message_log_path,
            m_evse_security,
            callbacks
        );
    // clang-format on
}

void ChargePointV2::connect_websocket() {
    m_charge_point->connect_websocket();
}
void ChargePointV2::disconnect_websocket() {
    m_charge_point->disconnect_websocket();
}
void ChargePointV2::set_message_queue_resume_delay(std::chrono::seconds delay) {
    m_charge_point->set_message_queue_resume_delay(delay);
}
void ChargePointV2::start(ocpp::v2::BootReasonEnum bootreason, bool start_connecting) {
    m_charge_point->start(bootreason, start_connecting);
}
void ChargePointV2::stop() {
    m_charge_point->stop();
}

std::optional<ocpp::v2::DataTransferResponse>
ChargePointV2::data_transfer_req(const ocpp::v2::DataTransferRequest& request) {
    return m_charge_point->data_transfer_req(request);
}

std::optional<bool> ChargePointV2::get_bool(const ocpp::v2::Component& component_id,
                                            const ocpp::v2::Variable& variable_id,
                                            ocpp::v2::AttributeEnum attribute_enum) {
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
    const auto result = m_charge_point->request_value<std::string>(component_id, variable_id, attribute_enum);
    std::optional<std::string> res;
    if (result.status == ocpp::v2::GetVariableStatusEnum::Accepted) {
        res = result.value;
    }
    return res;
}

std::vector<ocpp::v2::EnhancedCompositeSchedule>
ChargePointV2::get_all_composite_schedules(std::int32_t duration_s, const ocpp::v2::ChargingRateUnitEnum& unit) {
    return m_charge_point->get_all_composite_schedules(duration_s, unit);
}
std::vector<ocpp::v2::GetVariableResult>
ChargePointV2::get_variables(const std::vector<ocpp::v2::GetVariableData>& get_variable_data_vector) {
    return m_charge_point->get_variables(get_variable_data_vector);
}

void ChargePointV2::on_authorized(std::int32_t evse_id, std::int32_t connector_id, const ocpp::v2::IdToken& id_token) {
    m_charge_point->on_authorized(evse_id, connector_id, id_token);
}
ocpp::v2::ChangeAvailabilityResponse
ChargePointV2::on_change_availability(const ocpp::v2::ChangeAvailabilityRequest& request) {
    return m_charge_point->on_change_availability(request);
}
bool ChargePointV2::on_charging_state_changed(std::uint32_t evse_id, ocpp::v2::ChargingStateEnum charging_state,
                                              ocpp::v2::TriggerReasonEnum trigger_reason) {
    return m_charge_point->on_charging_state_changed(evse_id, charging_state, trigger_reason);
}
void ChargePointV2::on_enabled(std::int32_t evse_id, std::int32_t connector_id) {
    m_charge_point->on_enabled(evse_id, connector_id);
}
void ChargePointV2::on_ev_charging_needs(const ocpp::v2::NotifyEVChargingNeedsRequest& request) {
    m_charge_point->on_ev_charging_needs(request);
}
void ChargePointV2::on_event(const std::vector<ocpp::v2::EventData>& events) {
    m_charge_point->on_event(events);
}
void ChargePointV2::on_fault_cleared(std::int32_t evse_id, std::int32_t connector_id) {
    m_charge_point->on_fault_cleared(evse_id, connector_id);
}
void ChargePointV2::on_faulted(std::int32_t evse_id, std::int32_t connector_id) {
    m_charge_point->on_faulted(evse_id, connector_id);
}
void ChargePointV2::on_firmware_update_status_notification(std::int32_t request_id,
                                                           const ocpp::v2::FirmwareStatusEnum& firmware_update_status) {
    m_charge_point->on_firmware_update_status_notification(request_id, firmware_update_status);
}
ocpp::v2::Get15118EVCertificateResponse
ChargePointV2::on_get_15118_ev_certificate_request(const ocpp::v2::Get15118EVCertificateRequest& request) {
    return m_charge_point->on_get_15118_ev_certificate_request(request);
}
void ChargePointV2::on_log_status_notification(ocpp::v2::UploadLogStatusEnum status, std::int32_t requestId) {
    m_charge_point->on_log_status_notification(status, requestId);
}
void ChargePointV2::on_meter_value(std::int32_t evse_id, const ocpp::v2::MeterValue& meter_value) {
    m_charge_point->on_meter_value(evse_id, meter_value);
}
void ChargePointV2::on_reservation_status(std::int32_t reservation_id, ocpp::v2::ReservationUpdateStatusEnum status) {
    m_charge_point->on_reservation_status(reservation_id, status);
}
void ChargePointV2::on_reservation_cleared(std::int32_t evse_id, std::int32_t connector_id) {
    m_charge_point->on_reservation_cleared(evse_id, connector_id);
}
void ChargePointV2::on_reserved(std::int32_t evse_id, std::int32_t connector_id) {
    m_charge_point->on_reserved(evse_id, connector_id);
}
void ChargePointV2::on_security_event(const ocpp::CiString<50>& event_type,
                                      const std::optional<ocpp::CiString<255>>& tech_info,
                                      const std::optional<bool>& critical,
                                      const std::optional<ocpp::DateTime>& timestamp) {
    m_charge_point->on_security_event(event_type, tech_info, critical, timestamp);
}
void ChargePointV2::on_session_finished(std::int32_t evse_id, std::int32_t connector_id) {
    m_charge_point->on_session_finished(evse_id, connector_id);
}
void ChargePointV2::on_session_started(std::int32_t evse_id, std::int32_t connector_id) {
    m_charge_point->on_session_started(evse_id, connector_id);
}
void ChargePointV2::on_transaction_finished(std::int32_t evse_id, const ocpp::DateTime& timestamp,
                                            const ocpp::v2::MeterValue& meter_stop, ocpp::v2::ReasonEnum reason,
                                            ocpp::v2::TriggerReasonEnum trigger_reason,
                                            const std::optional<ocpp::v2::IdToken>& id_token,
                                            const std::optional<std::string>& signed_meter_value,
                                            ocpp::v2::ChargingStateEnum charging_state) {
    m_charge_point->on_transaction_finished(evse_id, timestamp, meter_stop, reason, trigger_reason, id_token,
                                            signed_meter_value, charging_state);
}
void ChargePointV2::on_transaction_started(
    std::int32_t evse_id, std::int32_t connector_id, const std::string& session_id, const ocpp::DateTime& timestamp,
    ocpp::v2::TriggerReasonEnum trigger_reason, const ocpp::v2::MeterValue& meter_start,
    const std::optional<ocpp::v2::IdToken>& id_token, const std::optional<ocpp::v2::IdToken>& group_id_token,
    const std::optional<std::int32_t>& reservation_id, const std::optional<std::int32_t>& remote_start_id,
    ocpp::v2::ChargingStateEnum charging_state) {
    m_charge_point->on_transaction_started(evse_id, connector_id, session_id, timestamp, trigger_reason, meter_start,
                                           id_token, group_id_token, reservation_id, remote_start_id, charging_state);
}
void ChargePointV2::on_unavailable(std::int32_t evse_id, std::int32_t connector_id) {
    m_charge_point->on_unavailable(evse_id, connector_id);
}

void ChargePointV2::register_variable_listener(listener_t&& listener) {
    m_charge_point->register_variable_listener(std::move(listener));
}
std::map<ocpp::v2::SetVariableData, ocpp::v2::SetVariableResult>
ChargePointV2::set_variables(const std::vector<ocpp::v2::SetVariableData>& set_variable_data_vector,
                             const std::string& source) {
    return m_charge_point->set_variables(set_variable_data_vector, source);
}

ocpp::v2::AuthorizeResponse
ChargePointV2::validate_token(const ocpp::v2::IdToken& id_token,
                              const std::optional<ocpp::CiString<10000>>& certificate,
                              const std::optional<std::vector<ocpp::v2::OCSPRequestData>>& ocsp_request_data) {
    return m_charge_point->validate_token(id_token, certificate, ocsp_request_data);
}

} // namespace ocpp_multi
