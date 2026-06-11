// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include "generic_chargepoint_interface.hpp"
#include <string_view>

namespace ocpp_multi {

class ChargePointV2 : public GenericChargePointInterface {
private:
    using ResetType = ocpp_multi::GenericChargePointCallbacks::ResetType;

    std::shared_ptr<EvseSecurity> m_evse_security;
    std::unique_ptr<ocpp::v2::ChargePoint> m_charge_point;
    ocpp_multi::GenericChargePointCallbacks* m_callbacks_ptr{nullptr};

    void check_configured(const std::string_view& fn);
    ocpp::v2::Callbacks configure_callbacks();

    void cb_reset(const std::optional<const std::int32_t>& evse_id, const ocpp::v2::ResetEnum& type);

public:
    explicit ChargePointV2(ocpp_multi::GenericChargePointCallbacks& callbacks, evse_securityIntf& security) :
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
