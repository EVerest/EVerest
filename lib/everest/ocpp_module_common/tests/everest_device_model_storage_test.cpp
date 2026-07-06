// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <optional>
#include <vector>

#include <everest/ocpp_module_common/device_model/everest_device_model_storage.hpp>

#include <generated/types/grid_support.hpp>
#include <generated/types/iso15118.hpp>

#include <ocpp/v2/ctrlr_component_variables.hpp>
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

} // namespace
