// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <chrono>
#include <future>

#include <generated/types/evse_board_support.hpp>

#include "bsp_capabilities_store.hpp"

using types::evse_board_support::HardwareCapabilities;
using namespace std::chrono_literals;

namespace {

HardwareCapabilities make_caps(int max_phase_count_import) {
    HardwareCapabilities caps{};
    caps.max_phase_count_import = max_phase_count_import;
    return caps;
}

} // namespace

TEST(BspCapabilitiesStoreTest, get_returns_defaults_before_apply) {
    module::BspCapabilitiesStore hw_caps(make_caps(1));

    EXPECT_EQ(hw_caps.get(), make_caps(1));
}

TEST(BspCapabilitiesStoreTest, apply_when_allowed_blocks_until_allow_updates) {
    module::BspCapabilitiesStore hw_caps(make_caps(1));

    auto applied = std::async(std::launch::async, [&hw_caps]() { hw_caps.apply_when_allowed(make_caps(3)); });

    EXPECT_EQ(applied.wait_for(200ms), std::future_status::timeout);
    EXPECT_EQ(hw_caps.get(), make_caps(1));

    hw_caps.allow_updates();

    ASSERT_EQ(applied.wait_for(5s), std::future_status::ready);
    EXPECT_EQ(hw_caps.get(), make_caps(3));
}

TEST(BspCapabilitiesStoreTest, apply_after_allow_updates_stores_without_blocking) {
    module::BspCapabilitiesStore hw_caps(make_caps(1));
    hw_caps.allow_updates();

    auto applied = std::async(std::launch::async, [&hw_caps]() { hw_caps.apply_when_allowed(make_caps(3)); });

    ASSERT_EQ(applied.wait_for(5s), std::future_status::ready);
    EXPECT_EQ(hw_caps.get(), make_caps(3));
}

TEST(BspCapabilitiesStoreTest, allow_updates_racing_apply_never_loses_the_wakeup) {
    constexpr int iterations = 2000;

    for (int i = 0; i < iterations; ++i) {
        module::BspCapabilitiesStore hw_caps(make_caps(1));
        std::promise<void> start;
        auto started = start.get_future().share();

        auto applied = std::async(std::launch::async, [&hw_caps, started]() {
            started.wait();
            hw_caps.apply_when_allowed(make_caps(3));
        });
        auto allowed = std::async(std::launch::async, [&hw_caps, started]() {
            started.wait();
            hw_caps.allow_updates();
        });
        start.set_value();

        ASSERT_EQ(allowed.wait_for(5s), std::future_status::ready) << "iteration " << i;
        if (applied.wait_for(5s) != std::future_status::ready) {
            // Unblock the waiter so the test can report instead of hanging.
            hw_caps.allow_updates();
            FAIL() << "apply_when_allowed() missed the wakeup from allow_updates() in iteration " << i;
        }
        EXPECT_EQ(hw_caps.get(), make_caps(3)) << "iteration " << i;
    }
}

TEST(BspCapabilitiesStoreTest, modify_changes_what_get_returns) {
    module::BspCapabilitiesStore hw_caps(make_caps(1));

    const auto result = hw_caps.modify([](HardwareCapabilities& caps) {
        caps.max_phase_count_import = 3;
        return caps.max_phase_count_import;
    });

    EXPECT_EQ(result, 3);
    EXPECT_EQ(hw_caps.get(), make_caps(3));
}
