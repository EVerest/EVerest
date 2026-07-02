// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include "generic_chargepoint_interface.hpp"

#include <everest/conversions/ocpp/evse_security_ocpp.hpp>
#include <transaction_handler.hpp>

namespace ocpp_multi {

class ChargePointV2 : public GenericChargePointInterface {
private:
    using ResetType = ocpp_multi::GenericChargePointCallbacks::ResetType;

    std::shared_ptr<EvseSecurity> m_evse_security;
    ocpp_multi::GenericChargePointCallbacks* m_callbacks_ptr{nullptr};
    std::unique_ptr<ocpp::v2::ChargePoint> m_charge_point;
    listener_t m_variable_listener{nullptr};

    void check_configured(const std::string_view& fn);
    ocpp::v2::Callbacks configure_callbacks();

    void cb_default_price(const std::vector<ocpp::DisplayMessageContent>& messages);
    ocpp::v2::GetLogResponse cb_get_log_request(const ocpp::v2::GetLogRequest& request);
    bool cb_is_reset_allowed(const std::optional<const std::int32_t>& evse_id, const ocpp::v2::ResetEnum& type);
    ocpp::v2::RequestStartStopStatusEnum
    cb_remote_start_transaction(const ocpp::v2::RequestStartTransactionRequest& request, bool authorize_remote_start);
    void cb_reset(const std::optional<const std::int32_t>& evse_id, const ocpp::v2::ResetEnum& type);
    ocpp::v2::RequestStartStopStatusEnum cb_stop_transaction(std::int32_t evse_id,
                                                             const ocpp::v2::ReasonEnum& stop_reason);
    void cb_tariff_message(const ocpp::TariffMessage& message);
    void cb_variable_listener(const std::unordered_map<std::int64_t, ocpp::v2::VariableMonitoringMeta>& monitors,
                              const ocpp::v2::Component& component, const ocpp::v2::Variable& variable,
                              const ocpp::v2::VariableCharacteristics& characteristics,
                              const ocpp::v2::VariableAttribute& attribute, const std::string& value_previous,
                              const std::string& value_current);

    std::optional<bool> get_bool(const ocpp::v2::Component& component_id, const ocpp::v2::Variable& variable_id,
                                 ocpp::v2::AttributeEnum attribute_enum);
    std::optional<std::int32_t> get_int32(const ocpp::v2::Component& component_id,
                                          const ocpp::v2::Variable& variable_id,
                                          ocpp::v2::AttributeEnum attribute_enum);
    std::optional<std::string> get_string(const ocpp::v2::Component& component_id,
                                          const ocpp::v2::Variable& variable_id,
                                          ocpp::v2::AttributeEnum attribute_enum);

    void process_tx_event_effect(std::int32_t evse_id, module::TxEventEffect tx_event_effect,
                                 const types::evse_manager::SessionEvent& session_event);

public:
    explicit ChargePointV2(ocpp_multi::GenericChargePointCallbacks& callbacks, evse_securityIntf& security) :
        m_evse_security(std::make_unique<EvseSecurity>(security)), m_callbacks_ptr(&callbacks) {
    }

    // ------------------------------------------------------------------------
    // GenericChargePointConfiguration
    std::optional<bool> get_central_contract_validation_allowed() override;
    std::optional<bool> get_contract_certificate_installation_enabled() override;
    std::optional<bool> get_pnc_enabled() override;
    std::optional<std::int32_t> get_ev_connection_timeout() override;
    std::optional<std::string> get_setpoint_priority() override;
    std::optional<std::string> get_master_pass_group_id() override;
    std::optional<std::string> get_tx_start_point() override;
    std::optional<std::string> get_tx_stop_point() override;

    void init(init_args_t& args) override;
    void set_mode(modes_t new_mode) override {
    }

    void connect_websocket() override;
    void disconnect_websocket() override;
    void set_message_queue_resume_delay(std::chrono::seconds delay) override;
    bool restart() override;
    void start(ocpp::v2::BootReasonEnum bootreason, bool start_connecting) override;
    void stop() override;

    std::optional<ocpp::v2::DataTransferResponse>
    data_transfer_req(const ocpp::v2::DataTransferRequest& request) override;

    std::vector<ocpp::v2::EnhancedCompositeSchedule>
    get_all_composite_schedules(std::int32_t duration_s, ocpp::v2::ChargingRateUnitEnum unit) override;
    std::vector<ocpp::v2::GetVariableResult>
    get_variables(const std::vector<ocpp::v2::GetVariableData>& get_variable_data_vector) override;

    void on_authorized(std::int32_t evse_id, std::int32_t connector_id, const ocpp::v2::IdToken& id_token) override;
    ocpp::v2::ChangeAvailabilityResponse
    on_change_availability(const ocpp::v2::ChangeAvailabilityRequest& request) override;
    bool on_charging_state_changed(std::uint32_t evse_id, ocpp::v2::ChargingStateEnum charging_state,
                                   ocpp::v2::TriggerReasonEnum trigger_reason) override;
    void on_enabled(std::int32_t evse_id, std::int32_t connector_id) override;
    void on_ev_charging_needs(const ocpp::v2::NotifyEVChargingNeedsRequest& request) override;
    void on_event(const EventInfo& event) override;

    void on_event_authorised(std::int32_t evse_id, std::int32_t connector_id,
                             const types::evse_manager::SessionEvent& session_event) override;
    void on_event_deauthorised(std::int32_t evse_id, std::int32_t connector_id,
                               const types::evse_manager::SessionEvent& session_event) override;
    void on_event_disabled(std::int32_t evse_id, std::int32_t connector_id,
                           const types::evse_manager::SessionEvent& session_event) override;
    void on_event_enabled(std::int32_t evse_id, std::int32_t connector_id,
                          const types::evse_manager::SessionEvent& session_event) override;
    void on_event_charging_paused_ev(std::int32_t evse_id, std::int32_t connector_id,
                                     const types::evse_manager::SessionEvent& session_event) override;
    void on_event_charging_paused_evse(std::int32_t evse_id, std::int32_t connector_id,
                                       const types::evse_manager::SessionEvent& session_event) override;
    void on_event_charging_started(std::int32_t evse_id, std::int32_t connector_id,
                                   const types::evse_manager::SessionEvent& session_event) override;
    void on_event_plugin_timeout(std::int32_t evse_id, std::int32_t connector_id,
                                 const types::evse_manager::SessionEvent& session_event) override;
    void on_event_reservation_end(std::int32_t evse_id, std::int32_t connector_id,
                                  const types::evse_manager::SessionEvent& session_event) override;
    void on_event_reservation_start(std::int32_t evse_id, std::int32_t connector_id,
                                    const types::evse_manager::SessionEvent& session_event) override;
    void on_event_session_finished(std::int32_t evse_id, std::int32_t connector_id,
                                   const types::evse_manager::SessionEvent& session_event) override;
    void on_event_session_resumed(std::int32_t evse_id, std::int32_t connector_id,
                                  const types::evse_manager::SessionEvent& session_event) override;
    SessionResult on_event_session_started(std::int32_t evse_id, std::int32_t connector_id,
                                           const types::evse_manager::SessionEvent& session_event) override;
    void on_event_switching_phases(std::int32_t evse_id, std::int32_t connector_id,
                                   const types::evse_manager::SessionEvent& session_event) override;
    void on_event_transaction_finished(std::int32_t evse_id, std::int32_t connector_id,
                                       const types::evse_manager::SessionEvent& session_event) override;
    SessionResult on_event_transaction_started(std::int32_t evse_id, std::int32_t connector_id,
                                               const types::evse_manager::SessionEvent& session_event) override;

    void on_fault_cleared(std::int32_t evse_id, std::int32_t connector_id) override;
    void on_faulted(std::int32_t evse_id, std::int32_t connector_id) override;
    void on_firmware_update_status_notification(std::int32_t request_id,
                                                ocpp::v2::FirmwareStatusEnum firmware_update_status) override;
    void on_get_15118_ev_certificate_request(std::int32_t extensions_id,
                                             const ocpp::v2::Get15118EVCertificateRequest& request) override;
    void on_log_status_notification(ocpp::v2::UploadLogStatusEnum status, std::int32_t requestId) override;
    void on_meter_value(std::int32_t evse_id, std::optional<float> soc,
                        const types::powermeter::Powermeter& power_meter) override;
    void on_reservation_status(std::int32_t reservation_id, ocpp::v2::ReservationUpdateStatusEnum status) override;
    void on_reservation_cleared(std::int32_t evse_id, std::int32_t connector_id) override;
    void on_reserved(std::int32_t evse_id, std::int32_t connector_id) override;
    void on_security_event(const ocpp::CiString<50>& event_type, const std::optional<ocpp::CiString<255>>& tech_info,
                           const std::optional<bool>& critical,
                           const std::optional<ocpp::DateTime>& timestamp) override;
    void on_session_finished(std::int32_t evse_id, std::int32_t connector_id,
                             const types::evse_manager::SessionEvent& session_event) override;
    void on_session_started(std::int32_t evse_id, std::int32_t connector_id,
                            const types::evse_manager::SessionEvent& session_event) override;
    void on_transaction_finished(std::int32_t evse_id, const std::string& session_id, const ocpp::DateTime& timestamp,
                                 const ocpp::v2::MeterValue& meter_stop,
                                 types::evse_manager::StopTransactionReason reason,
                                 ocpp::v2::TriggerReasonEnum trigger_reason,
                                 const std::optional<ocpp::v2::IdToken>& id_token,
                                 const std::optional<std::string>& signed_meter_value,
                                 ocpp::v2::ChargingStateEnum charging_state) override;
    void on_transaction_started(std::int32_t evse_id, std::int32_t connector_id, const std::string& session_id,
                                const ocpp::DateTime& timestamp, ocpp::v2::TriggerReasonEnum trigger_reason,
                                const ocpp::v2::MeterValue& meter_start,
                                const std::optional<ocpp::v2::IdToken>& id_token,
                                const std::optional<ocpp::v2::IdToken>& group_id_token,
                                const std::optional<std::int32_t>& reservation_id,
                                const std::optional<std::int32_t>& remote_start_id,
                                ocpp::v2::ChargingStateEnum charging_state) override;
    void on_unavailable(std::int32_t evse_id, std::int32_t connector_id) override;

    void register_variable_listener(const ocpp::v2::Component& component, const ocpp::v2::Variable& variable,
                                    listener_t listener) override;
    bool set_powermeter_public_key(std::int32_t connector, const std::string& public_key_pem) override;
    std::vector<ocpp::v2::SetVariableResult>
    set_variables(const std::vector<ocpp::v2::SetVariableData>& set_variable_data_vector,
                  const std::string& source) override;

    ocpp::v2::AuthorizeResponse validate_token(const types::authorization::ProvidedIdToken& provided_token) override;
};

} // namespace ocpp_multi
