// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <ocpp/v2/request_origin_registry.hpp>

namespace ocpp::v2 {

using RequestKind = RequestOriginRegistry::RequestKind;

TEST(RequestOriginRegistryTest, NothingRememberedIsNotFound) {
    RequestOriginRegistry registry;
    EXPECT_FALSE(registry.lookup(RequestKind::Report, 1, false).has_value());
}

TEST(RequestOriginRegistryTest, RememberedOriginIsFoundAgain) {
    RequestOriginRegistry registry;
    registry.remember(RequestKind::Report, 42, ConnectionId::secondary("EMS"));

    const auto origin = registry.lookup(RequestKind::Report, 42, false);
    ASSERT_TRUE(origin.has_value());
    EXPECT_EQ(origin.value(), ConnectionId::secondary("EMS"));
}

TEST(RequestOriginRegistryTest, OriginSurvivesLookupsUntilTheLastOne) {
    RequestOriginRegistry registry;
    registry.remember(RequestKind::Report, 42, ConnectionId::secondary("EMS"));

    // A GetBaseReport answered by several NotifyReports: every part but the last leaves the entry
    // in place.
    EXPECT_TRUE(registry.lookup(RequestKind::Report, 42, false).has_value());
    EXPECT_TRUE(registry.lookup(RequestKind::Report, 42, false).has_value());
    EXPECT_TRUE(registry.lookup(RequestKind::Report, 42, true).has_value());
    EXPECT_FALSE(registry.lookup(RequestKind::Report, 42, false).has_value());
}

TEST(RequestOriginRegistryTest, TheSameIdOfADifferentKindIsADifferentRequest) {
    RequestOriginRegistry registry;
    registry.remember(RequestKind::Report, 7, ConnectionId::secondary("EMS"));
    registry.remember(RequestKind::Log, 7, ConnectionId::secondary("MAINT"));

    EXPECT_EQ(registry.lookup(RequestKind::Report, 7, false).value(), ConnectionId::secondary("EMS"));
    EXPECT_EQ(registry.lookup(RequestKind::Log, 7, false).value(), ConnectionId::secondary("MAINT"));

    // Finishing one leaves the other alone.
    EXPECT_TRUE(registry.lookup(RequestKind::Report, 7, true).has_value());
    EXPECT_TRUE(registry.lookup(RequestKind::Log, 7, false).has_value());
}

TEST(RequestOriginRegistryTest, TheNewerRequestWinsACollision) {
    // Application note section 5.3.1: two connections can pick the same requestId. The one that
    // asked most recently is the live request, and the earlier one loses its follow-ups.
    RequestOriginRegistry registry;
    registry.remember(RequestKind::Report, 5, ConnectionId::primary());
    registry.remember(RequestKind::Report, 5, ConnectionId::secondary("EMS"));

    EXPECT_EQ(registry.lookup(RequestKind::Report, 5, false).value(), ConnectionId::secondary("EMS"));
}

TEST(RequestOriginRegistryTest, ThePrimaryIsRememberedLikeAnyOtherConnection) {
    RequestOriginRegistry registry;
    registry.remember(RequestKind::FirmwareUpdate, 3, ConnectionId::primary());

    const auto origin = registry.lookup(RequestKind::FirmwareUpdate, 3, true);
    ASSERT_TRUE(origin.has_value());
    EXPECT_TRUE(origin->is_primary());
}

TEST(RequestOriginRegistryTest, ForgetDropsTheEntry) {
    RequestOriginRegistry registry;
    registry.remember(RequestKind::CustomerInformation, 9, ConnectionId::secondary("DATACOLLECTION"));
    registry.forget(RequestKind::CustomerInformation, 9);

    EXPECT_FALSE(registry.lookup(RequestKind::CustomerInformation, 9, false).has_value());

    // Forgetting something that was never there is not an error.
    EXPECT_NO_THROW(registry.forget(RequestKind::CustomerInformation, 9));
}

} // namespace ocpp::v2
