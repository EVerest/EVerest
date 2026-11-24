// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

#include <ocpp/v16/variable_resolver.hpp>
#include <ocpp/v2/comparators.hpp>

#include <v16_key_binder.hpp>

#include "stubs/device_model_stub.hpp"

namespace {

using ocpp::v2::RegistrationRejectionReason;
using ocpp_module_common::device_model::DeclaredOcpp16Key;

ocpp::v2::Component make_component(const std::string& name) {
    ocpp::v2::Component component;
    component.name = name;
    return component;
}

ocpp::v2::Variable make_variable(const std::string& name) {
    ocpp::v2::Variable variable;
    variable.name = name;
    return variable;
}

ocpp::v2::Ocpp16KeyMappings file_mappings() {
    return {{"FileKey", {make_component("VendorCtrlr"), make_variable("FileSetting")}}};
}

class V16KeyBinderTest : public ::testing::Test {
protected:
    V16KeyBinderTest() : m_resolver(file_mappings()), m_binder(m_resolver, m_device_model, file_mappings()) {
        m_device_model.add(make_component("OCPP16LegacyCtrlr"), make_variable("LightIntensity"), "12");
        m_device_model.set_data_type(make_component("OCPP16LegacyCtrlr"), make_variable("LightIntensity"),
                                     ocpp::v2::DataEnum::integer);
    }

    /// \brief Binds \p key for the variable \p variable of \p component, registered by \p owner with \p data_type
    std::vector<ocpp::v2::RejectedVariable> bind(const std::string& owner, const std::string& component,
                                                 const std::string& variable, const std::string& key,
                                                 ocpp::v2::DataEnum data_type = ocpp::v2::DataEnum::integer,
                                                 bool with_actual = true) {
        ocpp::v2::VariableAttribute attribute;
        attribute.type = with_actual ? ocpp::v2::AttributeEnum::Actual : ocpp::v2::AttributeEnum::Target;
        attribute.value = "1";
        ocpp::v2::RegisteredVariable registered;
        registered.variable = make_variable(variable);
        registered.characteristics.dataType = data_type;
        registered.attributes.push_back(attribute);
        const std::vector<ocpp::v2::RegisteredComponent> components{{make_component(component), {registered}}};
        return m_binder.bind(owner, components,
                             {DeclaredOcpp16Key{make_component(component), make_variable(variable), key}});
    }

    static std::optional<RegistrationRejectionReason> reason(const std::vector<ocpp::v2::RejectedVariable>& rejected) {
        if (rejected.empty()) {
            return std::nullopt;
        }
        return rejected.front().reason;
    }

    stubs::DeviceModelStub m_device_model;
    ocpp::v16::VariableResolver m_resolver;
    ocpp_multi::V16KeyBinder m_binder;
};

TEST_F(V16KeyBinderTest, CustomKeyIsMappedToTheVariable) {
    EXPECT_TRUE(bind("cooling", "CoolingUnit", "FanSpeed", "FanKey").empty());
    const auto cv = m_resolver.key_to_cv("FanKey");
    ASSERT_TRUE(cv.has_value());
    EXPECT_EQ(cv->first.name.get(), "CoolingUnit");
    EXPECT_EQ(cv->second.name.get(), "FanSpeed");
}

TEST_F(V16KeyBinderTest, KeyAndVariableNamedInTheFileAreInUse) {
    EXPECT_EQ(reason(bind("cooling", "CoolingUnit", "FanSpeed", "FileKey")),
              RegistrationRejectionReason::Ocpp16KeyInUse);
    EXPECT_EQ(reason(bind("vendor", "VendorCtrlr", "FileSetting", "OwnerKey")),
              RegistrationRejectionReason::Ocpp16KeyInUse);
    EXPECT_FALSE(m_resolver.key_to_cv("OwnerKey").has_value());
}

// A file entry naming a target that no longer qualifies is reserved all the same
TEST_F(V16KeyBinderTest, FileReservesItsNamesWhetherUsableOrNot) {
    m_resolver.replace_file_mappings({});
    EXPECT_EQ(reason(bind("cooling", "CoolingUnit", "FanSpeed", "FileKey")),
              RegistrationRejectionReason::Ocpp16KeyInUse);
}

TEST_F(V16KeyBinderTest, FileKeyOnTheSameVariableIsLeftToTheFile) {
    EXPECT_TRUE(bind("vendor", "VendorCtrlr", "FileSetting", "FileKey").empty());
    m_binder.unbind("vendor");
    EXPECT_EQ(m_resolver.cv_to_key(make_component("VendorCtrlr"), make_variable("FileSetting")), "FileKey");
}

TEST_F(V16KeyBinderTest, KeyOfAnotherOwnerIsInUse) {
    ASSERT_TRUE(bind("first", "CoolingUnit", "FanSpeed", "FanKey").empty());
    EXPECT_EQ(reason(bind("second", "OtherUnit", "FanSpeed", "FanKey")), RegistrationRejectionReason::Ocpp16KeyInUse);
    EXPECT_EQ(m_resolver.key_to_cv("FanKey")->first.name.get(), "CoolingUnit");
}

TEST_F(V16KeyBinderTest, BindingAgainReplacesTheKeysOfTheOwner) {
    ASSERT_TRUE(bind("cooling", "CoolingUnit", "FanSpeed", "FirstKey").empty());
    ASSERT_TRUE(bind("cooling", "CoolingUnit", "FanSpeed", "SecondKey").empty());
    EXPECT_FALSE(m_resolver.key_to_cv("FirstKey").has_value());
    EXPECT_TRUE(m_resolver.key_to_cv("SecondKey").has_value());
}

TEST_F(V16KeyBinderTest, UnbindingRemovesTheKeys) {
    ASSERT_TRUE(bind("cooling", "CoolingUnit", "FanSpeed", "FanKey").empty());
    m_binder.unbind_variables(
        "cooling",
        {{make_component("CoolingUnit"), make_variable("FanSpeed"), RegistrationRejectionReason::AlreadyDefined, {}}});
    EXPECT_FALSE(m_resolver.key_to_cv("FanKey").has_value());

    ASSERT_TRUE(bind("cooling", "CoolingUnit", "FanSpeed", "FanKey").empty());
    m_binder.unbind("cooling");
    EXPECT_FALSE(m_resolver.key_to_cv("FanKey").has_value());
    EXPECT_TRUE(bind("other", "OtherUnit", "FanSpeed", "FanKey").empty());
}

TEST_F(V16KeyBinderTest, OverridableKeyNeedsAnActualAttributeOfTheBuiltinDataType) {
    EXPECT_EQ(reason(bind("lamp", "Lamp", "Intensity", "LightIntensity", ocpp::v2::DataEnum::string)),
              RegistrationRejectionReason::Ocpp16KeyInvalid);
    EXPECT_EQ(reason(bind("lamp", "Lamp", "Intensity", "LightIntensity", ocpp::v2::DataEnum::integer, false)),
              RegistrationRejectionReason::Ocpp16KeyInvalid);
    EXPECT_FALSE(m_resolver.is_overridden(ocpp::v16::keys::valid_keys::LightIntensity));

    EXPECT_TRUE(bind("lamp", "Lamp", "Intensity", "LightIntensity").empty());
    EXPECT_TRUE(m_resolver.is_overridden(ocpp::v16::keys::valid_keys::LightIntensity));
    EXPECT_EQ(m_resolver.known_key_to_cv(ocpp::v16::keys::valid_keys::LightIntensity)->first.name.get(), "Lamp");
}

TEST_F(V16KeyBinderTest, SecondVariableDeclaringTheSameKeyIsRefused) {
    ocpp::v2::VariableAttribute attribute;
    attribute.type = ocpp::v2::AttributeEnum::Actual;
    attribute.value = "1";
    ocpp::v2::RegisteredVariable first;
    first.variable = make_variable("FanSpeed");
    first.characteristics.dataType = ocpp::v2::DataEnum::integer;
    first.attributes.push_back(attribute);
    auto second = first;
    second.variable = make_variable("PumpSpeed");
    const auto component = make_component("CoolingUnit");

    const auto refused = m_binder.bind("cooling", {{component, {first, second}}},
                                       {DeclaredOcpp16Key{component, make_variable("FanSpeed"), "SpeedKey"},
                                        DeclaredOcpp16Key{component, make_variable("PumpSpeed"), "SpeedKey"}});

    ASSERT_EQ(refused.size(), 1u);
    EXPECT_EQ(refused.front().variable.name.get(), "PumpSpeed");
    EXPECT_EQ(refused.front().reason, RegistrationRejectionReason::Ocpp16KeyInUse);
    EXPECT_EQ(m_resolver.key_to_cv("SpeedKey")->second.name.get(), "FanSpeed");
}

TEST_F(V16KeyBinderTest, RefusedRebindingLeavesNoKeyOfTheOwner) {
    ASSERT_TRUE(bind("cooling", "CoolingUnit", "FanSpeed", "FirstKey").empty());
    EXPECT_EQ(reason(bind("cooling", "CoolingUnit", "FanSpeed", "FileKey")),
              RegistrationRejectionReason::Ocpp16KeyInUse);
    EXPECT_FALSE(m_resolver.key_to_cv("FirstKey").has_value());
    EXPECT_FALSE(m_resolver.cv_to_key(make_component("CoolingUnit"), make_variable("FanSpeed")).has_value());
}

TEST_F(V16KeyBinderTest, AttributeWithoutTypeIsTheActualAttribute) {
    ocpp::v2::VariableAttribute attribute;
    attribute.value = "1";
    ocpp::v2::RegisteredVariable registered;
    registered.variable = make_variable("Intensity");
    registered.characteristics.dataType = ocpp::v2::DataEnum::integer;
    registered.attributes.push_back(attribute);
    const auto component = make_component("Lamp");

    EXPECT_TRUE(m_binder
                    .bind("lamp", {{component, {registered}}},
                          {DeclaredOcpp16Key{component, make_variable("Intensity"), "LightIntensity"}})
                    .empty());
    EXPECT_TRUE(m_resolver.is_overridden(ocpp::v16::keys::valid_keys::LightIntensity));
}

// The legacy variable of an overridden key is removed from the device model at startup
TEST(V16KeyBinder, OverrideWithoutItsLegacyVariableIsAccepted) {
    stubs::DeviceModelStub device_model;
    ocpp::v16::VariableResolver resolver;
    ocpp_multi::V16KeyBinder binder(resolver, device_model, {});
    ocpp::v2::VariableAttribute attribute;
    attribute.type = ocpp::v2::AttributeEnum::Actual;
    attribute.value = "bright";
    ocpp::v2::RegisteredVariable registered;
    registered.variable = make_variable("Intensity");
    registered.characteristics.dataType = ocpp::v2::DataEnum::string;
    registered.attributes.push_back(attribute);
    const auto component = make_component("Lamp");

    EXPECT_TRUE(binder
                    .bind("lamp", {{component, {registered}}},
                          {DeclaredOcpp16Key{component, make_variable("Intensity"), "LightIntensity"}})
                    .empty());
    EXPECT_TRUE(resolver.is_overridden(ocpp::v16::keys::valid_keys::LightIntensity));
}

TEST_F(V16KeyBinderTest, KeysAndVariablesTheStackOwnsAreRefused) {
    EXPECT_EQ(reason(bind("owner", "Other", "Heartbeat", "HeartbeatInterval")),
              RegistrationRejectionReason::Ocpp16KeyInUse);
    EXPECT_EQ(reason(bind("owner", "OCPPCommCtrlr", "HeartbeatInterval", "MyInterval")),
              RegistrationRejectionReason::Ocpp16KeyInUse);
    EXPECT_EQ(reason(bind("owner", "Other", "Connectors", "NumberOfConnectors")),
              RegistrationRejectionReason::Ocpp16KeyInvalid);
    EXPECT_EQ(reason(bind("owner", "LocalAuthListCtrlr", "Entries", "MyEntries")),
              RegistrationRejectionReason::Ocpp16KeyInvalid);
}

/// \brief The key mapping file overrides LightIntensity with Lamp.Intensity
class V16KeyBinderFileOverrideTest : public ::testing::Test {
protected:
    static ocpp::v2::Ocpp16KeyMappings override_mappings() {
        return {{"LightIntensity", {make_component("Lamp"), make_variable("Intensity")}}};
    }

    V16KeyBinderFileOverrideTest() : m_binder(m_resolver, m_device_model, override_mappings()) {
        m_device_model.add(make_component("OCPP16LegacyCtrlr"), make_variable("LightIntensity"), "12");
        m_device_model.set_data_type(make_component("OCPP16LegacyCtrlr"), make_variable("LightIntensity"),
                                     ocpp::v2::DataEnum::integer);
    }

    std::vector<ocpp::v2::RejectedVariable> bind_intensity(ocpp::v2::DataEnum data_type) {
        ocpp::v2::VariableAttribute attribute;
        attribute.type = ocpp::v2::AttributeEnum::Actual;
        attribute.value = "1";
        ocpp::v2::RegisteredVariable registered;
        registered.variable = make_variable("Intensity");
        registered.characteristics.dataType = data_type;
        registered.attributes.push_back(attribute);
        const auto component = make_component("Lamp");
        return m_binder.bind("lamp", {{component, {registered}}},
                             {DeclaredOcpp16Key{component, make_variable("Intensity"), "LightIntensity"}});
    }

    std::string intensity_target() const {
        return m_resolver.known_key_to_cv(ocpp::v16::keys::valid_keys::LightIntensity)->first.name.get();
    }

    stubs::DeviceModelStub m_device_model;
    // The file layer is filled by load(), which runs after the startup registrations
    ocpp::v16::VariableResolver m_resolver;
    ocpp_multi::V16KeyBinder m_binder;
};

TEST_F(V16KeyBinderFileOverrideTest, FileKeyBeforeLoadIsLeftToTheFile) {
    EXPECT_TRUE(bind_intensity(ocpp::v2::DataEnum::integer).empty());
    EXPECT_FALSE(m_resolver.is_overridden(ocpp::v16::keys::valid_keys::LightIntensity));
}

// load() drops the file entry for the same reason, so the key would serve nothing
TEST_F(V16KeyBinderFileOverrideTest, FileKeyForAnUnusableTargetIsRefused) {
    const auto rejected = bind_intensity(ocpp::v2::DataEnum::string);
    ASSERT_EQ(rejected.size(), 1u);
    EXPECT_EQ(rejected.front().reason, RegistrationRejectionReason::Ocpp16KeyInvalid);
}

// The file entry was dropped at load() because its target was not registered yet; the file layer is replaced only
// at the next start
TEST_F(V16KeyBinderFileOverrideTest, FileKeyDroppedAtLoadIsBoundToTheOwner) {
    m_resolver.replace_file_mappings({});
    m_binder.on_file_mappings_loaded();

    EXPECT_TRUE(bind_intensity(ocpp::v2::DataEnum::integer).empty());
    EXPECT_EQ(intensity_target(), "Lamp");

    m_binder.unbind("lamp");
    EXPECT_FALSE(m_resolver.is_overridden(ocpp::v16::keys::valid_keys::LightIntensity));
}

// A file entry in effect after load() stays the file's
TEST_F(V16KeyBinderFileOverrideTest, FileKeyInEffectAfterLoadIsLeftToTheFile) {
    m_resolver.replace_file_mappings(override_mappings());
    m_binder.on_file_mappings_loaded();

    EXPECT_TRUE(bind_intensity(ocpp::v2::DataEnum::integer).empty());
    m_binder.unbind("lamp");
    EXPECT_EQ(intensity_target(), "Lamp");
}

} // namespace
