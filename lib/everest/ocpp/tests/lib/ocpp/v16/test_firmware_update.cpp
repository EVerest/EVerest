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

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

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

/// \brief Fixture that additionally captures the registered message callback and the outgoing frames, so a test
/// can complete the boot handshake and inject incoming CSMS messages (incoming CALL routing is gated on the
/// Booted connection state).
class ChargePointUpdateFirmwareRequestTest : public ChargePointFirmwareUpdateTestBase {
protected:
    void SetUp() override {
        ChargePointFirmwareUpdateTestBase::SetUp();

        ON_CALL(*this->connectivity_manager, set_message_callback(::testing::_))
            .WillByDefault(::testing::SaveArg<0>(&this->message_callback));
        ON_CALL(*this->connectivity_manager, is_websocket_connected()).WillByDefault(::testing::Return(true));
        ON_CALL(*this->connectivity_manager, send_to_websocket(::testing::_))
            .WillByDefault(::testing::Invoke([this](const std::string& message) {
                const std::lock_guard<std::mutex> lock(this->mtx);
                this->sent_messages.push_back(message);
                this->cv.notify_all();
                return true;
            }));
    }

    /// \brief Wait (bounded) until a CALL with the given \p action has been handed to send_to_websocket and
    /// return its uniqueId.
    std::optional<std::string> wait_for_outgoing_call(const std::string& action) {
        std::unique_lock<std::mutex> lock(this->mtx);
        std::optional<std::string> unique_id;
        this->cv.wait_for(lock, std::chrono::seconds(5), [&]() {
            for (const auto& message : this->sent_messages) {
                const json parsed = json::parse(message, nullptr, false);
                if (parsed.is_array() and parsed.size() > CALL_ACTION and
                    parsed.at(MESSAGE_TYPE_ID) == static_cast<int>(MessageTypeId::CALL) and
                    parsed.at(CALL_ACTION) == action) {
                    unique_id = parsed.at(MESSAGE_ID).get<std::string>();
                    return true;
                }
            }
            return false;
        });
        return unique_id;
    }

    /// \brief Complete the websocket-connect + accepted-BootNotification handshake so incoming CALL messages are
    /// dispatched to their handlers.
    void boot_charge_point(ChargePointImpl& charge_point) {
        charge_point.on_websocket_connected(0, ocpp::v2::NetworkConnectionProfile{}, ocpp::OcppProtocolVersion::v16);

        const auto boot_notification_id = wait_for_outgoing_call("BootNotification");
        ASSERT_TRUE(boot_notification_id.has_value()) << "BootNotification.req was not sent within the timeout";

        json boot_response = json::array();
        boot_response.push_back(MessageTypeId::CALLRESULT);
        boot_response.push_back(boot_notification_id.value());
        boot_response.push_back(
            json{{"status", "Accepted"}, {"currentTime", ocpp::DateTime().to_rfc3339()}, {"interval", 0}});
        ASSERT_NE(this->message_callback, nullptr);
        this->message_callback(boot_response.dump());
    }

    /// \brief Feed an incoming CALL with the given \p action and \p payload into the registered message callback.
    void send_call(const std::string& action, const json& payload, const std::string& unique_id) {
        json call = json::array();
        call.push_back(MessageTypeId::CALL);
        call.push_back(unique_id);
        call.push_back(action);
        call.push_back(payload);
        this->message_callback(call.dump());
    }

    /// \brief The payloads of all outgoing CALLs with the given \p action, in the order they were sent.
    static std::vector<json> extract_call_payloads(const std::vector<std::string>& messages,
                                                   const std::string& action) {
        std::vector<json> payloads;
        for (const auto& message : messages) {
            const json parsed = json::parse(message, nullptr, false);
            if (parsed.is_array() and parsed.size() > CALL_PAYLOAD and
                parsed.at(MESSAGE_TYPE_ID) == static_cast<int>(MessageTypeId::CALL) and
                parsed.at(CALL_ACTION) == action) {
                payloads.push_back(parsed.at(CALL_PAYLOAD));
            }
        }
        return payloads;
    }

    /// \brief Wait (bounded) until at least \p count outgoing CALLs with the given \p action have been handed to
    /// send_to_websocket and return their payloads.
    std::vector<json> wait_for_outgoing_calls(const std::string& action, std::size_t count) {
        std::unique_lock<std::mutex> lock(this->mtx);
        this->cv.wait_for(lock, std::chrono::seconds(5),
                          [&]() { return extract_call_payloads(this->sent_messages, action).size() >= count; });
        return extract_call_payloads(this->sent_messages, action);
    }

    /// \brief A minimal, schema-valid UpdateFirmware.req payload.
    static json update_firmware_payload() {
        json payload = json::object();
        payload["location"] = "ftp://example.com/firmware.bin";
        payload["retrieveDate"] = ocpp::DateTime().to_rfc3339();
        return payload;
    }

    /// \brief A minimal, schema-valid SignedUpdateFirmware.req payload. The certificate content is irrelevant -
    /// the tests stub EvseSecurity::verify_certificate to decide whether it is accepted.
    static json signed_update_firmware_payload(const std::int32_t request_id) {
        json firmware = json::object();
        firmware["location"] = "ftp://example.com/firmware.bin";
        firmware["retrieveDateTime"] = ocpp::DateTime().to_rfc3339();
        firmware["signingCertificate"] = "-----BEGIN CERTIFICATE-----\nMIIB\n-----END CERTIFICATE-----\n";
        firmware["signature"] = "c2lnbmF0dXJl";

        json payload = json::object();
        payload["requestId"] = request_id;
        payload["firmware"] = firmware;
        return payload;
    }

    std::function<void(const std::string&)> message_callback;
    std::mutex mtx;
    std::condition_variable cv;
    std::vector<std::string> sent_messages;
};

// Test that an UpdateFirmware.req starts a new update cycle: it re-arms the single-fire guard around
// all_connectors_unavailable_callback (and clears the pending-install state), so an update that died without
// reporting a terminal status cannot leave the guard latched for the next cycle.
TEST_F(ChargePointUpdateFirmwareRequestTest, UpdateFirmwareRequestResetsSingleFireGuard) {
    auto& charge_point = start_charge_point();
    charge_point.register_update_firmware_callback([](const UpdateFirmwareRequest&) {});
    boot_charge_point(charge_point);
    if (::testing::Test::HasFatalFailure()) {
        return;
    }

    // Latch the guard; a repeated trigger shows it holds.
    charge_point.on_firmware_update_status_notification(-1, FirmwareStatusNotification::InstallScheduled,
                                                        std::optional<bool>{true});
    charge_point.on_firmware_update_status_notification(-1, FirmwareStatusNotification::InstallScheduled,
                                                        std::optional<bool>{true});
    EXPECT_EQ(this->all_connectors_unavailable_count, 1);

    // The update dies without ever reporting a terminal status; the CSMS then requests a new update.
    json update_firmware_call = json::array();
    update_firmware_call.push_back(MessageTypeId::CALL);
    update_firmware_call.push_back("update-firmware-request-1");
    update_firmware_call.push_back("UpdateFirmware");
    update_firmware_call.push_back(
        json{{"location", "ftp://example.com/firmware.bin"}, {"retrieveDate", ocpp::DateTime().to_rfc3339()}});
    this->message_callback(update_firmware_call.dump());

    // The request re-armed the guard: a new opt-in trigger fires the unavailable callback again.
    charge_point.on_firmware_update_status_notification(-1, FirmwareStatusNotification::InstallScheduled,
                                                        std::optional<bool>{true});
    EXPECT_EQ(this->all_connectors_unavailable_count, 2);
}

/// ---------------------------------------------------------------------------------------------------------------
/// The tests below model the cases that are still open on the review comment
///   "This is not reset e.g. when the fw update aborts/crashes and doesnt notify or reports Idle and then this
///    guard stays latched. I think we should reset on every firmware update request and we can remove this part
///    here. This also needs fixing in v2."
/// Each one states in its comment whether it is expected to pass against the current implementation (regression
/// coverage for behaviour that already works) or to fail (a gap that still has to be closed).
/// ---------------------------------------------------------------------------------------------------------------

// PASSES today - regression coverage for the literal scenario in the review comment.
//
// The firmware updater dies and reports Idle. Idle is not a terminal status, so nothing along the status path
// re-arms the guard; only the next UpdateFirmware.req may.
TEST_F(ChargePointUpdateFirmwareRequestTest, IdleStatusFromDyingUpdateThenNewRequestResetsSingleFireGuard) {
    auto& charge_point = start_charge_point();
    charge_point.register_update_firmware_callback([](const UpdateFirmwareRequest&) {});
    boot_charge_point(charge_point);
    if (::testing::Test::HasFatalFailure()) {
        return;
    }

    charge_point.on_firmware_update_status_notification(-1, FirmwareStatusNotification::InstallScheduled,
                                                        std::optional<bool>{true});
    EXPECT_EQ(this->all_connectors_unavailable_count, 1);

    // The update gives up and falls back to Idle instead of reporting a terminal status. That must not re-arm the
    // guard on its own - the update cycle is still the old one.
    charge_point.on_firmware_update_status_notification(-1, FirmwareStatusNotification::Idle, std::nullopt);
    charge_point.on_firmware_update_status_notification(-1, FirmwareStatusNotification::InstallScheduled,
                                                        std::optional<bool>{true});
    EXPECT_EQ(this->all_connectors_unavailable_count, 1);

    // Only the next request starts a new cycle and re-arms the guard.
    send_call("UpdateFirmware", update_firmware_payload(), "update-firmware-after-idle");

    charge_point.on_firmware_update_status_notification(-1, FirmwareStatusNotification::InstallScheduled,
                                                        std::optional<bool>{true});
    EXPECT_EQ(this->all_connectors_unavailable_count, 2);
}

// PASSES today - missing coverage: the same reset must hold for the signed variant of the request, which is the
// only way an OCPP 1.6 SecurityExtensions firmware update is ever started.
TEST_F(ChargePointUpdateFirmwareRequestTest, SignedUpdateFirmwareRequestResetsSingleFireGuard) {
    ON_CALL(*this->evse_security, verify_certificate(::testing::_, ::testing::An<const ocpp::LeafCertificateType&>()))
        .WillByDefault(::testing::Return(ocpp::CertificateValidationResult::Valid));

    auto& charge_point = start_charge_point();
    charge_point.register_signed_update_firmware_callback(
        [](const SignedUpdateFirmwareRequest) { return UpdateFirmwareStatusEnumType::Accepted; });
    boot_charge_point(charge_point);
    if (::testing::Test::HasFatalFailure()) {
        return;
    }

    // Latch the guard, then let the update die without ever reporting a terminal status.
    charge_point.on_firmware_update_status_notification(1, FirmwareStatusNotification::InstallScheduled,
                                                        std::optional<bool>{true});
    charge_point.on_firmware_update_status_notification(1, FirmwareStatusNotification::InstallScheduled,
                                                        std::optional<bool>{true});
    EXPECT_EQ(this->all_connectors_unavailable_count, 1);

    send_call("SignedUpdateFirmware", signed_update_firmware_payload(2), "signed-update-firmware-1");

    charge_point.on_firmware_update_status_notification(2, FirmwareStatusNotification::InstallScheduled,
                                                        std::optional<bool>{true});
    EXPECT_EQ(this->all_connectors_unavailable_count, 2);
}

// FAILS today - handleSignedUpdateFirmware calls clear_firmware_install_pending() before verify_certificate, so a
// request that is answered with InvalidCertificate (no new update cycle is started) still wipes the state of the
// update that is actually running: the guard is re-armed and the availability changes queued behind running
// transactions are dropped.
TEST_F(ChargePointUpdateFirmwareRequestTest, InvalidCertificateSignedUpdateFirmwareDoesNotDisturbRunningUpdate) {
    ON_CALL(*this->evse_security, verify_certificate(::testing::_, ::testing::An<const ocpp::LeafCertificateType&>()))
        .WillByDefault(::testing::Return(ocpp::CertificateValidationResult::InvalidSignature));

    auto& charge_point = start_charge_point();
    charge_point.register_signed_update_firmware_callback(
        [](const SignedUpdateFirmwareRequest) { return UpdateFirmwareStatusEnumType::Accepted; });
    boot_charge_point(charge_point);
    if (::testing::Test::HasFatalFailure()) {
        return;
    }

    // An update is running and has already notified once.
    charge_point.on_firmware_update_status_notification(1, FirmwareStatusNotification::InstallScheduled,
                                                        std::optional<bool>{true});
    EXPECT_EQ(this->all_connectors_unavailable_count, 1);

    // The CSMS sends a request whose signing certificate does not validate. It is rejected with InvalidCertificate,
    // so no new update cycle starts and the running one must be left untouched.
    send_call("SignedUpdateFirmware", signed_update_firmware_payload(2), "signed-update-firmware-invalid");

    // The still-running update notifies again - the guard must still be latched from its first notification.
    charge_point.on_firmware_update_status_notification(1, FirmwareStatusNotification::InstallScheduled,
                                                        std::optional<bool>{true});
    EXPECT_EQ(this->all_connectors_unavailable_count, 1);
}

// FAILS today - "reset on every firmware update request" has to cover the reported firmware status as well.
// firmware_status is only put back to Idle on a terminal status, so after an update that aborts while Installing
// a TriggerMessage(FirmwareStatusNotification) keeps reporting the dead update's status forever, even after the
// CSMS has started a new one.
TEST_F(ChargePointUpdateFirmwareRequestTest, NewUpdateFirmwareRequestResetsReportedFirmwareStatus) {
    auto& charge_point = start_charge_point();
    charge_point.register_update_firmware_callback([](const UpdateFirmwareRequest&) {});
    boot_charge_point(charge_point);
    if (::testing::Test::HasFatalFailure()) {
        return;
    }

    // The update reaches Installing and then dies without reporting a terminal status.
    charge_point.on_firmware_update_status_notification(-1, FirmwareStatusNotification::Installing, std::nullopt);
    const auto before_trigger = wait_for_outgoing_calls("FirmwareStatusNotification", 1);
    ASSERT_FALSE(before_trigger.empty()) << "the Installing FirmwareStatusNotification.req was not sent in time";

    // The CSMS starts a new update cycle ...
    send_call("UpdateFirmware", update_firmware_payload(), "update-firmware-after-abort");

    // ... and asks for the current firmware status. The new cycle has not reported anything yet, so the answer must
    // be Idle rather than the leftover Installing of the dead cycle.
    send_call("TriggerMessage", json{{"requestedMessage", "FirmwareStatusNotification"}}, "trigger-fw-status");

    const auto payloads = wait_for_outgoing_calls("FirmwareStatusNotification", before_trigger.size() + 1);
    ASSERT_GT(payloads.size(), before_trigger.size())
        << "the triggered FirmwareStatusNotification.req was not sent within the timeout";
    EXPECT_EQ(payloads.back().at("status"), "Idle");
}

} // namespace v16
} // namespace ocpp
