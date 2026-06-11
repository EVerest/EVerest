// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include "generic_chargepoint_interface.hpp"

namespace ocpp_multi {

class GenericChargePoint : public GenericChargePointInterface {
public:
    enum class state_t : std::uint8_t {
        v2_active,
        v16_active,
        idle,
    };

private:
    std::shared_ptr<EvseSecurity> m_evse_security;
    std::shared_ptr<ocpp::v2::DeviceModelAbstract> m_device_model;

    std::map<std::int32_t, std::int32_t> m_evse_connector_structure;
    std::unique_ptr<ocpp::v2::DeviceModelStorageInterface> m_device_model_storage_interface;
    std::string m_ocpp_share_path;
    std::string m_core_database_path;
    std::string m_sql_init_path;
    std::string m_message_log_path;
    ocpp::v2::Callbacks m_callbacks;

    state_t m_state{state_t::idle};

public:
    using listener_t = std::function<void(
        const std::unordered_map<std::int64_t, ocpp::v2::VariableMonitoringMeta>& monitors,
        const ocpp::v2::Component& component, const ocpp::v2::Variable& variable,
        const ocpp::v2::VariableCharacteristics& characteristics, const ocpp::v2::VariableAttribute& attribute,
        const std::string& value_previous, const std::string& value_current)>;

    explicit GenericChargePoint(evse_securityIntf& security) :
        m_evse_security(std::make_unique<EvseSecurity>(security)) {
    }

    void init(init_args_t& args) override;

    void connect_websocket() override;
    void disconnect_websocket() override;
    void set_message_queue_resume_delay(std::chrono::seconds delay) override;
    void start(ocpp::v2::BootReasonEnum bootreason, bool start_connecting) override;
    void stop() override;

    std::optional<ocpp::v2::DataTransferResponse>
    data_transfer_req(const ocpp::v2::DataTransferRequest& request) override;

    template <typename T>
    std::optional<T> get(const ocpp::v2::Component& component_id, const ocpp::v2::Variable& variable_id,
                         const ocpp::v2::AttributeEnum& attribute_enum);

    std::optional<bool> get_bool(const ocpp::v2::Component& component_id, const ocpp::v2::Variable& variable_id,
                                 ocpp::v2::AttributeEnum attribute_enum) override {
        return get<bool>(component_id, variable_id, attribute_enum);
    }
    std::optional<std::int32_t> get_int32(const ocpp::v2::Component& component_id,
                                          const ocpp::v2::Variable& variable_id,
                                          ocpp::v2::AttributeEnum attribute_enum) override {
        return get<std::int32_t>(component_id, variable_id, attribute_enum);
    }
    std::optional<std::string> get_string(const ocpp::v2::Component& component_id,
                                          const ocpp::v2::Variable& variable_id,
                                          ocpp::v2::AttributeEnum attribute_enum) override {
        return get<std::string>(component_id, variable_id, attribute_enum);
    }

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
