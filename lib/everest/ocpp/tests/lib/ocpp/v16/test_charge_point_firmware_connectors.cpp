// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

/// \file test_charge_point_firmware_connectors.cpp
/// \brief Tests of connector availability during a firmware update in ChargePointImpl.

#include <atomic>

#include "charge_point_test_base.hpp"

namespace ocpp {
namespace v16 {

namespace {
constexpr std::int32_t CONNECTOR = 1;
constexpr std::int32_t TRANSACTION_ID = 42;
constexpr std::int32_t SIGNED_REQUEST_ID = 42;
constexpr std::int32_t PLAIN_REQUEST_ID = -1;
const std::string SESSION_ID = "session-1";
const std::string ID_TAG = "TAG1";
} // namespace

class ChargePointFirmwareConnectorsTestBase : public ChargePointTestBase {
protected:
    nlohmann::json response_payload_for_locked(const std::string& action) override {
        if (action == "StartTransaction") {
            return nlohmann::json{{"idTagInfo", {{"status", "Accepted"}}}, {"transactionId", TRANSACTION_ID}};
        }
        return ChargePointTestBase::response_payload_for_locked(action);
    }

    bool wait_for_status(const std::string& status) {
        std::unique_lock<std::mutex> lock(this->mtx);
        return this->cv.wait_for(lock, WAIT_TIMEOUT, [this, &status]() {
            for (const auto& call : this->sent) {
                if (call.at(2) == "StatusNotification" and call.at(3).at("connectorId") == CONNECTOR and
                    call.at(3).at("status") == status) {
                    return true;
                }
            }
            return false;
        });
    }

    /// \brief A charge point with an accepted BootNotification whose availability callbacks are counted from here on.
    std::unique_ptr<ChargePointImpl> make_booted_charge_point() {
        auto charge_point = make_charge_point();
        charge_point->register_enable_evse_callback([this](std::int32_t) {
            this->enabled_connectors += 1;
            return true;
        });
        charge_point->register_disable_evse_callback([this](std::int32_t) {
            this->disabled_connectors += 1;
            return true;
        });
        charge_point->register_all_connectors_unavailable_callback([this]() { this->install_allowed += 1; });
        charge_point->start({{0, ChargePointStatus::Available}, {CONNECTOR, ChargePointStatus::Available}},
                            BootReasonEnum::PowerUp, {});
        charge_point->on_websocket_connected(0, ocpp::v2::NetworkConnectionProfile{}, ocpp::OcppProtocolVersion::v16);
        EXPECT_TRUE(wait_for_status("Available"));
        EXPECT_TRUE(wait_for_all_answered());
        this->enabled_connectors = 0;
        this->disabled_connectors = 0;
        this->install_allowed = 0;
        return charge_point;
    }

    void start_transaction(ChargePointImpl& charge_point) {
        charge_point.on_session_started(CONNECTOR, SESSION_ID, ocpp::SessionStartedReason::EVConnected, std::nullopt);
        charge_point.on_transaction_started(CONNECTOR, SESSION_ID, ID_TAG, /*meter_start=*/0.0,
                                            /*reservation_id=*/std::nullopt, ocpp::DateTime(),
                                            /*signed_meter_value=*/std::nullopt);
        EXPECT_TRUE(wait_for_action_count("StartTransaction", 1, WAIT_TIMEOUT));
        EXPECT_TRUE(wait_for_all_answered());
    }

    void stop_transaction(ChargePointImpl& charge_point) {
        charge_point.on_transaction_stopped(CONNECTOR, SESSION_ID, Reason::Local, ocpp::DateTime(),
                                            /*energy_wh_import=*/1000.0F, /*id_tag_end=*/std::nullopt,
                                            /*signed_meter_value=*/std::nullopt,
                                            /*start_signed_meter_value=*/std::nullopt);
    }

    void send_signed_update_firmware() {
        to_charge_point(nlohmann::json::array({2,
                                               "signed-update",
                                               "SignedUpdateFirmware",
                                               {{"requestId", SIGNED_REQUEST_ID},
                                                {"firmware",
                                                 {{"location", "https://firmware.example.com/firmware.bin"},
                                                  {"retrieveDateTime", ocpp::DateTime().to_rfc3339()},
                                                  {"signingCertificate", "certificate"},
                                                  {"signature", "signature"}}}}})
                            .dump());
        const auto answer = wait_for_answer("signed-update");
        ASSERT_TRUE(answer.has_value());
        ASSERT_EQ(answer->at(2).at("status"), "Accepted");
    }

    static FirmwareStatusNotification install_pending_status(const std::int32_t request_id) {
        return request_id == PLAIN_REQUEST_ID ? FirmwareStatusNotification::Downloaded
                                              : FirmwareStatusNotification::SignatureVerified;
    }

    std::atomic<int> enabled_connectors{0};
    std::atomic<int> disabled_connectors{0};
    std::atomic<int> install_allowed{0};
};

class ChargePointFirmwareConnectorsTest : public ChargePointFirmwareConnectorsTestBase,
                                          public ::testing::WithParamInterface<int> {};

class ChargePointSignedFirmwareConnectorsTest : public ChargePointFirmwareConnectorsTestBase {};

TEST_P(ChargePointFirmwareConnectorsTest, OfflineSessionEndAllowsInstallation) {
    const auto request_id = GetParam();
    auto charge_point = make_booted_charge_point();
    start_transaction(*charge_point);

    charge_point->on_websocket_disconnected(0, ocpp::v2::NetworkConnectionProfile{});
    charge_point->on_firmware_update_status_notification(request_id, install_pending_status(request_id));
    EXPECT_EQ(disabled_connectors, 0);
    EXPECT_EQ(install_allowed, 0);

    stop_transaction(*charge_point);
    charge_point->on_session_stopped(CONNECTOR, SESSION_ID);

    EXPECT_EQ(count_action("StopTransaction"), 0u) << "the StopTransaction.req left the station while offline";
    EXPECT_EQ(disabled_connectors, 1);
    EXPECT_EQ(install_allowed, 1) << "the installation is not allowed although no transaction is left";
    charge_point->stop();
}

TEST_P(ChargePointFirmwareConnectorsTest, CsmsScheduledInoperativeSurvivesFirmwareCycle) {
    const auto request_id = GetParam();
    auto charge_point = make_booted_charge_point();
    start_transaction(*charge_point);

    to_charge_point(
        nlohmann::json::array(
            {2, "change-availability", "ChangeAvailability", {{"connectorId", CONNECTOR}, {"type", "Inoperative"}}})
            .dump());
    const auto answer = wait_for_answer("change-availability");
    ASSERT_TRUE(answer.has_value());
    ASSERT_EQ(answer->at(2).at("status"), "Scheduled");

    charge_point->on_firmware_update_status_notification(request_id, install_pending_status(request_id));
    charge_point->on_firmware_update_status_notification(request_id, FirmwareStatusNotification::InstallationFailed);
    EXPECT_EQ(disabled_connectors, 0);

    stop_transaction(*charge_point);
    EXPECT_TRUE(wait_for_action_count("StopTransaction", 1, WAIT_TIMEOUT));
    EXPECT_TRUE(wait_for_all_answered());
    charge_point->on_session_stopped(CONNECTOR, SESSION_ID);

    EXPECT_EQ(disabled_connectors, 1) << "the ChangeAvailability.req answered with Scheduled was not executed";
    EXPECT_TRUE(wait_for_all_answered());
    charge_point->stop();
}

TEST_F(ChargePointSignedFirmwareConnectorsTest, CsmsScheduledInoperativeIsExecutedAfterTransaction) {
    auto charge_point = make_booted_charge_point();
    start_transaction(*charge_point);

    to_charge_point(
        nlohmann::json::array(
            {2, "change-availability", "ChangeAvailability", {{"connectorId", CONNECTOR}, {"type", "Inoperative"}}})
            .dump());
    const auto answer = wait_for_answer("change-availability");
    ASSERT_TRUE(answer.has_value());
    ASSERT_EQ(answer->at(2).at("status"), "Scheduled");

    stop_transaction(*charge_point);
    EXPECT_TRUE(wait_for_action_count("StopTransaction", 1, WAIT_TIMEOUT));
    EXPECT_TRUE(wait_for_all_answered());
    charge_point->on_session_stopped(CONNECTOR, SESSION_ID);

    EXPECT_EQ(disabled_connectors, 1);
    EXPECT_TRUE(wait_for_all_answered());
    charge_point->stop();
}

TEST_P(ChargePointFirmwareConnectorsTest, DownloadFailedDoesNotEnableUntouchedConnectors) {
    const auto request_id = GetParam();
    auto charge_point = make_booted_charge_point();

    charge_point->on_firmware_update_status_notification(request_id, FirmwareStatusNotification::Downloading);
    charge_point->on_firmware_update_status_notification(request_id, FirmwareStatusNotification::DownloadFailed);

    EXPECT_EQ(disabled_connectors, 0);
    EXPECT_EQ(enabled_connectors, 0) << "connectors that were never disabled were enabled";
    EXPECT_TRUE(wait_for_all_answered());
    charge_point->stop();
}

TEST_P(ChargePointFirmwareConnectorsTest, IdleRearmsInstallNotification) {
    const auto request_id = GetParam();
    auto charge_point = make_booted_charge_point();

    charge_point->on_firmware_update_status_notification(request_id, FirmwareStatusNotification::InstallScheduled,
                                                         true);
    EXPECT_EQ(install_allowed, 1);
    charge_point->on_firmware_update_status_notification(request_id, FirmwareStatusNotification::Idle);
    charge_point->on_firmware_update_status_notification(request_id, FirmwareStatusNotification::InstallScheduled,
                                                         true);
    EXPECT_EQ(install_allowed, 2);

    EXPECT_TRUE(wait_for_all_answered());
    charge_point->stop();
}

INSTANTIATE_TEST_SUITE_P(SignedAndPlain, ChargePointFirmwareConnectorsTest,
                         ::testing::Values(SIGNED_REQUEST_ID, PLAIN_REQUEST_ID));

TEST_F(ChargePointSignedFirmwareConnectorsTest, StatusPublishedFromSignedUpdateCallbackIsKept) {
    auto charge_point = make_booted_charge_point();
    charge_point->register_signed_update_firmware_callback([&charge_point](const SignedUpdateFirmwareRequest request) {
        charge_point->on_firmware_update_status_notification(request.requestId,
                                                             FirmwareStatusNotification::Downloading);
        return UpdateFirmwareStatusEnumType::Accepted;
    });

    send_signed_update_firmware();
    EXPECT_TRUE(wait_for_action_count("SignedFirmwareStatusNotification", 1, WAIT_TIMEOUT));
    EXPECT_TRUE(wait_for_all_answered());

    to_charge_point(
        nlohmann::json::array(
            {2, "trigger-status", "ExtendedTriggerMessage", {{"requestedMessage", "FirmwareStatusNotification"}}})
            .dump());
    ASSERT_TRUE(wait_for_action_count("SignedFirmwareStatusNotification", 2, WAIT_TIMEOUT));
    EXPECT_EQ(last_payload("SignedFirmwareStatusNotification")->at("status"), "Downloading");

    EXPECT_TRUE(wait_for_all_answered());
    charge_point->stop();
}

TEST_F(ChargePointSignedFirmwareConnectorsTest, InstallStateSetFromSignedUpdateCallbackIsKept) {
    auto charge_point = make_booted_charge_point();
    charge_point->register_signed_update_firmware_callback([&charge_point](const SignedUpdateFirmwareRequest request) {
        charge_point->on_firmware_update_status_notification(request.requestId,
                                                             FirmwareStatusNotification::SignatureVerified);
        return UpdateFirmwareStatusEnumType::Accepted;
    });
    start_transaction(*charge_point);

    send_signed_update_firmware();
    EXPECT_EQ(install_allowed, 0);

    stop_transaction(*charge_point);
    EXPECT_TRUE(wait_for_action_count("StopTransaction", 1, WAIT_TIMEOUT));
    EXPECT_TRUE(wait_for_all_answered());
    charge_point->on_session_stopped(CONNECTOR, SESSION_ID);

    EXPECT_GE(disabled_connectors, 1);
    EXPECT_EQ(install_allowed, 1);
    EXPECT_TRUE(wait_for_all_answered());
    charge_point->stop();
}

TEST_F(ChargePointSignedFirmwareConnectorsTest, AcceptedRequestRestoresConnectorsOfUnfinishedUpdate) {
    auto charge_point = make_booted_charge_point();
    charge_point->register_signed_update_firmware_callback(
        [](const SignedUpdateFirmwareRequest) { return UpdateFirmwareStatusEnumType::Accepted; });

    charge_point->on_firmware_update_status_notification(SIGNED_REQUEST_ID,
                                                         FirmwareStatusNotification::InstallScheduled, true);
    EXPECT_EQ(disabled_connectors, 1);
    EXPECT_EQ(enabled_connectors, 0);

    send_signed_update_firmware();
    EXPECT_GE(enabled_connectors, 1) << "connectors of the superseded update stay unavailable";

    EXPECT_TRUE(wait_for_all_answered());
    charge_point->stop();
}

TEST_F(ChargePointSignedFirmwareConnectorsTest, IgnoredUpdateFirmwareRequestKeepsPendingInstall) {
    auto charge_point = make_booted_charge_point();
    charge_point->register_update_firmware_callback([](const UpdateFirmwareRequest) {});
    start_transaction(*charge_point);

    charge_point->on_firmware_update_status_notification(PLAIN_REQUEST_ID, FirmwareStatusNotification::Downloaded);
    to_charge_point(nlohmann::json::array({2,
                                           "update",
                                           "UpdateFirmware",
                                           {{"location", "https://firmware.example.com/firmware.bin"},
                                            {"retrieveDate", ocpp::DateTime().to_rfc3339()}}})
                        .dump());
    ASSERT_TRUE(wait_for_answer("update").has_value());

    stop_transaction(*charge_point);
    EXPECT_TRUE(wait_for_action_count("StopTransaction", 1, WAIT_TIMEOUT));
    EXPECT_TRUE(wait_for_all_answered());
    charge_point->on_session_stopped(CONNECTOR, SESSION_ID);

    EXPECT_GE(disabled_connectors, 1);
    EXPECT_EQ(install_allowed, 1);
    EXPECT_TRUE(wait_for_all_answered());
    charge_point->stop();
}

TEST_P(ChargePointFirmwareConnectorsTest, CsmsScheduledOperativeIsExecutedBesideFirmwareDisable) {
    const auto request_id = GetParam();
    auto charge_point = make_booted_charge_point();
    start_transaction(*charge_point);

    to_charge_point(
        nlohmann::json::array(
            {2, "change-availability", "ChangeAvailability", {{"connectorId", CONNECTOR}, {"type", "Operative"}}})
            .dump());
    const auto answer = wait_for_answer("change-availability");
    ASSERT_TRUE(answer.has_value());
    ASSERT_EQ(answer->at(2).at("status"), "Scheduled");

    charge_point->on_firmware_update_status_notification(request_id, install_pending_status(request_id));
    enabled_connectors = 0;

    stop_transaction(*charge_point);
    EXPECT_TRUE(wait_for_action_count("StopTransaction", 1, WAIT_TIMEOUT));
    EXPECT_TRUE(wait_for_all_answered());
    charge_point->on_session_stopped(CONNECTOR, SESSION_ID);

    EXPECT_EQ(enabled_connectors, 1) << "the ChangeAvailability.req answered with Scheduled was not executed";
    EXPECT_GE(disabled_connectors, 1);
    EXPECT_EQ(install_allowed, 1);
    EXPECT_TRUE(wait_for_all_answered());
    charge_point->stop();
}

} // namespace v16
} // namespace ocpp
