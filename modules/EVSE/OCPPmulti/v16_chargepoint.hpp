// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include "generic_chargepoint_interface.hpp"
#include "ocpp/v2/ocpp_types.hpp"

#include <everest/conversions/ocpp/evse_security_ocpp.hpp>
#include <ocpp/v16/charge_point_configuration_interface.hpp>

namespace ocpp_multi {

class ChargePointV16 : public GenericChargePointInterface {
private:
    using key_monitor_map_t = std::map<std::string, std::pair<ocpp::v2::Component, ocpp::v2::Variable>>;

    std::shared_ptr<EvseSecurity> m_evse_security;
    std::unique_ptr<ocpp::v16::ChargePointConfigurationInterface> m_charge_point_configuration;
    std::unique_ptr<ocpp::v16::ChargePoint> m_charge_point;
    ocpp_multi::GenericChargePointCallbacks* m_callbacks_ptr{nullptr};
    listener_t m_variable_listener{nullptr};
    key_monitor_map_t m_monitor_map;
    ConnectorStructureV16 m_connector_mapping;

    struct config_info_t {
        const std::string& chargepoint_config_path;
        const std::string& message_log_path;
        const std::string& ocpp_share_path;
        const std::string& sql_init_path;
        const std::string& user_config_path;
        std::uint32_t number_of_connectors; // r_evse_manager.size();
        // Device-model database/migration/config paths shared with the OCPP 2.x path.
        std::string device_model_database_path;
        std::string device_model_database_migration_path;
        std::string device_model_config_path;
        std::string device_model_config_mappings;
        std::int32_t ocpp16_network_config_slot;
        bool enable_legacy_config_migration;
    };

    void check_configured(const std::string_view& fn);
    void configure_callbacks();
    void configure_data_model(const config_info_t& config);

    void cb_boot_notification_response(const ocpp::v16::BootNotificationResponse& boot_notification_response);
    void cb_connection_state_changed(bool is_connected);
    ocpp::v16::DataTransferResponse cb_data_transfer(const ocpp::v16::DataTransferRequest& request);
    void cb_default_price(const ocpp::TariffMessage& message);
    bool cb_disable_evse(std::int32_t connector);
    bool cb_enable_evse(std::int32_t connector);
    void cb_generic_configuration_key_changed(const ocpp::v16::KeyValue& key_value);
    bool cb_is_reset_allowed(const ocpp::v16::ResetType& reset_type);
    ocpp::ReservationCheckStatus cb_is_token_reserved_for_connector(std::int32_t connector,
                                                                    const std::string& id_token);
    void cb_provide_token(const std::string& id_token, std::vector<std::int32_t> referenced_connectors,
                          bool prevalidated);
    ocpp::v16::ReservationStatus cb_reserve_now(std::int32_t reservation_id, std::int32_t connector,
                                                ocpp::DateTime expiryDate, ocpp::CiString<20> idTag,
                                                std::optional<ocpp::CiString<20>> parent_id);
    void cb_reset(const ocpp::v16::ResetType& reset_type);
    ocpp::v16::DataTransferResponse cb_session_cost(const ocpp::RunningCost& running_cost,
                                                    std::uint32_t number_of_decimals);
    ocpp::v16::DataTransferResponse cb_set_display_message(const std::vector<ocpp::DisplayMessage>& messages);
    void cb_set_system_time(const std::string& system_time);
    ocpp::v16::UpdateFirmwareStatusEnumType cb_signed_update_firmware(ocpp::v16::SignedUpdateFirmwareRequest msg);
    bool cb_stop_transaction(std::int32_t connector, ocpp::v16::Reason reason);
    ocpp::v16::DataTransferResponse cb_tariff_message(const ocpp::TariffMessage& message);
    void cb_transaction_started(std::int32_t connector, const std::string& session_id);
    void cb_transaction_stopped(std::int32_t connector, const std::string& session_id, std::int32_t transaction_id);
    void cb_transaction_updated(std::int32_t connector, const std::string& session_id, std::int32_t transaction_id,
                                const ocpp::v16::IdTagInfo& id_tag_info);
    ocpp::v16::UnlockStatus cb_unlock_connector(std::int32_t connector);
    void cb_update_firmware(ocpp::v16::UpdateFirmwareRequest msg);
    ocpp::v16::GetLogResponse cb_upload_diagnostics(const ocpp::v16::GetDiagnosticsRequest& request);
    ocpp::v16::GetLogResponse cb_upload_logs(ocpp::v16::GetLogRequest request);
    void cb_variable_listener(const ocpp::v16::KeyValue& key_value);

    ocpp::v16::ErrorInfo convert_error(const Everest::error::Error& error);
    ocpp::v2::AuthorizeResponse validate_pnc(const types::authorization::ProvidedIdToken& provided_token);
    ocpp::v2::AuthorizeResponse validate_standard(const types::authorization::ProvidedIdToken& provided_token);

public:
    explicit ChargePointV16(ocpp_multi::GenericChargePointCallbacks& callbacks, evse_securityIntf& security) :
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
                                                ocpp::v2::FirmwareStatusEnum firmware_update_status,
                                                bool disable_connectors_during_install = true) override;
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

    static bool default_is_fault(const Everest::error::Error& error);
    static std::string default_vendor_error_code(const Everest::error::Error& error);
};

} // namespace ocpp_multi
