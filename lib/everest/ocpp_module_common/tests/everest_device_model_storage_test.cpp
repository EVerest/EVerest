// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <vector>

#include <everest/ocpp_module_common/device_model/everest_device_model_storage.hpp>

#include <generated/types/evse_manager.hpp>
#include <generated/types/grid_support.hpp>

#include <ocpp/v2/ctrlr_component_variables.hpp>
#include <ocpp/v2/device_model_storage_sqlite.hpp>
#include <ocpp/v2/init_device_model_db.hpp>
#include <ocpp/v2/ocpp_types.hpp>

namespace {

namespace gs = types::grid_support;

const ocpp::v2::DeviceModelVariable* find_variable(const std::vector<ocpp::v2::DeviceModelVariable>& variables,
                                                   const std::string& name) {
    for (const auto& variable : variables) {
        if (variable.name == name) {
            return &variable;
        }
    }
    return nullptr;
}

namespace dmn = ocpp_module_common::device_model;
namespace em = types::evse_manager;

// Builds a connector list from charge modes, all connectors sharing one hlc_capable value. Defaults to
// HLC capable so a row that is only about charge mode does not silently trip the AC HLC gate.
std::vector<em::Connector> connectors(const std::vector<em::ChargeMode>& modes, const bool hlc_capable = true) {
    std::vector<em::Connector> result;
    int32_t id = 1;
    for (const auto mode : modes) {
        em::Connector connector;
        connector.id = id++;
        connector.charge_mode = mode;
        connector.hlc_capable = hlc_capable;
        result.push_back(connector);
    }
    return result;
}

// Per-connector hlc_capable, for the rows where the connectors disagree about HLC.
std::vector<em::Connector> connectors_hlc(const std::vector<std::pair<em::ChargeMode, bool>>& specs) {
    std::vector<em::Connector> result;
    int32_t id = 1;
    for (const auto& [mode, hlc_capable] : specs) {
        em::Connector connector;
        connector.id = id++;
        connector.charge_mode = mode;
        connector.hlc_capable = hlc_capable;
        result.push_back(connector);
    }
    return result;
}

// The component is decided from static facts reported by evse_manager, never from reported energy
// transfer modes: those are published asynchronously and are not waited on, so an EVSE whose modes had
// not arrived by the time the device model was built used to get no DER controller for the lifetime of
// the process, answering every DER message UnknownComponent.
TEST(DerCtrlrComponentTest, DecidedFromWiringAndChargeMode) {
    using dmn::DerCtrlrComponent;
    constexpr auto AC = em::ChargeMode::AC;
    constexpr auto DC = em::ChargeMode::DC;

    // Unwired never gets a component, whatever the connectors say.
    EXPECT_EQ(dmn::der_ctrlr_component(false, connectors({AC})), DerCtrlrComponent::None);
    EXPECT_EQ(dmn::der_ctrlr_component(false, connectors({DC})), DerCtrlrComponent::None);

    // Wired and unanimous, for one connector and for several.
    EXPECT_EQ(dmn::der_ctrlr_component(true, connectors({AC})), DerCtrlrComponent::Ac);
    EXPECT_EQ(dmn::der_ctrlr_component(true, connectors({DC})), DerCtrlrComponent::Dc);
    EXPECT_EQ(dmn::der_ctrlr_component(true, connectors({AC, AC})), DerCtrlrComponent::Ac);
    EXPECT_EQ(dmn::der_ctrlr_component(true, connectors({DC, DC, DC})), DerCtrlrComponent::Dc);

    // Wired but with connectors that disagree: OCPP holds at most one controller per EVSE, so this
    // fails closed rather than guess. Order must not decide it.
    EXPECT_EQ(dmn::der_ctrlr_component(true, connectors({AC, DC})), DerCtrlrComponent::None);
    EXPECT_EQ(dmn::der_ctrlr_component(true, connectors({DC, AC})), DerCtrlrComponent::None);
    EXPECT_EQ(dmn::der_ctrlr_component(true, connectors({AC, AC, DC})), DerCtrlrComponent::None);

    // A connector list the schema forbids is a malformed payload, not a modelled case; it must still
    // not resolve to a component.
    EXPECT_EQ(dmn::der_ctrlr_component(true, connectors({})), DerCtrlrComponent::None);
}

// AC DER is reachable only over ISO 15118-20, so the AC arm additionally requires HLC. The DC arm does
// not: EvseManager refuses to start a DC EVSE without HLC, so a DC connector reporting no HLC could not
// have booted, and gating it there would be unreachable code.
TEST(DerCtrlrComponentTest, AcRequiresHlcAndDcDoesNot) {
    using dmn::DerCtrlrComponent;
    constexpr auto AC = em::ChargeMode::AC;
    constexpr auto DC = em::ChargeMode::DC;

    EXPECT_EQ(dmn::der_ctrlr_component(true, connectors({AC}, /*hlc_capable=*/false)), DerCtrlrComponent::None);
    EXPECT_EQ(dmn::der_ctrlr_component(true, connectors({AC, AC}, /*hlc_capable=*/false)), DerCtrlrComponent::None);
    EXPECT_EQ(dmn::der_ctrlr_component(true, connectors({AC}, /*hlc_capable=*/true)), DerCtrlrComponent::Ac);

    // Unreachable in production but asserted so the asymmetry is not silently "fixed" later.
    EXPECT_EQ(dmn::der_ctrlr_component(true, connectors({DC}, /*hlc_capable=*/false)), DerCtrlrComponent::Dc);

    // One HLC capable connector is enough: the ISO 15118-20 session runs on that plug.
    EXPECT_EQ(dmn::der_ctrlr_component(true, connectors_hlc({{AC, false}, {AC, true}})), DerCtrlrComponent::Ac);
    EXPECT_EQ(dmn::der_ctrlr_component(true, connectors_hlc({{AC, true}, {AC, false}})), DerCtrlrComponent::Ac);
}

// Builds an Evse carrying the given connector charge modes.
em::Evse evse_with(const std::int32_t evse_id, const std::vector<em::ChargeMode>& modes) {
    em::Evse evse;
    evse.id = evse_id;
    evse.connectors = connectors(modes);
    return evse;
}

// The whole-station decision. with_der_components false is the OCPP 1.6 case: no EVSE gets an entry at
// all, so nothing is provisioned and nothing is cleared. Otherwise every EVSE gets an entry, including
// the ones that resolve to None, because the clearing pass that runs after the storage opens is driven
// off this map and has to know about them.
TEST(DecideDerCtrlrComponentsTest, WithDerComponentsFalseDecidesNothing) {
    constexpr auto AC = em::ChargeMode::AC;

    const std::vector<em::Evse> evses{evse_with(1, {AC}), evse_with(2, {AC})};

    EXPECT_TRUE(dmn::decide_der_ctrlr_components(evses, {1, 2}, /*with_der_components=*/false).empty());
}

TEST(DecideDerCtrlrComponentsTest, EachEvseDecidedFromItsOwnWiringAndConnectors) {
    using dmn::DerCtrlrComponent;
    constexpr auto AC = em::ChargeMode::AC;
    constexpr auto DC = em::ChargeMode::DC;

    const std::vector<em::Evse> evses{
        evse_with(1, {AC}),     // wired, AC
        evse_with(2, {DC}),     // wired, DC
        evse_with(3, {AC}),     // not wired
        evse_with(4, {AC, DC}), // wired, but disagreeing
    };

    const auto decided = dmn::decide_der_ctrlr_components(evses, {1, 2, 4}, /*with_der_components=*/true);

    // Every EVSE is present, so the clearing pass covers the None ones too.
    EXPECT_EQ(decided.size(), 4u);
    EXPECT_EQ(decided.at(1), DerCtrlrComponent::Ac);
    EXPECT_EQ(decided.at(2), DerCtrlrComponent::Dc);
    EXPECT_EQ(decided.at(3), DerCtrlrComponent::None);
    EXPECT_EQ(decided.at(4), DerCtrlrComponent::None);
}

// Grid_support connections mapped to EVSEs this station does not serve must not invent entries, and the
// served EVSE must survive the diagnostic walk unchanged. Two unserved ids so a walk that stops at the
// first miss is caught as far as the map allows. This file has no log capture, so the message text is not
// asserted.
TEST(DecideDerCtrlrComponentsTest, WiredIdWithNoMatchingEvseIsIgnored) {
    constexpr auto AC = em::ChargeMode::AC;

    const auto decided =
        dmn::decide_der_ctrlr_components({evse_with(1, {AC})}, {1, 7, 9}, /*with_der_components=*/true);

    EXPECT_EQ(decided.size(), 1u);
    EXPECT_EQ(decided.count(7), 0u);
    EXPECT_EQ(decided.count(9), 0u);
    EXPECT_EQ(decided.at(1), dmn::DerCtrlrComponent::Ac);
}

TEST(DecideDerCtrlrComponentsTest, NoEvsesDecidesNothing) {
    EXPECT_TRUE(dmn::decide_der_ctrlr_components({}, {1}, /*with_der_components=*/true).empty());
}

// The storage constructor cannot be built in a unit test: ConfigServiceClient is concrete and needs a live
// MQTTAbstraction. So its provisioning pass is covered through build_der_component_configs here, and its
// clearing pass through disable_other_der_ctrlrs below.
TEST(BuildDerComponentConfigsTest, WithDerComponentsFalseBuildsNothing) {
    constexpr auto AC = em::ChargeMode::AC;

    const std::vector<em::Evse> evses{evse_with(1, {AC}), evse_with(2, {AC})};

    EXPECT_TRUE(dmn::build_der_component_configs(evses, {1, 2}, /*with_der_components=*/false).empty());
}

TEST(BuildDerComponentConfigsTest, AcWiredEvseGetsAcDerCtrlr) {
    constexpr auto AC = em::ChargeMode::AC;

    const auto configs = dmn::build_der_component_configs({evse_with(1, {AC})}, {1}, /*with_der_components=*/true);

    ASSERT_EQ(configs.size(), 1u);
    const auto& key = configs.cbegin()->first;
    EXPECT_EQ(key.name, "ACDERCtrlr");
    EXPECT_EQ(key.evse_id, 1);
}

TEST(BuildDerComponentConfigsTest, DcWiredEvseGetsDcDerCtrlr) {
    constexpr auto DC = em::ChargeMode::DC;

    const auto configs = dmn::build_der_component_configs({evse_with(1, {DC})}, {1}, /*with_der_components=*/true);

    ASSERT_EQ(configs.size(), 1u);
    const auto& key = configs.cbegin()->first;
    EXPECT_EQ(key.name, "DCDERCtrlr");
    EXPECT_EQ(key.evse_id, 1);
}

TEST(BuildDerComponentConfigsTest, UnwiredEvseGetsNoEntry) {
    constexpr auto AC = em::ChargeMode::AC;

    EXPECT_TRUE(dmn::build_der_component_configs({evse_with(1, {AC})}, {}, /*with_der_components=*/true).empty());
}

TEST(BuildDerComponentConfigsTest, AcWiredEvseWithoutHlcGetsNoEntry) {
    constexpr auto AC = em::ChargeMode::AC;

    em::Evse evse;
    evse.id = 1;
    evse.connectors = connectors({AC}, /*hlc_capable=*/false);

    EXPECT_TRUE(dmn::build_der_component_configs({evse}, {1}, /*with_der_components=*/true).empty());
}

TEST(BuildDerComponentConfigsTest, EachEvseKeyedByItsOwnId) {
    constexpr auto AC = em::ChargeMode::AC;
    constexpr auto DC = em::ChargeMode::DC;

    const std::vector<em::Evse> evses{
        evse_with(1, {AC}), // wired, AC
        evse_with(2, {DC}), // wired, DC
        evse_with(3, {AC}), // not wired
    };

    const auto configs = dmn::build_der_component_configs(evses, {1, 2}, /*with_der_components=*/true);

    ASSERT_EQ(configs.size(), 2u);
    std::map<int32_t, std::string> by_evse_id;
    for (const auto& [key, variables] : configs) {
        ASSERT_TRUE(key.evse_id.has_value());
        // A component provisioning no variables would have neither Available, Enabled nor ModesSupported.
        ASSERT_FALSE(variables.empty());
        by_evse_id[key.evse_id.value()] = key.name;
    }
    EXPECT_EQ(by_evse_id.at(1), "ACDERCtrlr");
    EXPECT_EQ(by_evse_id.at(2), "DCDERCtrlr");
    EXPECT_EQ(by_evse_id.count(3), 0u);
}

// Two EVSEs resolving to the same component only stay distinct because evse_id is part of the key. Were it
// dropped, the CSMS would see one ACDERCtrlr and EVSE 2 would answer every DER message UnknownComponent.
TEST(BuildDerComponentConfigsTest, TwoAcEvsesEachGetTheirOwnAcDerCtrlr) {
    constexpr auto AC = em::ChargeMode::AC;

    const std::vector<em::Evse> evses{evse_with(1, {AC}), evse_with(2, {AC})};

    const auto configs = dmn::build_der_component_configs(evses, {1, 2}, /*with_der_components=*/true);

    ASSERT_EQ(configs.size(), 2u);
    std::set<int32_t> evse_ids;
    for (const auto& [key, variables] : configs) {
        EXPECT_EQ(key.name, "ACDERCtrlr");
        ASSERT_TRUE(key.evse_id.has_value());
        evse_ids.insert(key.evse_id.value());
    }
    EXPECT_EQ(evse_ids, (std::set<int32_t>{1, 2}));
}

// The Ac component carries static presence (Available "true"/ReadOnly) and a runtime Enabled control
// (provisioned "true"/ReadWrite), with empty ModesSupported.
TEST(EverestDeviceModelStorageDerTest, AcComponentGeneratesAcDerCtrlr) {
    constexpr int32_t evse_id = 1;

    const auto config = dmn::build_der_ctrlr_component_config(evse_id, dmn::DerCtrlrComponent::Ac);

    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->first.name, "ACDERCtrlr");
    EXPECT_EQ(config->first.evse_id, evse_id);

    const auto* available = find_variable(config->second, ocpp::v2::DERComponentVariables::Available.name);
    ASSERT_NE(available, nullptr);
    ASSERT_EQ(available->attributes.size(), 1u);
    const auto& available_attr = available->attributes.at(0).variable_attribute;
    ASSERT_TRUE(available_attr.value.has_value());
    EXPECT_EQ(available_attr.value.value().get(), "true");
    ASSERT_TRUE(available_attr.mutability.has_value());
    EXPECT_EQ(available_attr.mutability.value(), ocpp::v2::MutabilityEnum::ReadOnly);

    const auto* enabled = find_variable(config->second, ocpp::v2::DERComponentVariables::Enabled.name);
    ASSERT_NE(enabled, nullptr);
    ASSERT_EQ(enabled->attributes.size(), 1u);
    const auto& enabled_attr = enabled->attributes.at(0).variable_attribute;
    ASSERT_TRUE(enabled_attr.value.has_value());
    EXPECT_EQ(enabled_attr.value.value().get(), "true");
    ASSERT_TRUE(enabled_attr.mutability.has_value());
    EXPECT_EQ(enabled_attr.mutability.value(), ocpp::v2::MutabilityEnum::ReadWrite);

    const auto* modes_supported = find_variable(config->second, ocpp::v2::DERComponentVariables::ModesSupported.name);
    ASSERT_NE(modes_supported, nullptr);
    ASSERT_EQ(modes_supported->attributes.size(), 1u);
    const auto& modes_attr = modes_supported->attributes.at(0).variable_attribute;
    ASSERT_TRUE(modes_attr.value.has_value());
    EXPECT_EQ(modes_attr.value.value().get(), "");
}

// The Dc component generates a DCDERCtrlr, not an ACDERCtrlr.
TEST(EverestDeviceModelStorageDerTest, DcComponentGeneratesDcDerCtrlr) {
    constexpr int32_t evse_id = 1;

    const auto config = dmn::build_der_ctrlr_component_config(evse_id, dmn::DerCtrlrComponent::Dc);

    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->first.name, "DCDERCtrlr");
    EXPECT_EQ(config->first.evse_id, evse_id);

    const auto* available = find_variable(config->second, ocpp::v2::DERComponentVariables::Available.name);
    ASSERT_NE(available, nullptr);
    ASSERT_EQ(available->attributes.size(), 1u);
    const auto& available_attr = available->attributes.at(0).variable_attribute;
    ASSERT_TRUE(available_attr.value.has_value());
    EXPECT_EQ(available_attr.value.value().get(), "true");
    ASSERT_TRUE(available_attr.mutability.has_value());
    EXPECT_EQ(available_attr.mutability.value(), ocpp::v2::MutabilityEnum::ReadOnly);

    const auto* enabled = find_variable(config->second, ocpp::v2::DERComponentVariables::Enabled.name);
    ASSERT_NE(enabled, nullptr);
    ASSERT_EQ(enabled->attributes.size(), 1u);
    const auto& enabled_attr = enabled->attributes.at(0).variable_attribute;
    ASSERT_TRUE(enabled_attr.value.has_value());
    EXPECT_EQ(enabled_attr.value.value().get(), "true");
    ASSERT_TRUE(enabled_attr.mutability.has_value());
    EXPECT_EQ(enabled_attr.mutability.value(), ocpp::v2::MutabilityEnum::ReadWrite);
}

// The None component generates nothing, which is what an unwired EVSE resolves to.
TEST(EverestDeviceModelStorageDerTest, NoneComponentGeneratesNoDerCtrlr) {
    constexpr int32_t evse_id = 2;

    const auto config = dmn::build_der_ctrlr_component_config(evse_id, dmn::DerCtrlrComponent::None);

    EXPECT_FALSE(config.has_value());
}

const ocpp::v2::SetVariableData* find_set_variable(const std::vector<ocpp::v2::SetVariableData>& vars,
                                                   const std::string& name) {
    for (const auto& v : vars) {
        if (v.variable.name.get() == name) {
            return &v;
        }
    }
    return nullptr;
}

// DC config emits ModesSupported and nameplate scalars but never the enabling Enabled write; the
// Enabled write is a separate single SetVariableData targeting the DCDERCtrlr component.
TEST(EverestDeviceModelStorageDerTest, DcCapabilityEmitsConfigAndSeparateEnabled) {
    gs::DERCapability capability;
    capability.supported_types = {gs::DirectiveType::VoltVar, gs::DirectiveType::FreqDroop};
    capability.nameplate.max_w_W = 11000.0f;
    capability.nameplate.max_va_VA = 12000.0f;
    capability.nameplate.v_nom_V = 230.0f;
    gs::DCCapability dc;
    gs::DeviceInfo info;
    info.manufacturer = "Acme";
    dc.device_info = info;
    capability.dc = dc;

    const auto config = ocpp_module_common::device_model::to_der_ctrlr_config_set_variables(1, capability);

    // Config carries no enabling Enabled write.
    EXPECT_EQ(find_set_variable(config, "Enabled"), nullptr);

    const auto* modes = find_set_variable(config, "ModesSupported");
    ASSERT_NE(modes, nullptr);
    EXPECT_EQ(modes->component.name.get(), "DCDERCtrlr");
    EXPECT_EQ(modes->attributeValue.get(), "VoltVar,FreqDroop");

    const auto* manufacturer = find_set_variable(config, "InverterManufacturer");
    ASSERT_NE(manufacturer, nullptr);
    EXPECT_EQ(manufacturer->attributeValue.get(), "Acme");
}

// AC config emits only ModesSupported (no enabling Enabled, no nameplate scalars); the Enabled write
// targets the ACDERCtrlr component.
TEST(EverestDeviceModelStorageDerTest, AcCapabilityUsesAcComponent) {
    gs::DERCapability capability;
    capability.supported_types = {gs::DirectiveType::VoltVar};
    capability.nameplate.max_w_W = 7400.0f;
    capability.nameplate.max_va_VA = 7400.0f;
    capability.nameplate.v_nom_V = 230.0f;
    gs::ACCapability ac;
    ac.phase_count = 3;
    capability.ac = ac;

    const auto config = ocpp_module_common::device_model::to_der_ctrlr_config_set_variables(2, capability);

    // Config carries no enabling Enabled write.
    EXPECT_EQ(find_set_variable(config, "Enabled"), nullptr);

    const auto* modes = find_set_variable(config, "ModesSupported");
    ASSERT_NE(modes, nullptr);
    EXPECT_EQ(modes->component.name.get(), "ACDERCtrlr");
    EXPECT_EQ(modes->attributeValue.get(), "VoltVar");

    // Nameplate scalars have no ACDERCtrlr variable and must not be emitted.
    EXPECT_EQ(find_set_variable(config, "MaxW"), nullptr);
    EXPECT_EQ(find_set_variable(config, "MaxVA"), nullptr);
    EXPECT_EQ(find_set_variable(config, "InverterManufacturer"), nullptr);
}

namespace dm = ocpp_module_common::device_model;

std::filesystem::path make_temp_db_path(const std::string& tag) {
    auto path = std::filesystem::temp_directory_path() / ("ocpp_module_common_der_disable_" + tag + ".db");
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return path;
}

// Builds a device model DB at db_path holding a single DER controller of the given component for evse_id.
// fresh=false reuses an existing DB, which is how a re-provisioning boot is simulated.
void init_db_with_der_ctrlr(const std::filesystem::path& db_path, const int32_t evse_id,
                            const dm::DerCtrlrComponent component, const bool fresh = true) {
    const auto der = dm::build_der_ctrlr_component_config(evse_id, component);
    ASSERT_TRUE(der.has_value());
    std::map<ocpp::v2::ComponentKey, std::vector<ocpp::v2::DeviceModelVariable>> component_configs;
    component_configs[der->first] = der->second;
    ocpp::v2::InitDeviceModelDb init_db(db_path, DEVICE_MODEL_MIGRATIONS_DIR);
    init_db.initialize_database(component_configs, fresh);
    init_db.close_connection();
}

std::optional<std::string> read_der_available(ocpp::v2::DeviceModelStorageInterface& storage,
                                              const ocpp::v2::ComponentVariable& cv) {
    const auto attr =
        storage.get_variable_attribute(cv.component, cv.variable.value(), ocpp::v2::AttributeEnum::Actual);
    if (not attr.has_value() or not attr->value.has_value()) {
        return std::nullopt;
    }
    return attr->value.value().get();
}

// Clearing with keep=None forces both a persisted DCDERCtrlr Available "true" and Enabled "true" back to "false".
TEST(EverestDeviceModelStorageDisableDerTest, DcDerCtrlrForcedToUnavailable) {
    const auto db_path = make_temp_db_path("dc");
    init_db_with_der_ctrlr(db_path, 1, dm::DerCtrlrComponent::Dc);

    ocpp::v2::DeviceModelStorageSqlite storage(db_path);
    const auto available_cv =
        ocpp::v2::DERComponentVariables::get_dc_component_variable(1, ocpp::v2::DERComponentVariables::Available);
    const auto enabled_cv =
        ocpp::v2::DERComponentVariables::get_dc_component_variable(1, ocpp::v2::DERComponentVariables::Enabled);
    ASSERT_TRUE(available_cv.variable.has_value());
    ASSERT_TRUE(enabled_cv.variable.has_value());

    ASSERT_EQ(storage.set_variable_attribute_value(available_cv.component, available_cv.variable.value(),
                                                   ocpp::v2::AttributeEnum::Actual, "true", "EVEREST"),
              ocpp::v2::SetVariableStatusEnum::Accepted);
    ASSERT_EQ(storage.set_variable_attribute_value(enabled_cv.component, enabled_cv.variable.value(),
                                                   ocpp::v2::AttributeEnum::Actual, "true", "EVEREST"),
              ocpp::v2::SetVariableStatusEnum::Accepted);
    ASSERT_EQ(read_der_available(storage, available_cv), "true");
    ASSERT_EQ(read_der_available(storage, enabled_cv), "true");

    dm::disable_other_der_ctrlrs(storage, 1, dm::DerCtrlrComponent::None);

    EXPECT_EQ(read_der_available(storage, available_cv), "false");
    EXPECT_EQ(read_der_available(storage, enabled_cv), "false");
}

// Clearing with keep=None forces both a persisted ACDERCtrlr Available "true" and Enabled "true" back to "false".
TEST(EverestDeviceModelStorageDisableDerTest, AcDerCtrlrForcedToUnavailable) {
    const auto db_path = make_temp_db_path("ac");
    init_db_with_der_ctrlr(db_path, 1, dm::DerCtrlrComponent::Ac);

    ocpp::v2::DeviceModelStorageSqlite storage(db_path);
    const auto available_cv =
        ocpp::v2::DERComponentVariables::get_ac_component_variable(1, ocpp::v2::DERComponentVariables::Available);
    const auto enabled_cv =
        ocpp::v2::DERComponentVariables::get_ac_component_variable(1, ocpp::v2::DERComponentVariables::Enabled);
    ASSERT_TRUE(available_cv.variable.has_value());
    ASSERT_TRUE(enabled_cv.variable.has_value());

    ASSERT_EQ(storage.set_variable_attribute_value(available_cv.component, available_cv.variable.value(),
                                                   ocpp::v2::AttributeEnum::Actual, "true", "EVEREST"),
              ocpp::v2::SetVariableStatusEnum::Accepted);
    ASSERT_EQ(storage.set_variable_attribute_value(enabled_cv.component, enabled_cv.variable.value(),
                                                   ocpp::v2::AttributeEnum::Actual, "true", "EVEREST"),
              ocpp::v2::SetVariableStatusEnum::Accepted);
    ASSERT_EQ(read_der_available(storage, available_cv), "true");
    ASSERT_EQ(read_der_available(storage, enabled_cv), "true");

    dm::disable_other_der_ctrlrs(storage, 1, dm::DerCtrlrComponent::None);

    EXPECT_EQ(read_der_available(storage, available_cv), "false");
    EXPECT_EQ(read_der_available(storage, enabled_cv), "false");
}

// The guard only overwrites a persisted "true": an Enabled already at "false" (e.g. a CSMS-written
// disable) is left untouched so its source marker survives an unwire/rewire cycle.
TEST(EverestDeviceModelStorageDisableDerTest, EnabledAlreadyFalseLeftUntouched) {
    const auto db_path = make_temp_db_path("enabled_false");
    init_db_with_der_ctrlr(db_path, 1, dm::DerCtrlrComponent::Dc);

    ocpp::v2::DeviceModelStorageSqlite storage(db_path);
    const auto enabled_cv =
        ocpp::v2::DERComponentVariables::get_dc_component_variable(1, ocpp::v2::DERComponentVariables::Enabled);
    ASSERT_TRUE(enabled_cv.variable.has_value());

    ASSERT_EQ(storage.set_variable_attribute_value(enabled_cv.component, enabled_cv.variable.value(),
                                                   ocpp::v2::AttributeEnum::Actual, "false", "CSMS"),
              ocpp::v2::SetVariableStatusEnum::Accepted);
    ASSERT_EQ(read_der_available(storage, enabled_cv), "false");

    dm::disable_other_der_ctrlrs(storage, 1, dm::DerCtrlrComponent::None);

    EXPECT_EQ(read_der_available(storage, enabled_cv), "false");
}

// An EVSE with no DER component is a silent no-op: no throw, nothing created.
TEST(EverestDeviceModelStorageDisableDerTest, NoDerCtrlrIsSilentNoOp) {
    const auto db_path = make_temp_db_path("noder");
    // The DB only holds a DER controller for evse 1; evse 2 has none.
    init_db_with_der_ctrlr(db_path, 1, dm::DerCtrlrComponent::Dc);

    ocpp::v2::DeviceModelStorageSqlite storage(db_path);
    EXPECT_NO_THROW(dm::disable_other_der_ctrlrs(storage, 2, dm::DerCtrlrComponent::None));

    const auto dc_cv =
        ocpp::v2::DERComponentVariables::get_dc_component_variable(2, ocpp::v2::DERComponentVariables::Available);
    EXPECT_FALSE(read_der_available(storage, dc_cv).has_value());
    const auto ac_cv =
        ocpp::v2::DERComponentVariables::get_ac_component_variable(2, ocpp::v2::DERComponentVariables::Available);
    EXPECT_FALSE(read_der_available(storage, ac_cv).has_value());
}

// Regression: InitDeviceModelDb keeps components that vanish from the config, so an EVSE that resolved to
// one component on an earlier boot and to the other now would carry both, each still Available "true".
// disable_other_der_ctrlrs is what makes provisioning total: at most one available controller per EVSE.
TEST(EverestDeviceModelStorageDisableDerTest, ReprovisioningLeavesOneAvailableComponent) {
    const auto db_path = make_temp_db_path("reprovision");
    const auto ac_available =
        ocpp::v2::DERComponentVariables::get_ac_component_variable(1, ocpp::v2::DERComponentVariables::Available);
    const auto dc_available =
        ocpp::v2::DERComponentVariables::get_dc_component_variable(1, ocpp::v2::DERComponentVariables::Available);

    // An earlier boot resolved this EVSE to the AC component.
    init_db_with_der_ctrlr(db_path, 1, dm::DerCtrlrComponent::Ac);
    // This boot resolves it to DC, onto the same database.
    init_db_with_der_ctrlr(db_path, 1, dm::DerCtrlrComponent::Dc, /*fresh=*/false);

    ocpp::v2::DeviceModelStorageSqlite storage(db_path);
    ASSERT_EQ(read_der_available(storage, ac_available), "true") << "precondition: the stale component survives";
    ASSERT_EQ(read_der_available(storage, dc_available), "true");

    dm::disable_other_der_ctrlrs(storage, 1, dm::DerCtrlrComponent::Dc);

    EXPECT_EQ(read_der_available(storage, dc_available), "true");
    EXPECT_EQ(read_der_available(storage, ac_available), "false");
}

// Mirror of the above with the roles swapped, so the Ac arm of the keep switch is gated too: a boot that
// resolves to AC must clear the DC controller an earlier boot left available.
TEST(EverestDeviceModelStorageDisableDerTest, ReprovisioningToAcLeavesOneAvailableComponent) {
    const auto db_path = make_temp_db_path("reprovision_ac");
    const auto ac_available =
        ocpp::v2::DERComponentVariables::get_ac_component_variable(1, ocpp::v2::DERComponentVariables::Available);
    const auto dc_available =
        ocpp::v2::DERComponentVariables::get_dc_component_variable(1, ocpp::v2::DERComponentVariables::Available);

    // An earlier boot resolved this EVSE to the DC component.
    init_db_with_der_ctrlr(db_path, 1, dm::DerCtrlrComponent::Dc);
    // This boot resolves it to AC, onto the same database.
    init_db_with_der_ctrlr(db_path, 1, dm::DerCtrlrComponent::Ac, /*fresh=*/false);

    ocpp::v2::DeviceModelStorageSqlite storage(db_path);
    ASSERT_EQ(read_der_available(storage, dc_available), "true") << "precondition: the stale component survives";
    ASSERT_EQ(read_der_available(storage, ac_available), "true");

    dm::disable_other_der_ctrlrs(storage, 1, dm::DerCtrlrComponent::Ac);

    EXPECT_EQ(read_der_available(storage, ac_available), "true");
    EXPECT_EQ(read_der_available(storage, dc_available), "false");
}

// None clears both, so an EVSE that lost its grid_support wiring keeps no available controller.
TEST(EverestDeviceModelStorageDisableDerTest, NoneComponentClearsBoth) {
    const auto db_path = make_temp_db_path("reprovision_none");
    init_db_with_der_ctrlr(db_path, 1, dm::DerCtrlrComponent::Ac);

    ocpp::v2::DeviceModelStorageSqlite storage(db_path);
    dm::disable_other_der_ctrlrs(storage, 1, dm::DerCtrlrComponent::None);

    const auto ac_available =
        ocpp::v2::DERComponentVariables::get_ac_component_variable(1, ocpp::v2::DERComponentVariables::Available);
    EXPECT_EQ(read_der_available(storage, ac_available), "false");
}

} // namespace
