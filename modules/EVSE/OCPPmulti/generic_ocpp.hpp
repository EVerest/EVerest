// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include "generic_chargepoint_interface.hpp"

#include <generated/interfaces/auth/Interface.hpp>
#include <generated/interfaces/auth_token_provider/Implementation.hpp>
#include <generated/interfaces/auth_token_validator/Implementation.hpp>
#include <generated/interfaces/charger_information/Interface.hpp>
#include <generated/interfaces/display_message/Interface.hpp>
#include <generated/interfaces/evse_manager/Interface.hpp>
#include <generated/interfaces/evse_security/Interface.hpp>
#include <generated/interfaces/external_energy_limits/Interface.hpp>
#include <generated/interfaces/iso15118_extensions/Interface.hpp>
#include <generated/interfaces/ocpp/Implementation.hpp>
#include <generated/interfaces/ocpp_1_6_charge_point/Implementation.hpp>
#include <generated/interfaces/ocpp_data_transfer/Implementation.hpp>
#include <generated/interfaces/ocpp_data_transfer/Interface.hpp>
#include <generated/interfaces/reservation/Interface.hpp>
#include <generated/interfaces/session_cost/Implementation.hpp>
#include <generated/interfaces/system/Interface.hpp>

#include <ocpp/v2/messages/BootNotification.hpp>
#include <ocpp/v2/messages/ClearDisplayMessage.hpp>
#include <ocpp/v2/messages/DataTransfer.hpp>
#include <ocpp/v2/messages/Get15118EVCertificate.hpp>
#include <ocpp/v2/messages/GetDisplayMessages.hpp>
#include <ocpp/v2/messages/GetLog.hpp>
#include <ocpp/v2/messages/NotifyEVChargingNeeds.hpp>
#include <ocpp/v2/messages/RequestStartTransaction.hpp>
#include <ocpp/v2/messages/ReserveNow.hpp>
#include <ocpp/v2/messages/SetDisplayMessage.hpp>
#include <ocpp/v2/messages/TransactionEvent.hpp>
#include <ocpp/v2/messages/UnlockConnector.hpp>
#include <ocpp/v2/messages/UpdateFirmware.hpp>

#include <device_model/everest_device_model_storage.hpp>
#include <error_handling.hpp>
#include <transaction_handler.hpp>

#include <mutex>
#include <queue>
#include <utility>
#include <variant>

namespace module {
struct Conf;
}

namespace ocpp_multi {

/// \brief Access module required and provided interfaces
///
/// Access to module interfaces is via this interface to aid unit testing
struct GenericOcppInterface {
    virtual ~GenericOcppInterface() = default;

    struct provides_t {
        auth_token_validatorImplBase& auth_validator;
        auth_token_providerImplBase& auth_provider;
        ocpp_data_transferImplBase& data_transfer;
        ocppImplBase& ocpp_generic;
        session_costImplBase& session_cost;
    };

    struct requires_t {
        authIntf& auth;
        const std::vector<std::unique_ptr<charger_informationIntf>>& charger_information;
        const std::vector<std::unique_ptr<ocpp_data_transferIntf>>& data_transfer;
        const std::vector<std::unique_ptr<display_messageIntf>>& display_message;
        const std::vector<std::unique_ptr<external_energy_limitsIntf>>& evse_energy_sink;
        const std::vector<std::unique_ptr<evse_managerIntf>>& evse_manager;
        const std::vector<std::unique_ptr<iso15118_extensionsIntf>>& extensions_15118;
        const std::vector<std::unique_ptr<reservationIntf>>& reservation;
        evse_securityIntf& security;
        systemIntf& system;
    };

    // auth_provider
    // auth_validator
    virtual types::authorization::ValidationResult
    handle_validate_token(const types::authorization::ProvidedIdToken& provided_token) = 0;
    // data_transfer
    virtual types::ocpp::DataTransferResponse handle_data_transfer(const types::ocpp::DataTransferRequest& request) = 0;
    // ocpp_generic
    virtual bool handle_stop() = 0;
    virtual bool handle_restart() = 0;
    virtual void handle_security_event(const types::ocpp::SecurityEvent& event) = 0;
    virtual std::vector<types::ocpp::GetVariableResult>
    handle_get_variables(const std::vector<types::ocpp::GetVariableRequest>& requests) = 0;
    virtual std::vector<types::ocpp::SetVariableResult>
    handle_set_variables(const std::vector<types::ocpp::SetVariableRequest>& requests, const std::string& source) = 0;
    virtual types::ocpp::ChangeAvailabilityResponse
    handle_change_availability(const types::ocpp::ChangeAvailabilityRequest& request) = 0;
    virtual void handle_monitor_variables(const std::vector<types::ocpp::ComponentVariable>& component_variables) = 0;
    // session cost
};

/// \brief Access configuration Interface
///
/// Access to configuration is via this interface to aid unit testing and also
/// to support locking if any of the configuration items become read-write
struct ConfigInterface {
    virtual ~ConfigInterface() = default;

    [[nodiscard]] virtual std::string getChargePointConfigPath() const = 0;
    [[nodiscard]] virtual int getCompositeScheduleIntervalS() const = 0;
    [[nodiscard]] virtual std::string getCoreDatabasePath() const = 0;
    [[nodiscard]] virtual std::string getCustomMrecErrorMapPath() const = 0;
    [[nodiscard]] virtual std::string getDatabasePath() const = 0;
    [[nodiscard]] virtual int getDelayOcppStart() const = 0;
    [[nodiscard]] virtual std::string getDeviceModelConfigMappings() const = 0;
    [[nodiscard]] virtual std::string getDeviceModelConfigPath() const = 0;
    [[nodiscard]] virtual std::string getDeviceModelDatabasePath() const = 0;
    [[nodiscard]] virtual std::string getDeviceModelDatabaseMigrationPath() const = 0;
    [[nodiscard]] virtual bool getEnableExternalWebsocketControl() const = 0;
    [[nodiscard]] virtual bool getEnableLegacyConfigMigration() const = 0;
    [[nodiscard]] virtual int getOcpp16NetworkConfigSlot() const = 0;
    [[nodiscard]] virtual std::string getEverestDeviceModelDatabasePath() const = 0;
    [[nodiscard]] virtual std::string getMessageLogPath() const = 0;
    [[nodiscard]] virtual int getMessageQueueResumeDelay() const = 0;
    [[nodiscard]] virtual int getRequestCompositeScheduleDurationS() const = 0;
    [[nodiscard]] virtual std::string getRequestCompositeScheduleUnit() const = 0;
    [[nodiscard]] virtual int getResetStopDelay() const = 0;
    [[nodiscard]] virtual std::string getUserConfigPath() const = 0;
};

class GenericOcpp : public GenericOcppInterface, public GenericChargePointCallbacks {
public:
    using MonitorListEntry = std::pair<ocpp::v2::Component, ocpp::v2::Variable>;
    using MonitorList = std::set<MonitorListEntry>;
    using ConnectorMap = std::map<std::int32_t, std::int32_t>;
    using EventInfo = GenericChargePointInterface::EventInfo;

private:
    struct powermeter_t {
        std::optional<float> state_of_charge;
        std::optional<types::powermeter::Powermeter> meter;
        std::optional<std::string> public_key;
    };

    using EventQueue =
        std::map<std::int32_t,
                 std::queue<std::variant<std::monostate, types::evse_manager::SessionEvent, EventInfo, powermeter_t,
                                         types::system::FirmwareUpdateStatus, types::system::LogStatus>>>;

    // member variables that don't require a mutex
    GenericChargePointInterface& mv_charge_point;
    const ModuleInfo& mv_info;
    ConfigInterface& mv_config;
    provides_t mv_provides;
    requires_t mv_requires;
    module::MREC_ERROR_MAP_TYPE mv_mrec_error_map;

    std::atomic<std::int32_t> mv_event_id_counter{0};
    std::atomic<ocpp::OcppProtocolVersion> mv_ocpp_protocol_version{ocpp::OcppProtocolVersion::Unknown};
    std::atomic_bool mv_started{false};

    everest::lib::util::monitor<std::map<std::int32_t, std::string>> mv_evse_evcc_id;
    everest::lib::util::monitor<std::map<std::int32_t, bool>> mv_evse_ready_map;
    everest::lib::util::monitor<std::map<std::int32_t, std::optional<float>>> mv_evse_soc_map;

    // these need to be thread safe - used by libocpp and this object
    std::shared_ptr<module::device_model::EverestDeviceModelStorage> m_everest_device_model_storage;

    // needs to be thread safe - used by v2_chargepoint and this object
    std::unique_ptr<module::TransactionHandler> m_transaction_handler;

    // these need protecting - used in device model storage and this object
    std::map<std::int32_t, types::evse_board_support::HardwareCapabilities> m_evse_hardware_capabilities_map;
    std::map<std::int32_t, std::vector<types::iso15118::EnergyTransferMode>> m_evse_supported_energy_transfer_modes;
    std::map<std::int32_t, bool> m_evse_service_renegotiation_supported;

    Everest::SteadyTimer m_charging_schedules_timer;

    std::mutex m_chargepoint_state_mutex; // mutex used for start/stop operations
    std::mutex m_session_event_mutex;
    std::mutex m_member_mux;

    EventQueue m_event_queue;
    MonitorList m_monitor_list;

    // Serialize + coalesce the charging-schedule recompute: it fires concurrently from the interval timer,
    // the libocpp message thread, and the K28 on_deadline/reaper callbacks.
    std::mutex recompute_mutex;
    std::atomic_bool recompute_pending{false};

public:
    using ConfigServiceClient = std::shared_ptr<Everest::config::ConfigServiceClient>;

    GenericOcpp(GenericChargePointInterface& charge_point, const ModuleInfo& info, ConfigInterface& config,
                const provides_t& provides, const requires_t& requires) :
        mv_charge_point(charge_point), mv_info(info), mv_config(config), mv_provides(provides), mv_requires(requires) {
    }

    void set_mode(GenericChargePointInterface::modes_t new_mode) {
        mv_charge_point.set_mode(new_mode);
    }

    // ------------------------------------------------------------------------
    // GenericOcppInterface

    types::authorization::ValidationResult
    handle_validate_token(const types::authorization::ProvidedIdToken& provided_token) override;
    types::ocpp::DataTransferResponse handle_data_transfer(const types::ocpp::DataTransferRequest& request) override;
    bool handle_stop() override;
    bool handle_restart() override;
    void handle_security_event(const types::ocpp::SecurityEvent& event) override;
    std::vector<types::ocpp::GetVariableResult>
    handle_get_variables(const std::vector<types::ocpp::GetVariableRequest>& requests) override;
    std::vector<types::ocpp::SetVariableResult>
    handle_set_variables(const std::vector<types::ocpp::SetVariableRequest>& requests,
                         const std::string& source) override;
    types::ocpp::ChangeAvailabilityResponse
    handle_change_availability(const types::ocpp::ChangeAvailabilityRequest& request) override;
    void handle_monitor_variables(const std::vector<types::ocpp::ComponentVariable>& component_variables) override;

    // ------------------------------------------------------------------------
    // startup

    void init();
    void ready(const ConfigServiceClient& client);

    void connect_websocket() {
        mv_charge_point.connect_websocket();
    }

    void disconnect_websocket() {
        mv_charge_point.disconnect_websocket();
    }

    // ------------------------------------------------------------------------
    // public handlers/callbacks

    void cb_error_cleared_handler(const Everest::error::Error& error);
    void cb_error_handler(const Everest::error::Error& error);

    // ------------------------------------------------------------------------
    // general purpose

    [[nodiscard]] const MonitorList& get_monitor_list() const {
        return m_monitor_list;
    }

protected:
    // Access to member variables for unit tests
    [[nodiscard]] auto& evse_evcc_id() {
        return mv_evse_evcc_id;
    }
    [[nodiscard]] auto& evse_hardware_capabilities_map() {
        return m_evse_hardware_capabilities_map;
    }
    [[nodiscard]] auto& evse_soc_map() {
        return mv_evse_soc_map;
    }
    [[nodiscard]] auto& evse_supported_energy_transfer_modes() {
        return m_evse_supported_energy_transfer_modes;
    }

    EventInfo convert_error(const Everest::error::Error& error);

    void init_check_energy_sink();
    void init_error_handlers();
    void init_evse_maps();
    void init_subscribe();
    void init_evse_subscribe();

    void ready_event_queue();
    void ready_module_configuration();
    void ready_transaction_handler();

    void visit_impl(std::int32_t evse_id, const types::evse_manager::SessionEvent& session_event);
    void visit_impl(std::int32_t evse_id, const EventInfo& event);
    void visit_impl(std::int32_t evse_id, const powermeter_t& meter);
    void visit_impl(std::int32_t evse_id, const types::system::FirmwareUpdateStatus& fw_update_status);
    void visit_impl(std::int32_t evse_id, const types::system::LogStatus& log_status);

    // ------------------------------------------------------------------------
    // GenericChargePointCallbacks

    void cb_all_connectors_unavailable() override;
    void cb_boot_notification(const ocpp::v2::BootNotificationResponse& boot_notification_response) override;
    bool cb_cancel_reservation(std::int32_t reservation_id) override;
    void cb_charging_needs(std::int32_t extensions_id, const types::iso15118::ChargingNeeds& charging_needs) override;
    ocpp::v2::ClearDisplayMessageResponse
    cb_clear_display_message(const ocpp::v2::ClearDisplayMessageRequest& request) override;
    std::future<ocpp::ConfigNetworkResult> cb_configure_network_connection_profile() override;
    bool cb_connector_effective_operative_status(std::int32_t evse_id, std::int32_t connector_id,
                                                 ocpp::v2::OperationalStatusEnum new_status) override;
    void cb_connection_state_changed(bool is_connected, ocpp::OcppProtocolVersion protocol_version) override;
    ocpp::v2::DataTransferResponse cb_data_transfer(const ocpp::v2::DataTransferRequest& request) override;
    void cb_default_price(const types::session_cost::DefaultPrice& messages) override;
    void cb_ev_info(std::int32_t evse_id, const types::evse_manager::EVInfo& ev_info) override;
    void cb_fault_cleared_handler(std::int32_t evse_id, const Everest::error::Error& error) override;
    void cb_fault_handler(std::int32_t evse_id, const Everest::error::Error& error) override;
    void cb_firmware_update_status(types::system::FirmwareUpdateStatus status) override;
    void cb_get_15118_ev_certificate_response(std::int32_t connector_id,
                                              const ocpp::v2::Get15118EVCertificateResponse& response,
                                              ocpp::v2::CertificateActionEnum certificate_action) override;
    std::vector<ocpp::DisplayMessage>
    cb_get_display_message(const ocpp::v2::GetDisplayMessagesRequest& request) override;
    ocpp::v2::GetLogResponse cb_get_log_request(const types::system::UploadLogsRequest& request) override;
    void cb_hw_capabilities(std::int32_t evse_id,
                            const types::evse_board_support::HardwareCapabilities& hw_capabilities) override;
    ocpp::ReservationCheckStatus
    cb_is_reservation_for_token(std::int32_t evse_id, const ocpp::CiString<255>& idToken,
                                const std::optional<ocpp::CiString<255>>& groupIdToken) override;
    bool cb_is_reset_allowed(const std::optional<std::int32_t>& evse_id, ResetType type) override;
    void cb_iso15118_certificate_request(std::int32_t extensions_id,
                                         const types::iso15118::RequestExiStreamSchema& certificate_request) override;
    void cb_log_status(types::system::LogStatus status) override;
    void cb_ocpp_messages(const std::string& message, ocpp::MessageDirection direction) override;
    bool cb_pause_charging(std::int32_t evse_id) override;
    void cb_powermeter(std::int32_t evse_id, const types::powermeter::Powermeter& power_meter) override;
    void cb_powermeter_public_key_ocmf(std::int32_t evse_id, const std::string& public_key) override;
    void cb_provide_token(const IdToken& id_token) override;
    void cb_ready(std::int32_t evse_id, bool ready) override;
    void cb_reservation_update(types::reservation::ReservationUpdateStatus status) override;
    ocpp::v2::ReserveNowStatusEnum cb_reserve_now(const ocpp::v2::ReserveNowRequest& request) override;
    void cb_reset(const std::optional<const std::int32_t>& evse_id, ResetType type) override;
    bool cb_resume_charging(std::int32_t evse_id) override;
    void cb_security_event(const ocpp::CiString<50>& event_type,
                           const std::optional<ocpp::CiString<255>>& tech_info) override;
    void cb_service_renegotiation_supported(std::int32_t extensions_id, bool service_renegotiation_supported) override;
    void cb_session_event(std::int32_t evse_id, types::evse_manager::SessionEvent session_event) override;
    void cb_set_charging_profiles() override;
    ocpp::v2::SetDisplayMessageResponse
    cb_set_display_message(const std::vector<ocpp::DisplayMessage>& messages) override;
    void cb_set_running_cost(const ocpp::RunningCost& running_cost, std::uint32_t number_of_decimals,
                             const std::optional<std::string>& currency_code) override;
    ocpp::v2::RequestStartStopStatusEnum
    cb_stop_transaction(std::int32_t evse_id, types::evse_manager::StopTransactionReason stop_reason) override;
    void cb_supported_energy_transfer_modes(
        std::int32_t evse_id,
        const std::vector<types::iso15118::EnergyTransferMode>& supported_energy_transfer_modes) override;
    void cb_tariff_message(const types::session_cost::TariffMessage& message) override;
    void cb_time_sync(const ocpp::DateTime& current_time) override;
    void cb_transaction_event(const ocpp::v2::TransactionEventRequest& transaction_event) override;
    void cb_transaction_event_response(const ocpp::v2::TransactionEventRequest& transaction_event,
                                       const ocpp::v2::TransactionEventResponse& transaction_event_response) override;
    ocpp::v2::UnlockConnectorResponse cb_unlock_connector(std::int32_t evse_id, std::int32_t connector_id) override;
    bool cb_update_allowed_energy_transfer_modes(
        const std::vector<ocpp::v2::EnergyTransferModeEnum>& allowed_energy_transfer_modes,
        const ocpp::CiString<36>& transaction_id) override;
    ocpp::v2::UpdateFirmwareResponse
    cb_update_firmware_request(const ocpp::v2::UpdateFirmwareRequest& request) override;
    ocpp::v2::SetNetworkProfileStatusEnum
    cb_validate_network_profile(const ocpp::v2::NetworkConnectionProfile& network_connection_profile) override;
    void cb_variable_set(const ocpp::v2::SetVariableData& set_variable_data) override;
    void cb_waiting_for_external_ready(std::int32_t evse_id, bool ready) override;
    bool map_error(const std::string& error, std::string& updated_error) override;
    void transaction_add(std::int32_t evse_id,
                         const std::shared_ptr<module::TransactionData>& transaction_data) override;
    std::shared_ptr<module::TransactionData> transaction_data(std::int32_t evse_id) override;
    module::TxEventEffect transaction_event(std::int32_t evse_id, module::TxEvent tx_event) override;
    void transaction_reset(std::int32_t evse_id) override;
    void update_evcc_id_token(std::int32_t evse, ocpp::v2::IdToken& id_token) override;

    // ------------------------------------------------------------------------
    // other callbacks

    void cb_variable_monitor(const ocpp::v2::Component& component, const ocpp::v2::Variable& variable,
                             const std::string& value);

    bool charging_schedules_timer_running();
    void charging_schedules_timer_start();
    void charging_schedules_timer_stop();

    std::optional<types::energy::ScheduleReqEntry>
    create_limits_entry(const std::string& timestamp, const ocpp::v2::EnhancedChargingSchedulePeriod& period,
                        ocpp::v2::ChargingRateUnitEnum unit);
    std::optional<types::energy::ScheduleSetpointEntry>
    create_setpoint_entry(std::int32_t setpoint_priority, const std::string& timestamp,
                          const ocpp::v2::EnhancedChargingSchedulePeriod& period, ocpp::v2::ChargingRateUnitEnum unit);
    std::pair<GenericChargePointInterface::ConnectorStructure, GenericChargePointInterface::ConnectorStructureV16>
    get_connector_structure();
    void process_authorised(std::int32_t evse_id, std::int32_t connector_id,
                            const types::evse_manager::SessionEvent& session_event);
    void process_deauthorised(std::int32_t evse_id, std::int32_t connector_id,
                              const types::evse_manager::SessionEvent& session_event);
    void process_charging_paused_ev(std::int32_t evse_id, std::int32_t connector_id,
                                    const types::evse_manager::SessionEvent& session_event);
    void process_charging_paused_evse(std::int32_t evse_id, std::int32_t connector_id,
                                      const types::evse_manager::SessionEvent& session_event);
    void process_charging_started(std::int32_t evse_id, std::int32_t connector_id,
                                  const types::evse_manager::SessionEvent& session_event);
    void process_disabled(std::int32_t evse_id, std::int32_t connector_id,
                          const types::evse_manager::SessionEvent& session_event);
    void process_enabled(std::int32_t evse_id, std::int32_t connector_id,
                         const types::evse_manager::SessionEvent& session_event);
    void process_reservation_end(std::int32_t evse_id, std::int32_t connector_id);
    void process_reserved(std::int32_t evse_id, std::int32_t connector_id);
    void process_session_event(std::int32_t evse_id, const types::evse_manager::SessionEvent& session_event);
    void process_session_finished(std::int32_t evse_id, std::int32_t connector_id,
                                  const types::evse_manager::SessionEvent& session_event);
    void process_session_resumed(std::int32_t evse_id, std::int32_t connector_id,
                                 const types::evse_manager::SessionEvent& session_event);
    void process_session_started(std::int32_t evse_id, std::int32_t connector_id,
                                 const types::evse_manager::SessionEvent& session_event);
    void process_transaction_finished(std::int32_t evse_id, std::int32_t connector_id,
                                      const types::evse_manager::SessionEvent& session_event);
    void process_transaction_started(std::int32_t evse_id, std::int32_t connector_id,
                                     const types::evse_manager::SessionEvent& session_event);
    void process_tx_event_effect(std::int32_t evse_id, module::TxEventEffect tx_event_effect,
                                 const types::evse_manager::SessionEvent& session_event);
    void publish_charging_schedules(const std::vector<ocpp::v2::EnhancedCompositeSchedule>& composite_schedules);
    void set_external_limits(const std::vector<ocpp::v2::EnhancedCompositeSchedule>& composite_schedules);
    void wait_all_ready();
};

} // namespace ocpp_multi
