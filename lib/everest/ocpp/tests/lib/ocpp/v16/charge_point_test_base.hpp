// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
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

#include "connectivity_manager_mock.hpp"
#include "evse_security_mock.hpp"

namespace ocpp::v16 {

namespace fs = std::filesystem;

class ChargePointTestBase : public ::testing::Test {
protected:
    static constexpr auto WAIT_TIMEOUT = std::chrono::seconds(10);

    void SetUp() override {
        this->evse_security = std::make_shared<::testing::NiceMock<EvseSecurityMock>>();
        this->connectivity_manager = std::make_shared<::testing::NiceMock<ConnectivityManagerMock>>();

        this->tmp_dir = libocpp_test::unique_temp_directory("ocpp_v16_charge_point_test");

        const auto user_config = this->tmp_dir / "user_config.json";
        std::ofstream(user_config) << "{}";

        std::ifstream ifs(CONFIG_FILE_LOCATION_V16);
        const std::string config_file((std::istreambuf_iterator<char>(ifs)), (std::istreambuf_iterator<char>()));
        this->configuration = std::make_unique<ChargePointConfiguration>(config_file, CONFIG_DIR_V16, user_config);

        ON_CALL(*this->connectivity_manager, is_websocket_connected()).WillByDefault(::testing::Return(true));
        ON_CALL(*this->connectivity_manager, set_message_callback(::testing::_))
            .WillByDefault(::testing::Invoke(
                [this](const std::function<void(const std::string&)>& callback) { this->to_charge_point = callback; }));
        ON_CALL(*this->connectivity_manager, send_to_websocket(::testing::_))
            .WillByDefault(
                ::testing::Invoke([this](const std::string& message) { return this->record_and_answer(message); }));

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
            const auto payload = this->response_payload_for_locked(action);
            if (payload.is_null()) {
                this->pending_responses.push_back(
                    nlohmann::json::array({4, unique_id, "InternalError", "", nlohmann::json::object()}));
            } else {
                this->pending_responses.push_back(nlohmann::json::array({3, unique_id, payload}));
            }
        }
        this->cv.notify_all();
        return true;
    }

    virtual nlohmann::json response_payload_for_locked(const std::string& action) {
        if (action == "BootNotification") {
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

    /// \brief Wait until every call sent so far has been answered and the answer was processed by the charge point.
    bool wait_for_all_answered() {
        std::unique_lock<std::mutex> lock(this->mtx);
        return this->cv.wait_for(lock, WAIT_TIMEOUT, [this]() { return this->delivered >= this->sent.size(); });
    }

    /// \brief Wait until at least \p count calls with \p action have been sent by the charge point.
    bool wait_for_action_count(const std::string& action, std::size_t count, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(this->mtx);
        return this->cv.wait_for(lock, timeout,
                                 [this, &action, count]() { return this->count_action_locked(action) >= count; });
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

    /// \brief The payload of the most recent call with \p action , if any was sent.
    std::optional<nlohmann::json> last_payload(const std::string& action) {
        const std::lock_guard<std::mutex> lock(this->mtx);
        for (auto it = this->sent.rbegin(); it != this->sent.rend(); ++it) {
            if (it->at(2) == action) {
                return it->at(3);
            }
        }
        return std::nullopt;
    }

    std::shared_ptr<::testing::NiceMock<EvseSecurityMock>> evse_security;
    std::shared_ptr<::testing::NiceMock<ConnectivityManagerMock>> connectivity_manager;
    std::unique_ptr<ChargePointConfiguration> configuration;
    fs::path tmp_dir;

    std::mutex mtx;
    std::condition_variable cv;
    std::vector<nlohmann::json> sent;
    std::deque<nlohmann::json> pending_responses;
    std::vector<std::string> escaped_exceptions;
    std::size_t delivered{0};
    std::function<void(const std::string&)> to_charge_point;
    std::thread responder;
    bool stop_responder{false};
};

} // namespace ocpp::v16
