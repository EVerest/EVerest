// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/ocpp_module_common/custom_error_mapping_validation.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace ocpp_module_common::custom_error_mapping;
using ::testing::HasSubstr;

const std::filesystem::path ERRORS_DIR{EVEREST_ERRORS_DIR};

CustomErrorMapping mapping_of(const std::string& content) {
    auto result = parse_error_mapping(content);
    EXPECT_NE(result.error_mapping, nullptr) << (result.findings.empty() ? "" : result.findings.front().to_string());
    return result.error_mapping != nullptr ? *result.error_mapping : CustomErrorMapping{};
}

Finding single(const std::vector<Finding>& findings) {
    EXPECT_EQ(findings.size(), 1U);
    return findings.empty() ? Finding{} : findings.front();
}

TEST(ErrorMappingErrorTypesTest, ReadsDeclaredErrorTypesFromErrorsDir) {
    const auto declared = read_declared_error_types(ERRORS_DIR);
    ASSERT_TRUE(declared.has_value());
    EXPECT_EQ(declared->count("generic/VendorError"), 1U);
    EXPECT_EQ(declared->count("evse_board_support/MREC3HighTemperature"), 1U);
}

TEST(ErrorMappingErrorTypesTest, MissingErrorsDirIsReported) {
    EXPECT_FALSE(read_declared_error_types("does/not/exist").has_value());
}

TEST(ErrorMappingErrorTypesTest, AcceptsDeclaredTypesAndAnySubType) {
    const auto mapping = mapping_of(R"({
        "generic/VendorError#AnySubType": {"v2": {"tech_code": "A"}},
        "evse_board_support/MREC3HighTemperature": {"v2": {"tech_code": "B"}}
    })");
    EXPECT_TRUE(validate_error_types(mapping, read_declared_error_types(ERRORS_DIR).value()).empty());
}

TEST(ErrorMappingErrorTypesTest, RejectsUnknownNamespace) {
    const auto mapping = mapping_of(R"({"genric/VendorError": {"v2": {"tech_code": "A"}}})");
    const auto finding = single(validate_error_types(mapping, read_declared_error_types(ERRORS_DIR).value()));
    EXPECT_EQ(finding.level, Finding::Level::Error);
    EXPECT_EQ(finding.entry, "genric/VendorError");
    EXPECT_THAT(finding.message, HasSubstr("unknown error namespace 'genric'"));
}

TEST(ErrorMappingErrorTypesTest, RejectsUnknownTypeInKnownNamespace) {
    const auto mapping = mapping_of(R"({"evse_board_support/MREC3HighTemprature": {"v2": {"tech_code": "A"}}})");
    const auto finding = single(validate_error_types(mapping, read_declared_error_types(ERRORS_DIR).value()));
    EXPECT_EQ(finding.level, Finding::Level::Error);
    EXPECT_THAT(finding.message, HasSubstr("unknown error type 'evse_board_support/MREC3HighTemprature'"));
}

TEST(ErrorMappingBuiltinTest, BuiltinTypesAreTheMrecKeys) {
    const auto builtin = builtin_error_types();
    EXPECT_EQ(builtin.size(), 23U);
    EXPECT_EQ(builtin.count("evse_board_support/MREC3HighTemperature"), 1U);
}

TEST(ErrorMappingBuiltinTest, ListsEntriesReplacingBuiltin) {
    const auto mapping = mapping_of(R"({
        "evse_board_support/MREC3HighTemperature": {"v2": {"tech_code": "T-210"}},
        "evse_board_support/MREC3HighTemperature#Sensor2": {"v2": {"tech_code": "T-211"}},
        "generic/VendorError": {"v2": {"tech_code": "A"}}
    })");
    EXPECT_EQ(replaced_builtin_entries(mapping, builtin_error_types()),
              std::vector<std::string>{"evse_board_support/MREC3HighTemperature"});
}

TEST(ErrorMappingValuesTest, AcceptsActualValuePlaceholder) {
    const auto mapping = mapping_of(R"({"generic/VendorError": {
        "v16": {"info": "at ${actual_value} deg"}, "v2": {"techInfo": "${actual_value}${actual_value}"}}})");
    EXPECT_TRUE(validate_values(mapping).empty());
}

TEST(ErrorMappingValuesTest, RejectsUnknownPlaceholder) {
    const auto mapping = mapping_of(R"({"generic/VendorError": {"v2": {"techInfo": "at ${actualValue} deg"}}})");
    const auto finding = single(validate_values(mapping));
    EXPECT_EQ(finding.level, Finding::Level::Error);
    EXPECT_EQ(finding.pointer, "/generic~1VendorError/v2/techInfo");
    EXPECT_THAT(finding.message, HasSubstr("${actualValue}"));
}

TEST(ErrorMappingValuesTest, RejectsUnterminatedPlaceholder) {
    const auto mapping = mapping_of(R"({"generic/VendorError": {"v16": {"info": "at ${actual_value deg"}}})");
    const auto finding = single(validate_values(mapping));
    EXPECT_EQ(finding.level, Finding::Level::Error);
    EXPECT_EQ(finding.pointer, "/generic~1VendorError/v16/info");
}

TEST(ErrorMappingValuesTest, WarnsAboutStaticTextOverOcppLimit) {
    const auto info = std::string(50, 'x') + "${actual_value}";
    const auto too_long_info = std::string(51, 'x');
    const auto mapping = mapping_of(R"({
        "generic/VendorError": {"v16": {"info": ")" +
                                    info + R"("}},
        "generic/VendorWarning": {"v16": {"info": ")" +
                                    too_long_info + R"("}}
    })");
    const auto finding = single(validate_values(mapping));
    EXPECT_EQ(finding.level, Finding::Level::Warning);
    EXPECT_EQ(finding.entry, "generic/VendorWarning");
    EXPECT_THAT(finding.message, HasSubstr("51"));
}

TEST(ErrorMappingValuesTest, RejectsConnectorOnChargingStation) {
    const auto mapping = mapping_of(R"({"generic/VendorError": {"tier_mapping": {"evse": 0, "connector": 1}}})");
    const auto finding = single(validate_values(mapping));
    EXPECT_EQ(finding.level, Finding::Level::Error);
    EXPECT_EQ(finding.pointer, "/generic~1VendorError/tier_mapping");
}

TEST(ErrorMappingTopologyTest, AcceptsExistingEvseAndConnector) {
    const auto mapping = mapping_of(R"({
        "generic/VendorError#A": {"tier_mapping": {"evse": 0}},
        "generic/VendorError#B": {"tier_mapping": {"evse": 2}},
        "generic/VendorError#C": {"tier_mapping": {"evse": 2, "connector": 2}},
        "generic/VendorError#D": {"v2": {"tech_code": "D"}}
    })");
    EXPECT_TRUE(validate_topology(mapping, {{1, 1}, {2, 2}}).empty());
}

TEST(ErrorMappingTopologyTest, RejectsUnknownEvse) {
    const auto mapping = mapping_of(R"({"generic/VendorError": {"tier_mapping": {"evse": 3}}})");
    const auto finding = single(validate_topology(mapping, {{1, 1}, {2, 1}}));
    EXPECT_EQ(finding.level, Finding::Level::Error);
    EXPECT_EQ(finding.pointer, "/generic~1VendorError/tier_mapping/evse");
}

TEST(ErrorMappingTopologyTest, RejectsUnknownConnector) {
    const auto mapping = mapping_of(R"({"generic/VendorError": {"tier_mapping": {"evse": 1, "connector": 2}}})");
    const auto finding = single(validate_topology(mapping, {{1, 1}}));
    EXPECT_EQ(finding.level, Finding::Level::Error);
    EXPECT_EQ(finding.pointer, "/generic~1VendorError/tier_mapping/connector");
}

/// \brief Device model holding the given components (name, evse, connector) with the given variables
class FakeDeviceModel {
public:
    void add(const std::string& component, std::optional<std::int32_t> evse, std::optional<std::int32_t> connector,
             std::set<std::string> variables) {
        m_components.push_back({component, evse, connector, std::move(variables)});
    }

    DeviceModelLookup operator()(const ocpp::v2::Component& component, const ocpp::v2::Variable& variable) {
        m_lookups.push_back(component);
        for (const auto& entry : m_components) {
            const auto evse =
                component.evse.has_value() ? std::optional<std::int32_t>(component.evse->id) : std::nullopt;
            const auto connector = component.evse.has_value() ? component.evse->connectorId : std::nullopt;
            if (entry.name == component.name.get() && entry.evse == evse && entry.connector == connector) {
                return entry.variables.count(variable.name.get()) > 0 ? DeviceModelLookup::Known
                                                                      : DeviceModelLookup::UnknownVariable;
            }
        }
        return DeviceModelLookup::UnknownComponent;
    }

    const std::vector<ocpp::v2::Component>& lookups() const {
        return m_lookups;
    }

private:
    struct Component {
        std::string name;
        std::optional<std::int32_t> evse;
        std::optional<std::int32_t> connector;
        std::set<std::string> variables;
    };
    std::vector<Component> m_components;
    std::vector<ocpp::v2::Component> m_lookups;
};

const EvseTopology TWO_EVSES{{1, 1}, {2, 1}};

TEST(ErrorMappingDeviceModelTest, SkipsEntriesWithoutComponentOrVariable) {
    FakeDeviceModel model;
    const auto mapping = mapping_of(R"({"generic/VendorError": {"v2": {"tech_code": "A"}}})");
    EXPECT_TRUE(validate_device_model(mapping, std::ref(model), TWO_EVSES, true).empty());
    EXPECT_TRUE(model.lookups().empty());
}

TEST(ErrorMappingDeviceModelTest, AcceptsCombinationOnAnyConnector) {
    FakeDeviceModel model;
    model.add("Connector", 2, 1, {"Temperature"});
    const auto mapping = mapping_of(
        R"({"generic/VendorError": {"v2": {"component_name": "Connector", "variable_name": "Temperature"}}})");
    EXPECT_TRUE(validate_device_model(mapping, std::ref(model), TWO_EVSES, true).empty());
}

TEST(ErrorMappingDeviceModelTest, UsesDefaultComponentAndVariable) {
    FakeDeviceModel model;
    model.add("EVSE", 1, std::nullopt, {"Problem"});
    const auto mapping = mapping_of(R"({"generic/VendorError": {"v2": {"variable_name": "Problem"}}})");
    EXPECT_TRUE(validate_device_model(mapping, std::ref(model), TWO_EVSES, false).empty());
    ASSERT_FALSE(model.lookups().empty());
    EXPECT_EQ(model.lookups().back().name.get(), "EVSE");
}

TEST(ErrorMappingDeviceModelTest, WarnsAboutUnknownVariable) {
    FakeDeviceModel model;
    model.add("Connector", 1, 1, {"Available"});
    const auto mapping = mapping_of(
        R"({"generic/VendorError": {"v2": {"component_name": "Connector", "variable_name": "Temperature"}}})");
    const auto finding = single(validate_device_model(mapping, std::ref(model), TWO_EVSES, false));
    EXPECT_EQ(finding.level, Finding::Level::Warning);
    EXPECT_EQ(finding.pointer, "/generic~1VendorError/v2");
    EXPECT_THAT(finding.message, HasSubstr("no variable 'Temperature' on component 'Connector'"));
}

TEST(ErrorMappingDeviceModelTest, StrictReportsUnknownComponentAsError) {
    FakeDeviceModel model;
    const auto mapping = mapping_of(R"({"generic/VendorError": {"v2": {"component_name": "SurgeProtector"}}})");
    const auto finding = single(validate_device_model(mapping, std::ref(model), TWO_EVSES, true));
    EXPECT_EQ(finding.level, Finding::Level::Error);
    EXPECT_THAT(finding.message, HasSubstr("no component 'SurgeProtector'"));
}

TEST(ErrorMappingDeviceModelTest, EntryMappingRestrictsTheLookupToItsEvse) {
    FakeDeviceModel model;
    model.add("Connector", 1, 1, {"Temperature"});
    const auto mapping = mapping_of(R"({"generic/VendorError": {"tier_mapping": {"evse": 2, "connector": 1},
        "v2": {"component_name": "Connector", "variable_name": "Temperature"}}})");
    const auto finding = single(validate_device_model(mapping, std::ref(model), TWO_EVSES, false));
    EXPECT_THAT(finding.message, HasSubstr("on evse 2 connector 1"));
    for (const auto& lookup : model.lookups()) {
        ASSERT_TRUE(lookup.evse.has_value());
        EXPECT_EQ(lookup.evse->id, 2);
    }
}

TEST(ErrorMappingDeviceModelTest, PassesInstancesToTheLookup) {
    FakeDeviceModel model;
    const auto mapping = mapping_of(R"({"generic/VendorError": {"tier_mapping": {"evse": 0},
        "v2": {"component_name": "Spd", "component_instance": "1", "variable_name": "Tripped", "variable_instance": "L1"}}})");
    validate_device_model(mapping, std::ref(model), TWO_EVSES, false);
    ASSERT_EQ(model.lookups().size(), 1U);
    EXPECT_EQ(model.lookups().front().instance.value().get(), "1");
    EXPECT_FALSE(model.lookups().front().evse.has_value());
}

Everest::error::Error error_from(std::optional<Mapping> origin_mapping) {
    Everest::error::Error error;
    error.type = "generic/VendorError";
    error.origin.module_id = "api";
    error.origin.implementation_id = "main";
    error.origin.mapping = origin_mapping;
    return error;
}

TEST(ErrorMappingOverrideTest, NoConflictWithoutEntryMapping) {
    const auto mapping = mapping_of(R"({"generic/VendorError": {"v2": {"tech_code": "A"}}})");
    EXPECT_FALSE(mapping_override(*mapping.find("generic/VendorError", ""), error_from(Mapping{1})).has_value());
}

TEST(ErrorMappingOverrideTest, NoConflictWithoutModuleMapping) {
    const auto mapping = mapping_of(R"({"generic/VendorError": {"tier_mapping": {"evse": 2}}})");
    EXPECT_FALSE(mapping_override(*mapping.find("generic/VendorError", ""), error_from(std::nullopt)).has_value());
}

TEST(ErrorMappingOverrideTest, NoConflictWithSameMapping) {
    const auto mapping = mapping_of(R"({"generic/VendorError": {"tier_mapping": {"evse": 2, "connector": 1}}})");
    EXPECT_FALSE(mapping_override(*mapping.find("generic/VendorError", ""), error_from(Mapping{2, 1})).has_value());
}

TEST(ErrorMappingOverrideTest, DescribesDifferentModuleMapping) {
    const auto mapping = mapping_of(R"({"generic/VendorError": {"tier_mapping": {"evse": 2}}})");
    const auto conflict = mapping_override(*mapping.find("generic/VendorError", ""), error_from(Mapping{1, 1}));
    ASSERT_TRUE(conflict.has_value());
    EXPECT_THAT(conflict.value(), HasSubstr("api/main to evse 2, overriding its module mapping evse 1 connector 1"));
}

} // namespace
