// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "v16_chargepoint.hpp"

namespace ocpp_multi {

// ----------------------------------------------------------------------------
// OCPP 1.6 ChargePoint

void ChargePointV16::init(std::map<std::int32_t, std::int32_t>&& evse_connector_structure,
                          std::unique_ptr<ocpp::v2::DeviceModelStorageInterface>&& device_model_storage_interface,
                          const std::string& ocpp_share_path, const std::string& core_database_path,
                          const std::string& sql_init_path, const std::string& message_log_path,
                          ocpp::v2::Callbacks&& callbacks) {

    // clang-format off
    m_charge_point =
        std::make_unique<ocpp::v16::ChargePoint>(
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

void ChargePointV16::connect_websocket() {
    m_charge_point->connect_websocket();
}
void ChargePointV16::disconnect_websocket() {
    m_charge_point->disconnect_websocket();
}
void ChargePointV16::set_message_queue_resume_delay(std::chrono::seconds delay) {
    m_charge_point->set_message_queue_resume_delay(delay);
}
void ChargePointV16::start(ocpp::v2::BootReasonEnum bootreason, bool start_connecting) {
    ocpp::v16::BootReasonEnum bootreason_v16;
    switch (bootreason) {}
    // m_charge_point->start(bootreason, start_connecting);
}
void ChargePointV16::stop() {
    m_charge_point->stop();
}

std::optional<ocpp::v2::DataTransferResponse>
ChargePointV16::data_transfer_req(const ocpp::v2::DataTransferRequest& request) {
}

std::optional<bool> ChargePointV16::get_bool(const ocpp::v2::Component& component_id,
                                             const ocpp::v2::Variable& variable_id,
                                             const ocpp::v2::AttributeEnum& attribute_enum) {
}
std::optional<std::int32_t> ChargePointV16::get_int32(const ocpp::v2::Component& component_id,
                                                      const ocpp::v2::Variable& variable_id,
                                                      const ocpp::v2::AttributeEnum& attribute_enum) {
}
std::optional<std::string> ChargePointV16::get_string(const ocpp::v2::Component& component_id,
                                                      const ocpp::v2::Variable& variable_id,
                                                      const ocpp::v2::AttributeEnum& attribute_enum) {
}

std::vector<ocpp::v2::EnhancedCompositeSchedule>
ChargePointV16::get_all_composite_schedules(std::int32_t duration_s, const ocpp::v2::ChargingRateUnitEnum& unit) {
}
std::vector<ocpp::v2::GetVariableResult>
ChargePointV16::get_variables(const std::vector<ocpp::v2::GetVariableData>& get_variable_data_vector) {
}

void ChargePointV16::on_authorized(std::int32_t evse_id, std::int32_t connector_id, const ocpp::v2::IdToken& id_token) {
}
ocpp::v2::ChangeAvailabilityResponse
ChargePointV16::on_change_availability(const ocpp::v2::ChangeAvailabilityRequest& request) {
}
bool ChargePointV16::on_charging_state_changed(std::uint32_t evse_id, ocpp::v2::ChargingStateEnum charging_state,
                                               ocpp::v2::TriggerReasonEnum trigger_reason) {
}
void ChargePointV16::on_enabled(std::int32_t evse_id, std::int32_t connector_id) {
}
void ChargePointV16::on_ev_charging_needs(const ocpp::v2::NotifyEVChargingNeedsRequest& request) {
}
void ChargePointV16::on_event(const std::vector<ocpp::v2::EventData>& events) {
}
void ChargePointV16::on_fault_cleared(std::int32_t evse_id, std::int32_t connector_id) {
}
void ChargePointV16::on_faulted(std::int32_t evse_id, std::int32_t connector_id) {
}
void ChargePointV16::on_firmware_update_status_notification(
    std::int32_t request_id, const ocpp::v2::FirmwareStatusEnum& firmware_update_status) {
}
ocpp::v2::Get15118EVCertificateResponse
ChargePointV16::on_get_15118_ev_certificate_request(const ocpp::v2::Get15118EVCertificateRequest& request) {
}
void ChargePointV16::on_log_status_notification(ocpp::v2::UploadLogStatusEnum status, std::int32_t requestId) {
}
void ChargePointV16::on_meter_value(std::int32_t evse_id, const ocpp::v2::MeterValue& meter_value) {
}
void ChargePointV16::on_reservation_status(std::int32_t reservation_id, ocpp::v2::ReservationUpdateStatusEnum status) {
}
void ChargePointV16::on_reservation_cleared(std::int32_t evse_id, std::int32_t connector_id) {
}
void ChargePointV16::on_reserved(std::int32_t evse_id, std::int32_t connector_id) {
}
void ChargePointV16::on_security_event(const ocpp::CiString<50>& event_type,
                                       const std::optional<ocpp::CiString<255>>& tech_info,
                                       const std::optional<bool>& critical,
                                       const std::optional<ocpp::DateTime>& timestamp) {
}
void ChargePointV16::on_session_finished(std::int32_t evse_id, std::int32_t connector_id) {
}
void ChargePointV16::on_session_started(std::int32_t evse_id, std::int32_t connector_id) {
}
void ChargePointV16::on_transaction_finished(std::int32_t evse_id, const ocpp::DateTime& timestamp,
                                             const ocpp::v2::MeterValue& meter_stop, ocpp::v2::ReasonEnum reason,
                                             ocpp::v2::TriggerReasonEnum trigger_reason,
                                             const std::optional<ocpp::v2::IdToken>& id_token,
                                             const std::optional<std::string>& signed_meter_value,
                                             ocpp::v2::ChargingStateEnum charging_state) {
}
void ChargePointV16::on_transaction_started(
    std::int32_t evse_id, std::int32_t connector_id, const std::string& session_id, const ocpp::DateTime& timestamp,
    ocpp::v2::TriggerReasonEnum trigger_reason, const ocpp::v2::MeterValue& meter_start,
    const std::optional<ocpp::v2::IdToken>& id_token, const std::optional<ocpp::v2::IdToken>& group_id_token,
    const std::optional<std::int32_t>& reservation_id, const std::optional<std::int32_t>& remote_start_id,
    ocpp::v2::ChargingStateEnum charging_state) {
}
void ChargePointV16::on_unavailable(std::int32_t evse_id, std::int32_t connector_id) {
}

void ChargePointV16::register_variable_listener(listener_t&& listener) {
}
std::map<ocpp::v2::SetVariableData, ocpp::v2::SetVariableResult>
ChargePointV16::set_variables(const std::vector<ocpp::v2::SetVariableData>& set_variable_data_vector,
                              const std::string& source) {
}

ocpp::v2::AuthorizeResponse
ChargePointV16::validate_token(const ocpp::v2::IdToken& id_token,
                               const std::optional<ocpp::CiString<10000>>& certificate,
                               const std::optional<std::vector<ocpp::v2::OCSPRequestData>>& ocsp_request_data) {
}

} // namespace ocpp_multi
