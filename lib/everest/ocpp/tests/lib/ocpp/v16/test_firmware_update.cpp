// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

/// \file test_firmware_update.cpp
/// \brief Behavioural unit tests for the v16 ChargePointImpl firmware-update connector-disable logic in
/// on_firmware_update_status_notification.
///
/// These tests construct a ChargePointImpl through its injection constructor (mocked ConnectivityManager, no
/// security config, no message callback) and assert only the observable side effects of the connector-disable
/// path: which connectors the disable_evse_callback is invoked for, and how often the
/// all_connectors_unavailable_callback fires. They cover the explicit-opt-in InstallScheduled trigger, the
/// default-true pre-install (Downloaded) trigger, the single-fire guard, and the terminal-status guard reset.
///
/// Note on the plain OCPP 1.6 path (request_id == -1): the FirmwareStatus enum cannot represent InstallScheduled,
/// so the enum conversion in on_firmware_update_status_notification throws std::out_of_range and is swallowed.
/// Nothing is sent to the CSMS for that status, but the connector-disable side effects still run. These tests
/// therefore assert on the disable/unavailable callbacks only, never on an outgoing CSMS message for
/// InstallScheduled.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <ocpp/common/connectivity_manager.hpp>
#include <ocpp/common/types.hpp>
#include <ocpp/v16/charge_point_configuration.hpp>
#include <ocpp/v16/charge_point_impl.hpp>
#include <ocpp/v16/charge_point_state_machine.hpp>

#include "connectivity_manager_mock.hpp"
#include "evse_security_mock.hpp"

namespace fs = std::filesystem;

using ::testing::NiceMock;

namespace ocpp {
namespace v16 {

class ChargePointFirmwareUpdateTestBase : public ::testing::Test {
protected:
    void SetUp() override {
        this->evse_security = std::make_shared<NiceMock<EvseSecurityMock>>();
        this->connectivity_manager = std::make_shared<NiceMock<ConnectivityManagerMock>>();

        std::ifstream ifs(CONFIG_FILE_LOCATION_V16);
        const std::string config_file((std::istreambuf_iterator<char>(ifs)), (std::istreambuf_iterator<char>()));
        this->configuration =
            std::make_unique<ChargePointConfiguration>(config_file, CONFIG_DIR_V16, USER_CONFIG_FILE_LOCATION_V16);

        // Each test gets its own temporary directory so the on-disk sqlite db and message logs do not collide.
        // Tests within a gtest binary run sequentially, so a simple incrementing counter is unique.
        static int test_dir_counter = 0;
        this->tmp_dir =
            fs::temp_directory_path() / ("ocpp_v16_firmware_update_test_" + std::to_string(test_dir_counter++));
        fs::create_directories(this->tmp_dir);
    }

    void TearDown() override {
        if (this->charge_point != nullptr) {
            this->charge_point->stop();
        }
        std::error_code ec;
        fs::remove_all(this->tmp_dir, ec);
    }

    /// \brief Construct a ChargePointImpl wired to the mocked ConnectivityManager and start it
    ChargePointImpl& start_charge_point() {
        this->charge_point = std::make_unique<ChargePointImpl>(
            *this->configuration, /*share_path=*/fs::path(CONFIG_DIR_V16), /*database_path=*/this->tmp_dir,
            /*sql_init_path=*/fs::path(MIGRATION_FILES_LOCATION_V16), /*message_log_path=*/this->tmp_dir,
            this->evse_security, this->connectivity_manager, /*security_configuration=*/std::nullopt,
            /*message_callback=*/nullptr);
        register_callbacks(*this->charge_point);
        this->charge_point->start({}, BootReasonEnum::PowerUp, {});
        return *this->charge_point;
    }

    /// \brief Register the callbacks the connector-disable path fires:
    ///   * disable_evse_callback records the connector id it is invoked for (and returns true),
    ///   * all_connectors_unavailable_callback counts how often it has been called,
    ///   * enable_evse_callback is a safe no-op (touched by the terminal-status restore path).
    void register_callbacks(ChargePointImpl& cp) {
        cp.register_disable_evse_callback([this](std::int32_t connector) {
            this->disabled_connectors.push_back(connector);
            return true;
        });
        cp.register_all_connectors_unavailable_callback([this]() { ++this->all_connectors_unavailable_count; });
        cp.register_enable_evse_callback([](std::int32_t) { return true; });
    }

    /// \brief The connectors 1..N the disable path is expected to disable when none are in an active transaction.
    std::vector<std::int32_t> expected_idle_connectors() const {
        std::vector<std::int32_t> connectors;
        for (std::int32_t connector = 1; connector <= this->configuration->getNumberOfConnectors(); ++connector) {
            connectors.push_back(connector);
        }
        return connectors;
    }

    std::shared_ptr<NiceMock<EvseSecurityMock>> evse_security;
    std::shared_ptr<NiceMock<ConnectivityManagerMock>> connectivity_manager;
    std::unique_ptr<ChargePointConfiguration> configuration;
    std::unique_ptr<ChargePointImpl> charge_point;
    fs::path tmp_dir;

    std::vector<std::int32_t> disabled_connectors;
    int all_connectors_unavailable_count{0};
};

using ChargePointFirmwareUpdateTest = ChargePointFirmwareUpdateTestBase;

// Test that InstallScheduled with disable_connectors_during_install = true disables all connectors and triggers
// all_connectors_unavailable_callback exactly once
TEST_F(ChargePointFirmwareUpdateTest, InstallScheduledOptInDisablesConnectorsSingleFire) {
    auto& charge_point = start_charge_point();

    charge_point.on_firmware_update_status_notification(-1, FirmwareStatusNotification::InstallScheduled,
                                                        std::optional<bool>{true});

    EXPECT_EQ(this->disabled_connectors, expected_idle_connectors());
    EXPECT_EQ(this->all_connectors_unavailable_count, 1);

    // Re-send the same trigger: the disable callback may run again, but the single-fire guard keeps the
    // all_connectors_unavailable_callback pinned at one invocation until a terminal status resets it.
    charge_point.on_firmware_update_status_notification(-1, FirmwareStatusNotification::InstallScheduled,
                                                        std::optional<bool>{true});

    EXPECT_EQ(this->all_connectors_unavailable_count, 1);
}

// Test that InstallScheduled without disable_connectors_during_install set does not disable connnectors and does not
// trigger all_connectors_unavailable_callback
class InstallScheduledNoOptInTest : public ChargePointFirmwareUpdateTestBase,
                                    public ::testing::WithParamInterface<std::optional<bool>> {};

TEST_P(InstallScheduledNoOptInTest, ForwardOnlyNoConnectorDisable) {
    auto& charge_point = start_charge_point();

    charge_point.on_firmware_update_status_notification(-1, FirmwareStatusNotification::InstallScheduled, GetParam());

    EXPECT_TRUE(this->disabled_connectors.empty());
    EXPECT_EQ(this->all_connectors_unavailable_count, 0);
}

INSTANTIATE_TEST_SUITE_P(InstallScheduledNoOptIn, InstallScheduledNoOptInTest,
                         ::testing::Values(std::optional<bool>{std::nullopt}, std::optional<bool>{false}));

// Test that Downloaded without disable_connectors_during_install set (nullopt) still disables the connectors and
// triggers all_connectors_unavailable_callback
TEST_F(ChargePointFirmwareUpdateTest, DownloadedDefaultsToDisablingConnectors) {
    auto& charge_point = start_charge_point();

    charge_point.on_firmware_update_status_notification(-1, FirmwareStatusNotification::Downloaded, std::nullopt);

    EXPECT_EQ(this->disabled_connectors, expected_idle_connectors());
    EXPECT_EQ(this->all_connectors_unavailable_count, 1);
}

// Test that Downloaded with disable_connectors_during_install = false does not disable the connectors and does not
// trigger all_connectors_unavailable_callback
TEST_F(ChargePointFirmwareUpdateTest, DownloadedWithFalseDoesNotDisableConnectors) {
    auto& charge_point = start_charge_point();

    charge_point.on_firmware_update_status_notification(-1, FirmwareStatusNotification::Downloaded,
                                                        std::optional<bool>{false});

    EXPECT_TRUE(this->disabled_connectors.empty());
    EXPECT_EQ(this->all_connectors_unavailable_count, 0);
}

// Test that a terminal status resets the guard around all_connectors_unavailable_callback and allows it to fire again
TEST_F(ChargePointFirmwareUpdateTest, TerminalStatusResetsSingleFireGuard) {
    auto& charge_point = start_charge_point();

    charge_point.on_firmware_update_status_notification(-1, FirmwareStatusNotification::InstallScheduled,
                                                        std::optional<bool>{true});
    EXPECT_EQ(this->all_connectors_unavailable_count, 1);

    // Terminal status: clears firmware_update_is_pending and, crucially, all_connectors_unavailable_notified.
    charge_point.on_firmware_update_status_notification(-1, FirmwareStatusNotification::InstallationFailed,
                                                        std::nullopt);
    EXPECT_EQ(this->all_connectors_unavailable_count, 1);

    // A new opt-in trigger fires the unavailable callback again now that the guard has been reset.
    charge_point.on_firmware_update_status_notification(-1, FirmwareStatusNotification::InstallScheduled,
                                                        std::optional<bool>{true});
    EXPECT_EQ(this->all_connectors_unavailable_count, 2);
}

} // namespace v16
} // namespace ocpp
