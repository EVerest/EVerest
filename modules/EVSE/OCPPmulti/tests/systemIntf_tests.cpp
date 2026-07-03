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
//   configure_network:             <used>
//
// vars:
//   firmware_update_status:        <used>
//   log_status:                    <used>
//   configure_network_status:      <used>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <mutex>
#include <thread>

#include <generic_ocpp.hpp>

#include "generated/types/system.hpp"
#include "generic_chargepoint_interface.hpp"
#include "ocpp/common/types.hpp"
#include "ocpp/v2/messages/GetLog.hpp"
#include "ocpp/v2/messages/SetNetworkProfile.hpp"
#include "ocpp/v2/messages/UpdateFirmware.hpp"
#include "ocpp/v2/ocpp_enums.hpp"
#include "stubs/generic_ocpp_stub.hpp"

namespace {
using namespace stubs;

// Bounded poll for a condition that may be satisfied from a subscription callback.
bool wait_for_condition(const std::function<bool()>& condition,
                        std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (condition()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return condition();
}

ocpp::v2::NetworkConnectionProfile network_profile(const ocpp::v2::OCPPInterfaceEnum iface) {
    ocpp::v2::NetworkConnectionProfile profile{};
    profile.ocppInterface = iface;
    return profile;
}

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

    // No firmware_update_metadata is set, so connectors are disabled during install by default (true).
    EXPECT_CALL(chargepoint,
                on_firmware_update_status_notification(update.request_id, FirmwareStatusEnum::Downloading, true))
        .Times(1);
    EXPECT_CALL(chargepoint,
                on_firmware_update_status_notification(update.request_id, FirmwareStatusEnum::Downloaded, true))
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

// ----------------------------------------------------------------------------
// configure_network: cb_configure_network_connection_profile() delegates to call_configure_network()
// synchronously; a Processing answer defers the outcome to the configure_network_status var subscription.
// Discipline: queue cmd results BEFORE invoking the cb.

class ConfigureNetworkTester : public GenericOcppRequiresTester {
protected:
    void SetUp() override {
        GenericOcppRequiresTester::SetUp();
        interfaces->subscribe_var("system", "call_configure_network",
                                  [this](const auto&, const auto&, const auto& data) {
                                      std::lock_guard<std::mutex> lock(m_mutex);
                                      m_received.push_back(data);
                                  });
    }

    std::size_t received_count() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_received.size();
    }

    json received_request(std::size_t index) {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_received.at(index).at("request");
    }

    std::mutex m_mutex;
    std::vector<json> m_received;
};

TEST_F(ConfigureNetworkTester, callConfigureNetworkReady) {
    // Ready direct answer resolves the future successfully and forwards the interface address

    interfaces->add_cmd_result(R"({"status":"Ready","interface_address":"192.168.5.1"})"_json);

    auto future = ocpp->cb_configure_network_connection_profile(1, network_profile(ocpp::v2::OCPPInterfaceEnum::Any));

    ASSERT_EQ(future.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    const auto result = future.get();
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(result.interface_address.has_value());
    EXPECT_EQ(result.interface_address.value(), "192.168.5.1");

    ASSERT_EQ(received_count(), 1);
    const auto request = received_request(0);
    EXPECT_GT(request.at("request_id").get<std::int32_t>(), 0);
    EXPECT_EQ(request.at("interface").get<std::string>(), "Any");
}

TEST_F(ConfigureNetworkTester, callConfigureNetworkFailedAndRejected) {
    // Failed and Rejected both resolve the future as unsuccessful, without an interface address

    for (const auto* status : {R"({"status":"Failed"})", R"({"status":"Rejected"})"}) {
        interfaces->add_cmd_result(json::parse(status));

        auto future =
            ocpp->cb_configure_network_connection_profile(1, network_profile(ocpp::v2::OCPPInterfaceEnum::Wired0));

        ASSERT_EQ(future.wait_for(std::chrono::seconds(5)), std::future_status::ready) << status;
        const auto result = future.get();
        EXPECT_FALSE(result.success) << status;
        EXPECT_FALSE(result.interface_address.has_value()) << status;
    }
}

TEST_F(ConfigureNetworkTester, callConfigureNetworkNotSupportedStillSucceeds) {
    // NotSupported (the stub default when no cmd result is queued) keeps legacy parity: success, no address

    auto future =
        ocpp->cb_configure_network_connection_profile(1, network_profile(ocpp::v2::OCPPInterfaceEnum::Wired0));

    ASSERT_EQ(future.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    const auto result = future.get();
    EXPECT_TRUE(result.success);
    EXPECT_FALSE(result.interface_address.has_value());
}

TEST_F(ConfigureNetworkTester, callConfigureNetworkProcessingThenStatusVar) {
    // Processing keeps the future pending; a status var with a foreign request_id is ignored (late/orphaned
    // no-op); the matching request_id resolves it

    interfaces->add_cmd_result(R"({"status":"Processing"})"_json);

    auto future =
        ocpp->cb_configure_network_connection_profile(1, network_profile(ocpp::v2::OCPPInterfaceEnum::Wired0));

    ASSERT_TRUE(wait_for_condition([this] { return received_count() >= 1; }));
    const auto request_id = received_request(0).at("request_id").get<std::int32_t>();

    EXPECT_EQ(future.wait_for(std::chrono::milliseconds(100)), std::future_status::timeout);

    interfaces->publish(0, "configure_network_status", json{{"request_id", request_id + 1000}, {"status", "Ready"}});
    EXPECT_EQ(future.wait_for(std::chrono::milliseconds(100)), std::future_status::timeout);

    interfaces->publish(0, "configure_network_status",
                        json{{"request_id", request_id}, {"status", "Ready"}, {"interface_address", "10.1.2.3"}});
    ASSERT_EQ(future.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    const auto result = future.get();
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(result.interface_address.has_value());
    EXPECT_EQ(result.interface_address.value(), "10.1.2.3");
}

TEST_F(ConfigureNetworkTester, callConfigureNetworkSameSlotDropsStaleRequest) {
    // A re-request for the same slot resolves the still-pending previous attempt as failed (not a broken
    // promise) and registers a fresh request_id; the new attempt is the only one a status var can fulfill

    interfaces->add_cmd_result(R"({"status":"Processing"})"_json);
    interfaces->add_cmd_result(R"({"status":"Processing"})"_json);

    constexpr std::int32_t SLOT = 7;
    auto first =
        ocpp->cb_configure_network_connection_profile(SLOT, network_profile(ocpp::v2::OCPPInterfaceEnum::Wired0));
    auto second =
        ocpp->cb_configure_network_connection_profile(SLOT, network_profile(ocpp::v2::OCPPInterfaceEnum::Wired0));

    // the dedup happens synchronously in the second invocation
    ASSERT_EQ(first.wait_for(std::chrono::seconds(0)), std::future_status::ready);
    const auto stale = first.get();
    EXPECT_FALSE(stale.success);

    ASSERT_TRUE(wait_for_condition([this] { return received_count() >= 2; }));
    const auto id_a = received_request(0).at("request_id").get<std::int32_t>();
    const auto id_b = received_request(1).at("request_id").get<std::int32_t>();
    ASSERT_NE(id_a, id_b);
    // ids increase monotonically; the pending attempt is the later (higher) one.
    const auto pending_id = std::max(id_a, id_b);

    interfaces->publish(0, "configure_network_status", json{{"request_id", pending_id}, {"status", "Ready"}});
    ASSERT_EQ(second.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    EXPECT_TRUE(second.get().success);
}

TEST_F(ConfigureNetworkTester, callConfigureNetworkInvalidStatusYieldsFailure) {
    // An unparseable command result throws inside the callback; the catch path resolves the future
    // as failed instead of leaking a broken promise

    interfaces->add_cmd_result(R"({"status":"Bogus"})"_json);

    auto future =
        ocpp->cb_configure_network_connection_profile(1, network_profile(ocpp::v2::OCPPInterfaceEnum::Wired0));

    ASSERT_EQ(future.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    const auto result = future.get();
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.interface_address.has_value());
}

} // namespace
