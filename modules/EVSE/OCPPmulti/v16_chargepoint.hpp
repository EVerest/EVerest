// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include "generic_chargepoint_interface.hpp"

#include <ocpp/v16/charge_point_configuration.hpp>

namespace ocpp_multi {

class ChargePointV16 : public GenericChargePointInterface {
private:
    std::shared_ptr<EvseSecurity> m_evse_security;
    std::unique_ptr<ocpp::v16::ChargePointConfiguration> m_charge_point_configuration;
    std::unique_ptr<ocpp::v16::ChargePoint> m_charge_point;
    ocpp_multi::GenericChargePointCallbacks* m_callbacks_ptr{nullptr};

    struct config_info_t {
        const std::string& chargepoint_config_path;
        const std::string& message_log_path;
        const std::string& ocpp_share_path;
        const std::string& sql_init_path;
        const std::string& user_config_path;
        std::uint32_t numnber_of_connectors; // r_evse_manager.size();
    };

    void check_configured(const std::string_view& fn);
    void configure_callbacks();
    void configure_data_model(const config_info_t& config);

    void cb_boot_notification_response(const ocpp::v16::BootNotificationResponse& boot_notification_response);
    void cb_connection_state_changed(bool is_connected);
    ocpp::v16::DataTransferResponse cb_data_transfer(const ocpp::v16::DataTransferRequest& request);
    bool cb_disable_evse(std::int32_t connector);
    bool cb_enable_evse(std::int32_t connector);
    void cb_generic_configuration_key_changed(const ocpp::v16::KeyValue& key_value);
    void cb_get_15118_ev_certificate_response(const std::int32_t connector,
                                              const ocpp::v2::Get15118EVCertificateResponse& certificate_response,
                                              const ocpp::v2::CertificateActionEnum& certificate_action);
    bool cb_is_reset_allowed(const ocpp::v16::ResetType& reset_type);
    ocpp::ReservationCheckStatus cb_is_token_reserved_for_connector(const std::int32_t connector,
                                                                    const std::string& id_token);
    bool cb_pause_charging(std::int32_t connector);
    void cb_provide_token(const std::string& id_token, std::vector<std::int32_t> referenced_connectors,
                          bool prevalidated);
    ocpp::v16::ReservationStatus cb_reserve_now(std::int32_t reservation_id, std::int32_t connector,
                                                ocpp::DateTime expiryDate, ocpp::CiString<20> idTag,
                                                std::optional<ocpp::CiString<20>> parent_id);
    void cb_reset(const ocpp::v16::ResetType& reset_type);
    bool cb_resume_charging(std::int32_t connector);
    ocpp::v16::DataTransferResponse cb_session_cost(const ocpp::RunningCost& running_cost,
                                                    const std::uint32_t number_of_decimals);
    void cb_set_connection_timeout(std::int32_t connection_timeout);
    ocpp::v16::DataTransferResponse cb_set_display_message(const std::vector<ocpp::DisplayMessage>& messages);
    void cb_set_system_time(const std::string& system_time);
    void cb_signal_set_charging_profiles();
    ocpp::v16::UpdateFirmwareStatusEnumType cb_signed_update_firmware(const ocpp::v16::SignedUpdateFirmwareRequest msg);
    bool cb_stop_transaction(std::int32_t connector, ocpp::v16::Reason reason);
    ocpp::v16::DataTransferResponse cb_tariff_message(const ocpp::TariffMessage& message);
    void cb_transaction_started(const std::int32_t connector, const std::string& session_id);
    void cb_transaction_stopped(const std::int32_t connector, const std::string& session_id,
                                const std::int32_t transaction_id);
    void cb_transaction_updated(const std::int32_t connector, const std::string& session_id,
                                const std::int32_t transaction_id, const ocpp::v16::IdTagInfo& id_tag_info);
    ocpp::v16::UnlockStatus cb_unlock_connector(std::int32_t connector);
    void cb_update_firmware(const ocpp::v16::UpdateFirmwareRequest msg);
    ocpp::v16::GetLogResponse cb_upload_diagnostics(const ocpp::v16::GetDiagnosticsRequest& request);
    ocpp::v16::GetLogResponse cb_upload_logs(ocpp::v16::GetLogRequest req);

public:
    explicit ChargePointV16(ocpp_multi::GenericChargePointCallbacks& callbacks, evse_securityIntf& security) :
        m_evse_security(std::make_unique<EvseSecurity>(security)), m_callbacks_ptr(&callbacks) {
    }

    void init(init_args_t& args) override;

    void connect_websocket() override;
    void disconnect_websocket() override;
    void set_message_queue_resume_delay(std::chrono::seconds delay) override;
    void start(ocpp::v2::BootReasonEnum bootreason, bool start_connecting) override;
    void stop() override;

    std::optional<ocpp::v2::DataTransferResponse>
    data_transfer_req(const ocpp::v2::DataTransferRequest& request) override;

    std::optional<bool> get_bool(const ocpp::v2::Component& component_id, const ocpp::v2::Variable& variable_id,
                                 ocpp::v2::AttributeEnum attribute_enum) override;
    std::optional<std::int32_t> get_int32(const ocpp::v2::Component& component_id,
                                          const ocpp::v2::Variable& variable_id,
                                          ocpp::v2::AttributeEnum attribute_enum) override;
    std::optional<std::string> get_string(const ocpp::v2::Component& component_id,
                                          const ocpp::v2::Variable& variable_id,
                                          ocpp::v2::AttributeEnum attribute_enum) override;

    std::vector<ocpp::v2::EnhancedCompositeSchedule>
    get_all_composite_schedules(std::int32_t duration_s, const ocpp::v2::ChargingRateUnitEnum& unit) override;
    std::vector<ocpp::v2::GetVariableResult>
    get_variables(const std::vector<ocpp::v2::GetVariableData>& get_variable_data_vector) override;

    void on_authorized(std::int32_t evse_id, std::int32_t connector_id, const ocpp::v2::IdToken& id_token) override;
    ocpp::v2::ChangeAvailabilityResponse
    on_change_availability(const ocpp::v2::ChangeAvailabilityRequest& request) override;
    bool on_charging_state_changed(std::uint32_t evse_id, ocpp::v2::ChargingStateEnum charging_state,
                                   ocpp::v2::TriggerReasonEnum trigger_reason) override;
    void on_enabled(std::int32_t evse_id, std::int32_t connector_id) override;
    void on_ev_charging_needs(const ocpp::v2::NotifyEVChargingNeedsRequest& request) override;
    void on_event(const std::vector<ocpp::v2::EventData>& events) override;
    void on_fault_cleared(std::int32_t evse_id, std::int32_t connector_id) override;
    void on_faulted(std::int32_t evse_id, std::int32_t connector_id) override;
    void on_firmware_update_status_notification(std::int32_t request_id,
                                                const ocpp::v2::FirmwareStatusEnum& firmware_update_status) override;
    ocpp::v2::Get15118EVCertificateResponse
    on_get_15118_ev_certificate_request(const ocpp::v2::Get15118EVCertificateRequest& request) override;
    void on_log_status_notification(ocpp::v2::UploadLogStatusEnum status, std::int32_t requestId) override;
    void on_meter_value(std::int32_t evse_id, const ocpp::v2::MeterValue& meter_value) override;
    void on_reservation_status(std::int32_t reservation_id, ocpp::v2::ReservationUpdateStatusEnum status) override;
    void on_reservation_cleared(std::int32_t evse_id, std::int32_t connector_id) override;
    void on_reserved(std::int32_t evse_id, std::int32_t connector_id) override;
    void on_security_event(const ocpp::CiString<50>& event_type, const std::optional<ocpp::CiString<255>>& tech_info,
                           const std::optional<bool>& critical,
                           const std::optional<ocpp::DateTime>& timestamp) override;
    void on_session_finished(std::int32_t evse_id, std::int32_t connector_id) override;
    void on_session_started(std::int32_t evse_id, std::int32_t connector_id) override;
    void on_transaction_finished(std::int32_t evse_id, const ocpp::DateTime& timestamp,
                                 const ocpp::v2::MeterValue& meter_stop, ocpp::v2::ReasonEnum reason,
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

    void register_variable_listener(listener_t&& listener) override;
    std::map<ocpp::v2::SetVariableData, ocpp::v2::SetVariableResult>
    set_variables(const std::vector<ocpp::v2::SetVariableData>& set_variable_data_vector,
                  const std::string& source) override;

    ocpp::v2::AuthorizeResponse
    validate_token(const ocpp::v2::IdToken& id_token, const std::optional<ocpp::CiString<10000>>& certificate,
                   const std::optional<std::vector<ocpp::v2::OCSPRequestData>>& ocsp_request_data) override;
};

} // namespace ocpp_multi
