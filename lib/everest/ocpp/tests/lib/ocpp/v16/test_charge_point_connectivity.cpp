// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

/// \file test_charge_point_connectivity.cpp
/// \brief Behavioural unit tests for the v16 ChargePointImpl <-> ConnectivityManager wiring.
///
/// These tests construct a ChargePointImpl through its constructor, passing a mocked
/// ConnectivityManager, and assert observable interactions only:
///   * that an injected manager is NOT auto-wired for the websocket lifecycle at construction (only set_logging),
///     and that the message callback is registered later in start(),
///   * that the drive surface (start/stop/outgoing message/offline query) hits the manager, and
///   * the security-profile switch + revert behaviour orchestrated via the manager and the
///     internal websocket revert timer.

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <thread>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

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
        this->tmp_dir = fs::temp_directory_path() /
                        ("ocpp_v16_connectivity_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        fs::create_directories(this->tmp_dir);
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

    std::shared_ptr<NiceMock<EvseSecurityMock>> evse_security;
    std::shared_ptr<NiceMock<ConnectivityManagerMock>> connectivity_manager;
    std::unique_ptr<ChargePointConfiguration> configuration;
    fs::path tmp_dir;
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
// When the new profile yields a successful connection (connected callback), the revert timer is cancelled: the new
// profile is kept and no second reload/connect (which would carry the old profile) ever fires.
TEST_F(ChargePointSecuritySwitchTest, AcceptedSwitchKeptOnSuccessfulConnect) {
    auto charge_point = make_charge_point();

    ASSERT_NE(this->configuration->getSecurityProfile(), 1);

    // The accepted switch must reload profiles and connect exactly once. If the revert erroneously fired, it would
    // produce a second reload_network_profiles()/connect() and exceed these cardinalities.
    EXPECT_CALL(*this->connectivity_manager, reload_network_profiles()).Times(1);
    EXPECT_CALL(*this->connectivity_manager, connect(_)).Times(1);

    const auto status = charge_point->set_configuration_key("SecurityProfile", "1");
    EXPECT_EQ(status, ConfigurationStatus::Accepted);
    EXPECT_EQ(this->configuration->getSecurityProfile(), 1);

    // A successful connection cancels the armed revert.
    charge_point->on_websocket_connected(0, ocpp::v2::NetworkConnectionProfile{}, ocpp::OcppProtocolVersion::v16);

    // Wait past the revert timeout to prove the revert does not fire
    std::this_thread::sleep_for(REVERT_TIMEOUT + std::chrono::seconds(1));
    EXPECT_EQ(this->configuration->getSecurityProfile(), 1);
}

// A CSMS-driven, accepted SecurityProfile switch that never sees a successful connection (no connected callback)
// must, after the configured revert timeout, revert to the previous profile and reload/connect again.
TEST_F(ChargePointSecuritySwitchTest, AcceptedSwitchRevertedOnTimeout) {
    auto charge_point = make_charge_point();

    const std::int32_t old_profile = this->configuration->getSecurityProfile();
    ASSERT_NE(old_profile, 1);

    std::mutex mtx;
    std::condition_variable cv;
    int connect_count = 0;
    int reload_count = 0;

    // Count the switch's reload+connect and the revert's reload+connect; signal when the revert (2nd) has happened.
    ON_CALL(*this->connectivity_manager, connect(_)).WillByDefault(Invoke([&](std::optional<std::int32_t>) {
        std::lock_guard<std::mutex> lock(mtx);
        ++connect_count;
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

    // The revert restored the previous profile and issued a second reload/connect.
    EXPECT_EQ(this->configuration->getSecurityProfile(), old_profile);
    {
        std::lock_guard<std::mutex> lock(mtx);
        EXPECT_EQ(connect_count, 2);
        EXPECT_EQ(reload_count, 2);
    }

    // No further revert is armed: give a brief grace window and confirm nothing else fires.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    {
        std::lock_guard<std::mutex> lock(mtx);
        EXPECT_EQ(connect_count, 2);
        EXPECT_EQ(reload_count, 2);
    }
}

} // namespace v16
} // namespace ocpp
