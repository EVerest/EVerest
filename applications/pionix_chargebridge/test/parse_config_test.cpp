// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

// Which configs load, what a loaded one reads, and that a rejected one names its key and the line
// and column it sits at. Each case edits the shipped EV config and hands the result to
// parse_config_multi through a file.

#include <charge_bridge/utilities/parse_config.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

std::string shipped_config() {
    std::ifstream file(std::string(CB_CONFIG_DIR) + "/config-CB-EVAL-EV.yaml");
    std::stringstream content;
    content << file.rdbuf();
    return content.str();
}

struct ConfigCase {
    std::string name;
    // Each "from" must occur exactly once in the shipped config; an empty one prepends "to".
    std::vector<std::pair<std::string, std::string>> edits;
    bool loads;
    std::vector<std::string> messages;
    // Text in the edited config, each occurring once, whose position the error must name.
    std::vector<std::string> at;
    std::optional<std::uint16_t> telemetry_port;
    std::vector<std::string> remotes;
};

void PrintTo(ConfigCase const& c, std::ostream* os) {
    *os << c.name;
}

std::size_t count(std::string const& text, std::string const& part) {
    std::size_t n = 0;
    for (auto pos = text.find(part); pos != std::string::npos; pos = text.find(part, pos + 1)) {
        ++n;
    }
    return n;
}

std::string apply(ConfigCase const& c) {
    auto config = shipped_config();
    for (auto const& [from, to] : c.edits) {
        if (from.empty()) {
            config = to + config;
            continue;
        }
        EXPECT_EQ(count(config, from), 1u) << "anchor must occur once: " << from;
        auto pos = config.find(from);
        if (pos != std::string::npos) {
            config.replace(pos, from.size(), to);
        }
    }
    return config;
}

// ryml locates a sequence entry at its value, and a quoted scalar after its opening quote.
std::string location(std::string const& config, std::string const& text) {
    EXPECT_EQ(count(config, text), 1u) << "location text must occur once: " << text;
    auto pos = config.find(text);
    if (pos == std::string::npos) {
        return "<" + text + " not found>";
    }
    auto value = text;
    if (value.rfind("- ", 0) == 0) {
        pos += 2;
        value = value.substr(2);
    }
    auto line_start = config.rfind('\n', pos);
    line_start = line_start == std::string::npos ? 0 : line_start + 1;
    auto line = std::count(config.begin(), config.begin() + static_cast<std::ptrdiff_t>(pos), '\n') + 1;
    auto column = pos - line_start + 1 + (value.front() == '"' ? 1 : 0);
    return "line " + std::to_string(line) + " column " + std::to_string(column);
}

class ParseConfig : public testing::TestWithParam<ConfigCase> {
protected:
    void SetUp() override {
        auto pattern = (std::filesystem::path(testing::TempDir()) / "parse_config_XXXXXX").string();
        ASSERT_NE(mkdtemp(pattern.data()), nullptr);
        m_dir = pattern;
    }

    void TearDown() override {
        if (not m_dir.empty()) {
            std::filesystem::remove_all(m_dir);
        }
    }

    std::filesystem::path m_dir;
};

TEST_P(ParseConfig, LoadsOrNamesTheProblem) {
    auto const& c = GetParam();
    auto config = apply(c);
    auto path = m_dir / "config.yaml";
    {
        std::ofstream file(path);
        file << config;
        ASSERT_TRUE(file.good()) << path;
    }

    testing::internal::CaptureStderr();
    auto configs = charge_bridge::utilities::parse_config_multi(path.string());
    auto errors = testing::internal::GetCapturedStderr();

    ASSERT_EQ(not configs.empty(), c.loads) << errors;
    for (auto const& message : c.messages) {
        EXPECT_NE(errors.find(message), std::string::npos) << "missing '" << message << "' in:\n" << errors;
    }
    for (auto const& text : c.at) {
        // Followed by the offset or the line end, so "line 2" cannot match "line 22".
        auto expected = location(config, text);
        auto named =
            errors.find(expected + " ") != std::string::npos or errors.find(expected + "\n") != std::string::npos;
        EXPECT_TRUE(named) << "missing '" << expected << "' in:\n" << errors;
    }
    if (c.telemetry_port) {
        ASSERT_TRUE(configs.front().telemetry.has_value());
        EXPECT_EQ(configs.front().telemetry->mqtt_port, *c.telemetry_port);
    }
    if (not c.remotes.empty()) {
        ASSERT_EQ(configs.size(), c.remotes.size());
        for (std::size_t i = 0; i < c.remotes.size(); ++i) {
            EXPECT_EQ(configs[i].cb_remote, c.remotes[i]);
        }
    }
}

const std::string telemetry_port = "mqtt_port: 1883\n  telemetry_topic";
const std::string plc_enable = "plc:\n  enable: true";

ConfigCase port(std::string name, std::string value, std::optional<std::uint16_t> reads,
                std::vector<std::string> messages = {}) {
    auto text = "mqtt_port: " + value;
    std::vector<std::string> at;
    if (not reads) {
        at.push_back(text);
    }
    return {std::move(name),
            {{telemetry_port, text + "\n  telemetry_topic"}},
            reads.has_value(),
            std::move(messages),
            at,
            reads,
            {}};
}

const ConfigCase cases[] = {
    {"shipped", {}, true, {}, {}, 1883, {}},
    {"leading_document_marker", {{"", "---\n"}}, true, {}, {}, 1883, {}},
    {"two_documents", {{"", "foo: 1\n---\n"}}, false, {"2 YAML documents"}, {}, {}, {}},

    port("plus_sign", "+1883", 1883),
    port("hex", "0x75B", 1883),
    port("largest_port", "65535", 65535),
    port("largest_port_with_plus_sign", "+65535", 65535),
    port("overflow", "65536", {}, {"telemetry::mqtt_port", "value out of range"}),
    port("hex_overflow", "0x10000", {}, {"value out of range"}),
    port("wrapping_overflow", "99999999", {}, {"value out of range"}),
    port("plus_sign_overflow", "+99999999", {}, {"value out of range"}),
    port("negative_unsigned", "-1", {}, {"telemetry::mqtt_port"}),
    {"nested_overflow",
     {{"cp_avg_ms: 10", "cp_avg_ms: 99999999999"}},
     false,
     {"cp_avg_ms", "value out of range"},
     {"cp_avg_ms: 99999999999"},
     {},
     {}},

    {"plc_missing",
     {{"plc:\n  enable: true\n  tap: \"cb_plc_ev\"\n  ip: 172.25.6.1\n  netmask: 255.255.255.0\n  mtu: 1438\n"
       "  powersaving_mode: 1\n",
       ""}},
     true,
     {},
     {},
     1883,
     {}},
    {"plc_without_powersaving_mode", {{"  powersaving_mode: 1\n", ""}}, true, {}, {}, 1883, {}},

    {"bad_bool",
     {{"telemetry:\n  enable: true", "telemetry:\n  enable: maybe"}},
     false,
     {"telemetry::enable"},
     {"enable: maybe"},
     {},
     {}},
    {"empty_optional",
     {{"connection_to_s: 10", "connection_to_s:"}},
     false,
     {"heartbeat::connection_to_s"},
     {"connection_to_s:"},
     {},
     {}},
    {"sequence_for_optional",
     {{"interval_s: 1\n  connection_to_s", "interval_s: [1]\n  connection_to_s"}},
     false,
     {"heartbeat::interval_s"},
     {"interval_s: [1]"},
     {},
     {}},
    {"map_for_optional",
     {{"interval_s: 1\n  connection_to_s", "interval_s: {a: 1}\n  connection_to_s"}},
     false,
     {"heartbeat::interval_s"},
     {"interval_s: {a: 1}"},
     {},
     {}},
    {"missing_key",
     {{"  mqtt_remote: \"localhost\"\n  " + telemetry_port, "  " + telemetry_port}},
     false,
     {"Node not found: telemetry::mqtt_remote, missing from the block at"},
     {"telemetry:\n"},
     {},
     {}},

    {"empty_block",
     {{"heartbeat:\n", "heartbeat:\nunused:\n"}},
     false,
     {"heartbeat must be a block of settings"},
     {"heartbeat:\nunused"},
     {},
     {}},
    {"scalar_for_block",
     {{"telemetry:\n", "telemetry: 5\nunused:\n"}},
     false,
     {"telemetry must be a block of settings"},
     {"telemetry: 5"},
     {},
     {}},
    {"scalar_for_converted_block",
     {{"safety:\n", "safety: 5\nunused:\n"}},
     false,
     {"safety must be a block of settings"},
     {"safety: 5"},
     {},
     {}},
    {"scalar_for_nested_block",
     {{"  relay_1:\n", "  relay_1: 5\n  unused:\n"}},
     false,
     {"relay_1 must be a block of settings"},
     {"relay_1: 5"},
     {},
     {}},

    {"bad_enum",
     {{"stopbits: OneStopBit\n  parity: None", "stopbits: Foo\n  parity: None"}},
     false,
     {"stopbits::Foo must be one of OneStopBit, TwoStopBits"},
     {"stopbits: Foo"},
     {},
     {}},
    {"bad_connector_type",
     {{"ev_bsp:\n  enable: true", "ev_bsp:\n  enable: false"},
      {"evse_bsp:\n  enable: false", "evse_bsp:\n  enable: true"},
      {"connector_type: \"IEC62196Type2Cable\"", "connector_type: Bogus"}},
     false,
     {"connector_type::Bogus is not an evse_board_support Connector_type"},
     {"connector_type: Bogus"},
     {},
     {}},
    {"bad_type",
     {{"  name: cb_eval_ev", "  name: cb_eval_ev\n  type: XX"}},
     false,
     {"charge_bridge::type must be 'EVSE' or 'EV', got 'XX'"},
     {"type: XX"},
     {},
     {}},
    {"bad_discovery_endpoint",
     {{"  ip: ANY_EV\n", "  ip: ANY_EVx\n"}},
     false,
     {"charge_bridge::ip 'ANY_EVx' is not a valid discovery endpoint"},
     {"ip: ANY_EVx"},
     {},
     {}},
    {"bad_carrier",
     {{plc_enable, plc_enable + "\n  carrier: bogus"}},
     false,
     {"plc::carrier must be 'none' or 'firmware', got 'bogus'"},
     {"carrier: bogus"},
     {},
     {}},
    {"carrier_gate_without_firmware",
     {{plc_enable, plc_enable + "\n  carrier_gate: ce_mated"}},
     false,
     {"plc::carrier_gate requires plc::carrier: firmware"},
     {"carrier_gate: ce_mated"},
     {},
     {}},
    {"station_id_out_of_range",
     {{plc_enable, plc_enable + "\n  station_id: 9"}},
     false,
     {"plc::station_id 9 is not a PLCA node id"},
     {"station_id: 9"},
     {},
     {}},
    {"renamed_gpio_block",
     {{"\nio:\n", "\ngpio:\n  enable: false\nio:\n"}},
     false,
     {"the 'gpio' section was renamed to 'io'"},
     {"gpio:"},
     {},
     {}},
    {"both_bsps_with_fixed_ip",
     {{"  ip: ANY_EV\n", "  ip: 10.0.0.5\n"}, {"evse_bsp:\n  enable: false", "evse_bsp:\n  enable: true"}},
     false,
     {"Cannot enable EVSE and EV BSP at the same time"},
     {"enable: true\n  module_id: \"ev_bsp\"", "enable: true\n  module_id: \"bsp_1\""},
     {},
     {}},

    {"ip_list",
     {{"", "charge_bridge_ip_list:\n  - 10.0.0.5\n  - \"[fd00::6]\"\n"}},
     true,
     {},
     {},
     1883,
     {"10.0.0.5", "fd00::6"}},
    {"ip_list_map",
     {{"", "charge_bridge_ip_list:\n  a: 10.0.0.5\n"}},
     false,
     {"must be a non-empty list"},
     {"charge_bridge_ip_list:"},
     {},
     {}},
    {"ip_list_empty",
     {{"", "charge_bridge_ip_list: []\n"}},
     false,
     {"must be a non-empty list"},
     {"charge_bridge_ip_list:"},
     {},
     {}},
    {"ip_list_empty_entry",
     {{"", "charge_bridge_ip_list:\n  - \"\"\n"}},
     false,
     {"entries must be addresses"},
     {"- \"\""},
     {},
     {}},
};

INSTANTIATE_TEST_SUITE_P(Cases, ParseConfig, testing::ValuesIn(cases),
                         [](auto const& info) { return info.param.name; });

TEST(ParseConfigFile, MissingFileNamesTheReason) {
    testing::internal::CaptureStderr();
    auto configs = charge_bridge::utilities::parse_config_multi("/nonexistent/config.yaml");
    auto errors = testing::internal::GetCapturedStderr();

    EXPECT_TRUE(configs.empty());
    EXPECT_NE(errors.find("No such file or directory"), std::string::npos) << errors;
    EXPECT_NE(errors.find("/nonexistent/config.yaml"), std::string::npos) << errors;
}

TEST(ParseConfigFile, DirectoryNamesTheReason) {
    testing::internal::CaptureStderr();
    auto configs = charge_bridge::utilities::parse_config_multi(std::filesystem::temp_directory_path().string());
    auto errors = testing::internal::GetCapturedStderr();

    EXPECT_TRUE(configs.empty());
    EXPECT_NE(errors.find("Is a directory"), std::string::npos) << errors;
}

} // namespace
