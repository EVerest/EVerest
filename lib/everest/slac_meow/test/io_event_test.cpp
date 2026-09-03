// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 - 2026 Pionix GmbH and Contributors to EVerest
//
// Carried over from the slac_neo misc test, which covers the transport this library now owns a
// copy of in io/.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <utility>

#include <everest/slac/io/event.hpp>

using namespace everest::slac;

namespace {

bool assert_true(bool cond, const char* test_name, const char* details) {
    if (not cond) {
        std::printf("[%s] FAIL: %s\n", test_name, details);
        return false;
    }
    return true;
}

bool test_slac_event_mac_is_zero_without_readable_interface() {
    // The interface MAC lookup in SlacEvent's constructor is best effort - the PLC device may
    // enumerate after module start - and its failure is swallowed. The MAC member used to be
    // default-initialized, so get_mac_addr() handed indeterminate bytes to the state machine.
    // Contract now: without a readable interface the MAC is deterministically all-zero, and the
    // module re-captures it from the I/O ready callback once the device is up.
    const char* test_name = "test_slac_event_mac_is_zero_without_readable_interface";

    io::SlacEvent event("noifc0");
    auto const* mac = event.get_mac_addr();
    const bool all_zero = std::all_of(mac, mac + 6, [](std::uint8_t byte) { return byte == 0; });
    return assert_true(all_zero, test_name, "MAC for a missing interface is not all-zero");
}

} // namespace

int main() {
    const auto tests = std::array<std::pair<const char*, bool (*)()>, 1>{
        std::make_pair("test_slac_event_mac_is_zero_without_readable_interface",
                       test_slac_event_mac_is_zero_without_readable_interface),
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
