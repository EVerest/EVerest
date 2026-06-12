// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <everest/conversions/ocpp/evse_security_ocpp.hpp>
#include <generated/interfaces/evse_manager/Interface.hpp>
#include <generated/interfaces/iso15118_extensions/Interface.hpp>
#include <generated/interfaces/reservation/Interface.hpp>
#include <generated/interfaces/system/Interface.hpp>
#include <ocpp/v16/charge_point.hpp>
#include <ocpp/v16/messages/GetConfiguration.hpp>
#include <ocpp/v2/charge_point.hpp>
#include <ocpp/v2/charge_point_callbacks.hpp>
#include <ocpp/v2/device_model_storage_interface.hpp>
#include <ocpp/v2/messages/Authorize.hpp>
#include <ocpp/v2/messages/ChangeAvailability.hpp>
#include <ocpp/v2/messages/Get15118EVCertificate.hpp>
#include <ocpp/v2/messages/NotifyEVChargingNeeds.hpp>
#include <ocpp/v2/ocpp_enums.hpp>
#include <ocpp/v2/ocpp_types.hpp>
#include <ocpp/v2/types.hpp>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

namespace ocpp_multi {

struct GenericChargePointCallbacks {
    virtual ~GenericChargePointCallbacks() = default;

    enum class ResetType : std::uint8_t {
        // v1.6
        Hard,
        Soft,
        // v2
        Immediate,
        OnIdle,
        ImmediateAndResume,
    };

    struct IdToken {
        ocpp::v2::IdToken token;
        bool prevalidated;
        std::vector<std::int32_t> connectors;   // v1.6 only
        std::optional<std::int32_t> evse_id;    // v2.x only
        std::optional<std::int32_t> request_id; // v2.x only
        std::optional<ocpp::v2::IdToken> group_id_token;
    };

    virtual void cb_all_connectors_unavailable() = 0;
    virtual void cb_boot_notification(const ocpp::v2::BootNotificationResponse& boot_notification_response) = 0;
    virtual bool cb_cancel_reservation(std::int32_t reservation_id) = 0;
    virtual void cb_charging_needs(std::int32_t extensions_id,
                                   const types::iso15118::ChargingNeeds& charging_needs) = 0;
    virtual ocpp::v2::ClearDisplayMessageResponse
    cb_clear_display_message(const ocpp::v2::ClearDisplayMessageRequest& request) = 0;
    virtual std::future<ocpp::ConfigNetworkResult> cb_configure_network_connection_profile() = 0;
    virtual bool cb_connector_effective_operative_status(std::int32_t evse_id, std::int32_t connector_id,
                                                         ocpp::v2::OperationalStatusEnum new_status) = 0;
    virtual void cb_connection_state_changed(bool is_connected, ocpp::OcppProtocolVersion protocol_version) = 0;
    virtual ocpp::v2::DataTransferResponse cb_data_transfer(const ocpp::v2::DataTransferRequest& request) = 0;
    virtual void cb_default_price(const std::vector<ocpp::DisplayMessageContent>& messages) = 0;
    virtual void cb_ev_info(std::int32_t evse_id, const types::evse_manager::EVInfo& ev_info) = 0;
    virtual void cb_fault_cleared_handler(std::int32_t evse_id, const Everest::error::Error& error) = 0;
    virtual void cb_fault_handler(std::int32_t evse_id, const Everest::error::Error& error) = 0;
    virtual void cb_firmware_update_status(types::system::FirmwareUpdateStatus status) = 0;
    virtual std::vector<ocpp::DisplayMessage>
    cb_get_display_message(const ocpp::v2::GetDisplayMessagesRequest& request) = 0;
    virtual ocpp::v2::GetLogResponse cb_get_log_request(const ocpp::v2::GetLogRequest& request) = 0;
    virtual void cb_hw_capabilities(std::int32_t evse_id,
                                    const types::evse_board_support::HardwareCapabilities& hw_capabilities) = 0;
    virtual ocpp::ReservationCheckStatus
    cb_is_reservation_for_token(std::int32_t evse_id, const ocpp::CiString<255>& idToken,
                                const std::optional<ocpp::CiString<255>>& groupIdToken) = 0;
    virtual bool cb_is_reset_allowed(const std::optional<std::int32_t>& evse_id, ResetType type) = 0;
    virtual void
    cb_iso15118_certificate_request(std::int32_t extensions_id,
                                    const types::iso15118::RequestExiStreamSchema& certificate_request) = 0;
    virtual void cb_log_status(types::system::LogStatus status) = 0;
    virtual void cb_ocpp_messages(const std::string& message, ocpp::MessageDirection direction) = 0;
    virtual bool cb_pause_charging(std::int32_t evse_id) = 0;
    virtual void cb_powermeter(std::int32_t evse_id, const types::powermeter::Powermeter& power_meter) = 0;
    virtual void cb_provide_token(const IdToken& id_token) = 0;
    virtual void cb_ready(std::int32_t evse_id, bool ready) = 0;
    virtual void cb_reservation_update(types::reservation::ReservationUpdateStatus status) = 0;
    virtual ocpp::v2::ReserveNowStatusEnum cb_reserve_now(const ocpp::v2::ReserveNowRequest& request) = 0;
    virtual void cb_reset(const std::optional<const std::int32_t>& evse_id, ResetType type) = 0;
    virtual bool cb_resume_charging(std::int32_t evse_id) = 0;
    virtual void cb_security_event(const ocpp::CiString<50>& event_type,
                                   const std::optional<ocpp::CiString<255>>& tech_info) = 0;
    virtual void cb_service_renegotiation_supported(std::int32_t extensions_id,
                                                    bool service_renegotiation_supported) = 0;
    virtual void cb_session_event(std::int32_t evse_id, types::evse_manager::SessionEvent session_event) = 0;
    virtual void cb_set_charging_profiles() = 0;
    virtual ocpp::v2::SetDisplayMessageResponse
    cb_set_display_message(const std::vector<ocpp::DisplayMessage>& messages) = 0;
    virtual void cb_set_running_cost(const ocpp::RunningCost& running_cost, std::uint32_t number_of_decimals,
                                     const std::optional<std::string>& currency_code) = 0;
    virtual ocpp::v2::RequestStartStopStatusEnum cb_stop_transaction(std::int32_t evse_id,
                                                                     ocpp::v2::ReasonEnum stop_reason) = 0;
    virtual void cb_supported_energy_transfer_modes(
        std::int32_t evse_id,
        const std::vector<types::iso15118::EnergyTransferMode>& supported_energy_transfer_modes) = 0;
    virtual void cb_tariff_message(const ocpp::TariffMessage& message) = 0;
    virtual void cb_time_sync(const ocpp::DateTime& current_time) = 0;
    virtual void cb_transaction_event(const ocpp::v2::TransactionEventRequest& transaction_event) = 0;
    virtual void
    cb_transaction_event_response(const ocpp::v2::TransactionEventRequest& transaction_event,
                                  const ocpp::v2::TransactionEventResponse& transaction_event_response) = 0;
    virtual ocpp::v2::UnlockConnectorResponse cb_unlock_connector(std::int32_t evse_id, std::int32_t connector_id) = 0;
    virtual bool cb_update_allowed_energy_transfer_modes(
        const std::vector<ocpp::v2::EnergyTransferModeEnum>& allowed_energy_transfer_modes,
        const ocpp::CiString<36>& transaction_id) = 0;
    virtual ocpp::v2::UpdateFirmwareResponse
    cb_update_firmware_request(const ocpp::v2::UpdateFirmwareRequest& request) = 0;
    virtual ocpp::v2::SetNetworkProfileStatusEnum
    cb_validate_network_profile(const ocpp::v2::NetworkConnectionProfile& network_connection_profile) = 0;
    virtual void cb_variable_set(const ocpp::v2::SetVariableData& set_variable_data) = 0;
    virtual void cb_waiting_for_external_ready(std::int32_t evse_id, bool ready) = 0;
};

struct GenericChargePointInterface {
    virtual ~GenericChargePointInterface() = default;

    struct NotConfigured : public std::runtime_error {
        using std::runtime_error::runtime_error;
    };

    struct init_args_t {
        const std::string& ocpp_share_path;
        const std::string& core_database_path;
        const std::string& sql_init_path;
        const std::string& message_log_path;
        const std::string& v16_chargepoint_config_path;
        const std::string& v16_user_config_path;
        std::map<std::int32_t, std::int32_t>&& evse_connector_structure;
        std::unique_ptr<ocpp::v2::DeviceModelStorageInterface>&& device_model_storage_interface;
    };

    using listener_t = std::function<void(
        const std::unordered_map<std::int64_t, ocpp::v2::VariableMonitoringMeta>& monitors,
        const ocpp::v2::Component& component, const ocpp::v2::Variable& variable,
        const ocpp::v2::VariableCharacteristics& characteristics, const ocpp::v2::VariableAttribute& attribute,
        const std::string& value_previous, const std::string& value_current)>;

    virtual void init(init_args_t& args) = 0;

    virtual void connect_websocket() = 0;
    virtual void disconnect_websocket() = 0;
    virtual void set_message_queue_resume_delay(std::chrono::seconds delay) = 0;
    virtual void start(ocpp::v2::BootReasonEnum bootreason, bool start_connecting) = 0;
    virtual void stop() = 0;

    virtual std::optional<ocpp::v2::DataTransferResponse>
    data_transfer_req(const ocpp::v2::DataTransferRequest& request) = 0;

    virtual std::optional<bool> get_bool(const ocpp::v2::Component& component_id, const ocpp::v2::Variable& variable_id,
                                         ocpp::v2::AttributeEnum attribute_enum) = 0;
    virtual std::optional<std::int32_t> get_int32(const ocpp::v2::Component& component_id,
                                                  const ocpp::v2::Variable& variable_id,
                                                  ocpp::v2::AttributeEnum attribute_enum) = 0;
    virtual std::optional<std::string> get_string(const ocpp::v2::Component& component_id,
                                                  const ocpp::v2::Variable& variable_id,
                                                  ocpp::v2::AttributeEnum attribute_enum) = 0;

    virtual std::vector<ocpp::v2::EnhancedCompositeSchedule>
    get_all_composite_schedules(std::int32_t duration_s, const ocpp::v2::ChargingRateUnitEnum& unit) = 0;
    virtual std::vector<ocpp::v2::GetVariableResult>
    get_variables(const std::vector<ocpp::v2::GetVariableData>& get_variable_data_vector) = 0;

    virtual void on_authorized(std::int32_t evse_id, std::int32_t connector_id, const ocpp::v2::IdToken& id_token) = 0;
    virtual ocpp::v2::ChangeAvailabilityResponse
    on_change_availability(const ocpp::v2::ChangeAvailabilityRequest& request) = 0;
    virtual bool on_charging_state_changed(std::uint32_t evse_id, ocpp::v2::ChargingStateEnum charging_state,
                                           ocpp::v2::TriggerReasonEnum trigger_reason) = 0;
    virtual void on_enabled(std::int32_t evse_id, std::int32_t connector_id) = 0;
    virtual void on_ev_charging_needs(const ocpp::v2::NotifyEVChargingNeedsRequest& request) = 0;
    virtual void on_event(const std::vector<ocpp::v2::EventData>& events) = 0;
    virtual void on_fault_cleared(std::int32_t evse_id, std::int32_t connector_id) = 0;
    virtual void on_faulted(std::int32_t evse_id, std::int32_t connector_id) = 0;
    virtual void on_firmware_update_status_notification(std::int32_t request_id,
                                                        const ocpp::v2::FirmwareStatusEnum& firmware_update_status) = 0;
    virtual ocpp::v2::Get15118EVCertificateResponse
    on_get_15118_ev_certificate_request(const ocpp::v2::Get15118EVCertificateRequest& request) = 0;
    virtual void on_log_status_notification(ocpp::v2::UploadLogStatusEnum status, std::int32_t requestId) = 0;
    virtual void on_meter_value(std::int32_t evse_id, const ocpp::v2::MeterValue& meter_value) = 0;
    virtual void on_reservation_status(std::int32_t reservation_id, ocpp::v2::ReservationUpdateStatusEnum status) = 0;
    virtual void on_reservation_cleared(std::int32_t evse_id, std::int32_t connector_id) = 0;
    virtual void on_reserved(std::int32_t evse_id, std::int32_t connector_id) = 0;
    virtual void on_security_event(const ocpp::CiString<50>& event_type,
                                   const std::optional<ocpp::CiString<255>>& tech_info,
                                   const std::optional<bool>& critical,
                                   const std::optional<ocpp::DateTime>& timestamp) = 0;
    virtual void on_session_finished(std::int32_t evse_id, std::int32_t connector_id) = 0;
    virtual void on_session_started(std::int32_t evse_id, std::int32_t connector_id) = 0;
    virtual void on_transaction_finished(std::int32_t evse_id, const ocpp::DateTime& timestamp,
                                         const ocpp::v2::MeterValue& meter_stop, ocpp::v2::ReasonEnum reason,
                                         ocpp::v2::TriggerReasonEnum trigger_reason,
                                         const std::optional<ocpp::v2::IdToken>& id_token,
                                         const std::optional<std::string>& signed_meter_value,
                                         ocpp::v2::ChargingStateEnum charging_state) = 0;
    virtual void on_transaction_started(std::int32_t evse_id, std::int32_t connector_id, const std::string& session_id,
                                        const ocpp::DateTime& timestamp, ocpp::v2::TriggerReasonEnum trigger_reason,
                                        const ocpp::v2::MeterValue& meter_start,
                                        const std::optional<ocpp::v2::IdToken>& id_token,
                                        const std::optional<ocpp::v2::IdToken>& group_id_token,
                                        const std::optional<std::int32_t>& reservation_id,
                                        const std::optional<std::int32_t>& remote_start_id,
                                        ocpp::v2::ChargingStateEnum charging_state) = 0;
    virtual void on_unavailable(std::int32_t evse_id, std::int32_t connector_id) = 0;

    virtual void register_variable_listener(listener_t&& listener) = 0;
    virtual std::map<ocpp::v2::SetVariableData, ocpp::v2::SetVariableResult>
    set_variables(const std::vector<ocpp::v2::SetVariableData>& set_variable_data_vector,
                  const std::string& source) = 0;

    virtual ocpp::v2::AuthorizeResponse
    validate_token(const ocpp::v2::IdToken& id_token, const std::optional<ocpp::CiString<10000>>& certificate,
                   const std::optional<std::vector<ocpp::v2::OCSPRequestData>>& ocsp_request_data) = 0;
};

} // namespace ocpp_multi
