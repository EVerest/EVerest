// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <ocpp/v16/known_keys.hpp>
#include <ocpp/v16/variable_resolver.hpp>
#include <ocpp/v2/comparators.hpp>
#include <ocpp/v2/ctrlr_component_variables.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace {

using ocpp::v16::CVClass;
using ocpp::v16::VariableResolver;
namespace keys = ocpp::v16::keys;

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

bool same_cv(const std::pair<ocpp::v2::Component, ocpp::v2::Variable>& lhs,
             const std::pair<ocpp::v2::Component, ocpp::v2::Variable>& rhs) {
    // shared comparators, comparing exactly like production (incl. evse)
    return (lhs.first == rhs.first) && (lhs.second == rhs.second);
}

ocpp::v2::Ocpp16CustomConfigMappings custom_mappings_fixture() {
    ocpp::v2::Ocpp16CustomConfigMappings mappings;
    // unambiguous custom mapping
    mappings.emplace("VendorBar", std::make_pair(make_component("VendorCtrlr"), make_variable("BarSetting")));
    // two custom keys mapping to the same CV -> ambiguous reverse
    mappings.emplace("VendorFoo", std::make_pair(make_component("VendorCtrlr"), make_variable("FooSetting")));
    mappings.emplace("VendorFooAlias", std::make_pair(make_component("VendorCtrlr"), make_variable("FooSetting")));
    // custom mapping colliding with the standard reverse entry for HeartbeatInterval
    mappings.emplace("MyHeartbeat",
                     std::make_pair(make_component("OCPPCommCtrlr"), make_variable("HeartbeatInterval")));
    // custom mapping shadowing a derived key must not be reachable via key_to_cv
    mappings.emplace("CentralSystemURI", std::make_pair(make_component("VendorCtrlr"), make_variable("CsmsUrlCopy")));
    return mappings;
}

// iterate all valid_keys values; the enum is generated contiguously starting at 0
template <typename Callable> void for_each_valid_key(Callable&& callable) {
    for (std::uint16_t i = 0; i < 256; ++i) {
        const auto key = static_cast<keys::valid_keys>(i);
        const auto name = keys::convert(key);
        if (name.empty()) {
            break;
        }
        callable(key, std::string{name});
    }
}

TEST(VariableResolver, RoundTripStandardKeys) {
    const VariableResolver resolver{{}};
    std::size_t checked = 0;
    for_each_valid_key([&](keys::valid_keys /*key*/, const std::string& name) {
        const auto cv = resolver.key_to_cv(name);
        if (!cv.has_value()) {
            return; // derived / unmapped / max-limit keys
        }
        const auto reverse = resolver.cv_to_key(cv->first, cv->second);
        ASSERT_TRUE(reverse.key.has_value()) << "no reverse mapping for " << name;
        EXPECT_EQ(reverse.key.value(), name);
        EXPECT_FALSE(reverse.ambiguous) << "unexpected ambiguity for " << name;
        ++checked;
    });
    EXPECT_GT(checked, 0);
}

TEST(VariableResolver, MaxLimitKeysHaveNoCv) {
    // max-limit keys map to VariableCharacteristics.maxLimit, not a CV's Actual value
    const VariableResolver resolver{{}};
    std::size_t checked = 0;
    for_each_valid_key([&](keys::valid_keys key, const std::string& name) {
        if (!keys::is_max_limit_key(key)) {
            return;
        }
        EXPECT_FALSE(resolver.key_to_cv(name).has_value()) << name;
        ++checked;
    });
    EXPECT_GT(checked, 0);
}

TEST(VariableResolver, DriftGuardKeyToCvAgreesWithConvertV2) {
    const VariableResolver resolver{{}};
    for_each_valid_key([&](keys::valid_keys key, const std::string& name) {
        if (keys::is_max_limit_key(key)) {
            return; // intentional divergence, covered by MaxLimitKeysHaveNoCv
        }
        const auto expected = keys::convert_v2(std::string_view{name});
        const auto actual = resolver.key_to_cv(name);
        ASSERT_EQ(expected.has_value(), actual.has_value()) << "drift for " << name;
        if (expected.has_value()) {
            EXPECT_TRUE(same_cv(expected.value(), actual.value())) << "drift for " << name;
        }
    });
}

TEST(VariableResolver, DerivedKeysHaveNoCv) {
    const VariableResolver resolver{custom_mappings_fixture()};
    const char* derived_keys[] = {"CentralSystemURI", "SecurityProfile", "AuthorizationKey",
                                  "HostName",         "ChargePointId",   "SupportedMeasurands"};
    for (const auto* key : derived_keys) {
        EXPECT_FALSE(resolver.key_to_cv(key).has_value()) << key;
    }
}

TEST(VariableResolver, CustomMappingForward) {
    const VariableResolver resolver{custom_mappings_fixture()};
    const auto cv = resolver.key_to_cv("VendorBar");
    ASSERT_TRUE(cv.has_value());
    EXPECT_TRUE(same_cv(cv.value(), {make_component("VendorCtrlr"), make_variable("BarSetting")}));

    // standard keys take precedence over custom mappings
    const auto heartbeat = resolver.key_to_cv("HeartbeatInterval");
    ASSERT_TRUE(heartbeat.has_value());
    EXPECT_EQ(heartbeat->first.name, "OCPPCommCtrlr");
    EXPECT_EQ(heartbeat->second.name, "HeartbeatInterval");

    EXPECT_FALSE(resolver.key_to_cv("NoSuchKey").has_value());
}

TEST(VariableResolver, CustomMappingReverse) {
    const VariableResolver resolver{custom_mappings_fixture()};
    const auto reverse = resolver.cv_to_key(make_component("VendorCtrlr"), make_variable("BarSetting"));
    ASSERT_TRUE(reverse.key.has_value());
    EXPECT_EQ(reverse.key.value(), "VendorBar");
    EXPECT_FALSE(reverse.ambiguous);
}

TEST(VariableResolver, CustomMappingReverseAmbiguousTwoKeysOneCv) {
    const VariableResolver resolver{custom_mappings_fixture()};
    const auto reverse = resolver.cv_to_key(make_component("VendorCtrlr"), make_variable("FooSetting"));
    EXPECT_TRUE(reverse.ambiguous);
}

TEST(VariableResolver, CustomMappingsDifferingByEvseAreDistinct) {
    // mappings that differ only in Component::evse must not collide into an ambiguous reverse entry
    const auto evse_component = [](std::int32_t id) {
        ocpp::v2::Component component = make_component("VendorCtrlr");
        ocpp::v2::EVSE evse;
        evse.id = id;
        component.evse = evse;
        return component;
    };
    ocpp::v2::Ocpp16CustomConfigMappings mappings;
    mappings.emplace("VendorEvse1", std::make_pair(evse_component(1), make_variable("PerEvseSetting")));
    mappings.emplace("VendorEvse2", std::make_pair(evse_component(2), make_variable("PerEvseSetting")));
    const VariableResolver resolver{mappings};

    const auto reverse1 = resolver.cv_to_key(evse_component(1), make_variable("PerEvseSetting"));
    ASSERT_TRUE(reverse1.key.has_value());
    EXPECT_EQ(reverse1.key.value(), "VendorEvse1");
    EXPECT_FALSE(reverse1.ambiguous);

    const auto reverse2 = resolver.cv_to_key(evse_component(2), make_variable("PerEvseSetting"));
    ASSERT_TRUE(reverse2.key.has_value());
    EXPECT_EQ(reverse2.key.value(), "VendorEvse2");
    EXPECT_FALSE(reverse2.ambiguous);
}

TEST(VariableResolver, CustomMappingReverseAmbiguousCollidesWithStandard) {
    const VariableResolver resolver{custom_mappings_fixture()};
    const auto reverse = resolver.cv_to_key(make_component("OCPPCommCtrlr"), make_variable("HeartbeatInterval"));
    EXPECT_TRUE(reverse.ambiguous);
    // the standard key remains reported
    ASSERT_TRUE(reverse.key.has_value());
    EXPECT_EQ(reverse.key.value(), "HeartbeatInterval");
}

TEST(VariableResolver, ClassifyConnectionConfig) {
    const VariableResolver resolver{{}};
    EXPECT_EQ(resolver.classify(make_component("NetworkConfiguration", "1"), make_variable("OcppCsmsUrl")),
              CVClass::ConnectionConfig);
    EXPECT_EQ(resolver.classify(make_component("NetworkConfiguration", "7"), make_variable("SecurityProfile")),
              CVClass::ConnectionConfig);
    EXPECT_EQ(resolver.classify(make_component("OCPPCommCtrlr"), make_variable("NetworkConfigurationPriority")),
              CVClass::ConnectionConfig);
    EXPECT_EQ(resolver.classify(make_component("OCPPCommCtrlr"), make_variable("ActiveNetworkProfile")),
              CVClass::ConnectionConfig);
    EXPECT_EQ(resolver.classify(make_component("SecurityCtrlr"), make_variable("Identity")), CVClass::ConnectionConfig);
    EXPECT_EQ(resolver.classify(make_component("SecurityCtrlr"), make_variable("BasicAuthPassword")),
              CVClass::ConnectionConfig);
}

TEST(VariableResolver, ClassifyReadOnlyDerived) {
    const VariableResolver resolver{{}};
    const auto& sfp = ocpp::v2::ControllerComponentVariables::SupportedFeatureProfiles;
    ASSERT_TRUE(sfp.variable.has_value());
    EXPECT_EQ(resolver.classify(sfp.component, sfp.variable.value()), CVClass::ReadOnlyDerived);

    const auto& max_limit = ocpp::v2::ControllerComponentVariables::EntriesChargingProfiles;
    ASSERT_TRUE(max_limit.variable.has_value());
    EXPECT_EQ(resolver.classify(max_limit.component, max_limit.variable.value()), CVClass::ReadOnlyDerived);
}

TEST(VariableResolver, ClassifyKeyBacked) {
    const VariableResolver resolver{custom_mappings_fixture()};
    // standard key-backed CV
    EXPECT_EQ(resolver.classify(make_component("OCPPCommCtrlr"), make_variable("WebSocketPingInterval")),
              CVClass::KeyBacked);
    // custom-mapping target
    EXPECT_EQ(resolver.classify(make_component("VendorCtrlr"), make_variable("BarSetting")), CVClass::KeyBacked);
}

TEST(VariableResolver, ClassifyFree) {
    const VariableResolver resolver{custom_mappings_fixture()};
    EXPECT_EQ(resolver.classify(make_component("VendorCtrlr"), make_variable("Foo")), CVClass::Free);
    // ambiguous custom CV is not key-backed
    EXPECT_EQ(resolver.classify(make_component("VendorCtrlr"), make_variable("FooSetting")), CVClass::Free);
}

} // namespace
