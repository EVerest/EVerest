// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <everest/util/async/monitor.hpp>

#include <generated/interfaces/evse_manager/Interface.hpp>
#include <generated/interfaces/iso15118_extensions/Interface.hpp>
#include <generated/types/evse_board_support.hpp>
#include <generated/types/evse_manager.hpp>
#include <generated/types/iso15118.hpp>

#include <filesystem>
#include <optional>

#include <generated/types/grid_support.hpp>

#include <ocpp/v2/device_model_registry_interface.hpp>
#include <ocpp/v2/device_model_storage_interface.hpp>
#include <ocpp/v2/init_device_model_db.hpp>

namespace ocpp_module_common::device_model {

/// \brief Which DER controller component an EVSE is provisioned with.
enum class DerCtrlrComponent {
    None, ///< no DER controller component for this EVSE
    Dc,   ///< DCDERCtrlr
    Ac,   ///< ACDERCtrlr
};

/// \brief Whether an ISO 15118 session can ever occur on this EVSE.
/// \details True when any connector reports hlc_capable. Asking "any" rather than "all" because the
///          question is whether the EVSE speaks ISO 15118 at all: on a mixed EVSE the session runs on
///          whichever plug can carry it, and reporting false there would misinform the CSMS.
bool evse_hlc_capable(const std::vector<types::evse_manager::Connector>& connectors);

/// \brief Decides an EVSE's DER controller component from static facts.
///
/// \param der_wired Whether a grid_support provider is wired to this EVSE.
/// \param connectors The EVSE's connectors as reported by evse_manager, each carrying its own charge mode.
///
/// An EVSE gets at most one DER controller, so the connectors' charge modes are collapsed to one answer:
/// every connector AC yields Ac, every connector DC yields Dc. Fails closed everywhere else, which is an
/// unwired EVSE and an EVSE whose connectors disagree - OCPP cannot express two controllers, and
/// provisioning the wrong one is worse than provisioning none.
///
/// Ac additionally requires that some connector is hlc_capable, because AC DER exists only as an
/// ISO 15118-20 relay. Dc does not check it: EvseManager refuses to start a DC EVSE without HLC, so a DC
/// connector reporting no HLC could not have booted.
///
/// Deliberately not derived from an EVSE's supported energy transfer modes, which are published
/// asynchronously and are not waited on before the device model is built.
DerCtrlrComponent der_ctrlr_component(bool der_wired, const std::vector<types::evse_manager::Connector>& connectors);

/// \brief Decides the DER controller component for every EVSE of the station.
///
/// \param evses The EVSEs as reported by their evse_manager.
/// \param der_wired_evse_ids The EVSE ids with a wired grid_support connection.
/// \param with_der_components False on a station that provisions no DER at all, which yields an empty map.
///
/// With with_der_components set, logs the reason for every EVSE that resolves to None and reports as an
/// error every wired EVSE id that matches no served EVSE, whose connection yields no entry and is inert.
/// Logs nothing otherwise.
std::map<int32_t, DerCtrlrComponent> decide_der_ctrlr_components(const std::vector<types::evse_manager::Evse>& evses,
                                                                 const std::set<int32_t>& der_wired_evse_ids,
                                                                 bool with_der_components);

/// \brief Builds the DER controller component config for a single EVSE.
///
/// The component is provisioned with Available "true" (ReadOnly, marking static presence) and Enabled "true"
/// (ReadWrite, the CSMS runtime control) with empty ModesSupported.
///
/// \returns The (ComponentKey, variables) pair for a DCDERCtrlr/ACDERCtrlr component, or std::nullopt for
///          DerCtrlrComponent::None.
std::optional<std::pair<ocpp::v2::ComponentKey, std::vector<ocpp::v2::DeviceModelVariable>>>
build_der_ctrlr_component_config(int32_t evse_id, DerCtrlrComponent component);

/// \brief Joins the OCPP EnergyTransferModeEnum names of \p modes with ',', each name once in first-seen order.
std::string
supported_energy_transfer_modes_vector_to_string(const std::vector<types::iso15118::EnergyTransferMode>& modes);

/// \brief Assembles the DER controller and ISO 15118 component configs of the station.
///
/// \param evses The EVSEs as reported by their evse_manager.
/// \param der_wired_evse_ids The EVSE ids with a wired grid_support connection.
/// \param with_der_components False on a station that provisions no DER at all, which gates the DER
///        controllers only: the ISO 15118 components are provisioned regardless.
/// \param iso_extension_evse_ids The EVSE ids an iso15118_extensions connection is mapped to.
/// \param evse_service_renegotiation_supported Per-EVSE ServiceRenegotiationSupport as reported by the
///        extension. A missing entry reads as false.
///
/// Decides with decide_der_ctrlr_components, then assembles. An EVSE resolving to DerCtrlrComponent::None
/// contributes no DER entry, unlike decide_der_ctrlr_components which keeps it for the clearing pass.
///
/// \returns The (ComponentKey, variables) entries, keyed by ACDERCtrlr/DCDERCtrlr resp. ISO15118Ctrlr
///          component key with evse_id set. Empty when nothing resolves and no extension is mapped, or maps
///          only to EVSEs this station does not serve.
std::map<ocpp::v2::ComponentKey, std::vector<ocpp::v2::DeviceModelVariable>>
build_der_component_configs(const std::vector<types::evse_manager::Evse>& evses,
                            const std::set<int32_t>& der_wired_evse_ids, bool with_der_components,
                            const std::vector<int32_t>& iso_extension_evse_ids,
                            const std::map<int32_t, bool>& evse_service_renegotiation_supported);

/// \brief Assembles the same component configs from an existing DER decision.
///
/// \param der_ctrlr_components The per-EVSE decision from decide_der_ctrlr_components.
/// \param evses The EVSEs as reported by their evse_manager, read for the ISO 15118 components.
/// \param iso_extension_evse_ids The EVSE ids an iso15118_extensions connection is mapped to.
/// \param evse_service_renegotiation_supported Per-EVSE ServiceRenegotiationSupport as reported by the
///        extension. A missing entry reads as false.
///
/// For a caller that already holds the decision, so deciding (and its logging) happens only once. The
/// whole assembly lives here and the deciding overload delegates to it.
///
/// \returns The (ComponentKey, variables) entries, keyed by ACDERCtrlr/DCDERCtrlr resp. ISO15118Ctrlr
///          component key with evse_id set. Empty when nothing resolves and no extension is mapped, or maps
///          only to EVSEs this station does not serve.
std::map<ocpp::v2::ComponentKey, std::vector<ocpp::v2::DeviceModelVariable>>
build_der_component_configs(const std::map<int32_t, DerCtrlrComponent>& der_ctrlr_components,
                            const std::vector<types::evse_manager::Evse>& evses,
                            const std::vector<int32_t>& iso_extension_evse_ids,
                            const std::map<int32_t, bool>& evse_service_renegotiation_supported);

/// \brief Builds the device-model SetVariableData vector that configures (but does not enable) the DER
///        controller for a given DER \p capability on EVSE \p evse_id.
/// \details Emits ModesSupported and, on the DC path (capability.dc set), best-effort nameplate scalars and
///          inverter strings. The nameplate scalars exist only on the DC component; no ACDERCtrlr nameplate
///          variables exist. This writes config variables only and never enables the component.
std::vector<ocpp::v2::SetVariableData>
to_der_ctrlr_config_set_variables(int32_t evse_id, const types::grid_support::DERCapability& capability);

/// \brief Builds the components that EVerest provides at runtime: EVSE, Connector, V2XChargingCtrlr,
/// ISO15118Ctrlr, ConnectedEV and the DER controllers
std::map<ocpp::v2::ComponentKey, std::vector<ocpp::v2::DeviceModelVariable>> build_runtime_component_configs(
    const std::vector<types::evse_manager::Evse>& evses,
    const std::map<int32_t, types::evse_board_support::HardwareCapabilities>& evse_hardware_capabilities_map,
    const std::map<int32_t, std::vector<types::iso15118::EnergyTransferMode>>& evse_supported_energy_transfers,
    const std::map<int32_t, bool>& evse_service_renegotiation_supported, bool with_der_components,
    const std::set<int32_t>& der_wired_evse_ids, const std::vector<int32_t>& iso_extension_evse_ids);

/// \brief Converts \p component_configs into a registration. Every variable supports monitoring except WriteOnly
/// ones, and the Enabled attribute of the DER controllers is persistent.
std::vector<ocpp::v2::RegisteredComponent> to_registered_components(
    const std::map<ocpp::v2::ComponentKey, std::vector<ocpp::v2::DeviceModelVariable>>& component_configs);

/// \brief Stores \p values in \p ocpp_storage, except those it holds a value for already
/// \return false if \p ocpp_storage did not accept one of them
bool import_stored_values(const std::vector<ocpp::v2::StoredVariableValue>& values,
                          ocpp::v2::DeviceModelStorageInterface& ocpp_storage);

/// \brief The DER Enabled values that the CSMS set while the DER controllers were stored in the EVerest device model
/// database at \p everest_db_path
std::vector<ocpp::v2::StoredVariableValue> read_legacy_der_enabled_values(const std::filesystem::path& everest_db_path);

/// \brief Takes over the EVerest device model database that earlier versions kept at \p everest_db_path: the DER
/// Enabled values the CSMS set are imported into \p ocpp_storage, then the file is renamed to \c *.migrated. The
/// other values it holds are not needed, the components EVerest provides are registered at runtime and the module
/// configurations are read from the configuration service. Does nothing when the file does not exist, so it runs once.
/// A path that is not a regular file, or that names the device model database at \p device_model_db_path, is left
/// alone. A file that cannot be read is renamed without importing anything. A value \p ocpp_storage does not accept
/// keeps the file, so the next start repeats the import.
void migrate_everest_device_model_database(const std::filesystem::path& everest_db_path,
                                           const std::filesystem::path& device_model_db_path,
                                           ocpp::v2::DeviceModelStorageInterface& ocpp_storage);

/// \brief Registers the components that EVerest provides with the device model under the source \c EVEREST and
/// reports their runtime state to it
class EverestDeviceModel {
public:
    EverestDeviceModel(
        const std::vector<std::unique_ptr<evse_managerIntf>>& r_evse_manager,
        const std::vector<std::unique_ptr<iso15118_extensionsIntf>>& r_extensions_15118,
        const std::map<int32_t, types::evse_board_support::HardwareCapabilities>& evse_hardware_capabilities_map,
        const std::map<int32_t, std::vector<types::iso15118::EnergyTransferMode>>& evse_supported_energy_transfers,
        const std::map<int32_t, bool>& evse_service_renegotiation_supported, bool with_der_components,
        const std::set<int32_t>& der_wired_evse_ids);

    /// \brief Registers the components with \p registry, which receives the later changes. \p registry must stay
    /// valid until this object is destroyed or registers with another registry.
    ocpp::v2::ComponentRegistrationResult register_with(ocpp::v2::DeviceModelRegistryInterface& registry);

    /// \brief Stops reporting to the registry. Must be called before the registry is destroyed.
    void detach();

    /// \brief The registered components with their current values
    std::vector<ocpp::v2::RegisteredComponent> get_components();

    void update_power(int32_t evse_id, float total_power_active_import);
    void update_connected_ev_available(int32_t evse_id, bool connected);
    void update_connected_ev_vehicle_id(int32_t evse_id, const std::string& vehicle_id);

    /// \brief Reports the DER controller values of EVSE \p evse_id that \p capability determines, see
    /// to_der_ctrlr_config_set_variables.
    /// \returns One result per variable. A variable the EVSE's controller does not have is UnknownComponent or
    ///          UnknownVariable.
    std::vector<ocpp::v2::SetVariableResult>
    update_der_capability(int32_t evse_id, const types::grid_support::DERCapability& capability);

private:
    struct State {
        ocpp::v2::DeviceModelRegistryInterface* registry{nullptr};
        std::vector<ocpp::v2::RegisteredComponent> components;
        std::size_t reports_in_progress{0};
    };
    everest::lib::util::monitor<State> state;

    /// \brief Keeps \p value as the current value of the variable and reports it to the registry
    ocpp::v2::SetVariableStatusEnum set_value(const ocpp::v2::Component& component, const ocpp::v2::Variable& variable,
                                              const std::string& value);
    void update_connected_ev_information(const ocpp::v2::Component& connected_ev_component,
                                         const types::iso15118::EvInformation& ev_information);
};

} // namespace ocpp_module_common::device_model
