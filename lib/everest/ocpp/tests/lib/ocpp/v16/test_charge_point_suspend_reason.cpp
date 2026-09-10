// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

/// \file test_charge_point_suspend_reason.cpp
/// \brief Wire-level tests for the v16 suspend entry points of ChargePointImpl.
///
/// A ChargePointImpl is driven through a mocked ConnectivityManager. Outgoing calls are captured and answered by a
/// responder thread so the message queue keeps flowing, which makes the emitted StatusNotification.req sequence
/// observable.

#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
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
constexpr std::int32_t CONNECTOR = 1;
} // namespace

class ChargePointSuspendReasonTest : public ::testing::Test {
protected:
    void SetUp() override {
        this->evse_security = std::make_shared<NiceMock<EvseSecurityMock>>();
        this->connectivity_manager = std::make_shared<NiceMock<ConnectivityManagerMock>>();

        this->tmp_dir = fs::temp_directory_path() /
                        ("ocpp_v16_suspend_reason_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        fs::create_directories(this->tmp_dir);

        // A per-test user config keeps configuration writes out of the shared test resources.
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
                nlohmann::json::array({3, unique_id, this->response_payload_for(action)}));
        }
        this->cv.notify_all();
        return true;
    }

    nlohmann::json response_payload_for(const std::string& action) const {
        if (action == "BootNotification") {
            return nlohmann::json{
                {"currentTime", ocpp::DateTime().to_rfc3339()}, {"interval", 86400}, {"status", "Accepted"}};
        }
        return nlohmann::json::object();
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

    /// \brief Wait until a StatusNotification.req reporting \p status for \p CONNECTOR has been sent.
    bool wait_for_status(const std::string& status) {
        std::unique_lock<std::mutex> lock(this->mtx);
        return this->cv.wait_for(lock, WAIT_TIMEOUT,
                                 [this, &status]() { return this->count_status_locked(status) != 0; });
    }

    /// \brief Wait for a StatusNotification.req carrying \p info . Used with a short \p timeout to assert absence.
    bool wait_for_info(const std::string& info, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(this->mtx);
        return this->cv.wait_for(lock, timeout, [this, &info]() {
            for (const auto& call : this->sent) {
                if (call.at(2) == "StatusNotification" and call.at(3).value("info", std::string{}) == info) {
                    return true;
                }
            }
            return false;
        });
    }

    std::size_t count_status(const std::string& status) {
        const std::lock_guard<std::mutex> lock(this->mtx);
        return this->count_status_locked(status);
    }

    std::size_t sent_count() {
        const std::lock_guard<std::mutex> lock(this->mtx);
        return this->sent.size();
    }

    /// \brief Feed a CSMS TriggerMessage(StatusNotification) into the charge point.
    void trigger_status_notification(const std::optional<std::int32_t> connector_id) {
        nlohmann::json payload{{"requestedMessage", "StatusNotification"}};
        if (connector_id.has_value()) {
            payload["connectorId"] = connector_id.value();
        }
        const auto unique_id = "trigger-" + std::to_string(++this->trigger_counter);
        this->to_charge_point(nlohmann::json::array({2, unique_id, "TriggerMessage", payload}).dump());
    }

    /// \brief Wait for the first StatusNotification.req for \p CONNECTOR recorded at or after index \p from .
    std::optional<nlohmann::json> wait_for_status_notification_after(std::size_t from) {
        std::unique_lock<std::mutex> lock(this->mtx);
        const auto found = this->cv.wait_for(lock, WAIT_TIMEOUT, [this, from]() {
            for (std::size_t i = from; i < this->sent.size(); ++i) {
                if (this->sent.at(i).at(2) == "StatusNotification" and
                    this->sent.at(i).at(3).at("connectorId") == CONNECTOR) {
                    return true;
                }
            }
            return false;
        });
        if (not found) {
            return std::nullopt;
        }
        for (std::size_t i = from; i < this->sent.size(); ++i) {
            if (this->sent.at(i).at(2) == "StatusNotification" and
                this->sent.at(i).at(3).at("connectorId") == CONNECTOR) {
                return this->sent.at(i).at(3);
            }
        }
        return std::nullopt;
    }

    /// \brief Bring a charge point up to an accepted BootNotification with \p CONNECTOR available.
    std::unique_ptr<ChargePointImpl> make_booted_charge_point() {
        auto charge_point = make_charge_point();
        charge_point->start({{0, ChargePointStatus::Available}, {CONNECTOR, ChargePointStatus::Available}},
                            BootReasonEnum::PowerUp, {});
        charge_point->on_websocket_connected(0, ocpp::v2::NetworkConnectionProfile{}, ocpp::OcppProtocolVersion::v16);
        return charge_point;
    }

    std::size_t count_status_locked(const std::string& status) const {
        std::size_t count = 0;
        for (const auto& call : this->sent) {
            if (call.at(2) == "StatusNotification" and call.at(3).at("connectorId") == CONNECTOR and
                call.at(3).at("status") == status) {
                ++count;
            }
        }
        return count;
    }

    std::shared_ptr<NiceMock<EvseSecurityMock>> evse_security;
    std::shared_ptr<NiceMock<ConnectivityManagerMock>> connectivity_manager;
    std::unique_ptr<ChargePointConfiguration> configuration;
    fs::path tmp_dir;

    std::mutex mtx;
    std::condition_variable cv;
    std::vector<nlohmann::json> sent;
    std::deque<nlohmann::json> pending_responses;
    std::function<void(const std::string&)> to_charge_point;
    std::thread responder;
    bool stop_responder{false};
    std::int32_t trigger_counter{0};
};

// ChargePointV16 decides whether to encode a pause reason by reading the variable back over GetConfiguration, so the
// key has to be dispatched by name. An unknownKey here would leave the feature permanently off.
TEST_F(ChargePointSuspendReasonTest, ConfigurationKeyIsReadableByName) {
    auto charge_point = make_charge_point();

    GetConfigurationRequest request;
    request.key = {CiString<50>("ReportSuspendedEVSEReasonChange")};

    auto response = charge_point->get_configuration_key(request);
    EXPECT_FALSE(response.unknownKey.has_value());
    ASSERT_TRUE(response.configurationKey.has_value());
    ASSERT_EQ(response.configurationKey->size(), 1u);
    ASSERT_TRUE(response.configurationKey->at(0).value.has_value());
    EXPECT_EQ(static_cast<std::string>(response.configurationKey->at(0).value.value()), "false");

    this->configuration->setReportSuspendedEVSEReasonChange(true);

    response = charge_point->get_configuration_key(request);
    ASSERT_TRUE(response.configurationKey.has_value());
    ASSERT_EQ(response.configurationKey->size(), 1u);
    ASSERT_TRUE(response.configurationKey->at(0).value.has_value());
    EXPECT_EQ(static_cast<std::string>(response.configurationKey->at(0).value.value()), "true");
}

// With ReportSuspendedEVSEReasonChange at its default false, a phase switch reported while the connector already sits
// in SuspendedEVSE must not reach the wire. Releases before the SuspendedEVSE self-transition existed dropped it in
// the state machine; the ungated entry point has to drop it now.
TEST_F(ChargePointSuspendReasonTest, PhaseSwitchWhileSuspendedIsNotReported) {
    ASSERT_FALSE(this->configuration->getReportSuspendedEVSEReasonChange());

    auto charge_point = make_charge_point();
    charge_point->start({{0, ChargePointStatus::Available}, {CONNECTOR, ChargePointStatus::Available}},
                        BootReasonEnum::PowerUp, {});
    charge_point->on_websocket_connected(0, ocpp::v2::NetworkConnectionProfile{}, ocpp::OcppProtocolVersion::v16);

    ASSERT_TRUE(wait_for_status("Available")) << "boot handshake did not produce the initial StatusNotification.req";

    charge_point->on_suspend_charging_evse(CONNECTOR);
    ASSERT_TRUE(wait_for_status("SuspendedEVSE")) << "the entry transition was not reported";

    charge_point->on_suspend_charging_evse(CONNECTOR, CiString<50>("SwitchingPhases"));

    // The notification timer runs at zero seconds, so an emitted message shows up well inside this window.
    EXPECT_FALSE(wait_for_info("SwitchingPhases", SETTLE_TIMEOUT))
        << "a phase switch on an already suspended connector reached the wire";
    EXPECT_EQ(count_status("SuspendedEVSE"), 1u);

    // The pipeline is still live: the next genuine transition is reported.
    charge_point->on_resume_charging(CONNECTOR);
    EXPECT_TRUE(wait_for_status("Charging")) << "the resume transition was not reported";

    charge_point->stop();
}

// A phase switch that genuinely enters SuspendedEVSE still carries its info verbatim.
TEST_F(ChargePointSuspendReasonTest, PhaseSwitchEnteringSuspendedIsReported) {
    auto charge_point = make_charge_point();
    charge_point->start({{0, ChargePointStatus::Available}, {CONNECTOR, ChargePointStatus::Available}},
                        BootReasonEnum::PowerUp, {});
    charge_point->on_websocket_connected(0, ocpp::v2::NetworkConnectionProfile{}, ocpp::OcppProtocolVersion::v16);

    ASSERT_TRUE(wait_for_status("Available")) << "boot handshake did not produce the initial StatusNotification.req";

    charge_point->on_suspend_charging_evse(CONNECTOR, CiString<50>("SwitchingPhases"));
    ASSERT_TRUE(wait_for_status("SuspendedEVSE")) << "the entry transition was not reported";

    EXPECT_TRUE(wait_for_info("SwitchingPhases", SETTLE_TIMEOUT)) << "the entry transition dropped its info";

    charge_point->stop();
}

// With ReportSuspendedEVSEReasonChange set, the guard on the ungated entry point must not fire, so a phase switch on
// an already suspended connector reaches the wire.
TEST_F(ChargePointSuspendReasonTest, PhaseSwitchWhileSuspendedIsReportedWhenEnabled) {
    this->configuration->setReportSuspendedEVSEReasonChange(true);

    auto charge_point = make_charge_point();
    charge_point->start({{0, ChargePointStatus::Available}, {CONNECTOR, ChargePointStatus::Available}},
                        BootReasonEnum::PowerUp, {});
    charge_point->on_websocket_connected(0, ocpp::v2::NetworkConnectionProfile{}, ocpp::OcppProtocolVersion::v16);

    ASSERT_TRUE(wait_for_status("Available")) << "boot handshake did not produce the initial StatusNotification.req";

    charge_point->on_suspend_charging_evse(CONNECTOR);
    ASSERT_TRUE(wait_for_status("SuspendedEVSE")) << "the entry transition was not reported";

    charge_point->on_suspend_charging_evse(CONNECTOR, CiString<50>("SwitchingPhases"));
    EXPECT_TRUE(wait_for_info("SwitchingPhases", WAIT_TIMEOUT)) << "the phase switch was suppressed";

    charge_point->stop();
}

// With the variable at its default false, a triggered StatusNotification reports error state only, even for a
// connector that entered SuspendedEVSE carrying an info.
TEST_F(ChargePointSuspendReasonTest, TriggeredStatusNotificationOmitsInfoWhenDisabled) {
    ASSERT_FALSE(this->configuration->getReportSuspendedEVSEReasonChange());

    auto charge_point = make_booted_charge_point();
    ASSERT_TRUE(wait_for_status("Available")) << "boot handshake did not produce the initial StatusNotification.req";

    charge_point->on_suspend_charging_evse(CONNECTOR, CiString<50>("SwitchingPhases"));
    ASSERT_TRUE(wait_for_status("SuspendedEVSE")) << "the entry transition was not reported";

    const auto from = sent_count();
    trigger_status_notification(CONNECTOR);

    const auto triggered = wait_for_status_notification_after(from);
    ASSERT_TRUE(triggered.has_value()) << "the TriggerMessage produced no StatusNotification.req";
    EXPECT_EQ(triggered->at("status"), "SuspendedEVSE");
    EXPECT_FALSE(triggered->contains("info")) << "a triggered message reported info with the variable off";

    charge_point->stop();
}

// With the variable set, a triggered StatusNotification falls back to the current suspend reason when the connector
// is suspended and carries no active error.
TEST_F(ChargePointSuspendReasonTest, TriggeredStatusNotificationReportsSuspendReasonWhenEnabled) {
    this->configuration->setReportSuspendedEVSEReasonChange(true);

    auto charge_point = make_booted_charge_point();
    ASSERT_TRUE(wait_for_status("Available")) << "boot handshake did not produce the initial StatusNotification.req";

    charge_point->on_suspend_charging_evse(CONNECTOR, CiString<50>("NoEnergy"));
    ASSERT_TRUE(wait_for_status("SuspendedEVSE")) << "the entry transition was not reported";

    const auto from = sent_count();
    trigger_status_notification(CONNECTOR);

    const auto triggered = wait_for_status_notification_after(from);
    ASSERT_TRUE(triggered.has_value()) << "the TriggerMessage produced no StatusNotification.req";
    EXPECT_EQ(triggered->at("status"), "SuspendedEVSE");
    EXPECT_EQ(triggered->value("info", std::string{}), "NoEnergy");

    charge_point->stop();
}

// The all-connectors form of the TriggerMessage reports the suspend reason too.
TEST_F(ChargePointSuspendReasonTest, TriggeredStatusNotificationForAllConnectorsReportsSuspendReason) {
    this->configuration->setReportSuspendedEVSEReasonChange(true);

    auto charge_point = make_booted_charge_point();
    ASSERT_TRUE(wait_for_status("Available")) << "boot handshake did not produce the initial StatusNotification.req";

    charge_point->on_suspend_charging_evse(CONNECTOR, CiString<50>("NoEnergy"));
    ASSERT_TRUE(wait_for_status("SuspendedEVSE")) << "the entry transition was not reported";

    const auto from = sent_count();
    trigger_status_notification(std::nullopt);

    const auto triggered = wait_for_status_notification_after(from);
    ASSERT_TRUE(triggered.has_value()) << "the TriggerMessage produced no StatusNotification.req";
    EXPECT_EQ(triggered->at("status"), "SuspendedEVSE");
    EXPECT_EQ(triggered->value("info", std::string{}), "NoEnergy");

    charge_point->stop();
}

// An active error still owns the info field: the suspend reason is a fallback, not an override.
TEST_F(ChargePointSuspendReasonTest, TriggeredStatusNotificationKeepsActiveErrorInfo) {
    this->configuration->setReportSuspendedEVSEReasonChange(true);

    auto charge_point = make_booted_charge_point();
    ASSERT_TRUE(wait_for_status("Available")) << "boot handshake did not produce the initial StatusNotification.req";

    charge_point->on_suspend_charging_evse(CONNECTOR, CiString<50>("NoEnergy"));
    ASSERT_TRUE(wait_for_status("SuspendedEVSE")) << "the entry transition was not reported";

    charge_point->on_error(CONNECTOR, ErrorInfo("uuid1", ChargePointErrorCode::OtherError, false, "SomeError"));
    ASSERT_TRUE(wait_for_info("SomeError", WAIT_TIMEOUT)) << "the error was not reported";

    const auto from = sent_count();
    trigger_status_notification(CONNECTOR);

    const auto triggered = wait_for_status_notification_after(from);
    ASSERT_TRUE(triggered.has_value()) << "the TriggerMessage produced no StatusNotification.req";
    EXPECT_EQ(triggered->at("status"), "SuspendedEVSE");
    EXPECT_EQ(triggered->at("errorCode"), "OtherError");
    EXPECT_EQ(triggered->value("info", std::string{}), "SomeError");

    charge_point->stop();
}

} // namespace v16
} // namespace ocpp
