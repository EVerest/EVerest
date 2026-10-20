// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <algorithm>
#include <string_view>

#include <everest/logging.hpp>

#include <everest/database/sqlite/connection.hpp>
#include <everest/ocpp_module_common/conversions.hpp>
#include <everest/ocpp_module_common/device_model/definitions.hpp>
#include <everest/ocpp_module_common/device_model/everest_device_model.hpp>
#include <ocpp/v2/ctrlr_component_variables.hpp>
#include <ocpp/v2/init_device_model_db.hpp>
#include <ocpp/v2/ocpp_types.hpp>

using ocpp::v2::Component;
using ocpp::v2::DataEnum;
using ocpp::v2::EVSE;
using ocpp::v2::Variable;
using ocpp::v2::VariableAttribute;
using ocpp::v2::VariableCharacteristics;
using ocpp::v2::VariableMap;
using ocpp::v2::VariableMetaData;

static constexpr auto VARIABLE_SOURCE_EVEREST = "EVEREST";
static constexpr auto NUMBER_OF_CONNECTED_EV_PROTOCOLS = 20;

using ocpp::v2::ComponentKey;
using ocpp::v2::DbVariableAttribute;
using ocpp::v2::DeviceModelVariable;

namespace ocpp_module_common::device_model {
namespace {

Component get_evse_component(const int32_t evse_id) {
    Component component;
    component.name = "EVSE";
    component.evse = EvseDefinitions::get_evse(evse_id);
    return component;
}

Component get_connector_component(const int32_t evse_id, const int32_t connector_id) {
    Component component;
    component.name = "Connector";
    component.evse = EvseDefinitions::get_evse(evse_id, connector_id);
    return component;
}

Component get_v2x_component(const int32_t evse_id) {
    Component component;
    component.name = "V2XChargingCtrlr";
    component.evse = EvseDefinitions::get_evse(evse_id);
    return component;
}

Component get_iso15118_component(const int32_t evse_id) {
    Component component;
    component.name = "ISO15118Ctrlr";
    component.evse = EvseDefinitions::get_evse(evse_id);
    return component;
}

Component get_connected_ev_component(const int32_t evse_id) {
    Component component;
    component.name = "ConnectedEV";
    component.evse = EvseDefinitions::get_evse(evse_id);
    return component;
}

ComponentKey get_evse_component_key(const int32_t evse_id) {
    ComponentKey component;
    component.name = "EVSE";
    component.evse_id = evse_id;
    return component;
}

ComponentKey get_connector_component_key(const int32_t evse_id, const int32_t connector_id) {
    ComponentKey component;
    component.name = "Connector";
    component.evse_id = evse_id;
    component.connector_id = connector_id;
    return component;
}

ComponentKey get_v2x_component_key(const int32_t evse_id) {
    ComponentKey component;
    component.name = "V2XChargingCtrlr";
    component.evse_id = evse_id;
    return component;
}

ComponentKey get_iso15118_component_key(const int32_t evse_id) {
    ComponentKey component;
    component.name = "ISO15118Ctrlr";
    component.evse_id = evse_id;
    return component;
}

ComponentKey get_connected_ev_component_key(const int32_t evse_id) {
    ComponentKey component;
    component.name = "ConnectedEV";
    component.evse_id = evse_id;
    return component;
}

ComponentKey get_dc_der_ctrlr_component_key(const int32_t evse_id) {
    ComponentKey component;
    component.name = "DCDERCtrlr";
    component.evse_id = evse_id;
    return component;
}

ComponentKey get_ac_der_ctrlr_component_key(const int32_t evse_id) {
    ComponentKey component;
    component.name = "ACDERCtrlr";
    component.evse_id = evse_id;
    return component;
}

// Helper function to construct DeviceModelVariable with common structure
DeviceModelVariable make_variable(const std::string& name, const ocpp::v2::VariableCharacteristics& characteristics,
                                  const std::string& value = "",
                                  ocpp::v2::MutabilityEnum mutability = ocpp::v2::MutabilityEnum::ReadOnly) {
    DeviceModelVariable var_data;
    var_data.name = name;
    var_data.characteristics = characteristics;

    DbVariableAttribute db_attr;
    VariableAttribute attr;
    attr.type = ocpp::v2::AttributeEnum::Actual;
    attr.value = value;
    attr.mutability = mutability;
    db_attr.variable_attribute = attr;

    var_data.attributes.push_back(db_attr);
    return var_data;
}

// Helper function to construct DeviceModelVariable with common structure
DeviceModelVariable
make_variable_with_instance(const std::string& name, const std::string& instance,
                            const ocpp::v2::VariableCharacteristics& characteristics, const std::string& value = "",
                            ocpp::v2::MutabilityEnum mutability = ocpp::v2::MutabilityEnum::ReadOnly) {
    DeviceModelVariable var_data;
    var_data.name = name;
    var_data.instance = instance;
    var_data.characteristics = characteristics;

    DbVariableAttribute db_attr;
    VariableAttribute attr;
    attr.type = ocpp::v2::AttributeEnum::Actual;
    attr.value = value;
    attr.mutability = mutability;
    db_attr.variable_attribute = attr;

    var_data.attributes.push_back(db_attr);
    return var_data;
}

// Populates EVSE variables. libocpp provides AvailabilityState, which its ComponentStateManager computes.
std::vector<DeviceModelVariable> build_evse_variables(const float max_power) {
    std::vector<DeviceModelVariable> variables;

    auto evse_power_characteristics = EvseDefinitions::Characteristics::EVSEPower;
    evse_power_characteristics.maxLimit = max_power;

    return {make_variable(ocpp::v2::EvseComponentVariables::Available.name, EvseDefinitions::Characteristics::Available,
                          "true"),
            make_variable(ocpp::v2::EvseComponentVariables::Power.name, evse_power_characteristics),
            make_variable(ocpp::v2::EvseComponentVariables::SupplyPhases.name,
                          EvseDefinitions::Characteristics::SupplyPhases),
            make_variable(ocpp::v2::EvseComponentVariables::AllowReset.name,
                          EvseDefinitions::Characteristics::AllowReset, "false"),
            make_variable(ocpp::v2::EvseComponentVariables::ISO15118EvseId.name,
                          EvseDefinitions::Characteristics::ISO15118EvseId, "DEFAULT_EVSE_ID")};
}

// Populates Connector variables. libocpp provides AvailabilityState.
std::vector<DeviceModelVariable> build_connector_variables() {

    return {make_variable(ocpp::v2::ConnectorComponentVariables::Available.name,
                          ConnectorDefinitions::Characteristics::Available, "true"),
            make_variable(ocpp::v2::ConnectorComponentVariables::Type.name,
                          ConnectorDefinitions::Characteristics::ConnectorType),
            make_variable(ocpp::v2::ConnectorComponentVariables::SupplyPhases.name,
                          ConnectorDefinitions::Characteristics::SupplyPhases)};
}

// Populates V2X variables
std::vector<DeviceModelVariable> build_v2x_variables(const bool v2x_supported,
                                                     const std::string& supported_energy_transfers,
                                                     const std::string& supported_operation_modes) {
    std::string v2x_supported_string = v2x_supported ? "true" : "false";
    return {make_variable(ocpp::v2::V2xComponentVariables::Available.name, V2XDefinitions::Characteristics::Available,
                          v2x_supported_string),
            make_variable(ocpp::v2::V2xComponentVariables::Enabled.name, V2XDefinitions::Characteristics::Enabled,
                          v2x_supported_string),
            make_variable(ocpp::v2::V2xComponentVariables::SupportedEnergyTransferModes.name,
                          V2XDefinitions::Characteristics::SupportedEnergyTransferModes, supported_energy_transfers),
            make_variable(ocpp::v2::V2xComponentVariables::SupportedOperationModes.name,
                          V2XDefinitions::Characteristics::SupportedOperationModes, supported_operation_modes)};
}

// Populates ISO15118 variables
std::vector<DeviceModelVariable> build_iso15118_variables(const bool iso_supported,
                                                          const bool service_renegotiation_supported,
                                                          const std::string& supported_protocols) {
    return {make_variable(ocpp::v2::ISO15118ComponentVariables::Enabled.name,
                          ISO15118Definitions::Characteristics::Enabled, iso_supported ? "true" : "false"),
            make_variable(ocpp::v2::ISO15118ComponentVariables::ServiceRenegotiationSupport.name,
                          ISO15118Definitions::Characteristics::ServiceRenegotiationSupport,
                          service_renegotiation_supported ? "true" : "false"),
            make_variable(ocpp::v2::ISO15118ComponentVariables::ProtocolSupported.name,
                          ISO15118Definitions::Characteristics::ProtocolSupported, supported_protocols)};
}

// Populates ConnectedEV variables
std::vector<DeviceModelVariable> build_connected_ev_variables() {
    std::vector<DeviceModelVariable> connected_ev_variables{
        make_variable(ocpp::v2::ConnectedEvComponentVariables::Available.name,
                      ConnectedEVDefinitions::Characteristics::Available, "false"),
        make_variable(ocpp::v2::ConnectedEvComponentVariables::VehicleId.name,
                      ConnectedEVDefinitions::Characteristics::VehicleId, ""),
        make_variable(ocpp::v2::ConnectedEvComponentVariables::ProtocolAgreed.name,
                      ConnectedEVDefinitions::Characteristics::ProtocolAgreed, ""),
        make_variable_with_instance(ocpp::v2::ConnectedEvComponentVariables::VehicleCertificateLeaf.name,
                                    ocpp::v2::ConnectedEvComponentVariables::VehicleCertificateLeaf.instance.value(),
                                    ConnectedEVDefinitions::Characteristics::VehicleCertificateLeaf, ""),
        make_variable_with_instance(ocpp::v2::ConnectedEvComponentVariables::VehicleCertificateSubCa1.name,
                                    ocpp::v2::ConnectedEvComponentVariables::VehicleCertificateSubCa1.instance.value(),
                                    ConnectedEVDefinitions::Characteristics::VehicleCertificateSubCa1, ""),
        make_variable_with_instance(ocpp::v2::ConnectedEvComponentVariables::VehicleCertificateSubCa2.name,
                                    ocpp::v2::ConnectedEvComponentVariables::VehicleCertificateSubCa2.instance.value(),
                                    ConnectedEVDefinitions::Characteristics::VehicleCertificateSubCa2, ""),
        make_variable_with_instance(ocpp::v2::ConnectedEvComponentVariables::VehicleCertificateRoot.name,
                                    ocpp::v2::ConnectedEvComponentVariables::VehicleCertificateRoot.instance.value(),
                                    ConnectedEVDefinitions::Characteristics::VehicleCertificateRoot, "")};
    const int number_of_variables = NUMBER_OF_CONNECTED_EV_PROTOCOLS + connected_ev_variables.size();
    connected_ev_variables.reserve(number_of_variables);

    std::string variable_name = ocpp::v2::ConnectedEvComponentVariables::get_protocol_supported_by_ev(1).name;
    for (int i = 1; i <= NUMBER_OF_CONNECTED_EV_PROTOCOLS; ++i) {
        connected_ev_variables.emplace_back(make_variable_with_instance(
            variable_name, std::to_string(i), ConnectedEVDefinitions::Characteristics::ProtocolSupportedByEV));
    }
    return connected_ev_variables;
}

// DC DER controller variables: Available marks static presence (provisioned "true", ReadOnly) and Enabled
// is the CSMS runtime control (provisioned "true", ReadWrite). Every variable the config/enable builders may
// write
// on the DC path must be defined here, else the write targets an unknown variable and is rejected.
std::vector<DeviceModelVariable> build_dc_der_ctrlr_variables() {
    std::vector<DeviceModelVariable> variables;
    variables.push_back(make_variable(ocpp::v2::DERComponentVariables::Available.name,
                                      DERDefinitions::Characteristics::Available, "true",
                                      ocpp::v2::MutabilityEnum::ReadOnly));
    variables.push_back(make_variable(ocpp::v2::DERComponentVariables::Enabled.name,
                                      DERDefinitions::Characteristics::Enabled, "true",
                                      ocpp::v2::MutabilityEnum::ReadWrite));
    variables.push_back(make_variable(ocpp::v2::DERComponentVariables::ModesSupported.name,
                                      DERDefinitions::Characteristics::ModesSupported, ""));
    for (const auto& name : DERDefinitions::DecimalVariableNames) {
        variables.push_back(make_variable(name, DERDefinitions::Characteristics::Decimal));
    }
    for (const auto& name : DERDefinitions::InverterStringVariableNames) {
        variables.push_back(make_variable(name, DERDefinitions::Characteristics::InverterString));
    }
    return variables;
}

// AC DER controller variables: Available (static presence, ReadOnly), Enabled (CSMS runtime control, provisioned
// "true", ReadWrite) and ModesSupported. The nameplate scalars live exclusively on the DC component
// (matching to_der_ctrlr_config_set_variables).
std::vector<DeviceModelVariable> build_ac_der_ctrlr_variables() {
    return {make_variable(ocpp::v2::DERComponentVariables::Available.name, DERDefinitions::Characteristics::Available,
                          "true", ocpp::v2::MutabilityEnum::ReadOnly),
            make_variable(ocpp::v2::DERComponentVariables::Enabled.name, DERDefinitions::Characteristics::Enabled,
                          "true", ocpp::v2::MutabilityEnum::ReadWrite),
            make_variable(ocpp::v2::DERComponentVariables::ModesSupported.name,
                          DERDefinitions::Characteristics::ModesSupported, "")};
}

std::string supported_operation_modes_vector_to_string(
    const std::vector<ocpp::v2::OperationModeEnum>& evse_supported_operation_modes) {
    std::string supported_string{};
    for (const auto& operation_mode : evse_supported_operation_modes) {
        supported_string += ocpp::v2::conversions::operation_mode_enum_to_string(operation_mode) + ",";
    }
    if (!supported_string.empty()) {
        supported_string.pop_back();
    }
    return supported_string;
}

bool supports_v2x(const std::vector<types::iso15118::EnergyTransferMode>& supported_energy_transfer_modes) {
    return std::any_of(supported_energy_transfer_modes.cbegin(), supported_energy_transfer_modes.cend(),
                       [](const types::iso15118::EnergyTransferMode& mode) {
                           return mode == types::iso15118::EnergyTransferMode::AC_BPT or
                                  mode == types::iso15118::EnergyTransferMode::AC_BPT_DER or
                                  mode == types::iso15118::EnergyTransferMode::DC_BPT or
                                  mode == types::iso15118::EnergyTransferMode::DC_ACDP_BPT;
                       });
}

// The setpoint operation modes direct the discharge of the EV, which needs a bidirectional energy transfer mode
std::string supported_operation_modes_string(const bool v2x_supported) {
    std::vector<ocpp::v2::OperationModeEnum> operation_modes{ocpp::v2::OperationModeEnum::ChargingOnly,
                                                             ocpp::v2::OperationModeEnum::Idle};
    if (v2x_supported) {
        operation_modes.push_back(ocpp::v2::OperationModeEnum::CentralSetpoint);
        operation_modes.push_back(ocpp::v2::OperationModeEnum::ExternalSetpoint);
    }
    return supported_operation_modes_vector_to_string(operation_modes);
}

std::string build_supported_protocol_string(const std::string& uri, const int32_t major, const int32_t minor) {
    return uri + ',' + std::to_string(major) + ',' + std::to_string(minor);
}

ocpp::v2::SetVariableData make_set_variable_data(const ocpp::v2::ComponentVariable& component_variable,
                                                 const std::string& value) {
    ocpp::v2::SetVariableData data;
    data.component = component_variable.component;
    if (component_variable.variable) {
        data.variable = component_variable.variable.value();
    }
    data.attributeValue = value;
    return data;
}

} // anonymous namespace

std::string
supported_energy_transfer_modes_vector_to_string(const std::vector<types::iso15118::EnergyTransferMode>& modes) {
    std::vector<ocpp::v2::EnergyTransferModeEnum> ocpp_modes;
    for (const auto mode : modes) {
        const auto ocpp_mode = ocpp_module_common::conversions::to_ocpp_energy_transfer_mode(mode);
        if (std::find(ocpp_modes.cbegin(), ocpp_modes.cend(), ocpp_mode) == ocpp_modes.cend()) {
            ocpp_modes.push_back(ocpp_mode);
        }
    }
    std::string supported_string{};
    for (const auto ocpp_mode : ocpp_modes) {
        supported_string += ocpp::v2::conversions::energy_transfer_mode_enum_to_string(ocpp_mode) + ",";
    }
    if (!supported_string.empty()) {
        supported_string.pop_back();
    }
    return supported_string;
}

bool evse_hlc_capable(const std::vector<types::evse_manager::Connector>& connectors) {
    return std::any_of(connectors.cbegin(), connectors.cend(),
                       [](const types::evse_manager::Connector& c) { return c.hlc_capable; });
}

DerCtrlrComponent der_ctrlr_component(const bool der_wired,
                                      const std::vector<types::evse_manager::Connector>& connectors) {
    if (not der_wired) {
        return DerCtrlrComponent::None;
    }
    // The schema requires at least one connector, so this is a malformed payload rather than a case to
    // model; guarded because the alternative is reading front() off an empty vector.
    if (connectors.empty()) {
        return DerCtrlrComponent::None;
    }
    // Take the answer from a connector and require the rest to agree, rather than testing each mode in
    // turn: the component then follows the reported data instead of the order the modes are checked in.
    const auto mode = connectors.front().charge_mode;
    const bool unanimous =
        std::all_of(connectors.cbegin(), connectors.cend(),
                    [mode](const types::evse_manager::Connector& c) { return c.charge_mode == mode; });
    if (not unanimous) {
        // OCPP holds at most one DER controller per EVSE, so there is no answer here that is not a guess
        // between two true ones.
        return DerCtrlrComponent::None;
    }
    if (mode == types::evse_manager::ChargeMode::DC) {
        // No HLC check: EvseManager refuses to start a DC EVSE without HLC and SLAC wired, so a DC
        // connector that reported no HLC could not have booted (EvseManager.cpp, "DC mode requires
        // slac, HLC and powersupply DCDC to be connected").
        return DerCtrlrComponent::Dc;
    }
    // AC DER exists only as an ISO 15118-20 relay, so an EVSE that can never run an HLC session can
    // never do AC DER.
    return evse_hlc_capable(connectors) ? DerCtrlrComponent::Ac : DerCtrlrComponent::None;
}

namespace {

// A connector reporting no hlc_capable otherwise reads as a charge mode disagreement, sending an operator
// after a conflict that is not there. Only meaningful for a (der_wired, connectors) pair that
// der_ctrlr_component already resolved to None. Re-asks with HLC forced on so the HLC rule is not
// restated here; a wired None cause that forcing HLC does not flip needs its own branch above, or it
// reports as a mode disagreement.
std::string_view none_reason(const bool der_wired, const std::vector<types::evse_manager::Connector>& connectors) {
    if (not der_wired) {
        return ": no grid_support connection";
    }
    if (connectors.empty()) {
        return ": it reports no connectors";
    }
    auto hlc_forced = connectors;
    for (auto& connector : hlc_forced) {
        connector.hlc_capable = true;
    }
    if (der_ctrlr_component(true, hlc_forced) != DerCtrlrComponent::None) {
        return ": AC DER runs only over ISO 15118-20 and no connector is hlc_capable";
    }
    return ": its connectors report no single charge mode";
}

} // anonymous namespace

std::map<int32_t, DerCtrlrComponent> decide_der_ctrlr_components(const std::vector<types::evse_manager::Evse>& evses,
                                                                 const std::set<int32_t>& der_wired_evse_ids,
                                                                 const bool with_der_components) {
    std::map<int32_t, DerCtrlrComponent> result;
    if (not with_der_components) {
        return result;
    }
    for (const auto& evse : evses) {
        const bool der_wired = der_wired_evse_ids.count(evse.id) > 0;
        const auto component = der_ctrlr_component(der_wired, evse.connectors);
        if (component == DerCtrlrComponent::None) {
            EVLOG_info << "No DER controller for EVSE " << evse.id << none_reason(der_wired, evse.connectors);
        }
        result[evse.id] = component;
    }
    // Every served EVSE got an entry above, None ones included, so a wired id missing from the map matches
    // no EVSE at all: the connection exists but can never route.
    for (const auto wired_id : der_wired_evse_ids) {
        if (result.count(wired_id) == 0) {
            EVLOG_error << "grid_support is wired to EVSE " << wired_id
                        << ", which this station does not serve; the connection is inert";
        }
    }
    return result;
}

std::optional<std::pair<ocpp::v2::ComponentKey, std::vector<DeviceModelVariable>>>
build_der_ctrlr_component_config(const int32_t evse_id, const DerCtrlrComponent component) {
    switch (component) {
    case DerCtrlrComponent::Dc:
        return std::make_pair(get_dc_der_ctrlr_component_key(evse_id), build_dc_der_ctrlr_variables());
    case DerCtrlrComponent::Ac:
        return std::make_pair(get_ac_der_ctrlr_component_key(evse_id), build_ac_der_ctrlr_variables());
    case DerCtrlrComponent::None:
        break;
    }
    return std::nullopt;
}

std::map<ocpp::v2::ComponentKey, std::vector<DeviceModelVariable>>
build_der_component_configs(const std::map<int32_t, DerCtrlrComponent>& der_ctrlr_components,
                            const std::vector<types::evse_manager::Evse>& evses,
                            const std::vector<int32_t>& iso_extension_evse_ids,
                            const std::map<int32_t, bool>& evse_service_renegotiation_supported) {
    std::map<ocpp::v2::ComponentKey, std::vector<DeviceModelVariable>> result;
    for (const auto& [evse_id, component] : der_ctrlr_components) {
        auto config = build_der_ctrlr_component_config(evse_id, component);
        if (config.has_value()) {
            result[config->first] = std::move(config->second);
        }
    }
    for (const auto evse_id : iso_extension_evse_ids) {
        const auto evse = std::find_if(evses.cbegin(), evses.cend(),
                                       [evse_id](const types::evse_manager::Evse& e) { return e.id == evse_id; });
        if (evse == evses.cend()) {
            EVLOG_error << "iso15118_extensions is mapped to EVSE " << evse_id
                        << ", which this station does not serve; no ISO15118Ctrlr component is provisioned";
            continue;
        }
        // Unreachable through today's callers: the guard above narrows evse_id to a served EVSE and both
        // modules seed this map over that same range. Kept because this function is public and directly
        // callable, and "not supported" is the safe default.
        const auto renegotiation = evse_service_renegotiation_supported.find(evse_id);
        const bool renegotiation_supported =
            renegotiation != evse_service_renegotiation_supported.cend() and renegotiation->second;
        // TODO(mlitre): Correctly fill iso supported protocols
        // Reported by the serving evse_manager rather than assumed: an extension can be wired to an
        // EVSE whose HLC is switched off, and Enabled is provisioned ReadOnly, so a hardcoded "true"
        // told the CSMS an ISO 15118 session was possible when it never was.
        result[get_iso15118_component_key(evse_id)] =
            build_iso15118_variables(evse_hlc_capable(evse->connectors), renegotiation_supported, "");
    }
    return result;
}

std::map<ocpp::v2::ComponentKey, std::vector<DeviceModelVariable>>
build_der_component_configs(const std::vector<types::evse_manager::Evse>& evses,
                            const std::set<int32_t>& der_wired_evse_ids, const bool with_der_components,
                            const std::vector<int32_t>& iso_extension_evse_ids,
                            const std::map<int32_t, bool>& evse_service_renegotiation_supported) {
    return build_der_component_configs(decide_der_ctrlr_components(evses, der_wired_evse_ids, with_der_components),
                                       evses, iso_extension_evse_ids, evse_service_renegotiation_supported);
}

std::vector<ocpp::v2::SetVariableData>
to_der_ctrlr_config_set_variables(const int32_t evse_id, const types::grid_support::DERCapability& capability) {
    std::vector<ocpp::v2::SetVariableData> result;

    using ComponentVariableGetter = ocpp::v2::ComponentVariable (*)(const std::int32_t, const ocpp::v2::Variable&);
    const ComponentVariableGetter get_component_variable =
        capability.dc.has_value() ? &ocpp::v2::DERComponentVariables::get_dc_component_variable
                                  : &ocpp::v2::DERComponentVariables::get_ac_component_variable;

    const auto named_variable = [&](const std::string& name) {
        ocpp::v2::Variable variable;
        variable.name = name;
        return get_component_variable(evse_id, variable);
    };

    std::string modes_csv;
    for (const auto& directive_type : capability.supported_types) {
        if (not modes_csv.empty()) {
            modes_csv += ",";
        }
        modes_csv += types::grid_support::directive_type_to_string(directive_type);
    }
    result.push_back(make_set_variable_data(
        get_component_variable(evse_id, ocpp::v2::DERComponentVariables::ModesSupported), modes_csv));

    // Nameplate scalars below exist only on the DC component (DCDERCtrlr_1.json); emit on DC path only.
    if (not capability.dc) {
        return result;
    }

    const auto& nameplate = capability.nameplate;
    const auto emit_float = [&](const std::string& name, const float value) {
        result.push_back(make_set_variable_data(named_variable(name), std::to_string(value)));
    };
    const auto emit_string = [&](const std::string& name, const std::string& value) {
        result.push_back(make_set_variable_data(named_variable(name), value));
    };

    emit_float("MaxW", nameplate.max_w_W);
    emit_float("MaxVA", nameplate.max_va_VA);
    if (nameplate.max_var_over_excited_var) {
        emit_float("MaxVar", nameplate.max_var_over_excited_var.value());
    }
    if (nameplate.max_var_under_excited_var) {
        emit_float("MaxVarNeg", nameplate.max_var_under_excited_var.value());
    }
    if (nameplate.max_charge_w_W) {
        emit_float("MaxChargeRateW", nameplate.max_charge_w_W.value());
    }

    const auto& dc = capability.dc.value();
    if (dc.device_info) {
        const auto& info = dc.device_info.value();
        if (info.manufacturer) {
            emit_string("InverterManufacturer", info.manufacturer.value());
        }
        if (info.model) {
            emit_string("InverterModel", info.model.value());
        }
        if (info.sw_version) {
            emit_string("InverterSwVersion", info.sw_version.value());
        }
        if (info.hw_version) {
            emit_string("InverterHwVersion", info.hw_version.value());
        }
    }
    if (dc.over_excited_pf) {
        emit_float("OverExcitedPF", dc.over_excited_pf.value());
    }
    if (dc.under_excited_pf) {
        emit_float("UnderExcitedPF", dc.under_excited_pf.value());
    }
    if (dc.over_excited_w) {
        emit_float("OverExcitedW", dc.over_excited_w.value());
    }
    if (dc.under_excited_w) {
        emit_float("UnderExcitedW", dc.under_excited_w.value());
    }
    if (dc.reactive_susceptance) {
        emit_float("ReactiveSusceptance", dc.reactive_susceptance.value());
    }

    return result;
}

std::map<ComponentKey, std::vector<DeviceModelVariable>> build_runtime_component_configs(
    const std::vector<types::evse_manager::Evse>& evses,
    const std::map<int32_t, types::evse_board_support::HardwareCapabilities>& evse_hardware_capabilities_map,
    const std::map<int32_t, std::vector<types::iso15118::EnergyTransferMode>>& evse_supported_energy_transfers,
    const std::map<int32_t, bool>& evse_service_renegotiation_supported, const bool with_der_components,
    const std::set<int32_t>& der_wired_evse_ids, const std::vector<int32_t>& iso_extension_evse_ids) {
    std::map<ComponentKey, std::vector<DeviceModelVariable>> component_configs;

    for (const auto& evse_info : evses) {
        const auto hw_capabilities_it = evse_hardware_capabilities_map.find(evse_info.id);
        auto max_power = 0.0F;
        std::optional<int32_t> supply_phases;
        if (hw_capabilities_it != evse_hardware_capabilities_map.end()) {
            const auto& hw_capabilities = hw_capabilities_it->second;
            max_power = hw_capabilities.max_current_A_import * 230.0F * hw_capabilities.max_phase_count_import;
            supply_phases = hw_capabilities.max_phase_count_import;
        } else {
            EVLOG_error << "No hardware capabilities found for EVSE with ID " << evse_info.id;
        }

        auto evse_variables = build_evse_variables(max_power);
        if (supply_phases.has_value()) {
            for (auto& variable : evse_variables) {
                if (variable.name == ocpp::v2::EvseComponentVariables::SupplyPhases.name.get()) {
                    variable.attributes.at(0).variable_attribute.value = std::to_string(supply_phases.value());
                }
            }
        }
        component_configs[get_evse_component_key(evse_info.id)] = std::move(evse_variables);

        for (const auto& connector : evse_info.connectors) {
            auto connector_variables = build_connector_variables();
            if (connector.type.has_value()) {
                for (auto& variable : connector_variables) {
                    if (variable.name == ocpp::v2::ConnectorComponentVariables::Type.name.get()) {
                        variable.attributes.at(0).variable_attribute.value =
                            types::evse_manager::connector_type_enum_to_string(connector.type.value());
                    }
                }
            }
            component_configs[get_connector_component_key(evse_info.id, connector.id)] = std::move(connector_variables);
        }

        const auto transfers_it = evse_supported_energy_transfers.find(evse_info.id);
        const auto supported_energy_transfer_modes = transfers_it != evse_supported_energy_transfers.end()
                                                         ? transfers_it->second
                                                         : std::vector<types::iso15118::EnergyTransferMode>{};
        const bool v2x_supported = supports_v2x(supported_energy_transfer_modes);
        component_configs[get_v2x_component_key(evse_info.id)] = build_v2x_variables(
            v2x_supported, supported_energy_transfer_modes_vector_to_string(supported_energy_transfer_modes),
            supported_operation_modes_string(v2x_supported));

        component_configs[get_connected_ev_component_key(evse_info.id)] = build_connected_ev_variables();
    }

    const auto der_ctrlr_components = decide_der_ctrlr_components(evses, der_wired_evse_ids, with_der_components);
    for (auto& [component_key, variables] : build_der_component_configs(
             der_ctrlr_components, evses, iso_extension_evse_ids, evse_service_renegotiation_supported)) {
        component_configs[component_key] = std::move(variables);
    }
    return component_configs;
}

std::vector<ocpp::v2::RegisteredComponent>
to_registered_components(const std::map<ComponentKey, std::vector<DeviceModelVariable>>& component_configs) {
    std::vector<ocpp::v2::RegisteredComponent> components;
    components.reserve(component_configs.size());
    for (const auto& [component_key, variables] : component_configs) {
        ocpp::v2::RegisteredComponent registered_component;
        registered_component.component.name = component_key.name;
        if (component_key.instance.has_value()) {
            registered_component.component.instance = component_key.instance.value();
        }
        if (component_key.evse_id.has_value()) {
            registered_component.component.evse =
                EvseDefinitions::get_evse(component_key.evse_id.value(), component_key.connector_id);
        }
        const bool der_ctrlr = component_key.name == "ACDERCtrlr" or component_key.name == "DCDERCtrlr";
        for (const auto& variable : variables) {
            ocpp::v2::RegisteredVariable registered_variable;
            registered_variable.variable.name = variable.name;
            if (variable.instance.has_value()) {
                registered_variable.variable.instance = variable.instance.value();
            }
            registered_variable.characteristics = variable.characteristics;
            bool write_only = false;
            for (const auto& attribute : variable.attributes) {
                auto registered_attribute = attribute.variable_attribute;
                write_only = write_only or registered_attribute.mutability == ocpp::v2::MutabilityEnum::WriteOnly;
                // The CSMS sets DER Enabled, which must survive a restart
                registered_attribute.persistent =
                    der_ctrlr and variable.name == ocpp::v2::DERComponentVariables::Enabled.name.get();
                registered_variable.attributes.push_back(std::move(registered_attribute));
            }
            // A monitor is evaluated only when the value changes, so monitoring is cheap. A monitor would report a
            // WriteOnly value in NotifyEvent.
            registered_variable.characteristics.supportsMonitoring = not write_only;
            registered_component.variables.push_back(std::move(registered_variable));
        }
        components.push_back(std::move(registered_component));
    }
    return components;
}

bool import_stored_values(const std::vector<ocpp::v2::StoredVariableValue>& values,
                          ocpp::v2::DeviceModelStorageInterface& ocpp_storage) {
    const auto stored_values = ocpp_storage.get_stored_values();
    bool all_imported = true;
    for (const auto& value : values) {
        const bool stored = std::any_of(stored_values.begin(), stored_values.end(), [&value](const auto& stored_value) {
            return stored_value.component == value.component and stored_value.variable == value.variable and
                   stored_value.attribute == value.attribute;
        });
        if (stored) {
            continue;
        }
        if (ocpp_storage.set_stored_value(value) != ocpp::v2::SetVariableStatusEnum::Accepted) {
            EVLOG_error << "Could not import " << value.component << "." << value.variable << " = " << value.value
                        << " into the device model database";
            all_imported = false;
            continue;
        }
        EVLOG_info << "Imported " << value.component << "." << value.variable << " = " << value.value
                   << " into the device model database";
    }
    return all_imported;
}

std::vector<ocpp::v2::StoredVariableValue>
read_legacy_der_enabled_values(const std::filesystem::path& everest_db_path) {
    everest::db::sqlite::Connection connection(everest_db_path);
    if (not connection.open_connection()) {
        EVLOG_error << "Could not open " << everest_db_path << " to read the DER Enabled values";
        return {};
    }
    // Only values the CSMS set: EVerest wrote the provisioned ones itself
    auto statement = connection.new_statement(
        "SELECT c.NAME, c.EVSE_ID, va.VALUE, va.VALUE_SOURCE FROM COMPONENT c JOIN VARIABLE v ON v.COMPONENT_ID = c.ID "
        "JOIN VARIABLE_ATTRIBUTE va ON va.VARIABLE_ID = v.ID WHERE c.NAME IN ('ACDERCtrlr', 'DCDERCtrlr') AND "
        "c.EVSE_ID IS NOT NULL AND v.NAME = 'Enabled' AND va.TYPE_ID = 0 AND va.VALUE IS NOT NULL AND "
        "va.VALUE_SOURCE IS NOT NULL AND va.VALUE_SOURCE NOT IN ('EVEREST', 'default')");
    std::vector<ocpp::v2::StoredVariableValue> values;
    while (statement->step() == SQLITE_ROW) {
        Component component;
        component.name = statement->column_text(0);
        component.evse = EvseDefinitions::get_evse(statement->column_int(1));
        values.push_back({component, ocpp::v2::DERComponentVariables::Enabled, ocpp::v2::AttributeEnum::Actual,
                          statement->column_text(2), statement->column_text(3), VARIABLE_SOURCE_EVEREST});
    }
    return values;
}

void migrate_everest_device_model_database(const std::filesystem::path& everest_db_path,
                                           const std::filesystem::path& device_model_db_path,
                                           ocpp::v2::DeviceModelStorageInterface& ocpp_storage) {
    std::error_code error;
    const auto status = std::filesystem::status(everest_db_path, error);
    if (not std::filesystem::exists(status)) {
        return;
    }
    if (not std::filesystem::is_regular_file(status)) {
        EVLOG_error << "EverestDeviceModelDatabasePath " << everest_db_path << " is not a file: it is not taken over";
        return;
    }
    if (std::filesystem::equivalent(everest_db_path, device_model_db_path, error)) {
        EVLOG_error << "EverestDeviceModelDatabasePath and DeviceModelDatabasePath both name " << everest_db_path
                    << ": it is not taken over";
        return;
    }

    std::vector<ocpp::v2::StoredVariableValue> values;
    try {
        values = read_legacy_der_enabled_values(everest_db_path);
    } catch (const std::exception& e) {
        EVLOG_error << "Could not read the DER Enabled values from " << everest_db_path << ": " << e.what();
    }
    try {
        if (not import_stored_values(values, ocpp_storage)) {
            EVLOG_error << "Keeping " << everest_db_path << " to import its DER Enabled values on the next start";
            return;
        }
    } catch (const std::exception& e) {
        EVLOG_error << "Could not import the DER Enabled values from " << everest_db_path << ": " << e.what()
                    << ". Keeping it to import them on the next start";
        return;
    }

    // Renamed last: an interrupted first start repeats the import, which keeps the values imported already
    auto migrated_path = everest_db_path;
    migrated_path += ".migrated";
    std::filesystem::rename(everest_db_path, migrated_path, error);
    if (error) {
        EVLOG_error << "Could not rename " << everest_db_path << " to " << migrated_path << ": " << error.message();
        return;
    }
    EVLOG_info << "Took over the EVerest device model database, renamed it to " << migrated_path;
}

EverestDeviceModel::EverestDeviceModel(
    const std::vector<std::unique_ptr<evse_managerIntf>>& r_evse_manager,
    const std::vector<std::unique_ptr<iso15118_extensionsIntf>>& r_extensions_15118,
    const std::map<int32_t, types::evse_board_support::HardwareCapabilities>& evse_hardware_capabilities_map,
    const std::map<int32_t, std::vector<types::iso15118::EnergyTransferMode>>& evse_supported_energy_transfers,
    const std::map<int32_t, bool>& evse_service_renegotiation_supported, const bool with_der_components,
    const std::set<int32_t>& der_wired_evse_ids) {
    std::vector<types::evse_manager::Evse> evses;
    for (const auto& evse_manager : r_evse_manager) {
        evses.push_back(evse_manager->call_get_evse());
    }
    std::vector<int32_t> iso_extension_evse_ids;
    for (const auto& extension : r_extensions_15118) {
        const auto mapping = extension->get_mapping();
        if (mapping.has_value()) {
            iso_extension_evse_ids.push_back(mapping->evse);
        }
    }

    this->state.handle()->components = to_registered_components(build_runtime_component_configs(
        evses, evse_hardware_capabilities_map, evse_supported_energy_transfers, evse_service_renegotiation_supported,
        with_der_components, der_wired_evse_ids, iso_extension_evse_ids));

    for (std::size_t i = 0; i < r_evse_manager.size(); i++) {
        const auto evse_id = evses.at(i).id;
        const auto evse_component = get_evse_component(evse_id);
        r_evse_manager.at(i)->subscribe_hw_capabilities(
            [this, evse_component](const types::evse_board_support::HardwareCapabilities& hw_capabilities) {
                this->set_value(evse_component, ocpp::v2::EvseComponentVariables::SupplyPhases,
                                std::to_string(hw_capabilities.max_phase_count_import));
            });
        const auto v2x_component = get_v2x_component(evse_id);
        r_evse_manager.at(i)->subscribe_supported_energy_transfer_modes(
            [this, v2x_component](const std::vector<types::iso15118::EnergyTransferMode>& modes) {
                this->set_value(v2x_component, ocpp::v2::V2xComponentVariables::SupportedEnergyTransferModes,
                                supported_energy_transfer_modes_vector_to_string(modes));
                const bool v2x_supported = supports_v2x(modes);
                const std::string v2x_supported_string = v2x_supported ? "true" : "false";
                this->set_value(v2x_component, ocpp::v2::V2xComponentVariables::Available, v2x_supported_string);
                this->set_value(v2x_component, ocpp::v2::V2xComponentVariables::Enabled, v2x_supported_string);
                this->set_value(v2x_component, ocpp::v2::V2xComponentVariables::SupportedOperationModes,
                                supported_operation_modes_string(v2x_supported));
            });
    }
    for (const auto& extension : r_extensions_15118) {
        const auto mapping = extension->get_mapping();
        if (not mapping.has_value()) {
            continue;
        }
        const auto iso15118_component = get_iso15118_component(mapping->evse);
        extension->subscribe_service_renegotiation_supported(
            [this, iso15118_component](const bool service_renegotiation_supported) {
                this->set_value(iso15118_component, ocpp::v2::ISO15118ComponentVariables::ServiceRenegotiationSupport,
                                service_renegotiation_supported ? "true" : "false");
            });
        const auto connected_ev_component = get_connected_ev_component(mapping->evse);
        extension->subscribe_ev_info(
            [this, connected_ev_component](const types::iso15118::EvInformation& ev_information) {
                this->update_connected_ev_information(connected_ev_component, ev_information);
            });
    }
}

ocpp::v2::ComponentRegistrationResult
EverestDeviceModel::register_with(ocpp::v2::DeviceModelRegistryInterface& registry) {
    // Held while registering, so that a value reported meanwhile is either part of the registration or reported
    // to the registry afterwards
    auto handle = this->state.handle();
    handle->registry = &registry;
    const auto result = registry.register_components(VARIABLE_SOURCE_EVEREST, handle->components, nullptr);
    for (const auto& rejected : result.rejected) {
        EVLOG_error << "The device model rejected " << rejected.component << "." << rejected.variable
                    << " that EVerest provides";
    }
    return result;
}

void EverestDeviceModel::detach() {
    auto handle = this->state.handle();
    handle->registry = nullptr;
    handle.wait([&handle]() { return handle->reports_in_progress == 0; });
}

std::vector<ocpp::v2::RegisteredComponent> EverestDeviceModel::get_components() {
    return this->state.handle()->components;
}

ocpp::v2::SetVariableStatusEnum EverestDeviceModel::set_value(const Component& component, const Variable& variable,
                                                              const std::string& value) {
    ocpp::v2::DeviceModelRegistryInterface* registry = nullptr;
    {
        auto handle = this->state.handle();
        const auto registered_component =
            std::find_if(handle->components.begin(), handle->components.end(),
                         [&component](const auto& candidate) { return candidate.component == component; });
        if (registered_component == handle->components.end()) {
            return ocpp::v2::SetVariableStatusEnum::UnknownComponent;
        }
        const auto registered_variable =
            std::find_if(registered_component->variables.begin(), registered_component->variables.end(),
                         [&variable](const auto& candidate) { return candidate.variable == variable; });
        if (registered_variable == registered_component->variables.end() or registered_variable->attributes.empty()) {
            return ocpp::v2::SetVariableStatusEnum::UnknownVariable;
        }
        registered_variable->attributes.front().value = value;
        if (handle->registry == nullptr) {
            return ocpp::v2::SetVariableStatusEnum::Accepted;
        }
        registry = handle->registry;
        handle->reports_in_progress++;
    }
    // Reported without the lock: the registry runs the variable listeners inside notify_value_changed. detach waits
    // until no report is in progress.
    const auto result = registry->notify_value_changed(component, variable, ocpp::v2::AttributeEnum::Actual, value,
                                                       VARIABLE_SOURCE_EVEREST);
    if (result != ocpp::v2::SetVariableStatusEnum::Accepted) {
        EVLOG_debug << "The device model did not accept " << component << "." << variable << " = " << value;
    }
    this->state.handle()->reports_in_progress--;
    this->state.notify_all();
    return result;
}

std::vector<ocpp::v2::SetVariableResult>
EverestDeviceModel::update_der_capability(const int32_t evse_id, const types::grid_support::DERCapability& capability) {
    std::vector<ocpp::v2::SetVariableResult> results;
    for (const auto& data : to_der_ctrlr_config_set_variables(evse_id, capability)) {
        ocpp::v2::SetVariableResult result;
        result.component = data.component;
        result.variable = data.variable;
        result.attributeType = data.attributeType;
        result.attributeStatus = this->set_value(data.component, data.variable, data.attributeValue.get());
        results.push_back(std::move(result));
    }
    return results;
}

void EverestDeviceModel::update_connected_ev_information(const ocpp::v2::Component& connected_ev_component,
                                                         const types::iso15118::EvInformation& ev_information) {
    // We update to true even though it should already be true as a precaution
    this->set_value(connected_ev_component, ocpp::v2::ConnectedEvComponentVariables::Available, "true");
    this->set_value(connected_ev_component, ocpp::v2::ConnectedEvComponentVariables::VehicleId, ev_information.evcc_id);
    this->set_value(connected_ev_component, ocpp::v2::ConnectedEvComponentVariables::ProtocolAgreed,
                    build_supported_protocol_string(ev_information.selected_protocol.protocol_namespace,
                                                    ev_information.selected_protocol.version_number_major,
                                                    ev_information.selected_protocol.version_number_minor));
    this->set_value(connected_ev_component, ocpp::v2::ConnectedEvComponentVariables::VehicleCertificateLeaf,
                    ev_information.tls_leaf_certificate.value_or(""));
    this->set_value(connected_ev_component, ocpp::v2::ConnectedEvComponentVariables::VehicleCertificateSubCa1,
                    ev_information.tls_sub_ca_1_certificate.value_or(""));
    this->set_value(connected_ev_component, ocpp::v2::ConnectedEvComponentVariables::VehicleCertificateSubCa2,
                    ev_information.tls_sub_ca_2_certificate.value_or(""));
    this->set_value(connected_ev_component, ocpp::v2::ConnectedEvComponentVariables::VehicleCertificateRoot,
                    ev_information.tls_root_certificate.value_or(""));
    for (const auto& protocol : ev_information.supported_protocols.Protocols) {
        this->set_value(connected_ev_component,
                        ocpp::v2::ConnectedEvComponentVariables::get_protocol_supported_by_ev(protocol.priority),
                        build_supported_protocol_string(protocol.protocol_namespace, protocol.version_number_major,
                                                        protocol.version_number_minor));
    }
}

void EverestDeviceModel::update_connected_ev_available(const int32_t evse_id, const bool connected) {
    const auto connected_ev_component = get_connected_ev_component(evse_id);
    this->set_value(connected_ev_component, ocpp::v2::ConnectedEvComponentVariables::Available,
                    connected ? "true" : "false");
    if (connected) {
        return;
    }
    for (const auto& variable :
         {ocpp::v2::ConnectedEvComponentVariables::VehicleId, ocpp::v2::ConnectedEvComponentVariables::ProtocolAgreed,
          ocpp::v2::ConnectedEvComponentVariables::VehicleCertificateLeaf,
          ocpp::v2::ConnectedEvComponentVariables::VehicleCertificateSubCa1,
          ocpp::v2::ConnectedEvComponentVariables::VehicleCertificateSubCa2,
          ocpp::v2::ConnectedEvComponentVariables::VehicleCertificateRoot}) {
        this->set_value(connected_ev_component, variable, "");
    }
    for (int i = 1; i <= NUMBER_OF_CONNECTED_EV_PROTOCOLS; ++i) {
        this->set_value(connected_ev_component,
                        ocpp::v2::ConnectedEvComponentVariables::get_protocol_supported_by_ev(i), "");
    }
}

void EverestDeviceModel::update_connected_ev_vehicle_id(const int32_t evse_id, const std::string& vehicle_id) {
    this->set_value(get_connected_ev_component(evse_id), ocpp::v2::ConnectedEvComponentVariables::VehicleId,
                    vehicle_id);
}

void EverestDeviceModel::update_power(const int32_t evse_id, const float total_power_active_import) {
    this->set_value(get_evse_component(evse_id), ocpp::v2::EvseComponentVariables::Power,
                    std::to_string(total_power_active_import));
}

} // namespace ocpp_module_common::device_model
