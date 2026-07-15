// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "generic_ocpp.hpp"
#include "everest/logging.hpp"
#include "ocpp/common/types.hpp"
#include "ocpp/common/utils.hpp"

#include <everest/conversions/ocpp/ocpp_conversions.hpp>
#include <everest/external_energy_limits/external_energy_limits.hpp>
#include <everest/ocpp_module_common/conversions.hpp>
#include <ld-ev.hpp>
#include <ocpp/v2/ctrlr_component_variables.hpp>

namespace fs = std::filesystem;

namespace {

// OCPP 2.0.1 specific configuration variable names
constexpr const auto CENTRAL_CONTRACT_VALIDATION_ALLOWED_VAR_NAME = "CentralContractValidationAllowed";
constexpr const auto CONTRACT_CERTIFICATE_INSTALLATION_ENABLED_VAR_NAME = "ContractCertificateInstallationEnabled";
constexpr const auto EV_CONNECTION_TIMEOUT_VAR_NAME = "EVConnectionTimeOut";
constexpr const auto MASTER_PASS_GROUP_ID_VAR_NAME = "MasterPassGroupId";
constexpr const auto PNC_ENABLED_VAR_NAME = "PnCEnabled";
constexpr const auto SETPOINT_PRIORITY_VAR_NAME = "SetpointPriority";
constexpr const auto SETPOINT_SOURCE = "OCPP";
constexpr const auto TX_START_POINT_VAR_NAME = "TxStartPoint";
constexpr const auto TX_STOP_POINT_VAR_NAME = "TxStopPoint";

constexpr std::int32_t LOWEST_SETPOINT_PRIORITY = 1000;
constexpr std::int32_t HIGHEST_SETPOINT_PRIORITY = 0;

std::optional<ocpp::v2::IdToken> get_authorised_id_token(const types::evse_manager::SessionEvent& session_event) {
    using namespace module::conversions;

    std::optional<ocpp::v2::IdToken> result;

    const auto event_type = session_event.event;
    if (event_type == types::evse_manager::SessionEventEnum::SessionStarted) {
        if (!session_event.session_started.has_value()) {
            throw std::runtime_error("SessionEvent SessionStarted does not contain session_started context");
        }
        const auto session_started = session_event.session_started.value();
        if (session_started.id_tag.has_value()) {
            result = to_ocpp_id_token(session_started.id_tag.value().id_token);
        }
    } else if (event_type == types::evse_manager::SessionEventEnum::TransactionStarted) {
        if (!session_event.transaction_started.has_value()) {
            throw std::runtime_error("SessionEvent TransactionStarted does not contain transaction_started context");
        }
        const auto transaction_started = session_event.transaction_started.value();
        result = to_ocpp_id_token(transaction_started.id_tag.id_token);
    }

    return result;
}

std::int32_t get_connector_id_from_error(const Everest::error::Error& error) {
    if (error.origin.mapping.has_value() and error.origin.mapping.value().connector.has_value()) {
        return error.origin.mapping.value().connector.value();
    }
    return 1;
}

std::set<module::TxStartStopPoint> get_tx_start_stop_points(const std::string& tx_start_stop_point_csl) {
    using namespace module;

    std::set<TxStartStopPoint> tx_start_stop_points;
    for (const auto& tx_start_stop_point : ocpp::split_string(tx_start_stop_point_csl, ',', true)) {
        if (tx_start_stop_point == "ParkingBayOccupancy") {
            tx_start_stop_points.insert(TxStartStopPoint::ParkingBayOccupancy);
        } else if (tx_start_stop_point == "EVConnected") {
            tx_start_stop_points.insert(TxStartStopPoint::EVConnected);
        } else if (tx_start_stop_point == "Authorized") {
            tx_start_stop_points.insert(TxStartStopPoint::Authorized);
        } else if (tx_start_stop_point == "PowerPathClosed") {
            tx_start_stop_points.insert(TxStartStopPoint::PowerPathClosed);
        } else if (tx_start_stop_point == "EnergyTransfer") {
            tx_start_stop_points.insert(TxStartStopPoint::EnergyTransfer);
        } else if (tx_start_stop_point == "DataSigned") {
            tx_start_stop_points.insert(TxStartStopPoint::DataSigned);
        } else {
            // default to PowerPathClosed for now
            tx_start_stop_points.insert(TxStartStopPoint::PowerPathClosed);
        }
    }
    return tx_start_stop_points;
}

ocpp::v2::ChargingRateUnitEnum get_unit_or_default(const std::string& unit_string) {
    try {
        return ocpp::v2::conversions::string_to_charging_rate_unit_enum(unit_string);
    } catch (const std::out_of_range& e) {
        EVLOG_warning << "RequestCompositeScheduleUnit configured incorrectly with: " << unit_string
                      << ". Defaulting to using Amps.";
        return ocpp::v2::ChargingRateUnitEnum::A;
    }
}

std::string ocpp_protocol_version_to_string(const ocpp::OcppProtocolVersion ocpp_protocol_version) {
    switch (ocpp_protocol_version) {
    case ocpp::OcppProtocolVersion::v16:
        return "1.6";
    case ocpp::OcppProtocolVersion::v201:
        return "2.0.1";
    case ocpp::OcppProtocolVersion::v21:
        return "2.1";
    case ocpp::OcppProtocolVersion::Unknown:
    default:
        break;
    }
    return "Unknown";
}

inline std::filesystem::path update_path(const std::string_view dir, const std::filesystem::path& share,
                                         const std::filesystem::path& path) {
    std::filesystem::path result;

    if (path.is_relative()) {
        result = share / dir / path;
    } else {
        result = path;
    }

    return std::filesystem::absolute(result);
}

std::filesystem::path update_path_multi(const std::filesystem::path& share, const std::filesystem::path& path) {
    return update_path("OCPPmulti", share, path);
}

std::filesystem::path update_path_v16(const std::filesystem::path& share, const std::filesystem::path& path) {
    return update_path("OCPP", share, path);
}

std::filesystem::path update_path_v2(const std::filesystem::path& share, const std::filesystem::path& path) {
    return update_path("OCPP201", share, path);
}

std::filesystem::path remove_dir(std::filesystem::path path) {
    std::filesystem::path result = path;
    if (!path.empty()) {
        auto tmp = path.string();
        if (auto pos = tmp.rfind('/'); pos != std::string::npos) {
            tmp.erase(pos);
            result = tmp;
        }
    }
    return result;
}

} // namespace

namespace ocpp_multi {

// ----------------------------------------------------------------------------
// GenericOcpp

types::authorization::ValidationResult
GenericOcpp::handle_validate_token(const types::authorization::ProvidedIdToken& provided_token) {
    types::authorization::ValidationResult validation_result;

    if (mv_started.load()) {
        const auto response = mv_charge_point.validate_token(provided_token);
        validation_result = module::conversions::to_everest_validation_result(response);
    } else {
        EVLOG_warning << "ChargePoint not initialized, cannot handle validate token command";
        validation_result.authorization_status = types::authorization::AuthorizationStatus::Unknown;
    }

    return validation_result;
}

types::ocpp::DataTransferResponse GenericOcpp::handle_data_transfer(const types::ocpp::DataTransferRequest& request) {
    using namespace module::conversions;

    types::ocpp::DataTransferResponse response{};

    if (mv_started.load()) {
        ocpp::v2::DataTransferRequest ocpp_request = to_ocpp_data_transfer_request(request);
        auto ocpp_response = mv_charge_point.data_transfer_req(ocpp_request);

        if (ocpp_response.has_value()) {
            response = to_everest_data_transfer_response(ocpp_response.value());
        } else {
            response.status = types::ocpp::DataTransferStatus::Offline;
        }
    } else {
        EVLOG_warning << "ChargePoint not initialized, cannot data transfer command";
        response.status = types::ocpp::DataTransferStatus::Offline;
    }

    return response;
}

bool GenericOcpp::handle_stop() {
    // Disconnects the websocket connection and stops the OCPP communication.
    // No OCPP messages will be stored and sent after a restart.

    bool result{false};
    if (mv_started.load()) {
        std::lock_guard lock(m_chargepoint_state_mutex);
        charging_schedules_timer_stop();
        mv_charge_point.stop();
        result = true;
    } else {
        EVLOG_warning << "ChargePoint not initialized, cannot handle stop command";
    }
    return result;
}

bool GenericOcpp::handle_restart() {
    // Connects the websocket and enables OCPP communication after a previous
    // stop call.

    bool result{false};
    if (mv_started.load()) {
        std::lock_guard lock(m_chargepoint_state_mutex);
        charging_schedules_timer_start();
        result = mv_charge_point.restart();
    } else {
        EVLOG_warning << "ChargePoint not initialized, cannot handle restart command";
    }
    return result;
}

void GenericOcpp::handle_security_event(const types::ocpp::SecurityEvent& event) {
    if (mv_started.load()) {
        std::optional<ocpp::DateTime> timestamp;
        if (event.timestamp.has_value()) {
            timestamp = ocpp_conversions::to_ocpp_datetime_or_now(event.timestamp.value());
        }
        const auto event_type = ocpp::CiString<50>(event.type, ocpp::StringTooLarge::Truncate);
        std::optional<ocpp::CiString<255>> tech_info;
        if (event.info.has_value()) {
            tech_info = ocpp::CiString<255>(event.info.value(), ocpp::StringTooLarge::Truncate);
        }
        mv_charge_point.on_security_event(event_type, tech_info, event.critical, timestamp);
    } else {
        EVLOG_warning << "ChargePoint not yet initialized. Cannot handle security event.";
    }
}

std::vector<types::ocpp::GetVariableResult>
GenericOcpp::handle_get_variables(const std::vector<types::ocpp::GetVariableRequest>& requests) {
    using namespace module::conversions;

    std::vector<types::ocpp::GetVariableResult> results;

    if (mv_started.load()) {
        const auto _requests = to_ocpp_get_variable_data_vector(requests);
        const auto response = mv_charge_point.get_variables(_requests);
        results = to_everest_get_variable_result_vector(response);
    } else {
        EVLOG_warning << "ChargePoint not yet initialized. Cannot handle get variables request.";
        for (const auto& req : requests) {
            types::ocpp::GetVariableResult result{};
            result.status = types::ocpp::GetVariableStatusEnumType::Rejected;
            result.component_variable.component = req.component_variable.component;
            result.component_variable.variable = req.component_variable.variable;
            result.attribute_type = req.attribute_type;
            results.push_back(result);
        }
    }

    return results;
}

std::vector<types::ocpp::SetVariableResult>
GenericOcpp::handle_set_variables(const std::vector<types::ocpp::SetVariableRequest>& requests,
                                  const std::string& source) {
    using namespace module::conversions;

    std::vector<types::ocpp::SetVariableResult> results;

    if (mv_started.load()) {
        const auto _requests = to_ocpp_set_variable_data_vector(requests);
        const auto response_v2 = mv_charge_point.set_variables(_requests, source);
        results = to_everest_set_variable_result_vector(response_v2);
    } else {
        EVLOG_warning << "ChargePoint not yet initialized. Cannot handle set variables request.";
        for (const auto& req : requests) {
            types::ocpp::SetVariableResult result;
            result.status = types::ocpp::SetVariableStatusEnumType::Rejected;
            result.component_variable.component = req.component_variable.component;
            result.component_variable.variable = req.component_variable.variable;
            result.attribute_type = req.attribute_type;
            results.push_back(result);
        }
    }

    return results;
}

types::ocpp::ChangeAvailabilityResponse
GenericOcpp::handle_change_availability(const types::ocpp::ChangeAvailabilityRequest& request) {
    using namespace module::conversions;
    using ChangeAvailabilityStatusEnum = ocpp::v2::ChangeAvailabilityStatusEnum;

    ocpp::v2::ChangeAvailabilityResponse result;
    result.status = ChangeAvailabilityStatusEnum::Rejected;

    if (mv_started.load()) {
        const auto ocpp_request = to_ocpp_change_availability_request(request);
        result = mv_charge_point.on_change_availability(ocpp_request);
    } else {
        EVLOG_warning << "ChargePoint not initialized, cannot handle change availability command";
    }

    return to_everest_change_availability_response(result);
}

void GenericOcpp::handle_monitor_variables(const std::vector<types::ocpp::ComponentVariable>& component_variables) {
    using namespace module::conversions;

    if (mv_started.load()) {
        // register_variable_listener needs to support OCPP 1.6 and 2.x
        // for 1.6 every variable needs to be separately registered
        // for 2.0 only a single register is required
        // charge point implementations take care of these differences

        std::vector<MonitorListEntry> entries;
        entries.reserve(component_variables.size());
        for (const auto& cv : component_variables) {
            entries.emplace_back(to_ocpp_component(cv.component), to_ocpp_variable(cv.variable));
        }

        {
            // failures to insert are likely to
            // be the same variable being requested again
            std::lock_guard lock(m_member_mux);
            for (const auto& entry : entries) {
                (void)m_monitor_list.insert(entry);
            }
        }

        // register outside m_member_mux
        for (const auto& [component, variable] : entries) {
            mv_charge_point.register_variable_listener(component, variable,
                                                       [this](auto&&... args) { cb_variable_monitor(args...); });
        }
    } else {
        EVLOG_warning << "ChargePoint not initialized, cannot handle monitor variables command";
    }
}

// ----------------------------------------------------------------------------
// internal methods

GenericOcpp::EventInfo GenericOcpp::convert_error(const Everest::error::Error& error) {
    using namespace module;

    EventInfo event_data{};
    event_data.event_id = mv_event_id_counter++;
    event_data.evse_id = get_component_from_error(error).evse.value_or(ocpp::v2::EVSE{0}).id;
    event_data.error = error;
    event_data.event_cleared = true;
    return event_data;
}

void GenericOcpp::init() {
    // was originally in ready()
    const auto log_path = mv_config.getMessageLogPath();
    if (!fs::exists(log_path)) {
        try {
            fs::create_directory(log_path);
        } catch (const fs::filesystem_error& ex) {
            EVLOG_AND_THROW(ex);
        }
    }

    init_check_energy_sink();
    init_evse_maps();
    init_subscribe();
    init_evse_subscribe();
}

void GenericOcpp::ready(const ConfigServiceClient& client) {
    using namespace module::conversions;

    wait_all_ready();
    auto [evse_connector_structure, connector_mapping] = get_connector_structure();
    const auto der_routing_structure = evse_connector_structure;

    const auto share_path = remove_dir(mv_info.paths.share);

    const auto charge_point_config_path = update_path_v16(share_path, mv_config.getChargePointConfigPath());
    const auto database_path = update_path_v16(share_path, mv_config.getDatabasePath());
    const auto user_config_path = update_path_v16(share_path, mv_config.getUserConfigPath());

    const auto core_database_path = update_path_v2(share_path, mv_config.getCoreDatabasePath());
    const auto device_model_config_path = update_path_v2(share_path, mv_config.getDeviceModelConfigPath());
    const auto device_model_database_migration_path =
        update_path_v2(share_path, mv_config.getDeviceModelDatabaseMigrationPath());
    const auto device_model_database_path = update_path_v2(share_path, mv_config.getDeviceModelDatabasePath());
    const auto everest_device_model_database_path =
        update_path_v2(share_path, mv_config.getEverestDeviceModelDatabasePath());

    EVLOG_info << "Configuration";
    EVLOG_info << "Share path:                         " << share_path;
    EVLOG_info << "v16 config path:                    " << charge_point_config_path;
    EVLOG_info << "v16 database path:                  " << database_path;
    EVLOG_info << "v16 user config path:               " << user_config_path;
    EVLOG_info << "v2 core database path:              " << core_database_path;
    EVLOG_info << "v2 device model config path:        " << device_model_config_path;
    EVLOG_info << "v2 device model migration path:     " << device_model_database_migration_path;
    EVLOG_info << "v2 device model database path:      " << device_model_database_path;
    EVLOG_info << "EVerest device model database path: " << everest_device_model_database_path;

    {
        std::lock_guard lock(m_member_mux);
        // initialise everest device model
        m_everest_device_model_storage = std::make_shared<module::device_model::EverestDeviceModelStorage>(
            mv_requires.evse_manager, mv_requires.extensions_15118, m_evse_hardware_capabilities_map,
            m_evse_supported_energy_transfer_modes, m_evse_service_renegotiation_supported,
            everest_device_model_database_path, device_model_database_migration_path, client);
    }

    // clang-format off
    ocpp_multi::GenericChargePointInterface::init_args_t args{
        mv_config.getMessageLogPath(),
        share_path,
        charge_point_config_path,
        database_path,
        user_config_path,
        core_database_path,
        device_model_config_path,
        device_model_database_migration_path,
        device_model_database_path,
        std::move(evse_connector_structure),
        std::move(connector_mapping),
        m_everest_device_model_storage,
        mv_config.getDeviceModelConfigMappings(),
        static_cast<std::int32_t>(mv_config.getOcpp16NetworkConfigSlot()),
        mv_config.getEnableLegacyConfigMigration(),
    };
    // clang-format on

    // if charger information interface is connected, override only these specific
    // properties which were loaded from configuration file(s)
    if (!mv_requires.charger_information.empty()) {
        args.charger_info = mv_requires.charger_information.at(0)->call_get_charger_information();
    }

    // must run before the charge point is built: der_active_directives_callback can fire during
    // construction, and unmapped EVSEs need their DER controller disabled in the device model first
    if (ocpp_2_selected()) {
        init_grid_support_routing(der_routing_structure);
    } else if (!mv_requires.grid_support.empty()) {
        EVLOG_warning << "grid_support connections are wired but the active OCPP version is 1.6; DER is a "
                         "2.x-only feature and the grid_support wiring is inert";
    }

    mv_charge_point.init(args);

    // publish charging schedules at least once on startup
    cb_set_charging_profiles();
    charging_schedules_timer_start();

    if (grid_support_active()) {
        flush_pending_grid_support();
        grid_support_heartbeat_timer_start();
    }

    ready_module_configuration();
    ready_transaction_handler();

    const auto boot_reason = to_ocpp_boot_reason(mv_requires.system.call_get_boot_reason());
    mv_charge_point.set_message_queue_resume_delay(std::chrono::seconds(mv_config.getMessageQueueResumeDelay()));
    std::set<std::string> resuming_session_ids;
    {
        std::lock_guard lock(m_member_mux);
        resuming_session_ids = m_resuming_session_ids;
    }
    // we can now initialise the charge point's state machine. It reads the connector availability from the internal
    // database and potentially triggers enable/disable callbacks at the evse.
    mv_charge_point.start(boot_reason, resuming_session_ids, false);
    // drain queued events, then open the live path: mv_started flips only once the queue is
    // observed empty, so queued events always stay ahead of live-path events
    ready_event_queue();
    EVLOG_info << "OCPP started";

    // Signal to EVSEs to start their internal state machines
    for (const auto& evse : mv_requires.evse_manager) {
        evse->call_external_ready_to_start_charging();
    }

    // wait for potential events from the evses in order to start OCPP with the correct initial state (e.g. EV might
    // be plugged in at startup)
    std::this_thread::sleep_for(std::chrono::milliseconds(mv_config.getDelayOcppStart()));
    // start OCPP connection
    mv_charge_point.connect_websocket();
}

void GenericOcpp::init_check_energy_sink() {
    // ensure all evse_energy_sink(s) that are connected have an evse id mapping
    for (const auto& evse_sink : mv_requires.evse_energy_sink) {
        if (not evse_sink->get_mapping().has_value()) {
            EVLOG_critical << "Please configure an evse mapping in your configuration file for the connected "
                              "r_evse_energy_sink with module_id: "
                           << evse_sink->module_id;
            throw std::runtime_error("At least one connected evse_energy_sink misses a mapping to an evse.");
        }
    }
}

void GenericOcpp::init_evse_maps() {
    const auto n_managers = mv_requires.evse_manager.size();

    {
        auto ready_handle = mv_evse_ready_map.handle();
        for (std::size_t evse_id = 1; evse_id <= n_managers; evse_id++) {
            (*ready_handle)[evse_id] = false;
        }
    }
    {
        auto soc_handle = mv_evse_soc_map.handle();
        for (std::size_t evse_id = 1; evse_id <= n_managers; evse_id++) {
            (*soc_handle)[evse_id] = std::nullopt;
        }
    }
    {
        auto evse_evcc_id_handle = mv_evse_evcc_id.handle();
        for (std::size_t evse_id = 1; evse_id <= n_managers; evse_id++) {
            (*evse_evcc_id_handle)[evse_id] = "";
        }
    }

    std::lock_guard lock(m_member_mux);
    for (std::size_t evse_id = 1; evse_id <= n_managers; evse_id++) {
        m_evse_hardware_capabilities_map[evse_id] = types::evse_board_support::HardwareCapabilities{};
        m_evse_supported_energy_transfer_modes[evse_id] = {};
        m_evse_service_renegotiation_supported[evse_id] = false;
    }
}

void GenericOcpp::init_subscribe() {
    mv_requires.system.subscribe_firmware_update_status([this](auto arg) { cb_firmware_update_status(arg); });
    mv_requires.system.subscribe_log_status([this](auto arg) { cb_log_status(arg); });

    if (!mv_requires.reservation.empty() && mv_requires.reservation.at(0) != nullptr) {
        mv_requires.reservation.at(0)->subscribe_reservation_update([this](auto arg) { cb_reservation_update(arg); });
    }

    // Capability payloads carry their own evse_id and alarms are station-level, so subscribe on every
    // connection and forward regardless of which one raised the update.
    for (const auto& grid_support : mv_requires.grid_support) {
        grid_support->subscribe_capability([this](types::grid_support::EVSECapability evse_capability) {
            on_grid_support_capability(evse_capability);
        });
        grid_support->subscribe_alarm([this](types::grid_support::GridAlarm alarm) { on_grid_support_alarm(alarm); });
    }
}

void GenericOcpp::init_evse_subscribe() {
    using namespace module;

    int evse_id = 0;
    for (const auto& evse : mv_requires.evse_manager) {
        // IDs start at 1
        evse_id++;

        evse->subscribe_waiting_for_external_ready(
            [this, evse_id](bool ready) { cb_waiting_for_external_ready(evse_id, ready); });
        evse->subscribe_ready([this, evse_id](bool ready) { cb_ready(evse_id, ready); });
        evse->subscribe_hw_capabilities(
            [this, evse_id](const auto& hw_capabilities) { cb_hw_capabilities(evse_id, hw_capabilities); });
        evse->subscribe_supported_energy_transfer_modes([this, evse_id](const auto& supported_energy_transfer_modes) {
            cb_supported_energy_transfer_modes(evse_id, supported_energy_transfer_modes);
        });
        evse->subscribe_session_event(
            [this, evse_id](const auto& session_event) { cb_session_event(evse_id, session_event); });
        evse->subscribe_powermeter([this, evse_id](const auto& power_meter) { cb_powermeter(evse_id, power_meter); });
        evse->subscribe_powermeter_public_key_ocmf(
            [this, evse_id](const auto& power_meter) { cb_powermeter_public_key_ocmf(evse_id, power_meter); });
        evse->subscribe_ev_info([this, evse_id](const auto& ev_info) { cb_ev_info(evse_id, ev_info); });
        auto fault_handler = [this, evse_id](const auto& error) { cb_fault_handler(evse_id, error); };
        auto fault_cleared_handler = [this, evse_id](const auto& error) { cb_fault_cleared_handler(evse_id, error); };
        // A permanent fault from the evse requirement indicates that the evse should move to faulted state
        evse->subscribe_error(EVSE_MANAGER_INOPERATIVE_ERROR, fault_handler, fault_cleared_handler);
    }

    std::int32_t extensions_id = 0;
    for (const auto& extension : mv_requires.extensions_15118) {
        extension->subscribe_iso15118_certificate_request([this, extensions_id](const auto& certificate_request) {
            cb_iso15118_certificate_request(extensions_id, certificate_request);
        });
        extension->subscribe_charging_needs(
            [this, extensions_id](const auto& charging_needs) { cb_charging_needs(extensions_id, charging_needs); });
        extension->subscribe_service_renegotiation_supported(
            [this, extensions_id](bool service_renegotiation_supported) {
                cb_service_renegotiation_supported(extensions_id, service_renegotiation_supported);
            });

        extensions_id++;
    }
}

void GenericOcpp::ready_event_queue() {
    using namespace module;
    using namespace module::conversions;

    // Drain in rounds
    while (true) {
        EventQueue to_process;
        {
            std::lock_guard lock(m_member_mux);
            if (m_event_queue.empty()) {
                mv_started.store(true);
                break;
            }
            to_process = std::move(m_event_queue);
            m_event_queue.clear();
        }

        for (auto& [evse_id, evse_event_queue] : to_process) {
            while (!evse_event_queue.empty()) {
                // dispatch via the visit_impl overload set
                std::visit([this, evse_id](const auto& arg) { visit_impl(evse_id, arg); }, evse_event_queue.front());
                evse_event_queue.pop();
            }
        }
    }
}

bool GenericOcpp::enqueue_if_not_started(std::int32_t evse_id, const Event& event) {
    std::lock_guard lock(m_member_mux);
    bool result{false};
    if (!mv_started) {
        m_event_queue[evse_id].emplace(event);
        result = true;
    }
    return result;
}

void GenericOcpp::ready_module_configuration() {
    const auto ev_connection_timeout = mv_charge_point.get_ev_connection_timeout();
    if (ev_connection_timeout) {
        mv_requires.auth.call_set_connection_timeout(ev_connection_timeout.value());
    }

    const auto master_pass_group_id = mv_charge_point.get_master_pass_group_id();
    if (master_pass_group_id) {
        mv_requires.auth.call_set_master_pass_group_id(master_pass_group_id.value());
    }

    types::evse_manager::PlugAndChargeConfiguration pnc_config;

    const auto iso15118_pnc_enabled = mv_charge_point.get_pnc_enabled();
    pnc_config.pnc_enabled = iso15118_pnc_enabled;

    const auto central_contract_validation_allowed = mv_charge_point.get_central_contract_validation_allowed();
    pnc_config.central_contract_validation_allowed = central_contract_validation_allowed;

    const auto contract_certificate_installation_enabled =
        mv_charge_point.get_contract_certificate_installation_enabled();
    pnc_config.contract_certificate_installation_enabled = contract_certificate_installation_enabled;

    for (const auto& evse_manager : mv_requires.evse_manager) {
        evse_manager->call_set_plug_and_charge_configuration(pnc_config);
    }
}

void GenericOcpp::ready_transaction_handler() {
    std::set<module::TxStartStopPoint> tx_start_points;
    std::set<module::TxStartStopPoint> tx_stop_points;

    const auto tx_start_point_request_value = mv_charge_point.get_tx_start_point();
    if (tx_start_point_request_value) {
        auto tx_start_point_csl =
            tx_start_point_request_value.value(); // contains comma seperated list of TxStartPoints
        tx_start_points = get_tx_start_stop_points(tx_start_point_csl);
        EVLOG_info << "TxStartPoints from device model: " << tx_start_point_csl;
    }

    if (tx_start_points.empty()) {
        tx_start_points = {module::TxStartStopPoint::PowerPathClosed};
    }

    const auto tx_stop_point_request_value = mv_charge_point.get_tx_stop_point();
    if (tx_stop_point_request_value) {
        auto tx_stop_point_csl = tx_stop_point_request_value.value(); // contains comma seperated list of TxStopPoints
        tx_stop_points = get_tx_start_stop_points(tx_stop_point_csl);
        EVLOG_info << "TxStopPoints from device model: " << tx_stop_point_csl;
    }

    if (tx_stop_points.empty()) {
        tx_stop_points = {module::TxStartStopPoint::EVConnected, module::TxStartStopPoint::Authorized};
    }

    m_transaction_handler =
        std::make_unique<module::TransactionHandler>(mv_requires.evse_manager.size(), tx_start_points, tx_stop_points);
}

void GenericOcpp::visit_impl(std::int32_t evse_id, const types::evse_manager::SessionEvent& session_event) {
    EVLOG_info << "Processing queued event for evse_id: " << evse_id << ", event: " << session_event.event;
    process_session_event(evse_id, session_event);
}

void GenericOcpp::visit_impl(std::int32_t evse_id, const EventInfo& event) {
    EVLOG_info << "Processing queued error event for evse_id: " << evse_id << ": " << event.evse_id;
    mv_charge_point.on_event(event);

    if (event.error) {
        // We do only report inoperative errors as faults
        if (event.error->type == module::EVSE_MANAGER_INOPERATIVE_ERROR) {
            if (event.event_cleared) {
                mv_charge_point.on_fault_cleared(evse_id, get_connector_id_from_error(event.error.value()));
            } else {
                mv_charge_point.on_faulted(evse_id, get_connector_id_from_error(event.error.value()));
            }
        }
    }
}

void GenericOcpp::visit_impl(std::int32_t evse_id, const powermeter_t& meter) {
    if (meter.meter) {
        EVLOG_info << "Processing queued meter value for evse_id: " << evse_id;
        mv_charge_point.on_meter_value(evse_id, meter.state_of_charge, meter.meter.value());
        if (meter.meter->power_W) {
            m_everest_device_model_storage->update_power(evse_id, meter.meter->power_W->total);
        }
    }
    if (meter.public_key) {
        (void)mv_charge_point.set_powermeter_public_key(evse_id, meter.public_key.value());
    }
}

void GenericOcpp::visit_impl(std::int32_t evse_id, const types::system::FirmwareUpdateStatus& fw_update_status) {
    using namespace module::conversions;
    EVLOG_info << "Processing queued firmware update status";
    const auto disable_connectors_during_install =
        !fw_update_status.firmware_update_metadata.has_value() ||
        fw_update_status.firmware_update_metadata.value().disable_connectors_during_install.value_or(true);
    mv_charge_point.on_firmware_update_status_notification(
        fw_update_status.request_id, to_ocpp_firmware_status_enum(fw_update_status.firmware_update_status),
        disable_connectors_during_install);
}

void GenericOcpp::visit_impl(std::int32_t evse_id, const types::system::LogStatus& log_status) {
    using namespace module::conversions;
    EVLOG_info << "Processing queued log status";
    mv_charge_point.on_log_status_notification(to_ocpp_upload_logs_status_enum(log_status.log_status),
                                               log_status.request_id);
}

void GenericOcpp::visit_impl(std::int32_t evse_id, const types::reservation::ReservationUpdateStatus& status) {
    using namespace module::conversions;
    EVLOG_info << "Processing queued reservation status update for reservation " << status.reservation_id;
    try {
        mv_charge_point.on_reservation_status(status.reservation_id,
                                              to_ocpp_reservation_update_status_enum(status.reservation_status));
    } catch (const std::out_of_range& e) {
        EVLOG_error << "Failed to update status of reservation " << status.reservation_id
                    << "; the CSMS may still consider it active: " << e.what();
    }
}

// ----------------------------------------------------------------------------
// callbacks

void GenericOcpp::cb_all_connectors_unavailable() {
    EVLOG_info << "All connectors unavailable, proceed with firmware installation";
    mv_requires.system.call_allow_firmware_installation();
}

void GenericOcpp::cb_boot_notification(const ocpp::v2::BootNotificationResponse& boot_notification_response) {
    using namespace module::conversions;

    const auto everest_boot_notification_response = to_everest_boot_notification_response(boot_notification_response);
    mv_provides.ocpp_generic.publish_boot_notification_response(everest_boot_notification_response);
}

bool GenericOcpp::cb_cancel_reservation(std::int32_t reservation_id) {
    EVLOG_debug << "Received cancel reservation request for reservation id " << reservation_id;

    bool result{false};
    if (!mv_requires.reservation.empty() && mv_requires.reservation.at(0) != nullptr) {
        result = mv_requires.reservation.at(0)->call_cancel_reservation(reservation_id);
    }
    return result;
}

void GenericOcpp::cb_charging_needs(std::int32_t extensions_id, const types::iso15118::ChargingNeeds& charging_needs) {
    using namespace module::conversions;

    if (mv_started.load()) {
        const auto& mapping = mv_requires.extensions_15118.at(extensions_id)->get_mapping();
        if (mapping.has_value()) {
            try {
                ocpp::v2::NotifyEVChargingNeedsRequest charge_needs;
                charge_needs.chargingNeeds = to_ocpp_charging_needs(charging_needs);
                charge_needs.evseId = mapping.value().evse;

                mv_charge_point.on_ev_charging_needs(charge_needs);
            } catch (const std::out_of_range& e) {
                EVLOG_warning << "Could not convert iso15118 ChargingNeeds to OCPP NotifyEVChargingNeedsRequest: "
                              << e.what();
            }
        } else {
            EVLOG_warning << "ISO15118 Extension interface mapping not set! Not sending 'ChargingNeeds'!";
        }
    } else {
        EVLOG_info << "ISO15118 charging_needs received before OCPP was initialised, ignoring";
    }
}

ocpp::v2::ClearDisplayMessageResponse
GenericOcpp::cb_clear_display_message(const ocpp::v2::ClearDisplayMessageRequest& request) {
    using namespace module::conversions;

    ocpp::v2::ClearDisplayMessageResponse result;

    if (mv_requires.display_message.empty()) {
        result.status = ocpp::v2::ClearMessageStatusEnum::Unknown;
    } else {
        types::display_message::ClearDisplayMessageResponse response =
            mv_requires.display_message.at(0)->call_clear_display_message(
                to_everest_clear_display_message_request(request));
        result = to_ocpp_clear_display_message_response(response);
    }

    return result;
}

std::future<ocpp::ConfigNetworkResult> GenericOcpp::cb_configure_network_connection_profile() {
    std::promise<ocpp::ConfigNetworkResult> promise;
    std::future<ocpp::ConfigNetworkResult> future = promise.get_future();
    ocpp::ConfigNetworkResult result;
    result.success = true;
    promise.set_value(result);
    return future;
}

bool GenericOcpp::cb_connector_effective_operative_status(std::int32_t evse_id, std::int32_t connector_id,
                                                          ocpp::v2::OperationalStatusEnum new_status) {
    bool result{false};

    if (evse_id > 0) {
        auto& evse = mv_requires.evse_manager.at(evse_id - 1);

        if (new_status == ocpp::v2::OperationalStatusEnum::Operative) {
            if (evse->call_enable_disable(connector_id, {types::evse_manager::Enable_source::CSMS,
                                                         types::evse_manager::Enable_state::Enable, 5000})) {
                mv_charge_point.on_enabled(evse_id, connector_id);
                result = true;
            }
        } else {
            if (evse->call_enable_disable(connector_id, {types::evse_manager::Enable_source::CSMS,
                                                         types::evse_manager::Enable_state::Disable, 5000})) {
                mv_charge_point.on_unavailable(evse_id, connector_id);
                result = true;
            }
        }
    } else {
        EVLOG_warning << "cb_connector_effective_operative_status: invalid evse_id: " << evse_id;
    }

    return result;
}

void GenericOcpp::cb_connection_state_changed(bool is_connected, ocpp::OcppProtocolVersion protocol_version) {
    if (is_connected) {
        mv_ocpp_protocol_version = protocol_version;
    } else {
        mv_ocpp_protocol_version = ocpp::OcppProtocolVersion::Unknown;
    }
    mv_provides.ocpp_generic.publish_is_connected(is_connected);
}

ocpp::v2::DataTransferResponse GenericOcpp::cb_data_transfer(const ocpp::v2::DataTransferRequest& request) {
    ocpp::v2::DataTransferResponse response{};
    if (mv_requires.data_transfer.empty()) {
        EVLOG_error << "data_transfer called with no configured connections";
        // If the data_transfer requirement is not fullfiled we reply with UnknownVendorId
        // Rejected is reserved for "Message has been accepted but the contained request is rejected" according
        // to the OCPP spec (similar definition through all protocol versions)
        response.status = ocpp::v2::DataTransferStatusEnum::UnknownVendorId;
    } else {
        using namespace module::conversions;

        types::ocpp::DataTransferRequest data_transfer_request = to_everest_data_transfer_request(request);
        types::ocpp::DataTransferResponse data_transfer_response =
            mv_requires.data_transfer.at(0)->call_data_transfer(data_transfer_request);
        response = to_ocpp_data_transfer_response(data_transfer_response);
    }
    return response;
}

void GenericOcpp::cb_default_price(const types::session_cost::DefaultPrice& messages) {
    mv_provides.session_cost.publish_default_price(messages);
}

void GenericOcpp::cb_error_cleared_handler(const Everest::error::Error& error) {
    using namespace module;

    // handled by specific evse_manager error handler
    if (error.type != EVSE_MANAGER_INOPERATIVE_ERROR) {
        auto event_data = convert_error(error);
        event_data.event_cleared = true;
        if (!enqueue_if_not_started(event_data.evse_id, event_data)) {
            mv_charge_point.on_event(event_data);
        }
    }
}

void GenericOcpp::cb_error_handler(const Everest::error::Error& error) {
    using namespace module;

    // handled by specific evse_manager error handler
    if (error.type != EVSE_MANAGER_INOPERATIVE_ERROR) {
        auto event_data = convert_error(error);
        event_data.event_cleared = false;
        if (!enqueue_if_not_started(event_data.evse_id, event_data)) {
            mv_charge_point.on_event(event_data);
        }
    }
}

void GenericOcpp::cb_ev_info(std::int32_t evse_id, const types::evse_manager::EVInfo& ev_info) {
    if (mv_started.load()) {
        if (ev_info.soc.has_value()) {
            mv_evse_soc_map.handle()->at(evse_id) = ev_info.soc;
        }
        if (ev_info.evcc_id.has_value()) {
            mv_evse_evcc_id.handle()->at(evse_id) = ev_info.evcc_id.value();
            m_everest_device_model_storage->update_connected_ev_vehicle_id(evse_id, ev_info.evcc_id.value());
        }
    } else {
        EVLOG_info << "EV Info received from evse_manager before DM was instantiated, ignoring...";
        EVLOG_info << "EV Info will be retrieved later";
    }
}

void GenericOcpp::cb_fault_cleared_handler(std::int32_t evse_id, const Everest::error::Error& error) {
    using namespace module;

    auto event_data = convert_error(error);
    event_data.event_cleared = true;
    if (!enqueue_if_not_started(event_data.evse_id, event_data)) {
        mv_charge_point.on_event(event_data);
        mv_charge_point.on_fault_cleared(evse_id, get_connector_id_from_error(error));
    }
}

void GenericOcpp::cb_fault_handler(std::int32_t evse_id, const Everest::error::Error& error) {
    using namespace module;

    auto event_data = convert_error(error);
    event_data.event_cleared = false;
    if (!enqueue_if_not_started(event_data.evse_id, event_data)) {
        mv_charge_point.on_event(event_data);
        mv_charge_point.on_faulted(evse_id, get_connector_id_from_error(error));
    }
}

void GenericOcpp::cb_firmware_update_status(types::system::FirmwareUpdateStatus status) {
    using namespace module::conversions;

    if (!enqueue_if_not_started(0, status)) {
        const auto disable_connectors_during_install =
            !status.firmware_update_metadata.has_value() ||
            status.firmware_update_metadata.value().disable_connectors_during_install.value_or(true);
        mv_charge_point.on_firmware_update_status_notification(
            status.request_id, to_ocpp_firmware_status_enum(status.firmware_update_status),
            disable_connectors_during_install);
    }
}

void GenericOcpp::cb_get_15118_ev_certificate_response(std::int32_t connector_id,
                                                       const ocpp::v2::Get15118EVCertificateResponse& response,
                                                       ocpp::v2::CertificateActionEnum certificate_action) {
    using namespace module::conversions;

    EVLOG_debug << "Received response from get_15118_ev_certificate_request: " << response;
    // transform response, inject action, send to associated EvseManager
    types::iso15118::ResponseExiStreamStatus everest_response;
    everest_response.status = to_everest_iso15118_status(response.status);
    everest_response.certificate_action = to_everest_certificate_action_enum(certificate_action);
    if (not response.exiResponse.get().empty()) {
        // since exi_response is an optional in the EVerest type we only set it when not empty
        everest_response.exi_response = response.exiResponse.get();
    }

    mv_requires.extensions_15118.at(connector_id)->call_set_get_certificate_response(everest_response);
}

std::vector<ocpp::DisplayMessage>
GenericOcpp::cb_get_display_message(const ocpp::v2::GetDisplayMessagesRequest& request) {
    using namespace module::conversions;

    std::vector<ocpp::DisplayMessage> result;

    if (!mv_requires.display_message.empty()) {
        types::display_message::GetDisplayMessageRequest get_request;

        types::display_message::GetDisplayMessageResponse response =
            mv_requires.display_message.at(0)->call_get_display_messages(to_everest_display_message_request(request));

        if (response.messages.has_value() && !response.messages.value().empty()) {
            for (const auto& message : response.messages.value()) {
                result.push_back(ocpp_conversions::to_ocpp_display_message(message));
            }
        }
    }
    return result;
}

ocpp::v2::GetLogResponse GenericOcpp::cb_get_log_request(const types::system::UploadLogsRequest& request) {
    using namespace module::conversions;

    auto req = request;
    if (req.retries.has_value()) {
        // As defined in OCPP the initial attempt does not count as a retry
        // hence the + 1
        req.retries = req.retries.value() + 1;
    }
    const auto response = mv_requires.system.call_upload_logs(req);
    return to_ocpp_get_log_response(response);
}

void GenericOcpp::cb_hw_capabilities(std::int32_t evse_id,
                                     const types::evse_board_support::HardwareCapabilities& hw_capabilities) {
    std::lock_guard lock(m_member_mux);
    m_evse_hardware_capabilities_map[evse_id] = hw_capabilities;
}

ocpp::ReservationCheckStatus
GenericOcpp::cb_is_reservation_for_token(std::int32_t evse_id, const ocpp::CiString<255>& idToken,
                                         const std::optional<ocpp::CiString<255>>& groupIdToken) {

    // for v1.6 the incoming id is the OCPP1.6 connector id, which maps 1:1 to the EVerest evse_id
    auto reservation_status = types::reservation::ReservationCheckStatus::NotReserved;

    if (!mv_requires.reservation.empty() && mv_requires.reservation.at(0) != nullptr) {
        types::reservation::ReservationCheck reservation_check_request;
        reservation_check_request.evse_id = evse_id;
        reservation_check_request.id_token = idToken.get();
        if (groupIdToken.has_value()) {
            reservation_check_request.group_id_token = groupIdToken.value().get();
        }

        reservation_status = mv_requires.reservation.at(0)->call_exists_reservation(reservation_check_request);
    }
    return ocpp_conversions::to_ocpp_reservation_check_status(reservation_status);
}

bool GenericOcpp::cb_is_reset_allowed(const std::optional<std::int32_t>& evse_id, ResetType type) {
    bool result{false};
    if (evse_id.has_value()) {
        EVLOG_debug << "Reset of EVSE is currently not supported";
    } else {
        auto r_type{types::system::ResetType::NotSpecified};
        bool do_reset{true};

        switch (type) {
        case ResetType::Hard:
            r_type = types::system::ResetType::Hard;
            break;
        case ResetType::Soft:
            r_type = types::system::ResetType::Soft;
            break;
        case ResetType::Immediate:
        case ResetType::ImmediateAndResume:
        case ResetType::OnIdle:
            break;
        default:
            EVLOG_warning << "Could not convert OCPP ResetEnum to EVerest ResetType while executing "
                             "is_reset_allowed_callback.";
            do_reset = false;
            break;
        }

        if (do_reset) {
            result = mv_requires.system.call_is_reset_allowed(r_type);
        }
    }

    return result;
}

void GenericOcpp::cb_iso15118_certificate_request(std::int32_t extensions_id,
                                                  const types::iso15118::RequestExiStreamSchema& certificate_request) {
    using namespace module::conversions;

    if (mv_started.load()) {
        // response is via cb_iso15118_certificate_response()
        mv_charge_point.on_get_15118_ev_certificate_request(extensions_id,
                                                            to_ocpp_get_15118_certificate_request(certificate_request));
    } else {
        EVLOG_info << "ISO15118 certificate_request received before OCPP was initialised, ignoring";
    }
}

void GenericOcpp::cb_log_status(types::system::LogStatus status) {
    using namespace module::conversions;

    if (!enqueue_if_not_started(0, status)) {
        mv_charge_point.on_log_status_notification(to_ocpp_upload_logs_status_enum(status.log_status),
                                                   status.request_id);
    }
}

void GenericOcpp::cb_ocpp_messages(const std::string& message, ocpp::MessageDirection direction) {
    types::ocpp::Message ocpp_message;
    ocpp_message.message = message;
    if (mv_ocpp_protocol_version != ocpp::OcppProtocolVersion::Unknown) {
        ocpp_message.version = ocpp_protocol_version_to_string(mv_ocpp_protocol_version);
    }
    switch (direction) {
    case ocpp::MessageDirection::CSMSToChargingStation:
        ocpp_message.direction = types::ocpp::MessageDirection::CSMSToChargingStation;
        break;
    case ocpp::MessageDirection::ChargingStationToCSMS:
        ocpp_message.direction = types::ocpp::MessageDirection::ChargingStationToCSMS;
        break;
    default:
        // unknown message direction (ignored)
        break;
    }
    mv_provides.ocpp_generic.publish_ocpp_message(ocpp_message);
}

bool GenericOcpp::cb_pause_charging(std::int32_t evse_id) {
    bool result{false};
    if (evse_id > 0 && evse_id <= mv_requires.evse_manager.size()) {
        result = mv_requires.evse_manager.at(evse_id - 1)->call_pause_charging();
    }
    return result;
}

void GenericOcpp::cb_powermeter(std::int32_t evse_id, const types::powermeter::Powermeter& power_meter) {
    using namespace module::conversions;
    powermeter_t meter;
    meter.meter = power_meter;
    {
        auto evse_soc_map_handle = mv_evse_soc_map.handle();
        if (evse_soc_map_handle->at(evse_id).has_value()) {
            meter.state_of_charge = evse_soc_map_handle->at(evse_id);
        }
    }

    if (!enqueue_if_not_started(evse_id, meter)) {
        mv_charge_point.on_meter_value(evse_id, meter.state_of_charge, meter.meter.value());
        if (power_meter.power_W) {
            m_everest_device_model_storage->update_power(evse_id, power_meter.power_W->total);
        }
    }
}

void GenericOcpp::cb_powermeter_public_key_ocmf(std::int32_t evse_id, const std::string& public_key) {
    if (!enqueue_if_not_started(evse_id, powermeter_t{{}, {}, public_key})) {
        (void)mv_charge_point.set_powermeter_public_key(evse_id, public_key);
    }
}

void GenericOcpp::cb_ready(std::int32_t evse_id, bool ready) {
    if (ready) {
        EVLOG_info << "EVSE " << evse_id << " ready.";
        mv_evse_ready_map.handle()->at(evse_id) = true;
        mv_evse_ready_map.notify_one();
    }
}

void GenericOcpp::cb_provide_token(const IdToken& id_token) {
    using namespace module::conversions;

    types::authorization::ProvidedIdToken provided_token;
    provided_token.id_token = to_everest_id_token(id_token.token);
    provided_token.authorization_type = types::authorization::AuthorizationType::OCPP;
    provided_token.prevalidated = id_token.prevalidated;
    provided_token.request_id = id_token.request_id;

    if (id_token.group_id_token.has_value()) {
        provided_token.parent_id_token = to_everest_id_token(id_token.group_id_token.value());
    }

    if (id_token.evse_id.has_value()) {
        provided_token.connectors = std::vector<std::int32_t>{id_token.evse_id.value()};
    } else if (!id_token.connectors.empty()) {
        provided_token.connectors = id_token.connectors;
    }
    mv_provides.auth_provider.publish_provided_token(provided_token);
}

void GenericOcpp::cb_reservation_update(types::reservation::ReservationUpdateStatus status) {
    if (status.reservation_status == types::reservation::Reservation_status::Expired ||
        status.reservation_status == types::reservation::Reservation_status::Removed) {
        EVLOG_debug << "Received reservation status update for reservation " << status.reservation_id << ": "
                    << (status.reservation_status == types::reservation::Reservation_status::Expired ? "Expired"
                                                                                                     : "Removed");
        if (!enqueue_if_not_started(0, status)) {
            visit_impl(0, status);
        }
    }
}

ocpp::v2::ReserveNowStatusEnum GenericOcpp::cb_reserve_now(const ocpp::v2::ReserveNowRequest& request) {
    using namespace module::conversions;

    auto result = ocpp::v2::ReserveNowStatusEnum::Rejected;

    if (mv_requires.reservation.empty() || mv_requires.reservation.at(0) == nullptr) {
        EVLOG_info << "Reservation rejected because the interface r_reservation is a nullptr";
    } else {
        types::reservation::Reservation reservation;
        reservation.reservation_id = request.id;
        reservation.expiry_time = request.expiryDateTime.to_rfc3339();
        reservation.id_token = request.idToken.idToken;
        reservation.evse_id = request.evseId;
        if (request.groupIdToken.has_value()) {
            reservation.parent_id_token = request.groupIdToken.value().idToken;
        }
        if (request.connectorType.has_value()) {
            try {
                reservation.connector_type =
                    types::evse_manager::string_to_connector_type_enum(request.connectorType.value());
            } catch (const std::out_of_range&) {
                // CSMS-controlled input; must not throw into libocpp's message thread
                EVLOG_warning << "ReserveNow rejected: unknown connector type: " << request.connectorType.value();
                return ocpp::v2::ReserveNowStatusEnum::Rejected;
            }
        }
        const auto revervation_result = mv_requires.reservation.at(0)->call_reserve_now(reservation);
        result = to_ocpp_reservation_status(revervation_result);
    }
    return result;
}

void GenericOcpp::cb_reset(const std::optional<const std::int32_t>& evse_id, ResetType type) {
    if (evse_id.has_value()) {
        EVLOG_warning << "Reset of EVSE is currently not supported";
    } else {
        bool scheduled = type == ResetType::OnIdle;
        auto r_type{types::system::ResetType::NotSpecified};
        bool do_reset{true};

        switch (type) {
        case ResetType::Hard:
            r_type = types::system::ResetType::Hard;
            break;
        case ResetType::Soft:
            r_type = types::system::ResetType::Soft;
            break;
        case ResetType::Immediate:
        case ResetType::ImmediateAndResume:
        case ResetType::OnIdle:
            break;
        default:
            EVLOG_warning << "Could not convert OCPP ResetEnum to EVerest ResetType while executing reset_callack. No "
                             "reset will be executed.";
            do_reset = false;
            break;
        }

        if (do_reset) {
            // small delay before stopping the charge point to make sure all responses are received
            std::this_thread::sleep_for(std::chrono::seconds(mv_config.getResetStopDelay()));
            mv_charge_point.stop();
            mv_requires.system.call_reset(r_type, scheduled);
        }
    }
}

bool GenericOcpp::cb_resume_charging(std::int32_t evse_id) {
    bool result{false};
    if (evse_id > 0 && evse_id <= mv_requires.evse_manager.size()) {
        result = mv_requires.evse_manager.at(evse_id - 1)->call_resume_charging();
    }
    return result;
}

void GenericOcpp::cb_security_event(const ocpp::CiString<50>& event_type,
                                    const std::optional<ocpp::CiString<255>>& tech_info) {
    types::ocpp::SecurityEvent event;
    event.type = event_type.get();
    EVLOG_info << "Security Event in OCPP occurred: " << event.type;
    if (tech_info.has_value()) {
        event.info = tech_info.value().get();
    }
    mv_provides.ocpp_generic.publish_security_event(event);
}

void GenericOcpp::cb_service_renegotiation_supported(std::int32_t extensions_id, bool service_renegotiation_supported) {
    const auto& mapping = mv_requires.extensions_15118.at(extensions_id)->get_mapping();
    if (mapping.has_value()) {
        std::lock_guard lock(m_member_mux);
        m_evse_service_renegotiation_supported[mapping->evse] = service_renegotiation_supported;
    } else {
        EVLOG_warning << "ISO15118 Extension interface mapping not set! Not retrieving 'Service "
                         "Renegotiation Supported'!";
    }
}

void GenericOcpp::cb_session_event(std::int32_t evse_id, types::evse_manager::SessionEvent session_event) {
    if (session_event.event == types::evse_manager::SessionEventEnum::SessionResumed) {
        // collect resumed session ids so that start() can inform the charge point that these
        // transactions shall not be stopped; the event itself is still queued/processed since
        // OCPP2.x restores the transaction data in its session resumed handler
        std::lock_guard lock(m_member_mux);
        m_resuming_session_ids.insert(session_event.uuid);
    }
    if (enqueue_if_not_started(evse_id, session_event)) {
        EVLOG_info << "OCPP not fully initialised, but received a session event on evse_id: " << evse_id
                   << " that will be queued up: " << session_event.event;
    } else {
        process_session_event(evse_id, session_event);
    }
}

void GenericOcpp::cb_set_charging_profiles() {
    // shutdown() has gated us; a stray timer/libocpp fire must not touch mv_charge_point during teardown.
    if (mv_shutting_down.load()) {
        return;
    }
    // Single-flight + coalesce: the recompute body publishes schedules within EVerest and applies the
    // external limits. It now fires concurrently from the interval timer, the libocpp message thread,
    // and the K28 on_deadline/reaper callbacks, so serialize it here (the convergence point) — concurrent
    // fires must not apply a stale composite over a fresh one, and a burst collapses into one recompute.
    recompute_pending.store(true);
    // Acquire / run / release / re-check: a fire whose store(true) lands after the holder's final
    // exchange(false) but before it releases the mutex would fail try_lock and otherwise be dropped
    // until the next trigger. Re-checking the flag after the lock is released runs that pending
    // request instead of losing it.
    while (recompute_pending.load()) {
        if (!recompute_mutex.try_lock()) {
            // Another thread holds the recompute; it will observe recompute_pending and run our update.
            return;
        }
        {
            try {
                std::lock_guard guard(recompute_mutex, std::adopt_lock);
                // Re-check under the lock: shutdown() sets the flag then drains this mutex, so a fire
                // that slipped past the check above cannot start a recompute once teardown has begun.
                if (mv_shutting_down.load()) {
                    recompute_pending.store(false);
                    return;
                }
                while (recompute_pending.exchange(false)) {
                    const auto composite_schedule_unit =
                        get_unit_or_default(mv_config.getRequestCompositeScheduleUnit());
                    const auto composite_schedules = mv_charge_point.get_all_composite_schedules(
                        mv_config.getRequestCompositeScheduleDurationS(), composite_schedule_unit);
                    publish_charging_schedules(composite_schedules);
                    set_external_limits(composite_schedules);
                }
            } catch (const std::exception& error) {
                EVLOG_warning << "Composite calculation failed, unable to send external_limits";
            }
        }
    }
}

ocpp::v2::SetDisplayMessageResponse
GenericOcpp::cb_set_display_message(const std::vector<ocpp::DisplayMessage>& messages) {
    using namespace module::conversions;

    ocpp::v2::SetDisplayMessageResponse response;
    if (mv_requires.display_message.empty()) {
        response.status = ocpp::v2::DisplayMessageStatusEnum::Rejected;
        return response;
    }

    std::vector<types::display_message::DisplayMessage> display_messages;
    for (const ocpp::DisplayMessage& message : messages) {
        const types::display_message::DisplayMessage m = ocpp_conversions::to_everest_display_message(message);
        display_messages.push_back(m);
    }

    const types::display_message::SetDisplayMessageResponse display_message_response =
        mv_requires.display_message.at(0)->call_set_display_message(display_messages);
    response = to_ocpp_set_display_message_response(display_message_response);

    return response;
}

void GenericOcpp::cb_set_running_cost(const ocpp::RunningCost& running_cost, std::uint32_t number_of_decimals,
                                      const std::optional<std::string>& currency_code) {
    std::optional<types::money::CurrencyCode> currency;
    if (currency_code.has_value()) {
        try {
            currency = types::money::string_to_currency_code(currency_code.value());
        } catch (const std::out_of_range& e) {
            // If conversion fails, we just don't add the currency code. But we want to see it in the
            // logging.
            EVLOG_error << e.what();
        }
    }
    const types::session_cost::SessionCost cost =
        ocpp_conversions::create_session_cost(running_cost, number_of_decimals, currency);
    mv_provides.session_cost.publish_session_cost(cost);
}

ocpp::v2::RequestStartStopStatusEnum
GenericOcpp::cb_stop_transaction(std::int32_t evse_id, types::evse_manager::StopTransactionReason stop_reason) {
    using namespace module::conversions;

    auto result = ocpp::v2::RequestStartStopStatusEnum::Rejected;

    if (evse_id > 0 && evse_id <= mv_requires.evse_manager.size()) {
        types::evse_manager::StopTransactionRequest req;
        req.reason = stop_reason;
        result = mv_requires.evse_manager.at(evse_id - 1)->call_stop_transaction(req)
                     ? ocpp::v2::RequestStartStopStatusEnum::Accepted
                     : ocpp::v2::RequestStartStopStatusEnum::Rejected;
    }
    return result;
}

void GenericOcpp::cb_supported_energy_transfer_modes(
    std::int32_t evse_id, const std::vector<types::iso15118::EnergyTransferMode>& supported_energy_transfer_modes) {
    std::lock_guard lock(m_member_mux);
    m_evse_supported_energy_transfer_modes[evse_id] = supported_energy_transfer_modes;
}

void GenericOcpp::cb_tariff_message(const types::session_cost::TariffMessage& message) {
    mv_provides.session_cost.publish_tariff_message(message);
}

void GenericOcpp::cb_time_sync(const ocpp::DateTime& current_time) {
    mv_requires.system.call_set_system_time(current_time.to_rfc3339());
}

void GenericOcpp::cb_transaction_event(const ocpp::v2::TransactionEventRequest& transaction_event,
                                       const std::optional<std::string>& transaction_id) {
    using namespace module::conversions;

    auto ocpp_transaction_event = to_everest_ocpp_transaction_event(transaction_event);
    ocpp_transaction_event.transaction_id = transaction_id;
    mv_provides.ocpp_generic.publish_ocpp_transaction_event(ocpp_transaction_event);
}

void GenericOcpp::cb_transaction_event_response(const ocpp::v2::TransactionEventRequest& transaction_event,
                                                const ocpp::v2::TransactionEventResponse& transaction_event_response,
                                                const std::optional<std::string>& transaction_id) {
    using namespace module::conversions;

    auto ocpp_transaction_event = to_everest_ocpp_transaction_event(transaction_event);
    ocpp_transaction_event.transaction_id = transaction_id;
    auto ocpp_transaction_event_response = to_everest_transaction_event_response(transaction_event_response);
    ocpp_transaction_event_response.original_transaction_event = ocpp_transaction_event;
    mv_provides.ocpp_generic.publish_ocpp_transaction_event_response(ocpp_transaction_event_response);
    if (transaction_event_response.idTokenInfo.has_value() and transaction_event.evse.has_value()) {
        types::authorization::ValidationResultUpdate result_update;
        result_update.validation_result = to_everest_validation_result(transaction_event_response.idTokenInfo.value());
        result_update.connector_id = transaction_event.evse->id;
        mv_provides.auth_validator.publish_validate_result_update(result_update);
    }
}

ocpp::v2::UnlockConnectorResponse GenericOcpp::cb_unlock_connector(std::int32_t evse_id, std::int32_t connector_id) {
    // FIXME: This needs to properly handle different connectors
    ocpp::v2::UnlockConnectorResponse response;
    if (evse_id > 0 && evse_id <= mv_requires.evse_manager.size()) {
        if (mv_requires.evse_manager.at(evse_id - 1)->call_force_unlock(connector_id)) {
            response.status = ocpp::v2::UnlockStatusEnum::Unlocked;
        } else {
            response.status = ocpp::v2::UnlockStatusEnum::UnlockFailed;
        }
    } else {
        response.status = ocpp::v2::UnlockStatusEnum::UnknownConnector;
    }
    return response;
}

bool GenericOcpp::cb_update_allowed_energy_transfer_modes(
    const std::vector<ocpp::v2::EnergyTransferModeEnum>& allowed_energy_transfer_modes,
    const ocpp::CiString<36>& transaction_id) {
    using namespace module::conversions;

    bool result{false};
    const auto evse_id = m_transaction_handler->get_evse_id(transaction_id);
    if (evse_id > 0 && evse_id <= mv_requires.evse_manager.size()) {
        auto& evse = mv_requires.evse_manager.at(evse_id - 1); // evse_id starts at 1 if valid

        if (evse != nullptr) {
            result = evse->call_update_allowed_energy_transfer_modes(
                         to_everest_allowed_energy_transfer_modes(allowed_energy_transfer_modes)) ==
                     types::evse_manager::UpdateAllowedEnergyTransferModesResult::Accepted;
        }
    }
    return result;
}

ocpp::v2::UpdateFirmwareResponse
GenericOcpp::cb_update_firmware_request(const ocpp::v2::UpdateFirmwareRequest& request) {
    using namespace module::conversions;

    auto req = to_everest_firmware_update_request(request);
    if (req.retries.has_value()) {
        // As defined in OCPP the initial attempt does not count as a retry
        // hence the + 1
        req.retries = req.retries.value() + 1;
    }
    const auto response = mv_requires.system.call_update_firmware(req);
    return to_ocpp_update_firmware_response(response);
}

ocpp::v2::SetNetworkProfileStatusEnum
GenericOcpp::cb_validate_network_profile(const ocpp::v2::NetworkConnectionProfile& network_connection_profile) {
    const auto ws_uri = ocpp::uri(network_connection_profile.ocppCsmsUrl.get());
    return (ws_uri.get_valid()) ? ocpp::v2::SetNetworkProfileStatusEnum::Accepted
                                : ocpp::v2::SetNetworkProfileStatusEnum::Rejected;
    // TODO(piet): Add further validation of the NetworkConnectionProfile
}

void GenericOcpp::cb_variable_monitor(const ocpp::v2::Component& component, const ocpp::v2::Variable& variable,
                                      const std::string& value) {
    using namespace module::conversions;

    MonitorListEntry entry{component, variable};
    bool publish;
    {
        std::lock_guard lock(m_member_mux);
        const auto it = m_monitor_list.find(entry);
        publish = it != m_monitor_list.end();
    }
    if (publish) {
        // monitor entry exists - publish
        types::ocpp::EventData event_data;
        event_data.component_variable.component = to_everest_component(component);
        event_data.component_variable.variable = to_everest_variable(variable);
        event_data.event_id = 0;
        event_data.timestamp = ocpp::DateTime();
        event_data.trigger = types::ocpp::EventTriggerEnum::Alerting;
        event_data.actual_value = value;
        event_data.event_notification_type = types::ocpp::EventNotificationType::CustomMonitor;
        mv_provides.ocpp_generic.publish_event_data(event_data);
    } else {
        EVLOG_warning << "cb_variable_monitor: unexpected variable: " << component.name << ':' << variable.name;
    }
}

void GenericOcpp::cb_variable_set(const ocpp::v2::SetVariableData& set_variable_data) {
    using namespace ocpp::v2;
    const auto& component = set_variable_data.component;
    const auto& name = set_variable_data.variable.name.get();

    if (component == ControllerComponents::TxCtrlr) {
        if (name == EV_CONNECTION_TIMEOUT_VAR_NAME) {
            try {
                auto ev_connection_timeout = std::stoi(set_variable_data.attributeValue.get());
                mv_requires.auth.call_set_connection_timeout(ev_connection_timeout);
            } catch (const std::exception& e) {
                EVLOG_error << "Could not parse EVConnectionTimeOut and did not set it in Auth module, error: "
                            << e.what();
            }
        } else if (name == TX_STOP_POINT_VAR_NAME) {
            const auto tx_stop_points = get_tx_start_stop_points(set_variable_data.attributeValue.get());
            if (tx_stop_points.empty()) {
                EVLOG_warning << "Could not set TxStartPoints";
            } else {
                m_transaction_handler->set_tx_stop_points(tx_stop_points);
            }
        } else if (name == TX_START_POINT_VAR_NAME) {
            const auto tx_start_points = get_tx_start_stop_points(set_variable_data.attributeValue.get());
            if (tx_start_points.empty()) {
                EVLOG_warning << "Could not set TxStartPoints";
            } else {
                m_transaction_handler->set_tx_start_points(tx_start_points);
            }
        }
    } else if (component == ControllerComponents::AuthCtrlr) {
        if (name == MASTER_PASS_GROUP_ID_VAR_NAME) {
            mv_requires.auth.call_set_master_pass_group_id(set_variable_data.attributeValue.get());
        }
    } else if (component == ControllerComponents::ISO15118Ctrlr) {
        if (name == PNC_ENABLED_VAR_NAME) {
            types::evse_manager::PlugAndChargeConfiguration pnc_config;
            pnc_config.pnc_enabled = ocpp::conversions::string_to_bool(set_variable_data.attributeValue.get());
            for (const auto& evse_manager : mv_requires.evse_manager) {
                evse_manager->call_set_plug_and_charge_configuration(pnc_config);
            }
        } else if (name == CENTRAL_CONTRACT_VALIDATION_ALLOWED_VAR_NAME) {
            types::evse_manager::PlugAndChargeConfiguration pnc_config;
            pnc_config.central_contract_validation_allowed =
                ocpp::conversions::string_to_bool(set_variable_data.attributeValue.get());
            for (const auto& evse_manager : mv_requires.evse_manager) {
                evse_manager->call_set_plug_and_charge_configuration(pnc_config);
            }
        } else if (name == CONTRACT_CERTIFICATE_INSTALLATION_ENABLED_VAR_NAME) {
            types::evse_manager::PlugAndChargeConfiguration pnc_config;
            pnc_config.contract_certificate_installation_enabled =
                ocpp::conversions::string_to_bool(set_variable_data.attributeValue.get());
            for (const auto& evse_manager : mv_requires.evse_manager) {
                evse_manager->call_set_plug_and_charge_configuration(pnc_config);
            }
        }
    } else if ((component.name == "DCDERCtrlr" || component.name == "ACDERCtrlr") and name == "Enabled" and
               set_variable_data.attributeType.value_or(AttributeEnum::Actual) == AttributeEnum::Actual) {
        if (not component.evse.has_value()) {
            EVLOG_warning << "DER Enabled write without an EVSE id; ignoring";
            return;
        }
        const auto evse_id = component.evse.value().id;
        const auto enabled = set_variable_data.attributeValue.get() == "true";
        {
            auto state_handle = m_grid_support_state.handle();
            state_handle->set_enabled(evse_id, enabled);
        }
        push_active_directive_sets();
    }
}

void GenericOcpp::cb_waiting_for_external_ready(std::int32_t evse_id, bool ready) {
    if (ready) {
        mv_evse_ready_map.handle()->at(evse_id) = true;
        mv_evse_ready_map.notify_one();
    }
}

bool GenericOcpp::map_error(const std::string& error, std::string& updated_error) {
    bool result{false};
    if (const auto it = mv_mrec_error_map.find(error); it != mv_mrec_error_map.end()) {
        updated_error = it->second;
        result = true;
    } else {
        updated_error = error;
    }
    return result;
}

void GenericOcpp::transaction_add(std::int32_t evse_id,
                                  const std::shared_ptr<module::TransactionData>& transaction_data) {
    if (m_transaction_handler) {
        m_transaction_handler->add_transaction_data(evse_id, transaction_data);
    }
}

std::shared_ptr<module::TransactionData> GenericOcpp::transaction_data(std::int32_t evse_id) {
    std::shared_ptr<module::TransactionData> result{};
    if (m_transaction_handler) {
        result = m_transaction_handler->get_transaction_data(evse_id);
    }
    return result;
}

module::TxEventEffect GenericOcpp::transaction_event(std::int32_t evse_id, module::TxEvent tx_event) {
    auto result{module::TxEventEffect::NONE};
    if (m_transaction_handler) {
        result = m_transaction_handler->submit_event(evse_id, tx_event);
    }
    return result;
}

void GenericOcpp::transaction_reset(std::int32_t evse_id) {
    if (m_transaction_handler) {
        m_transaction_handler->reset_transaction_data(evse_id);
    }
}

void GenericOcpp::update_evcc_id_token(std::int32_t evse, ocpp::v2::IdToken& id_token) {
    auto evse_evcc_id_handle = mv_evse_evcc_id.handle();
    if (!evse_evcc_id_handle->at(evse).empty()) {
        const auto evcc_id = evse_evcc_id_handle->at(evse);

        if (mv_ocpp_protocol_version == ocpp::OcppProtocolVersion::v21) {
            auto info_vector = id_token.additionalInfo.has_value() ? id_token.additionalInfo.value()
                                                                   : std::vector<ocpp::v2::AdditionalInfo>{};
            if (!id_token.additionalInfo.has_value() or
                (id_token.additionalInfo.has_value() and
                 std::find_if(id_token.additionalInfo->cbegin(), id_token.additionalInfo->cend(),
                              [evcc_id](const ocpp::v2::AdditionalInfo& info) {
                                  return info.additionalIdToken.get() == evcc_id;
                              }) == id_token.additionalInfo->cend())) {
                ocpp::v2::AdditionalInfo info;
                info.additionalIdToken = evcc_id;
                info.type = "EVCCID";
                info_vector.push_back(info);
                id_token.additionalInfo = info_vector;
            }
        }
    }
}

// ----------------------------------------------------------------------------
// general

bool GenericOcpp::charging_schedules_timer_running() {
    return m_charging_schedules_timer.is_running();
}

void GenericOcpp::charging_schedules_timer_start() {
    const auto interval = mv_config.getCompositeScheduleIntervalS();
    if (interval > 0) {
        m_charging_schedules_timer.interval([this]() { cb_set_charging_profiles(); }, std::chrono::seconds(interval));
    }
}

void GenericOcpp::charging_schedules_timer_stop() {
    m_charging_schedules_timer.stop();
}

void GenericOcpp::shutdown() {
    // Order matters: set both gates before stopping the timer, then block on recompute_mutex to
    // wait out a recompute already inside mv_charge_point.
    mv_started.store(false);
    mv_shutting_down.store(true);
    charging_schedules_timer_stop();
    grid_support_heartbeat_timer_stop();
    std::lock_guard<std::mutex> lock(recompute_mutex);
}

bool GenericOcpp::ocpp_2_selected() const {
    const auto mode = mv_mode.load();
    return mode == GenericChargePointInterface::modes_t::ocpp_2_only ||
           mode == GenericChargePointInterface::modes_t::prefer_ocpp_2;
}

bool GenericOcpp::grid_support_active() const {
    return not mv_requires.grid_support.empty() and ocpp_2_selected();
}

void GenericOcpp::cb_der_active_directives(const std::vector<ocpp::v21::SetDERControlRequest>& active_controls) {
    if (not grid_support_active()) {
        EVLOG_warning << "Dropping DER active directives: grid_support is unwired or the active OCPP version is 1.6";
        return;
    }

    const auto received_at = ocpp::DateTime().to_rfc3339();
    auto directives = module::conversions::translate_active_controls(
        active_controls, [&received_at](const ocpp::v21::SetDERControlRequest& control) {
            return module::conversions::to_grid_support_directive(control, "ocpp", received_at);
        });

    {
        auto state_handle = m_grid_support_state.handle();
        state_handle->set_active_directives(std::move(directives));
    }
    push_active_directive_sets();
}

GenericOcpp::DerApplyResult GenericOcpp::apply_der_capability(std::int32_t evse_id,
                                                              const types::grid_support::DERCapability& capability) {
    DerApplyResult result;

    // Handle per access: set_variables/on_der_republish re-enter der_active_directives_callback (self-deadlock).
    std::optional<types::grid_support::DERCapability> previous;
    {
        auto state_handle = m_grid_support_state.handle();
        previous = state_handle->capability_for(evse_id);
        state_handle->set_capability(evse_id, capability);
    }

    const auto collect_rejected = [](const std::vector<ocpp::v2::SetVariableResult>& set_results) {
        std::string rejected_details;
        for (const auto& set_result : set_results) {
            if (set_result.attributeStatus != ocpp::v2::SetVariableStatusEnum::Accepted) {
                if (not rejected_details.empty()) {
                    rejected_details += ", ";
                }
                rejected_details +=
                    set_result.variable.name.get() + "=" +
                    ocpp::v2::conversions::set_variable_status_enum_to_string(set_result.attributeStatus);
            }
        }
        return rejected_details;
    };

    const auto reject = [&](const std::string& rejected_details) {
        if (previous.has_value()) {
            EVLOG_error << "Rejecting set_capability for EVSE " << evse_id
                        << ": device model rejected DER capability variables: " << rejected_details
                        << "; rolling back to last accepted capability";
            {
                auto state_handle = m_grid_support_state.handle();
                state_handle->set_capability(evse_id, *previous);
            }
            // Best-effort restore of the previous config variables. Enabled stays untouched: the EVSE
            // keeps running its previous accepted config.
            // Variables the rejected capability wrote but the previous one does not define keep their new values.
            const auto rollback_rejected = collect_rejected(mv_charge_point.set_variables(
                module::device_model::to_der_ctrlr_config_set_variables(evse_id, *previous), "internal"));
            if (not rollback_rejected.empty()) {
                EVLOG_warning << "DER capability rollback for EVSE " << evse_id
                              << " partially rejected: " << rollback_rejected;
            }
        } else {
            EVLOG_error << "Rejecting set_capability for EVSE " << evse_id
                        << ": device model rejected DER capability variables: " << rejected_details;
            {
                auto state_handle = m_grid_support_state.handle();
                state_handle->unregister(evse_id);
            }
            if (evse_id >= 1 and static_cast<std::size_t>(evse_id) <= mv_requires.evse_manager.size()) {
                // best-effort DER withdraw; a NoHlc result here is benign
                (void)mv_requires.evse_manager.at(evse_id - 1)->call_set_der_available(false);
            }
        }
        result.accepted = false;
        result.rejected_details = rejected_details;
    };

    // Config variables only (no Available/Enabled): a partial rejection leaves the component state untouched.
    const auto config_rejected = collect_rejected(mv_charge_point.set_variables(
        module::device_model::to_der_ctrlr_config_set_variables(evse_id, capability), "internal"));
    if (not config_rejected.empty()) {
        reject(config_rejected);
        return result;
    }

    mv_charge_point.on_der_republish_active_directives();

    if (evse_id >= 1 and static_cast<std::size_t>(evse_id) <= mv_requires.evse_manager.size()) {
        const auto der_result = mv_requires.evse_manager.at(evse_id - 1)->call_set_der_available(true);
        if (der_result != types::evse_manager::SetDerAvailableResult::Accepted) {
            EVLOG_warning << "EvseManager did not accept DER availability for EVSE " << evse_id << ": "
                          << types::evse_manager::set_der_available_result_to_string(der_result);
        }
    }

    std::vector<types::grid_support::GridAlarm> pending_alarms;
    {
        auto state_handle = m_grid_support_state.handle();
        state_handle->set_alarms_live();
        pending_alarms = state_handle->take_pending_alarms();
    }
    for (const auto& alarm : pending_alarms) {
        try {
            mv_charge_point.on_der_alarm(module::conversions::to_ocpp_notify_der_alarm(alarm));
        } catch (const std::invalid_argument& e) {
            EVLOG_error << "Buffered DER alarm dropped: " << e.what();
        }
    }

    result.accepted = true;
    return result;
}

void GenericOcpp::flush_pending_grid_support() {
    // Persisted DERCtrlr Enabled for evse_id (AC then DC controller), or nullopt if it has no DER controller.
    const auto read_persisted_der_enabled = [this](std::int32_t evse_id) -> std::optional<bool> {
        for (const auto& component_variable : {ocpp::v2::DERComponentVariables::get_ac_component_variable(
                                                   evse_id, ocpp::v2::DERComponentVariables::Enabled),
                                               ocpp::v2::DERComponentVariables::get_dc_component_variable(
                                                   evse_id, ocpp::v2::DERComponentVariables::Enabled)}) {
            ocpp::v2::GetVariableData data;
            data.component = component_variable.component;
            data.variable = component_variable.variable.value();
            data.attributeType = ocpp::v2::AttributeEnum::Actual;
            const auto results = mv_charge_point.get_variables({data});
            if (not results.empty() and results.front().attributeStatus == ocpp::v2::GetVariableStatusEnum::Accepted and
                results.front().attributeValue.has_value()) {
                return ocpp::conversions::string_to_bool(results.front().attributeValue.value().get());
            }
        }
        return std::nullopt;
    };

    std::vector<std::pair<std::int32_t, types::grid_support::DERCapability>> pending;
    {
        auto state_handle = m_grid_support_state.handle();
        state_handle->set_capabilities_live();
        pending = state_handle->take_pending_capabilities();
        for (const auto& [evse_id, capability] : pending) {
            const auto enabled = read_persisted_der_enabled(evse_id);
            if (enabled.has_value()) {
                state_handle->set_enabled(evse_id, enabled.value());
            }
        }
    }
    for (const auto& [pending_evse_id, pending_capability] : pending) {
        const auto apply_result = apply_der_capability(pending_evse_id, pending_capability);
        if (not apply_result.accepted) {
            EVLOG_error << "Buffered DER capability for EVSE " << pending_evse_id
                        << " rejected on flush (consumer already received Accepted, not re-notified): "
                        << apply_result.rejected_details;
        }
    }
}

void GenericOcpp::push_active_directive_sets() {
    if (not grid_support_active()) {
        return;
    }

    std::vector<types::grid_support::ActiveDirectiveSet> snapshots;
    {
        auto state_handle = m_grid_support_state.handle();
        const auto evses = state_handle->registered_evses();
        snapshots.reserve(evses.size());
        for (const auto evse_id : evses) {
            snapshots.push_back(state_handle->build_active_set(evse_id));
        }
    }
    for (const auto& snapshot : snapshots) {
        auto* const target = grid_support_for_evse(snapshot.evse_id);
        if (target == nullptr) {
            continue;
        }
        const auto response = target->call_set_active_directives(snapshot);
        if (not response.accepted) {
            EVLOG_warning << "DER device rejected active directives for EVSE " << snapshot.evse_id
                          << (response.reason.has_value() ? ": " + response.reason.value() : "");
        }
    }
}

void GenericOcpp::init_grid_support_routing(const std::map<std::int32_t, std::int32_t>& evse_connector_structure) {
    for (const auto& grid_support : mv_requires.grid_support) {
        const auto mapping = grid_support->get_mapping();
        if (not mapping.has_value()) {
            EVLOG_error << "grid_support connection on module " << grid_support->module_id
                        << " has no evse mapping; excluding it from DER routing";
            continue;
        }
        const auto [it, inserted] = m_grid_support_by_evse.emplace(mapping->evse, grid_support.get());
        if (not inserted) {
            EVLOG_error << "grid_support connection on module " << grid_support->module_id << " maps evse "
                        << mapping->evse << " already served by another connection; keeping the first";
        }
    }

    for (const auto& [evse_id, connector_count] : evse_connector_structure) {
        if (m_grid_support_by_evse.find(evse_id) == m_grid_support_by_evse.end()) {
            m_everest_device_model_storage->disable_der(evse_id);
            EVLOG_info << "No grid_support connection for EVSE " << evse_id << ": DER controller disabled";
        }
    }
}

grid_supportIntf* GenericOcpp::grid_support_for_evse(std::int32_t evse_id) const {
    const auto it = m_grid_support_by_evse.find(evse_id);
    if (it != m_grid_support_by_evse.end()) {
        return it->second;
    }
    return nullptr;
}

void GenericOcpp::grid_support_heartbeat_timer_callback() {
    push_active_directive_sets();
}

void GenericOcpp::grid_support_heartbeat_timer_start() {
    if (mv_config.getGridSupportHeartbeatS() > 0) {
        const auto grid_support_heartbeat_callback = [this]() { grid_support_heartbeat_timer_callback(); };
        m_grid_support_heartbeat_timer.interval(grid_support_heartbeat_callback,
                                                std::chrono::seconds(mv_config.getGridSupportHeartbeatS()));
    }
}

void GenericOcpp::grid_support_heartbeat_timer_stop() {
    m_grid_support_heartbeat_timer.stop();
}

void GenericOcpp::on_grid_support_capability(const types::grid_support::EVSECapability& evse_capability) {
    const auto evse_id = evse_capability.evse_id;
    const auto& capability = evse_capability.capability;

    if (not capability.ac.has_value() and not capability.dc.has_value()) {
        EVLOG_warning << "Ignoring DER capability for EVSE " << evse_id << ": no inverter class declared";
        return;
    }

    // Route on the flag under the handle (charge_point read races flush); deliver only after releasing it.
    {
        auto state_handle = m_grid_support_state.handle();
        if (not state_handle->capabilities_live()) {
            state_handle->buffer_pending_capability(evse_id, capability);
            return;
        }
    }

    const auto apply_result = apply_der_capability(evse_id, capability);
    if (not apply_result.accepted) {
        EVLOG_error << "Rejecting DER capability for EVSE " << evse_id
                    << ": device model rejected DER capability variables: " << apply_result.rejected_details;
    }
}

void GenericOcpp::on_grid_support_alarm(const types::grid_support::GridAlarm& alarm) {
    // Same rule as on_grid_support_capability; alarms_live implies charge_point exists.
    {
        auto state_handle = m_grid_support_state.handle();
        if (not state_handle->alarms_live()) {
            state_handle->buffer_pending_alarm(alarm);
            return;
        }
    }

    try {
        mv_charge_point.on_der_alarm(module::conversions::to_ocpp_notify_der_alarm(alarm));
    } catch (const std::invalid_argument& e) {
        EVLOG_warning << "Rejecting DER alarm: " << e.what();
    }
}

std::optional<types::energy::ScheduleReqEntry>
GenericOcpp::create_limits_entry(const std::string& timestamp, const ocpp::v2::EnhancedChargingSchedulePeriod& period,
                                 ocpp::v2::ChargingRateUnitEnum unit) {
    std::optional<types::energy::ScheduleReqEntry> result;

    if (period.limit.has_value()) {
        types::energy::ScheduleReqEntry entry;
        entry.timestamp = timestamp;

        const auto source_ext_limit = mv_info.id + "/OCPP_set_external_limits";

        types::energy::LimitsReq limits_req;
        if (unit == ocpp::v2::ChargingRateUnitEnum::A) {
            limits_req.ac_max_current_A = {period.limit.value(), source_ext_limit};
            if (period.numberPhases.has_value()) {
                limits_req.ac_max_phase_count = {period.numberPhases.value(), source_ext_limit};
            }
        } else {
            limits_req.total_power_W = {period.limit.value(), source_ext_limit};
        }

        entry.limits_to_leaves = limits_req;
        result = std::move(entry);
    }

    return result;
};

std::optional<types::energy::ScheduleSetpointEntry>
GenericOcpp::create_setpoint_entry(std::int32_t setpoint_priority, const std::string& timestamp,
                                   const ocpp::v2::EnhancedChargingSchedulePeriod& period,
                                   ocpp::v2::ChargingRateUnitEnum unit) {
    std::optional<types::energy::ScheduleSetpointEntry> result;
    const bool has_basic_setpoint = period.setpoint.has_value();
    const bool has_freq_table = period.v2xFreqWattCurve.has_value() && !period.v2xFreqWattCurve->empty();

    if (has_basic_setpoint || has_freq_table) {
        types::energy::ScheduleSetpointEntry entry;
        types::energy::SetpointType setpoint;
        setpoint.source = SETPOINT_SOURCE;
        setpoint.priority = setpoint_priority;
        entry.timestamp = timestamp;

        if (has_basic_setpoint) {
            if (unit == ocpp::v2::ChargingRateUnitEnum::A) {
                setpoint.ac_current_A = period.setpoint;
            } else {
                setpoint.total_power_W = period.setpoint;
            }
        }

        if (has_freq_table) {
            std::vector<types::energy::FrequencyWattPoint> frequency_table;
            for (const auto& point : period.v2xFreqWattCurve.value()) {
                types::energy::FrequencyWattPoint freq_point{};
                freq_point.frequency_Hz = point.frequency;
                freq_point.total_power_W = point.power;
                frequency_table.push_back(freq_point);
            }
            setpoint.frequency_table = std::move(frequency_table);
        }

        entry.setpoint = std::move(setpoint);
        result = std::move(entry);
    }

    return result;
}

std::pair<GenericChargePointInterface::ConnectorStructure, GenericChargePointInterface::ConnectorStructureV16>
GenericOcpp::get_connector_structure() {
    GenericChargePointInterface::ConnectorStructure evse_connector_structure;
    GenericChargePointInterface::ConnectorStructureV16 connector_mapping;

    std::int32_t evse_id = 1;
    std::int32_t v16_connector_id = 1; // this represents the OCPP connector id

    for (const auto& evse : mv_requires.evse_manager) {
        auto evse_info = evse->call_get_evse();
        std::int32_t num_connectors = evse_info.connectors.size();
        std::map<int32_t, int32_t> v16_connector_map;

        if (evse_info.id != evse_id) {
            throw std::runtime_error("Configured evse_id(s) must start with 1 counting upwards");
        }
        if (num_connectors > 0) {
            std::int32_t connector_id = 1;
            for (const auto& connector : evse_info.connectors) {
                if (connector.id != connector_id) {
                    throw std::runtime_error("Configured connector_id(s) must start with 1 counting upwards");
                }
                v16_connector_map[connector_id++] = v16_connector_id++;
            }
        } else {
            v16_connector_map[1] = v16_connector_id++;
            num_connectors = 1;
        }

        evse_connector_structure[evse_id] = num_connectors;
        connector_mapping[evse_id] = std::move(v16_connector_map);
        evse_id++;
    }

    return {evse_connector_structure, connector_mapping};
}

void GenericOcpp::process_session_event(std::int32_t evse_id, const types::evse_manager::SessionEvent& session_event) {
    const auto connector_id = session_event.connector_id.value_or(1);
    std::lock_guard lock(m_session_event_mutex);
    try {
        switch (session_event.event) {
        case types::evse_manager::SessionEventEnum::Authorized:
            mv_charge_point.on_event_authorised(evse_id, connector_id, session_event);
            break;
        case types::evse_manager::SessionEventEnum::ChargingPausedEV:
            mv_charge_point.on_event_charging_paused_ev(evse_id, connector_id, session_event);
            break;
        case types::evse_manager::SessionEventEnum::ChargingPausedEVSE:
            mv_charge_point.on_event_charging_paused_evse(evse_id, connector_id, session_event);
            break;
        case types::evse_manager::SessionEventEnum::ChargingStarted:
            mv_charge_point.on_event_charging_started(evse_id, connector_id, session_event);
            break;
        case types::evse_manager::SessionEventEnum::Deauthorized:
            mv_charge_point.on_event_deauthorised(evse_id, connector_id, session_event);
            break;
        case types::evse_manager::SessionEventEnum::Disabled:
            mv_charge_point.on_event_disabled(evse_id, connector_id, session_event);
            break;
        case types::evse_manager::SessionEventEnum::Enabled:
            mv_charge_point.on_event_enabled(evse_id, connector_id, session_event);
            break;
        case types::evse_manager::SessionEventEnum::PluginTimeout:
            mv_charge_point.on_event_plugin_timeout(evse_id, connector_id, session_event);
            break;
        case types::evse_manager::SessionEventEnum::ReservationEnd:
            mv_charge_point.on_event_reservation_end(evse_id, connector_id, session_event);
            break;
        case types::evse_manager::SessionEventEnum::ReservationStart:
            mv_charge_point.on_event_reservation_start(evse_id, connector_id, session_event);
            break;
        case types::evse_manager::SessionEventEnum::SessionFinished:
            mv_evse_soc_map.handle()->at(evse_id).reset();
            mv_evse_evcc_id.handle()->at(evse_id) = "";
            mv_charge_point.on_event_session_finished(evse_id, connector_id, session_event);
            m_everest_device_model_storage->update_connected_ev_available(evse_id, false);
            break;
        case types::evse_manager::SessionEventEnum::SessionResumed:
            mv_charge_point.on_event_session_resumed(evse_id, connector_id, session_event);
            break;
        case types::evse_manager::SessionEventEnum::SessionStarted:
            if (mv_charge_point.on_event_session_started(evse_id, connector_id, session_event)) {
                m_everest_device_model_storage->update_connected_ev_available(evse_id, true);
            }
            break;
        case types::evse_manager::SessionEventEnum::SwitchingPhases:
            mv_charge_point.on_event_switching_phases(evse_id, connector_id, session_event);
            break;
        case types::evse_manager::SessionEventEnum::TransactionFinished:
            mv_charge_point.on_event_transaction_finished(evse_id, connector_id, session_event);
            break;
        case types::evse_manager::SessionEventEnum::TransactionStarted:
            if (mv_charge_point.on_event_transaction_started(evse_id, connector_id, session_event)) {
                m_everest_device_model_storage->update_connected_ev_available(evse_id, true);
            }
            break;

        // explicitly ignore the following session events for now
        // TODO(kai): implement
        case types::evse_manager::SessionEventEnum::AuthRequired:
        case types::evse_manager::SessionEventEnum::ChargingFinished:
        case types::evse_manager::SessionEventEnum::PrepareCharging:
        case types::evse_manager::SessionEventEnum::StoppingCharging:
            break;
        }

        // process authorised event which will inititate a TransactionEvent(Updated) message in case the token has not
        // yet been authorised by the CSMS
        auto authorized_id_token = get_authorised_id_token(session_event);
        if (authorized_id_token.has_value()) {
            update_evcc_id_token(evse_id, authorized_id_token.value());
            mv_charge_point.on_authorized(evse_id, connector_id, authorized_id_token.value());
        }
    } catch (const std::exception& e) {
        EVLOG_error << "Failed to process session event " << session_event.event << " on evse_id " << evse_id
                    << ", connector_id " << connector_id << " (session " << session_event.uuid << "): " << e.what();
    }
}

void GenericOcpp::publish_charging_schedules(
    const std::vector<ocpp::v2::EnhancedCompositeSchedule>& composite_schedules) {
    using namespace module::conversions;

    const auto everest_schedules = to_everest_charging_schedules(composite_schedules);
    mv_provides.ocpp_generic.publish_charging_schedules(everest_schedules);
}

void GenericOcpp::set_external_limits(const std::vector<ocpp::v2::EnhancedCompositeSchedule>& composite_schedules) {
    const auto start_time = ocpp::DateTime();

    auto to_timestamp = [&](int seconds_offset) {
        return ocpp::DateTime(start_time.to_time_point() + std::chrono::seconds(seconds_offset)).to_rfc3339();
    };

    std::int32_t setpoint_priority = LOWEST_SETPOINT_PRIORITY;
    const auto resp = mv_charge_point.get_setpoint_priority();

    if (resp) {
        setpoint_priority = resp.value() == "CSMS" ? HIGHEST_SETPOINT_PRIORITY : LOWEST_SETPOINT_PRIORITY;
    }

    for (const auto& composite_schedule : composite_schedules) {
        auto evse_id = composite_schedule.evseId;
        if (not external_energy_limits::is_evse_sink_configured(mv_requires.evse_energy_sink, evse_id)) {
            EVLOG_warning << "Can not apply external limits! No evse energy sink configured for evse_id: " << evse_id;
            continue;
        }

        types::energy::ExternalLimits limits;
        std::vector<types::energy::ScheduleReqEntry> schedule_import;
        std::vector<types::energy::ScheduleSetpointEntry> schedule_setpoints;

        const auto& unit = composite_schedule.chargingRateUnit;

        for (const auto& period : composite_schedule.chargingSchedulePeriod) {
            const auto timestamp = to_timestamp(period.startPeriod);

            if (auto setpoint_entry = create_setpoint_entry(setpoint_priority, timestamp, period, unit)) {
                schedule_setpoints.push_back(*setpoint_entry);
            }

            if (auto limits_entry = create_limits_entry(timestamp, period, unit)) {
                schedule_import.push_back(*limits_entry);
            }
        }

        limits.schedule_import = std::move(schedule_import);
        limits.schedule_setpoints = std::move(schedule_setpoints);

        auto& evse_sink = external_energy_limits::get_evse_sink_by_evse_id(mv_requires.evse_energy_sink, evse_id);
        evse_sink.call_set_external_limits(limits);
    }
}

void GenericOcpp::wait_all_ready() {
    auto ready_handle = mv_evse_ready_map.handle();
    ready_handle.wait([this, &ready_handle]() {
        for (const auto& [evse, ready] : *ready_handle) {
            if (!ready) {
                return false;
            }
        }
        EVLOG_info << "All EVSE ready. Starting OCPP service";
        return true;
    });
}

} // namespace ocpp_multi
