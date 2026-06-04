// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <bridge_config.hpp>

using everest::lib::io::shm_to_mqtt_bridge::app_config;
using everest::lib::io::shm_to_mqtt_bridge::mqtt_topic_for;
using everest::lib::io::shm_to_mqtt_bridge::parse_config_from_string;
using everest::lib::io::shm_to_mqtt_bridge::resolve_subscription_topics;
using everest::lib::io::shm_to_mqtt_bridge::shm_config;
using everest::lib::io::shm_to_mqtt_bridge::topic_resolution;
using everest::lib::io::shm_to_mqtt_bridge::trim_slashes;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << "\n";
        std::exit(1);
    }
}

template <typename Func> void require_throws(Func&& func, const char* message) {
    try {
        func();
    } catch (const std::runtime_error&) {
        return;
    } catch (const std::exception&) {
        return;
    }
    std::cerr << "FAILED: " << message << "\n";
    std::exit(1);
}

app_config parse_yaml(std::string_view yaml) {
    return parse_config_from_string(yaml, "test.yaml");
}

void test_parse_explicit_topics_only() {
    const auto config = parse_yaml(R"(bridge:
  name: shm-inspect

shm:
  client_id: shm-bridge
  control_socket: /tmp/everest-shm-control.sock
  topics:
    - ping_pong/ping
    - ping_pong/pong

mqtt:
  host: 127.0.0.1
  client_id: shm-bridge
)");
    require(config.shm.topics.size() == 2U, "parser should accept explicit topics");
    require(config.shm.topic_filters.empty(), "explicit-topics config should leave filters empty");
    require(!config.shm.subscribe_all, "explicit-topics config should default subscribe_all to false");
    require(config.shm.topics[0] == "ping_pong/ping", "parser should preserve first explicit topic");
    require(config.shm.topics[1] == "ping_pong/pong", "parser should preserve second explicit topic");
}

void test_parse_subscribe_all() {
    const auto config = parse_yaml(R"(bridge:
  name: shm-inspect

shm:
  client_id: shm-bridge
  control_socket: /tmp/everest-shm-control.sock
  subscribe_all: true

mqtt:
  host: 127.0.0.1
  client_id: shm-bridge
)");
    require(config.shm.subscribe_all, "parser should accept subscribe_all=true");
    require(config.shm.topics.empty(), "subscribe_all config may have no explicit topics");
    require(config.shm.topic_filters.empty(), "subscribe_all config may have no filters");
}

void test_parse_topic_filters() {
    const auto config = parse_yaml(R"(bridge:
  name: shm-inspect

shm:
  client_id: shm-bridge
  control_socket: /tmp/everest-shm-control.sock
  topic_filters:
    - everest/#
    - everest/modules/+/state

mqtt:
  host: 127.0.0.1
  client_id: shm-bridge
)");
    require(config.shm.topic_filters.size() == 2U, "parser should accept multiple filters");
    require(config.shm.topic_filters[0] == "everest/#", "parser should preserve first filter");
    require(config.shm.topic_filters[1] == "everest/modules/+/state", "parser should preserve second filter");
}

void test_parse_requires_at_least_one_selector() {
    require_throws(
        []() {
            parse_yaml(R"(bridge:
  name: shm-inspect

shm:
  client_id: shm-bridge
  control_socket: /tmp/everest-shm-control.sock

mqtt:
  host: 127.0.0.1
  client_id: shm-bridge
)");
        },
        "config with no shm selector should be rejected");
}

void test_parse_rejects_invalid_topic_filter() {
    require_throws(
        []() {
            parse_yaml(R"(bridge:
  name: shm-inspect

shm:
  client_id: shm-bridge
  control_socket: /tmp/everest-shm-control.sock
  topic_filters:
    - everest/#/voltage

mqtt:
  host: 127.0.0.1
  client_id: shm-bridge
)");
        },
        "config with invalid topic filter should be rejected");
}

void test_parse_rejects_wildcards_in_explicit_topics() {
    require_throws(
        []() {
            parse_yaml(R"(bridge:
  name: shm-inspect

shm:
  client_id: shm-bridge
  control_socket: /tmp/everest-shm-control.sock
  topics:
    - everest/+

mqtt:
  host: 127.0.0.1
  client_id: shm-bridge
)");
        },
        "config with wildcard in shm.topics should be rejected");
}

void test_parse_rejects_duplicate_explicit_topic() {
    require_throws(
        []() {
            parse_yaml(R"(bridge:
  name: shm-inspect

shm:
  client_id: shm-bridge
  control_socket: /tmp/everest-shm-control.sock
  topics:
    - ping_pong/ping
    - ping_pong/ping

mqtt:
  host: 127.0.0.1
  client_id: shm-bridge
)");
        },
        "config with duplicate explicit topic should be rejected");
}

void test_parse_rejects_duplicate_filter() {
    require_throws(
        []() {
            parse_yaml(R"(bridge:
  name: shm-inspect

shm:
  client_id: shm-bridge
  control_socket: /tmp/everest-shm-control.sock
  topic_filters:
    - everest/#
    - everest/#

mqtt:
  host: 127.0.0.1
  client_id: shm-bridge
)");
        },
        "config with duplicate filter should be rejected");
}

void test_parse_accepts_combined_selectors() {
    const auto config = parse_yaml(R"(bridge:
  name: shm-inspect

shm:
  client_id: shm-bridge
  control_socket: /tmp/everest-shm-control.sock
  topics:
    - ping_pong/ping
  topic_filters:
    - everest/+/state
  subscribe_all: false

mqtt:
  host: 127.0.0.1
  client_id: shm-bridge
)");
    require(config.shm.topics.size() == 1U, "combined config should keep explicit topics");
    require(config.shm.topic_filters.size() == 1U, "combined config should keep filters");
    require(!config.shm.subscribe_all, "combined config should keep explicit subscribe_all=false");
}

void test_mqtt_topic_for_strips_slashes() {
    everest::lib::io::shm_to_mqtt_bridge::mqtt_config mqtt;
    mqtt.topic_prefix = "/everest/shm_inspect/";
    require(mqtt_topic_for(mqtt, "ping_pong/ping") == "everest/shm_inspect/ping_pong/ping",
            "mqtt_topic_for should normalize trailing slashes");
    require(trim_slashes("/foo/") == "foo", "trim_slashes should strip leading and trailing slashes");
    require(trim_slashes("") == "", "trim_slashes should accept empty string");
}

shm_config make_shm_config(std::vector<std::string> topics, std::vector<std::string> filters, bool subscribe_all) {
    shm_config config;
    config.client_id = "bridge";
    config.control_socket = "/tmp/everest-shm-control.sock";
    config.topics = std::move(topics);
    config.topic_filters = std::move(filters);
    config.subscribe_all = subscribe_all;
    return config;
}

void test_resolve_explicit_topics_only() {
    const auto config = make_shm_config({"ping_pong/ping", "ping_pong/pong"}, {}, false);
    const auto resolution = resolve_subscription_topics(config, {}, false);
    require(resolution.topics.size() == 2U, "resolution should keep both explicit topics");
    require(resolution.topics[0] == "ping_pong/ping", "resolution should sort explicit topics");
    require(resolution.topics[1] == "ping_pong/pong", "resolution should sort explicit topics");
    require(resolution.unmatched_filters.empty(), "explicit-topics resolution should not report unmatched filters");
    require(!resolution.registry_queried, "explicit-topics resolution should not have queried the registry");
}

void test_resolve_subscribe_all_unions_with_explicit_topics() {
    const auto config = make_shm_config({"local/explicit"}, {}, true);
    const std::vector<std::string> registered{"telemetry/a", "telemetry/b"};
    const auto resolution = resolve_subscription_topics(config, registered, true);
    require(resolution.topics.size() == 3U, "subscribe_all resolution should union explicit and registered");
    require(resolution.topics[0] == "local/explicit", "subscribe_all resolution should sort union");
    require(resolution.topics[1] == "telemetry/a", "subscribe_all resolution should include registered");
    require(resolution.topics[2] == "telemetry/b", "subscribe_all resolution should include registered");
    require(resolution.registry_queried, "subscribe_all resolution should mark registry queried");
}

void test_resolve_filters_expand_against_registry() {
    const auto config = make_shm_config({}, {"telemetry/+", "external/#"}, false);
    const std::vector<std::string> registered{"telemetry/a", "telemetry/b", "external/x", "internal/y"};
    const auto resolution = resolve_subscription_topics(config, registered, true);
    require(resolution.topics.size() == 3U, "filter resolution should match three topics");
    require(resolution.topics[0] == "external/x", "filter resolution should include external/x");
    require(resolution.topics[1] == "telemetry/a", "filter resolution should include telemetry/a");
    require(resolution.topics[2] == "telemetry/b", "filter resolution should include telemetry/b");
    require(resolution.unmatched_filters.empty(), "filter resolution should match all configured filters");
}

void test_resolve_filters_dedup_overlap_with_explicit() {
    const auto config = make_shm_config({"telemetry/a"}, {"telemetry/+"}, false);
    const std::vector<std::string> registered{"telemetry/a", "telemetry/b"};
    const auto resolution = resolve_subscription_topics(config, registered, true);
    require(resolution.topics.size() == 2U, "filter+explicit overlap should dedup");
    require(resolution.topics[0] == "telemetry/a", "dedup union should contain telemetry/a once");
    require(resolution.topics[1] == "telemetry/b", "dedup union should add telemetry/b");
}

void test_resolve_subscribe_all_dedup_with_filters() {
    const auto config = make_shm_config({}, {"telemetry/+"}, true);
    const std::vector<std::string> registered{"telemetry/a", "telemetry/b", "other/c"};
    const auto resolution = resolve_subscription_topics(config, registered, true);
    require(resolution.topics.size() == 3U, "subscribe_all + filter should still dedup");
    require(resolution.topics[0] == "other/c", "dedup union should include other/c via subscribe_all");
}

void test_resolve_unmatched_filter_is_reported() {
    const auto config = make_shm_config({}, {"telemetry/+", "missing/#"}, false);
    const std::vector<std::string> registered{"telemetry/a"};
    const auto resolution = resolve_subscription_topics(config, registered, true);
    require(resolution.topics.size() == 1U, "matched filter should yield one topic");
    require(resolution.topics[0] == "telemetry/a", "matched filter should yield telemetry/a");
    require(resolution.unmatched_filters.size() == 1U, "unmatched filter should be reported");
    require(resolution.unmatched_filters[0] == "missing/#", "unmatched filter should preserve filter string");
}

void test_resolve_empty_registry_with_subscribe_all_is_empty() {
    const auto config = make_shm_config({}, {}, true);
    const auto resolution = resolve_subscription_topics(config, {}, true);
    require(resolution.topics.empty(), "subscribe_all against empty registry should yield no topics");
    require(resolution.unmatched_filters.empty(), "no filters configured should report no unmatched");
    require(resolution.registry_queried, "subscribe_all resolution should mark registry queried");
}

void test_resolve_empty_registry_with_filters_reports_unmatched() {
    const auto config = make_shm_config({}, {"telemetry/+"}, false);
    const auto resolution = resolve_subscription_topics(config, {}, true);
    require(resolution.topics.empty(), "filter against empty registry should yield no topics");
    require(resolution.unmatched_filters.size() == 1U, "filter against empty registry should be unmatched");
    require(resolution.unmatched_filters[0] == "telemetry/+", "unmatched filter should preserve filter string");
}

void test_resolve_filters_with_explicit_still_subscribe_to_explicit() {
    const auto config = make_shm_config({"explicit/keep"}, {"missing/#"}, false);
    const std::vector<std::string> registered{"telemetry/a"};
    const auto resolution = resolve_subscription_topics(config, registered, true);
    require(resolution.topics.size() == 1U, "explicit-only match should still resolve to one topic");
    require(resolution.topics[0] == "explicit/keep", "explicit topic should still be present");
    require(resolution.unmatched_filters.size() == 1U, "unmatched filter should still be reported");
}

int main() {
    test_parse_explicit_topics_only();
    test_parse_subscribe_all();
    test_parse_topic_filters();
    test_parse_requires_at_least_one_selector();
    test_parse_rejects_invalid_topic_filter();
    test_parse_rejects_wildcards_in_explicit_topics();
    test_parse_rejects_duplicate_explicit_topic();
    test_parse_rejects_duplicate_filter();
    test_parse_accepts_combined_selectors();
    test_mqtt_topic_for_strips_slashes();

    test_resolve_explicit_topics_only();
    test_resolve_subscribe_all_unions_with_explicit_topics();
    test_resolve_filters_expand_against_registry();
    test_resolve_filters_dedup_overlap_with_explicit();
    test_resolve_subscribe_all_dedup_with_filters();
    test_resolve_unmatched_filter_is_reported();
    test_resolve_empty_registry_with_subscribe_all_is_empty();
    test_resolve_empty_registry_with_filters_reports_unmatched();
    test_resolve_filters_with_explicit_still_subscribe_to_explicit();

    return 0;
}
