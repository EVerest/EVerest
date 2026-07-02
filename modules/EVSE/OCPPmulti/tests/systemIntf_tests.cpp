// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

// cmds:
//   update_firmware:               <used>
//   allow_firmware_installation:   <used>
//   upload_logs:                   <used>
//   is_reset_allowed:              <used>
//   reset:                         <used>
//   set_system_time:               <used>
//   get_boot_reason:               <used> (tested in GenericOcppTester, init)
//
// vars:
//   firmware_update_status:        <used>
//   log_status:                    <used>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <generic_ocpp.hpp>

#include "generated/types/system.hpp"
#include "generic_chargepoint_interface.hpp"
#include "ocpp/common/types.hpp"
#include "ocpp/v2/messages/GetLog.hpp"
#include "ocpp/v2/messages/UpdateFirmware.hpp"
#include "ocpp/v2/ocpp_enums.hpp"
#include "stubs/generic_ocpp_stub.hpp"

namespace {
using namespace stubs;

TEST_F(GenericOcppRequiresTester, callUpdateFirmware) {
    // call_update_firmware() used in cb_update_firmware_request()

    using ocpp::DateTime;
    using ocpp::v2::Firmware;
    using ocpp::v2::UpdateFirmwareRequest;
    using ocpp::v2::UpdateFirmwareResponse;
    using ocpp::v2::UpdateFirmwareStatusEnum;

    std::vector<json> received;
    interfaces->subscribe_var("system", "call_update_firmware",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    Firmware firmware;
    firmware.location = "https://127.0.0.1:8445/bundle.ota";
    firmware.retrieveDateTime = DateTime{"2026-01-01T12:00:45Z"};
    // std::optional<ocpp::DateTime> installDateTime;
    // std::optional<CiString<5500>> signingCertificate;
    // std::optional<CiString<800>> signature;
    // std::optional<CustomData> customData;

    UpdateFirmwareRequest request;
    request.requestId = 5974;
    request.firmware = firmware;
    // std::optional<std::int32_t> retries;
    // std::optional<std::int32_t> retryInterval;
    // std::optional<CustomData> customData;

    // types::system::UpdateFirmwareResponse
    // - Accepted,
    // - Rejected,
    // - AcceptedCanceled,
    // - InvalidCertificate,
    // - RevokedCertificate,

    interfaces->add_cmd_result(R"("InvalidCertificate")"_json);

    UpdateFirmwareResponse expected;
    expected.status = UpdateFirmwareStatusEnum::InvalidCertificate;
    // std::optional<StatusInfo> statusInfo;
    // std::optional<CustomData> customData;

    const auto result = ocpp->cb_update_firmware_request(request);

    ASSERT_EQ(received.size(), 1);
    EXPECT_EQ(
        received[0],
        R"({"firmware_update_request":{"location":"https://127.0.0.1:8445/bundle.ota","request_id":5974,"retrieve_timestamp":"2026-01-01T12:00:45.000Z"}})"_json);

    EXPECT_EQ(result, expected);
}

TEST_F(GenericOcppRequiresTester, callAllowFirmwareInstallation) {
    // call_allow_firmware_installation() used in cb_all_connectors_unavailable()

    std::vector<json> received;
    interfaces->subscribe_var("system", "call_allow_firmware_installation",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    ocpp->cb_all_connectors_unavailable();

    ASSERT_EQ(received.size(), 1);
    EXPECT_EQ(received[0], json{});
}

TEST_F(GenericOcppRequiresTester, callUploadLogs) {
    // call_upload_logs() used in cb_get_log_request()

    using ocpp::v2::GetLogRequest;
    using ocpp::v2::GetLogResponse;
    using ocpp::v2::LogEnum;
    using ocpp::v2::LogParameters;
    using ocpp::v2::LogStatusEnum;
    using types::system::UploadLogsRequest;

    std::vector<json> received;
    interfaces->subscribe_var("system", "call_upload_logs",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    UploadLogsRequest request;
    request.location = "sftp://127.0.0.1:9957/diagnostics.log";
    request.type = "DiagnosticsLog";
    request.request_id = 18234;

    interfaces->add_cmd_result(R"({"upload_logs_status":"Accepted"})"_json);

    const auto result = ocpp->cb_get_log_request(request);

    ASSERT_EQ(received.size(), 1);
    EXPECT_EQ(
        received[0],
        R"({"upload_logs_request":{"location":"sftp://127.0.0.1:9957/diagnostics.log","request_id":18234,"type":"DiagnosticsLog"}})"_json);

    GetLogResponse expected;
    expected.status = LogStatusEnum::Accepted;
    // std::optional<StatusInfo> statusInfo;
    // std::optional<CiString<255>> filename;
    // std::optional<CustomData> customData;

    EXPECT_EQ(result, expected);
}

TEST_F(GenericOcppRequiresTester, callIsResetAllowed) {
    // call_is_reset_allowed() used in cb_is_reset_allowed()

    using ResetType = ocpp_multi::GenericChargePointCallbacks::ResetType;

    std::vector<json> received;
    interfaces->subscribe_var("system", "call_is_reset_allowed",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    interfaces->add_cmd_result(R"(true)"_json);
    interfaces->add_cmd_result(R"(true)"_json);

    auto result = ocpp->cb_is_reset_allowed(1, ResetType::Immediate);
    EXPECT_FALSE(result);
    result = ocpp->cb_is_reset_allowed(std::nullopt, ResetType::Immediate);
    EXPECT_TRUE(result);
    result = ocpp->cb_is_reset_allowed(std::nullopt, ResetType::Soft);
    EXPECT_TRUE(result);

    ASSERT_EQ(received.size(), 2);
    EXPECT_EQ(received[0], R"({"type":"NotSpecified"})"_json);
    EXPECT_EQ(received[1], R"({"type":"Soft"})"_json);
}

TEST_F(GenericOcppRequiresTester, callReset) {
    // call_reset() used in cb_is_reset_allowed()

    using ResetType = ocpp_multi::GenericChargePointCallbacks::ResetType;

    std::vector<json> received;
    interfaces->subscribe_var("system", "call_reset",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    interfaces->add_cmd_result(R"(true)"_json);
    interfaces->add_cmd_result(R"(true)"_json);
    interfaces->add_cmd_result(R"(true)"_json);

    ocpp->cb_reset(1, ResetType::OnIdle);               // no MQTT message sent
    ocpp->cb_reset(std::nullopt, ResetType::Immediate); // v2
    ocpp->cb_reset(std::nullopt, ResetType::Hard);      // v1.6
    ocpp->cb_reset(std::nullopt, ResetType::Soft);      // v1.6

    ASSERT_EQ(received.size(), 3);
    EXPECT_EQ(received[0], R"({"scheduled":false,"type":"NotSpecified"})"_json);
    EXPECT_EQ(received[1], R"({"scheduled":false,"type":"Hard"})"_json);
    EXPECT_EQ(received[2], R"({"scheduled":false,"type":"Soft"})"_json);
}

TEST_F(GenericOcppRequiresTester, callSetSystemTime) {
    // call_set_system_time() used in cb_time_sync()

    using ocpp::DateTime;

    std::vector<json> received;
    interfaces->subscribe_var("system", "call_set_system_time",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    interfaces->add_cmd_result(R"(true)"_json);

    ocpp->cb_time_sync(DateTime{"2026-01-01T12:25:30Z"});

    ASSERT_EQ(received.size(), 1);
    EXPECT_EQ(received[0], R"({"timestamp":"2026-01-01T12:25:30.000Z"})"_json);
}

TEST_F(GenericOcppRequiresTester, subscribeSupportedEnergyTransferModes) {
    // subscribe_firmware_update_status() calls cb_firmware_update_status()

    using ocpp::v2::FirmwareStatusEnum;
    using types::system::FirmwareUpdateStatus;
    using types::system::FirmwareUpdateStatusEnum;

    FirmwareUpdateStatus update;
    update.request_id = 128847;
    update.firmware_update_status = FirmwareUpdateStatusEnum::Downloading;

    EXPECT_CALL(chargepoint, on_firmware_update_status_notification(update.request_id, FirmwareStatusEnum::Downloading))
        .Times(1);
    EXPECT_CALL(chargepoint, on_firmware_update_status_notification(update.request_id, FirmwareStatusEnum::Downloaded))
        .Times(1);

    interfaces->publish(0, "firmware_update_status", update);
    update.firmware_update_status = FirmwareUpdateStatusEnum::Downloaded;
    interfaces->publish(0, "firmware_update_status", update);
}

TEST_F(GenericOcppRequiresTester, subscribeLogStatus) {
    // subscribe_log_status() calls cb_log_status()

    using ocpp::v2::UploadLogStatusEnum;
    using types::system::LogStatus;
    using types::system::LogStatusEnum;

    LogStatus update;
    update.log_status = LogStatusEnum::Uploaded;
    update.request_id = 456978;

    EXPECT_CALL(chargepoint, on_log_status_notification(UploadLogStatusEnum::Uploaded, update.request_id)).Times(1);
    EXPECT_CALL(chargepoint, on_log_status_notification(UploadLogStatusEnum::Idle, update.request_id)).Times(1);

    interfaces->publish(0, "log_status", update);
    update.log_status = LogStatusEnum::Idle;
    interfaces->publish(0, "log_status", update);
}

} // namespace
