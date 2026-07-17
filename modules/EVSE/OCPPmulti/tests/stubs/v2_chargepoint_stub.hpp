// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <generic_chargepoint_interface.hpp>
#include <ocpp/v2/charge_point.hpp>
#include <ocpp_module_common_aliases.hpp>

namespace stubs {

// gmock of libocpp's ocpp::v2::ChargePointInterface, injected into ChargePointV2 for unit tests
struct Ocpp2ChargePointMock : public ocpp::v2::ChargePointInterface {
    MOCK_METHOD(void, start, (ocpp::v2::BootReasonEnum bootreason, bool start_connecting), (override));
    MOCK_METHOD(void, stop, (), (override));
    MOCK_METHOD(bool, is_message_queue_idle, (), (override));
    MOCK_METHOD(void, connect_websocket, (std::optional<std::int32_t> network_profile_slot), (override));
    MOCK_METHOD(void, disconnect_websocket, (), (override));
    MOCK_METHOD(void, on_websocket_connected,
                (const int configuration_slot, const ocpp::v2::NetworkConnectionProfile& network_connection_profile,
                 const ocpp::OcppProtocolVersion ocpp_version),
                (override));
    MOCK_METHOD(void, on_websocket_disconnected,
                (const int configuration_slot, const ocpp::v2::NetworkConnectionProfile& network_connection_profile),
                (override));
    MOCK_METHOD(void, on_websocket_connection_failed, (ocpp::ConnectionFailedReason reason), (override));
    MOCK_METHOD(void, on_network_disconnected, (ocpp::v2::OCPPInterfaceEnum ocpp_interface), (override));
    MOCK_METHOD(void, on_firmware_update_status_notification,
                (std::int32_t request_id, const ocpp::v2::FirmwareStatusEnum& firmware_update_status,
                 const bool disable_connectors_during_install),
                (override));
    MOCK_METHOD(void, on_session_started, (const std::int32_t evse_id, const std::int32_t connector_id), (override));
    MOCK_METHOD(ocpp::v2::Get15118EVCertificateResponse, on_get_15118_ev_certificate_request,
                (const ocpp::v2::Get15118EVCertificateRequest& request), (override));
    MOCK_METHOD(void, on_transaction_started,
                (const std::int32_t evse_id, const std::int32_t connector_id, const std::string& session_id,
                 const ocpp::DateTime& timestamp, const ocpp::v2::TriggerReasonEnum trigger_reason,
                 const ocpp::v2::MeterValue& meter_start, const std::optional<ocpp::v2::IdToken>& id_token,
                 const std::optional<ocpp::v2::IdToken>& group_id_token,
                 const std::optional<std::int32_t>& reservation_id, const std::optional<std::int32_t>& remote_start_id,
                 const ocpp::v2::ChargingStateEnum charging_state),
                (override));
    MOCK_METHOD(void, on_transaction_finished,
                (const std::int32_t evse_id, const ocpp::DateTime& timestamp, const ocpp::v2::MeterValue& meter_stop,
                 const ocpp::v2::ReasonEnum reason, const ocpp::v2::TriggerReasonEnum trigger_reason,
                 const std::optional<ocpp::v2::IdToken>& id_token, const std::optional<std::string>& signed_meter_value,
                 const ocpp::v2::ChargingStateEnum charging_state,
                 const std::optional<ocpp::v2::SignedMeterValue>& start_signed_meter_value),
                (override));
    MOCK_METHOD(void, on_session_finished, (const std::int32_t evse_id, const std::int32_t connector_id), (override));
    MOCK_METHOD(void, on_authorized,
                (const std::int32_t evse_id, const std::int32_t connector_id, const ocpp::v2::IdToken& id_token),
                (override));
    MOCK_METHOD(void, on_meter_value, (const std::int32_t evse_id, const ocpp::v2::MeterValue& meter_value),
                (override));
    MOCK_METHOD(void, on_unavailable, (const std::int32_t evse_id, const std::int32_t connector_id), (override));
    MOCK_METHOD(void, on_enabled, (const std::int32_t evse_id, const std::int32_t connector_id), (override));
    MOCK_METHOD(void, on_faulted, (const std::int32_t evse_id, const std::int32_t connector_id), (override));
    MOCK_METHOD(void, on_fault_cleared, (const std::int32_t evse_id, const std::int32_t connector_id), (override));
    MOCK_METHOD(void, on_reserved, (const std::int32_t evse_id, const std::int32_t connector_id), (override));
    MOCK_METHOD(void, on_reservation_cleared, (const std::int32_t evse_id, const std::int32_t connector_id),
                (override));
    MOCK_METHOD(bool, on_charging_state_changed,
                (const std::uint32_t evse_id, const ocpp::v2::ChargingStateEnum charging_state,
                 const ocpp::v2::TriggerReasonEnum trigger_reason),
                (override));
    MOCK_METHOD(ocpp::v2::ChangeAvailabilityResponse, on_change_availability,
                (const ocpp::v2::ChangeAvailabilityRequest& request), (override));
    MOCK_METHOD(std::optional<std::string>, get_evse_transaction_id, (std::int32_t evse_id), (override));
    MOCK_METHOD(void, on_event, (const std::vector<ocpp::v2::EventData>& events), (override));
    MOCK_METHOD(void, on_log_status_notification, (ocpp::v2::UploadLogStatusEnum status, std::int32_t requestId),
                (override));
    MOCK_METHOD(void, on_security_event,
                (const ocpp::CiString<50>& event_type, const std::optional<ocpp::CiString<255>>& tech_info,
                 const std::optional<bool>& critical, const std::optional<ocpp::DateTime>& timestamp),
                (override));
    MOCK_METHOD(void, on_variable_changed, (const ocpp::v2::SetVariableData& set_variable_data), (override));
    MOCK_METHOD(void, on_reservation_status,
                (const std::int32_t reservation_id, const ocpp::v2::ReservationUpdateStatusEnum status), (override));
    MOCK_METHOD(void, on_ev_charging_needs, (const ocpp::v2::NotifyEVChargingNeedsRequest& request), (override));
    MOCK_METHOD(ocpp::v2::AuthorizeResponse, validate_token,
                (const ocpp::v2::IdToken id_token, const std::optional<ocpp::CiString<10000>>& certificate,
                 const std::optional<std::vector<ocpp::v2::OCSPRequestData>>& ocsp_request_data),
                (override));
    MOCK_METHOD(std::optional<ocpp::v2::DataTransferResponse>, data_transfer_req,
                (const ocpp::CiString<255>& vendorId, const std::optional<ocpp::CiString<50>>& messageId,
                 const std::optional<nlohmann::json>& data),
                (override));
    MOCK_METHOD(std::optional<ocpp::v2::DataTransferResponse>, data_transfer_req,
                (const ocpp::v2::DataTransferRequest& request), (override));
    MOCK_METHOD(void, set_message_queue_resume_delay, (std::chrono::seconds delay), (override));
    MOCK_METHOD(std::vector<ocpp::v2::GetVariableResult>, get_variables,
                (const std::vector<ocpp::v2::GetVariableData>& get_variable_data_vector), (override));
    MOCK_METHOD((std::map<ocpp::v2::SetVariableData, ocpp::v2::SetVariableResult>), set_variables,
                (const std::vector<ocpp::v2::SetVariableData>& set_variable_data_vector, const std::string& source),
                (override));

    using variable_listener_t = std::function<void(
        const std::unordered_map<std::int64_t, ocpp::v2::VariableMonitoringMeta>& monitors,
        const ocpp::v2::Component& component, const ocpp::v2::Variable& variable,
        const ocpp::v2::VariableCharacteristics& characteristics, const ocpp::v2::VariableAttribute& attribute,
        const std::string& value_previous, const std::string& value_current)>;
    MOCK_METHOD(void, register_variable_listener, (variable_listener_t && listener), (override));

    MOCK_METHOD(ocpp::v2::EnhancedCompositeScheduleResponse, get_composite_schedule,
                (const ocpp::v2::GetCompositeScheduleRequest& request), (override));
    MOCK_METHOD(std::optional<ocpp::v2::EnhancedCompositeSchedule>, get_composite_schedule,
                (std::int32_t evse_id, std::chrono::seconds duration, ocpp::v2::ChargingRateUnitEnum unit), (override));
    MOCK_METHOD(std::vector<ocpp::v2::EnhancedCompositeSchedule>, get_all_composite_schedules,
                (const std::int32_t duration, const ocpp::v2::ChargingRateUnitEnum& unit), (override));
    MOCK_METHOD(std::optional<ocpp::v2::NetworkConnectionProfile>, get_network_connection_profile,
                (const std::int32_t configuration_slot), (const, override));
    MOCK_METHOD(std::optional<int>, get_priority_from_configuration_slot, (const int configuration_slot),
                (const, override));
    MOCK_METHOD(std::vector<int>, get_network_connection_slots, (), (const, override));
};

// gmock of the module-side callback sink used by the version wrappers
struct GenericChargePointCallbacksMock : public ocpp_multi::GenericChargePointCallbacks {
    MOCK_METHOD(void, cb_all_connectors_unavailable, (), (override));
    MOCK_METHOD(void, cb_boot_notification, (const ocpp::v2::BootNotificationResponse& boot_notification_response),
                (override));
    MOCK_METHOD(bool, cb_cancel_reservation, (std::int32_t reservation_id), (override));
    MOCK_METHOD(void, cb_charging_needs,
                (std::int32_t extensions_id, const types::iso15118::ChargingNeeds& charging_needs), (override));
    MOCK_METHOD(ocpp::v2::ClearDisplayMessageResponse, cb_clear_display_message,
                (const ocpp::v2::ClearDisplayMessageRequest& request), (override));
    MOCK_METHOD(std::future<ocpp::ConfigNetworkResult>, cb_configure_network_connection_profile, (), (override));
    MOCK_METHOD(bool, cb_connector_effective_operative_status,
                (std::int32_t evse_id, std::int32_t connector_id, ocpp::v2::OperationalStatusEnum new_status),
                (override));
    MOCK_METHOD(void, cb_connection_state_changed, (bool is_connected, ocpp::OcppProtocolVersion protocol_version),
                (override));
    MOCK_METHOD(ocpp::v2::DataTransferResponse, cb_data_transfer, (const ocpp::v2::DataTransferRequest& request),
                (override));
    MOCK_METHOD(void, cb_default_price, (const types::session_cost::DefaultPrice& messages), (override));
    MOCK_METHOD(void, cb_ev_info, (std::int32_t evse_id, const types::evse_manager::EVInfo& ev_info), (override));
    MOCK_METHOD(void, cb_fault_cleared_handler, (std::int32_t evse_id, const Everest::error::Error& error), (override));
    MOCK_METHOD(void, cb_fault_handler, (std::int32_t evse_id, const Everest::error::Error& error), (override));
    MOCK_METHOD(void, cb_firmware_update_status, (types::system::FirmwareUpdateStatus status), (override));
    MOCK_METHOD(void, cb_get_15118_ev_certificate_response,
                (std::int32_t connector_id, const ocpp::v2::Get15118EVCertificateResponse& response,
                 ocpp::v2::CertificateActionEnum certificate_action),
                (override));
    MOCK_METHOD(std::vector<ocpp::DisplayMessage>, cb_get_display_message,
                (const ocpp::v2::GetDisplayMessagesRequest& request), (override));
    MOCK_METHOD(ocpp::v2::GetLogResponse, cb_get_log_request, (const types::system::UploadLogsRequest& request),
                (override));
    MOCK_METHOD(void, cb_hw_capabilities,
                (std::int32_t evse_id, const types::evse_board_support::HardwareCapabilities& hw_capabilities),
                (override));
    MOCK_METHOD(ocpp::ReservationCheckStatus, cb_is_reservation_for_token,
                (std::int32_t evse_id, const ocpp::CiString<255>& idToken,
                 const std::optional<ocpp::CiString<255>>& groupIdToken),
                (override));
    MOCK_METHOD(bool, cb_is_reset_allowed, (const std::optional<std::int32_t>& evse_id, ResetType type), (override));
    MOCK_METHOD(void, cb_iso15118_certificate_request,
                (std::int32_t extensions_id, const types::iso15118::RequestExiStreamSchema& certificate_request),
                (override));
    MOCK_METHOD(void, cb_log_status, (types::system::LogStatus status), (override));
    MOCK_METHOD(void, cb_ocpp_messages, (const std::string& message, ocpp::MessageDirection direction), (override));
    MOCK_METHOD(bool, cb_pause_charging, (std::int32_t evse_id), (override));
    MOCK_METHOD(void, cb_powermeter, (std::int32_t evse_id, const types::powermeter::Powermeter& power_meter),
                (override));
    MOCK_METHOD(void, cb_powermeter_public_key_ocmf, (std::int32_t evse_id, const std::string& public_key), (override));
    MOCK_METHOD(void, cb_provide_token, (const IdToken& id_token), (override));
    MOCK_METHOD(void, cb_ready, (std::int32_t evse_id, bool ready), (override));
    MOCK_METHOD(void, cb_reservation_update, (types::reservation::ReservationUpdateStatus status), (override));
    MOCK_METHOD(ocpp::v2::ReserveNowStatusEnum, cb_reserve_now, (const ocpp::v2::ReserveNowRequest& request),
                (override));
    MOCK_METHOD(void, cb_reset, (const std::optional<const std::int32_t>& evse_id, ResetType type), (override));
    MOCK_METHOD(bool, cb_resume_charging, (std::int32_t evse_id), (override));
    MOCK_METHOD(void, cb_security_event,
                (const ocpp::CiString<50>& event_type, const std::optional<ocpp::CiString<255>>& tech_info),
                (override));
    MOCK_METHOD(void, cb_service_renegotiation_supported,
                (std::int32_t extensions_id, bool service_renegotiation_supported), (override));
    MOCK_METHOD(void, cb_session_event, (std::int32_t evse_id, types::evse_manager::SessionEvent session_event),
                (override));
    MOCK_METHOD(void, cb_set_charging_profiles, (), (override));
    MOCK_METHOD(ocpp::v2::SetDisplayMessageResponse, cb_set_display_message,
                (const std::vector<ocpp::DisplayMessage>& messages), (override));
    MOCK_METHOD(void, cb_set_running_cost,
                (const ocpp::RunningCost& running_cost, std::uint32_t number_of_decimals,
                 const std::optional<std::string>& currency_code),
                (override));
    MOCK_METHOD(ocpp::v2::RequestStartStopStatusEnum, cb_stop_transaction,
                (std::int32_t evse_id, types::evse_manager::StopTransactionReason stop_reason), (override));
    MOCK_METHOD(void, cb_supported_energy_transfer_modes,
                (std::int32_t evse_id,
                 const std::vector<types::iso15118::EnergyTransferMode>& supported_energy_transfer_modes),
                (override));
    MOCK_METHOD(void, cb_tariff_message, (const types::session_cost::TariffMessage& message), (override));
    MOCK_METHOD(void, cb_time_sync, (const ocpp::DateTime& current_time), (override));
    MOCK_METHOD(void, cb_transaction_event,
                (const ocpp::v2::TransactionEventRequest& transaction_event,
                 const std::optional<std::string>& transaction_id),
                (override));
    MOCK_METHOD(void, cb_transaction_event_response,
                (const ocpp::v2::TransactionEventRequest& transaction_event,
                 const ocpp::v2::TransactionEventResponse& transaction_event_response,
                 const std::optional<std::string>& transaction_id),
                (override));
    MOCK_METHOD(ocpp::v2::UnlockConnectorResponse, cb_unlock_connector,
                (std::int32_t evse_id, std::int32_t connector_id), (override));
    MOCK_METHOD(bool, cb_update_allowed_energy_transfer_modes,
                (const std::vector<ocpp::v2::EnergyTransferModeEnum>& allowed_energy_transfer_modes,
                 const ocpp::CiString<36>& transaction_id),
                (override));
    MOCK_METHOD(ocpp::v2::UpdateFirmwareResponse, cb_update_firmware_request,
                (const ocpp::v2::UpdateFirmwareRequest& request), (override));
    MOCK_METHOD(ocpp::v2::SetNetworkProfileStatusEnum, cb_validate_network_profile,
                (const ocpp::v2::NetworkConnectionProfile& network_connection_profile), (override));
    MOCK_METHOD(void, cb_variable_set, (const ocpp::v2::SetVariableData& set_variable_data), (override));
    MOCK_METHOD(void, cb_waiting_for_external_ready, (std::int32_t evse_id, bool ready), (override));

    MOCK_METHOD(bool, map_error, (const std::string& error, std::string& updated_error), (override));
    MOCK_METHOD(void, transaction_add,
                (std::int32_t evse_id, const std::shared_ptr<module::TransactionData>& transaction_data), (override));
    MOCK_METHOD(std::shared_ptr<module::TransactionData>, transaction_data, (std::int32_t evse_id), (override));
    MOCK_METHOD(module::TxEventEffect, transaction_event, (std::int32_t evse_id, module::TxEvent tx_event), (override));
    MOCK_METHOD(void, transaction_reset, (std::int32_t evse_id), (override));
    MOCK_METHOD(void, update_evcc_id_token, (std::int32_t evse, ocpp::v2::IdToken& id_token), (override));
};

} // namespace stubs
