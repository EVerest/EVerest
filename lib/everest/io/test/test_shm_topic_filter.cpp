// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <cstdlib>
#include <iostream>

#include <everest/io/shm/topic_filter.hpp>

using everest::lib::io::shm::is_valid_topic_filter;
using everest::lib::io::shm::topic_filter_matches;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << "\n";
        std::exit(1);
    }
}

void test_exact_matches_and_mismatches() {
    require(topic_filter_matches("telemetry/current", "telemetry/current"), "literal topic should match exactly");
    require(!topic_filter_matches("telemetry/current", "telemetry/voltage"),
            "different literal topic should not match");
    require(!topic_filter_matches("telemetry/current", "telemetry/current/phase_a"),
            "literal topic should not match extra levels");
    require(!topic_filter_matches("telemetry/current/phase_a", "telemetry/current"),
            "literal topic should not match missing levels");
}

void test_single_level_wildcard_matches_and_mismatches() {
    require(topic_filter_matches("telemetry/+/voltage", "telemetry/evse_1/voltage"),
            "single-level wildcard should match one level");
    require(topic_filter_matches("+/evse_1/voltage", "telemetry/evse_1/voltage"),
            "single-level wildcard should match first level");
    require(topic_filter_matches("telemetry/evse_1/+", "telemetry/evse_1/voltage"),
            "single-level wildcard should match final level");
    require(!topic_filter_matches("telemetry/+/voltage", "telemetry/evse_1/ac/voltage"),
            "single-level wildcard should not match multiple levels");
    require(!topic_filter_matches("telemetry/+/voltage", "telemetry/voltage"),
            "single-level wildcard should not match zero levels");
}

void test_multi_level_wildcard_matches_and_mismatches() {
    require(topic_filter_matches("telemetry/#", "telemetry"), "trailing wildcard should match zero remaining levels");
    require(topic_filter_matches("telemetry/#", "telemetry/evse_1"),
            "trailing wildcard should match one remaining level");
    require(topic_filter_matches("telemetry/#", "telemetry/evse_1/voltage"),
            "trailing wildcard should match multiple remaining levels");
    require(topic_filter_matches("#", "telemetry/evse_1/voltage"), "bare trailing wildcard should match any topic");
    require(!topic_filter_matches("telemetry/evse_1/#", "telemetry/evse_2/voltage"),
            "trailing wildcard should still require matching prefix");
}

void test_invalid_wildcard_placement() {
    require(!is_valid_topic_filter("telemetry/#/voltage"), "multi-level wildcard should be final");
    require(!is_valid_topic_filter("telemetry/voltage#"), "multi-level wildcard should occupy a full level");
    require(!is_valid_topic_filter("telemetry/+voltage"), "single-level wildcard should occupy a full level");
    require(!is_valid_topic_filter("telemetry/voltage+"), "single-level wildcard should not suffix a literal");
    require(!topic_filter_matches("telemetry/#/voltage", "telemetry/evse_1/voltage"),
            "invalid multi-level wildcard placement should not match");
    require(!topic_filter_matches("telemetry/+voltage", "telemetry/evse_1"),
            "invalid single-level wildcard placement should not match");
}

void test_empty_topic_levels() {
    require(topic_filter_matches("telemetry/+/voltage", "telemetry//voltage"),
            "single-level wildcard should match an empty middle level");
    require(topic_filter_matches("+/telemetry", "/telemetry"),
            "single-level wildcard should match an empty leading level");
    require(topic_filter_matches("telemetry/+", "telemetry/"),
            "single-level wildcard should match an empty trailing level");
    require(topic_filter_matches("telemetry//voltage", "telemetry//voltage"),
            "literal empty levels should match exactly");
    require(!topic_filter_matches("telemetry//voltage", "telemetry/evse_1/voltage"),
            "literal empty levels should not match non-empty levels");
}

void test_no_regex_or_glob_behavior() {
    require(!topic_filter_matches("telemetry/*/voltage", "telemetry/evse_1/voltage"),
            "asterisk should be treated as a literal");
    require(topic_filter_matches("telemetry/*/voltage", "telemetry/*/voltage"), "literal asterisk should match itself");
    require(!topic_filter_matches("telemetry/evse.?/voltage", "telemetry/evse_1/voltage"),
            "regex punctuation should be treated as literal text");
    require(!topic_filter_matches("telemetry/[abc]/voltage", "telemetry/a/voltage"),
            "character classes should not behave like regex");
}

void test_empty_filter_or_topic_is_rejected() {
    require(!is_valid_topic_filter(""), "empty filter should be invalid");
    require(!topic_filter_matches("", "telemetry"), "empty filter should not match");
    require(!topic_filter_matches("#", ""), "empty topic should not match");
}

int main() {
    test_exact_matches_and_mismatches();
    test_single_level_wildcard_matches_and_mismatches();
    test_multi_level_wildcard_matches_and_mismatches();
    test_invalid_wildcard_placement();
    test_empty_topic_levels();
    test_no_regex_or_glob_behavior();
    test_empty_filter_or_topic_is_rejected();
}
