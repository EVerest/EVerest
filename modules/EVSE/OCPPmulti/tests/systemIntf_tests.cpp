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

#include <atomic>
#include <chrono>
#include <future>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <tuple>

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

TEST_F(GenericOcppRequiresTester, callIsResetAllowedImmediateAndResume) {
    // B13.FR.01 (TxCtrlr.ResumptionTimeout) is checked by libocpp, the system decides like for Immediate

    using ResetType = ocpp_multi::GenericChargePointCallbacks::ResetType;

    std::vector<json> received;
    interfaces->subscribe_var("system", "call_is_reset_allowed",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    interfaces->add_cmd_result(R"(true)"_json);
    interfaces->add_cmd_result(R"(false)"_json);

    EXPECT_TRUE(ocpp->cb_is_reset_allowed(std::nullopt, ResetType::ImmediateAndResume));
    EXPECT_FALSE(ocpp->cb_is_reset_allowed(std::nullopt, ResetType::ImmediateAndResume));
    EXPECT_FALSE(ocpp->cb_is_reset_allowed(1, ResetType::ImmediateAndResume));

    ASSERT_EQ(received.size(), 2);
    EXPECT_EQ(received[0], R"({"type":"NotSpecified"})"_json);
}

struct ResetWithResumptionCalls {
    std::mutex mutex;
    std::vector<int> prepared;
    std::vector<json> prepared_args;
    std::vector<std::pair<int, json>> stopped;
    std::vector<int> cancelled;
    std::vector<json> resets;
    std::size_t prepared_before_stop{0};

    std::size_t reset_count() {
        std::lock_guard<std::mutex> lock(mutex);
        return resets.size();
    }
};

void record_reset_with_resumption(stubs::ModuleInterfaces& interfaces, ResetWithResumptionCalls& calls) {
    for (int index : {0, 1}) {
        interfaces.subscribe_var("evse_manager", "call_prepare_resume_after_reset", index,
                                 [&calls, index](const auto&, const auto&, const auto& data) {
                                     std::lock_guard<std::mutex> lock(calls.mutex);
                                     calls.prepared.push_back(index);
                                     calls.prepared_args.push_back(data);
                                 });
        interfaces.subscribe_var("evse_manager", "call_stop_transaction", index,
                                 [&calls, index](const auto&, const auto&, const auto& data) {
                                     std::lock_guard<std::mutex> lock(calls.mutex);
                                     calls.stopped.emplace_back(index, data);
                                 });
        interfaces.subscribe_var("evse_manager", "call_cancel_resume_after_reset", index,
                                 [&calls, index](const auto&, const auto&, const auto&) {
                                     std::lock_guard<std::mutex> lock(calls.mutex);
                                     calls.cancelled.push_back(index);
                                 });
    }
    interfaces.subscribe_var("system", "call_reset", [&calls](const auto&, const auto&, const auto& data) {
        std::lock_guard<std::mutex> lock(calls.mutex);
        calls.resets.push_back(data);
    });
}

TEST_F(GenericOcppRequiresTester, callResetWithResumptionKeepsTransactions) {
    using ResetType = ocpp_multi::GenericChargePointCallbacks::ResetType;
    ResetWithResumptionCalls calls;
    record_reset_with_resumption(*interfaces, calls);
    EXPECT_CALL(chargepoint, stop()).WillOnce([&calls]() {
        std::lock_guard<std::mutex> lock(calls.mutex);
        calls.prepared_before_stop = calls.prepared.size();
    });

    interfaces->add_cmd_result("true"_json); // prepare_resume_after_reset EVSE 1
    interfaces->add_cmd_result("true"_json); // prepare_resume_after_reset EVSE 2
    interfaces->add_cmd_result("true"_json); // reset

    ocpp->cb_reset(std::nullopt, ResetType::ImmediateAndResume,
                   ocpp_multi::GenericChargePointCallbacks::TransactionResumption{false});
    EXPECT_TRUE(wait_for_condition([&] { return calls.reset_count() >= 1; }));
    ocpp->shutdown();

    EXPECT_EQ(calls.prepared, (std::vector<int>{0, 1}));
    EXPECT_EQ(calls.prepared_before_stop, 2);
    ASSERT_EQ(calls.prepared_args.size(), 2);
    EXPECT_EQ(calls.prepared_args[0], R"({"allow_energy_transfer":false})"_json);
    EXPECT_TRUE(calls.stopped.empty());
    ASSERT_EQ(calls.resets.size(), 1);
    EXPECT_EQ(calls.resets[0], R"({"scheduled":false,"type":"NotSpecified"})"_json);
}

TEST_F(GenericOcppRequiresTester, callResetWithResumptionStopsTransactionsThatCannotBeKept) {
    using ResetType = ocpp_multi::GenericChargePointCallbacks::ResetType;
    ResetWithResumptionCalls calls;
    record_reset_with_resumption(*interfaces, calls);

    ocpp->m_reset_stop_timeout = std::chrono::milliseconds(100);

    interfaces->add_cmd_result("false"_json); // prepare_resume_after_reset EVSE 1
    interfaces->add_cmd_result("true"_json);  // stop_transaction EVSE 1
    interfaces->add_cmd_result("true"_json);  // prepare_resume_after_reset EVSE 2
    interfaces->add_cmd_result("true"_json);  // reset

    ocpp->cb_reset(std::nullopt, ResetType::Hard, ocpp_multi::GenericChargePointCallbacks::TransactionResumption{});
    EXPECT_TRUE(wait_for_condition([&] { return calls.reset_count() >= 1; }));
    ocpp->shutdown();

    EXPECT_EQ(calls.prepared, (std::vector<int>{0, 1}));
    ASSERT_EQ(calls.stopped.size(), 1);
    EXPECT_EQ(calls.stopped[0].first, 0);
    EXPECT_EQ(calls.stopped[0].second.at("request").at("reason"), "HardReset");
    ASSERT_EQ(calls.resets.size(), 1);
    EXPECT_EQ(calls.resets[0], R"({"scheduled":false,"type":"Hard"})"_json);
}

types::evse_manager::SessionEvent transaction_finished_event() {
    types::evse_manager::SessionEvent event;
    event.uuid = "session";
    event.timestamp = "2026-01-01T00:00:00Z";
    event.event = types::evse_manager::SessionEventEnum::TransactionFinished;
    event.connector_id = 1;
    types::evse_manager::TransactionFinished finished;
    finished.reason = types::evse_manager::StopTransactionReason::HardReset;
    event.transaction_finished = finished;
    return event;
}

// The stop is reported before the charge point stops, so that it is not lost in the reset
TEST_F(GenericOcppRequiresTester, callResetWithResumptionWaitsForTheTransactionsItStops) {
    using ResetType = ocpp_multi::GenericChargePointCallbacks::ResetType;
    ResetWithResumptionCalls calls;
    record_reset_with_resumption(*interfaces, calls);
    std::atomic_bool finished_reported{false};
    std::atomic_bool finished_before_stop{false};
    EXPECT_CALL(chargepoint, stop()).WillOnce([&]() { finished_before_stop = finished_reported.load(); });

    interfaces->add_cmd_result("false"_json); // prepare_resume_after_reset EVSE 1
    interfaces->add_cmd_result("true"_json);  // stop_transaction EVSE 1
    interfaces->add_cmd_result("true"_json);  // prepare_resume_after_reset EVSE 2
    interfaces->add_cmd_result("true"_json);  // reset

    ocpp->cb_reset(std::nullopt, ResetType::Hard, ocpp_multi::GenericChargePointCallbacks::TransactionResumption{});
    ASSERT_TRUE(wait_for_condition([&] {
        std::lock_guard<std::mutex> lock(calls.mutex);
        return not calls.stopped.empty();
    }));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_EQ(calls.reset_count(), 0);

    finished_reported = true;
    ocpp->process_session_event(1, transaction_finished_event());
    EXPECT_TRUE(wait_for_condition([&] { return calls.reset_count() >= 1; }));
    ocpp->shutdown();

    EXPECT_TRUE(finished_before_stop);
}

TEST_F(GenericOcppRequiresTester, callResetWithResumptionCancelsWhenTheResetFails) {
    using ResetType = ocpp_multi::GenericChargePointCallbacks::ResetType;
    ResetWithResumptionCalls calls;
    record_reset_with_resumption(*interfaces, calls);
    interfaces->subscribe_var("system", "call_reset",
                              [](const auto&, const auto&, const auto&) { throw std::runtime_error("reset failed"); });

    interfaces->add_cmd_result("true"_json); // prepare_resume_after_reset EVSE 1
    interfaces->add_cmd_result("true"_json); // prepare_resume_after_reset EVSE 2

    ocpp->cb_reset(std::nullopt, ResetType::ImmediateAndResume,
                   ocpp_multi::GenericChargePointCallbacks::TransactionResumption{});
    EXPECT_TRUE(wait_for_condition([&] {
        std::lock_guard<std::mutex> lock(calls.mutex);
        return calls.cancelled.size() >= 2;
    }));
    ocpp->shutdown();

    EXPECT_EQ(calls.cancelled, (std::vector<int>{0, 1}));
}

// An EVSE whose preparation fails is treated as one that cannot keep its transaction
TEST_F(GenericOcppRequiresTester, callResetWithResumptionStopsATransactionWhosePreparationFails) {
    using ResetType = ocpp_multi::GenericChargePointCallbacks::ResetType;
    ResetWithResumptionCalls calls;
    record_reset_with_resumption(*interfaces, calls);
    interfaces->subscribe_var("evse_manager", "call_prepare_resume_after_reset", 0,
                              [](const auto&, const auto&, const auto&) { throw std::runtime_error("no answer"); });
    ocpp->m_reset_stop_timeout = std::chrono::milliseconds(100);

    interfaces->add_cmd_result("true"_json); // stop_transaction EVSE 1
    interfaces->add_cmd_result("true"_json); // prepare_resume_after_reset EVSE 2
    interfaces->add_cmd_result("true"_json); // reset

    ocpp->cb_reset(std::nullopt, ResetType::ImmediateAndResume,
                   ocpp_multi::GenericChargePointCallbacks::TransactionResumption{});
    EXPECT_TRUE(wait_for_condition([&] { return calls.reset_count() >= 1; }));
    ocpp->shutdown();

    EXPECT_EQ(calls.prepared, (std::vector<int>{0, 1}));
    ASSERT_EQ(calls.stopped.size(), 1);
    EXPECT_EQ(calls.stopped[0].first, 0);
    EXPECT_TRUE(calls.cancelled.empty());
}

TEST_F(GenericOcppRequiresTester, callResetWithResumptionCancelsWhenStoppingTheChargePointFails) {
    using ResetType = ocpp_multi::GenericChargePointCallbacks::ResetType;
    ResetWithResumptionCalls calls;
    record_reset_with_resumption(*interfaces, calls);
    EXPECT_CALL(chargepoint, stop()).WillOnce([]() { throw std::runtime_error("stop failed"); });

    interfaces->add_cmd_result("true"_json); // prepare_resume_after_reset EVSE 1
    interfaces->add_cmd_result("true"_json); // prepare_resume_after_reset EVSE 2

    ocpp->cb_reset(std::nullopt, ResetType::ImmediateAndResume,
                   ocpp_multi::GenericChargePointCallbacks::TransactionResumption{});
    EXPECT_TRUE(wait_for_condition([&] {
        std::lock_guard<std::mutex> lock(calls.mutex);
        return calls.cancelled.size() >= 2;
    }));
    ocpp->shutdown();

    EXPECT_EQ(calls.cancelled, (std::vector<int>{0, 1}));
    EXPECT_EQ(calls.reset_count(), 0);
}

TEST_F(GenericOcppRequiresTester, callResetWithResumptionCancelsTheOtherEvsesWhenOneCancelFails) {
    using ResetType = ocpp_multi::GenericChargePointCallbacks::ResetType;
    ResetWithResumptionCalls calls;
    record_reset_with_resumption(*interfaces, calls);
    interfaces->subscribe_var("evse_manager", "call_cancel_resume_after_reset", 0,
                              [](const auto&, const auto&, const auto&) { throw std::runtime_error("no answer"); });
    interfaces->subscribe_var("system", "call_reset",
                              [](const auto&, const auto&, const auto&) { throw std::runtime_error("reset failed"); });

    interfaces->add_cmd_result("true"_json); // prepare_resume_after_reset EVSE 1
    interfaces->add_cmd_result("true"_json); // prepare_resume_after_reset EVSE 2

    ocpp->cb_reset(std::nullopt, ResetType::ImmediateAndResume,
                   ocpp_multi::GenericChargePointCallbacks::TransactionResumption{});
    EXPECT_TRUE(wait_for_condition([&] {
        std::lock_guard<std::mutex> lock(calls.mutex);
        return calls.cancelled.size() >= 2;
    }));
    ocpp->shutdown();

    EXPECT_EQ(calls.cancelled, (std::vector<int>{0, 1}));
}

TEST_F(GenericOcppRequiresTester, callResetWithoutResumptionCancelsNothingWhenTheResetFails) {
    using ResetType = ocpp_multi::GenericChargePointCallbacks::ResetType;
    ResetWithResumptionCalls calls;
    record_reset_with_resumption(*interfaces, calls);
    interfaces->subscribe_var("system", "call_reset",
                              [](const auto&, const auto&, const auto&) { throw std::runtime_error("reset failed"); });

    ocpp->cb_reset(std::nullopt, ResetType::Immediate, std::nullopt);
    EXPECT_TRUE(wait_for_condition([&] { return calls.reset_count() >= 1; }));
    ocpp->shutdown();

    EXPECT_TRUE(calls.cancelled.empty());
}

TEST_F(GenericOcppRequiresTester, callResetWithoutResumptionKeepsNothing) {
    using ResetType = ocpp_multi::GenericChargePointCallbacks::ResetType;
    ResetWithResumptionCalls calls;
    record_reset_with_resumption(*interfaces, calls);
    interfaces->add_cmd_result("true"_json); // reset

    ocpp->cb_reset(std::nullopt, ResetType::Immediate, std::nullopt);
    EXPECT_TRUE(wait_for_condition([&] { return calls.reset_count() >= 1; }));
    ocpp->shutdown();

    EXPECT_TRUE(calls.prepared.empty());
}

using ResetType = ocpp_multi::GenericChargePointCallbacks::ResetType;

struct GenericOcppResetTester : public GenericOcppRequiresTester,
                                public testing::WithParamInterface<std::tuple<ResetType, json>> {};

TEST_P(GenericOcppResetTester, callReset) {
    // call_reset() used in cb_reset()

    const auto& [type, expected] = GetParam();

    std::mutex mutex;
    std::vector<json> received;
    interfaces->subscribe_var("system", "call_reset", [&](const auto&, const auto&, const auto& data) {
        std::lock_guard<std::mutex> lock(mutex);
        received.push_back(data);
    });
    const auto received_count = [&]() {
        std::lock_guard<std::mutex> lock(mutex);
        return received.size();
    };

    interfaces->add_cmd_result(R"(true)"_json);

    ocpp->cb_reset(std::nullopt, type, std::nullopt);
    EXPECT_TRUE(wait_for_condition([&] { return received_count() >= 1; }));

    // joins the reset work before the fixture tears down the interfaces
    ocpp->shutdown();

    ASSERT_EQ(received.size(), 1);
    EXPECT_EQ(received[0], expected);
}

INSTANTIATE_TEST_SUITE_P(ResetTypes, GenericOcppResetTester,
                         testing::Values(std::make_tuple(ResetType::Immediate,
                                                         R"({"scheduled":false,"type":"NotSpecified"})"_json), // v2
                                         std::make_tuple(ResetType::Hard,
                                                         R"({"scheduled":false,"type":"Hard"})"_json), // v1.6
                                         std::make_tuple(ResetType::Soft,
                                                         R"({"scheduled":false,"type":"Soft"})"_json))); // v1.6

TEST_F(GenericOcppRequiresTester, callResetOfEvseNotSent) {
    std::vector<json> received;
    interfaces->subscribe_var("system", "call_reset",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    ocpp->cb_reset(1, ResetType::OnIdle, std::nullopt);
    ocpp->shutdown();

    EXPECT_TRUE(received.empty());
}

TEST_F(GenericOcppRequiresTester, callResetDoesNotBlockCaller) {
    ASSERT_EQ(config.ResetStopDelay, 0);

    std::mutex mutex;
    std::vector<json> received;
    interfaces->subscribe_var("system", "call_reset", [&](const auto&, const auto&, const auto& data) {
        std::lock_guard<std::mutex> lock(mutex);
        received.push_back(data);
    });
    const auto received_count = [&]() {
        std::lock_guard<std::mutex> lock(mutex);
        return received.size();
    };

    std::promise<void> stop_entered;
    std::promise<void> release_stop;
    auto release = release_stop.get_future().share();
    EXPECT_CALL(chargepoint, stop()).WillOnce([&stop_entered, release]() {
        stop_entered.set_value();
        release.wait_for(std::chrono::seconds(5));
    });

    auto reset =
        std::async(std::launch::async, [this]() { ocpp->cb_reset(std::nullopt, ResetType::Hard, std::nullopt); });

    const bool returned = reset.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
    const bool stop_blocked = stop_entered.get_future().wait_for(std::chrono::seconds(2)) == std::future_status::ready;
    const auto published_while_blocked = received_count();
    release_stop.set_value();

    EXPECT_TRUE(returned) << "cb_reset() waited for stop()";
    EXPECT_TRUE(stop_blocked);
    EXPECT_EQ(published_while_blocked, 0);
    EXPECT_TRUE(wait_for_condition([&] { return received_count() >= 1; }));

    // joins the reset work before the fixture tears down the interfaces
    ocpp->shutdown();

    ASSERT_EQ(received_count(), 1);
    EXPECT_EQ(received[0], R"({"scheduled":false,"type":"Hard"})"_json);
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

    // No firmware_update_metadata is set, so the flag is forwarded unset and libocpp applies its defaults
    EXPECT_CALL(chargepoint, on_firmware_update_status_notification(update.request_id, FirmwareStatusEnum::Downloading,
                                                                    std::optional<bool>{}))
        .Times(1);
    EXPECT_CALL(chargepoint, on_firmware_update_status_notification(update.request_id, FirmwareStatusEnum::Downloaded,
                                                                    std::optional<bool>{}))
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
// configure_network: with DelegateNetworkConfigurationToSystem set, cb_configure_network_connection_profile() delegates
// to call_configure_network() synchronously; a Processing answer defers the outcome to the configure_network_status var
// subscription. Discipline: queue cmd results BEFORE invoking the cb.

class ConfigureNetworkTester : public GenericOcppRequiresTester {
protected:
    void SetUp() override {
        config.DelegateNetworkConfigurationToSystem = true;
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

// ----------------------------------------------------------------------------
// configure_network with DelegateNetworkConfigurationToSystem left at its default (false): no provider round-trip,
// the future resolves immediately like the OCPP201 stub does

class ConfigureNetworkDisabledTester : public GenericOcppRequiresTester {
protected:
    void SetUp() override {
        ASSERT_FALSE(config.DelegateNetworkConfigurationToSystem);
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

    std::mutex m_mutex;
    std::vector<json> m_received;
};

TEST_F(ConfigureNetworkDisabledTester, disabledResolvesImmediatelyWithoutCallingProvider) {
    // a queued Ready answer must never be consumed: the provider is not asked at all

    interfaces->add_cmd_result(R"({"status":"Ready","interface_address":"192.168.5.1"})"_json);

    auto future = ocpp->cb_configure_network_connection_profile(1, network_profile(ocpp::v2::OCPPInterfaceEnum::Any));

    ASSERT_EQ(future.wait_for(std::chrono::seconds(0)), std::future_status::ready);
    const auto result = future.get();
    EXPECT_TRUE(result.success);
    EXPECT_FALSE(result.interface_address.has_value());

    EXPECT_FALSE(wait_for_condition([this] { return received_count() >= 1; }, std::chrono::milliseconds(200)));
}

TEST_F(ConfigureNetworkDisabledTester, disabledIgnoresStatusVar) {
    // without a pending request a configure_network_status publish is a no-op; nothing is fulfilled twice

    auto future =
        ocpp->cb_configure_network_connection_profile(1, network_profile(ocpp::v2::OCPPInterfaceEnum::Wired0));
    ASSERT_EQ(future.wait_for(std::chrono::seconds(0)), std::future_status::ready);
    EXPECT_TRUE(future.get().success);

    interfaces->publish(0, "configure_network_status",
                        json{{"request_id", 1}, {"status", "Ready"}, {"interface_address", "10.1.2.3"}});
    interfaces->publish(0, "configure_network_status", json{{"request_id", 1}, {"status", "Failed"}});

    EXPECT_FALSE(wait_for_condition([this] { return received_count() >= 1; }, std::chrono::milliseconds(200)));
}

} // namespace
