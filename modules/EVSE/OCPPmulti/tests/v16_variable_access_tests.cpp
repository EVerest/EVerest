// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

#include <ocpp/v16/variable_resolver.hpp>

#include <v16_variable_access.hpp>

#include "stubs/device_model_stub.hpp"

namespace {

ocpp::v2::Component make_component(const std::string& name, const std::optional<std::string>& instance = std::nullopt) {
    ocpp::v2::Component component;
    component.name = name;
    if (instance.has_value()) {
        component.instance = instance.value();
    }
    return component;
}

ocpp::v2::Variable make_variable(const std::string& name, const std::optional<std::string>& instance = std::nullopt) {
    ocpp::v2::Variable variable;
    variable.name = name;
    if (instance.has_value()) {
        variable.instance = instance.value();
    }
    return variable;
}

ocpp::v2::GetVariableData make_get(const std::string& component, const std::string& variable,
                                   const std::optional<std::string>& component_instance = std::nullopt) {
    ocpp::v2::GetVariableData data;
    data.component = make_component(component, component_instance);
    data.variable = make_variable(variable);
    return data;
}

ocpp::v2::SetVariableData make_set(const std::string& component, const std::string& variable, const std::string& value,
                                   const std::optional<std::string>& component_instance = std::nullopt) {
    ocpp::v2::SetVariableData data;
    data.attributeValue = value;
    data.component = make_component(component, component_instance);
    data.variable = make_variable(variable);
    return data;
}

ocpp::v2::Ocpp16CustomConfigMappings custom_mappings_fixture() {
    ocpp::v2::Ocpp16CustomConfigMappings mappings;
    mappings.emplace("VendorBar", std::make_pair(make_component("VendorCtrlr"), make_variable("BarSetting")));
    // two custom keys mapping to the same CV -> ambiguous reverse
    mappings.emplace("VendorFoo", std::make_pair(make_component("VendorCtrlr"), make_variable("FooSetting")));
    mappings.emplace("VendorFooAlias", std::make_pair(make_component("VendorCtrlr"), make_variable("FooSetting")));
    return mappings;
}

// keys the fake 1.6 chargepoint knows, with programmable set results
class Fixture : public ::testing::Test {
protected:
    Fixture() : m_resolver(custom_mappings_fixture()) {
        m_keys["HeartbeatInterval"] = "42";
        m_keys["VendorBar"] = "bar-value";
        m_keys["LegacyOnlyKey"] = "legacy-value";
        m_keys["SupportedFeatureProfiles"] = "Core,Reservation";
        m_keys["AuthorizationKey"] = ""; // derived key, write-only in 1.6
    }

    ocpp_multi::V16VariableAccess make_access() {
        return ocpp_multi::V16VariableAccess(
            m_resolver, m_device_model,
            [this](const std::vector<ocpp::CiString<50>>& keys) {
                ++m_get_keys_calls;
                ocpp::v16::GetConfigurationResponse response;
                std::vector<ocpp::v16::KeyValue> found;
                std::vector<ocpp::CiString<50>> unknown;
                for (const auto& key : keys) {
                    const auto it = m_keys.find(key.get());
                    if (it != m_keys.end()) {
                        ocpp::v16::KeyValue kv;
                        kv.key = it->first;
                        kv.readonly = false;
                        kv.value = ocpp::CiString<500>(it->second);
                        found.push_back(kv);
                    } else {
                        unknown.push_back(key);
                    }
                }
                if (!found.empty()) {
                    response.configurationKey = found;
                }
                if (!unknown.empty()) {
                    response.unknownKey = unknown;
                }
                return response;
            },
            [this](const ocpp::CiString<50>& key, const ocpp::CiString<500>& value) {
                m_set_calls.emplace_back(key.get(), value.get());
                const auto it = m_set_results.find(key.get());
                if (it != m_set_results.end()) {
                    return it->second;
                }
                if (m_keys.count(key.get()) == 0) {
                    return ocpp::v16::ConfigurationStatus::NotSupported;
                }
                m_keys[key.get()] = value.get();
                return ocpp::v16::ConfigurationStatus::Accepted;
            });
    }

    ocpp::v16::VariableResolver m_resolver;
    stubs::DeviceModelStub m_device_model;
    std::map<std::string, std::string> m_keys;
    std::map<std::string, ocpp::v16::ConfigurationStatus> m_set_results;
    std::vector<std::pair<std::string, std::string>> m_set_calls;
    int m_get_keys_calls{0};
};

using V16VariableAccess = Fixture;

// ---- path 1: legacy (empty component) ----

TEST_F(V16VariableAccess, legacyGetKeepsLegacyShape) {
    auto access = make_access();
    ocpp::v2::GetVariableData known = make_get("", "HeartbeatInterval");
    ocpp::v2::GetVariableData unknown = make_get("", "NoSuchKey");
    const auto results = access.get({known, unknown});

    ASSERT_EQ(results.size(), 2);
    EXPECT_EQ(results[0].attributeStatus, ocpp::v2::GetVariableStatusEnum::Accepted);
    EXPECT_EQ(results[0].component.name.get(), "");
    EXPECT_EQ(results[0].variable.name.get(), "HeartbeatInterval");
    ASSERT_TRUE(results[0].attributeValue.has_value());
    EXPECT_EQ(results[0].attributeValue.value().get(), "42");
    EXPECT_EQ(results[0].attributeType, ocpp::v2::AttributeEnum::Actual);

    EXPECT_EQ(results[1].attributeStatus, ocpp::v2::GetVariableStatusEnum::UnknownVariable);
    EXPECT_EQ(results[1].component.name.get(), "");
    EXPECT_EQ(results[1].variable.name.get(), "NoSuchKey");
    EXPECT_FALSE(results[1].attributeValue.has_value());
}

TEST_F(V16VariableAccess, legacySetKeepsLegacyShapeAndStatusMapping) {
    m_set_results["RebootKey"] = ocpp::v16::ConfigurationStatus::RebootRequired;
    m_set_results["RejectKey"] = ocpp::v16::ConfigurationStatus::Rejected;
    auto access = make_access();

    const auto results = access.set({make_set("", "HeartbeatInterval", "60"), make_set("", "RebootKey", "x"),
                                     make_set("", "RejectKey", "x"), make_set("", "NoSuchKey", "x")},
                                    "csms");

    ASSERT_EQ(results.size(), 4);
    EXPECT_EQ(results[0].result.attributeStatus, ocpp::v2::SetVariableStatusEnum::Accepted);
    EXPECT_EQ(results[0].result.component.name.get(), "");
    EXPECT_EQ(results[0].result.variable.name.get(), "HeartbeatInterval");
    EXPECT_FALSE(results[0].monitor_value.has_value());
    EXPECT_EQ(results[1].result.attributeStatus, ocpp::v2::SetVariableStatusEnum::RebootRequired);
    EXPECT_EQ(results[2].result.attributeStatus, ocpp::v2::SetVariableStatusEnum::Rejected);
    // NotSupported in 1.6 means unknown key
    EXPECT_EQ(results[3].result.attributeStatus, ocpp::v2::SetVariableStatusEnum::UnknownVariable);
    EXPECT_EQ(m_keys["HeartbeatInterval"], "60");
}

TEST_F(V16VariableAccess, legacyDeprecationWarningThrottledPerKey) {
    auto access = make_access();
    access.get({make_get("", "HeartbeatInterval")});
    EXPECT_EQ(access.warned_keys().count("HeartbeatInterval"), 1);
    access.set({make_set("", "HeartbeatInterval", "60")}, "csms");
    access.get({make_get("", "AuthorizationKey")}); // derived key, no canonical CV
    EXPECT_EQ(access.warned_keys().size(), 2);
    EXPECT_EQ(access.warned_keys().count("AuthorizationKey"), 1);
    // both requests were still routed to the key path
    EXPECT_EQ(m_get_keys_calls, 2);
    EXPECT_EQ(m_set_calls.size(), 1);
}

// ---- path 2: canonical form, reverse-resolves to a 1.6 key ----

TEST_F(V16VariableAccess, canonicalGetRoutesKeyAndEchoesCv) {
    auto access = make_access();
    const auto results = access.get({make_get("OCPPCommCtrlr", "HeartbeatInterval")});

    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].attributeStatus, ocpp::v2::GetVariableStatusEnum::Accepted);
    EXPECT_EQ(results[0].component.name.get(), "OCPPCommCtrlr");
    EXPECT_EQ(results[0].variable.name.get(), "HeartbeatInterval");
    ASSERT_TRUE(results[0].attributeValue.has_value());
    EXPECT_EQ(results[0].attributeValue.value().get(), "42");
    EXPECT_TRUE(access.warned_keys().empty()); // no deprecation warning on the canonical path
}

TEST_F(V16VariableAccess, canonicalGetCustomMappingRoutesKey) {
    auto access = make_access();
    const auto results = access.get({make_get("VendorCtrlr", "BarSetting")});

    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].attributeStatus, ocpp::v2::GetVariableStatusEnum::Accepted);
    EXPECT_EQ(results[0].component.name.get(), "VendorCtrlr");
    EXPECT_EQ(results[0].variable.name.get(), "BarSetting");
    ASSERT_TRUE(results[0].attributeValue.has_value());
    EXPECT_EQ(results[0].attributeValue.value().get(), "bar-value");
}

TEST_F(V16VariableAccess, canonicalSetRoutesKeyAndEchoesCv) {
    auto access = make_access();
    const auto results = access.set({make_set("OCPPCommCtrlr", "HeartbeatInterval", "90")}, "csms");

    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].result.attributeStatus, ocpp::v2::SetVariableStatusEnum::Accepted);
    EXPECT_EQ(results[0].result.component.name.get(), "OCPPCommCtrlr");
    EXPECT_EQ(results[0].result.variable.name.get(), "HeartbeatInterval");
    EXPECT_FALSE(results[0].monitor_value.has_value());
    ASSERT_EQ(m_set_calls.size(), 1);
    EXPECT_EQ(m_set_calls[0].first, "HeartbeatInterval");
    EXPECT_EQ(m_set_calls[0].second, "90");
}

TEST_F(V16VariableAccess, nonActualAttributeOnKeyPathIsNotSupported) {
    auto access = make_access();
    auto get_request = make_get("OCPPCommCtrlr", "HeartbeatInterval");
    get_request.attributeType = ocpp::v2::AttributeEnum::Target;
    const auto get_results = access.get({get_request});
    ASSERT_EQ(get_results.size(), 1);
    EXPECT_EQ(get_results[0].attributeStatus, ocpp::v2::GetVariableStatusEnum::NotSupportedAttributeType);
    EXPECT_EQ(get_results[0].component.name.get(), "OCPPCommCtrlr");
    EXPECT_EQ(m_get_keys_calls, 0);

    auto set_request = make_set("OCPPCommCtrlr", "HeartbeatInterval", "90");
    set_request.attributeType = ocpp::v2::AttributeEnum::MaxSet;
    const auto set_results = access.set({set_request}, "csms");
    ASSERT_EQ(set_results.size(), 1);
    EXPECT_EQ(set_results[0].result.attributeStatus, ocpp::v2::SetVariableStatusEnum::NotSupportedAttributeType);
    EXPECT_TRUE(m_set_calls.empty());
}

TEST_F(V16VariableAccess, instanceQualifiedCanonicalCvDoesNotAliasGlobalKey) {
    auto access = make_access();

    // an instance-qualified CV must hit the device model, not the global 1.6 key
    const auto get_results = access.get({make_get("OCPPCommCtrlr", "HeartbeatInterval", std::string("x"))});
    ASSERT_EQ(get_results.size(), 1);
    EXPECT_EQ(get_results[0].attributeStatus, ocpp::v2::GetVariableStatusEnum::UnknownComponent);
    EXPECT_EQ(m_get_keys_calls, 0);

    const auto set_results =
        access.set({make_set("OCPPCommCtrlr", "HeartbeatInterval", "60", std::string("x"))}, "csms");
    ASSERT_EQ(set_results.size(), 1);
    EXPECT_EQ(set_results[0].result.attributeStatus, ocpp::v2::SetVariableStatusEnum::UnknownComponent);
    EXPECT_TRUE(m_set_calls.empty());
    EXPECT_EQ(m_keys["HeartbeatInterval"], "42"); // global key untouched
}

// ---- path 3: canonical form, no 1.6 key -> device model ----

TEST_F(V16VariableAccess, freeCvReadsAndWritesDeviceModel) {
    m_device_model.add(make_component("VendorCtrlr"), make_variable("FreeSetting"), "initial");
    auto access = make_access();

    const auto get_results = access.get({make_get("VendorCtrlr", "FreeSetting")});
    ASSERT_EQ(get_results.size(), 1);
    EXPECT_EQ(get_results[0].attributeStatus, ocpp::v2::GetVariableStatusEnum::Accepted);
    ASSERT_TRUE(get_results[0].attributeValue.has_value());
    EXPECT_EQ(get_results[0].attributeValue.value().get(), "initial");

    const auto set_results = access.set({make_set("VendorCtrlr", "FreeSetting", "updated")}, "csms");
    ASSERT_EQ(set_results.size(), 1);
    EXPECT_EQ(set_results[0].result.attributeStatus, ocpp::v2::SetVariableStatusEnum::Accepted);
    ASSERT_TRUE(set_results[0].monitor_value.has_value());
    EXPECT_EQ(set_results[0].monitor_value.value(), "updated");
    EXPECT_EQ(m_device_model.entry(make_component("VendorCtrlr"), make_variable("FreeSetting")).value, "updated");
    EXPECT_EQ(m_device_model.entry(make_component("VendorCtrlr"), make_variable("FreeSetting")).last_source, "csms");
    EXPECT_EQ(m_device_model.set_read_only_calls(), 0);
    EXPECT_TRUE(m_set_calls.empty()); // never touched the 1.6 key path
}

TEST_F(V16VariableAccess, freeCvReadOnlyMutabilityRejectsWrite) {
    m_device_model.add(make_component("VendorCtrlr"), make_variable("FrozenSetting"), "fixed",
                       ocpp::v2::MutabilityEnum::ReadOnly);
    auto access = make_access();

    const auto results = access.set({make_set("VendorCtrlr", "FrozenSetting", "nope")}, "csms");
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].result.attributeStatus, ocpp::v2::SetVariableStatusEnum::Rejected);
    EXPECT_FALSE(results[0].monitor_value.has_value());
    EXPECT_EQ(m_device_model.entry(make_component("VendorCtrlr"), make_variable("FrozenSetting")).value, "fixed");
    EXPECT_EQ(m_device_model.set_read_only_calls(), 0);
}

TEST_F(V16VariableAccess, freeCvWriteOnlyMutabilityHidesValueOnRead) {
    m_device_model.add(make_component("SecurityCtrlr"), make_variable("SecretSetting"), "top-secret",
                       ocpp::v2::MutabilityEnum::WriteOnly);
    auto access = make_access();

    const auto results = access.get({make_get("SecurityCtrlr", "SecretSetting")});
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].attributeStatus, ocpp::v2::GetVariableStatusEnum::Rejected);
    EXPECT_FALSE(results[0].attributeValue.has_value());
}

TEST_F(V16VariableAccess, freeCvWriteOnlyMutabilityMasksMonitorValue) {
    m_device_model.add(make_component("SecurityCtrlr"), make_variable("SecretSetting"), "old-secret",
                       ocpp::v2::MutabilityEnum::WriteOnly);
    auto access = make_access();

    const auto results = access.set({make_set("SecurityCtrlr", "SecretSetting", "new-secret")}, "csms");
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].result.attributeStatus, ocpp::v2::SetVariableStatusEnum::Accepted);
    // the committed write still fires a monitor event, but the write-only value is masked
    ASSERT_TRUE(results[0].monitor_value.has_value());
    EXPECT_EQ(results[0].monitor_value.value(), "");
    EXPECT_EQ(m_device_model.entry(make_component("SecurityCtrlr"), make_variable("SecretSetting")).value,
              "new-secret");
}

TEST_F(V16VariableAccess, freeCvNonActualAttributePassesDeviceModelStatusThrough) {
    // Path 3 (unlike path 2) does NOT gate on attribute type: the device model decides.
    m_device_model.add(make_component("VendorCtrlr"), make_variable("FreeSetting"), "initial");
    auto access = make_access();

    auto get_request = make_get("VendorCtrlr", "FreeSetting");
    get_request.attributeType = ocpp::v2::AttributeEnum::Target;
    const auto get_results = access.get({get_request});
    ASSERT_EQ(get_results.size(), 1);
    EXPECT_EQ(get_results[0].attributeStatus, ocpp::v2::GetVariableStatusEnum::NotSupportedAttributeType);

    auto set_request = make_set("VendorCtrlr", "FreeSetting", "updated");
    set_request.attributeType = ocpp::v2::AttributeEnum::Target;
    const auto set_results = access.set({set_request}, "csms");
    ASSERT_EQ(set_results.size(), 1);
    EXPECT_EQ(set_results[0].result.attributeStatus, ocpp::v2::SetVariableStatusEnum::NotSupportedAttributeType);
    EXPECT_FALSE(set_results[0].monitor_value.has_value());
    EXPECT_TRUE(m_set_calls.empty()); // never touched the 1.6 key path
}

TEST_F(V16VariableAccess, freeCvDirectRebootRequiredStillFlagsWrite) {
    m_device_model.add(make_component("VendorCtrlr"), make_variable("FreeSetting"), "initial");
    m_device_model.set_forced_set_status(make_component("VendorCtrlr"), make_variable("FreeSetting"),
                                         ocpp::v2::SetVariableStatusEnum::RebootRequired);
    auto access = make_access();

    const auto results = access.set({make_set("VendorCtrlr", "FreeSetting", "updated")}, "csms");
    ASSERT_EQ(results.size(), 1);
    // Free CV: the device-model-originated RebootRequired passes through unchanged...
    EXPECT_EQ(results[0].result.attributeStatus, ocpp::v2::SetVariableStatusEnum::RebootRequired);
    // ...but the committed write must still carry the monitor value so an event is synthesized.
    EXPECT_TRUE(results[0].monitor_value.has_value());
    EXPECT_EQ(m_device_model.entry(make_component("VendorCtrlr"), make_variable("FreeSetting")).value, "updated");
}

TEST_F(V16VariableAccess, connectionConfigWriteIsRebootRequired) {
    m_device_model.add(make_component("NetworkConfiguration", std::string("2")), make_variable("OcppCsmsUrl"),
                       "wss://old");
    m_device_model.add(make_component("OCPPCommCtrlr"), make_variable("NetworkConfigurationPriority"), "1");
    auto access = make_access();

    const auto results = access.set({make_set("NetworkConfiguration", "OcppCsmsUrl", "wss://new", std::string("2")),
                                     make_set("OCPPCommCtrlr", "NetworkConfigurationPriority", "2")},
                                    "csms");
    ASSERT_EQ(results.size(), 2);
    EXPECT_EQ(results[0].result.attributeStatus, ocpp::v2::SetVariableStatusEnum::RebootRequired);
    EXPECT_TRUE(results[0].monitor_value.has_value());
    EXPECT_EQ(results[0].result.component.name.get(), "NetworkConfiguration");
    EXPECT_EQ(results[1].result.attributeStatus, ocpp::v2::SetVariableStatusEnum::RebootRequired);
    EXPECT_TRUE(results[1].monitor_value.has_value());
    EXPECT_EQ(
        m_device_model.entry(make_component("NetworkConfiguration", std::string("2")), make_variable("OcppCsmsUrl"))
            .value,
        "wss://new");
    EXPECT_EQ(
        m_device_model.entry(make_component("OCPPCommCtrlr"), make_variable("NetworkConfigurationPriority")).value,
        "2");
    EXPECT_EQ(m_device_model.set_read_only_calls(), 0);
}

TEST_F(V16VariableAccess, connectionConfigReadsDeviceModel) {
    m_device_model.add(make_component("NetworkConfiguration", std::string("2")), make_variable("OcppCsmsUrl"),
                       "wss://csms");
    auto access = make_access();

    const auto results = access.get({make_get("NetworkConfiguration", "OcppCsmsUrl", std::string("2"))});
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].attributeStatus, ocpp::v2::GetVariableStatusEnum::Accepted);
    ASSERT_TRUE(results[0].attributeValue.has_value());
    EXPECT_EQ(results[0].attributeValue.value().get(), "wss://csms");
    ASSERT_TRUE(results[0].component.instance.has_value());
    EXPECT_EQ(results[0].component.instance.value().get(), "2");
}

TEST_F(V16VariableAccess, readOnlyDerivedReadsDeviceModelAndRejectsWrite) {
    // LocalAuthListCtrlr/Entries: max-limit CV with no reverse key -> ReadOnlyDerived
    m_device_model.add(make_component("LocalAuthListCtrlr"), make_variable("Entries"), "1000");
    auto access = make_access();

    const auto get_results = access.get({make_get("LocalAuthListCtrlr", "Entries")});
    ASSERT_EQ(get_results.size(), 1);
    EXPECT_EQ(get_results[0].attributeStatus, ocpp::v2::GetVariableStatusEnum::Accepted);
    ASSERT_TRUE(get_results[0].attributeValue.has_value());
    EXPECT_EQ(get_results[0].attributeValue.value().get(), "1000");

    const auto set_results = access.set({make_set("LocalAuthListCtrlr", "Entries", "5")}, "csms");
    ASSERT_EQ(set_results.size(), 1);
    EXPECT_EQ(set_results[0].result.attributeStatus, ocpp::v2::SetVariableStatusEnum::Rejected);
    EXPECT_FALSE(set_results[0].monitor_value.has_value());
    ASSERT_TRUE(set_results[0].result.attributeStatusInfo.has_value());
    EXPECT_EQ(set_results[0].result.attributeStatusInfo.value().reasonCode.get(), "ReadOnly");
    ASSERT_TRUE(set_results[0].result.attributeStatusInfo.value().additionalInfo.has_value());
    EXPECT_EQ(set_results[0].result.attributeStatusInfo.value().additionalInfo.value().get(),
              "Managed by the OCPP1.6 stack; read-only via this API");
    EXPECT_EQ(m_device_model.entry(make_component("LocalAuthListCtrlr"), make_variable("Entries")).value, "1000");
}

TEST_F(V16VariableAccess, unknownComponentAndVariableAreDistinguished) {
    m_device_model.add(make_component("VendorCtrlr"), make_variable("FreeSetting"), "x");
    auto access = make_access();

    const auto get_results =
        access.get({make_get("NoSuchCtrlr", "Whatever"), make_get("VendorCtrlr", "NoSuchSetting")});
    ASSERT_EQ(get_results.size(), 2);
    EXPECT_EQ(get_results[0].attributeStatus, ocpp::v2::GetVariableStatusEnum::UnknownComponent);
    EXPECT_EQ(get_results[1].attributeStatus, ocpp::v2::GetVariableStatusEnum::UnknownVariable);

    const auto set_results =
        access.set({make_set("NoSuchCtrlr", "Whatever", "x"), make_set("VendorCtrlr", "NoSuchSetting", "x")}, "csms");
    ASSERT_EQ(set_results.size(), 2);
    EXPECT_EQ(set_results[0].result.attributeStatus, ocpp::v2::SetVariableStatusEnum::UnknownComponent);
    EXPECT_FALSE(set_results[0].monitor_value.has_value());
    EXPECT_EQ(set_results[1].result.attributeStatus, ocpp::v2::SetVariableStatusEnum::UnknownVariable);
    EXPECT_FALSE(set_results[1].monitor_value.has_value());
}

// ---- path 4: ambiguous reverse mapping ----

TEST_F(V16VariableAccess, ambiguousCustomMappingIsRejected) {
    auto access = make_access();

    const auto get_results = access.get({make_get("VendorCtrlr", "FooSetting")});
    ASSERT_EQ(get_results.size(), 1);
    EXPECT_EQ(get_results[0].attributeStatus, ocpp::v2::GetVariableStatusEnum::Rejected);
    ASSERT_TRUE(get_results[0].attributeStatusInfo.has_value());
    EXPECT_EQ(get_results[0].attributeStatusInfo.value().reasonCode.get(), "AmbiguousMapping");
    ASSERT_TRUE(get_results[0].attributeStatusInfo.value().additionalInfo.has_value());
    EXPECT_EQ(get_results[0].attributeStatusInfo.value().additionalInfo.value().get(),
              "Ambiguous custom config mapping; fix DeviceModelConfigMappings");
    EXPECT_EQ(m_get_keys_calls, 0);

    const auto set_results = access.set({make_set("VendorCtrlr", "FooSetting", "x")}, "csms");
    ASSERT_EQ(set_results.size(), 1);
    EXPECT_EQ(set_results[0].result.attributeStatus, ocpp::v2::SetVariableStatusEnum::Rejected);
    ASSERT_TRUE(set_results[0].result.attributeStatusInfo.has_value());
    EXPECT_EQ(set_results[0].result.attributeStatusInfo.value().reasonCode.get(), "AmbiguousMapping");
    ASSERT_TRUE(set_results[0].result.attributeStatusInfo.value().additionalInfo.has_value());
    EXPECT_EQ(set_results[0].result.attributeStatusInfo.value().additionalInfo.value().get(),
              "Ambiguous custom config mapping; fix DeviceModelConfigMappings");
    EXPECT_TRUE(m_set_calls.empty());
}

// ---- mixed batches keep request order and legacy population semantics ----

TEST_F(V16VariableAccess, mixedBatchKeepsOrderAndSingleKeyFetch) {
    m_device_model.add(make_component("VendorCtrlr"), make_variable("FreeSetting"), "free-value");
    auto access = make_access();

    const auto results = access.get({make_get("", "LegacyOnlyKey"), make_get("VendorCtrlr", "FreeSetting"),
                                     make_get("OCPPCommCtrlr", "HeartbeatInterval")});
    ASSERT_EQ(results.size(), 3);
    EXPECT_EQ(results[0].attributeStatus, ocpp::v2::GetVariableStatusEnum::Accepted);
    EXPECT_EQ(results[0].attributeValue.value().get(), "legacy-value");
    EXPECT_EQ(results[0].component.name.get(), "");
    EXPECT_EQ(results[1].attributeStatus, ocpp::v2::GetVariableStatusEnum::Accepted);
    EXPECT_EQ(results[1].attributeValue.value().get(), "free-value");
    EXPECT_EQ(results[2].attributeStatus, ocpp::v2::GetVariableStatusEnum::Accepted);
    EXPECT_EQ(results[2].attributeValue.value().get(), "42");
    // legacy and canonical key requests share one 1.6 fetch, like today's batching
    EXPECT_EQ(m_get_keys_calls, 1);
}

TEST_F(V16VariableAccess, mixedLegacyAndCanonicalSameKeyBothPopulated) {
    // Legacy ("", HeartbeatInterval) and canonical (OCPPCommCtrlr, HeartbeatInterval) resolve to the
    // same 1.6 key; the single response value must fan out to both pending entries.
    auto access = make_access();
    const auto results =
        access.get({make_get("", "HeartbeatInterval"), make_get("OCPPCommCtrlr", "HeartbeatInterval")});
    ASSERT_EQ(results.size(), 2);
    EXPECT_EQ(results[0].attributeStatus, ocpp::v2::GetVariableStatusEnum::Accepted);
    ASSERT_TRUE(results[0].attributeValue.has_value());
    EXPECT_EQ(results[0].attributeValue.value().get(), "42");
    EXPECT_EQ(results[0].component.name.get(), ""); // legacy shape
    EXPECT_EQ(results[1].attributeStatus, ocpp::v2::GetVariableStatusEnum::Accepted);
    ASSERT_TRUE(results[1].attributeValue.has_value());
    EXPECT_EQ(results[1].attributeValue.value().get(), "42");
    EXPECT_EQ(results[1].component.name.get(), "OCPPCommCtrlr"); // canonical echo
    EXPECT_EQ(m_get_keys_calls, 1);
}

} // namespace
