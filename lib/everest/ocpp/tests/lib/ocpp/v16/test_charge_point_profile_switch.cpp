// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

/// \file test_charge_point_profile_switch.cpp
/// \brief Registration behaviour of ChargePointImpl when the CSMS connection moves to a different network
/// connection slot without a reboot.

#include <atomic>
#include <deque>
#include <thread>

#include "charge_point_test_base.hpp"

namespace ocpp {
namespace v16 {

namespace {
constexpr int SLOT_1 = 1;
constexpr int SLOT_2 = 2;
constexpr auto SETTLE_TIME = std::chrono::milliseconds(1000);
} // namespace

class ChargePointProfileSwitchTest : public ChargePointTestBase {
protected:
    static ocpp::v2::NetworkConnectionProfile profile(const std::string& ocpp_csms_url = "ws://csms.example.com",
                                                      const int security_profile = 1) {
        ocpp::v2::NetworkConnectionProfile network_connection_profile;
        network_connection_profile.ocppCsmsUrl = ocpp_csms_url;
        network_connection_profile.securityProfile = security_profile;
        return network_connection_profile;
    }

    /// \brief The profile of \p slot , each slot pointing at a Central System of its own
    static ocpp::v2::NetworkConnectionProfile slot_profile(const int slot) {
        return profile("ws://csms-" + std::to_string(slot) + ".example.com");
    }

    /// \brief Answers the next BootNotification.req with \p status and \p interval instead of Accepted
    void answer_next_boot_with(const std::string& status, const int interval) {
        const std::lock_guard<std::mutex> lock(this->mtx);
        this->boot_answers.push_back(
            nlohmann::json{{"currentTime", ocpp::DateTime().to_rfc3339()}, {"interval", interval}, {"status", status}});
    }

    /// \brief Answers the next BootNotification.req with a status the charge point cannot read
    void answer_next_boot_unreadably() {
        const std::lock_guard<std::mutex> lock(this->mtx);
        this->boot_answers.push_back(nlohmann::json{
            {"currentTime", ocpp::DateTime().to_rfc3339()}, {"interval", 1}, {"status", "NotARegistrationStatus"}});
    }

    /// \brief Leaves the next BootNotification.req unanswered, as if the connection were lost before the answer
    void leave_next_boot_unanswered() {
        const std::lock_guard<std::mutex> lock(this->mtx);
        this->boots_to_leave_unanswered += 1;
    }

    bool answers_locked(const std::string& action) override {
        if (action == "BootNotification" and this->boots_to_leave_unanswered > 0) {
            this->boots_to_leave_unanswered -= 1;
            return false;
        }
        return true;
    }

    nlohmann::json response_payload_for_locked(const std::string& action) override {
        if (action == "BootNotification" and not this->boot_answers.empty()) {
            auto payload = this->boot_answers.front();
            this->boot_answers.pop_front();
            return payload;
        }
        return ChargePointTestBase::response_payload_for_locked(action);
    }

    std::unique_ptr<ChargePointImpl> start_registered_on(const int slot) {
        auto charge_point = make_charge_point();
        charge_point->start({}, BootReasonEnum::PowerUp, {});
        charge_point->on_websocket_connected(slot, slot_profile(slot), ocpp::OcppProtocolVersion::v16);
        EXPECT_TRUE(wait_for_action_count("BootNotification", 1, WAIT_TIMEOUT));
        EXPECT_TRUE(wait_for_all_answered());
        return charge_point;
    }

    void stop_charge_point(ChargePointImpl& charge_point) {
        EXPECT_TRUE(wait_for_all_answered()) << "not every answer was processed before stopping";
        EXPECT_THAT(escaped_exceptions, ::testing::IsEmpty());
        charge_point.stop();
    }

    /// \brief Waits until every call is answered and no new call followed for a while
    void wait_until_settled() {
        std::size_t last_size = 0;
        int quiet_rounds = 0;
        const auto deadline = std::chrono::steady_clock::now() + WAIT_TIMEOUT;
        while (quiet_rounds < 3 and std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            const std::lock_guard<std::mutex> lock(this->mtx);
            const bool answered = this->delivered + this->unanswered >= this->sent.size();
            quiet_rounds = (answered and this->sent.size() == last_size) ? quiet_rounds + 1 : 0;
            last_size = this->sent.size();
        }
    }

    void switch_slot(ChargePointImpl& charge_point, const int from, const int to) {
        charge_point.on_websocket_disconnected(from, slot_profile(from));
        charge_point.on_websocket_connected(to, slot_profile(to), ocpp::OcppProtocolVersion::v16);
    }

    std::deque<nlohmann::json> boot_answers;
    int boots_to_leave_unanswered{0};
};

// The connectivity manager learns the last successful network connection slot, the B10.FR.07 fallback target, from
// record_successful_connection(). OCPP 1.6 does not use confirm_successful_connection(), whose security profile
// handling and persistence are OCPP 2.x behaviour.
TEST_F(ChargePointProfileSwitchTest, AcceptedBootNotificationRecordsSuccessfulConnection) {
    std::atomic<int> recorded{0};
    ON_CALL(*this->connectivity_manager, record_successful_connection()).WillByDefault(::testing::Invoke([&recorded]() {
        recorded += 1;
    }));
    EXPECT_CALL(*this->connectivity_manager, confirm_successful_connection()).Times(0);

    auto charge_point = start_registered_on(SLOT_1);

    EXPECT_GE(recorded.load(), 1)
        << "an Accepted BootNotification.conf did not record the connection with the connectivity manager";

    stop_charge_point(*charge_point);
}

// As in OCPP 2.0.1 and 2.1 (B10.FR.10), the CSMS behind a different network connection slot may not know the charge
// point, so by default it re-registers with a BootNotification.req although it did not reboot.
TEST_F(ChargePointProfileSwitchTest, SwitchToDifferentSlotSendsBootNotificationByDefault) {
    auto charge_point = start_registered_on(SLOT_1);

    charge_point->on_websocket_disconnected(SLOT_1, slot_profile(SLOT_1));
    charge_point->on_websocket_connected(SLOT_2, slot_profile(SLOT_2), ocpp::OcppProtocolVersion::v16);

    EXPECT_TRUE(wait_for_action_count("BootNotification", 2, WAIT_TIMEOUT))
        << "no BootNotification.req after switching from configuration slot " << SLOT_1 << " to " << SLOT_2;

    stop_charge_point(*charge_point);
}

// A message queued while the connection was down reaches the Central System behind the new slot only after the
// BootNotification.req.
TEST_F(ChargePointProfileSwitchTest, SwitchToDifferentSlotSendsBootNotificationFirst) {
    auto charge_point = start_registered_on(SLOT_1);

    charge_point->on_websocket_disconnected(SLOT_1, slot_profile(SLOT_1));
    const auto calls_before_switch = [this]() {
        const std::lock_guard<std::mutex> lock(this->mtx);
        return this->sent.size();
    }();
    const auto security_events_before_switch = count_action("SecurityEventNotification");
    charge_point->on_security_event(CiString<50>(ocpp::security_events::SETTINGSYSTEMTIME), std::nullopt, true);
    charge_point->on_websocket_connected(SLOT_2, slot_profile(SLOT_2), ocpp::OcppProtocolVersion::v16);

    EXPECT_TRUE(wait_for_action_count("SecurityEventNotification", security_events_before_switch + 1, WAIT_TIMEOUT))
        << "the queued SecurityEventNotification.req was not sent after the switch";
    {
        const std::lock_guard<std::mutex> lock(this->mtx);
        if (this->sent.size() > calls_before_switch) {
            EXPECT_EQ(this->sent.at(calls_before_switch).at(2), "BootNotification")
                << this->sent.at(calls_before_switch).at(2)
                << " was sent to the Central System behind the new slot before the BootNotification.req";
        } else {
            ADD_FAILURE() << "nothing was sent after switching configuration slot";
        }
    }

    stop_charge_point(*charge_point);
}

// BootNotificationAfterNetworkProfileSwitch turns re-registration off, as OCPP-J 1.6 section 5.4 advises.
TEST_F(ChargePointProfileSwitchTest, SwitchToDifferentSlotWhenDisabledSendsNoBootNotification) {
    ASSERT_EQ(this->configuration->set("BootNotificationAfterNetworkProfileSwitch", "false"),
              ConfigurationStatus::Accepted);
    auto charge_point = start_registered_on(SLOT_1);

    charge_point->on_websocket_disconnected(SLOT_1, slot_profile(SLOT_1));
    charge_point->on_websocket_connected(SLOT_2, slot_profile(SLOT_2), ocpp::OcppProtocolVersion::v16);
    std::this_thread::sleep_for(SETTLE_TIME);

    EXPECT_EQ(count_action("BootNotification"), 1);

    stop_charge_point(*charge_point);
}

// OCPP-J 1.6 section 5.4: reconnecting through the same slot does not repeat the BootNotification.req.
TEST_F(ChargePointProfileSwitchTest, ReconnectOnSameSlotDoesNotSendBootNotification) {
    auto charge_point = start_registered_on(SLOT_1);

    charge_point->on_websocket_disconnected(SLOT_1, slot_profile(SLOT_1));
    charge_point->on_websocket_connected(SLOT_1, slot_profile(SLOT_1), ocpp::OcppProtocolVersion::v16);
    std::this_thread::sleep_for(SETTLE_TIME);

    EXPECT_EQ(count_action("BootNotification"), 1);

    stop_charge_point(*charge_point);
}

// SetNetworkProfile or a configuration change can point the registered slot itself at another Central System, which
// does not know the charge point either.
TEST_F(ChargePointProfileSwitchTest, SameSlotWithNewCsmsUrlSendsBootNotification) {
    auto charge_point = start_registered_on(SLOT_1);

    charge_point->on_websocket_disconnected(SLOT_1, slot_profile(SLOT_1));
    charge_point->on_websocket_connected(SLOT_1, profile("ws://other-csms.example.com"),
                                         ocpp::OcppProtocolVersion::v16);

    EXPECT_TRUE(wait_for_action_count("BootNotification", 2, WAIT_TIMEOUT))
        << "no BootNotification.req after connecting to another Central System through the same configuration slot";

    stop_charge_point(*charge_point);
}

// The Central System behind the new slot answered Pending, so falling back to the slot whose Central System accepted
// the charge point before registers again at once instead of waiting for the retry interval.
TEST_F(ChargePointProfileSwitchTest, FallbackAfterPendingSendsBootNotificationAtOnce) {
    auto charge_point = start_registered_on(SLOT_1);

    answer_next_boot_with("Pending", 3600);
    charge_point->on_websocket_disconnected(SLOT_1, slot_profile(SLOT_1));
    charge_point->on_websocket_connected(SLOT_2, profile("ws://other-csms.example.com"),
                                         ocpp::OcppProtocolVersion::v16);
    ASSERT_TRUE(wait_for_action_count("BootNotification", 2, WAIT_TIMEOUT))
        << "no BootNotification.req after switching configuration slot";
    ASSERT_TRUE(wait_for_all_answered());

    charge_point->on_websocket_disconnected(SLOT_2, profile("ws://other-csms.example.com"));
    charge_point->on_websocket_connected(SLOT_1, slot_profile(SLOT_1), ocpp::OcppProtocolVersion::v16);

    EXPECT_TRUE(wait_for_action_count("BootNotification", 3, WAIT_TIMEOUT))
        << "no BootNotification.req after falling back to configuration slot " << SLOT_1;

    stop_charge_point(*charge_point);
}

// The connection is lost before the Central System behind the new slot answers the BootNotification.req. After falling
// back to the slot whose Central System accepted the charge point, the BootNotification.req goes out at once and its
// acceptance releases the queued messages.
TEST_F(ChargePointProfileSwitchTest, UnansweredBootNotificationIsResentAfterFallback) {
    auto charge_point = start_registered_on(SLOT_1);

    leave_next_boot_unanswered();
    charge_point->on_websocket_disconnected(SLOT_1, slot_profile(SLOT_1));
    const auto security_events_before_switch = count_action("SecurityEventNotification");
    charge_point->on_security_event(CiString<50>(ocpp::security_events::SETTINGSYSTEMTIME), std::nullopt, true);
    charge_point->on_websocket_connected(SLOT_2, slot_profile(SLOT_2), ocpp::OcppProtocolVersion::v16);
    ASSERT_TRUE(wait_for_action_count("BootNotification", 2, WAIT_TIMEOUT))
        << "no BootNotification.req after switching configuration slot";

    switch_slot(*charge_point, SLOT_2, SLOT_1);
    EXPECT_TRUE(wait_for_action_count("BootNotification", 3, WAIT_TIMEOUT))
        << "the unanswered BootNotification.req was not resent after falling back";
    EXPECT_TRUE(wait_for_action_count("SecurityEventNotification", security_events_before_switch + 1, WAIT_TIMEOUT))
        << "the queued message was not released once the registration was accepted";

    stop_charge_point(*charge_point);
}

// The connection is lost before the Central System behind the new slot answers, and the charge point connects through
// a third slot. Its Central System receives a single BootNotification.req.
TEST_F(ChargePointProfileSwitchTest, SwitchWithUnansweredBootNotificationSendsSingleBootNotification) {
    constexpr int SLOT_3 = 3;
    auto charge_point = start_registered_on(SLOT_1);

    leave_next_boot_unanswered();
    switch_slot(*charge_point, SLOT_1, SLOT_2);
    ASSERT_TRUE(wait_for_action_count("BootNotification", 2, WAIT_TIMEOUT))
        << "no BootNotification.req after switching configuration slot";

    switch_slot(*charge_point, SLOT_2, SLOT_3);
    EXPECT_TRUE(wait_for_action_count("BootNotification", 3, WAIT_TIMEOUT))
        << "no BootNotification.req after switching to configuration slot " << SLOT_3;
    ASSERT_TRUE(wait_for_all_answered());
    std::this_thread::sleep_for(SETTLE_TIME);
    EXPECT_EQ(count_action("BootNotification"), 3)
        << "the Central System behind configuration slot " << SLOT_3 << " received more than one BootNotification.req";

    stop_charge_point(*charge_point);
}

// As after Pending, a Central System that answered Rejected does not know the charge point, so falling back to the
// slot whose Central System accepted it registers again at once.
TEST_F(ChargePointProfileSwitchTest, FallbackAfterRejectedSendsBootNotificationAtOnce) {
    auto charge_point = start_registered_on(SLOT_1);

    answer_next_boot_with("Rejected", 3600);
    switch_slot(*charge_point, SLOT_1, SLOT_2);
    ASSERT_TRUE(wait_for_action_count("BootNotification", 2, WAIT_TIMEOUT))
        << "no BootNotification.req after switching configuration slot";
    ASSERT_TRUE(wait_for_all_answered());

    switch_slot(*charge_point, SLOT_2, SLOT_1);
    EXPECT_TRUE(wait_for_action_count("BootNotification", 3, WAIT_TIMEOUT))
        << "no BootNotification.req after falling back to configuration slot " << SLOT_1;

    stop_charge_point(*charge_point);
}

// The charge point is Pending at start-up. A switch to another slot registers there at once by default, and not at all
// with BootNotificationAfterNetworkProfileSwitch turned off, which leaves the retry to the Pending interval.
TEST_F(ChargePointProfileSwitchTest, SwitchWhilePendingSendsBootNotificationByDefault) {
    answer_next_boot_with("Pending", 3600);
    auto charge_point = start_registered_on(SLOT_1);

    switch_slot(*charge_point, SLOT_1, SLOT_2);

    EXPECT_TRUE(wait_for_action_count("BootNotification", 2, WAIT_TIMEOUT))
        << "no BootNotification.req after switching configuration slot while Pending";

    stop_charge_point(*charge_point);
}

TEST_F(ChargePointProfileSwitchTest, SwitchWhilePendingWhenDisabledSendsNoBootNotification) {
    ASSERT_EQ(this->configuration->set("BootNotificationAfterNetworkProfileSwitch", "false"),
              ConfigurationStatus::Accepted);
    answer_next_boot_with("Pending", 3600);
    auto charge_point = start_registered_on(SLOT_1);

    switch_slot(*charge_point, SLOT_1, SLOT_2);
    std::this_thread::sleep_for(SETTLE_TIME);

    EXPECT_EQ(count_action("BootNotification"), 1);

    stop_charge_point(*charge_point);
}

// A reconnect through the same slot while Booted records the connection with the connectivity manager, so the
// B10.FR.07 fallback target follows plain reconnects too.
TEST_F(ChargePointProfileSwitchTest, ReconnectOnSameSlotRecordsSuccessfulConnection) {
    std::atomic<int> recorded{0};
    ON_CALL(*this->connectivity_manager, record_successful_connection()).WillByDefault(::testing::Invoke([&recorded]() {
        recorded += 1;
    }));
    auto charge_point = start_registered_on(SLOT_1);
    const auto recorded_after_boot = recorded.load();

    switch_slot(*charge_point, SLOT_1, SLOT_1);
    ASSERT_TRUE(wait_for_all_answered());

    EXPECT_GT(recorded.load(), recorded_after_boot)
        << "a reconnect while Booted did not record the connection with the connectivity manager";

    stop_charge_point(*charge_point);
}

// With BootNotificationAfterNetworkProfileSwitch turned off, the charge point treats the Central System behind the new
// slot as the one it is registered with. Turning the key on later does not register again with it.
TEST_F(ChargePointProfileSwitchTest, SwitchWhenDisabledAdoptsNewSlot) {
    ASSERT_EQ(this->configuration->set("BootNotificationAfterNetworkProfileSwitch", "false"),
              ConfigurationStatus::Accepted);
    auto charge_point = start_registered_on(SLOT_1);
    switch_slot(*charge_point, SLOT_1, SLOT_2);
    ASSERT_TRUE(wait_for_all_answered());

    this->configuration->setBootNotificationAfterNetworkProfileSwitch(true);
    switch_slot(*charge_point, SLOT_2, SLOT_2);
    std::this_thread::sleep_for(SETTLE_TIME);

    EXPECT_EQ(count_action("BootNotification"), 1);

    stop_charge_point(*charge_point);
}

// The BootNotification.req sent at once after falling back replaces the retry scheduled by the Pending answer of the
// other Central System, which therefore sends nothing when its interval expires.
TEST_F(ChargePointProfileSwitchTest, FallbackAfterPendingCancelsScheduledRetry) {
    auto charge_point = start_registered_on(SLOT_1);

    answer_next_boot_with("Pending", 1);
    switch_slot(*charge_point, SLOT_1, SLOT_2);
    ASSERT_TRUE(wait_for_action_count("BootNotification", 2, WAIT_TIMEOUT))
        << "no BootNotification.req after switching configuration slot";
    ASSERT_TRUE(wait_for_all_answered());

    switch_slot(*charge_point, SLOT_2, SLOT_1);
    ASSERT_TRUE(wait_for_action_count("BootNotification", 3, WAIT_TIMEOUT))
        << "no BootNotification.req after falling back to configuration slot " << SLOT_1;
    ASSERT_TRUE(wait_for_all_answered());

    std::this_thread::sleep_for(std::chrono::milliseconds(2500));
    EXPECT_EQ(count_action("BootNotification"), 3)
        << "the retry scheduled by the Pending answer still sent a BootNotification.req";

    stop_charge_point(*charge_point);
}

// Reconnecting through the same slot while Pending or Rejected keeps the retry interval the Central System asked for:
// no BootNotification.req goes out at once, and the scheduled retry still does.
TEST_F(ChargePointProfileSwitchTest, ReconnectOnSameSlotWhilePendingKeepsRetryInterval) {
    answer_next_boot_with("Pending", 2);
    auto charge_point = start_registered_on(SLOT_1);

    switch_slot(*charge_point, SLOT_1, SLOT_1);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EXPECT_EQ(count_action("BootNotification"), 1) << "a reconnect while Pending sent a BootNotification.req at once";

    EXPECT_TRUE(wait_for_action_count("BootNotification", 2, WAIT_TIMEOUT))
        << "the BootNotification.req scheduled by the Pending answer was not sent";

    stop_charge_point(*charge_point);
}

TEST_F(ChargePointProfileSwitchTest, ReconnectOnSameSlotWhileRejectedKeepsRetryInterval) {
    answer_next_boot_with("Rejected", 2);
    auto charge_point = start_registered_on(SLOT_1);

    switch_slot(*charge_point, SLOT_1, SLOT_1);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EXPECT_EQ(count_action("BootNotification"), 1) << "a reconnect while Rejected sent a BootNotification.req at once";

    EXPECT_TRUE(wait_for_action_count("BootNotification", 2, WAIT_TIMEOUT))
        << "the BootNotification.req scheduled by the Rejected answer was not sent";

    stop_charge_point(*charge_point);
}

// Once the Central System behind the new slot accepts the charge point, it learns the status of every connector.
TEST_F(ChargePointProfileSwitchTest, AcceptedReRegistrationSendsStatusNotifications) {
    auto charge_point = start_registered_on(SLOT_1);
    wait_until_settled();
    const auto status_notifications_before_switch = count_action("StatusNotification");
    ASSERT_GT(status_notifications_before_switch, 0) << "the accepted start-up registration sent no StatusNotification";

    switch_slot(*charge_point, SLOT_1, SLOT_2);
    ASSERT_TRUE(wait_for_action_count("BootNotification", 2, WAIT_TIMEOUT))
        << "no BootNotification.req after switching configuration slot";

    EXPECT_TRUE(wait_for_action_count("StatusNotification", 2 * status_notifications_before_switch, WAIT_TIMEOUT))
        << "the Central System behind the new slot did not receive the status of every connector";

    stop_charge_point(*charge_point);
}

// OCPP-J 1.6 section 5.4 advises against a BootNotification.req on reconnect, so the charge point only registers again
// when the new slot points at another Central System. A second slot towards the same Central System, such as another
// network interface, is a plain reconnect.
TEST_F(ChargePointProfileSwitchTest, SwitchToDifferentSlotWithSameCsmsUrlSendsNoBootNotification) {
    auto charge_point = start_registered_on(SLOT_1);
    wait_until_settled();
    const auto status_notifications_before_switch = count_action("StatusNotification");

    charge_point->on_websocket_disconnected(SLOT_1, slot_profile(SLOT_1));
    charge_point->on_websocket_connected(SLOT_2, slot_profile(SLOT_1), ocpp::OcppProtocolVersion::v16);
    std::this_thread::sleep_for(SETTLE_TIME);

    EXPECT_EQ(count_action("BootNotification"), 1);
    EXPECT_GT(count_action("StatusNotification"), status_notifications_before_switch)
        << "the reconnect did not report the connector status";

    stop_charge_point(*charge_point);
}

// The same Central System can be written in more than one way, such as Internal.CentralSystemURI without a scheme and
// a network connection profile with one.
TEST_F(ChargePointProfileSwitchTest, SwitchToSlotWithEquivalentCsmsUrlSendsNoBootNotification) {
    auto charge_point = start_registered_on(SLOT_1);

    charge_point->on_websocket_disconnected(SLOT_1, slot_profile(SLOT_1));
    charge_point->on_websocket_connected(SLOT_2,
                                         profile("CSMS-1.example.com:80/" + this->configuration->getChargePointId()),
                                         ocpp::OcppProtocolVersion::v16);
    std::this_thread::sleep_for(SETTLE_TIME);

    EXPECT_EQ(count_action("BootNotification"), 1);

    stop_charge_point(*charge_point);
}

// Internal.CentralSystemURI usually has no scheme, so the security profile picks it.
// ChangeConfiguration(SecurityProfile) changes how the charge point connects, not which Central System it connects to.
TEST_F(ChargePointProfileSwitchTest, SecurityProfileChangeOnUrlWithoutSchemeSendsNoBootNotification) {
    const auto central_system_uri = "csms-1.example.com/" + this->configuration->getChargePointId();
    auto charge_point = make_charge_point();
    charge_point->start({}, BootReasonEnum::PowerUp, {});
    charge_point->on_websocket_connected(SLOT_1, profile(central_system_uri, 1), ocpp::OcppProtocolVersion::v16);
    ASSERT_TRUE(wait_for_action_count("BootNotification", 1, WAIT_TIMEOUT));
    ASSERT_TRUE(wait_for_all_answered());

    charge_point->on_websocket_disconnected(SLOT_1, profile(central_system_uri, 1));
    charge_point->on_websocket_connected(SLOT_1, profile(central_system_uri, 2), ocpp::OcppProtocolVersion::v16);
    std::this_thread::sleep_for(SETTLE_TIME);

    EXPECT_EQ(count_action("BootNotification"), 1);

    stop_charge_point(*charge_point);
}

// The message queue does not send a BootNotification.req again once the Central System answered it. When the charge
// point cannot read the answer it is still not registered, so it asks again after the retry interval.
TEST_F(ChargePointProfileSwitchTest, UnreadableBootNotificationResponseIsRetried) {
    answer_next_boot_with("Pending", 1);
    answer_next_boot_unreadably();
    auto charge_point = start_registered_on(SLOT_1);

    EXPECT_TRUE(wait_for_action_count("BootNotification", 3, WAIT_TIMEOUT))
        << "no BootNotification.req after a BootNotification.conf that could not be read";

    stop_charge_point(*charge_point);
}

// With IgnoredProfilePurposesOffline the composite schedule depends on whether the charge point is registered. It is
// not while it registers again, so the schedule is signalled once more when the BootNotification.req is accepted.
TEST_F(ChargePointProfileSwitchTest, AcceptedReRegistrationSignalsChargingProfiles) {
    // the key is optional and absent from the test configuration
    std::ifstream ifs(CONFIG_FILE_LOCATION_V16);
    auto config = nlohmann::json::parse(ifs);
    config["Internal"]["IgnoredProfilePurposesOffline"] = "TxProfile";
    this->configuration =
        std::make_unique<ChargePointConfiguration>(config.dump(), CONFIG_DIR_V16, this->tmp_dir / "user_config.json");
    ASSERT_FALSE(this->configuration->getIgnoredProfilePurposesOffline().empty());
    auto charge_point = start_registered_on(SLOT_1);
    std::atomic<int> signals{0};
    charge_point->register_signal_set_charging_profiles_callback([&signals]() { signals += 1; });

    // Pending first, so that nothing but the accepted retry can signal after the connection is up
    answer_next_boot_with("Pending", 2);
    switch_slot(*charge_point, SLOT_1, SLOT_2);
    ASSERT_TRUE(wait_for_action_count("BootNotification", 2, WAIT_TIMEOUT))
        << "no BootNotification.req after switching configuration slot";
    ASSERT_TRUE(wait_for_all_answered());
    const auto signals_before_accepted = signals.load();

    ASSERT_TRUE(wait_for_action_count("BootNotification", 3, WAIT_TIMEOUT))
        << "the BootNotification.req was not retried after Pending";
    wait_until_settled();

    EXPECT_GT(signals.load(), signals_before_accepted)
        << "the composite schedule was not signalled after the registration was accepted";

    stop_charge_point(*charge_point);
}

} // namespace v16
} // namespace ocpp
