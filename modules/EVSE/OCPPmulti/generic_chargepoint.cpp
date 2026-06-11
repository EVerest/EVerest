// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "generic_chargepoint.hpp"

namespace ocpp_multi {

// ----------------------------------------------------------------------------
// GenericChargePoint

void GenericChargePoint::init(init_args_t& args) {
    m_evse_connector_structure = std::move(args.evse_connector_structure);
    m_device_model_storage_interface = std::move(args.device_model_storage_interface);
    m_ocpp_share_path = args.ocpp_share_path;
    m_core_database_path = args.core_database_path;
    m_sql_init_path = args.sql_init_path;
    m_message_log_path = args.message_log_path;

    // TODO(james-ctc): how to share the database

    m_state = state_t::v2_active;
}

void GenericChargePoint::connect_websocket() {
}

void GenericChargePoint::disconnect_websocket() {
}

void GenericChargePoint::set_message_queue_resume_delay(std::chrono::seconds delay) {
}

void GenericChargePoint::start(ocpp::v2::BootReasonEnum bootreason, bool start_connecting) {
}

void GenericChargePoint::stop() {
}

std::optional<ocpp::v2::DataTransferResponse>
GenericChargePoint::data_transfer_req(const ocpp::v2::DataTransferRequest& request) {
    return {};
}

template <typename T>
std::optional<T> GenericChargePoint::get(const ocpp::v2::Component& component_id, const ocpp::v2::Variable& variable_id,
                                         const ocpp::v2::AttributeEnum& attribute_enum) {
    std::optional<T> result;
    if (m_device_model) {
        const auto res = m_device_model->request_value<T>(component_id, variable_id, attribute_enum);
        if (res.status == ocpp::v2::GetVariableStatusEnum::Accepted) {
            result = res.value;
        }
    }
    return result;
}

template std::optional<bool> GenericChargePoint::get<bool>(const ocpp::v2::Component& component_id,
                                                           const ocpp::v2::Variable& variable_id,
                                                           const ocpp::v2::AttributeEnum& attribute_enum);
template std::optional<std::int32_t>
GenericChargePoint::get<std::int32_t>(const ocpp::v2::Component& component_id, const ocpp::v2::Variable& variable_id,
                                      const ocpp::v2::AttributeEnum& attribute_enum);
template std::optional<std::string> GenericChargePoint::get<std::string>(const ocpp::v2::Component& component_id,
                                                                         const ocpp::v2::Variable& variable_id,
                                                                         const ocpp::v2::AttributeEnum& attribute_enum);

std::vector<ocpp::v2::EnhancedCompositeSchedule>
GenericChargePoint::get_all_composite_schedules(std::int32_t duration_s, const ocpp::v2::ChargingRateUnitEnum& unit) {
    return {};
}

std::vector<ocpp::v2::GetVariableResult>
GenericChargePoint::get_variables(const std::vector<ocpp::v2::GetVariableData>& get_variable_data_vector) {
    return {};
}

void GenericChargePoint::on_authorized(std::int32_t evse_id, std::int32_t connector_id,
                                       const ocpp::v2::IdToken& id_token) {
}

ocpp::v2::ChangeAvailabilityResponse
GenericChargePoint::on_change_availability(const ocpp::v2::ChangeAvailabilityRequest& request) {
    return {};
}

bool GenericChargePoint::on_charging_state_changed(std::uint32_t evse_id, ocpp::v2::ChargingStateEnum charging_state,
                                                   ocpp::v2::TriggerReasonEnum trigger_reason) {
    return false;
}

void GenericChargePoint::on_enabled(std::int32_t evse_id, std::int32_t connector_id) {
}

void GenericChargePoint::on_ev_charging_needs(const ocpp::v2::NotifyEVChargingNeedsRequest& request) {
}

void GenericChargePoint::on_event(const std::vector<ocpp::v2::EventData>& events) {
}

void GenericChargePoint::on_fault_cleared(std::int32_t evse_id, std::int32_t connector_id) {
}

void GenericChargePoint::on_faulted(std::int32_t evse_id, std::int32_t connector_id) {
}

void GenericChargePoint::on_firmware_update_status_notification(
    std::int32_t request_id, const ocpp::v2::FirmwareStatusEnum& firmware_update_status) {
}

ocpp::v2::Get15118EVCertificateResponse
GenericChargePoint::on_get_15118_ev_certificate_request(const ocpp::v2::Get15118EVCertificateRequest& request) {
    return {};
}

void GenericChargePoint::on_log_status_notification(ocpp::v2::UploadLogStatusEnum status, std::int32_t requestId) {
}

void GenericChargePoint::on_meter_value(std::int32_t evse_id, const ocpp::v2::MeterValue& meter_value) {
}

void GenericChargePoint::on_reservation_status(std::int32_t reservation_id,
                                               ocpp::v2::ReservationUpdateStatusEnum status) {
}

void GenericChargePoint::on_reservation_cleared(std::int32_t evse_id, std::int32_t connector_id) {
}

void GenericChargePoint::on_reserved(std::int32_t evse_id, std::int32_t connector_id) {
}

void GenericChargePoint::on_security_event(const ocpp::CiString<50>& event_type,
                                           const std::optional<ocpp::CiString<255>>& tech_info,
                                           const std::optional<bool>& critical,
                                           const std::optional<ocpp::DateTime>& timestamp) {
}

void GenericChargePoint::on_session_finished(std::int32_t evse_id, std::int32_t connector_id) {
}

void GenericChargePoint::on_session_started(std::int32_t evse_id, std::int32_t connector_id) {
}

void GenericChargePoint::on_transaction_finished(std::int32_t evse_id, const ocpp::DateTime& timestamp,
                                                 const ocpp::v2::MeterValue& meter_stop, ocpp::v2::ReasonEnum reason,
                                                 ocpp::v2::TriggerReasonEnum trigger_reason,
                                                 const std::optional<ocpp::v2::IdToken>& id_token,
                                                 const std::optional<std::string>& signed_meter_value,
                                                 ocpp::v2::ChargingStateEnum charging_state) {
}

void GenericChargePoint::on_transaction_started(
    std::int32_t evse_id, std::int32_t connector_id, const std::string& session_id, const ocpp::DateTime& timestamp,
    ocpp::v2::TriggerReasonEnum trigger_reason, const ocpp::v2::MeterValue& meter_start,
    const std::optional<ocpp::v2::IdToken>& id_token, const std::optional<ocpp::v2::IdToken>& group_id_token,
    const std::optional<std::int32_t>& reservation_id, const std::optional<std::int32_t>& remote_start_id,
    ocpp::v2::ChargingStateEnum charging_state) {
}

void GenericChargePoint::on_unavailable(std::int32_t evse_id, std::int32_t connector_id) {
}

void GenericChargePoint::register_variable_listener(listener_t&& listener) {
}

std::map<ocpp::v2::SetVariableData, ocpp::v2::SetVariableResult>
GenericChargePoint::set_variables(const std::vector<ocpp::v2::SetVariableData>& set_variable_data_vector,
                                  const std::string& source) {
    return {};
}

ocpp::v2::AuthorizeResponse
GenericChargePoint::validate_token(const ocpp::v2::IdToken& id_token,
                                   const std::optional<ocpp::CiString<10000>>& certificate,
                                   const std::optional<std::vector<ocpp::v2::OCSPRequestData>>& ocsp_request_data) {
    return {};
}

} // namespace ocpp_multi
