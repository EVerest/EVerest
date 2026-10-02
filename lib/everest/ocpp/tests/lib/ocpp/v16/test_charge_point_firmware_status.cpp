// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

/// \file test_charge_point_firmware_status.cpp
/// \brief Tests of firmware status reporting in ChargePointImpl.

#include <atomic>

#include "charge_point_test_base.hpp"

namespace ocpp {
namespace v16 {

namespace {
constexpr std::int32_t CONNECTOR = 1;
} // namespace

class ChargePointFirmwareStatusTest : public ChargePointTestBase, public ::testing::WithParamInterface<int> {
protected:
    bool boot_pending = false;

    nlohmann::json response_payload_for_locked(const std::string& action) override {
        auto response = ChargePointTestBase::response_payload_for_locked(action);
        if (action == "BootNotification" and boot_pending) {
            response["status"] = "Pending";
        }
        return response;
    }

    void trigger_firmware_status() {
        const auto trigger = GetParam() == -1 ? "TriggerMessage" : "ExtendedTriggerMessage";
        to_charge_point(
            nlohmann::json::array({2, "trigger-status", trigger, {{"requestedMessage", "FirmwareStatusNotification"}}})
                .dump());
    }

    /// \brief Stop the charge point once every answer has been processed. The websocket joins its receive thread
    /// before disconnect() returns, so no message callback runs during teardown; the responder thread has no such
    /// coupling and has to be drained explicitly.
    void stop_charge_point(ChargePointImpl& charge_point) {
        EXPECT_TRUE(wait_for_all_answered()) << "not every answer was processed before stopping";
        EXPECT_THAT(escaped_exceptions, ::testing::IsEmpty());
        charge_point.stop();
    }
};

TEST_P(ChargePointFirmwareStatusTest, LatestFirmwareStatusWaitsForBoot) {
    const auto request_id = GetParam();
    const auto action = request_id == -1 ? "FirmwareStatusNotification" : "SignedFirmwareStatusNotification";
    auto charge_point = make_charge_point();
    charge_point->init({{0, ChargePointStatus::Available}, {CONNECTOR, ChargePointStatus::Available}}, {});
    charge_point->on_firmware_update_status_notification(request_id, FirmwareStatusNotification::Installing);
    charge_point->on_firmware_update_status_notification(request_id, FirmwareStatusNotification::Installed);
    EXPECT_EQ(count_action(action), 0);

    charge_point->start({}, BootReasonEnum::FirmwareUpdate, {});
    charge_point->on_websocket_connected(0, ocpp::v2::NetworkConnectionProfile{}, ocpp::OcppProtocolVersion::v16);
    EXPECT_TRUE(wait_for_action_count(action, 1, WAIT_TIMEOUT));
    const auto installed = last_payload(action);
    if (installed.has_value()) {
        EXPECT_EQ(installed->at("status"), "Installed");
        if (request_id != -1) {
            EXPECT_EQ(installed->at("requestId"), request_id);
        } else {
            EXPECT_FALSE(installed->contains("requestId"));
        }
    }
    EXPECT_EQ(count_action(action), 1);
    EXPECT_TRUE(wait_for_all_answered());
    trigger_firmware_status();
    EXPECT_TRUE(wait_for_action_count(action, 2, WAIT_TIMEOUT));
    EXPECT_EQ(last_payload(action)->at("status"), "Idle");
    EXPECT_TRUE(wait_for_all_answered());

    charge_point->on_firmware_update_status_notification(request_id, FirmwareStatusNotification::Downloading);
    EXPECT_TRUE(wait_for_action_count(action, 3, WAIT_TIMEOUT));
    EXPECT_EQ(last_payload(action)->at("status"), "Downloading");
    EXPECT_TRUE(wait_for_all_answered());
    charge_point->on_firmware_update_status_notification(request_id, FirmwareStatusNotification::Installed);
    EXPECT_TRUE(wait_for_action_count(action, 4, WAIT_TIMEOUT));
    EXPECT_EQ(last_payload(action)->at("status"), "Installed");
    EXPECT_TRUE(wait_for_all_answered());
    trigger_firmware_status();
    EXPECT_TRUE(wait_for_action_count(action, 5, WAIT_TIMEOUT));
    EXPECT_EQ(last_payload(action)->at("status"), "Idle");
    stop_charge_point(*charge_point);
}

TEST_P(ChargePointFirmwareStatusTest, TriggerDeliversHeldStatusDuringPendingOnce) {
    const auto request_id = GetParam();
    const auto action = request_id == -1 ? "FirmwareStatusNotification" : "SignedFirmwareStatusNotification";
    {
        const std::lock_guard<std::mutex> lock(mtx);
        boot_pending = true;
    }
    auto charge_point = make_charge_point();
    charge_point->init({{0, ChargePointStatus::Available}, {CONNECTOR, ChargePointStatus::Available}}, {});
    charge_point->start({}, BootReasonEnum::FirmwareUpdate, {});
    charge_point->on_websocket_connected(0, ocpp::v2::NetworkConnectionProfile{}, ocpp::OcppProtocolVersion::v16);
    EXPECT_TRUE(wait_for_action_count("BootNotification", 1, WAIT_TIMEOUT));
    EXPECT_TRUE(wait_for_all_answered());
    charge_point->on_firmware_update_status_notification(request_id, FirmwareStatusNotification::Installed);
    EXPECT_EQ(count_action(action), 0);

    trigger_firmware_status();
    EXPECT_TRUE(wait_for_action_count(action, 1, WAIT_TIMEOUT));
    const auto installed = last_payload(action);
    if (installed.has_value()) {
        EXPECT_EQ(installed->at("status"), "Installed");
        if (request_id != -1) {
            EXPECT_EQ(installed->at("requestId"), request_id);
        } else {
            EXPECT_FALSE(installed->contains("requestId"));
        }
    }
    EXPECT_TRUE(wait_for_all_answered());
    {
        const std::lock_guard<std::mutex> lock(mtx);
        boot_pending = false;
    }
    to_charge_point(
        nlohmann::json::array({2, "trigger-boot", "TriggerMessage", {{"requestedMessage", "BootNotification"}}})
            .dump());
    EXPECT_TRUE(wait_for_action_count("BootNotification", 2, WAIT_TIMEOUT));
    EXPECT_TRUE(wait_for_all_answered());
    EXPECT_FALSE(wait_for_action_count(action, 2, std::chrono::milliseconds(300)));
    EXPECT_EQ(count_action(action), 1);

    trigger_firmware_status();
    EXPECT_TRUE(wait_for_action_count(action, 2, WAIT_TIMEOUT));
    EXPECT_EQ(last_payload(action)->at("status"), "Idle");
    stop_charge_point(*charge_point);
}

TEST_P(ChargePointFirmwareStatusTest, TriggerDeliversHeldStatusWithItsConnectorFlag) {
    const auto request_id = GetParam();
    const auto action = request_id == -1 ? "FirmwareStatusNotification" : "SignedFirmwareStatusNotification";
    const auto pending_install =
        request_id == -1 ? FirmwareStatusNotification::Downloaded : FirmwareStatusNotification::SignatureVerified;
    const auto pending_install_status = request_id == -1 ? "Downloaded" : "SignatureVerified";
    {
        const std::lock_guard<std::mutex> lock(mtx);
        boot_pending = true;
    }
    auto charge_point = make_charge_point();
    std::atomic<int> disabled_connectors{0};
    charge_point->register_disable_evse_callback([&](std::int32_t) {
        disabled_connectors += 1;
        return true;
    });
    charge_point->init({{0, ChargePointStatus::Available}, {CONNECTOR, ChargePointStatus::Available}}, {});
    charge_point->start({}, BootReasonEnum::PowerUp, {});
    charge_point->on_websocket_connected(0, ocpp::v2::NetworkConnectionProfile{}, ocpp::OcppProtocolVersion::v16);
    EXPECT_TRUE(wait_for_action_count("BootNotification", 1, WAIT_TIMEOUT));
    EXPECT_TRUE(wait_for_all_answered());
    charge_point->on_firmware_update_status_notification(request_id, pending_install, false);
    EXPECT_EQ(count_action(action), 0);

    trigger_firmware_status();
    EXPECT_TRUE(wait_for_action_count(action, 1, WAIT_TIMEOUT));
    EXPECT_EQ(last_payload(action)->at("status"), pending_install_status);
    EXPECT_TRUE(wait_for_all_answered());
    EXPECT_EQ(disabled_connectors, 0);
    stop_charge_point(*charge_point);
}

TEST_P(ChargePointFirmwareStatusTest, HeldTerminalStatusDoesNotResetNextUpdate) {
    const auto request_id = GetParam();
    const auto action = request_id == -1 ? "FirmwareStatusNotification" : "SignedFirmwareStatusNotification";
    auto charge_point = make_charge_point();
    charge_point->init({{0, ChargePointStatus::Available}, {CONNECTOR, ChargePointStatus::Available}}, {});
    bool resumed = false;
    // Finish registration and start another update before the terminal-status callback returns.
    charge_point->register_enable_evse_callback([&](std::int32_t) {
        if (!resumed) {
            resumed = true;
            charge_point->start({}, BootReasonEnum::FirmwareUpdate, {});
            charge_point->on_websocket_connected(0, ocpp::v2::NetworkConnectionProfile{},
                                                 ocpp::OcppProtocolVersion::v16);
            EXPECT_TRUE(wait_for_action_count(action, 1, WAIT_TIMEOUT));
            EXPECT_TRUE(wait_for_all_answered());
            charge_point->on_firmware_update_status_notification(request_id, FirmwareStatusNotification::Downloading);
            EXPECT_TRUE(wait_for_action_count(action, 2, WAIT_TIMEOUT));
            EXPECT_TRUE(wait_for_all_answered());
        }
        return true;
    });

    charge_point->on_firmware_update_status_notification(request_id, FirmwareStatusNotification::Installed);
    EXPECT_TRUE(resumed);
    trigger_firmware_status();
    EXPECT_TRUE(wait_for_action_count(action, 3, WAIT_TIMEOUT));
    EXPECT_EQ(last_payload(action)->at("status"), "Downloading");
    stop_charge_point(*charge_point);
}

INSTANTIATE_TEST_SUITE_P(SignedAndPlain, ChargePointFirmwareStatusTest, ::testing::Values(42, -1));

} // namespace v16
} // namespace ocpp
