// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <everest/ocpp_module_common/error_placeholders.hpp>

#include <utils/date.hpp>
#include <utils/error.hpp>

#include <optional>

namespace ocpp_module_common {

namespace {

Everest::error::Error make_error(std::optional<Mapping> mapping = Mapping{2, 3}) {
    return Everest::error::Error{"evse_board_support/MREC2GroundFailure",
                                 "SomeSubType",
                                 "test error message",
                                 "some description",
                                 ImplementationIdentifier{"evse_board_support", "main", mapping},
                                 "vendor",
                                 Everest::error::Severity::High,
                                 Everest::Date::from_rfc3339("2026-01-02T03:04:05.000Z"),
                                 Everest::error::UUID{"0123-uuid"}};
}

} // namespace

TEST(SubstituteErrorPlaceholders, ReplacesEachSupportedPlaceholder) {
    const auto error = make_error();

    EXPECT_EQ(substitute_error_placeholders(error, "${type}"), error.type);
    EXPECT_EQ(substitute_error_placeholders(error, "${sub_type}"), "SomeSubType");
    EXPECT_EQ(substitute_error_placeholders(error, "${message}"), "test error message");
    EXPECT_EQ(substitute_error_placeholders(error, "${description}"), "some description");
    EXPECT_EQ(substitute_error_placeholders(error, "${vendor_id}"), "vendor");
    EXPECT_EQ(substitute_error_placeholders(error, "${origin}"), error.origin.to_string());
    EXPECT_EQ(substitute_error_placeholders(error, "${origin_module}"), "evse_board_support");
    EXPECT_EQ(substitute_error_placeholders(error, "${origin_implementation}"), "main");
    EXPECT_EQ(substitute_error_placeholders(error, "${evse}"), "2");
    EXPECT_EQ(substitute_error_placeholders(error, "${connector}"), "3");
    EXPECT_EQ(substitute_error_placeholders(error, "${severity}"), "High");
    EXPECT_EQ(substitute_error_placeholders(error, "${state}"), Everest::error::state_to_string(error.state));
    EXPECT_EQ(substitute_error_placeholders(error, "${timestamp}"), Everest::Date::to_rfc3339(error.timestamp));
    EXPECT_EQ(substitute_error_placeholders(error, "${uuid}"), "0123-uuid");
}

TEST(SubstituteErrorPlaceholders, ReplacesMultiplePlaceholdersInText) {
    EXPECT_EQ(substitute_error_placeholders(make_error(), "Severity ${severity} on EVSE ${evse}/${connector}."),
              "Severity High on EVSE 2/3.");
}

TEST(SubstituteErrorPlaceholders, PatternWithoutPlaceholdersIsUnchanged) {
    EXPECT_EQ(substitute_error_placeholders(make_error(), "plain text $ { }"), "plain text $ { }");
    EXPECT_EQ(substitute_error_placeholders(make_error(), ""), "");
}

TEST(SubstituteErrorPlaceholders, UnknownAndEmptyPlaceholdersAreKept) {
    EXPECT_EQ(substitute_error_placeholders(make_error(), "a ${unknown} b ${} c"), "a ${unknown} b ${} c");
}

TEST(SubstituteErrorPlaceholders, UnterminatedPlaceholderIsKept) {
    EXPECT_EQ(substitute_error_placeholders(make_error(), "${severity} ${type"), "High ${type");
}

TEST(SubstituteErrorPlaceholders, SubstitutedValuesAreNotExpandedAgain) {
    auto error = make_error();
    error.message = "${type}";
    EXPECT_EQ(substitute_error_placeholders(error, "${message}"), "${type}");
}

TEST(SubstituteErrorPlaceholders, EvseAndConnectorAreEmptyWithoutMapping) {
    EXPECT_EQ(substitute_error_placeholders(make_error(std::nullopt), "[${evse}][${connector}]"), "[][]");
}

TEST(SubstituteErrorPlaceholders, ConnectorIsEmptyWhenMappingHasNoConnector) {
    EXPECT_EQ(substitute_error_placeholders(make_error(Mapping{4}), "[${evse}][${connector}]"), "[4][]");
}

TEST(IsErrorPlaceholder, KnowsEverySubstitutedName) {
    for (const auto* name : {"type", "sub_type", "message", "description", "vendor_id", "origin", "origin_module",
                             "origin_implementation", "evse", "connector", "severity", "state", "timestamp", "uuid"}) {
        EXPECT_TRUE(is_error_placeholder(name)) << name;
    }
}

TEST(IsErrorPlaceholder, RejectsOtherNames) {
    EXPECT_FALSE(is_error_placeholder(""));
    EXPECT_FALSE(is_error_placeholder("actual_value"));
    EXPECT_FALSE(is_error_placeholder("${type}"));
    EXPECT_FALSE(is_error_placeholder("Type"));
}

} // namespace ocpp_module_common
