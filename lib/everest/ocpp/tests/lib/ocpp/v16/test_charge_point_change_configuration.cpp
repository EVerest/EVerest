// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

/// \file test_charge_point_change_configuration.cpp
/// \brief ReadOnly configuration keys: rejected for a ChangeConfiguration.req, writable by local callers.

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
#include <ocpp/v16/charge_point_configuration_devicemodel.hpp>
#include <ocpp/v16/charge_point_impl.hpp>

#include "connectivity_manager_mock.hpp"
#include "evse_security_mock.hpp"
#include "v2config/memory_storage.hpp"

namespace fs = std::filesystem;

using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;

namespace ocpp {
namespace v16 {

namespace {
constexpr auto WAIT_TIMEOUT = std::chrono::seconds(10);
const std::string READ_ONLY_KEY = "CentralSystemURI";
const std::string NEW_URI = "ws://localhost:9000";
} // namespace

class ChargePointChangeConfigurationTest : public ::testing::Test {
protected:
    void SetUp() override {
        this->evse_security = std::make_shared<NiceMock<EvseSecurityMock>>();
        this->connectivity_manager = std::make_shared<NiceMock<ConnectivityManagerMock>>();
        this->tmp_dir = libocpp_test::unique_temp_directory("ocpp_v16_change_configuration_test");
        this->configuration = create_configuration();

        ON_CALL(*this->connectivity_manager, is_websocket_connected()).WillByDefault(Return(true));
        ON_CALL(*this->connectivity_manager, set_message_callback(_))
            .WillByDefault(Invoke(
                [this](const std::function<void(const std::string&)>& callback) { this->to_charge_point = callback; }));
        ON_CALL(*this->connectivity_manager, send_to_websocket(_))
            .WillByDefault(Invoke([this](const std::string& message) { return this->record(message); }));

        this->responder = std::thread([this]() { this->run_responder(); });
    }

    void TearDown() override {
        {
            const std::lock_guard<std::mutex> lock(this->mtx);
            this->stop_responder = true;
        }
        this->cv.notify_all();
        this->responder.join();
        if (this->started) {
            this->charge_point->stop();
        }
        std::error_code ec;
        fs::remove_all(this->tmp_dir, ec);
    }

    virtual std::unique_ptr<ChargePointConfigurationInterface> create_configuration() {
        const auto user_config = this->tmp_dir / "user_config.json";
        std::ofstream(user_config) << "{}";
        std::ifstream ifs(CONFIG_FILE_LOCATION_V16);
        const std::string config_file((std::istreambuf_iterator<char>(ifs)), (std::istreambuf_iterator<char>()));
        return std::make_unique<ChargePointConfiguration>(config_file, CONFIG_DIR_V16, user_config);
    }

    void make_charge_point() {
        this->charge_point = std::make_unique<ChargePointImpl>(
            *this->configuration, /*share_path=*/fs::path(CONFIG_DIR_V16), /*database_path=*/this->tmp_dir,
            /*sql_init_path=*/fs::path(MIGRATION_FILES_LOCATION_V16), /*message_log_path=*/this->tmp_dir,
            this->evse_security, this->connectivity_manager, /*security_configuration=*/std::nullopt,
            /*message_callback=*/nullptr);
    }

    /// \brief Start, connect and wait until the BootNotification.req was accepted
    void make_booted_charge_point() {
        make_charge_point();
        this->charge_point->start({{0, ChargePointStatus::Available}, {1, ChargePointStatus::Available}},
                                  BootReasonEnum::PowerUp, {});
        this->started = true;
        this->charge_point->on_websocket_connected(0, ocpp::v2::NetworkConnectionProfile{},
                                                   ocpp::OcppProtocolVersion::v16);
        std::unique_lock<std::mutex> lock(this->mtx);
        this->websocket_connected = true;
        this->cv.notify_all();
        EXPECT_TRUE(this->cv.wait_for(lock, WAIT_TIMEOUT, [this]() { return this->boot_accepted; }));
    }

    /// \brief Send a ChangeConfiguration.req as the CSMS and return the status of its response
    std::optional<std::string> change_configuration(const std::string& key, const std::string& value) {
        const std::string unique_id = "change-configuration-1";
        std::unique_lock<std::mutex> lock(this->mtx);
        this->incoming.push_back(
            nlohmann::json::array({2, unique_id, "ChangeConfiguration", {{"key", key}, {"value", value}}}));
        this->cv.notify_all();
        std::optional<std::string> status;
        this->cv.wait_for(lock, WAIT_TIMEOUT, [this, &unique_id, &status]() {
            for (const auto& frame : this->call_results) {
                if (frame.at(1) == unique_id) {
                    status = frame.at(2).at("status").get<std::string>();
                    return true;
                }
            }
            return false;
        });
        return status;
    }

    bool record(const std::string& message) {
        const auto frame = nlohmann::json::parse(message, nullptr, false);
        if (frame.is_discarded() or not frame.is_array() or frame.size() < 3) {
            return true;
        }
        {
            const std::lock_guard<std::mutex> lock(this->mtx);
            if (frame.at(0) == 3) {
                this->call_results.push_back(frame);
            } else if (frame.at(0) == 2 and frame.at(2) == "BootNotification") {
                this->incoming.push_back(nlohmann::json::array(
                    {3,
                     frame.at(1),
                     {{"currentTime", ocpp::DateTime().to_rfc3339()}, {"interval", 86400}, {"status", "Accepted"}}}));
            } else if (frame.at(0) == 2) {
                this->incoming.push_back(nlohmann::json::array({3, frame.at(1), nlohmann::json::object()}));
            }
        }
        this->cv.notify_all();
        return true;
    }

    /// \brief Delivers incoming frames one at a time, like the websocket, and off the message-queue worker thread
    /// \note Frames are held back until on_websocket_connected(), because the charge point drops earlier messages
    void run_responder() {
        while (true) {
            nlohmann::json frame;
            {
                std::unique_lock<std::mutex> lock(this->mtx);
                this->cv.wait(lock, [this]() {
                    return this->stop_responder or (this->websocket_connected and not this->incoming.empty());
                });
                if (this->stop_responder) {
                    return;
                }
                frame = this->incoming.front();
                this->incoming.pop_front();
            }
            const bool is_boot_frame = frame.at(0) == 3 and frame.at(2).contains("interval");
            this->to_charge_point(frame.dump());
            if (is_boot_frame) {
                const std::lock_guard<std::mutex> lock(this->mtx);
                this->boot_accepted = true;
            }
            this->cv.notify_all();
        }
    }

    std::shared_ptr<NiceMock<EvseSecurityMock>> evse_security;
    std::shared_ptr<NiceMock<ConnectivityManagerMock>> connectivity_manager;
    std::unique_ptr<ChargePointConfigurationInterface> configuration;
    std::unique_ptr<ChargePointImpl> charge_point;
    bool started{false};
    fs::path tmp_dir;
    std::function<void(const std::string&)> to_charge_point;

    std::mutex mtx;
    std::condition_variable cv;
    std::deque<nlohmann::json> incoming;
    std::vector<nlohmann::json> call_results;
    bool websocket_connected{false};
    bool boot_accepted{false};
    bool stop_responder{false};
    std::thread responder;
};

TEST_F(ChargePointChangeConfigurationTest, CsmsCannotChangeReadOnlyKey) {
    make_booted_charge_point();
    ASSERT_TRUE(this->configuration->get(READ_ONLY_KEY)->readonly);
    const auto uri_before = this->configuration->getCentralSystemURI();

    EXPECT_EQ(change_configuration(READ_ONLY_KEY, NEW_URI), "Rejected");
    EXPECT_EQ(this->configuration->getCentralSystemURI(), uri_before);
}

TEST_F(ChargePointChangeConfigurationTest, CsmsCanChangeWritableKey) {
    make_booted_charge_point();

    EXPECT_EQ(change_configuration("HeartbeatInterval", "120"), "Accepted");
    EXPECT_EQ(this->configuration->getHeartbeatInterval(), 120);
}

TEST_F(ChargePointChangeConfigurationTest, LocalCallerCanChangeReadOnlyKey) {
    make_charge_point();
    ASSERT_TRUE(this->configuration->get(READ_ONLY_KEY)->readonly);

    EXPECT_EQ(this->charge_point->set_configuration_key(READ_ONLY_KEY, NEW_URI), ConfigurationStatus::RebootRequired);
    EXPECT_EQ(this->configuration->getCentralSystemURI(), NEW_URI);
}

class ChargePointChangeConfigurationDeviceModelTest : public ChargePointChangeConfigurationTest {
protected:
    std::unique_ptr<ChargePointConfigurationInterface> create_configuration() override {
        this->device_model = std::make_unique<stubs::MemoryStorage>();
        this->device_model->set("SomeOtherCtrlr", "ACustomKeyVar", "initial");
        this->device_model->set_readonly("ACustomKeyVar");
        ocpp::v2::Ocpp16CustomConfigMappings mappings;
        mappings.emplace(CUSTOM_KEY,
                         std::make_pair(ocpp::v2::Component{"SomeOtherCtrlr"}, ocpp::v2::Variable{"ACustomKeyVar"}));
        return std::make_unique<ChargePointConfigurationDeviceModel>(
            CONFIG_DIR_V16, std::make_unique<stubs::MemoryStorageProxy>(*this->device_model), std::move(mappings));
    }

    static constexpr auto CUSTOM_KEY = "ACustomKey";
    std::unique_ptr<stubs::MemoryStorage> device_model;
};

TEST_F(ChargePointChangeConfigurationDeviceModelTest, CsmsCannotChangeReadOnlyKey) {
    make_booted_charge_point();
    ASSERT_TRUE(this->configuration->get("ChargePointModel")->readonly);

    EXPECT_EQ(change_configuration("ChargePointModel", "Other"), "Rejected");
    EXPECT_EQ(this->configuration->getChargePointModel().get(), "Yeti");
}

TEST_F(ChargePointChangeConfigurationDeviceModelTest, CsmsCannotChangeReadOnlyCustomKey) {
    make_booted_charge_point();
    ASSERT_TRUE(this->configuration->get(CUSTOM_KEY)->readonly);

    EXPECT_EQ(change_configuration(CUSTOM_KEY, "changed"), "Rejected");
    EXPECT_EQ(this->configuration->get(CUSTOM_KEY)->value.value().get(), "initial");
}

TEST_F(ChargePointChangeConfigurationDeviceModelTest, CsmsCanChangeWritableKey) {
    make_booted_charge_point();

    EXPECT_EQ(change_configuration("HeartbeatInterval", "120"), "Accepted");
    EXPECT_EQ(this->configuration->getHeartbeatInterval(), 120);
}

TEST_F(ChargePointChangeConfigurationDeviceModelTest, LocalCallerCanChangeReadOnlyKey) {
    make_charge_point();
    ASSERT_TRUE(this->configuration->get("ChargePointModel")->readonly);

    EXPECT_EQ(this->charge_point->set_configuration_key("ChargePointModel", "Other"), ConfigurationStatus::Accepted);
    EXPECT_EQ(this->configuration->getChargePointModel().get(), "Other");
}

} // namespace v16
} // namespace ocpp
