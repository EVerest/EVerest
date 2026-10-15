// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

/// \file test_charge_point_status_notification.cpp
/// \brief Wire-level tests for how ChargePointImpl schedules StatusNotification.req.
///
/// A ChargePointImpl is driven through a mocked ConnectivityManager. Outgoing calls are captured and
/// answered by a responder thread so the message queue keeps flowing, which makes the emitted
/// StatusNotification.req sequence observable.

#include <algorithm>
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
#include <test_temp_paths.hpp>

#include <ocpp/common/connectivity_manager.hpp>
#include <ocpp/v16/charge_point_configuration.hpp>
#include <ocpp/v16/charge_point_impl.hpp>
#include <ocpp/v16/charge_point_state_machine.hpp>

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
constexpr auto SETTLE_TIMEOUT = std::chrono::milliseconds(300);
constexpr std::int32_t CONNECTOR = 1;
} // namespace

class ChargePointStatusNotificationTest : public ::testing::Test {
protected:
    void SetUp() override {
        this->evse_security = std::make_shared<NiceMock<EvseSecurityMock>>();
        this->connectivity_manager = std::make_shared<NiceMock<ConnectivityManagerMock>>();

        this->tmp_dir = libocpp_test::unique_temp_directory("ocpp_v16_status_notification_test");

        // A per-test user config keeps configuration writes out of the shared test resources.
        const auto user_config = this->tmp_dir / "user_config.json";
        std::ofstream(user_config) << "{}";

        build_configuration(std::nullopt);

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

    /// \brief Build the configuration, optionally carrying a MinimumStatusDuration. The shared test config omits
    /// the key, and setMinimumStatusDuration only writes keys that are already present, so it is injected here.
    void build_configuration(const std::optional<std::int32_t> minimum_status_duration) {
        std::ifstream ifs(CONFIG_FILE_LOCATION_V16);
        auto config = nlohmann::json::parse(
            std::string((std::istreambuf_iterator<char>(ifs)), (std::istreambuf_iterator<char>())));
        if (minimum_status_duration.has_value()) {
            config["Core"]["MinimumStatusDuration"] = minimum_status_duration.value();
        }
        this->configuration = std::make_unique<ChargePointConfiguration>(config.dump(), CONFIG_DIR_V16,
                                                                         this->tmp_dir / "user_config.json");
    }

    /// \brief Capture an outgoing call and queue a CallResult for the responder thread.
    bool record_and_answer(const std::string& message) {
        const auto call = nlohmann::json::parse(message, nullptr, false);
        if (call.is_discarded() or not call.is_array() or call.size() < 4 or call.at(0) != 2) {
            return true;
        }

        const std::string unique_id = call.at(1);
        const std::string action = call.at(2);
        nlohmann::json response_payload = nlohmann::json::object();
        if (action == "BootNotification") {
            response_payload = nlohmann::json{
                {"currentTime", ocpp::DateTime().to_rfc3339()}, {"interval", 86400}, {"status", "Accepted"}};
        }

        {
            const std::lock_guard<std::mutex> lock(this->mtx);
            if (action == "StatusNotification" and call.at(3).at("connectorId") == CONNECTOR) {
                this->status_infos.push_back(call.at(3).value("info", std::string{}));
            }
            this->pending_responses.push_back(nlohmann::json::array({3, unique_id, response_payload}));
        }
        this->cv.notify_all();
        return true;
    }

    /// \brief Deliver queued CallResults off the message-queue worker thread to avoid re-entering it.
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
                this->to_charge_point(response.dump());
            }
        }
    }

    /// \brief Wait until a StatusNotification.req carrying \p info has been sent for \p CONNECTOR .
    bool wait_for_info(const std::string& info, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(this->mtx);
        return this->cv.wait_for(lock, timeout, [this, &info]() {
            return std::find(this->status_infos.begin(), this->status_infos.end(), info) != this->status_infos.end();
        });
    }

    /// \brief Bring a charge point up to an accepted BootNotification with \p CONNECTOR available.
    std::unique_ptr<ChargePointImpl> make_booted_charge_point() {
        auto charge_point = std::make_unique<ChargePointImpl>(
            *this->configuration, /*share_path=*/fs::path(CONFIG_DIR_V16), /*database_path=*/this->tmp_dir,
            /*sql_init_path=*/fs::path(MIGRATION_FILES_LOCATION_V16), /*message_log_path=*/this->tmp_dir,
            this->evse_security, this->connectivity_manager, /*security_configuration=*/std::nullopt,
            /*message_callback=*/nullptr);
        charge_point->start({{0, ChargePointStatus::Available}, {CONNECTOR, ChargePointStatus::Available}},
                            BootReasonEnum::PowerUp, {});
        charge_point->on_websocket_connected(0, ocpp::v2::NetworkConnectionProfile{}, ocpp::OcppProtocolVersion::v16);
        return charge_point;
    }

    std::shared_ptr<NiceMock<EvseSecurityMock>> evse_security;
    std::shared_ptr<NiceMock<ConnectivityManagerMock>> connectivity_manager;
    std::unique_ptr<ChargePointConfiguration> configuration;
    fs::path tmp_dir;

    std::mutex mtx;
    std::condition_variable cv;
    std::vector<std::string> status_infos;
    std::deque<nlohmann::json> pending_responses;
    std::function<void(const std::string&)> to_charge_point;
    std::thread responder;
    bool stop_responder{false};
};

// A board-support error and the fault EVerest derives from it arrive well under a millisecond apart and carry
// different diagnostics, so both have to reach the CSMS. Scheduling them on the timer thread at a zero
// MinimumStatusDuration let the second raise cancel the first notification before it was ever sent.
TEST_F(ChargePointStatusNotificationTest, ErrorsInQuickSuccessionAreEachReported) {
    ASSERT_EQ(this->configuration->getMinimumStatusDuration().value_or(0), 0)
        << "this test covers the undebounced path";

    auto charge_point = make_booted_charge_point();
    ASSERT_TRUE(wait_for_info("", WAIT_TIMEOUT)) << "boot handshake did not produce the initial StatusNotification.req";

    charge_point->on_error(CONNECTOR, ErrorInfo("uuid-vendor", ChargePointErrorCode::OtherError, /*is_fault=*/false,
                                                "bsp_1->main", "some vendor diagnostic text"));
    charge_point->on_error(CONNECTOR,
                           ErrorInfo("uuid-inoperative", ChargePointErrorCode::OtherError,
                                     /*is_fault=*/true, "caused_by:evse_board_support/VendorError", "EVerest"));

    EXPECT_TRUE(wait_for_info("bsp_1->main", WAIT_TIMEOUT)) << "the first error was never reported to the CSMS";
    EXPECT_TRUE(wait_for_info("caused_by:evse_board_support/VendorError", WAIT_TIMEOUT))
        << "the second error was never reported to the CSMS";

    charge_point->stop();
}

// A configured MinimumStatusDuration still holds the notification back for that long, so a status that is not
// yet stable can be superseded. Sending undebounced must stay confined to a configured duration of zero.
TEST_F(ChargePointStatusNotificationTest, ConfiguredMinimumStatusDurationDelaysTheNotification) {
    build_configuration(1);

    auto charge_point = make_booted_charge_point();
    ASSERT_TRUE(wait_for_info("", WAIT_TIMEOUT)) << "boot handshake did not produce the initial StatusNotification.req";

    charge_point->on_error(CONNECTOR, ErrorInfo("uuid-debounced", ChargePointErrorCode::OtherError, /*is_fault=*/true,
                                                "debounced", "EVerest"));

    EXPECT_FALSE(wait_for_info("debounced", SETTLE_TIMEOUT)) << "the notification was not debounced at all";
    EXPECT_TRUE(wait_for_info("debounced", WAIT_TIMEOUT)) << "the debounced notification was never released";

    charge_point->stop();
}

} // namespace v16
} // namespace ocpp
