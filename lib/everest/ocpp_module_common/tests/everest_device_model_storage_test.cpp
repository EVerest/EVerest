// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <filesystem>
#include <map>
#include <optional>
#include <vector>

#include <everest/ocpp_module_common/device_model/everest_device_model_storage.hpp>

#include <generated/types/grid_support.hpp>
#include <generated/types/iso15118.hpp>

#include <ocpp/v2/ctrlr_component_variables.hpp>
#include <ocpp/v2/device_model_storage_sqlite.hpp>
#include <ocpp/v2/init_device_model_db.hpp>
#include <ocpp/v2/ocpp_types.hpp>

namespace {

namespace etm = types::iso15118;
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

// AC_BPT_DER generates an ACDERCtrlr, provisioned disabled (Available "false"/ReadWrite, empty ModesSupported).
TEST(EverestDeviceModelStorageDerTest, AcDerCapableEvseGeneratesAcDerCtrlr) {
    constexpr int32_t evse_id = 1;
    const std::vector<etm::EnergyTransferMode> modes{etm::EnergyTransferMode::AC_single_phase_core,
                                                     etm::EnergyTransferMode::AC_BPT_DER};

    const auto config = ocpp_module_common::device_model::build_der_ctrlr_component_config(evse_id, modes);

    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->first.name, "ACDERCtrlr");
    EXPECT_EQ(config->first.evse_id, evse_id);

    const auto* available = find_variable(config->second, ocpp::v2::DERComponentVariables::Available.name);
    ASSERT_NE(available, nullptr);
    ASSERT_EQ(available->attributes.size(), 1u);
    const auto& available_attr = available->attributes.at(0).variable_attribute;
    ASSERT_TRUE(available_attr.value.has_value());
    EXPECT_EQ(available_attr.value.value().get(), "false");
    ASSERT_TRUE(available_attr.mutability.has_value());
    EXPECT_EQ(available_attr.mutability.value(), ocpp::v2::MutabilityEnum::ReadWrite);

    const auto* modes_supported = find_variable(config->second, ocpp::v2::DERComponentVariables::ModesSupported.name);
    ASSERT_NE(modes_supported, nullptr);
    ASSERT_EQ(modes_supported->attributes.size(), 1u);
    const auto& modes_attr = modes_supported->attributes.at(0).variable_attribute;
    ASSERT_TRUE(modes_attr.value.has_value());
    EXPECT_EQ(modes_attr.value.value().get(), "");
}

// DC_BPT generates a DCDERCtrlr, not an ACDERCtrlr.
TEST(EverestDeviceModelStorageDerTest, DcDerCapableEvseGeneratesDcDerCtrlr) {
    constexpr int32_t evse_id = 1;
    const std::vector<etm::EnergyTransferMode> modes{etm::EnergyTransferMode::DC_extended,
                                                     etm::EnergyTransferMode::DC_BPT};

    const auto config = ocpp_module_common::device_model::build_der_ctrlr_component_config(evse_id, modes);

    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->first.name, "DCDERCtrlr");
    EXPECT_EQ(config->first.evse_id, evse_id);

    const auto* available = find_variable(config->second, ocpp::v2::DERComponentVariables::Available.name);
    ASSERT_NE(available, nullptr);
    ASSERT_EQ(available->attributes.size(), 1u);
    const auto& available_attr = available->attributes.at(0).variable_attribute;
    ASSERT_TRUE(available_attr.value.has_value());
    EXPECT_EQ(available_attr.value.value().get(), "false");
    ASSERT_TRUE(available_attr.mutability.has_value());
    EXPECT_EQ(available_attr.mutability.value(), ocpp::v2::MutabilityEnum::ReadWrite);

    const auto* modes_supported = find_variable(config->second, ocpp::v2::DERComponentVariables::ModesSupported.name);
    ASSERT_NE(modes_supported, nullptr);
    ASSERT_EQ(modes_supported->attributes.size(), 1u);
    const auto& modes_attr = modes_supported->attributes.at(0).variable_attribute;
    ASSERT_TRUE(modes_attr.value.has_value());
    EXPECT_EQ(modes_attr.value.value().get(), "");
}

// With both a DC-DER (DC_BPT) and an AC-DER (AC_BPT_DER) mode, the DC branch takes precedence.
TEST(EverestDeviceModelStorageDerTest, DcDerWinsOverAcDerWhenBothPresent) {
    constexpr int32_t evse_id = 1;
    const std::vector<etm::EnergyTransferMode> modes{etm::EnergyTransferMode::DC_BPT,
                                                     etm::EnergyTransferMode::AC_BPT_DER};

    const auto config = ocpp_module_common::device_model::build_der_ctrlr_component_config(evse_id, modes);

    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->first.name, "DCDERCtrlr");
    EXPECT_EQ(config->first.evse_id, evse_id);

    const auto* available = find_variable(config->second, ocpp::v2::DERComponentVariables::Available.name);
    ASSERT_NE(available, nullptr);
    ASSERT_EQ(available->attributes.size(), 1u);
    const auto& available_attr = available->attributes.at(0).variable_attribute;
    ASSERT_TRUE(available_attr.value.has_value());
    EXPECT_EQ(available_attr.value.value().get(), "false");
    ASSERT_TRUE(available_attr.mutability.has_value());
    EXPECT_EQ(available_attr.mutability.value(), ocpp::v2::MutabilityEnum::ReadWrite);

    const auto* modes_supported = find_variable(config->second, ocpp::v2::DERComponentVariables::ModesSupported.name);
    ASSERT_NE(modes_supported, nullptr);
    ASSERT_EQ(modes_supported->attributes.size(), 1u);
    const auto& modes_attr = modes_supported->attributes.at(0).variable_attribute;
    ASSERT_TRUE(modes_attr.value.has_value());
    EXPECT_EQ(modes_attr.value.value().get(), "");
}

// Non-DER DC mode (DC_core) plus bare AC_DER_IEC falls through to the AC branch. Also covers the bare
// AC_DER_IEC disjunct (the AC test above uses AC_BPT_DER).
TEST(EverestDeviceModelStorageDerTest, NonDcDerWithBareAcDerGeneratesAcDerCtrlr) {
    constexpr int32_t evse_id = 1;
    const std::vector<etm::EnergyTransferMode> modes{etm::EnergyTransferMode::DC_core,
                                                     etm::EnergyTransferMode::AC_DER_IEC};

    const auto config = ocpp_module_common::device_model::build_der_ctrlr_component_config(evse_id, modes);

    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->first.name, "ACDERCtrlr");
    EXPECT_EQ(config->first.evse_id, evse_id);

    const auto* available = find_variable(config->second, ocpp::v2::DERComponentVariables::Available.name);
    ASSERT_NE(available, nullptr);
    ASSERT_EQ(available->attributes.size(), 1u);
    const auto& available_attr = available->attributes.at(0).variable_attribute;
    ASSERT_TRUE(available_attr.value.has_value());
    EXPECT_EQ(available_attr.value.value().get(), "false");
    ASSERT_TRUE(available_attr.mutability.has_value());
    EXPECT_EQ(available_attr.mutability.value(), ocpp::v2::MutabilityEnum::ReadWrite);

    const auto* modes_supported = find_variable(config->second, ocpp::v2::DERComponentVariables::ModesSupported.name);
    ASSERT_NE(modes_supported, nullptr);
    ASSERT_EQ(modes_supported->attributes.size(), 1u);
    const auto& modes_attr = modes_supported->attributes.at(0).variable_attribute;
    ASSERT_TRUE(modes_attr.value.has_value());
    EXPECT_EQ(modes_attr.value.value().get(), "");
}

// Plain AC/DC charging modes (none of AC_DER_IEC/AC_DER_SAE/AC_BPT_DER/DC_BPT/DC_ACDP_BPT) generate no DER controller.
TEST(EverestDeviceModelStorageDerTest, NonDerEvseGeneratesNoDerCtrlr) {
    constexpr int32_t evse_id = 2;
    const std::vector<etm::EnergyTransferMode> modes{etm::EnergyTransferMode::AC_single_phase_core,
                                                     etm::EnergyTransferMode::AC_three_phase_core,
                                                     etm::EnergyTransferMode::DC_extended};

    const auto config = ocpp_module_common::device_model::build_der_ctrlr_component_config(evse_id, modes);

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

// DC config emits ModesSupported and nameplate scalars but never the enabling Available write; the
// Available write is a separate single SetVariableData targeting the DCDERCtrlr component.
TEST(EverestDeviceModelStorageDerTest, DcCapabilityEmitsConfigAndSeparateAvailable) {
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

    // Config carries no enabling Available write.
    EXPECT_EQ(find_set_variable(config, "Available"), nullptr);

    const auto* modes = find_set_variable(config, "ModesSupported");
    ASSERT_NE(modes, nullptr);
    EXPECT_EQ(modes->component.name.get(), "DCDERCtrlr");
    EXPECT_EQ(modes->attributeValue.get(), "VoltVar,FreqDroop");

    const auto* manufacturer = find_set_variable(config, "InverterManufacturer");
    ASSERT_NE(manufacturer, nullptr);
    EXPECT_EQ(manufacturer->attributeValue.get(), "Acme");

    const auto available = ocpp_module_common::device_model::to_der_ctrlr_available_set_variable(1, capability);
    EXPECT_EQ(available.variable.name.get(), "Available");
    EXPECT_EQ(available.component.name.get(), "DCDERCtrlr");
    EXPECT_EQ(available.attributeValue.get(), "true");
}

// AC config emits only ModesSupported (no enabling Available, no nameplate scalars); the Available write
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

    // Config carries no enabling Available write.
    EXPECT_EQ(find_set_variable(config, "Available"), nullptr);

    const auto* modes = find_set_variable(config, "ModesSupported");
    ASSERT_NE(modes, nullptr);
    EXPECT_EQ(modes->component.name.get(), "ACDERCtrlr");
    EXPECT_EQ(modes->attributeValue.get(), "VoltVar");

    // Nameplate scalars have no ACDERCtrlr variable and must not be emitted.
    EXPECT_EQ(find_set_variable(config, "MaxW"), nullptr);
    EXPECT_EQ(find_set_variable(config, "MaxVA"), nullptr);
    EXPECT_EQ(find_set_variable(config, "InverterManufacturer"), nullptr);

    const auto available = ocpp_module_common::device_model::to_der_ctrlr_available_set_variable(2, capability);
    EXPECT_EQ(available.variable.name.get(), "Available");
    EXPECT_EQ(available.component.name.get(), "ACDERCtrlr");
    EXPECT_EQ(available.attributeValue.get(), "true");
}

namespace dm = ocpp_module_common::device_model;

std::filesystem::path make_temp_db_path(const std::string& tag) {
    auto path = std::filesystem::temp_directory_path() / ("ocpp_module_common_der_disable_" + tag + ".db");
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return path;
}

// Builds a device model DB at db_path holding a single DER controller for evse_id, derived from modes.
void init_db_with_der_ctrlr(const std::filesystem::path& db_path, const int32_t evse_id,
                            const std::vector<etm::EnergyTransferMode>& modes) {
    const auto der = dm::build_der_ctrlr_component_config(evse_id, modes);
    ASSERT_TRUE(der.has_value());
    std::map<ocpp::v2::ComponentKey, std::vector<ocpp::v2::DeviceModelVariable>> component_configs;
    component_configs[der->first] = der->second;
    ocpp::v2::InitDeviceModelDb init_db(db_path, DEVICE_MODEL_MIGRATIONS_DIR);
    init_db.initialize_database(component_configs, true);
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

// disable_der_ctrlr forces a persisted DCDERCtrlr Available "true" back to "false".
TEST(EverestDeviceModelStorageDisableDerTest, DcDerCtrlrForcedToUnavailable) {
    const auto db_path = make_temp_db_path("dc");
    init_db_with_der_ctrlr(db_path, 1, {etm::EnergyTransferMode::DC_extended, etm::EnergyTransferMode::DC_BPT});

    ocpp::v2::DeviceModelStorageSqlite storage(db_path);
    const auto cv =
        ocpp::v2::DERComponentVariables::get_dc_component_variable(1, ocpp::v2::DERComponentVariables::Available);
    ASSERT_TRUE(cv.variable.has_value());

    ASSERT_EQ(storage.set_variable_attribute_value(cv.component, cv.variable.value(), ocpp::v2::AttributeEnum::Actual,
                                                   "true", "EVEREST"),
              ocpp::v2::SetVariableStatusEnum::Accepted);
    ASSERT_EQ(read_der_available(storage, cv), "true");

    dm::disable_der_ctrlr(storage, 1);

    EXPECT_EQ(read_der_available(storage, cv), "false");
}

// disable_der_ctrlr forces a persisted ACDERCtrlr Available "true" back to "false".
TEST(EverestDeviceModelStorageDisableDerTest, AcDerCtrlrForcedToUnavailable) {
    const auto db_path = make_temp_db_path("ac");
    init_db_with_der_ctrlr(db_path, 1,
                           {etm::EnergyTransferMode::AC_single_phase_core, etm::EnergyTransferMode::AC_BPT_DER});

    ocpp::v2::DeviceModelStorageSqlite storage(db_path);
    const auto cv =
        ocpp::v2::DERComponentVariables::get_ac_component_variable(1, ocpp::v2::DERComponentVariables::Available);
    ASSERT_TRUE(cv.variable.has_value());

    ASSERT_EQ(storage.set_variable_attribute_value(cv.component, cv.variable.value(), ocpp::v2::AttributeEnum::Actual,
                                                   "true", "EVEREST"),
              ocpp::v2::SetVariableStatusEnum::Accepted);
    ASSERT_EQ(read_der_available(storage, cv), "true");

    dm::disable_der_ctrlr(storage, 1);

    EXPECT_EQ(read_der_available(storage, cv), "false");
}

// An EVSE with no DER component is a silent no-op: no throw, nothing created.
TEST(EverestDeviceModelStorageDisableDerTest, NoDerCtrlrIsSilentNoOp) {
    const auto db_path = make_temp_db_path("noder");
    // The DB only holds a DER controller for evse 1; evse 2 has none.
    init_db_with_der_ctrlr(db_path, 1, {etm::EnergyTransferMode::DC_extended, etm::EnergyTransferMode::DC_BPT});

    ocpp::v2::DeviceModelStorageSqlite storage(db_path);
    EXPECT_NO_THROW(dm::disable_der_ctrlr(storage, 2));

    const auto dc_cv =
        ocpp::v2::DERComponentVariables::get_dc_component_variable(2, ocpp::v2::DERComponentVariables::Available);
    EXPECT_FALSE(read_der_available(storage, dc_cv).has_value());
    const auto ac_cv =
        ocpp::v2::DERComponentVariables::get_ac_component_variable(2, ocpp::v2::DERComponentVariables::Available);
    EXPECT_FALSE(read_der_available(storage, ac_cv).has_value());
}

} // namespace
