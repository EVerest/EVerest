// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/ocpp_module_common/custom_error_mapping.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <string>
#include <type_traits>

namespace {

using namespace ocpp_module_common::custom_error_mapping;
using ocpp::v16::ChargePointErrorCode;
using ::testing::HasSubstr;

const std::filesystem::path EXAMPLE_FILE =
    std::filesystem::path(CUSTOM_ERROR_MAPPING_SCHEMAS_DIR) / "custom_error_mapping.example.json";

std::vector<Finding> errors_of(const LoadResult& result) {
    std::vector<Finding> errors;
    std::copy_if(result.findings.begin(), result.findings.end(), std::back_inserter(errors),
                 [](const Finding& finding) { return finding.level == Finding::Level::Error; });
    return errors;
}

/// \returns the single error of \p content; fails the test if there is not exactly one. An error about an entry
///          leaves that entry out of the mapping; an error about the file leaves no mapping.
Finding single_error(const std::string& content) {
    const auto result = parse_error_mapping(content);
    const auto errors = errors_of(result);
    EXPECT_EQ(errors.size(), 1U) << content;
    if (errors.empty()) {
        return Finding{};
    }
    if (errors.front().entry.empty()) {
        EXPECT_EQ(result.error_mapping, nullptr) << content;
    } else if (result.error_mapping == nullptr) {
        ADD_FAILURE() << "an error about an entry must keep the mapping: " << content;
    } else {
        EXPECT_TRUE(result.error_mapping->entries().empty()) << content;
    }
    return errors.front();
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
    ASSERT_TRUE(result.error_mapping != nullptr)
        << (result.findings.empty() ? "" : result.findings.front().to_string());
    EXPECT_TRUE(result.findings.empty());
    EXPECT_EQ(result.error_mapping->entries().size(), 5U);

    const auto* api = result.error_mapping->find("generic/VendorError", "YourCustomErrorType");
    ASSERT_NE(api, nullptr);
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
    EXPECT_EQ(api->v2->severity.value(), 5);

    const auto* internal = result.error_mapping->find("evse_board_support/MREC3HighTemperature", "");
    ASSERT_NE(internal, nullptr);
    EXPECT_EQ(internal->v16->error_code, ChargePointErrorCode::HighTemperature);

    const auto* surge = result.error_mapping->find("generic/VendorError", "SurgeProtectionDevice2");
    ASSERT_NE(surge, nullptr);
    ASSERT_TRUE(surge->v2.has_value());
    EXPECT_EQ(surge->v2->tech_code, "SPD-2");
    EXPECT_FALSE(surge->v16.has_value());

    const auto* lock = result.error_mapping->find("connector_lock/MREC1ConnectorLockFailure", "");
    ASSERT_NE(lock, nullptr);
    ASSERT_TRUE(lock->v16.has_value());
    EXPECT_EQ(lock->v16->error_code, ChargePointErrorCode::ConnectorLockFailure);
}

TEST(ErrorMappingLoaderTest, CopiesOfTheResultShareOneImmutableMapping) {
    const auto result = load_error_mapping(EXAMPLE_FILE);
    ASSERT_NE(result.error_mapping, nullptr);
    const auto copy = result;
    EXPECT_EQ(copy.error_mapping.get(), result.error_mapping.get());
    static_assert(std::is_const_v<decltype(result.error_mapping)::element_type>);
}

TEST(ErrorMappingLoaderTest, SubTypeEntryTakesPrecedenceOverTypeEntry) {
    const auto result = parse_error_mapping(R"({
        "generic/VendorError": {"v2": {"tech_code": "TYPE"}},
        "generic/VendorError#A": {"v2": {"tech_code": "SUB"}}
    })");
    ASSERT_TRUE(result.error_mapping != nullptr);
    EXPECT_EQ(result.error_mapping->find("generic/VendorError", "A")->v2->tech_code, "SUB");
    EXPECT_EQ(result.error_mapping->find("generic/VendorError", "B")->v2->tech_code, "TYPE");
    EXPECT_EQ(result.error_mapping->find("generic/VendorError", "")->v2->tech_code, "TYPE");
    EXPECT_EQ(result.error_mapping->find("generic/VendorWarning", "A"), nullptr);
}

TEST(ErrorMappingLoaderTest, SubTypeEntryWithoutTypeEntryDoesNotMatchOtherSubTypes) {
    const auto result = parse_error_mapping(R"({"generic/VendorError#A": {"v2": {"tech_code": "SUB"}}})");
    ASSERT_TRUE(result.error_mapping != nullptr);
    EXPECT_EQ(result.error_mapping->find("generic/VendorError", "B"), nullptr);
    EXPECT_EQ(result.error_mapping->find("generic/VendorError", ""), nullptr);
}

TEST(ErrorMappingLoaderTest, AcceptsEmptyFileAndSchemaReference) {
    const auto empty = parse_error_mapping("{}");
    ASSERT_TRUE(empty.error_mapping != nullptr);
    EXPECT_TRUE(empty.error_mapping->entries().empty());

    const auto with_schema = parse_error_mapping(R"({"$schema": "error_mapping.schema.json"})");
    ASSERT_TRUE(with_schema.error_mapping != nullptr);
    EXPECT_TRUE(with_schema.error_mapping->entries().empty());
}

TEST(ErrorMappingLoaderTest, RejectsTheInoperativeError) {
    for (const auto* key : {"evse_manager/Inoperative", "evse_manager/Inoperative#SubType"}) {
        const auto finding = single_error(std::string(R"({")") + key + R"(": {"v16": {"vendor_error_code": "X"}}})");
        EXPECT_EQ(finding.entry, key);
        EXPECT_THAT(finding.message, HasSubstr("cannot be mapped"));
    }
}

TEST(ErrorMappingLoaderTest, RejectsUnknownEntryField) {
    const auto finding = single_error(R"({"generic/VendorError": {"v16": {}, "v3": {}}})");
    EXPECT_EQ(finding.entry, "generic/VendorError");
    EXPECT_THAT(finding.message, HasSubstr("v3"));
}

TEST(ErrorMappingLoaderTest, RejectsUnknownSectionField) {
    const auto finding = single_error(R"({"generic/VendorError": {"v2": {"techInfo": "x"}}})");
    EXPECT_EQ(finding.entry, "generic/VendorError");
    EXPECT_THAT(finding.message, HasSubstr("techInfo"));
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

TEST(ErrorMappingLoaderTest, RejectsEmptyText) {
    EXPECT_EQ(single_error(R"({"generic/VendorError": {"v16": {"info": ""}}})").pointer,
              "/generic~1VendorError/v16/info");
    EXPECT_EQ(single_error(R"({"generic/VendorError": {"v2": {"tech_info": ""}}})").pointer,
              "/generic~1VendorError/v2/tech_info");
}

// the final length is known only after placeholders are expanded, so longer text is truncated rather than rejected
TEST(ErrorMappingLoaderTest, AcceptsTextLongerThanOcppLimit) {
    const auto result = parse_error_mapping(R"({"generic/VendorError": {"v16": {"info": ")" + std::string(51, 'x') +
                                            R"("}, "v2": {"tech_info": ")" + std::string(501, 'x') + R"("}}})");
    EXPECT_TRUE(errors_of(result).empty());
    ASSERT_NE(result.error_mapping, nullptr);
    EXPECT_EQ(result.error_mapping->entries().size(), 1U);
}

TEST(ErrorMappingLoaderTest, RejectsSeverityOutOfRange) {
    const auto finding = single_error(R"({"generic/VendorError": {"v2": {"severity": 10}}})");
    EXPECT_EQ(finding.pointer, "/generic~1VendorError/v2/severity");
}

TEST(ErrorMappingLoaderTest, RejectsNonIntegerSeverity) {
    const auto finding = single_error(R"({"generic/VendorError": {"v2": {"severity": "high"}}})");
    EXPECT_EQ(finding.pointer, "/generic~1VendorError/v2/severity");
}

TEST(ErrorMappingLoaderTest, KeepsSeverityUnsetWithoutTheField) {
    const auto result = parse_error_mapping(R"({"generic/VendorError": {"v2": {"tech_code": "A"}}})");
    ASSERT_NE(result.error_mapping, nullptr);
    const auto* entry = result.error_mapping->find("generic/VendorError", "");
    ASSERT_NE(entry, nullptr);
    ASSERT_TRUE(entry->v2.has_value());
    EXPECT_FALSE(entry->v2->severity.has_value());
}

// where an error is reported follows from the raising module's mapping in the EVerest configuration only
TEST(ErrorMappingLoaderTest, RejectsTierMapping) {
    const auto finding = single_error(R"({"generic/VendorError": {"v2": {}, "tier_mapping": {"evse": 1}}})");
    EXPECT_EQ(finding.entry, "generic/VendorError");
    EXPECT_THAT(finding.message, HasSubstr("tier_mapping"));
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

TEST(ErrorMappingLoaderTest, ReportsAllViolationsAndKeepsTheValidEntries) {
    const auto result = parse_error_mapping(R"({
        "generic/VendorError": {"v16": {"error_code": "Bad"}},
        "generic/VendorWarning": {"v2": {"unknown": 1}},
        "generic/CommunicationFault": {"v2": {"tech_code": "OK"}}
    })");
    ASSERT_NE(result.error_mapping, nullptr);
    EXPECT_EQ(result.error_mapping->entries().size(), 1U);
    EXPECT_NE(result.error_mapping->find("generic/CommunicationFault", ""), nullptr);
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
    EXPECT_TRUE(result.error_mapping != nullptr);
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
    EXPECT_FALSE(result.error_mapping != nullptr);
    ASSERT_EQ(result.findings.size(), 1U);
    EXPECT_THAT(result.findings.front().message, HasSubstr("does/not/exist.json"));
}

TEST(ErrorMappingLoaderTest, WithoutDropsTheEntriesNamedByErrors) {
    const auto result = parse_error_mapping(R"({
        "generic/VendorError": {"v2": {"tech_code": "A"}},
        "generic/VendorError#Sub": {"v2": {"tech_code": "B"}},
        "generic/VendorWarning": {"v2": {"tech_code": "C"}}
    })");
    ASSERT_NE(result.error_mapping, nullptr);
    const auto reduced = result.error_mapping->without({
        {Finding::Level::Error, "generic/VendorError#Sub", "", "dropped"},
        {Finding::Level::Warning, "generic/VendorWarning", "", "kept"},
    });
    EXPECT_EQ(reduced->entries().size(), 2U);
    EXPECT_EQ(reduced->find("generic/VendorError", "Sub")->v2->tech_code, "A");
    EXPECT_NE(reduced->find("generic/VendorWarning", ""), nullptr);
    EXPECT_EQ(result.error_mapping->entries().size(), 3U);
}

TEST(ErrorMappingLoaderTest, FindingToStringNamesEntryAndPointer) {
    const Finding finding{Finding::Level::Warning, "generic/VendorError", "/generic~1VendorError/v2", "text"};
    EXPECT_EQ(finding.to_string(), "warning in entry 'generic/VendorError' at /generic~1VendorError/v2: text");
}

} // namespace
