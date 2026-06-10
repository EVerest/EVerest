// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <everest/conversions/ocpp/evse_security_ocpp.hpp>
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
#include <string>

namespace ocpp_multi {

struct GenericChargePointInterface {
    virtual ~GenericChargePointInterface() = default;

    using listener_t = std::function<void(
        const std::unordered_map<std::int64_t, ocpp::v2::VariableMonitoringMeta>& monitors,
        const ocpp::v2::Component& component, const ocpp::v2::Variable& variable,
        const ocpp::v2::VariableCharacteristics& characteristics, const ocpp::v2::VariableAttribute& attribute,
        const std::string& value_previous, const std::string& value_current)>;

    virtual void init(std::map<std::int32_t, std::int32_t>&& evse_connector_structure,
                      std::unique_ptr<ocpp::v2::DeviceModelStorageInterface>&& device_model_storage_interface,
                      const std::string& ocpp_main_path, const std::string& core_database_path,
                      const std::string& sql_init_path, const std::string& message_log_path,
                      ocpp::v2::Callbacks&& callbacks) = 0;

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
