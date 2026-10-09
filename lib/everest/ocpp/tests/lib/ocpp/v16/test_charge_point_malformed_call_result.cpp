// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

/// \file test_charge_point_malformed_call_result.cpp
/// \brief Tests of CALLRESULT handling in ChargePointImpl.

#include "charge_point_test_base.hpp"

#include <map>

namespace ocpp {
namespace v16 {

namespace {
constexpr auto SETTLE_TIMEOUT = std::chrono::milliseconds(500);
// A BootNotification.req is repeated after DEFAULT_BOOT_NOTIFICATION_INTERVAL_S, BOOT_RETRY_TIMEOUT needs to be higher
constexpr auto BOOT_RETRY_TIMEOUT = std::chrono::seconds(90);
constexpr std::int32_t CONNECTOR = 1;
const std::string BOOT_NOTIFICATION_ACTION = "BootNotification";
const std::string START_TRANSACTION_ACTION = "StartTransaction";
const std::string STOP_TRANSACTION_ACTION = "StopTransaction";
// Transaction messages are repeated after TransactionMessageRetryInterval, which the fixture sets to one second.
// The test configuration allows a single attempt, so retries have to be enabled per test.
constexpr std::int32_t TRANSACTION_MESSAGE_RETRY_INTERVAL_S = 1;
constexpr std::int32_t TRANSACTION_MESSAGE_ATTEMPTS = 3;
constexpr auto TRANSACTION_RETRY_TIMEOUT = std::chrono::seconds(10);
const std::string SESSION_ID = "session-1";
const std::string ID_TAG = "TAG1";
constexpr std::int32_t TRANSACTION_ID = 42;
} // namespace

class ChargePointMalformedCallResultTest : public ChargePointTestBase {
protected:
    /// \brief Start a charge point and report the websocket as connected, which sends the first BootNotification.req.
    std::unique_ptr<ChargePointImpl> make_connected_charge_point() {
        auto charge_point = make_charge_point();
        charge_point->start({{0, ChargePointStatus::Available}, {CONNECTOR, ChargePointStatus::Available}},
                            BootReasonEnum::PowerUp, {});
        charge_point->on_websocket_connected(0, ocpp::v2::NetworkConnectionProfile{}, ocpp::OcppProtocolVersion::v16);
        return charge_point;
    }

    /// \brief payload for the given \p action . A payload staged with stage_payload() is used once, in staging order,
    /// before the well-formed default answer.
    nlohmann::json response_payload_for_locked(const std::string& action) override {
        auto staged = this->staged_payloads.find(action);
        if (staged != this->staged_payloads.end() and not staged->second.empty()) {
            auto payload = staged->second.front();
            staged->second.pop_front();
            return payload;
        }
        if (action == START_TRANSACTION_ACTION) {
            return nlohmann::json{{"idTagInfo", {{"status", "Accepted"}}}, {"transactionId", TRANSACTION_ID}};
        }
        return ChargePointTestBase::response_payload_for_locked(action);
    }

    /// \brief Wait until at least \p count CallResults arrived at the chargepoint.
    bool wait_for_delivered(std::size_t count) {
        std::unique_lock<std::mutex> lock(this->mtx);
        return this->cv.wait_for(lock, WAIT_TIMEOUT, [this, count]() { return this->delivered >= count; });
    }

    /// \brief Wait until a StatusNotification.req with the given \p status for the tested connector is received.
    /// A (short) \p timeout can be used to check for a missing StatusNotification.req
    bool wait_for_status(const std::string& status, std::chrono::milliseconds timeout = WAIT_TIMEOUT) {
        std::unique_lock<std::mutex> lock(this->mtx);
        return this->cv.wait_for(lock, timeout, [this, &status]() {
            for (const auto& call : this->sent) {
                if (call.at(2) == "StatusNotification" and call.at(3).at("connectorId") == CONNECTOR and
                    call.at(3).at("status") == status) {
                    return true;
                }
            }
            return false;
        });
    }

    std::vector<std::string> get_escaped_exceptions() {
        const std::lock_guard<std::mutex> lock(this->mtx);
        return this->escaped_exceptions;
    }

    /// \brief Answer the next \p action call with \p payload instead of the well-formed default. A null payload
    /// answers with a CALLERROR instead of a CALLRESULT.
    void stage_payload(const std::string& action, nlohmann::json payload) {
        const std::lock_guard<std::mutex> lock(this->mtx);
        this->staged_payloads[action].push_back(std::move(payload));
    }

    /// \brief Bring a charge point up to an accepted BootNotification with \p CONNECTOR available.
    std::unique_ptr<ChargePointImpl> make_booted_charge_point() {
        auto charge_point = make_connected_charge_point();
        EXPECT_TRUE(wait_for_status("Available"))
            << "boot handshake did not produce the initial StatusNotification.req";
        return charge_point;
    }

    /// \brief Let transaction messages be repeated quickly. Has to be called before the charge point is constructed.
    void enable_transaction_message_retries() {
        this->configuration->setTransactionMessageAttempts(TRANSACTION_MESSAGE_ATTEMPTS);
        this->configuration->setTransactionMessageRetryInterval(TRANSACTION_MESSAGE_RETRY_INTERVAL_S);
    }

    /// \brief Stop the charge point once every answer has been processed. The websocket joins its receive thread
    /// before disconnect() returns, so no message callback runs during teardown; the responder thread has no such
    /// coupling and has to be drained explicitly.
    void stop_charge_point(ChargePointImpl& charge_point) {
        EXPECT_TRUE(wait_for_all_answered()) << "not every answer was processed before stopping";
        charge_point.stop();
    }

    void start_transaction(ChargePointImpl& charge_point) {
        charge_point.on_session_started(CONNECTOR, SESSION_ID, ocpp::SessionStartedReason::EVConnected, std::nullopt);
        charge_point.on_transaction_started(CONNECTOR, SESSION_ID, ID_TAG, /*meter_start=*/0.0,
                                            /*reservation_id=*/std::nullopt, ocpp::DateTime(),
                                            /*signed_meter_value=*/std::nullopt);
    }

    void stop_transaction(ChargePointImpl& charge_point) {
        charge_point.on_transaction_stopped(CONNECTOR, SESSION_ID, Reason::Local, ocpp::DateTime(),
                                            /*energy_wh_import=*/1000.0F, /*id_tag_end=*/std::nullopt,
                                            /*signed_meter_value=*/std::nullopt,
                                            /*start_signed_meter_value=*/std::nullopt);
    }

    std::map<std::string, std::deque<nlohmann::json>> staged_payloads;
};

// Ensure that a malformed CALLRESULT does not throw a json type_error.
TEST_F(ChargePointMalformedCallResultTest, NumberPayloadDoesNotEscapeMessageCallback) {
    stage_payload(BOOT_NOTIFICATION_ACTION, 12345);

    auto charge_point = make_connected_charge_point();

    ASSERT_TRUE(wait_for_action_count(BOOT_NOTIFICATION_ACTION, 1, WAIT_TIMEOUT)) << "no BootNotification.req was sent";
    ASSERT_TRUE(wait_for_delivered(1)) << "the malformed CALLRESULT was not delivered";

    EXPECT_THAT(get_escaped_exceptions(), ::testing::IsEmpty());

    stop_charge_point(*charge_point);
}

// After a malformed answer the BootNotification.req is unanswered from the charge point's point of view: it has neither
// a registration status nor an interval. The charge point has to repeat it, and once the CSMS answers properly the boot
// completes with the initial StatusNotification.req.
TEST_F(ChargePointMalformedCallResultTest, NumberPayloadIsFollowedByBootNotificationRetry) {
    stage_payload(BOOT_NOTIFICATION_ACTION, 12345);

    auto charge_point = make_connected_charge_point();

    ASSERT_TRUE(wait_for_action_count(BOOT_NOTIFICATION_ACTION, 1, WAIT_TIMEOUT)) << "no BootNotification.req was sent";
    ASSERT_TRUE(wait_for_delivered(1)) << "the malformed CALLRESULT was not delivered";
    ASSERT_THAT(get_escaped_exceptions(), ::testing::IsEmpty());

    // The charge point must not accept the malformed answer as a registration.
    EXPECT_FALSE(wait_for_status("Available", SETTLE_TIMEOUT))
        << "a StatusNotification.req was sent without an accepted boot";

    EXPECT_TRUE(wait_for_action_count(BOOT_NOTIFICATION_ACTION, 2, BOOT_RETRY_TIMEOUT))
        << "the BootNotification.req was not repeated after the malformed CALLRESULT";
    EXPECT_TRUE(wait_for_status("Available")) << "the repeated BootNotification.req did not complete the boot";

    stop_charge_point(*charge_point);
}

// The unhappy path must not disturb an ordinary boot: with a proper BootNotification.conf the same fixture reaches
// Available.
TEST_F(ChargePointMalformedCallResultTest, WellFormedPayloadBoots) {
    auto charge_point = make_connected_charge_point();

    EXPECT_TRUE(wait_for_status("Available")) << "boot handshake did not produce the initial StatusNotification.req";
    EXPECT_THAT(get_escaped_exceptions(), ::testing::IsEmpty());
    EXPECT_EQ(count_action(BOOT_NOTIFICATION_ACTION), 1u);

    stop_charge_point(*charge_point);
}

// A transaction with well-formed answers completes with a single StartTransaction.req and StopTransaction.req.
TEST_F(ChargePointMalformedCallResultTest, WellFormedTransactionRoundTrip) {
    enable_transaction_message_retries();

    auto charge_point = make_booted_charge_point();
    start_transaction(*charge_point);

    ASSERT_TRUE(wait_for_action_count(START_TRANSACTION_ACTION, 1, WAIT_TIMEOUT)) << "no StartTransaction.req was sent";
    ASSERT_TRUE(wait_for_all_answered()) << "the StartTransaction.conf was not processed";

    stop_transaction(*charge_point);

    ASSERT_TRUE(wait_for_action_count(STOP_TRANSACTION_ACTION, 1, WAIT_TIMEOUT)) << "no StopTransaction.req was sent";
    ASSERT_TRUE(wait_for_all_answered()) << "the StopTransaction.conf was not processed";
    EXPECT_EQ(count_action(START_TRANSACTION_ACTION), 1u);
    EXPECT_EQ(count_action(STOP_TRANSACTION_ACTION), 1u);
    EXPECT_THAT(get_escaped_exceptions(), ::testing::IsEmpty());

    stop_charge_point(*charge_point);
}

// A genuine CALLERROR for a StopTransaction.req takes the retry path without involving CALLRESULT validation.
TEST_F(ChargePointMalformedCallResultTest, CallErrorForStopTransactionIsRetried) {
    enable_transaction_message_retries();
    stage_payload(STOP_TRANSACTION_ACTION, nullptr);

    auto charge_point = make_booted_charge_point();
    start_transaction(*charge_point);

    ASSERT_TRUE(wait_for_action_count(START_TRANSACTION_ACTION, 1, WAIT_TIMEOUT)) << "no StartTransaction.req was sent";
    ASSERT_TRUE(wait_for_all_answered()) << "the StartTransaction.conf was not processed";

    stop_transaction(*charge_point);

    ASSERT_TRUE(wait_for_action_count(STOP_TRANSACTION_ACTION, 1, WAIT_TIMEOUT)) << "no StopTransaction.req was sent";
    EXPECT_TRUE(wait_for_action_count(STOP_TRANSACTION_ACTION, 2, TRANSACTION_RETRY_TIMEOUT))
        << "the StopTransaction.req was not repeated after the CALLERROR";
    EXPECT_THAT(get_escaped_exceptions(), ::testing::IsEmpty());

    stop_charge_point(*charge_point);
}

// A StartTransaction.conf whose payload is a bare number carries no transactionId. Like a CALLERROR, it must lead to
// the StartTransaction.req being repeated, and the transactionId from the repeated exchange has to reach the
// StopTransaction.req.
TEST_F(ChargePointMalformedCallResultTest, NumberPayloadForStartTransactionIsRetried) {
    enable_transaction_message_retries();
    stage_payload(START_TRANSACTION_ACTION, 12345);

    auto charge_point = make_booted_charge_point();
    start_transaction(*charge_point);

    ASSERT_TRUE(wait_for_action_count(START_TRANSACTION_ACTION, 1, WAIT_TIMEOUT)) << "no StartTransaction.req was sent";
    EXPECT_TRUE(wait_for_action_count(START_TRANSACTION_ACTION, 2, TRANSACTION_RETRY_TIMEOUT))
        << "the StartTransaction.req was not repeated after the malformed CALLRESULT";
    ASSERT_TRUE(wait_for_all_answered()) << "not every call was answered";
    EXPECT_THAT(get_escaped_exceptions(), ::testing::IsEmpty());

    stop_transaction(*charge_point);

    ASSERT_TRUE(wait_for_action_count(STOP_TRANSACTION_ACTION, 1, WAIT_TIMEOUT)) << "no StopTransaction.req was sent";
    const auto stop_payload = last_payload(STOP_TRANSACTION_ACTION);
    ASSERT_TRUE(stop_payload.has_value());
    EXPECT_EQ(stop_payload->at("transactionId"), TRANSACTION_ID)
        << "the StopTransaction.req does not carry the transactionId of the repeated StartTransaction.conf";
    EXPECT_THAT(get_escaped_exceptions(), ::testing::IsEmpty());

    stop_charge_point(*charge_point);
}

// A StopTransaction.conf whose payload is a bare number must lead to the StopTransaction.req being repeated; dropping
// it silently would lose the end of the transaction on the CSMS side.
TEST_F(ChargePointMalformedCallResultTest, NumberPayloadForStopTransactionIsRetried) {
    enable_transaction_message_retries();
    stage_payload(STOP_TRANSACTION_ACTION, 12345);

    auto charge_point = make_booted_charge_point();
    start_transaction(*charge_point);

    ASSERT_TRUE(wait_for_action_count(START_TRANSACTION_ACTION, 1, WAIT_TIMEOUT)) << "no StartTransaction.req was sent";
    ASSERT_TRUE(wait_for_all_answered()) << "the StartTransaction.conf was not processed";

    stop_transaction(*charge_point);

    ASSERT_TRUE(wait_for_action_count(STOP_TRANSACTION_ACTION, 1, WAIT_TIMEOUT)) << "no StopTransaction.req was sent";
    EXPECT_TRUE(wait_for_action_count(STOP_TRANSACTION_ACTION, 2, TRANSACTION_RETRY_TIMEOUT))
        << "the StopTransaction.req was not repeated after the malformed CALLRESULT";
    EXPECT_THAT(get_escaped_exceptions(), ::testing::IsEmpty());

    stop_charge_point(*charge_point);
}

} // namespace v16
} // namespace ocpp
