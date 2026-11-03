// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

/// \brief ChangeConfiguration.req: ReadOnly keys are rejected but writable by local callers, AuthEnabled is applied.

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
#include <utility>
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
const std::string SUPPORTED_FEATURE_PROFILES_WITH_PNC =
    "Core,FirmwareManagement,RemoteTrigger,Reservation,LocalAuthListManagement,SmartCharging,PnC";
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

    /// \brief Starts a transaction for \p id_tag, which the CSMS answers with Invalid, and waits until its
    /// StartTransaction.conf is handled
    void start_transaction_rejected_by_csms(const std::string& id_tag = "FREESERVICE") {
        this->charge_point->register_pause_charging_callback([this](std::int32_t) {
            const std::lock_guard<std::mutex> lock(this->mtx);
            this->charging_paused = true;
            return true;
        });
        this->charge_point->register_stop_transaction_callback([this](std::int32_t, Reason) {
            const std::lock_guard<std::mutex> lock(this->mtx);
            this->transaction_stopped = true;
            return true;
        });
        this->charge_point->register_transaction_updated_callback(
            [this](const std::string&, const StartTransactionRequest&, const StartTransactionResponse&) {
                const std::lock_guard<std::mutex> lock(this->mtx);
                this->start_transaction_handled = true;
                this->cv.notify_all();
            });
        this->charge_point->on_session_started(1, "session-1", SessionStartedReason::EVConnected, std::nullopt);
        this->charge_point->on_transaction_started(1, "session-1", id_tag, 0, std::nullopt, ocpp::DateTime(),
                                                   std::nullopt);
        std::unique_lock<std::mutex> lock(this->mtx);
        EXPECT_TRUE(this->cv.wait_for(lock, WAIT_TIMEOUT, [this]() { return this->start_transaction_handled; }));
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
            if (frame.at(0) == 2) {
                this->sent_actions.push_back(frame.at(2).get<std::string>());
            }
            if (frame.at(0) == 3) {
                this->call_results.push_back(frame);
            } else if (frame.at(0) == 2 and frame.at(2) == "BootNotification") {
                this->incoming.push_back(nlohmann::json::array(
                    {3,
                     frame.at(1),
                     {{"currentTime", ocpp::DateTime().to_rfc3339()}, {"interval", 86400}, {"status", "Accepted"}}}));
            } else if (frame.at(0) == 2 and frame.at(2) == "StartTransaction") {
                this->incoming.push_back(nlohmann::json::array(
                    {3, frame.at(1), {{"idTagInfo", {{"status", "Invalid"}}}, {"transactionId", 1}}}));
            } else if (frame.at(0) == 2 and frame.at(2) == "Authorize") {
                this->incoming.push_back(
                    nlohmann::json::array({3, frame.at(1), {{"idTagInfo", {{"status", "Accepted"}}}}}));
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
    std::vector<std::string> sent_actions;
    bool charging_paused{false};
    bool transaction_stopped{false};
    bool start_transaction_handled{false};
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
        ocpp::v2::Ocpp16KeyMappings mappings;
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

TEST_F(ChargePointChangeConfigurationDeviceModelTest, CsmsCanChangeAuthEnabled) {
    this->device_model->set("Internal", "AuthEnabled", "true");
    make_booted_charge_point();
    ASSERT_TRUE(this->configuration->getAuthEnabled());

    EXPECT_EQ(change_configuration("AuthEnabled", "false"), "Accepted");
    EXPECT_FALSE(this->configuration->getAuthEnabled());
    EXPECT_EQ(this->device_model->get("AuthCtrlr", "Enabled"), "false");
}

TEST_F(ChargePointChangeConfigurationDeviceModelTest, AuthDisabledAcceptsIdTokenWithoutAuthorize) {
    this->device_model->set("Internal", "AuthEnabled", "false");
    make_booted_charge_point();

    EXPECT_EQ(this->charge_point->authorize_id_token("UNKNOWN").id_tag_info.status, AuthorizationStatus::Accepted);
    const std::lock_guard<std::mutex> lock(this->mtx);
    EXPECT_THAT(this->sent_actions, ::testing::Not(::testing::Contains("Authorize")));
}

namespace {
// a device-model variable that exists (metadata, mutability) but has no value until it is first set
class ValuelessCustomCvStorage : public stubs::MemoryStorageProxy {
public:
    ValuelessCustomCvStorage(stubs::MemoryStorage& storage, ocpp::v2::Component component,
                             ocpp::v2::Variable variable) :
        stubs::MemoryStorageProxy(storage),
        storage(storage),
        component(std::move(component)),
        variable(std::move(variable)) {
    }

    SetVariableStatusEnum set_value(const Component& component_id, const Variable& variable_id,
                                    const AttributeEnum& attribute_enum, const std::string& value,
                                    const std::string& source, bool allow_read_only) override {
        if (is_valueless_cv(component_id, variable_id)) {
            storage.set(component_id.name.get(), variable_id.name.get(), value);
            return SetVariableStatusEnum::Accepted;
        }
        return stubs::MemoryStorageProxy::set_value(component_id, variable_id, attribute_enum, value, source,
                                                    allow_read_only);
    }

    std::optional<MutabilityEnum> get_mutability(const Component& component_id, const Variable& variable_id,
                                                 const AttributeEnum& attribute_enum) override {
        if (is_valueless_cv(component_id, variable_id)) {
            return MutabilityEnum::ReadWrite;
        }
        return stubs::MemoryStorageProxy::get_mutability(component_id, variable_id, attribute_enum);
    }

    std::optional<VariableMetaData> get_variable_meta_data(const Component& component_id,
                                                           const Variable& variable_id) override {
        if (is_valueless_cv(component_id, variable_id)) {
            return VariableMetaData{};
        }
        return stubs::MemoryStorageProxy::get_variable_meta_data(component_id, variable_id);
    }

private:
    bool is_valueless_cv(const Component& component_id, const Variable& variable_id) const {
        return component_id.name == component.name && variable_id.name == variable.name;
    }

    stubs::MemoryStorage& storage;
    ocpp::v2::Component component;
    ocpp::v2::Variable variable;
};
} // namespace

class ChargePointChangeConfigurationValuelessCustomKeyTest : public ChargePointChangeConfigurationTest {
protected:
    std::unique_ptr<ChargePointConfigurationInterface> create_configuration() override {
        this->device_model = std::make_unique<stubs::MemoryStorage>();
        const ocpp::v2::Component component{"SomeOtherCtrlr"};
        const ocpp::v2::Variable variable{"AValuelessVar"};
        ocpp::v2::Ocpp16KeyMappings mappings;
        mappings.emplace(VALUELESS_KEY, std::make_pair(component, variable));
        return std::make_unique<ChargePointConfigurationDeviceModel>(
            CONFIG_DIR_V16, std::make_unique<ValuelessCustomCvStorage>(*this->device_model, component, variable),
            std::move(mappings));
    }

    static constexpr auto VALUELESS_KEY = "AValuelessKey";
    std::unique_ptr<stubs::MemoryStorage> device_model;
};

TEST_F(ChargePointChangeConfigurationValuelessCustomKeyTest, KnownKeyIsReportedWithoutValue) {
    const auto kv = this->configuration->get(VALUELESS_KEY);
    ASSERT_TRUE(kv.has_value());
    EXPECT_FALSE(kv->readonly);
    EXPECT_FALSE(kv->value.has_value());

    const auto all = this->configuration->get_all_key_value();
    const auto it =
        std::find_if(all.begin(), all.end(), [](const KeyValue& entry) { return entry.key.get() == VALUELESS_KEY; });
    ASSERT_NE(it, all.end());
    EXPECT_FALSE(it->value.has_value());
}

TEST_F(ChargePointChangeConfigurationValuelessCustomKeyTest, CsmsCanSetKeyWithoutValue) {
    make_booted_charge_point();

    EXPECT_EQ(change_configuration(VALUELESS_KEY, "first"), "Accepted");
    EXPECT_EQ(this->configuration->get(VALUELESS_KEY)->value.value().get(), "first");
}

TEST_F(ChargePointChangeConfigurationValuelessCustomKeyTest, LocalCallerCanSetKeyWithoutValue) {
    make_charge_point();

    EXPECT_EQ(this->charge_point->set_configuration_key(VALUELESS_KEY, "first"), ConfigurationStatus::Accepted);
    EXPECT_EQ(this->configuration->get(VALUELESS_KEY)->value.value().get(), "first");
}

TEST_F(ChargePointChangeConfigurationDeviceModelTest, AuthEnabledSendsAuthorize) {
    this->device_model->set("Internal", "AuthEnabled", "true");
    make_booted_charge_point();

    EXPECT_EQ(this->charge_point->authorize_id_token("UNKNOWN").id_tag_info.status, AuthorizationStatus::Accepted);
    const std::lock_guard<std::mutex> lock(this->mtx);
    EXPECT_THAT(this->sent_actions, ::testing::Contains("Authorize"));
}

TEST_F(ChargePointChangeConfigurationDeviceModelTest, AuthDisabledAcceptsPncWithoutDataTransfer) {
    this->device_model->set("Internal", "AuthEnabled", "false");
    this->device_model->set("Core", "SupportedFeatureProfiles", SUPPORTED_FEATURE_PROFILES_WITH_PNC);
    make_booted_charge_point();

    const auto response = this->charge_point->data_transfer_pnc_authorize("EMAID", std::nullopt, std::nullopt);
    EXPECT_EQ(response.idTokenInfo.status, ocpp::v2::AuthorizationStatusEnum::Accepted);
    EXPECT_FALSE(response.certificateStatus.has_value());
    const std::lock_guard<std::mutex> lock(this->mtx);
    EXPECT_THAT(this->sent_actions, ::testing::Not(::testing::Contains("DataTransfer")));
}

TEST_F(ChargePointChangeConfigurationDeviceModelTest, AuthDisabledStillRejectsPncWhilePncIsDisabled) {
    this->device_model->set("Internal", "AuthEnabled", "false");
    this->device_model->set("Core", "SupportedFeatureProfiles", SUPPORTED_FEATURE_PROFILES_WITH_PNC);
    this->device_model->set("PnC", "ISO15118PnCEnabled", "false");
    make_booted_charge_point();

    EXPECT_NE(this->charge_point->data_transfer_pnc_authorize("EMAID", std::nullopt, std::nullopt).idTokenInfo.status,
              ocpp::v2::AuthorizationStatusEnum::Accepted);
}

TEST_F(ChargePointChangeConfigurationDeviceModelTest, InvalidIdTagStopsTransactionWhileAuthEnabled) {
    this->device_model->set("Internal", "AuthEnabled", "true");
    make_booted_charge_point();

    start_transaction_rejected_by_csms();
    const std::lock_guard<std::mutex> lock(this->mtx);
    EXPECT_TRUE(this->charging_paused);
    EXPECT_TRUE(this->transaction_stopped);
}

TEST_F(ChargePointChangeConfigurationDeviceModelTest, InvalidIdTagKeepsTransactionStartedWhileAuthDisabled) {
    this->device_model->set("Internal", "AuthEnabled", "false");
    make_booted_charge_point();

    start_transaction_rejected_by_csms();
    const std::lock_guard<std::mutex> lock(this->mtx);
    EXPECT_FALSE(this->charging_paused);
    EXPECT_FALSE(this->transaction_stopped);
}

TEST_F(ChargePointChangeConfigurationDeviceModelTest, InvalidIdTagStopsTransactionStartedAfterAuthReenabled) {
    this->device_model->set("Internal", "AuthEnabled", "false");
    make_booted_charge_point();
    EXPECT_EQ(this->charge_point->authorize_id_token("UNKNOWN").id_tag_info.status, AuthorizationStatus::Accepted);
    EXPECT_EQ(change_configuration("AuthEnabled", "true"), "Accepted");

    start_transaction_rejected_by_csms("UNKNOWN");
    const std::lock_guard<std::mutex> lock(this->mtx);
    EXPECT_TRUE(this->charging_paused);
    EXPECT_TRUE(this->transaction_stopped);
}

} // namespace v16
} // namespace ocpp
