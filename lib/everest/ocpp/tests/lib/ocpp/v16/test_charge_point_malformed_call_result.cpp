// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

/// \file test_charge_point_malformed_call_result.cpp
/// \brief Tests of CALLRESULT handling in ChargePointImpl.

#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <ocpp/common/connectivity_manager.hpp>
#include <ocpp/v16/charge_point_configuration.hpp>
#include <ocpp/v16/charge_point_impl.hpp>

#include "connectivity_manager_mock.hpp"
#include "evse_security_mock.hpp"

namespace fs = std::filesystem;

using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;

namespace ocpp {
namespace v16 {

namespace {
constexpr auto WAIT_TIMEOUT = std::chrono::seconds(10);
constexpr auto SETTLE_TIMEOUT = std::chrono::milliseconds(500);
// A BootNotification.req is repeated after DEFAULT_BOOT_NOTIFICATION_INTERVAL_S, BOOT_RETRY_TIMEOUT needs to be higher
constexpr auto BOOT_RETRY_TIMEOUT = std::chrono::seconds(90);
constexpr std::int32_t CONNECTOR = 1;
const std::string BOOT_NOTIFICATION_ACTION = "BootNotification";
} // namespace

class ChargePointMalformedCallResultTest : public ::testing::Test {
protected:
    void SetUp() override {
        this->evse_security = std::make_shared<NiceMock<EvseSecurityMock>>();
        this->connectivity_manager = std::make_shared<NiceMock<ConnectivityManagerMock>>();

        this->tmp_dir = fs::temp_directory_path() / ("ocpp_v16_malformed_call_result_test_" +
                                                     std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        fs::create_directories(this->tmp_dir);

        const auto user_config = this->tmp_dir / "user_config.json";
        std::ofstream(user_config) << "{}";

        std::ifstream ifs(CONFIG_FILE_LOCATION_V16);
        const std::string config_file((std::istreambuf_iterator<char>(ifs)), (std::istreambuf_iterator<char>()));
        this->configuration = std::make_unique<ChargePointConfiguration>(config_file, CONFIG_DIR_V16, user_config);

        ON_CALL(*this->connectivity_manager, is_websocket_connected()).WillByDefault(Return(true));
        ON_CALL(*this->connectivity_manager, set_message_callback(_))
            .WillByDefault(Invoke(
                [this](const std::function<void(const std::string&)>& callback) { this->to_charge_point = callback; }));
        ON_CALL(*this->connectivity_manager, send_to_websocket(_))
            .WillByDefault(Invoke([this](const std::string& message) { return this->record_and_answer(message); }));

        this->responder = std::thread([this]() { this->run_responder(); });
    }

    void TearDown() override {
        {
            const std::lock_guard<std::mutex> lock(this->mtx);
            this->stop_responder = true;
        }
        this->cv.notify_all();
        if (this->responder.joinable()) {
            this->responder.join();
        }
        std::error_code ec;
        fs::remove_all(this->tmp_dir, ec);
    }

    std::unique_ptr<ChargePointImpl> make_charge_point() {
        return std::make_unique<ChargePointImpl>(
            *this->configuration, /*share_path=*/fs::path(CONFIG_DIR_V16), /*database_path=*/this->tmp_dir,
            /*sql_init_path=*/fs::path(MIGRATION_FILES_LOCATION_V16), /*message_log_path=*/this->tmp_dir,
            this->evse_security, this->connectivity_manager, /*security_configuration=*/std::nullopt,
            /*message_callback=*/nullptr);
    }

    /// \brief Start a charge point and report the websocket as connected, which sends the first BootNotification.req.
    std::unique_ptr<ChargePointImpl> make_connected_charge_point() {
        auto charge_point = make_charge_point();
        charge_point->start({{0, ChargePointStatus::Available}, {CONNECTOR, ChargePointStatus::Available}},
                            BootReasonEnum::PowerUp, {});
        charge_point->on_websocket_connected(0, ocpp::v2::NetworkConnectionProfile{}, ocpp::OcppProtocolVersion::v16);
        return charge_point;
    }

    /// \brief Capture an outgoing call and queue a CallResult for the responder thread.
    bool record_and_answer(const std::string& message) {
        const auto call = nlohmann::json::parse(message, nullptr, false);
        if (call.is_discarded() or not call.is_array() or call.size() < 4 or call.at(0) != 2) {
            return true;
        }

        const std::string unique_id = call.at(1);
        const std::string action = call.at(2);

        {
            const std::lock_guard<std::mutex> lock(this->mtx);
            this->sent.push_back(call);
            this->pending_responses.push_back(
                nlohmann::json::array({3, unique_id, this->response_payload_for_locked(action)}));
        }
        this->cv.notify_all();
        return true;
    }

    /// \brief payload for the given \p action
    nlohmann::json response_payload_for_locked(const std::string& action) {
        if (action == BOOT_NOTIFICATION_ACTION) {
            /// The first BootNotification.req gets a specific payload, all others are accepted
            if (this->first_boot_notification_payload.has_value()) {
                auto payload = this->first_boot_notification_payload.value();
                this->first_boot_notification_payload.reset();
                return payload;
            }
            return nlohmann::json{
                {"currentTime", ocpp::DateTime().to_rfc3339()}, {"interval", 86400}, {"status", "Accepted"}};
        }
        return nlohmann::json::object();
    }

    /// \brief Deliver queued CallResults off the message-queue worker thread to avoid re-entering it.
    /// Exceptions are recorded instead of terminating the test binary.
    void run_responder() {
        while (true) {
            nlohmann::json response;
            {
                std::unique_lock<std::mutex> lock(this->mtx);
                this->cv.wait(lock, [this]() { return this->stop_responder or not this->pending_responses.empty(); });
                if (this->stop_responder) {
                    return;
                }
                response = this->pending_responses.front();
                this->pending_responses.pop_front();
            }
            if (this->to_charge_point) {
                std::optional<std::string> failure;
                try {
                    this->to_charge_point(response.dump());
                } catch (const std::exception& e) {
                    failure = e.what();
                } catch (...) {
                    failure = "non-std exception";
                }
                {
                    const std::lock_guard<std::mutex> lock(this->mtx);
                    this->delivered += 1;
                    if (failure.has_value()) {
                        this->escaped_exceptions.push_back(failure.value());
                    }
                }
                this->cv.notify_all();
            }
        }
    }

    /// \brief Wait until at least \p count CallResults arrived at the chargepoint.
    bool wait_for_delivered(std::size_t count) {
        std::unique_lock<std::mutex> lock(this->mtx);
        return this->cv.wait_for(lock, WAIT_TIMEOUT, [this, count]() { return this->delivered >= count; });
    }

    /// \brief Wait until at least \p count calls with \p action have been sent by the charge point.
    bool wait_for_action_count(const std::string& action, std::size_t count, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(this->mtx);
        return this->cv.wait_for(lock, timeout,
                                 [this, &action, count]() { return this->count_action_locked(action) >= count; });
    }

    /// \brief Wait until a StatusNotification.req with the given \p status for the tested connector is received.
    /// A (short) \p timeout can be used to check for a missing StatusNotification.req
    bool wait_for_status(const std::string& status, std::chrono::milliseconds timeout = WAIT_TIMEOUT) {
        std::unique_lock<std::mutex> lock(this->mtx);
        return this->cv.wait_for(lock, timeout, [this, &status]() {
            for (const auto& call : this->sent) {
                if (call.at(2) == "StatusNotification" and call.at(3).at("connectorId") == CONNECTOR and
                    call.at(3).at("status") == status) {
                    return true;
                }
            }
            return false;
        });
    }

    std::size_t count_action(const std::string& action) {
        const std::lock_guard<std::mutex> lock(this->mtx);
        return this->count_action_locked(action);
    }

    std::size_t count_action_locked(const std::string& action) const {
        std::size_t count = 0;
        for (const auto& call : this->sent) {
            if (call.at(2) == action) {
                count += 1;
            }
        }
        return count;
    }

    std::vector<std::string> get_escaped_exceptions() {
        const std::lock_guard<std::mutex> lock(this->mtx);
        return this->escaped_exceptions;
    }

    void stage_first_boot_notification_payload(nlohmann::json payload) {
        const std::lock_guard<std::mutex> lock(this->mtx);
        this->first_boot_notification_payload = std::move(payload);
    }

    std::shared_ptr<NiceMock<EvseSecurityMock>> evse_security;
    std::shared_ptr<NiceMock<ConnectivityManagerMock>> connectivity_manager;
    std::unique_ptr<ChargePointConfiguration> configuration;
    fs::path tmp_dir;

    std::mutex mtx;
    std::condition_variable cv;
    std::vector<nlohmann::json> sent;
    std::deque<nlohmann::json> pending_responses;
    std::optional<nlohmann::json> first_boot_notification_payload;
    std::vector<std::string> escaped_exceptions;
    std::size_t delivered{0};
    std::function<void(const std::string&)> to_charge_point;
    std::thread responder;
    bool stop_responder{false};
};

// Ensure that a malformed CALLRESULT does not throw a json type_error.
TEST_F(ChargePointMalformedCallResultTest, NumberPayloadDoesNotEscapeMessageCallback) {
    stage_first_boot_notification_payload(12345);

    auto charge_point = make_connected_charge_point();

    ASSERT_TRUE(wait_for_action_count(BOOT_NOTIFICATION_ACTION, 1, WAIT_TIMEOUT)) << "no BootNotification.req was sent";
    ASSERT_TRUE(wait_for_delivered(1)) << "the malformed CALLRESULT was not delivered";

    EXPECT_THAT(get_escaped_exceptions(), ::testing::IsEmpty());

    charge_point->stop();
}

// After a malformed answer the BootNotification.req is unanswered from the charge point's point of view: it has neither
// a registration status nor an interval. The charge point has to repeat it, and once the CSMS answers properly the boot
// completes with the initial StatusNotification.req.
TEST_F(ChargePointMalformedCallResultTest, NumberPayloadIsFollowedByBootNotificationRetry) {
    stage_first_boot_notification_payload(12345);

    auto charge_point = make_connected_charge_point();

    ASSERT_TRUE(wait_for_action_count(BOOT_NOTIFICATION_ACTION, 1, WAIT_TIMEOUT)) << "no BootNotification.req was sent";
    ASSERT_TRUE(wait_for_delivered(1)) << "the malformed CALLRESULT was not delivered";
    ASSERT_THAT(get_escaped_exceptions(), ::testing::IsEmpty());

    // The charge point must not accept the malformed answer as a registration.
    EXPECT_FALSE(wait_for_status("Available", SETTLE_TIMEOUT))
        << "a StatusNotification.req was sent without an accepted boot";

    EXPECT_TRUE(wait_for_action_count(BOOT_NOTIFICATION_ACTION, 2, BOOT_RETRY_TIMEOUT))
        << "the BootNotification.req was not repeated after the malformed CALLRESULT";
    EXPECT_TRUE(wait_for_status("Available")) << "the repeated BootNotification.req did not complete the boot";

    charge_point->stop();
}

// The unhappy path must not disturb an ordinary boot: with a proper BootNotification.conf the same fixture reaches
// Available.
TEST_F(ChargePointMalformedCallResultTest, WellFormedPayloadBoots) {
    auto charge_point = make_connected_charge_point();

    EXPECT_TRUE(wait_for_status("Available")) << "boot handshake did not produce the initial StatusNotification.req";
    EXPECT_THAT(get_escaped_exceptions(), ::testing::IsEmpty());
    EXPECT_EQ(count_action(BOOT_NOTIFICATION_ACTION), 1u);

    charge_point->stop();
}

} // namespace v16
} // namespace ocpp
