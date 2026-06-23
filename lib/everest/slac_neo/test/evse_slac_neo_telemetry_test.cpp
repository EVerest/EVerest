// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 - 2026 Pionix GmbH and Contributors to EVerest

#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>

#include <everest/slac/telemetry.hpp>
#include <nlohmann/json.hpp>

using namespace everest::lib::slac;

namespace {

bool assert_true(bool cond, const char* test_name, const char* details) {
    if (not cond) {
        std::printf("[%s] FAIL: %s\n", test_name, details);
        return false;
    }
    return true;
}

bool test_default_slac_telemetry_serializes_with_zero_ev_mac() {
    const char* test_name = "test_default_slac_telemetry_serializes_with_zero_ev_mac";

    SlacTelemetry telemetry;
    auto const serialized = serialize(telemetry);
    auto const serialized_json = nlohmann::json::parse(serialized);
    if (!assert_true(serialized_json.at("ev_mac") == "00:00:00:00:00:00", test_name,
                     "Serialized ev_mac is not 00:00:00:00:00:00")) {
        return false;
    }

    auto const deserialized = deserialize(serialized);
    auto const zero = messages::HomeplugMessage::MacAddress{};
    return assert_true(deserialized.ev_mac == zero, test_name,
                      "Deserialized ev_mac is not zeroed") &&
           assert_true(deserialized.match_state == SlacState::Init, test_name,
                      "Default deserialized match_state changed from Init") &&
           assert_true(deserialized.d3_state == D3State::Unmatched, test_name,
                      "Default deserialized d3_state changed from Unmatched");
}

} // namespace

int main() {
    const auto tests = std::array<std::pair<const char*, bool (*)()>, 1>{
        std::make_pair("test_default_slac_telemetry_serializes_with_zero_ev_mac",
                       test_default_slac_telemetry_serializes_with_zero_ev_mac),
    };

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
