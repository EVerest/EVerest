// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

/// \file test_charge_point_connectivity.cpp
/// \brief Behavioural unit tests for the v16 ChargePointImpl <-> ConnectivityManager wiring.
///
/// These tests construct a ChargePointImpl through its constructor, passing a mocked
/// ConnectivityManager, and assert observable interactions only:
///   * that an injected manager is NOT auto-wired for the websocket lifecycle at construction (only set_logging),
///     and that the message callback is registered later in start(),
///   * that the drive surface (start/stop/outgoing message/offline query) hits the manager,
///   * that the connection callbacks stay armed for the whole lifetime and are disarmed only at
///     destruction, and
///   * the security-profile switch + revert behaviour orchestrated via the manager and the
///     internal websocket revert timer.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <test_temp_paths.hpp>

#include <ocpp/common/connectivity_manager.hpp>
#include <ocpp/v16/charge_point_configuration.hpp>
#include <ocpp/v16/charge_point_impl.hpp>
#include <ocpp/v16/charge_point_state_machine.hpp>

#include "connectivity_manager_mock.hpp"
#include "evse_security_mock.hpp"

namespace fs = std::filesystem;

using ::testing::_;
using ::testing::AtLeast;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;

namespace ocpp {
namespace v16 {

class ChargePointConnectivityTestBase : public ::testing::Test {
protected:
    void SetUp() override {
        this->evse_security = std::make_shared<NiceMock<EvseSecurityMock>>();
        this->connectivity_manager = std::make_shared<NiceMock<ConnectivityManagerMock>>();

        std::ifstream ifs(CONFIG_FILE_LOCATION_V16);
        const std::string config_file((std::istreambuf_iterator<char>(ifs)), (std::istreambuf_iterator<char>()));
        this->configuration =
            std::make_unique<ChargePointConfiguration>(config_file, CONFIG_DIR_V16, USER_CONFIG_FILE_LOCATION_V16);

        // Each test gets its own temporary directory so the on-disk sqlite db and message logs do not collide.
        this->tmp_dir = libocpp_test::unique_temp_directory("ocpp_v16_connectivity_test");
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(this->tmp_dir, ec);
    }

    /// \brief Construct a ChargePointImpl wired to the mocked ConnectivityManager. EXPECT_CALLs that need to
    /// observe construction-time interactions must be set before calling this.
    std::unique_ptr<ChargePointImpl> make_charge_point() {
        return std::make_unique<ChargePointImpl>(
            *this->configuration, /*share_path=*/fs::path(CONFIG_DIR_V16), /*database_path=*/this->tmp_dir,
            /*sql_init_path=*/fs::path(MIGRATION_FILES_LOCATION_V16), /*message_log_path=*/this->tmp_dir,
            this->evse_security, this->connectivity_manager, /*security_configuration=*/std::nullopt,
            /*message_callback=*/nullptr);
    }

    /// \brief Construct a ChargePointImpl with a nullptr connectivity_manager, exercising the internal-manager build
    /// path: ChargePointImpl constructs a real ocpp::ConnectivityManager from the configuration, evse_security and
    /// share path, and wires it up itself.
    std::unique_ptr<ChargePointImpl> make_charge_point_internal_cm() {
        return std::make_unique<ChargePointImpl>(
            *this->configuration, /*share_path=*/fs::path(CONFIG_DIR_V16), /*database_path=*/this->tmp_dir,
            /*sql_init_path=*/fs::path(MIGRATION_FILES_LOCATION_V16), /*message_log_path=*/this->tmp_dir,
            this->evse_security, /*connectivity_manager=*/nullptr, /*security_configuration=*/std::nullopt,
            /*message_callback=*/nullptr);
    }

    /// \brief Waits (bounded) for a message handed to send_to_websocket that matches \p match, and returns it.
    nlohmann::json wait_for_sent(const std::function<bool(const nlohmann::json&)>& match) {
        std::unique_lock<std::mutex> lock(this->sent_mutex);
        const auto matching = [&]() { return std::find_if(this->sent.begin(), this->sent.end(), match); };
        const bool found =
            this->sent_cv.wait_for(lock, std::chrono::seconds(5), [&]() { return matching() != this->sent.end(); });
        EXPECT_TRUE(found) << "No matching message was handed to send_to_websocket within the timeout";
        return found ? *matching() : nlohmann::json::array();
    }

    /// \brief Starts \p charge_point on an open connection, answers its BootNotification.req with Accepted, and
    /// returns the next CALL it sends, which stays unanswered.
    nlohmann::json start_with_call_in_flight(ChargePointImpl& charge_point) {
        ON_CALL(*this->connectivity_manager, is_websocket_connected()).WillByDefault(Return(true));
        ON_CALL(*this->connectivity_manager, set_message_callback(_))
            .WillByDefault(Invoke([this](const std::function<void(const std::string&)>& callback) {
                this->csms_to_charge_point = callback;
            }));
        ON_CALL(*this->connectivity_manager, send_to_websocket(_)).WillByDefault(Invoke([this](const std::string& m) {
            std::lock_guard<std::mutex> lock(this->sent_mutex);
            this->sent.push_back(nlohmann::json::parse(m));
            this->sent_cv.notify_all();
            return true;
        }));

        charge_point.start({}, BootReasonEnum::PowerUp, {});
        charge_point.on_websocket_connected(0, ocpp::v2::NetworkConnectionProfile{}, ocpp::OcppProtocolVersion::v16);

        const auto boot_notification = wait_for_sent(
            [](const nlohmann::json& m) { return m.at(0) == MessageTypeId::CALL && m.at(2) == "BootNotification"; });
        const nlohmann::json accepted = {
            {"status", "Accepted"}, {"currentTime", DateTime().to_rfc3339()}, {"interval", 300}};
        this->csms_to_charge_point(nlohmann::json{3, boot_notification.at(1), accepted}.dump());
        return wait_for_sent([&](const nlohmann::json& m) {
            return m.at(0) == MessageTypeId::CALL && m.at(1) != boot_notification.at(1);
        });
    }

    std::shared_ptr<NiceMock<EvseSecurityMock>> evse_security;
    std::shared_ptr<NiceMock<ConnectivityManagerMock>> connectivity_manager;
    std::unique_ptr<ChargePointConfiguration> configuration;
    fs::path tmp_dir;
    std::function<void(const std::string&)> csms_to_charge_point;
    std::mutex sent_mutex;
    std::condition_variable sent_cv;
    std::vector<nlohmann::json> sent;
};

using ChargePointConnectivityTest = ChargePointConnectivityTestBase;

// An INJECTED ConnectivityManager is treated like the OCPP 2.x path: the charge point does NOT auto-wire the
// websocket lifecycle callbacks onto it at construction. Construction only sets the logging hook; the message
// callback is registered later, in start(). The external owner drives the lifecycle handlers directly.
TEST_F(ChargePointConnectivityTest, InjectedManagerNotAutoWiredForLifecycle) {
    // At construction the injected manager receives only set_logging -- no lifecycle or message callbacks.
    EXPECT_CALL(*this->connectivity_manager, set_logging(_)).Times(1);
    EXPECT_CALL(*this->connectivity_manager, set_websocket_connected_callback(_)).Times(0);
    EXPECT_CALL(*this->connectivity_manager, set_websocket_disconnected_callback(_)).Times(0);
    EXPECT_CALL(*this->connectivity_manager, set_websocket_connection_failed_callback(_)).Times(0);
    EXPECT_CALL(*this->connectivity_manager, set_message_callback(_)).Times(0);
    EXPECT_CALL(*this->connectivity_manager, set_configure_network_connection_profile_callback(_)).Times(0);

    auto charge_point = make_charge_point();

    // Separate construction-time expectations from start-time expectations.
    testing::Mock::VerifyAndClearExpectations(this->connectivity_manager.get());

    // The message callback is registered in start(), not at construction.
    EXPECT_CALL(*this->connectivity_manager, set_message_callback(_)).Times(1);
    ON_CALL(*this->connectivity_manager, is_websocket_connected()).WillByDefault(Return(false));

    charge_point->start({}, BootReasonEnum::PowerUp, {});
    charge_point->stop();
}

// Starting the charge point must drive connect() on the manager; stopping must drive disconnect().
TEST_F(ChargePointConnectivityTest, StartConnectsStopDisconnects) {
    ON_CALL(*this->connectivity_manager, is_websocket_connected()).WillByDefault(Return(false));
    EXPECT_CALL(*this->connectivity_manager, connect(_)).Times(AtLeast(1));
    EXPECT_CALL(*this->connectivity_manager, disconnect()).Times(AtLeast(1));

    auto charge_point = make_charge_point();
    charge_point->start({}, BootReasonEnum::PowerUp, {});
    charge_point->stop();
}

// stop() is the external "stop OCPP communication" control, not destruction: the charge point stays alive
// and restartable, and its owner still gets the disconnect notification. Disarming here raced the deferred
// delivery of that notification and swallowed it.
TEST_F(ChargePointConnectivityTest, StopKeepsConnectionCallbacksArmed) {
    ON_CALL(*this->connectivity_manager, is_websocket_connected()).WillByDefault(Return(false));

    EXPECT_CALL(*this->connectivity_manager, disconnect()).Times(AtLeast(1));
    EXPECT_CALL(*this->connectivity_manager, disarm_connection_callbacks()).Times(0);

    auto charge_point = make_charge_point();
    charge_point->start({}, BootReasonEnum::PowerUp, {});
    charge_point->stop();

    // Verify while still alive: destruction is the only disarm site.
    testing::Mock::VerifyAndClearExpectations(this->connectivity_manager.get());
}

// Neither half the owner waits for (offline after stop(), online after restart()) survives a disarm.
TEST_F(ChargePointConnectivityTest, StopRestartCycleNeverDisarms) {
    ON_CALL(*this->connectivity_manager, is_websocket_connected()).WillByDefault(Return(false));

    EXPECT_CALL(*this->connectivity_manager, connect(_)).Times(AtLeast(2));
    EXPECT_CALL(*this->connectivity_manager, disarm_connection_callbacks()).Times(0);

    auto charge_point = make_charge_point();
    charge_point->start({}, BootReasonEnum::PowerUp, {});
    charge_point->stop();
    EXPECT_TRUE(charge_point->restart({}, BootReasonEnum::ApplicationReset));
    charge_point->stop();

    testing::Mock::VerifyAndClearExpectations(this->connectivity_manager.get());
}

// The use-after-free is at destruction: a deferred callback landing while members are destroyed. The
// destructor body runs before any member is gone and blocks on an in-flight callback.
TEST_F(ChargePointConnectivityTest, DestructionDisarmsConnectionCallbacks) {
    ON_CALL(*this->connectivity_manager, is_websocket_connected()).WillByDefault(Return(false));

    auto charge_point = make_charge_point();
    charge_point->start({}, BootReasonEnum::PowerUp, {});
    charge_point->stop();

    EXPECT_CALL(*this->connectivity_manager, disarm_connection_callbacks()).Times(1);
    charge_point.reset();
}

// The connection state callback reports which of the configured network connection slots the CSMS connection
// uses, alongside the profile behind that slot, for both the connect and the disconnect direction.
TEST_F(ChargePointConnectivityTest, ConnectionStateChangedCallbackReportsConfigurationSlot) {
    ON_CALL(*this->connectivity_manager, is_websocket_connected()).WillByDefault(Return(false));

    constexpr int CONFIGURATION_SLOT = 7;
    constexpr std::int32_t SECURITY_PROFILE = 1;

    ocpp::v2::NetworkConnectionProfile profile;
    profile.securityProfile = SECURITY_PROFILE;

    struct Report {
        bool is_connected;
        int configuration_slot;
        std::int32_t security_profile;
    };
    std::vector<Report> reports;

    auto charge_point = make_charge_point();
    charge_point->register_connection_state_changed_callback(
        [&reports](const bool is_connected, const int configuration_slot,
                   const ocpp::v2::NetworkConnectionProfile& network_connection_profile) {
            reports.push_back({is_connected, configuration_slot, network_connection_profile.securityProfile});
        });

    charge_point->start({}, BootReasonEnum::PowerUp, {});

    charge_point->on_websocket_connected(CONFIGURATION_SLOT, profile, ocpp::OcppProtocolVersion::v16);
    charge_point->on_websocket_disconnected(CONFIGURATION_SLOT, profile);

    ASSERT_EQ(reports.size(), 2);
    EXPECT_TRUE(reports.at(0).is_connected);
    EXPECT_EQ(reports.at(0).configuration_slot, CONFIGURATION_SLOT);
    EXPECT_EQ(reports.at(0).security_profile, SECURITY_PROFILE);
    EXPECT_FALSE(reports.at(1).is_connected);
    EXPECT_EQ(reports.at(1).configuration_slot, CONFIGURATION_SLOT);
    EXPECT_EQ(reports.at(1).security_profile, SECURITY_PROFILE);

    charge_point->stop();
}

// Once the charge point observes a successful connection (connected callback -> message queue resume), queued
// outgoing OCPP messages such as the BootNotification.req are handed to the websocket via send_to_websocket().
TEST_F(ChargePointConnectivityTest, OutgoingMessageGoesToSendToWebsocket) {
    ON_CALL(*this->connectivity_manager, is_websocket_connected()).WillByDefault(Return(true));

    std::mutex mtx;
    std::condition_variable cv;
    bool sent = false;

    // The message-queue worker flushes on its own thread; notify a condvar from send_to_websocket so the test
    // can deterministically wait for the transmission instead of racing a fixed sleep.
    EXPECT_CALL(*this->connectivity_manager, send_to_websocket(_))
        .Times(AtLeast(1))
        .WillRepeatedly(Invoke([&](const std::string&) {
            std::lock_guard<std::mutex> lock(mtx);
            sent = true;
            cv.notify_all();
            return true;
        }));

    auto charge_point = make_charge_point();
    charge_point->start({}, BootReasonEnum::PowerUp, {});

    // Simulate the websocket coming up: this resumes the message queue, flushing the queued BootNotification.req.
    charge_point->on_websocket_connected(0, ocpp::v2::NetworkConnectionProfile{}, ocpp::OcppProtocolVersion::v16);

    // Wait (bounded) for the worker thread to hand the queued message to send_to_websocket.
    {
        std::unique_lock<std::mutex> lock(mtx);
        const bool transmitted = cv.wait_for(lock, std::chrono::seconds(5), [&]() { return sent; });
        EXPECT_TRUE(transmitted) << "Queued outgoing message was not handed to send_to_websocket within the timeout";
    }

    charge_point->stop();
}

// An outgoing DataTransfer.req queries is_websocket_connected() on the ConnectivityManager to decide its
// offline-sensitive path.
TEST_F(ChargePointConnectivityTest, OutgoingMessageObservesWebsocketConnected) {
    EXPECT_CALL(*this->connectivity_manager, is_websocket_connected()).Times(AtLeast(1)).WillRepeatedly(Return(false));

    auto charge_point = make_charge_point();
    charge_point->start({}, BootReasonEnum::PowerUp, {});

    charge_point->data_transfer("VendorId", CiString<50>("MessageId"), "data");

    charge_point->stop();
}

// An AuthorizationKey change on security profile 1 answers the ChangeConfiguration.req at once, but reconnects with
// the new key only after the CALL in flight is answered, so that answer is not lost with the old connection.
TEST_F(ChargePointConnectivityTest, AuthorizationKeyChangeAwaitsAnswerToCallInFlight) {
    const std::string new_key = "0123456789ABCDEF0123456789ABCDEF";
    this->configuration->setAuthorizationKey("DEADBEEFDEADBEEFDEADBEEFDEADBEEF");
    this->configuration->setSecurityProfile(1);
    auto charge_point = make_charge_point();

    std::promise<std::string> key_set;
    auto key_set_future = key_set.get_future();
    EXPECT_CALL(*this->connectivity_manager, set_websocket_authorization_key(_))
        .WillOnce(Invoke([&key_set](const std::string& key) { key_set.set_value(key); }));

    const auto in_flight = start_with_call_in_flight(*charge_point);

    this->csms_to_charge_point(
        nlohmann::json{2, "change-key", "ChangeConfiguration", {{"key", "AuthorizationKey"}, {"value", new_key}}}
            .dump());
    const auto call_result = wait_for_sent(
        [](const nlohmann::json& m) { return m.at(0) == MessageTypeId::CALLRESULT && m.at(1) == "change-key"; });
    ASSERT_FALSE(call_result.empty());
    EXPECT_EQ(call_result.at(2).at("status"), "Accepted");
    EXPECT_EQ(key_set_future.wait_for(std::chrono::milliseconds(100)), std::future_status::timeout);

    this->csms_to_charge_point(nlohmann::json{3, in_flight.at(1), nlohmann::json::object()}.dump());
    ASSERT_EQ(key_set_future.wait_for(std::chrono::seconds(1)), std::future_status::ready);
    EXPECT_EQ(key_set_future.get(), new_key);

    charge_point->stop();
}

class ChargePointSecuritySwitchTest : public ChargePointConnectivityTestBase {
protected:
    static constexpr auto REVERT_TIMEOUT = std::chrono::seconds(1);

    void SetUp() override {
        ChargePointConnectivityTestBase::SetUp();

        std::ifstream ifs(CONFIG_FILE_LOCATION_V16);
        const std::string config_file((std::istreambuf_iterator<char>(ifs)), (std::istreambuf_iterator<char>()));
        auto cfg = nlohmann::json::parse(config_file);
        cfg["Internal"]["SwitchSecurityProfileConnectionTimeout"] = REVERT_TIMEOUT.count();
        this->configuration =
            std::make_unique<ChargePointConfiguration>(cfg.dump(), CONFIG_DIR_V16, USER_CONFIG_FILE_LOCATION_V16);

        // Security profile 1 only requires an authorization key (no certificates), making it the cheapest
        // accepted switch from the default profile 0.
        this->configuration->setAuthorizationKey("DEADBEEFDEADBEEFDEADBEEFDEADBEEF");
    }
};

// A CSMS-driven, accepted SecurityProfile switch sets the new profile, reloads the network profiles and connects.
// The connected callback cancels the revert timer armed at that connect(): the websocket keeps reporting disconnected,
// so a timer that was not cancelled would revert with a second reload/connect.
TEST_F(ChargePointSecuritySwitchTest, AcceptedSwitchKeptOnSuccessfulConnect) {
    auto charge_point = make_charge_point();
    charge_point->start({}, BootReasonEnum::PowerUp, {}, /*start_connecting=*/false);

    ASSERT_NE(this->configuration->getSecurityProfile(), 1);

    // The accepted switch must reload profiles and connect exactly once. If the revert erroneously fired, it would
    // produce a second reload_network_profiles()/connect() and exceed these cardinalities.
    std::mutex mtx;
    std::optional<std::thread::id> connect_thread;
    std::promise<std::chrono::steady_clock::time_point> timer_armed;
    auto timer_armed_future = timer_armed.get_future();
    EXPECT_CALL(*this->connectivity_manager, reload_network_profiles()).Times(1);
    EXPECT_CALL(*this->connectivity_manager, connect(_)).WillOnce(Invoke([&](std::optional<std::int32_t>) {
        std::lock_guard<std::mutex> lock(mtx);
        connect_thread = std::this_thread::get_id();
    }));
    // The switch action asks for the connection state on its own thread right after it arms the revert timer.
    ON_CALL(*this->connectivity_manager, is_websocket_connected()).WillByDefault(Invoke([&]() {
        std::lock_guard<std::mutex> lock(mtx);
        if (connect_thread == std::this_thread::get_id()) {
            connect_thread.reset();
            timer_armed.set_value(std::chrono::steady_clock::now());
        }
        return false;
    }));

    const auto status = charge_point->set_configuration_key("SecurityProfile", "1");
    EXPECT_EQ(status, ConfigurationStatus::Accepted);
    EXPECT_EQ(this->configuration->getSecurityProfile(), 1);

    ASSERT_EQ(timer_armed_future.wait_for(std::chrono::seconds(1)), std::future_status::ready);
    const auto armed_at = timer_armed_future.get();
    charge_point->on_websocket_connected(0, ocpp::v2::NetworkConnectionProfile{}, ocpp::OcppProtocolVersion::v16);

    // Wait past the revert timeout, counted from the arming, to prove the cancelled revert does not fire
    std::this_thread::sleep_until(armed_at + REVERT_TIMEOUT + std::chrono::seconds(1));
    EXPECT_EQ(this->configuration->getSecurityProfile(), 1);

    charge_point->stop();
}

// A CSMS-driven, accepted SecurityProfile switch that never sees a successful connection (no connected callback)
// must, after the configured revert timeout, revert to the previous profile and reload/connect again.
TEST_F(ChargePointSecuritySwitchTest, AcceptedSwitchRevertedOnTimeout) {
    auto charge_point = make_charge_point();
    charge_point->start({}, BootReasonEnum::PowerUp, {}, /*start_connecting=*/false);

    const std::int32_t old_profile = this->configuration->getSecurityProfile();
    ASSERT_NE(old_profile, 1);

    std::mutex mtx;
    std::condition_variable cv;
    int connect_count = 0;
    int reload_count = 0;
    std::vector<std::chrono::steady_clock::time_point> connect_times;

    // Count the switch's reload+connect and the revert's reload+connect; signal when the revert (2nd) has happened.
    ON_CALL(*this->connectivity_manager, connect(_)).WillByDefault(Invoke([&](std::optional<std::int32_t>) {
        std::lock_guard<std::mutex> lock(mtx);
        ++connect_count;
        connect_times.push_back(std::chrono::steady_clock::now());
        cv.notify_all();
    }));
    ON_CALL(*this->connectivity_manager, reload_network_profiles()).WillByDefault(Invoke([&]() {
        std::lock_guard<std::mutex> lock(mtx);
        ++reload_count;
        cv.notify_all();
    }));

    const auto status = charge_point->set_configuration_key("SecurityProfile", "1");
    EXPECT_EQ(status, ConfigurationStatus::Accepted);
    EXPECT_EQ(this->configuration->getSecurityProfile(), 1);

    // No connected callback is delivered: wait for the revert timer to fire (return as soon as the 2nd connect lands).
    {
        std::unique_lock<std::mutex> lock(mtx);
        const bool reverted =
            cv.wait_for(lock, REVERT_TIMEOUT + std::chrono::seconds(2), [&]() { return connect_count >= 2; });
        EXPECT_TRUE(reverted) << "Revert timer did not fire within the timeout window";
    }

    // The revert restored the previous profile and issued a second reload/connect, a full revert timeout after the
    // switch's connect().
    EXPECT_EQ(this->configuration->getSecurityProfile(), old_profile);
    {
        std::lock_guard<std::mutex> lock(mtx);
        EXPECT_EQ(connect_count, 2);
        EXPECT_EQ(reload_count, 2);
        ASSERT_EQ(connect_times.size(), 2);
        EXPECT_GE(connect_times.at(1) - connect_times.at(0), REVERT_TIMEOUT);
    }

    // No further revert is armed: give a brief grace window and confirm nothing else fires.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    {
        std::lock_guard<std::mutex> lock(mtx);
        EXPECT_EQ(connect_count, 2);
        EXPECT_EQ(reload_count, 2);
    }

    charge_point->stop();
}

// An accepted switch while a CALL is in flight connects with the new profile only after that CALL is answered, so the
// answer is not lost with the old connection, and the revert timeout is counted from that connect().
TEST_F(ChargePointSecuritySwitchTest, AcceptedSwitchConnectsAfterCallInFlightIsAnswered) {
    auto charge_point = make_charge_point();

    std::promise<void> connected;
    auto connected_future = connected.get_future();
    EXPECT_CALL(*this->connectivity_manager, connect(_)).WillOnce(Invoke([&connected](std::optional<std::int32_t>) {
        connected.set_value();
    }));

    const auto in_flight = start_with_call_in_flight(*charge_point);

    this->csms_to_charge_point(
        nlohmann::json{2, "switch", "ChangeConfiguration", {{"key", "SecurityProfile"}, {"value", "1"}}}.dump());
    const auto call_result = wait_for_sent(
        [](const nlohmann::json& m) { return m.at(0) == MessageTypeId::CALLRESULT && m.at(1) == "switch"; });
    ASSERT_FALSE(call_result.empty());
    EXPECT_EQ(call_result.at(2).at("status"), "Accepted");
    EXPECT_EQ(this->configuration->getSecurityProfile(), 1);

    // Past the revert timeout with the CALL still unanswered: no connect() and no revert.
    EXPECT_EQ(connected_future.wait_for(REVERT_TIMEOUT + std::chrono::milliseconds(500)), std::future_status::timeout);
    EXPECT_EQ(this->configuration->getSecurityProfile(), 1);

    this->csms_to_charge_point(nlohmann::json{3, in_flight.at(1), nlohmann::json::object()}.dump());
    EXPECT_EQ(connected_future.wait_for(std::chrono::seconds(1)), std::future_status::ready);

    charge_point->stop();
}

TEST_F(ChargePointSecuritySwitchTest, OverlappingKeyChangeKeepsProfileSwitchRevert) {
    auto charge_point = make_charge_point();
    const auto in_flight = start_with_call_in_flight(*charge_point);

    std::atomic<bool> connected{true};
    ON_CALL(*this->connectivity_manager, is_websocket_connected()).WillByDefault(Invoke([&connected]() {
        return connected.load();
    }));

    std::atomic<int> action_order{0};
    std::promise<void> reverted;
    auto reverted_future = reverted.get_future();
    EXPECT_CALL(*this->connectivity_manager, reload_network_profiles()).Times(2);
    EXPECT_CALL(*this->connectivity_manager, connect(_))
        .WillOnce(Invoke([&](std::optional<std::int32_t>) {
            EXPECT_EQ(action_order.fetch_add(1), 0);
            connected = false;
        }))
        .WillOnce(Invoke([&](std::optional<std::int32_t>) {
            EXPECT_EQ(action_order.fetch_add(1), 2);
            reverted.set_value();
        }));

    const std::string new_key = "0123456789ABCDEF0123456789ABCDEF";
    std::promise<void> key_applied;
    auto key_future = key_applied.get_future();
    EXPECT_CALL(*this->connectivity_manager, set_websocket_authorization_key(new_key))
        .WillOnce(Invoke([&](const std::string&) {
            EXPECT_EQ(action_order.fetch_add(1), 1);
            key_applied.set_value();
        }));

    this->csms_to_charge_point(
        nlohmann::json{2, "switch", "ChangeConfiguration", {{"key", "SecurityProfile"}, {"value", "1"}}}.dump());
    this->csms_to_charge_point(
        nlohmann::json{2, "key", "ChangeConfiguration", {{"key", "AuthorizationKey"}, {"value", new_key}}}.dump());
    for (const auto* id : {"switch", "key"}) {
        const auto result = wait_for_sent(
            [id](const nlohmann::json& m) { return m.at(0) == MessageTypeId::CALLRESULT && m.at(1) == id; });
        ASSERT_FALSE(result.empty());
        EXPECT_EQ(result.at(2).at("status"), "Accepted");
    }

    EXPECT_EQ(action_order, 0);
    this->csms_to_charge_point(nlohmann::json{3, in_flight.at(1), nlohmann::json::object()}.dump());
    EXPECT_EQ(key_future.wait_for(std::chrono::seconds(1)), std::future_status::ready);

    charge_point->on_websocket_disconnected(0, ocpp::v2::NetworkConnectionProfile{});
    EXPECT_EQ(reverted_future.wait_for(REVERT_TIMEOUT + std::chrono::seconds(2)), std::future_status::ready);
    EXPECT_EQ(this->configuration->getSecurityProfile(), 0);

    charge_point->stop();
}

TEST_F(ChargePointSecuritySwitchTest, ConnectWithoutCloseReleasesQueuedCalls) {
    auto charge_point = make_charge_point();
    const auto in_flight = start_with_call_in_flight(*charge_point);

    std::promise<void> connect_called;
    auto connect_future = connect_called.get_future();
    EXPECT_CALL(*this->connectivity_manager, connect(_)).WillOnce(Invoke([&](std::optional<std::int32_t>) {
        connect_called.set_value();
    }));

    this->csms_to_charge_point(
        nlohmann::json{2, "switch", "ChangeConfiguration", {{"key", "SecurityProfile"}, {"value", "1"}}}.dump());
    const auto result = wait_for_sent(
        [](const nlohmann::json& m) { return m.at(0) == MessageTypeId::CALLRESULT && m.at(1) == "switch"; });
    ASSERT_FALSE(result.empty());
    EXPECT_EQ(result.at(2).at("status"), "Accepted");

    charge_point->on_security_event(CiString<50>("TestSecurityEvent"), CiString<255>("queued-without-close"), true);
    this->csms_to_charge_point(nlohmann::json{3, in_flight.at(1), nlohmann::json::object()}.dump());
    EXPECT_EQ(connect_future.wait_for(std::chrono::seconds(1)), std::future_status::ready);

    std::set<std::string> answered{in_flight.at(1).get<std::string>()};
    bool sent_queued_event = false;
    for (int i = 0; i < 4 && !sent_queued_event; ++i) {
        const auto next = wait_for_sent([&](const nlohmann::json& m) {
            return m.at(0) == MessageTypeId::CALL && m.at(2) != "BootNotification" &&
                   !answered.count(m.at(1).get<std::string>());
        });
        if (next.empty()) {
            break;
        }
        sent_queued_event =
            next.at(2) == "SecurityEventNotification" && next.at(3).at("techInfo") == "queued-without-close";
        answered.insert(next.at(1).get<std::string>());
        if (!sent_queued_event) {
            this->csms_to_charge_point(nlohmann::json{3, next.at(1), nlohmann::json::object()}.dump());
        }
    }
    EXPECT_TRUE(sent_queued_event);

    charge_point->stop();
}

} // namespace v16
} // namespace ocpp
