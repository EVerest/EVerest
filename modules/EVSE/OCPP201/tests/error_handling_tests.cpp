// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <error_handling.hpp>

#include <filesystem>
#include <stdexcept>
#include <string_view>

namespace module {

namespace {
std::filesystem::path test_data_path(std::string_view name) {
    return std::filesystem::path(TEST_DATA_DIR) / name;
}
} // namespace

TEST(GetEventData, NonEmptyMessageSetsTechInfoOnRaisedEvent) {
    // A non-empty `message` on the error is carried to the CSMS in the `techInfo` field
    const auto error = Everest::error::Error{"evse_board_support/MREC2GroundFailure",
                                             "SomeSubType",
                                             "test error message",
                                             "some description",
                                             "evse_board_support",
                                             "main"};

    const auto event_data = get_event_data(error, false, 1, MREC_ERROR_MAP);

    ASSERT_TRUE(event_data.techInfo.has_value());
    EXPECT_EQ(event_data.techInfo.value().get(), "test error message");
    ASSERT_TRUE(event_data.techCode.has_value());
    EXPECT_EQ(event_data.techCode.value().get(), "CX002");
    ASSERT_TRUE(event_data.cleared.has_value());
    EXPECT_FALSE(event_data.cleared.value());
}

TEST(GetEventData, ClearedEventEchoesSameTechInfoAsRaisedEvent) {
    // The cleared event repeats the same `techInfo` that was sent when the error was raised
    const auto error = Everest::error::Error{"evse_board_support/MREC2GroundFailure",
                                             "SomeSubType",
                                             "test error message",
                                             "some description",
                                             "evse_board_support",
                                             "main"};

    const auto raised_event_data = get_event_data(error, false, 1, MREC_ERROR_MAP);
    const auto cleared_event_data = get_event_data(error, true, 2, MREC_ERROR_MAP);

    ASSERT_TRUE(raised_event_data.techInfo.has_value());
    ASSERT_TRUE(cleared_event_data.techInfo.has_value());
    EXPECT_EQ(cleared_event_data.techInfo.value().get(), raised_event_data.techInfo.value().get());
    EXPECT_EQ(cleared_event_data.techInfo.value().get(), "test error message");
    ASSERT_TRUE(cleared_event_data.cleared.has_value());
    EXPECT_TRUE(cleared_event_data.cleared.value());
}

TEST(GetEventData, EmptyMessageFallsBackToErrorDescription) {
    // When the error has no `message`, `techInfo` falls back to the static error `description`
    const auto error = Everest::error::Error{
        "evse_board_support/MREC2GroundFailure", "SomeSubType", "", "some description", "evse_board_support", "main"};

    const auto event_data = get_event_data(error, false, 1, MREC_ERROR_MAP);

    ASSERT_TRUE(event_data.techInfo.has_value());
    EXPECT_EQ(event_data.techInfo.value().get(), "some description");
    ASSERT_TRUE(event_data.techCode.has_value());
    EXPECT_EQ(event_data.techCode.value().get(), "CX002");
}

TEST(GetEventData, OverlongMessageTruncatesTechInfoTo500Chars) {
    // A `message` longer than the `techInfo` limit is truncated to 500 chars, but still sent
    const std::string long_message(600, 'x');
    const auto error = Everest::error::Error{"evse_board_support/MREC2GroundFailure",
                                             "SomeSubType",
                                             long_message,
                                             "some description",
                                             "evse_board_support",
                                             "main"};

    const auto event_data = get_event_data(error, false, 1, MREC_ERROR_MAP);

    ASSERT_TRUE(event_data.techInfo.has_value());
    EXPECT_EQ(event_data.techInfo.value().get(), long_message.substr(0, 500));
    EXPECT_EQ(event_data.techInfo.value().get().size(), 500U);
    ASSERT_TRUE(event_data.techCode.has_value());
    EXPECT_EQ(event_data.techCode.value().get(), "CX002");
}

TEST(GetEventData, NonMrecErrorWithMessageAlsoSetsTechInfo) {
    // OCPP 2.0.1's mapping is uniform across all error types (unlike OCPP 1.6's MREC-only rule).
    const auto error = Everest::error::Error{"some_module/SomeGenericError",
                                             "SomeSubType",
                                             "custom diagnostic text",
                                             "some description",
                                             "some_module",
                                             "main"};

    const auto event_data = get_event_data(error, false, 1, MREC_ERROR_MAP);

    ASSERT_TRUE(event_data.techInfo.has_value());
    EXPECT_EQ(event_data.techInfo.value().get(), "custom diagnostic text");
    ASSERT_TRUE(event_data.techCode.has_value());
    EXPECT_EQ(event_data.techCode.value().get(), "some_module/SomeGenericError");
}

TEST(LoadMrecErrorMapOverrides, OverridesAndAddsEntries) {
    // An overrides file replaces existing mappings and adds new ones, keeping the untouched defaults
    const auto map = load_mrec_error_map_overrides(test_data_path("valid_overrides.json"));

    EXPECT_EQ(map.at("evse_manager/MREC4OverCurrentFailure"), "OVERRIDE_CX004");
    EXPECT_EQ(map.at("custom/MyNewError"), "CX999");
    EXPECT_EQ(map.at("connector_lock/MREC1ConnectorLockFailure"), "CX001");
}

TEST(LoadMrecErrorMapOverrides, EmptyObjectReturnsDefaults) {
    // An empty overrides object leaves the built-in default map unchanged
    const auto map = load_mrec_error_map_overrides(test_data_path("empty_object.json"));
    EXPECT_EQ(map, MREC_ERROR_MAP);
}

class LoadMrecErrorMapOverridesThrowsTest : public ::testing::TestWithParam<std::string_view> {};

TEST_P(LoadMrecErrorMapOverridesThrowsTest, Throws) {
    // A missing, malformed, or wrongly-typed overrides file is rejected rather than silently ignored
    EXPECT_THROW(load_mrec_error_map_overrides(test_data_path(GetParam())), std::runtime_error);
}

INSTANTIATE_TEST_SUITE_P(LoadMrecErrorMapOverrides, LoadMrecErrorMapOverridesThrowsTest,
                         ::testing::Values(std::string_view{"does_not_exist.json"}, std::string_view{"malformed.json"},
                                           std::string_view{"invalid_array.json"},
                                           std::string_view{"invalid_value_number.json"}));

} // namespace module
