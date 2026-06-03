// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include "ocpp/common/cistring.hpp"
#include "ocpp/common/types.hpp"
#include "ocpp/v2/ocpp_types.hpp"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <generic_chargepoint.hpp>

#include <generated/types/ocpp.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <ostream>
#include <string_view>
#include <variant>

namespace stubs {

class SimpleStore {
public:
    using entry_t = std::variant<std::monostate, bool, std::int32_t, std::string>;

private:
    std::map<std::string, entry_t> m_store;

public:
    void clear(const std::string& key);
    bool exists(const std::string& key);
    entry_t get(const std::string& key);
    void put(const std::string& key, entry_t value);
};

struct ChargePointStub : public ocpp_multi::GenericChargePointInterface {
    ChargePointStub() = default;

    // in general use MOCK except for device model get

    SimpleStore simple_store;
    void load_store(const std::string_view& file);

    MOCK_METHOD(void, init, (init_args_t & args), (override));
    MOCK_METHOD(void, set_mode, (modes_t args), (override));

    MOCK_METHOD(void, connect_websocket, (), (override));
    MOCK_METHOD(void, disconnect_websocket, (), (override));
    MOCK_METHOD(void, set_message_queue_resume_delay, (std::chrono::seconds delay), (override));
    MOCK_METHOD(bool, restart, (), (override));
    MOCK_METHOD(void, start, (ocpp::v2::BootReasonEnum bootreason, bool start_connecting), (override));
    MOCK_METHOD(void, stop, (), (override));

    MOCK_METHOD(std::optional<ocpp::v2::DataTransferResponse>, data_transfer_req,
                (const ocpp::v2::DataTransferRequest& request), (override));

    // obtain other variables for tests
    std::optional<bool> get_bool(const ocpp::v2::Component& component_id, const ocpp::v2::Variable& variable_id,
                                 ocpp::v2::AttributeEnum attribute_enum);
    std::optional<std::int32_t> get_int32(const ocpp::v2::Component& component_id,
                                          const ocpp::v2::Variable& variable_id,
                                          ocpp::v2::AttributeEnum attribute_enum);
    std::optional<std::string> get_string(const ocpp::v2::Component& component_id,
                                          const ocpp::v2::Variable& variable_id,
                                          ocpp::v2::AttributeEnum attribute_enum);

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

    MOCK_METHOD(std::vector<ocpp::v2::EnhancedCompositeSchedule>, get_all_composite_schedules,
                (std::int32_t duration_s, ocpp::v2::ChargingRateUnitEnum unit), (override));
    MOCK_METHOD(std::vector<ocpp::v2::GetVariableResult>, get_variables,
                (const std::vector<ocpp::v2::GetVariableData>& get_variable_data_vector), (override));

    MOCK_METHOD(void, on_authorized,
                (std::int32_t evse_id, std::int32_t connector_id, const ocpp::v2::IdToken& id_token), (override));
    MOCK_METHOD(ocpp::v2::ChangeAvailabilityResponse, on_change_availability,
                (const ocpp::v2::ChangeAvailabilityRequest& request), (override));
    MOCK_METHOD(bool, on_charging_state_changed,
                (std::uint32_t evse_id, ocpp::v2::ChargingStateEnum charging_state,
                 ocpp::v2::TriggerReasonEnum trigger_reason),
                (override));
    MOCK_METHOD(void, on_enabled, (std::int32_t evse_id, std::int32_t connector_id), (override));
    MOCK_METHOD(void, on_ev_charging_needs, (const ocpp::v2::NotifyEVChargingNeedsRequest& request), (override));
    MOCK_METHOD(void, on_event, (const EventInfo& events), (override));

    MOCK_METHOD(void, on_event_authorised,
                (std::int32_t evse_id, std::int32_t connector_id,
                 const types::evse_manager::SessionEvent& session_event),
                (override));
    MOCK_METHOD(void, on_event_deauthorised,
                (std::int32_t evse_id, std::int32_t connector_id,
                 const types::evse_manager::SessionEvent& session_event),
                (override));
    MOCK_METHOD(void, on_event_disabled,
                (std::int32_t evse_id, std::int32_t connector_id,
                 const types::evse_manager::SessionEvent& session_event),
                (override));
    MOCK_METHOD(void, on_event_enabled,
                (std::int32_t evse_id, std::int32_t connector_id,
                 const types::evse_manager::SessionEvent& session_event),
                (override));
    MOCK_METHOD(void, on_event_charging_paused_ev,
                (std::int32_t evse_id, std::int32_t connector_id,
                 const types::evse_manager::SessionEvent& session_event),
                (override));
    MOCK_METHOD(void, on_event_charging_paused_evse,
                (std::int32_t evse_id, std::int32_t connector_id,
                 const types::evse_manager::SessionEvent& session_event),
                (override));
    MOCK_METHOD(void, on_event_charging_started,
                (std::int32_t evse_id, std::int32_t connector_id,
                 const types::evse_manager::SessionEvent& session_event),
                (override));
    MOCK_METHOD(void, on_event_plugin_timeout,
                (std::int32_t evse_id, std::int32_t connector_id,
                 const types::evse_manager::SessionEvent& session_event),
                (override));
    MOCK_METHOD(void, on_event_reservation_end,
                (std::int32_t evse_id, std::int32_t connector_id,
                 const types::evse_manager::SessionEvent& session_event),
                (override));
    MOCK_METHOD(void, on_event_reservation_start,
                (std::int32_t evse_id, std::int32_t connector_id,
                 const types::evse_manager::SessionEvent& session_event),
                (override));
    MOCK_METHOD(void, on_event_session_finished,
                (std::int32_t evse_id, std::int32_t connector_id,
                 const types::evse_manager::SessionEvent& session_event),
                (override));
    MOCK_METHOD(void, on_event_session_resumed,
                (std::int32_t evse_id, std::int32_t connector_id,
                 const types::evse_manager::SessionEvent& session_event),
                (override));
    MOCK_METHOD(SessionResult, on_event_session_started,
                (std::int32_t evse_id, std::int32_t connector_id,
                 const types::evse_manager::SessionEvent& session_event),
                (override));
    MOCK_METHOD(void, on_event_switching_phases,
                (std::int32_t evse_id, std::int32_t connector_id,
                 const types::evse_manager::SessionEvent& session_event),
                (override));
    MOCK_METHOD(void, on_event_transaction_finished,
                (std::int32_t evse_id, std::int32_t connector_id,
                 const types::evse_manager::SessionEvent& session_event),
                (override));
    MOCK_METHOD(SessionResult, on_event_transaction_started,
                (std::int32_t evse_id, std::int32_t connector_id,
                 const types::evse_manager::SessionEvent& session_event),
                (override));

    MOCK_METHOD(void, on_fault_cleared, (std::int32_t evse_id, std::int32_t connector_id), (override));
    MOCK_METHOD(void, on_faulted, (std::int32_t evse_id, std::int32_t connector_id), (override));
    MOCK_METHOD(void, on_firmware_update_status_notification,
                (std::int32_t request_id, ocpp::v2::FirmwareStatusEnum firmware_update_status), (override));
    MOCK_METHOD(void, on_get_15118_ev_certificate_request,
                (std::int32_t extensions_id, const ocpp::v2::Get15118EVCertificateRequest& request), (override));
    MOCK_METHOD(void, on_log_status_notification, (ocpp::v2::UploadLogStatusEnum status, std::int32_t requestId),
                (override));
    MOCK_METHOD(void, on_meter_value,
                (std::int32_t evse_id, std::optional<float> soc, const types::powermeter::Powermeter& power_meter),
                (override));
    MOCK_METHOD(void, on_reservation_status,
                (std::int32_t reservation_id, ocpp::v2::ReservationUpdateStatusEnum status), (override));
    MOCK_METHOD(void, on_reservation_cleared, (std::int32_t evse_id, std::int32_t connector_id), (override));
    MOCK_METHOD(void, on_reserved, (std::int32_t evse_id, std::int32_t connector_id), (override));
    MOCK_METHOD(void, on_security_event,
                (const ocpp::CiString<50>& event_type, const std::optional<ocpp::CiString<255>>& tech_info,
                 const std::optional<bool>& critical, const std::optional<ocpp::DateTime>& timestamp),
                (override));
    MOCK_METHOD(void, on_session_finished,
                (std::int32_t evse_id, std::int32_t connector_id,
                 const types::evse_manager::SessionEvent& session_event),
                (override));
    MOCK_METHOD(void, on_session_started,
                (std::int32_t evse_id, std::int32_t connector_id,
                 const types::evse_manager::SessionEvent& session_event),
                (override));
    MOCK_METHOD(void, on_transaction_finished,
                (std::int32_t evse_id, const std::string& session_id, const ocpp::DateTime& timestamp,
                 const ocpp::v2::MeterValue& meter_stop, types::evse_manager::StopTransactionReason reason,
                 ocpp::v2::TriggerReasonEnum trigger_reason, const std::optional<ocpp::v2::IdToken>& id_token,
                 const std::optional<std::string>& signed_meter_value, ocpp::v2::ChargingStateEnum charging_state),
                (override));
    MOCK_METHOD(void, on_transaction_started,
                (std::int32_t evse_id, std::int32_t connector_id, const std::string& session_id,
                 const ocpp::DateTime& timestamp, ocpp::v2::TriggerReasonEnum trigger_reason,
                 const ocpp::v2::MeterValue& meter_start, const std::optional<ocpp::v2::IdToken>& id_token,
                 const std::optional<ocpp::v2::IdToken>& group_id_token,
                 const std::optional<std::int32_t>& reservation_id, const std::optional<std::int32_t>& remote_start_id,
                 ocpp::v2::ChargingStateEnum charging_state),
                (override));
    MOCK_METHOD(void, on_unavailable, (std::int32_t evse_id, std::int32_t connector_id), (override));

    MOCK_METHOD(void, register_variable_listener,
                (const ocpp::v2::Component& component, const ocpp::v2::Variable& variable, listener_t listener),
                (override));
    MOCK_METHOD(bool, set_powermeter_public_key, (std::int32_t connector, const std::string& public_key_pem),
                (override));
    MOCK_METHOD((std::vector<ocpp::v2::SetVariableResult>), set_variables,
                (const std::vector<ocpp::v2::SetVariableData>& set_variable_data_vector, const std::string& source),
                (override));

    MOCK_METHOD(ocpp::v2::AuthorizeResponse, validate_token,
                (const types::authorization::ProvidedIdToken& provided_token), (override));
};

} // namespace stubs

namespace ocpp {
template <std::size_t S> bool operator==(const std::string& lhs, const CiString<S>& rhs) {
    return lhs == static_cast<std::string>(rhs);
}
template <std::size_t S> bool operator==(const CiString<S>& lhs, const std::string& rhs) {
    return static_cast<std::string>(lhs) == rhs;
}

namespace v2 {
bool operator==(const AdditionalInfo& lhs, const AdditionalInfo& rhs);
bool operator==(const ChangeAvailabilityRequest& lhs, const ChangeAvailabilityRequest& rhs);
bool operator==(const DataTransferRequest& lhs, const DataTransferRequest& rhs);
bool operator==(const GetLogResponse& lhs, const GetLogResponse& rhs);
bool operator==(const GetVariableData& lhs, const GetVariableData& rhs);
bool operator==(const IdToken& lhs, const IdToken& rhs);
bool operator==(const MeterValue& lhs, const MeterValue& rhs);
bool operator==(const SampledValue& lhs, const SampledValue& rhs);
bool operator==(const SignedMeterValue& lhs, const SignedMeterValue& rhs);
bool operator==(const StatusInfo& lhs, const StatusInfo& rhs);
bool operator==(const UnitOfMeasure& lhs, const UnitOfMeasure& rhs);
bool operator==(const UpdateFirmwareResponse& lhs, const UpdateFirmwareResponse& rhs);
} // namespace v2
bool operator==(const DisplayMessage& lhs, const DisplayMessage& rhs);
bool operator==(const DisplayMessageContent& lhs, const DisplayMessageContent& rhs);
} // namespace ocpp

namespace types::ocpp {
std::ostream& operator<<(std::ostream& out, DataTransferStatus value);
}
