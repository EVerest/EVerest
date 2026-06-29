// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 - 2026 Pionix GmbH and Contributors to EVerest

#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <everest/slac/slac_types.hpp>

#include "../src/fsm/misc.hpp"

using namespace everest::lib::slac;

namespace {

bool assert_true(bool cond, char const* test_name, char const* details) {
    if (not cond) {
        std::printf("[%s] FAIL: %s\n", test_name, details);
        return false;
    }
    return true;
}

bool test_format_mac_addr_known_value() {
    const char* test_name = "test_format_mac_addr_known_value";

    MacAddress mac{{0x00, 0x11, 0x22, 0xAA, 0xBB, 0xCC}};
    auto const formatted = format_mac_addr(mac);
    return assert_true(formatted == "00:11:22:AA:BB:CC", test_name, "MAC format was not 00:11:22:AA:BB:CC");
}

bool test_parse_mac_addr_accepts_upper_and_lowercase() {
    const char* test_name = "test_parse_mac_addr_accepts_upper_and_lowercase";

    auto const upper = parse_mac_addr("00:11:22:AA:BB:CC");
    auto const lower = parse_mac_addr("00:11:22:aa:bb:cc");
    if (!upper) {
        return assert_true(false, test_name, "Uppercase input was rejected");
    }
    if (!lower) {
        return assert_true(false, test_name, "Lowercase input was rejected");
    }
    return assert_true(upper == lower, test_name, "Parsed upper/lowercase MAC values differ");
}

bool test_parse_mac_addr_rejects_wrong_length() {
    const char* test_name = "test_parse_mac_addr_rejects_wrong_length";
    return assert_true(!parse_mac_addr("00:11:22:AA:BB"), test_name, "Short MAC string was accepted") &&
           assert_true(!parse_mac_addr("00:11:22:AA:BB:CC:DD"), test_name, "Long MAC string was accepted");
}

bool test_parse_mac_addr_rejects_bad_separators() {
    const char* test_name = "test_parse_mac_addr_rejects_bad_separators";
    return assert_true(!parse_mac_addr("00;11:22:AA:BB:CC"), test_name, "Wrong separator in first pair was accepted") &&
           assert_true(!parse_mac_addr("0011:22:AA:BB:CC"), test_name, "Missing separator was accepted");
}

bool test_parse_mac_addr_rejects_non_hex() {
    const char* test_name = "test_parse_mac_addr_rejects_non_hex";
    return assert_true(!parse_mac_addr("00:11:22:AX:BB:CC"), test_name, "Non-hex characters were accepted");
}

bool test_parse_mac_addr_legacy_output_unchanged_on_failure() {
    const char* test_name = "test_parse_mac_addr_legacy_output_unchanged_on_failure";

    MacAddress mac{{0x01, 0x02, 0x03, 0x04, 0x05, 0x06}};
    auto const before = mac;
    auto const result = parse_mac_addr("00:11:22:33:44:ZZ", mac.data(), mac.size());
    return assert_true(result == false, test_name, "Invalid parse reported success") &&
           assert_true(mac == before, test_name, "Legacy output was modified after parse failure");
}

bool test_parse_and_format_round_trip() {
    const char* test_name = "test_parse_and_format_round_trip";
    auto const parsed = parse_mac_addr("ab:cd:ef:12:34:56");
    if (!parsed) {
        return assert_true(false, test_name, "Valid MAC was rejected");
    }
    auto const formatted = format_mac_addr(*parsed);
    return assert_true(formatted == "AB:CD:EF:12:34:56", test_name, "Round trip did not preserve value");
}

} // namespace

int main() {
    std::array<std::pair<const char*, bool (*)()>, 7> tests{{
        {"test_format_mac_addr_known_value", test_format_mac_addr_known_value},
        {"test_parse_mac_addr_accepts_upper_and_lowercase", test_parse_mac_addr_accepts_upper_and_lowercase},
        {"test_parse_mac_addr_rejects_wrong_length", test_parse_mac_addr_rejects_wrong_length},
        {"test_parse_mac_addr_rejects_bad_separators", test_parse_mac_addr_rejects_bad_separators},
        {"test_parse_mac_addr_rejects_non_hex", test_parse_mac_addr_rejects_non_hex},
        {"test_parse_mac_addr_legacy_output_unchanged_on_failure",
         test_parse_mac_addr_legacy_output_unchanged_on_failure},
        {"test_parse_and_format_round_trip", test_parse_and_format_round_trip},
    }};

    int failed_count = 0;
    for (auto const& test : tests) {
        if (not test.second()) {
            std::printf("[FAIL] %s\n", test.first);
            ++failed_count;
        } else {
            std::printf("[PASS] %s\n", test.first);
        }
    }

    if (failed_count > 0) {
        std::printf("FAILED (%d)\n", failed_count);
        return EXIT_FAILURE;
    }

    std::printf("PASSED\n");
    return EXIT_SUCCESS;
}
