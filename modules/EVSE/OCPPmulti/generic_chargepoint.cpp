// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "generic_chargepoint.hpp"

namespace ocpp_multi {

// ----------------------------------------------------------------------------
// GenericChargePoint

void GenericChargePoint::check_configured(const std::string_view& fn) {
    if (m_active_ptr == nullptr) {
        std::string msg{"GenericChargePoint not configured: "};
        msg += fn;
        throw NotConfigured(msg);
    }
}

void GenericChargePoint::init(init_args_t& args) {
    // TODO(james-ctc): how to share the database

    m_state = state_t::idle;
    m_charge_point_v16 = std::make_unique<ChargePointV16>(m_charge_point_callbacks, m_evse_security_interface);
    m_charge_point_v2 = std::make_unique<ChargePointV2>(m_charge_point_callbacks, m_evse_security_interface);

    switch (m_mode) {
    case modes_t::ocpp_1_6_only:
    case modes_t::prefer_ocpp_1_6:
        m_active_ptr = m_charge_point_v16.get();
        m_state = state_t::v16_active;
        break;
    case modes_t::ocpp_2_only:
    case modes_t::prefer_ocpp_2:
    default:
        m_active_ptr = m_charge_point_v2.get();
        m_state = state_t::v2_active;
        break;
    }

    m_active_ptr->init(args);
}

void GenericChargePoint::connect_websocket() {
    check_configured("connect_websocket");
    m_active_ptr->connect_websocket();
}

void GenericChargePoint::disconnect_websocket() {
    check_configured("disconnect_websocket");
    m_active_ptr->disconnect_websocket();
}

void GenericChargePoint::set_message_queue_resume_delay(std::chrono::seconds delay) {
    check_configured("set_message_queue_resume_delay");
    m_active_ptr->set_message_queue_resume_delay(delay);
}

bool GenericChargePoint::restart() {
    check_configured("restart");
    return m_active_ptr->restart();
}

void GenericChargePoint::start(ocpp::v2::BootReasonEnum bootreason, const std::set<std::string>& resuming_session_ids,
                               bool start_connecting) {
    check_configured("start");
    m_active_ptr->start(bootreason, resuming_session_ids, start_connecting);
}

void GenericChargePoint::stop() {
    check_configured("stop");
    m_active_ptr->stop();
}

std::optional<ocpp::v2::DataTransferResponse>
GenericChargePoint::data_transfer_req(const ocpp::v2::DataTransferRequest& request) {
    check_configured("data_transfer_req");
    return m_active_ptr->data_transfer_req(request);
}

std::optional<bool> GenericChargePoint::get_central_contract_validation_allowed() {
    check_configured("get_central_contract_validation_allowed");
    return m_active_ptr->get_central_contract_validation_allowed();
}

std::optional<bool> GenericChargePoint::get_contract_certificate_installation_enabled() {
    check_configured("get_contract_certificate_installation_enabled");
    return m_active_ptr->get_contract_certificate_installation_enabled();
}

std::optional<bool> GenericChargePoint::get_pnc_enabled() {
    check_configured("get_pnc_enabled");
    return m_active_ptr->get_pnc_enabled();
}

std::optional<std::int32_t> GenericChargePoint::get_ev_connection_timeout() {
    check_configured("get_ev_connection_timeout");
    return m_active_ptr->get_ev_connection_timeout();
}

std::optional<std::string> GenericChargePoint::get_setpoint_priority() {
    check_configured("get_setpoint_priority");
    return m_active_ptr->get_setpoint_priority();
}

std::optional<std::string> GenericChargePoint::get_master_pass_group_id() {
    check_configured("get_master_pass_group_id");
    return m_active_ptr->get_master_pass_group_id();
}

std::optional<std::string> GenericChargePoint::get_tx_start_point() {
    check_configured("get_tx_start_point");
    return m_active_ptr->get_tx_start_point();
}

std::optional<std::string> GenericChargePoint::get_tx_stop_point() {
    check_configured("get_tx_stop_point");
    return m_active_ptr->get_tx_stop_point();
}

std::vector<ocpp::v2::EnhancedCompositeSchedule>
GenericChargePoint::get_all_composite_schedules(std::int32_t duration_s, ocpp::v2::ChargingRateUnitEnum unit) {
    check_configured("get_all_composite_schedules");
    return m_active_ptr->get_all_composite_schedules(duration_s, unit);
}

std::vector<ocpp::v2::GetVariableResult>
GenericChargePoint::get_variables(const std::vector<ocpp::v2::GetVariableData>& get_variable_data_vector) {
    check_configured("get_variables");
    return m_active_ptr->get_variables(get_variable_data_vector);
}

void GenericChargePoint::on_authorized(std::int32_t evse_id, std::int32_t connector_id,
                                       const ocpp::v2::IdToken& id_token) {
    check_configured("on_authorized");
    m_active_ptr->on_authorized(evse_id, connector_id, id_token);
}

ocpp::v2::ChangeAvailabilityResponse
GenericChargePoint::on_change_availability(const ocpp::v2::ChangeAvailabilityRequest& request) {
    check_configured("on_change_availability");
    return m_active_ptr->on_change_availability(request);
}

bool GenericChargePoint::on_charging_state_changed(std::uint32_t evse_id, ocpp::v2::ChargingStateEnum charging_state,
                                                   ocpp::v2::TriggerReasonEnum trigger_reason) {
    check_configured("on_charging_state_changed");
    return m_active_ptr->on_charging_state_changed(evse_id, charging_state, trigger_reason);
}

void GenericChargePoint::on_enabled(std::int32_t evse_id, std::int32_t connector_id) {
    check_configured("on_enabled");
    m_active_ptr->on_enabled(evse_id, connector_id);
}

void GenericChargePoint::on_ev_charging_needs(const ocpp::v2::NotifyEVChargingNeedsRequest& request) {
    check_configured("on_ev_charging_needs");
    m_active_ptr->on_ev_charging_needs(request);
}

void GenericChargePoint::on_event(const EventInfo& event) {
    check_configured("on_event");
    m_active_ptr->on_event(event);
}

void GenericChargePoint::on_event_authorised(std::int32_t evse_id, std::int32_t connector_id,
                                             const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_event_authorised");
    m_active_ptr->on_event_authorised(evse_id, connector_id, session_event);
}
void GenericChargePoint::on_event_deauthorised(std::int32_t evse_id, std::int32_t connector_id,
                                               const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_event_deauthorised");
    m_active_ptr->on_event_deauthorised(evse_id, connector_id, session_event);
}
void GenericChargePoint::on_event_disabled(std::int32_t evse_id, std::int32_t connector_id,
                                           const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_event_disabled");
    m_active_ptr->on_event_disabled(evse_id, connector_id, session_event);
}
void GenericChargePoint::on_event_enabled(std::int32_t evse_id, std::int32_t connector_id,
                                          const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_event_enabled");
    m_active_ptr->on_event_enabled(evse_id, connector_id, session_event);
}
void GenericChargePoint::on_event_charging_paused_ev(std::int32_t evse_id, std::int32_t connector_id,
                                                     const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_event_charging_paused_ev");
    m_active_ptr->on_event_charging_paused_ev(evse_id, connector_id, session_event);
}
void GenericChargePoint::on_event_charging_paused_evse(std::int32_t evse_id, std::int32_t connector_id,
                                                       const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_event_charging_paused_evse");
    m_active_ptr->on_event_charging_paused_evse(evse_id, connector_id, session_event);
}
void GenericChargePoint::on_event_charging_started(std::int32_t evse_id, std::int32_t connector_id,
                                                   const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_event_charging_started");
    m_active_ptr->on_event_charging_started(evse_id, connector_id, session_event);
}
void GenericChargePoint::on_event_plugin_timeout(std::int32_t evse_id, std::int32_t connector_id,
                                                 const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_event_plugin_timeout");
    m_active_ptr->on_event_plugin_timeout(evse_id, connector_id, session_event);
}
void GenericChargePoint::on_event_reservation_end(std::int32_t evse_id, std::int32_t connector_id,
                                                  const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_event_reservation_end");
    m_active_ptr->on_event_reservation_end(evse_id, connector_id, session_event);
}
void GenericChargePoint::on_event_reservation_start(std::int32_t evse_id, std::int32_t connector_id,
                                                    const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_event_reservation_start");
    m_active_ptr->on_event_reservation_start(evse_id, connector_id, session_event);
}
void GenericChargePoint::on_event_session_finished(std::int32_t evse_id, std::int32_t connector_id,
                                                   const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_event_session_finished");
    m_active_ptr->on_event_session_finished(evse_id, connector_id, session_event);
}
void GenericChargePoint::on_event_session_resumed(std::int32_t evse_id, std::int32_t connector_id,
                                                  const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_event_session_resumed");
    m_active_ptr->on_event_session_resumed(evse_id, connector_id, session_event);
}
GenericChargePointInterface::SessionResult
GenericChargePoint::on_event_session_started(std::int32_t evse_id, std::int32_t connector_id,
                                             const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_event_session_started");
    return m_active_ptr->on_event_session_started(evse_id, connector_id, session_event);
}
void GenericChargePoint::on_event_switching_phases(std::int32_t evse_id, std::int32_t connector_id,
                                                   const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_event_switching_phases");
    m_active_ptr->on_event_switching_phases(evse_id, connector_id, session_event);
}
void GenericChargePoint::on_event_transaction_finished(std::int32_t evse_id, std::int32_t connector_id,
                                                       const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_event_transaction_finished");
    m_active_ptr->on_event_transaction_finished(evse_id, connector_id, session_event);
}
GenericChargePointInterface::SessionResult
GenericChargePoint::on_event_transaction_started(std::int32_t evse_id, std::int32_t connector_id,
                                                 const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_event_transaction_started");
    return m_active_ptr->on_event_transaction_started(evse_id, connector_id, session_event);
}

void GenericChargePoint::on_fault_cleared(std::int32_t evse_id, std::int32_t connector_id) {
    check_configured("on_fault_cleared");
    m_active_ptr->on_fault_cleared(evse_id, connector_id);
}

void GenericChargePoint::on_faulted(std::int32_t evse_id, std::int32_t connector_id) {
    check_configured("on_faulted");
    m_active_ptr->on_faulted(evse_id, connector_id);
}

void GenericChargePoint::on_firmware_update_status_notification(std::int32_t request_id,
                                                                ocpp::v2::FirmwareStatusEnum firmware_update_status,
                                                                bool disable_connectors_during_install) {
    check_configured("on_firmware_update_status_notification");
    m_active_ptr->on_firmware_update_status_notification(request_id, firmware_update_status,
                                                         disable_connectors_during_install);
}

void GenericChargePoint::on_get_15118_ev_certificate_request(std::int32_t extensions_id,
                                                             const ocpp::v2::Get15118EVCertificateRequest& request) {
    check_configured("on_get_15118_ev_certificate_request");
    m_active_ptr->on_get_15118_ev_certificate_request(extensions_id, request);
}

void GenericChargePoint::on_log_status_notification(ocpp::v2::UploadLogStatusEnum status, std::int32_t requestId) {
    check_configured("on_log_status_notification");
    m_active_ptr->on_log_status_notification(status, requestId);
}

void GenericChargePoint::on_meter_value(std::int32_t evse_id, std::optional<float> soc,
                                        const types::powermeter::Powermeter& power_meter) {
    check_configured("on_meter_value");
    m_active_ptr->on_meter_value(evse_id, soc, power_meter);
}

void GenericChargePoint::on_reservation_status(std::int32_t reservation_id,
                                               ocpp::v2::ReservationUpdateStatusEnum status) {
    check_configured("on_reservation_status");
    m_active_ptr->on_reservation_status(reservation_id, status);
}

void GenericChargePoint::on_reservation_cleared(std::int32_t evse_id, std::int32_t connector_id) {
    check_configured("on_reservation_cleared");
    m_active_ptr->on_reservation_cleared(evse_id, connector_id);
}

void GenericChargePoint::on_reserved(std::int32_t evse_id, std::int32_t connector_id) {
    check_configured("on_reserved");
    m_active_ptr->on_reserved(evse_id, connector_id);
}

void GenericChargePoint::on_security_event(const ocpp::CiString<50>& event_type,
                                           const std::optional<ocpp::CiString<255>>& tech_info,
                                           const std::optional<bool>& critical,
                                           const std::optional<ocpp::DateTime>& timestamp) {
    check_configured("on_security_event");
    m_active_ptr->on_security_event(event_type, tech_info, critical, timestamp);
}

void GenericChargePoint::on_session_finished(std::int32_t evse_id, std::int32_t connector_id,
                                             const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_session_finished");
    m_active_ptr->on_session_finished(evse_id, connector_id, session_event);
}

void GenericChargePoint::on_session_started(std::int32_t evse_id, std::int32_t connector_id,
                                            const types::evse_manager::SessionEvent& session_event) {
    check_configured("on_session_started");
    m_active_ptr->on_session_started(evse_id, connector_id, session_event);
}

void GenericChargePoint::on_transaction_finished(
    std::int32_t evse_id, const std::string& session_id, const ocpp::DateTime& timestamp,
    const ocpp::v2::MeterValue& meter_stop, types::evse_manager::StopTransactionReason reason,
    ocpp::v2::TriggerReasonEnum trigger_reason, const std::optional<ocpp::v2::IdToken>& id_token,
    const std::optional<std::string>& signed_meter_value, ocpp::v2::ChargingStateEnum charging_state) {
    check_configured("on_transaction_finished");
    m_active_ptr->on_transaction_finished(evse_id, session_id, timestamp, meter_stop, reason, trigger_reason, id_token,
                                          signed_meter_value, charging_state);
}

void GenericChargePoint::on_transaction_started(
    std::int32_t evse_id, std::int32_t connector_id, const std::string& session_id, const ocpp::DateTime& timestamp,
    ocpp::v2::TriggerReasonEnum trigger_reason, const ocpp::v2::MeterValue& meter_start,
    const std::optional<ocpp::v2::IdToken>& id_token, const std::optional<ocpp::v2::IdToken>& group_id_token,
    const std::optional<std::int32_t>& reservation_id, const std::optional<std::int32_t>& remote_start_id,
    ocpp::v2::ChargingStateEnum charging_state) {
    check_configured("on_transaction_started");
    m_active_ptr->on_transaction_started(evse_id, connector_id, session_id, timestamp, trigger_reason, meter_start,
                                         id_token, group_id_token, reservation_id, remote_start_id, charging_state);
}

void GenericChargePoint::on_unavailable(std::int32_t evse_id, std::int32_t connector_id) {
    check_configured("on_unavailable");
    m_active_ptr->on_unavailable(evse_id, connector_id);
}

void GenericChargePoint::register_variable_listener(const ocpp::v2::Component& component,
                                                    const ocpp::v2::Variable& variable, listener_t listener) {
    check_configured("register_variable_listener");
    m_active_ptr->register_variable_listener(component, variable, std::move(listener));
}

bool GenericChargePoint::set_powermeter_public_key(std::int32_t connector, const std::string& public_key_pem) {
    check_configured("set_powermeter_public_key");
    return m_active_ptr->set_powermeter_public_key(connector, public_key_pem);
}

std::vector<ocpp::v2::SetVariableResult>
GenericChargePoint::set_variables(const std::vector<ocpp::v2::SetVariableData>& set_variable_data_vector,
                                  const std::string& source) {
    check_configured("set_variables");
    return m_active_ptr->set_variables(set_variable_data_vector, source);
}

ocpp::v2::AuthorizeResponse
GenericChargePoint::validate_token(const types::authorization::ProvidedIdToken& provided_token) {
    check_configured("validate_token");
    return m_active_ptr->validate_token(provided_token);
}

} // namespace ocpp_multi
