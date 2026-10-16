// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <ocpp/v2/monitor_subscription.hpp>

namespace ocpp::v2 {

namespace {
EventData event_with(const EventNotificationEnum notification, const std::optional<std::int32_t> monitor_id) {
    EventData event;
    event.eventNotificationType = notification;
    event.variableMonitoringId = monitor_id;
    return event;
}
} // namespace

TEST(MonitorSubscriptionTest, AnEmptyListSubscribesToNothing) {
    EXPECT_TRUE(parse_monitor_id_list("").empty());
    EXPECT_TRUE(parse_monitor_id_list("   ").empty());
    EXPECT_TRUE(parse_monitor_id_list(",,").empty());
}

TEST(MonitorSubscriptionTest, IdsAreReadWhateverTheSpacingIs) {
    const std::set<std::int32_t> expected{2, 10, 11, 12, 13};

    // The example from application note section 5.5, written the way the note writes it.
    EXPECT_EQ(parse_monitor_id_list("2, 11, 12, 13, 10"), expected);
    EXPECT_EQ(parse_monitor_id_list("2,11,12,13,10"), expected);
    EXPECT_EQ(parse_monitor_id_list("  2 ,11,  12,13 , 10  "), expected);
}

TEST(MonitorSubscriptionTest, ARepeatedIdIsStillOneSubscription) {
    EXPECT_EQ(parse_monitor_id_list("7,7,7"), std::set<std::int32_t>{7});
}

TEST(MonitorSubscriptionTest, AnEntryThatIsNotAnIdIsSkippedRatherThanVoidingTheList) {
    // A list a CSMS wrote by hand must not lose everything else to one bad entry.
    const std::set<std::int32_t> expected{1, 3};
    EXPECT_EQ(parse_monitor_id_list("1, nonsense, 3"), expected);
    EXPECT_EQ(parse_monitor_id_list("1, 3x, 3"), expected);
    EXPECT_EQ(parse_monitor_id_list("1,,3"), expected);
}

TEST(MonitorSubscriptionTest, AListRoundTrips) {
    const std::set<std::int32_t> ids{13, 2, 11};
    EXPECT_EQ(to_monitor_id_list(ids), "2,11,13");
    EXPECT_EQ(parse_monitor_id_list(to_monitor_id_list(ids)), ids);
    EXPECT_EQ(to_monitor_id_list({}), "");
}

TEST(MonitorSubscriptionTest, HardwiredNotificationsBelongToEveryone) {
    EXPECT_TRUE(is_hardwired_notification(event_with(EventNotificationEnum::HardWiredNotification, std::nullopt)));
    EXPECT_TRUE(is_hardwired_notification(event_with(EventNotificationEnum::HardWiredMonitor, 1)));

    EXPECT_FALSE(is_hardwired_notification(event_with(EventNotificationEnum::PreconfiguredMonitor, 2)));
    EXPECT_FALSE(is_hardwired_notification(event_with(EventNotificationEnum::CustomMonitor, 10)));
}

} // namespace ocpp::v2
