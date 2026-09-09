// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

/// \file test_firmware_update.cpp
/// \brief Unit tests for the connector-disable logic in ChargePointImpl::on_firmware_update_status_notification.
///
/// The FirmwareStatus enum of plain OCPP 1.6 (request_id == -1) cannot represent InstallScheduled, so the
/// conversion throws std::out_of_range and is swallowed. Nothing is sent to the CSMS for that status, but the
/// connector-disable side effects still run, which is why these tests only assert on the callbacks.

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

#define private public // Make firmware_status accessible, so a reset of it can be asserted without the CSMS.
#include <ocpp/v16/charge_point_impl.hpp>
#undef private

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

        // One temporary directory per test, so the sqlite db and message logs do not collide. Tests in a gtest
        // binary run sequentially, so an incrementing counter is unique
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

    /// \brief Register the callbacks the connector-disable path fires and record what they are called with
    void register_callbacks(ChargePointImpl& cp) {
        cp.register_disable_evse_callback([this](std::int32_t connector) {
            this->disabled_connectors.push_back(connector);
            return true;
        });
        cp.register_all_connectors_unavailable_callback([this]() { ++this->all_connectors_unavailable_count; });
        cp.register_enable_evse_callback([this](std::int32_t connector) {
            this->enabled_connectors.push_back(connector);
            return true;
        });
    }

    /// \brief The connectors 1..N the disable path is expected to disable when none are in an active transaction.
    std::vector<std::int32_t> expected_idle_connectors() const {
        std::vector<std::int32_t> connectors;
        for (std::int32_t connector = 1; connector <= this->configuration->getNumberOfConnectors(); ++connector) {
            connectors.push_back(connector);
        }
        return connectors;
    }

    /// \brief The connectors the restore sequence re-enables: every one the database reports as Operative, which
    /// includes the charge-point-wide connector 0.
    std::vector<std::int32_t> expected_restored_connectors() const {
        std::vector<std::int32_t> connectors{0};
        for (const auto connector : this->expected_idle_connectors()) {
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
    std::vector<std::int32_t> enabled_connectors;
    int all_connectors_unavailable_count{0};
};

using ChargePointFirmwareUpdateTest = ChargePointFirmwareUpdateTestBase;

TEST_F(ChargePointFirmwareUpdateTest, InstallScheduledOptInDisablesConnectorsSingleFire) {
    auto& charge_point = start_charge_point();

    charge_point.on_firmware_update_status_notification(-1, FirmwareStatusNotification::InstallScheduled,
                                                        std::optional<bool>{true});

    EXPECT_EQ(this->disabled_connectors, expected_idle_connectors());
    EXPECT_EQ(this->all_connectors_unavailable_count, 1);

    // The same trigger again: the guard keeps the unavailable callback at one call until a terminal status
    // resets it
    charge_point.on_firmware_update_status_notification(-1, FirmwareStatusNotification::InstallScheduled,
                                                        std::optional<bool>{true});

    EXPECT_EQ(this->all_connectors_unavailable_count, 1);
}

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

TEST_F(ChargePointFirmwareUpdateTest, DownloadedDefaultsToDisablingConnectors) {
    auto& charge_point = start_charge_point();

    charge_point.on_firmware_update_status_notification(-1, FirmwareStatusNotification::Downloaded, std::nullopt);

    EXPECT_EQ(this->disabled_connectors, expected_idle_connectors());
    EXPECT_EQ(this->all_connectors_unavailable_count, 1);
}

TEST_F(ChargePointFirmwareUpdateTest, DownloadedWithFalseDoesNotDisableConnectors) {
    auto& charge_point = start_charge_point();

    charge_point.on_firmware_update_status_notification(-1, FirmwareStatusNotification::Downloaded,
                                                        std::optional<bool>{false});

    EXPECT_TRUE(this->disabled_connectors.empty());
    EXPECT_EQ(this->all_connectors_unavailable_count, 0);
}

TEST_F(ChargePointFirmwareUpdateTest, TerminalStatusResetsSingleFireGuard) {
    auto& charge_point = start_charge_point();

    charge_point.on_firmware_update_status_notification(-1, FirmwareStatusNotification::InstallScheduled,
                                                        std::optional<bool>{true});
    EXPECT_EQ(this->all_connectors_unavailable_count, 1);

    // Terminal status, which also clears all_connectors_unavailable_notified
    charge_point.on_firmware_update_status_notification(-1, FirmwareStatusNotification::InstallationFailed,
                                                        std::nullopt);
    EXPECT_EQ(this->all_connectors_unavailable_count, 1);

    // The guard is reset, so a new opt-in trigger fires the callback again
    charge_point.on_firmware_update_status_notification(-1, FirmwareStatusNotification::InstallScheduled,
                                                        std::optional<bool>{true});
    EXPECT_EQ(this->all_connectors_unavailable_count, 2);
}

// The CONNECTORS table is seeded first, because the restore sequence reads availability from the database and a
// fresh test database has no rows, which would make the restore loop a no-op whatever status triggered it
TEST_F(ChargePointFirmwareUpdateTest, IdleStatusRestoresConnectorsDisabledForInstall) {
    auto& charge_point = start_charge_point();

    for (const auto connector : this->expected_idle_connectors()) {
        charge_point.database_handler->insert_or_update_connector_availability(connector, AvailabilityType::Operative);
    }

    charge_point.on_firmware_update_status_notification(-1, FirmwareStatusNotification::InstallScheduled,
                                                        std::optional<bool>{true});
    ASSERT_EQ(this->disabled_connectors, expected_idle_connectors());
    ASSERT_EQ(this->all_connectors_unavailable_count, 1);

    // Boot already enabled the connectors it read as Operative, so only the enables from here on are the
    // restore sequence's own
    this->enabled_connectors.clear();

    // The updater aborts and falls back to Idle instead of a terminal status
    charge_point.on_firmware_update_status_notification(-1, FirmwareStatusNotification::Idle, std::nullopt);

    EXPECT_EQ(this->enabled_connectors, expected_restored_connectors())
        << "Idle did not run the restore sequence: connectors disabled for the firmware install are stuck "
           "Unavailable";
}

// The erase predicate has to discriminate on the persist flag, not on the connector id
TEST_F(ChargePointFirmwareUpdateTest, ClearFirmwareInstallPendingDropsOnlyNonPersistentQueuedChanges) {
    auto& charge_point = start_charge_point();

    // A CSMS ChangeAvailability.req queued behind a running transaction is always persist == true and must
    // survive whatever a firmware update does
    charge_point.change_availability_queue[1] = {AvailabilityType::Inoperative, /*persist=*/true};

    // The connector-disable path queues its own change as persist == false. Seed it on a different connector, so
    // the predicate is exercised on both flags at once
    charge_point.change_availability_queue[2] = {AvailabilityType::Inoperative, /*persist=*/false};

    charge_point.clear_firmware_install_pending();

    ASSERT_EQ(charge_point.change_availability_queue.count(1), 1u);
    EXPECT_TRUE(charge_point.change_availability_queue.at(1).persist);
    EXPECT_EQ(charge_point.change_availability_queue.count(2), 0u);
}

/// \brief Fixture that also captures the message callback and the outgoing frames, so a test can complete the
/// boot handshake and inject incoming CSMS messages (incoming CALLs are only routed once Booted).
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

    /// \brief A minimal, schema-valid UpdateFirmware.req payload.
    static json update_firmware_payload() {
        json payload = json::object();
        payload["location"] = "ftp://example.com/firmware.bin";
        payload["retrieveDate"] = ocpp::DateTime().to_rfc3339();
        return payload;
    }

    /// \brief A minimal, schema-valid SignedUpdateFirmware.req payload. The certificate content is irrelevant,
    /// because the tests stub EvseSecurity::verify_certificate to decide whether it is accepted.
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

// An update that died without reporting a terminal status must not leave the guard latched for the next cycle
TEST_F(ChargePointUpdateFirmwareRequestTest, UpdateFirmwareRequestResetsSingleFireGuard) {
    auto& charge_point = start_charge_point();
    charge_point.register_update_firmware_callback([](const UpdateFirmwareRequest&) {});
    boot_charge_point(charge_point);
    if (::testing::Test::HasFatalFailure()) {
        return;
    }

    // Latch the guard, a repeated trigger shows it holds
    charge_point.on_firmware_update_status_notification(-1, FirmwareStatusNotification::InstallScheduled,
                                                        std::optional<bool>{true});
    charge_point.on_firmware_update_status_notification(-1, FirmwareStatusNotification::InstallScheduled,
                                                        std::optional<bool>{true});
    EXPECT_EQ(this->all_connectors_unavailable_count, 1);

    // The update dies without ever reporting a terminal status, then the CSMS requests a new update
    json update_firmware_call = json::array();
    update_firmware_call.push_back(MessageTypeId::CALL);
    update_firmware_call.push_back("update-firmware-request-1");
    update_firmware_call.push_back("UpdateFirmware");
    update_firmware_call.push_back(
        json{{"location", "ftp://example.com/firmware.bin"}, {"retrieveDate", ocpp::DateTime().to_rfc3339()}});
    this->message_callback(update_firmware_call.dump());

    // The request re-armed the guard, so a new opt-in trigger fires the callback again
    charge_point.on_firmware_update_status_notification(-1, FirmwareStatusNotification::InstallScheduled,
                                                        std::optional<bool>{true});
    EXPECT_EQ(this->all_connectors_unavailable_count, 2);
}

// Idle is not a terminal status, so nothing along the status path re-arms the guard when the updater dies and
// reports it. Only the next UpdateFirmware.req may
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

    // The update gives up and falls back to Idle, which must not re-arm the guard on its own because the cycle
    // is still the old one
    charge_point.on_firmware_update_status_notification(-1, FirmwareStatusNotification::Idle, std::nullopt);
    charge_point.on_firmware_update_status_notification(-1, FirmwareStatusNotification::InstallScheduled,
                                                        std::optional<bool>{true});
    EXPECT_EQ(this->all_connectors_unavailable_count, 1);

    // Only the next request starts a new cycle and re-arms the guard
    send_call("UpdateFirmware", update_firmware_payload(), "update-firmware-after-idle");

    charge_point.on_firmware_update_status_notification(-1, FirmwareStatusNotification::InstallScheduled,
                                                        std::optional<bool>{true});
    EXPECT_EQ(this->all_connectors_unavailable_count, 2);
}

// The signed variant of the request is the only way a SecurityExtensions firmware update is ever started
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

    // Latch the guard, then let the update die without ever reporting a terminal status
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

// A request answered with InvalidCertificate starts no new cycle, so it must not wipe the state of the update
// that is actually running. handleSignedUpdateFirmware only clears the pending install once the certificate has
// been verified
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

    // An update is running and has already notified once
    charge_point.on_firmware_update_status_notification(1, FirmwareStatusNotification::InstallScheduled,
                                                        std::optional<bool>{true});
    EXPECT_EQ(this->all_connectors_unavailable_count, 1);

    // The signing certificate does not validate, so the request is rejected and starts no new cycle
    send_call("SignedUpdateFirmware", signed_update_firmware_payload(2), "signed-update-firmware-invalid");

    // The still-running update notifies again, so the guard must still be latched from its first notification
    charge_point.on_firmware_update_status_notification(1, FirmwareStatusNotification::InstallScheduled,
                                                        std::optional<bool>{true});
    EXPECT_EQ(this->all_connectors_unavailable_count, 1);
}

// The reported firmware status is only put back to Idle on a terminal status, so after an update that aborts
// while Installing a TriggerMessage(FirmwareStatusNotification) would keep reporting the dead update's status
//
// This asserts the status field rather than the message it would end up in, because a self-initiated
// FirmwareStatusNotification.req goes through MessageQueue::push_call_async, which answers offline without ever
// handing it to the websocket while the queue is paused
TEST_F(ChargePointUpdateFirmwareRequestTest, NewUpdateFirmwareRequestResetsFirmwareStatusToIdle) {
    auto& charge_point = start_charge_point();
    charge_point.register_update_firmware_callback([](const UpdateFirmwareRequest&) {});
    boot_charge_point(charge_point);
    if (::testing::Test::HasFatalFailure()) {
        return;
    }

    // The update reaches Installing and then dies, so this stays the status the charge point would report
    charge_point.on_firmware_update_status_notification(-1, FirmwareStatusNotification::Installing, std::nullopt);
    ASSERT_EQ(charge_point.firmware_status, FirmwareStatus::Installing);

    // The new cycle has not reported anything yet, so the dead cycle's leftover Installing must not be what a
    // TriggerMessage.req is answered with
    send_call("UpdateFirmware", update_firmware_payload(), "update-firmware-after-abort");

    EXPECT_EQ(charge_point.firmware_status, FirmwareStatus::Idle);
}

} // namespace v16
} // namespace ocpp
