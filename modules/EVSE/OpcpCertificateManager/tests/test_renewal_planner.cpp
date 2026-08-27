// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <gtest/gtest.h>

#include "certificate_manager.hpp"

using namespace module;
using opcp::IsoVersion;

namespace {
LeafObservation obs(IsoVersion v, bool managed, int days, bool valid) {
    return LeafObservation{v, managed, days, valid};
}
} // namespace

TEST(RenewalPlanner, due_when_below_threshold_iso2_first) {
    const std::vector<LeafObservation> observations{obs(IsoVersion::ISO15118_20, true, 10, true),
                                                    obs(IsoVersion::ISO15118_2, true, 5, true)};
    const auto due = RenewalPlanner::leafs_due(observations, 30, false);
    ASSERT_EQ(due.size(), 2u);
    EXPECT_EQ(due[0], IsoVersion::ISO15118_2);
    EXPECT_EQ(due[1], IsoVersion::ISO15118_20);
}

TEST(RenewalPlanner, not_due_when_valid_long_enough_or_unmanaged) {
    const std::vector<LeafObservation> observations{obs(IsoVersion::ISO15118_2, true, 200, true),
                                                    obs(IsoVersion::ISO15118_20, false, 1, true)};
    EXPECT_TRUE(RenewalPlanner::leafs_due(observations, 30, false).empty());
    // exactly the threshold is not "fewer than"
    EXPECT_TRUE(RenewalPlanner::leafs_due({obs(IsoVersion::ISO15118_2, true, 30, true)}, 30, false).empty());
    EXPECT_EQ(RenewalPlanner::leafs_due({obs(IsoVersion::ISO15118_2, true, 29, true)}, 30, false).size(), 1u);
}

TEST(RenewalPlanner, not_installed_counts_as_due_and_force_renews_managed_only) {
    const std::vector<LeafObservation> observations{obs(IsoVersion::ISO15118_2, true, 0, false),
                                                    obs(IsoVersion::ISO15118_20, false, 0, false)};
    auto due = RenewalPlanner::leafs_due(observations, 30, false);
    ASSERT_EQ(due.size(), 1u);
    EXPECT_EQ(due[0], IsoVersion::ISO15118_2);

    due = RenewalPlanner::leafs_due(
        {obs(IsoVersion::ISO15118_2, true, 300, true), obs(IsoVersion::ISO15118_20, false, 300, true)}, 30, true);
    ASSERT_EQ(due.size(), 1u) << "force renews the managed leaf only";
}

TEST(RenewalPlanner, auth_leaf_prefers_own_then_other) {
    const std::vector<LeafObservation> both{obs(IsoVersion::ISO15118_2, true, 10, true),
                                            obs(IsoVersion::ISO15118_20, true, 10, true)};
    EXPECT_EQ(RenewalPlanner::auth_leaf_for(IsoVersion::ISO15118_2, both), IsoVersion::ISO15118_2);
    EXPECT_EQ(RenewalPlanner::auth_leaf_for(IsoVersion::ISO15118_20, both), IsoVersion::ISO15118_20);

    const std::vector<LeafObservation> only_iso2{obs(IsoVersion::ISO15118_2, true, 10, true),
                                                 obs(IsoVersion::ISO15118_20, true, 0, false)};
    EXPECT_EQ(RenewalPlanner::auth_leaf_for(IsoVersion::ISO15118_20, only_iso2), IsoVersion::ISO15118_2);

    const std::vector<LeafObservation> none{obs(IsoVersion::ISO15118_2, true, 0, false),
                                            obs(IsoVersion::ISO15118_20, true, 0, false)};
    EXPECT_FALSE(RenewalPlanner::auth_leaf_for(IsoVersion::ISO15118_2, none).has_value());
}

TEST(RenewalPlanner, retry_delay_doubles_and_caps) {
    using std::chrono::seconds;
    EXPECT_EQ(RenewalPlanner::retry_delay(seconds(600), 1), seconds(600));
    EXPECT_EQ(RenewalPlanner::retry_delay(seconds(600), 2), seconds(1200));
    EXPECT_EQ(RenewalPlanner::retry_delay(seconds(600), 4), seconds(4800));
    EXPECT_EQ(RenewalPlanner::retry_delay(seconds(600), 20), seconds(86400));
    EXPECT_EQ(RenewalPlanner::retry_delay(seconds(600), 0), seconds(600));
}

TEST(ManagerConfig, from_module_config) {
    std::string error;
    auto config = manager_config_from("hubject-eu-qa", "", "", "", "", "cpo", "", true, true, "DE*PNX*E1", "", "Org",
                                      "DE", false, 30, 43200, 60, 600, true, "v2g,mo", 86400, 30000, error);
    ASSERT_TRUE(config.has_value()) << error;
    ASSERT_EQ(config->leafs.size(), 2u);
    EXPECT_TRUE(config->leafs[0].managed);
    EXPECT_EQ(config->leafs[1].subject.common_name, "DE*PNX*E1") << "iso20 CN falls back to iso2";
    EXPECT_EQ(config->root_types.size(), 2u);
    EXPECT_EQ(config->environment.simplereenroll_url(IsoVersion::ISO15118_2),
              "https://eu.plugncharge-qa.hubject.com/.well-known/cpo/simplereenroll");

    config =
        manager_config_from("custom", "https://pki.local:8443", "", "/re/{ca}/{iso}", "", "cpo", "/ca.pem", true, false,
                            "CN", "", "Org", "DE", false, 30, 43200, 60, 600, false, "v2g", 86400, 30000, error);
    ASSERT_TRUE(config.has_value()) << error;
    EXPECT_EQ(config->environment.simplereenroll_url(IsoVersion::ISO15118_20),
              "https://pki.local:8443/re/cpo/ISO15118-20");
    EXPECT_EQ(config->http.server_ca_bundle, "/ca.pem");
    EXPECT_FALSE(config->sync_roots);

    EXPECT_FALSE(manager_config_from("custom", "", "", "", "", "cpo", "", true, false, "CN", "", "Org", "DE", false, 30,
                                     43200, 60, 600, true, "v2g", 86400, 30000, error)
                     .has_value())
        << "custom without api_base_url";
    EXPECT_FALSE(manager_config_from("hubject-eu-qa", "", "", "", "", "cpo", "", true, false, "", "", "Org", "DE",
                                     false, 30, 43200, 60, 600, true, "v2g", 86400, 30000, error)
                     .has_value())
        << "managed leaf without CN";
    EXPECT_FALSE(manager_config_from("hubject-eu-qa", "", "", "", "", "cpo", "", true, false, "CN", "", "Org", "DE",
                                     false, 30, 43200, 60, 600, true, "v2g,pe", 86400, 30000, error)
                     .has_value())
        << "unknown root type";
}
