// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <ocpp/v16/known_keys.hpp>
#include <ocpp/v16/known_keys_yaml.hpp>

#include <ryml.hpp>
#include <ryml_std.hpp>

#include <cstddef>
#include <optional>
#include <string>

namespace {

namespace keys = ocpp::v16::keys;

#define COUNT(a, b) +1
constexpr std::size_t mapped_key_count = 0 FOR_ALL_MAPPED_KEYS(COUNT);
constexpr std::size_t unmapped_key_count = 0 FOR_ALL_UNMAPPED_KEYS(COUNT);
#undef COUNT

std::string value(const ryml::ConstNodeRef& node) {
    return {node.val().data(), node.val().size()};
}

std::optional<ryml::ConstNodeRef> find_entry(const ryml::ConstNodeRef& list, const std::string& key) {
    for (const auto& entry : list.children()) {
        if (value(entry["ocpp16_key"]) == key) {
            return entry;
        }
    }
    return std::nullopt;
}

class KnownKeysYamlTest : public ::testing::Test {
protected:
    std::string yaml{keys::default_mappings_to_yaml()};
    ryml::Tree tree{ryml::parse_in_arena(ryml::to_csubstr(yaml))};
    ryml::ConstNodeRef mappings{tree.crootref()["mappings"]};
    ryml::ConstNodeRef unmapped_keys{tree.crootref()["unmapped_keys"]};
};

TEST_F(KnownKeysYamlTest, ListsEveryKnownKey) {
    EXPECT_EQ(mappings.num_children(), mapped_key_count);
    EXPECT_EQ(unmapped_keys.num_children(), unmapped_key_count);
}

TEST_F(KnownKeysYamlTest, StandardKeyMapsToActual) {
    const auto entry = find_entry(mappings, "AllowOfflineTxForUnknownId");
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(value((*entry)["section"]), "Core");
    EXPECT_EQ(value((*entry)["component"]["name"]), "AuthCtrlr");
    EXPECT_EQ(value((*entry)["variable"]["name"]), "OfflineTxForUnknownIdEnabled");
    EXPECT_EQ(value((*entry)["target"]), "Actual");
}

TEST_F(KnownKeysYamlTest, MaxLimitKeyMapsToMaxLimit) {
    const auto entry = find_entry(mappings, "MeterValuesAlignedDataMaxLength");
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(value((*entry)["component"]["name"]), "AlignedDataCtrlr");
    EXPECT_EQ(value((*entry)["variable"]["name"]), "Measurands");
    EXPECT_EQ(value((*entry)["target"]), "maxLimit");
}

TEST_F(KnownKeysYamlTest, UnmappedKeyIsListedSeparately) {
    EXPECT_TRUE(find_entry(unmapped_keys, "CentralSystemURI").has_value());
    EXPECT_FALSE(find_entry(mappings, "CentralSystemURI").has_value());
}

TEST_F(KnownKeysYamlTest, EntriesMatchConvertV2) {
    for (const auto& entry : mappings.children()) {
        const auto key = value(entry["ocpp16_key"]);
        SCOPED_TRACE(key);
        const auto cv = keys::convert_v2(key);
        ASSERT_TRUE(cv.has_value());
        EXPECT_EQ(value(entry["component"]["name"]), cv->first.name.get());
        EXPECT_EQ(entry["component"].has_child("instance"), cv->first.instance.has_value());
        EXPECT_EQ(entry["component"].has_child("evse"), cv->first.evse.has_value());
        EXPECT_EQ(value(entry["variable"]["name"]), cv->second.name.get());
        EXPECT_EQ(entry["variable"].has_child("instance"), cv->second.instance.has_value());
    }
    for (const auto& entry : unmapped_keys.children()) {
        EXPECT_FALSE(keys::convert_v2(value(entry["ocpp16_key"])).has_value());
    }
}

} // namespace
