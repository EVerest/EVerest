// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
//
// Argument checks and failure reporting of the vcan shaper; installing needs CAP_NET_ADMIN.

#include <everest/io/netlink/vcan_netlink_manager.hpp>

#include <string>
#include <vector>

#include <gtest/gtest.h>

using everest::lib::io::netlink::vcan_netlink_manager;

namespace {

// Cannot exist, so a request past the argument checks fails on the name lookup.
constexpr char const* no_such_interface = "everest-io-no-such-if";

class vcan_netlink_manager_test : public ::testing::Test {
protected:
    void SetUp() override {
        vcan_netlink_manager::Instance().set_error_handler(
            [this](std::string const& message) { reports.push_back(message); });
    }
    void TearDown() override {
        vcan_netlink_manager::Instance().set_error_handler({});
    }

    bool last_report_mentions(std::string const& text) const {
        return not reports.empty() and reports.back().find(text) != std::string::npos;
    }

    std::vector<std::string> reports;
};

} // namespace

TEST_F(vcan_netlink_manager_test, a_burst_below_the_can_fd_mtu_is_rejected_before_anything_is_touched) {
    auto& manager = vcan_netlink_manager::Instance();
    EXPECT_FALSE(manager.set_transmit_rate_limit(no_such_interface, 1'000'000, 71, 10'000));
    ASSERT_EQ(reports.size(), 1U);
    EXPECT_TRUE(last_report_mentions("burst must be at least the CAN FD MTU of 72 bytes")) << reports.back();

    // 72 passes the check and reaches the lookup.
    EXPECT_FALSE(manager.set_transmit_rate_limit(no_such_interface, 1'000'000, 72, 10'000));
    ASSERT_EQ(reports.size(), 2U);
    EXPECT_TRUE(last_report_mentions("if_nametoindex failed")) << reports.back();
}

TEST_F(vcan_netlink_manager_test, zero_rate_burst_or_limit_is_rejected) {
    auto& manager = vcan_netlink_manager::Instance();
    EXPECT_FALSE(manager.set_transmit_rate_limit(no_such_interface, 0, 72, 10'000));
    EXPECT_FALSE(manager.set_transmit_rate_limit(no_such_interface, 1'000'000, 0, 10'000));
    EXPECT_FALSE(manager.set_transmit_rate_limit(no_such_interface, 1'000'000, 72, 0));
    ASSERT_EQ(reports.size(), 3U);
    for (auto const& report : reports) {
        EXPECT_NE(report.find("must be non-zero"), std::string::npos) << report;
    }
}

TEST_F(vcan_netlink_manager_test, a_burst_near_the_argument_limit_is_accepted_by_the_checks) {
    // The maximum burst passes the checks; the bucket arithmetic must not overflow for it.
    auto& manager = vcan_netlink_manager::Instance();
    EXPECT_FALSE(manager.set_transmit_rate_limit(no_such_interface, 1'000'000, 0xFFFFFFFFU, 10'000));
    ASSERT_EQ(reports.size(), 1U);
    EXPECT_TRUE(last_report_mentions("if_nametoindex failed")) << reports.back();
}

TEST_F(vcan_netlink_manager_test, clearing_an_unknown_interface_reports_the_lookup_failure) {
    auto& manager = vcan_netlink_manager::Instance();
    EXPECT_FALSE(manager.clear_transmit_rate_limit(no_such_interface));
    ASSERT_EQ(reports.size(), 1U);
    EXPECT_TRUE(last_report_mentions("'clearTransmitRateLimit'")) << reports.back();
    EXPECT_TRUE(last_report_mentions("if_nametoindex failed")) << reports.back();
}
