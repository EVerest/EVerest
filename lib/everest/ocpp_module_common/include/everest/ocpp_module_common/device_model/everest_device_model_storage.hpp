// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <mutex>

#include <generated/interfaces/evse_manager/Interface.hpp>
#include <generated/interfaces/iso15118_extensions/Interface.hpp>
#include <generated/types/evse_board_support.hpp>
#include <generated/types/grid_support.hpp>
#include <generated/types/iso15118.hpp>
#include <generated/types/powermeter.hpp>

#include <ocpp/v2/device_model_storage_interface.hpp>
#include <ocpp/v2/device_model_storage_sqlite.hpp>
#include <ocpp/v2/init_device_model_db.hpp>
#include <utils/config_service.hpp>

namespace ocpp_module_common::device_model {

/// \brief Builds the DER controller component config for a single EVSE from its supported energy transfer modes.
///
/// DER-capable if it advertises a DER/bidirectional mode: AC_DER_IEC/AC_DER_SAE/AC_BPT_DER for AC, DC_BPT/DC_ACDP_BPT
/// for DC. AC vs DC is chosen from the presence of any DC_* mode. The component is provisioned with Available "true"
/// (ReadOnly, marking static presence) and Enabled "true" (ReadWrite, the CSMS runtime control) with empty
/// ModesSupported.
///
/// \returns The (ComponentKey, variables) pair for a DCDERCtrlr/ACDERCtrlr component, or std::nullopt if
///          the EVSE is not DER-capable.
std::optional<std::pair<ocpp::v2::ComponentKey, std::vector<ocpp::v2::DeviceModelVariable>>>
build_der_ctrlr_component_config(
    int32_t evse_id, const std::vector<types::iso15118::EnergyTransferMode>& supported_energy_transfer_modes);

/// \brief Builds the device-model SetVariableData vector that configures (but does not enable) the DER
///        controller for a given DER \p capability on EVSE \p evse_id.
/// \details Emits ModesSupported and, on the DC path (capability.dc set), best-effort nameplate scalars and
///          inverter strings. The nameplate scalars exist only on the DC component; no ACDERCtrlr nameplate
///          variables exist. This writes config variables only and never enables the component.
std::vector<ocpp::v2::SetVariableData>
to_der_ctrlr_config_set_variables(int32_t evse_id, const types::grid_support::DERCapability& capability);

/// \brief Forces the DER controller (DCDERCtrlr and ACDERCtrlr) of \p evse_id to Available "false" and
///        Enabled "false" in \p storage.
/// \details Writes "false" to the Available and Enabled Actual attributes only when the current value is
///          "true"; absent components/variables and values already not "true" are skipped silently. The
///          only-clear-"true" guard preserves a CSMS-written Enabled "false" and its source across an
///          unwire/rewire cycle. Used at startup on an EVSE that no longer has a wired grid_support connection.
void disable_der_ctrlr(ocpp::v2::DeviceModelStorageInterface& storage, int32_t evse_id);

class EverestDeviceModelStorage : public ocpp::v2::DeviceModelStorageInterface {
public:
    EverestDeviceModelStorage(
        const std::vector<std::unique_ptr<evse_managerIntf>>& r_evse_manager,
        const std::vector<std::unique_ptr<iso15118_extensionsIntf>>& r_extensions_15118,
        const std::map<int32_t, types::evse_board_support::HardwareCapabilities>& evse_hardware_capabilities_map,
        const std::map<int32_t, std::vector<types::iso15118::EnergyTransferMode>>& evse_supported_energy_transfers,
        const std::map<int32_t, bool>& evse_service_renegotiation_supported, const std::filesystem::path& db_path,
        const std::filesystem::path& migration_files_path,
        std::shared_ptr<Everest::config::ConfigServiceClient> config_service_client);
    virtual ~EverestDeviceModelStorage() override = default;
    virtual ocpp::v2::DeviceModelMap get_device_model() override;
    virtual std::optional<ocpp::v2::VariableAttribute>
    get_variable_attribute(const ocpp::v2::Component& component_id, const ocpp::v2::Variable& variable_id,
                           const ocpp::v2::AttributeEnum& attribute_enum) override;
    virtual std::vector<ocpp::v2::VariableAttribute>
    get_variable_attributes(const ocpp::v2::Component& component_id, const ocpp::v2::Variable& variable_id,
                            const std::optional<ocpp::v2::AttributeEnum>& attribute_enum) override;
    virtual ocpp::v2::SetVariableStatusEnum set_variable_attribute_value(const ocpp::v2::Component& component_id,
                                                                         const ocpp::v2::Variable& variable_id,
                                                                         const ocpp::v2::AttributeEnum& attribute_enum,
                                                                         const std::string& value,
                                                                         const std::string& source) override;
    virtual std::optional<ocpp::v2::VariableMonitoringMeta>
    set_monitoring_data(const ocpp::v2::SetMonitoringData& data, const ocpp::v2::VariableMonitorType type) override;
    virtual bool update_monitoring_reference(const int32_t monitor_id, const std::string& reference_value) override;
    virtual std::vector<ocpp::v2::VariableMonitoringMeta>
    get_monitoring_data(const std::vector<ocpp::v2::MonitoringCriterionEnum>& criteria,
                        const ocpp::v2::Component& component_id, const ocpp::v2::Variable& variable_id) override;
    virtual ocpp::v2::ClearMonitoringStatusEnum clear_variable_monitor(int monitor_id, bool allow_protected) override;
    virtual int32_t clear_custom_variable_monitors() override;
    virtual void check_integrity() override;
    virtual bool create_network_configuration_slot_from_default_schema(std::int32_t new_slot) override;

    /// \brief Updates the actual value of the EVSE Power variable to the given \p total_power_active_import value
    void update_power(const int32_t evse_id, const float total_power_active_import);

    /// \bried Updates the Available variable for the ConnectedEV component
    void update_connected_ev_available(const int32_t evse_id, const bool connected);

    /// \bried Updates the VehicleId variable for the ConnectedEV component
    void update_connected_ev_vehicle_id(const int32_t evse_id, const std::string& vehicle_id);

    /// \brief Forces the DER controller (DC or AC) of \p evse_id to Available "false" and Enabled "false", if present.
    void disable_der(const int32_t evse_id);

private:
    const std::vector<std::unique_ptr<evse_managerIntf>>& r_evse_manager;
    const std::vector<std::unique_ptr<iso15118_extensionsIntf>>& r_extensions_15118;
    std::mutex device_model_mutex;
    std::unique_ptr<ocpp::v2::DeviceModelStorageSqlite> device_model_storage;
    std::set<ocpp::v2::ComponentVariable> stored_in_everest_config_service;
    std::shared_ptr<Everest::config::ConfigServiceClient> config_service_client;
    std::map<Everest::config::ModuleIdType, everest::config::ModuleConfigurationParameters> module_configs;
    std::map<std::string, ModuleTierMappings> mappings;

    void init_evse_components_and_variables(
        const std::map<int32_t, types::evse_board_support::HardwareCapabilities>& evse_hardware_capabilities_map,
        const std::map<int32_t, std::vector<types::iso15118::EnergyTransferMode>>& evse_supported_energy_transfers);
    void update_hw_capabilities(const ocpp::v2::Component& evse_component,
                                const types::evse_board_support::HardwareCapabilities& hw_capabilities);
    void update_supported_energy_transfers(
        const ocpp::v2::Component& evse_component,
        const std::vector<types::iso15118::EnergyTransferMode>& evse_supported_energy_transfers);
    void
    update_supported_operation_modes(const ocpp::v2::Component& evse_component,
                                     const std::vector<ocpp::v2::OperationModeEnum>& evse_supported_operation_modes);
    void update_service_renegotiation_supported(const ocpp::v2::Component& iso15118_component,
                                                const bool& service_renegotiation_supported);
    void update_connected_ev_information(const ocpp::v2::Component& connected_ev_component,
                                         const types::iso15118::EvInformation& ev_information);
    void init_everest_config();
};
} // namespace ocpp_module_common::device_model
