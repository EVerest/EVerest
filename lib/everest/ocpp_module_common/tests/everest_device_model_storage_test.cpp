// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <filesystem>
#include <map>
#include <optional>
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

// The component is decided from two static facts, never from reported energy transfer modes: those are
// published asynchronously and are not waited on, so an EVSE whose modes had not arrived by the time the
// device model was built used to get no DER controller for the lifetime of the process, answering every
// DER message UnknownComponent. Unwired or unknown charge mode fails closed.
TEST(DerCtrlrComponentTest, DecidedFromWiringAndChargeMode) {
    using dmn::DerCtrlrComponent;

    EXPECT_EQ(dmn::der_ctrlr_component(false, em::ChargeMode::AC), DerCtrlrComponent::None);
    EXPECT_EQ(dmn::der_ctrlr_component(false, em::ChargeMode::DC), DerCtrlrComponent::None);
    EXPECT_EQ(dmn::der_ctrlr_component(false, std::nullopt), DerCtrlrComponent::None);
    EXPECT_EQ(dmn::der_ctrlr_component(true, em::ChargeMode::AC), DerCtrlrComponent::Ac);
    EXPECT_EQ(dmn::der_ctrlr_component(true, em::ChargeMode::DC), DerCtrlrComponent::Dc);
    EXPECT_EQ(dmn::der_ctrlr_component(true, std::nullopt), DerCtrlrComponent::None);
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

// disable_der_ctrlr forces both a persisted DCDERCtrlr Available "true" and Enabled "true" back to "false".
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

    dm::disable_der_ctrlr(storage, 1);

    EXPECT_EQ(read_der_available(storage, available_cv), "false");
    EXPECT_EQ(read_der_available(storage, enabled_cv), "false");
}

// disable_der_ctrlr forces both a persisted ACDERCtrlr Available "true" and Enabled "true" back to "false".
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

    dm::disable_der_ctrlr(storage, 1);

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

    dm::disable_der_ctrlr(storage, 1);

    EXPECT_EQ(read_der_available(storage, enabled_cv), "false");
}

// An EVSE with no DER component is a silent no-op: no throw, nothing created.
TEST(EverestDeviceModelStorageDisableDerTest, NoDerCtrlrIsSilentNoOp) {
    const auto db_path = make_temp_db_path("noder");
    // The DB only holds a DER controller for evse 1; evse 2 has none.
    init_db_with_der_ctrlr(db_path, 1, dm::DerCtrlrComponent::Dc);

    ocpp::v2::DeviceModelStorageSqlite storage(db_path);
    EXPECT_NO_THROW(dm::disable_der_ctrlr(storage, 2));

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
