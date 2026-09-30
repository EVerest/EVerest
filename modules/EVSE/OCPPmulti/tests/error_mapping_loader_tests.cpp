// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "error_mapping/error_mapping.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <string>

namespace {

using namespace ocpp_multi::error_mapping;
using ocpp::v16::ChargePointErrorCode;
using ::testing::HasSubstr;

const std::filesystem::path EXAMPLE_FILE = std::filesystem::path(ERROR_MAPPING_DIR) / "error_mapping.example.json";

std::vector<Finding> errors_of(const LoadResult& result) {
    std::vector<Finding> errors;
    std::copy_if(result.findings.begin(), result.findings.end(), std::back_inserter(errors),
                 [](const Finding& finding) { return finding.level == Finding::Level::Error; });
    return errors;
}

/// \returns the single error of \p content; fails the test if there is not exactly one
Finding single_error(const std::string& content) {
    const auto result = parse_error_mapping(content);
    EXPECT_FALSE(result.error_mapping.has_value());
    const auto errors = errors_of(result);
    EXPECT_EQ(errors.size(), 1U) << content;
    return errors.empty() ? Finding{} : errors.front();
}

TEST(ErrorKeyTest, ParsesTypeOnly) {
    const auto key = parse_error_key("evse_board_support/MREC3HighTemperature");
    ASSERT_TRUE(key.has_value());
    EXPECT_EQ(key->type, "evse_board_support/MREC3HighTemperature");
    EXPECT_FALSE(key->sub_type.has_value());
    EXPECT_EQ(key->to_string(), "evse_board_support/MREC3HighTemperature");
}

TEST(ErrorKeyTest, ParsesSubTypeAfterFirstHash) {
    const auto key = parse_error_key("generic/VendorError#Surge/Device#2");
    ASSERT_TRUE(key.has_value());
    EXPECT_EQ(key->type, "generic/VendorError");
    EXPECT_EQ(key->sub_type, "Surge/Device#2");
    EXPECT_EQ(key->to_string(), "generic/VendorError#Surge/Device#2");
}

TEST(ErrorKeyTest, AcceptsUppercaseNamespace) {
    EXPECT_TRUE(parse_error_key("power_supply_DC/VendorError").has_value());
}

TEST(ErrorKeyTest, RejectsMalformedKeys) {
    for (const auto* key : {"", "VendorError", "generic/", "/VendorError", "generic/vendorError",
                            "generic/VendorError/YourCustomErrorType", "generic/VendorError#", "gen-eric/VendorError",
                            "generic/Vendor-Error"}) {
        EXPECT_FALSE(parse_error_key(key).has_value()) << key;
    }
}

TEST(ErrorMappingLoaderTest, LoadsExampleFileTyped) {
    const auto result = load_error_mapping(EXAMPLE_FILE);
    ASSERT_TRUE(result.error_mapping.has_value())
        << (result.findings.empty() ? "" : result.findings.front().to_string());
    EXPECT_TRUE(result.findings.empty());
    EXPECT_EQ(result.error_mapping->entries().size(), 4U);

    const auto* api = result.error_mapping->find("generic/VendorError", "YourCustomErrorType");
    ASSERT_NE(api, nullptr);
    EXPECT_FALSE(api->tier_mapping.has_value());
    ASSERT_TRUE(api->v16.has_value());
    EXPECT_EQ(api->v16->error_code, ChargePointErrorCode::OtherError);
    EXPECT_EQ(api->v16->vendor_id, "com.example");
    EXPECT_EQ(api->v16->vendor_error_code, "T-210");
    EXPECT_EQ(api->v16->info, "Temperature error raised at ${actual_value} deg");
    ASSERT_TRUE(api->v2.has_value());
    EXPECT_EQ(api->v2->tech_code, "T-210");
    EXPECT_EQ(api->v2->component_name, "Connector");
    EXPECT_EQ(api->v2->variable_name, "Temperature");
    EXPECT_FALSE(api->v2->component_instance.has_value());
    EXPECT_EQ(api->v2->tech_info, "Failed at connector temperature ${actual_value} deg");
    ASSERT_TRUE(api->v2->severity.has_value());
    EXPECT_EQ(api->v2->severity->high, 3);
    EXPECT_EQ(api->v2->severity->medium, 5);
    EXPECT_EQ(api->v2->severity->low, 8);

    const auto* internal = result.error_mapping->find("evse_board_support/MREC3HighTemperature", "");
    ASSERT_NE(internal, nullptr);
    EXPECT_EQ(internal->v16->error_code, ChargePointErrorCode::HighTemperature);

    const auto* surge = result.error_mapping->find("generic/VendorError", "SurgeProtectionDevice2");
    ASSERT_NE(surge, nullptr);
    ASSERT_TRUE(surge->tier_mapping.has_value());
    EXPECT_EQ(surge->tier_mapping->evse, 2);
    EXPECT_FALSE(surge->tier_mapping->connector.has_value());
    EXPECT_FALSE(surge->v16.has_value());
}

TEST(ErrorMappingLoaderTest, SubTypeEntryTakesPrecedenceOverTypeEntry) {
    const auto result = parse_error_mapping(R"({
        "generic/VendorError": {"v2": {"tech_code": "TYPE"}},
        "generic/VendorError#A": {"v2": {"tech_code": "SUB"}}
    })");
    ASSERT_TRUE(result.error_mapping.has_value());
    EXPECT_EQ(result.error_mapping->find("generic/VendorError", "A")->v2->tech_code, "SUB");
    EXPECT_EQ(result.error_mapping->find("generic/VendorError", "B")->v2->tech_code, "TYPE");
    EXPECT_EQ(result.error_mapping->find("generic/VendorError", "")->v2->tech_code, "TYPE");
    EXPECT_EQ(result.error_mapping->find("generic/VendorWarning", "A"), nullptr);
}

TEST(ErrorMappingLoaderTest, SubTypeEntryWithoutTypeEntryDoesNotMatchOtherSubTypes) {
    const auto result = parse_error_mapping(R"({"generic/VendorError#A": {"v2": {"tech_code": "SUB"}}})");
    ASSERT_TRUE(result.error_mapping.has_value());
    EXPECT_EQ(result.error_mapping->find("generic/VendorError", "B"), nullptr);
    EXPECT_EQ(result.error_mapping->find("generic/VendorError", ""), nullptr);
}

TEST(ErrorMappingLoaderTest, AcceptsEmptyFileAndSchemaReference) {
    const auto empty = parse_error_mapping("{}");
    ASSERT_TRUE(empty.error_mapping.has_value());
    EXPECT_TRUE(empty.error_mapping->entries().empty());

    const auto with_schema = parse_error_mapping(R"({"$schema": "error_mapping.schema.json"})");
    ASSERT_TRUE(with_schema.error_mapping.has_value());
    EXPECT_TRUE(with_schema.error_mapping->entries().empty());
}

TEST(ErrorMappingLoaderTest, ParsesMappingWithConnector) {
    const auto result =
        parse_error_mapping(R"({"generic/VendorError": {"tier_mapping": {"evse": 1, "connector": 2}}})");
    ASSERT_TRUE(result.error_mapping.has_value());
    const auto& mapping = result.error_mapping->find("generic/VendorError", "")->tier_mapping;
    ASSERT_TRUE(mapping.has_value());
    EXPECT_EQ(mapping->evse, 1);
    EXPECT_EQ(mapping->connector, 2);
}

TEST(ErrorMappingLoaderTest, RejectsUnknownEntryField) {
    const auto finding = single_error(R"({"generic/VendorError": {"v16": {}, "v3": {}}})");
    EXPECT_EQ(finding.entry, "generic/VendorError");
    EXPECT_THAT(finding.message, HasSubstr("v3"));
}

TEST(ErrorMappingLoaderTest, RejectsUnknownSectionField) {
    const auto finding = single_error(R"({"generic/VendorError": {"v2": {"tech_info": "x"}}})");
    EXPECT_EQ(finding.entry, "generic/VendorError");
    EXPECT_THAT(finding.message, HasSubstr("tech_info"));
}

TEST(ErrorMappingLoaderTest, RejectsWrongType) {
    const auto finding = single_error(R"({"generic/VendorError": {"v2": {"tech_code": 42}}})");
    EXPECT_EQ(finding.entry, "generic/VendorError");
    EXPECT_EQ(finding.pointer, "/generic~1VendorError/v2/tech_code");
}

TEST(ErrorMappingLoaderTest, RejectsUnknownV16ErrorCode) {
    const auto finding = single_error(R"({"generic/VendorError": {"v16": {"error_code": "HighTemprature"}}})");
    EXPECT_EQ(finding.pointer, "/generic~1VendorError/v16/error_code");
}

TEST(ErrorMappingLoaderTest, RejectsNoErrorAsV16ErrorCode) {
    const auto finding = single_error(R"({"generic/VendorError": {"v16": {"error_code": "NoError"}}})");
    EXPECT_EQ(finding.pointer, "/generic~1VendorError/v16/error_code");
}

TEST(ErrorMappingLoaderTest, RejectsFieldLongerThanOcppLimit) {
    const auto finding =
        single_error(R"({"generic/VendorError": {"v16": {"vendor_error_code": ")" + std::string(51, 'x') + R"("}}})");
    EXPECT_EQ(finding.pointer, "/generic~1VendorError/v16/vendor_error_code");
}

TEST(ErrorMappingLoaderTest, RejectsSeverityOutOfRange) {
    const auto finding =
        single_error(R"({"generic/VendorError": {"v2": {"severity": {"high": 10, "medium": 5, "low": 8}}}})");
    EXPECT_EQ(finding.pointer, "/generic~1VendorError/v2/severity/high");
}

TEST(ErrorMappingLoaderTest, RejectsIncompleteSeverity) {
    const auto result = parse_error_mapping(R"({"generic/VendorError": {"v2": {"severity": {"high": 3}}}})");
    EXPECT_FALSE(result.error_mapping.has_value());
    const auto errors = errors_of(result);
    ASSERT_EQ(errors.size(), 2U);
    EXPECT_THAT(errors[0].message, HasSubstr("medium"));
    EXPECT_THAT(errors[1].message, HasSubstr("low"));
    for (const auto& finding : errors) {
        EXPECT_EQ(finding.pointer, "/generic~1VendorError/v2/severity");
    }
}

TEST(ErrorMappingLoaderTest, RejectsNegativeEvse) {
    const auto finding = single_error(R"({"generic/VendorError": {"tier_mapping": {"evse": -1}}})");
    EXPECT_EQ(finding.pointer, "/generic~1VendorError/tier_mapping/evse");
}

TEST(ErrorMappingLoaderTest, RejectsMappingWithoutEvse) {
    const auto finding = single_error(R"({"generic/VendorError": {"tier_mapping": {"connector": 1}}})");
    EXPECT_EQ(finding.pointer, "/generic~1VendorError/tier_mapping");
}

TEST(ErrorMappingLoaderTest, RejectsEmptyEntry) {
    const auto finding = single_error(R"({"generic/VendorError": {}})");
    EXPECT_EQ(finding.entry, "generic/VendorError");
    EXPECT_THAT(finding.message, HasSubstr("at least one of"));
}

TEST(ErrorMappingLoaderTest, RejectsMalformedKey) {
    const auto finding = single_error(R"({"generic/VendorError/YourCustomErrorType": {"v16": {}}})");
    EXPECT_EQ(finding.entry, "generic/VendorError/YourCustomErrorType");
    EXPECT_THAT(finding.message, HasSubstr("invalid key"));
}

TEST(ErrorMappingLoaderTest, ReportsAllViolationsNamingTheirEntries) {
    const auto result = parse_error_mapping(R"({
        "generic/VendorError": {"v16": {"error_code": "Bad"}},
        "generic/VendorWarning": {"v2": {"unknown": 1}},
        "generic/CommunicationFault": {"v2": {"tech_code": "OK"}}
    })");
    EXPECT_FALSE(result.error_mapping.has_value());
    const auto errors = errors_of(result);
    ASSERT_EQ(errors.size(), 2U);
    std::set<std::string> entries;
    for (const auto& finding : errors) {
        entries.insert(finding.entry);
    }
    EXPECT_EQ(entries, (std::set<std::string>{"generic/VendorError", "generic/VendorWarning"}));
}

TEST(ErrorMappingLoaderTest, RejectsDuplicateEntryKey) {
    const auto finding = single_error(R"({
        "generic/VendorError": {"v2": {"tech_code": "A"}},
        "generic/VendorError": {"v2": {"tech_code": "B"}}
    })");
    EXPECT_EQ(finding.entry, "generic/VendorError");
    EXPECT_THAT(finding.message, HasSubstr("duplicate key"));
}

TEST(ErrorMappingLoaderTest, RejectsDuplicateFieldKey) {
    const auto finding = single_error(R"({"generic/VendorError": {"v2": {"tech_code": "A", "tech_code": "B"}}})");
    EXPECT_EQ(finding.entry, "generic/VendorError");
    EXPECT_EQ(finding.pointer, "/generic~1VendorError/v2/tech_code");
}

TEST(ErrorMappingLoaderTest, SameFieldInDifferentObjectsIsNoDuplicate) {
    const auto result = parse_error_mapping(R"({
        "generic/VendorError": {"v2": {"tech_code": "A"}},
        "generic/VendorWarning": {"v2": {"tech_code": "A"}}
    })");
    EXPECT_TRUE(result.error_mapping.has_value());
    EXPECT_TRUE(result.findings.empty());
}

TEST(ErrorMappingLoaderTest, RejectsMalformedJson) {
    const auto finding = single_error(R"({"generic/VendorError": {"v2": })");
    EXPECT_TRUE(finding.entry.empty());
    EXPECT_THAT(finding.message, HasSubstr("malformed JSON"));
}

TEST(ErrorMappingLoaderTest, RejectsNonObjectRoot) {
    const auto finding = single_error(R"(["generic/VendorError"])");
    EXPECT_THAT(finding.message, HasSubstr("JSON object"));
}

TEST(ErrorMappingLoaderTest, ReportsMissingFile) {
    const auto result = load_error_mapping("does/not/exist.json");
    EXPECT_FALSE(result.error_mapping.has_value());
    ASSERT_EQ(result.findings.size(), 1U);
    EXPECT_THAT(result.findings.front().message, HasSubstr("does/not/exist.json"));
}

TEST(ErrorMappingLoaderTest, FindingToStringNamesEntryAndPointer) {
    const Finding finding{Finding::Level::Warning, "generic/VendorError", "/generic~1VendorError/v2", "text"};
    EXPECT_EQ(finding.to_string(), "warning in entry 'generic/VendorError' at /generic~1VendorError/v2: text");
}

} // namespace
