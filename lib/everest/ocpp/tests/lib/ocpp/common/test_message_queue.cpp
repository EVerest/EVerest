// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <ocpp/common/message_queue.hpp>
#include <ocpp/common/ocpp_logging.hpp>

namespace ocpp {

using json = nlohmann::json;

/************************************************************************************************
 * Test Message Types
 */

enum class TestMessageType {
    TRANSACTIONAL,
    TRANSACTIONAL_RESPONSE,
    TRANSACTIONAL_UPDATE,
    TRANSACTIONAL_UPDATE_RESPONSE,
    NON_TRANSACTIONAL,
    NON_TRANSACTIONAL_RESPONSE,
    InternalError,
    BootNotification,
    BootNotificationResponse
};

static std::string to_string(TestMessageType m) {
    switch (m) {
    case TestMessageType::TRANSACTIONAL:
        return "transactional";
    case TestMessageType::TRANSACTIONAL_RESPONSE:
        return "transactionalResponse";
    case TestMessageType::TRANSACTIONAL_UPDATE:
        return "transactional_update";
    case TestMessageType::TRANSACTIONAL_UPDATE_RESPONSE:
        return "transactional_updateResponse";
    case TestMessageType::NON_TRANSACTIONAL:
        return "non_transactional";
    case TestMessageType::NON_TRANSACTIONAL_RESPONSE:
        return "non_transactionalResponse";
    case TestMessageType::InternalError:
        return "internal_error";
    case TestMessageType::BootNotification:
        return "boot_notification";
    case TestMessageType::BootNotificationResponse:
        return "boot_notificationResponse";
    }
    throw std::out_of_range("unknown TestMessageType");
};

static TestMessageType to_test_message_type(const std::string& s) {
    if (s == "transactional") {
        return TestMessageType::TRANSACTIONAL;
    }
    if (s == "transactionalResponse") {
        return TestMessageType::TRANSACTIONAL_RESPONSE;
    }
    if (s == "transactional_update") {
        return TestMessageType::TRANSACTIONAL_UPDATE;
    }
    if (s == "transactional_updateResponse") {
        return TestMessageType::TRANSACTIONAL_UPDATE_RESPONSE;
    }
    if (s == "non_transactional") {
        return TestMessageType::NON_TRANSACTIONAL;
    }
    if (s == "non_transactionalResponse") {
        return TestMessageType::NON_TRANSACTIONAL_RESPONSE;
    }
    if (s == "internal_error") {
        return TestMessageType::InternalError;
    }
    if (s == "boot_notification") {
        return TestMessageType::BootNotification;
    }
    if (s == "boot_notificationResponse") {
        return TestMessageType::BootNotificationResponse;
    }
    throw std::out_of_range("unknown string for TestMessageType");
};

struct TestRequest : Message {
    TestMessageType type = TestMessageType::NON_TRANSACTIONAL;
    std::optional<std::string> data;
    std::string get_type() const {
        return to_string(type);
    };
};

void to_json(json& j, const TestRequest& k) {
    j = json{};
    if (k.data) {
        j["data"] = k.data.value();
    }
}

void from_json(const json& j, TestRequest& k) {
    if (j.contains("data")) {
        k.data.emplace(j.at("data"));
    }
}

template <> std::string MessageQueue<TestMessageType>::messagetype_to_string(TestMessageType m) {
    return to_string(m);
}

template <> TestMessageType MessageQueue<TestMessageType>::string_to_messagetype(const std::string& s) {
    return to_test_message_type(s);
}

template <> ControlMessage<TestMessageType>::ControlMessage(const json& message, bool stall_until_accepted) {
    this->message = message.get<json::array_t>();
    EVLOG_info << this->message;
    this->messageType = to_test_message_type(this->message[2]);
    this->message_attempts = 0;
    this->initial_unique_id = this->message[MESSAGE_ID];
    this->stall_until_accepted = stall_until_accepted;
}

std::ostream& operator<<(std::ostream& os, const TestMessageType& message_type) {
    os << to_string(message_type);
    return os;
};

bool is_transaction_message(const TestMessageType message_type) {
    return (message_type == TestMessageType::TRANSACTIONAL) || (message_type == TestMessageType::TRANSACTIONAL_UPDATE);
}

bool is_start_transaction_message(const TestMessageType message_type) {
    return false;
}

template <> bool ControlMessage<TestMessageType>::is_transaction_update_message() const {
    return this->messageType == TestMessageType::TRANSACTIONAL_UPDATE;
}

bool is_boot_notification_message(const TestMessageType message_type) {
    return message_type == TestMessageType::BootNotification;
}

/************************************************************************************************
 * MessageQueueTest
 */

class DatabaseHandlerBaseMock : public common::DatabaseHandlerCommon {
private:
    void init_sql() override {
    }

public:
    DatabaseHandlerBaseMock() : common::DatabaseHandlerCommon(nullptr, "", 1) {
    }

    MOCK_METHOD(std::vector<common::DBTransactionMessage>, get_message_queue_messages, (const QueueType), (override));
    MOCK_METHOD(void, insert_message_queue_message, (const common::DBTransactionMessage&, const QueueType), (override));
    MOCK_METHOD(void, remove_message_queue_message, (const std::string&, const QueueType), (override));
};

class MessageQueueTest : public ::testing::Test {
    int internal_message_count{0};
    int call_count{0};
    int responses_scheduled{0};
    int responses_delivered{0};

protected:
    MessageQueueConfig<TestMessageType> config{};
    std::shared_ptr<DatabaseHandlerBaseMock> db;
    std::mutex call_marker_mutex;
    std::condition_variable call_marker_cond_var;
    testing::MockFunction<bool(json message)> send_callback_mock;
    Everest::SteadyTimer reception_timer;
    std::unique_ptr<MessageQueue<TestMessageType>> message_queue;

    int get_call_count() {
        std::lock_guard<std::mutex> lock(call_marker_mutex);
        return call_count;
    }

    /// \brief Increments call_count and notifies the condition variable.
    /// Use this from custom Invoke lambdas instead of accessing call_count directly.
    void mark_call_sent() {
        std::lock_guard<std::mutex> lock(call_marker_mutex);
        this->call_count++;
        this->call_marker_cond_var.notify_one();
    }

    /// \brief Answers \p message with an empty CALLRESULT and counts it as sent
    bool answer_and_mark(const json& message) {
        {
            std::lock_guard<std::mutex> lock(call_marker_mutex);
            this->responses_scheduled++;
        }
        const std::string message_id = message.at(1);
        reception_timer.timeout(
            [this, message_id]() {
                this->message_queue->receive(json{3, message_id, json::object()}.dump());
                std::lock_guard<std::mutex> lock(call_marker_mutex);
                this->responses_delivered++;
                this->call_marker_cond_var.notify_all();
            },
            std::chrono::milliseconds(0));
        mark_call_sent();
        return true;
    }

    template <typename R> auto MarkAndReturn(R value, bool respond = false) {
        return testing::Invoke([this, value, respond](const json::array_t& s) -> R {
            if (respond) {
                {
                    std::lock_guard<std::mutex> lock(call_marker_mutex);
                    this->responses_scheduled++;
                }
                reception_timer.timeout(
                    [this, s]() {
                        this->message_queue->receive(json{3, s[1], json::object()}.dump());
                        std::lock_guard<std::mutex> lock(call_marker_mutex);
                        this->responses_delivered++;
                        this->call_marker_cond_var.notify_all();
                    },
                    std::chrono::milliseconds(0));
            }
            std::lock_guard<std::mutex> lock(call_marker_mutex);
            this->call_count++;
            this->call_marker_cond_var.notify_all();
            return value;
        });
    }

    void wait_for_calls(int expected_calls = 1, std::chrono::seconds timeout = std::chrono::seconds(3)) {
        std::unique_lock<std::mutex> lock(call_marker_mutex);
        EXPECT_TRUE(call_marker_cond_var.wait_for(
            lock, timeout, [this, expected_calls] { return this->call_count >= expected_calls; }));
    }

    std::string push_message_call(const TestMessageType& message_type) {
        std::stringstream stream;
        stream << "test_call_" << internal_message_count;
        std::string unique_identifier = stream.str();
        internal_message_count++;
        return push_message_call(message_type, unique_identifier);
    }

    std::string push_message_call(const TestMessageType& message_type, const std::string& identifier) {
        Call<TestRequest> call;
        call.msg.type = message_type;
        call.msg.data = identifier;
        call.uniqueId = identifier;
        message_queue->push_call(call);
        return identifier;
    }

    void init_message_queue() {
        message_queue = std::make_unique<MessageQueue<TestMessageType>>(send_callback_mock.AsStdFunction(), config, db);
        message_queue->start();
        message_queue->set_registration_status_accepted();
        message_queue->resume(std::chrono::seconds(0));
    }

    void restart_message_queue() {
        if (message_queue) {
            message_queue->stop();
        }
        message_queue = std::make_unique<MessageQueue<TestMessageType>>(send_callback_mock.AsStdFunction(), config, db);
        message_queue->start();
        message_queue->set_registration_status_accepted();
        message_queue->resume(std::chrono::seconds(0));
    }

    void SetUp() override {
        call_count = 0;
        config = MessageQueueConfig<TestMessageType>{1, 1, 2, false};
        db = std::make_shared<DatabaseHandlerBaseMock>();
        init_message_queue();
    }

    void TearDown() override {
        // Let reception_timer deliver all pending CallResults before the queue stops
        {
            std::unique_lock<std::mutex> lock(call_marker_mutex);
            std::ignore = call_marker_cond_var.wait_for(lock, std::chrono::seconds(3), [this] {
                return this->responses_delivered == this->responses_scheduled;
            });
        }
        message_queue->stop();
    };
};

// \brief Test transmission priority decisions before and after registration is accepted
TEST(MessageTransmissionPriorityTest, test_non_transactional_discarded_before_registration) {
    EXPECT_EQ(get_message_transmission_priority(false, false, false, false, false),
              MessageTransmissionPriority::Discard);
}

TEST(MessageTransmissionPriorityTest, test_sent_immediately_after_registration) {
    EXPECT_EQ(get_message_transmission_priority(false, false, true, false, false),
              MessageTransmissionPriority::SendImmediately);
    EXPECT_EQ(get_message_transmission_priority(false, false, true, false, false, true),
              MessageTransmissionPriority::SendImmediately);
}

TEST(MessageTransmissionPriorityTest, test_transactional_queued_before_registration) {
    EXPECT_EQ(get_message_transmission_priority(false, false, false, true, false),
              MessageTransmissionPriority::SendAfterRegistrationStatusAccepted);
}

TEST(MessageTransmissionPriorityTest, test_queue_all_messages_queued_before_registration) {
    EXPECT_EQ(get_message_transmission_priority(false, false, false, false, true),
              MessageTransmissionPriority::SendAfterRegistrationStatusAccepted);
}

TEST(MessageTransmissionPriorityTest, test_queue_until_accepted_queued_before_registration) {
    EXPECT_EQ(get_message_transmission_priority(false, false, false, false, false, true),
              MessageTransmissionPriority::SendAfterRegistrationStatusAccepted);
}

// \brief Test that a non-transactional message pushed with stall_until_accepted while the queue is paused (before the
// first connection) is kept and sent once the queue is resumed and registration is accepted
TEST_F(MessageQueueTest, test_stall_until_accepted_message_pushed_while_paused_is_sent_after_accepted) {
    message_queue->stop();
    message_queue = std::make_unique<MessageQueue<TestMessageType>>(send_callback_mock.AsStdFunction(), config, db);
    message_queue->start();

    EXPECT_CALL(send_callback_mock, Call(json{2, "0", "non_transactional", json{{"data", "test_data"}}}))
        .WillOnce(MarkAndReturn(true));

    Call<TestRequest> call;
    call.msg.type = TestMessageType::NON_TRANSACTIONAL;
    call.msg.data = "test_data";
    call.uniqueId = "0";
    message_queue->push_call(call, true);

    message_queue->resume(std::chrono::seconds(0));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(0, get_call_count());

    message_queue->set_registration_status_accepted();
    wait_for_calls(1);
}

/// \brief Records the unique ids of the CALLs the queue sends, in order, and answers each of them.
class MessageQueueResetRegistrationTest : public MessageQueueTest {
protected:
    std::mutex sent_ids_mutex;
    std::vector<std::string> sent_ids;

    void SetUp() override {
        MessageQueueTest::SetUp();
        EXPECT_CALL(*db, insert_message_queue_message(testing::_, testing::_)).Times(testing::AnyNumber());
        EXPECT_CALL(*db, remove_message_queue_message(testing::_, testing::_)).Times(testing::AnyNumber());
        mark_and_answer = MarkAndReturn(true, true);
        EXPECT_CALL(send_callback_mock, Call(testing::_)).WillRepeatedly(testing::Invoke([this](const json& message) {
            {
                const std::lock_guard<std::mutex> lock(sent_ids_mutex);
                sent_ids.push_back(message.at(1).get<std::string>());
            }
            return mark_and_answer.Perform(std::make_tuple(message));
        }));
    }

    testing::Action<bool(json)> mark_and_answer;

    std::vector<std::string> get_sent_ids() {
        const std::lock_guard<std::mutex> lock(sent_ids_mutex);
        return sent_ids;
    }

    void expect_nothing_sent() {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        EXPECT_EQ(0, get_call_count());
    }
};

// \brief Messages queued while the registration was accepted are held after reset_registration_status() and sent in
// their original order once the registration is accepted again
TEST_F(MessageQueueResetRegistrationTest, test_normal_messages_queued_while_accepted_are_held) {
    config.queue_all_messages = true;
    restart_message_queue();

    message_queue->pause();
    push_message_call(TestMessageType::NON_TRANSACTIONAL, "first");
    push_message_call(TestMessageType::NON_TRANSACTIONAL, "second");
    message_queue->reset_registration_status();
    message_queue->resume(std::chrono::seconds(0));
    expect_nothing_sent();

    message_queue->set_registration_status_accepted();
    wait_for_calls(2);
    EXPECT_EQ(get_sent_ids(), (std::vector<std::string>{"first", "second"}));
}

// \brief Transaction messages queued while the registration was accepted are held as well and keep their order
TEST_F(MessageQueueResetRegistrationTest, test_transaction_messages_queued_while_accepted_are_held) {
    message_queue->pause();
    push_message_call(TestMessageType::TRANSACTIONAL, "started");
    push_message_call(TestMessageType::TRANSACTIONAL_UPDATE, "updated");
    push_message_call(TestMessageType::TRANSACTIONAL, "ended");
    message_queue->reset_registration_status();
    message_queue->resume(std::chrono::seconds(0));
    expect_nothing_sent();

    message_queue->set_registration_status_accepted();
    wait_for_calls(3);
    EXPECT_EQ(get_sent_ids(), (std::vector<std::string>{"started", "updated", "ended"}));
}

// \brief A message that is only sent because the charging station is registered is held when it reaches the queue
// after reset_registration_status(): its sender may have checked the registration just before the reset
TEST_F(MessageQueueResetRegistrationTest, test_message_requiring_registration_pushed_after_reset_is_held) {
    message_queue->reset_registration_status();
    Call<TestRequest> call;
    call.msg.type = TestMessageType::NON_TRANSACTIONAL;
    call.uniqueId = "late";
    message_queue->push_call(call, false, true);
    expect_nothing_sent();

    message_queue->set_registration_status_accepted();
    wait_for_calls(1);
    EXPECT_EQ(get_sent_ids(), (std::vector<std::string>{"late"}));
}

// \brief A message that does not depend on the registration, such as one requested by TriggerMessage, is still sent
// while the registration is not accepted
TEST_F(MessageQueueResetRegistrationTest, test_message_not_requiring_registration_is_sent_after_reset) {
    message_queue->reset_registration_status();
    push_message_call(TestMessageType::NON_TRANSACTIONAL, "triggered");
    wait_for_calls(1);
    EXPECT_EQ(get_sent_ids(), (std::vector<std::string>{"triggered"}));
}

// \brief A message requiring registration is not kept for later when the queue is offline and does not queue it
TEST_F(MessageQueueResetRegistrationTest, test_message_requiring_registration_is_dropped_while_offline) {
    message_queue->pause();
    Call<TestRequest> call;
    call.msg.type = TestMessageType::NON_TRANSACTIONAL;
    call.uniqueId = "offline";
    message_queue->push_call(call, false, true);
    message_queue->resume(std::chrono::seconds(0));
    expect_nothing_sent();
}

// \brief A message sent with a future that is only sent because the charging station is registered is answered as
// offline when it reaches the queue after reset_registration_status(), as its dispatcher does while not registered
TEST_F(MessageQueueResetRegistrationTest, test_async_message_requiring_registration_pushed_after_reset_is_offline) {
    message_queue->reset_registration_status();
    Call<TestRequest> call;
    call.msg.type = TestMessageType::NON_TRANSACTIONAL;
    call.uniqueId = "late";
    auto future = message_queue->push_call_async(call, true);

    ASSERT_EQ(future.wait_for(std::chrono::seconds(1)), std::future_status::ready);
    EXPECT_TRUE(future.get().offline);
    expect_nothing_sent();
}

// \brief A BootNotification is not held by reset_registration_status() and goes out before the held messages
TEST_F(MessageQueueResetRegistrationTest, test_boot_notification_is_sent_while_held_messages_wait) {
    config.queue_all_messages = true;
    restart_message_queue();

    message_queue->pause();
    push_message_call(TestMessageType::NON_TRANSACTIONAL, "held");
    message_queue->reset_registration_status();
    push_message_call(TestMessageType::BootNotification, "boot");
    message_queue->resume(std::chrono::seconds(0));
    wait_for_calls(1);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_EQ(get_sent_ids(), (std::vector<std::string>{"boot"}));

    message_queue->set_registration_status_accepted();
    wait_for_calls(2);
    EXPECT_EQ(get_sent_ids(), (std::vector<std::string>{"boot", "held"}));
}

// \brief A BootNotification left unanswered by a lost connection goes out again as soon as the queue resumes on the
// next connection, instead of after the retry interval meant for a CSMS that does not answer
TEST_F(MessageQueueTest, test_boot_notification_in_flight_when_paused_is_resent_on_resume) {
    std::mutex ids_mutex;
    std::vector<std::string> sent_ids;
    EXPECT_CALL(send_callback_mock, Call(testing::_))
        .Times(2)
        .WillRepeatedly(testing::Invoke([this, &ids_mutex, &sent_ids](const json& message) {
            {
                const std::lock_guard<std::mutex> lock(ids_mutex);
                sent_ids.push_back(message.at(1));
            }
            mark_call_sent();
            return true;
        }));

    push_message_call(TestMessageType::BootNotification, "boot");
    wait_for_calls(1);

    message_queue->pause();
    message_queue->resume(std::chrono::seconds(0));
    wait_for_calls(2);

    const std::lock_guard<std::mutex> lock(ids_mutex);
    ASSERT_EQ(sent_ids.size(), 2);
    EXPECT_NE(sent_ids.at(1), "boot") << "the BootNotification was resent with the message id of the lost one";
}

// \brief Only one BootNotification waits in the queue: a new one replaces the one queued before
TEST_F(MessageQueueTest, test_boot_notification_replaces_queued_boot_notification) {
    std::mutex ids_mutex;
    std::vector<std::string> sent_ids;
    EXPECT_CALL(send_callback_mock, Call(testing::_))
        .WillRepeatedly(testing::Invoke([this, &ids_mutex, &sent_ids](const json& message) {
            {
                const std::lock_guard<std::mutex> lock(ids_mutex);
                sent_ids.push_back(message.at(1));
            }
            return answer_and_mark(message);
        }));

    message_queue->pause();
    push_message_call(TestMessageType::BootNotification, "first");
    push_message_call(TestMessageType::BootNotification, "second");
    message_queue->resume(std::chrono::seconds(0));
    wait_for_calls(1);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    const std::lock_guard<std::mutex> lock(ids_mutex);
    EXPECT_EQ(sent_ids, (std::vector<std::string>{"second"}));
}

// \brief A BootNotification pushed after a lost connection replaces the unanswered one the queue put back, so the
// next connection gets a single BootNotification
TEST_F(MessageQueueTest, test_boot_notification_replaces_boot_notification_lost_with_connection) {
    std::mutex ids_mutex;
    std::vector<std::string> sent_ids;
    EXPECT_CALL(send_callback_mock, Call(testing::_))
        .WillRepeatedly(testing::Invoke([this, &ids_mutex, &sent_ids](const json& message) {
            {
                const std::lock_guard<std::mutex> lock(ids_mutex);
                sent_ids.push_back(message.at(1));
                if (sent_ids.size() == 1) {
                    mark_call_sent();
                    return true;
                }
            }
            return answer_and_mark(message);
        }));

    push_message_call(TestMessageType::BootNotification, "lost");
    wait_for_calls(1);
    message_queue->pause();
    push_message_call(TestMessageType::BootNotification, "again");
    message_queue->resume(std::chrono::seconds(0));
    wait_for_calls(2);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    const std::lock_guard<std::mutex> lock(ids_mutex);
    EXPECT_EQ(sent_ids, (std::vector<std::string>{"lost", "again"}));
}

// \brief Without a BootNotification the charging station never registers and holds every queued message, so with
// QueueAllMessages a BootNotification lost with the connection is not dropped once the message attempts are used up
TEST_F(MessageQueueTest, test_persisted_boot_notification_lost_with_connection_is_never_dropped) {
    config.queue_all_messages = true;
    config.transaction_message_attempts = 2;
    message_queue->stop();
    EXPECT_CALL(*db, insert_message_queue_message(testing::_, QueueType::Normal));
    message_queue = std::make_unique<MessageQueue<TestMessageType>>(send_callback_mock.AsStdFunction(), config, db);
    message_queue->start();
    message_queue->set_registration_status_accepted();
    message_queue->resume(std::chrono::seconds(0));
    EXPECT_CALL(send_callback_mock, Call(testing::_)).Times(4).WillRepeatedly(MarkAndReturn(true));
    EXPECT_CALL(*db, remove_message_queue_message(testing::_, testing::_)).Times(0);

    push_message_call(TestMessageType::BootNotification, "boot");
    for (int sent = 1; sent < 4; sent++) {
        wait_for_calls(sent);
        message_queue->pause();
        message_queue->resume(std::chrono::seconds(0));
    }
    wait_for_calls(4);
}

// \brief With QueueAllMessages a BootNotification answered with CALLERROR is retried after the BootNotification retry
// interval instead of being dropped once the message attempts are used up
TEST_F(MessageQueueTest, test_persisted_boot_notification_answered_with_callerror_is_never_dropped) {
    config.queue_all_messages = true;
    config.transaction_message_attempts = 1;
    config.boot_notification_retry_interval_seconds = 0;
    message_queue->stop();
    EXPECT_CALL(*db, insert_message_queue_message(testing::_, QueueType::Normal));
    message_queue = std::make_unique<MessageQueue<TestMessageType>>(send_callback_mock.AsStdFunction(), config, db);
    message_queue->start();
    message_queue->set_registration_status_accepted();
    message_queue->resume(std::chrono::seconds(0));
    std::mutex ids_mutex;
    std::vector<std::string> sent_ids;
    EXPECT_CALL(send_callback_mock, Call(testing::_))
        .Times(2)
        .WillRepeatedly(testing::Invoke([this, &ids_mutex, &sent_ids](const json& message) {
            {
                const std::lock_guard<std::mutex> lock(ids_mutex);
                sent_ids.push_back(message.at(1));
            }
            mark_call_sent();
            return true;
        }));
    EXPECT_CALL(*db, remove_message_queue_message(testing::_, testing::_)).Times(0);

    push_message_call(TestMessageType::BootNotification, "boot");
    wait_for_calls(1);
    message_queue->receive(json{4, "boot", "InternalError", "", json::object()}.dump());
    wait_for_calls(2);
}

// \brief With QueueAllMessages a replaced BootNotification is also removed from the database, so it is not loaded again
// after a restart
TEST_F(MessageQueueTest, test_replaced_persisted_boot_notification_is_removed_from_database) {
    config.queue_all_messages = true;
    message_queue->stop();
    EXPECT_CALL(*db, insert_message_queue_message(testing::_, QueueType::Normal)).Times(2);
    EXPECT_CALL(*db, remove_message_queue_message("first", QueueType::Normal));
    message_queue = std::make_unique<MessageQueue<TestMessageType>>(send_callback_mock.AsStdFunction(), config, db);
    message_queue->start();

    push_message_call(TestMessageType::BootNotification, "first");
    push_message_call(TestMessageType::BootNotification, "second");
}

// \brief A BootNotification waiting for its retry after a CALLERROR goes out first thing on the next connection, like
// one that was in flight when the connection was lost
TEST_F(MessageQueueTest, test_boot_notification_waiting_for_retry_is_sent_on_next_connection) {
    config.boot_notification_retry_interval_seconds = 60;
    restart_message_queue();
    EXPECT_CALL(send_callback_mock, Call(testing::_)).Times(2).WillRepeatedly(MarkAndReturn(true));

    push_message_call(TestMessageType::BootNotification, "boot");
    wait_for_calls(1);
    message_queue->receive(json{4, "boot", "InternalError", "", json::object()}.dump());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(get_call_count(), 1) << "the BootNotification was retried before its retry interval";

    message_queue->pause();
    message_queue->resume(std::chrono::seconds(0));
    wait_for_calls(2);
}

// \brief A BootNotification in flight when a newer one is pushed is not retried after a CALLERROR: the newer one
// replaces it, so the CSMS receives each BootNotification at most once
TEST_F(MessageQueueTest, test_boot_notification_in_flight_is_not_retried_when_a_newer_one_is_queued) {
    config.boot_notification_retry_interval_seconds = 0;
    restart_message_queue();
    std::mutex ids_mutex;
    std::vector<std::string> sent_ids;
    EXPECT_CALL(send_callback_mock, Call(testing::_))
        .WillRepeatedly(testing::Invoke([this, &ids_mutex, &sent_ids](const json& message) {
            {
                const std::lock_guard<std::mutex> lock(ids_mutex);
                sent_ids.push_back(message.at(1));
                if (sent_ids.size() == 1) {
                    mark_call_sent();
                    return true;
                }
            }
            return answer_and_mark(message);
        }));

    push_message_call(TestMessageType::BootNotification, "first");
    wait_for_calls(1);
    push_message_call(TestMessageType::BootNotification, "second");
    message_queue->receive(json{4, "first", "InternalError", "", json::object()}.dump());
    wait_for_calls(2);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    const std::lock_guard<std::mutex> lock(ids_mutex);
    EXPECT_EQ(sent_ids, (std::vector<std::string>{"first", "second"}));
}

// \brief A BootNotification in flight when a newer one is pushed is not resent after the connection is lost: the newer
// one replaces it on the next connection
TEST_F(MessageQueueTest, test_boot_notification_in_flight_is_not_resent_on_reconnect_when_a_newer_one_is_queued) {
    std::mutex ids_mutex;
    std::vector<std::string> sent_ids;
    EXPECT_CALL(send_callback_mock, Call(testing::_))
        .WillRepeatedly(testing::Invoke([this, &ids_mutex, &sent_ids](const json& message) {
            {
                const std::lock_guard<std::mutex> lock(ids_mutex);
                sent_ids.push_back(message.at(1));
                if (sent_ids.size() == 1) {
                    mark_call_sent();
                    return true;
                }
            }
            return answer_and_mark(message);
        }));

    push_message_call(TestMessageType::BootNotification, "first");
    wait_for_calls(1);
    push_message_call(TestMessageType::BootNotification, "second");
    message_queue->pause();
    message_queue->resume(std::chrono::seconds(0));
    wait_for_calls(2);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    const std::lock_guard<std::mutex> lock(ids_mutex);
    EXPECT_EQ(sent_ids, (std::vector<std::string>{"first", "second"}));
}

// \brief With QueueAllMessages a BootNotification in flight that a newer one replaces is removed from the database once
// the connection is lost
TEST_F(MessageQueueTest, test_replaced_persisted_boot_notification_in_flight_is_removed_from_database) {
    config.queue_all_messages = true;
    message_queue->stop();
    EXPECT_CALL(*db, insert_message_queue_message(testing::_, QueueType::Normal)).Times(2);
    EXPECT_CALL(*db, remove_message_queue_message("first", QueueType::Normal));
    message_queue = std::make_unique<MessageQueue<TestMessageType>>(send_callback_mock.AsStdFunction(), config, db);
    message_queue->start();
    message_queue->set_registration_status_accepted();
    message_queue->resume(std::chrono::seconds(0));
    EXPECT_CALL(send_callback_mock, Call(testing::_)).WillOnce(MarkAndReturn(true));

    push_message_call(TestMessageType::BootNotification, "first");
    wait_for_calls(1);
    push_message_call(TestMessageType::BootNotification, "second");
    message_queue->pause();
}

// \brief A BootNotification that is never answered is retried with the same message timeout each time: retries do not
// make the queue wait longer and longer for a CSMS that does not answer
TEST_F(MessageQueueTest, test_boot_notification_retries_keep_their_message_timeout) {
    config.message_timeout_seconds = 1;
    config.transaction_message_retry_interval = 2;
    config.boot_notification_retry_interval_seconds = 0;
    restart_message_queue();
    std::mutex times_mutex;
    std::vector<std::chrono::steady_clock::time_point> sent_at;
    EXPECT_CALL(send_callback_mock, Call(testing::_))
        .WillRepeatedly(testing::Invoke([this, &times_mutex, &sent_at](const json&) {
            {
                const std::lock_guard<std::mutex> lock(times_mutex);
                sent_at.push_back(std::chrono::steady_clock::now());
            }
            mark_call_sent();
            return true;
        }));

    push_message_call(TestMessageType::BootNotification, "boot");
    // Each attempt times out after message_timeout_seconds + transaction_message_retry_interval * 1 = 3 s. A timeout
    // that grows with the attempts makes the second wait 5 s.
    wait_for_calls(3, std::chrono::seconds(12));

    const std::lock_guard<std::mutex> lock(times_mutex);
    ASSERT_GE(sent_at.size(), 3);
    const auto first_wait = sent_at.at(1) - sent_at.at(0);
    const auto second_wait = sent_at.at(2) - sent_at.at(1);
    EXPECT_LT(second_wait, first_wait + std::chrono::seconds(1))
        << "the second retry waited longer than the first for the message timeout";
}

// \brief The retry interval of a BootNotification counts from its timeout, not from when it was first queued, so a
// BootNotification queued long before the connection is not retried back to back
TEST_F(MessageQueueTest, test_boot_notification_retry_interval_counts_from_timeout) {
    config.message_timeout_seconds = 1;
    config.transaction_message_retry_interval = 0;
    config.boot_notification_retry_interval_seconds = 2;
    restart_message_queue();
    std::mutex times_mutex;
    std::vector<std::chrono::steady_clock::time_point> sent_at;
    EXPECT_CALL(send_callback_mock, Call(testing::_))
        .WillRepeatedly(testing::Invoke([this, &times_mutex, &sent_at](const json&) {
            {
                const std::lock_guard<std::mutex> lock(times_mutex);
                sent_at.push_back(std::chrono::steady_clock::now());
            }
            mark_call_sent();
            return true;
        }));

    message_queue->pause();
    push_message_call(TestMessageType::BootNotification, "boot");
    std::this_thread::sleep_for(std::chrono::seconds(3));
    message_queue->resume(std::chrono::seconds(0));
    wait_for_calls(2, std::chrono::seconds(6));

    const std::lock_guard<std::mutex> lock(times_mutex);
    ASSERT_GE(sent_at.size(), 2);
    // message timeout of 1 s, then the retry interval of 2 s
    EXPECT_GE(sent_at.at(1) - sent_at.at(0), std::chrono::milliseconds(2500))
        << "the BootNotification was retried before its retry interval had passed since the timeout";
}

// \brief An action run while the queue is idle pauses sending without losing the connection, so a BootNotification
// waiting for its retry after a CALLERROR keeps its retry interval
TEST_F(MessageQueueTest, test_idle_action_keeps_boot_notification_retry_interval) {
    config.boot_notification_retry_interval_seconds = 60;
    restart_message_queue();
    EXPECT_CALL(send_callback_mock, Call(testing::_)).WillOnce(MarkAndReturn(true));

    push_message_call(TestMessageType::BootNotification, "boot");
    wait_for_calls(1);
    message_queue->receive(json{4, "boot", "InternalError", "", json::object()}.dump());

    std::promise<void> action_ran;
    message_queue->run_when_idle([&action_ran]() {
        action_ran.set_value();
        return true;
    });
    ASSERT_EQ(action_ran.get_future().wait_for(std::chrono::seconds(3)), std::future_status::ready);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    EXPECT_EQ(get_call_count(), 1) << "the idle action made the BootNotification skip its retry interval";
}

// \brief A BootNotification replaced by a newer one settles the future of its sender as offline
TEST_F(MessageQueueTest, test_replaced_boot_notification_settles_its_future) {
    message_queue->pause();
    Call<TestRequest> first;
    first.msg.type = TestMessageType::BootNotification;
    first.uniqueId = "first";
    auto first_future = message_queue->push_call_async(first);
    push_message_call(TestMessageType::BootNotification, "second");

    ASSERT_EQ(first_future.wait_for(std::chrono::seconds(1)), std::future_status::ready);
    try {
        EXPECT_TRUE(first_future.get().offline);
    } catch (const std::future_error& e) {
        ADD_FAILURE() << "the replaced BootNotification left its future unsettled: " << e.what();
    }
}

// \brief Test sending a transactional message
TEST_F(MessageQueueTest, test_transactional_message_is_sent) {

    EXPECT_CALL(send_callback_mock, Call(json{2, "0", "transactional", json{{"data", "test_data"}}}))
        .WillOnce(MarkAndReturn(true));
    EXPECT_CALL(*db, insert_message_queue_message(testing::_, testing::_));

    Call<TestRequest> call;
    call.msg.type = TestMessageType::TRANSACTIONAL;
    call.msg.data = "test_data";
    call.uniqueId = "0";
    message_queue->push_call(call);

    wait_for_calls();
}

// \brief Test sending a non-transactional message
TEST_F(MessageQueueTest, test_non_transactional_message_is_sent) {

    EXPECT_CALL(send_callback_mock, Call(json{2, "0", "non_transactional", json{{"data", "test_data"}}}))
        .WillOnce(MarkAndReturn(true));

    Call<TestRequest> call;
    call.msg.type = TestMessageType::NON_TRANSACTIONAL;
    call.msg.data = "test_data";
    call.uniqueId = "0";
    message_queue->push_call(call);

    wait_for_calls();
}

// \brief Test transactional messages that are sent while being offline are sent afterwards
TEST_F(MessageQueueTest, test_queuing_up_of_transactional_messages) {

    config.transaction_message_attempts = 2;
    restart_message_queue();

    int message_count = config.queues_total_size_threshold + 3;
    testing::Sequence s;

    // Setup: reject the first call ("offline"); after that, accept any call
    EXPECT_CALL(send_callback_mock, Call(testing::_)).InSequence(s).WillOnce(MarkAndReturn(false));
    EXPECT_CALL(send_callback_mock, Call(testing::_))
        .Times(message_count)
        .InSequence(s)
        .WillRepeatedly(MarkAndReturn(true, true));
    EXPECT_CALL(*db, insert_message_queue_message(testing::_, QueueType::Transaction)).Times(message_count);
    EXPECT_CALL(*db, remove_message_queue_message(testing::_, QueueType::Transaction)).Times(message_count);

    // Act:
    // push first call and wait for callback; then push all other calls and resume queue
    push_message_call(TestMessageType::TRANSACTIONAL);
    wait_for_calls(1);

    for (int i = 1; i < message_count; i++) {
        push_message_call(TestMessageType::TRANSACTIONAL);
    }

    // expect one repeated and all other calls been made
    wait_for_calls(message_count + 1);
}

// \brief Test that - with default setting -  non-transactional messages that are not sent afterwards
TEST_F(MessageQueueTest, test_non_queuing_up_of_non_transactional_messages) {

    int message_count = config.queues_total_size_threshold + 3;
    testing::Sequence s;

    // Setup: reject the first call ("offline"); after that, accept any call
    EXPECT_CALL(send_callback_mock, Call(testing::_)).InSequence(s).WillOnce(MarkAndReturn(false));
    EXPECT_CALL(send_callback_mock, Call(testing::_)).InSequence(s).WillRepeatedly(MarkAndReturn(true, true));

    // Act:
    // push first call and wait for callback; then push all other calls and resume queue
    push_message_call(TestMessageType::NON_TRANSACTIONAL);
    wait_for_calls(1);

    // go offline and push all other calls
    message_queue->pause();

    for (int i = 1; i < message_count; i++) {
        push_message_call(TestMessageType::NON_TRANSACTIONAL);
    }

    message_queue->resume(std::chrono::seconds(0));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // expect calls not repeated
    EXPECT_EQ(1, get_call_count());
}

// \brief Test that if queue_all_messages is set to true, non-transactional messages that are sent when online again
TEST_F(MessageQueueTest, test_queuing_up_of_non_transactional_messages) {
    config.queue_all_messages = true;
    config.transaction_message_attempts = 2;
    restart_message_queue();

    int message_count = config.queues_total_size_threshold;
    testing::Sequence s;

    // Setup: reject the first call ("offline"); after that, accept any call
    EXPECT_CALL(send_callback_mock, Call(testing::_)).InSequence(s).WillOnce(MarkAndReturn(false));
    EXPECT_CALL(send_callback_mock, Call(testing::_)).InSequence(s).WillRepeatedly(MarkAndReturn(true, true));

    // Act:
    // push first call and wait for callback; then push all other calls and resume queue
    push_message_call(TestMessageType::NON_TRANSACTIONAL);
    wait_for_calls(1);

    for (int i = 1; i < message_count; i++) {
        push_message_call(TestMessageType::NON_TRANSACTIONAL);
    }

    // expect calls _are_ repeated
    wait_for_calls(message_count + 1);
}

// \brief Test that if the max size threshold is exceeded, the non-transactional  messages are dropped
//  Sends both non-transactions and transactional messages while on pause, expects a certain amount of non-transactional
//  to be dropped.
TEST_F(MessageQueueTest, test_clean_up_non_transactional_queue) {

    const int sent_transactional_messages = 10;
    const int sent_non_transactional_messages = 15;
    config.queues_total_size_threshold =
        20; // expect two messages to be dropped each round (3x), end up with 15-6=9 non-transactional remaining
    config.queue_all_messages = true;
    const int expected_skipped_transactional_messages = 6;
    restart_message_queue();

    EXPECT_CALL(*db, insert_message_queue_message(testing::_, testing::_))
        .Times(sent_transactional_messages + sent_non_transactional_messages);
    EXPECT_CALL(*db, remove_message_queue_message(testing::_, testing::_))
        .Times(sent_transactional_messages + sent_non_transactional_messages)
        .WillRepeatedly(testing::Return());

    // go offline
    message_queue->pause();

    testing::Sequence s;
    for (int i = 0; i < sent_non_transactional_messages; i++) {
        auto msg_id = push_message_call(TestMessageType::NON_TRANSACTIONAL);

        if (i >= expected_skipped_transactional_messages) {
            EXPECT_CALL(send_callback_mock,
                        Call(json{2, msg_id, to_string(TestMessageType::NON_TRANSACTIONAL), json{{"data", msg_id}}}))
                .InSequence(s)
                .WillOnce(MarkAndReturn(true, true));
        }
    }
    for (int i = 0; i < sent_transactional_messages; i++) {
        auto msg_id = push_message_call(TestMessageType::TRANSACTIONAL);
        EXPECT_CALL(send_callback_mock,
                    Call(json{2, msg_id, to_string(TestMessageType::TRANSACTIONAL), json{{"data", msg_id}}}))
            .InSequence(s)
            .WillOnce(MarkAndReturn(true, true));
    }

    // go online again
    message_queue->resume(std::chrono::seconds(0));

    // expect calls _are_ repeated
    wait_for_calls(sent_transactional_messages + sent_non_transactional_messages -
                   expected_skipped_transactional_messages);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // assert no further calls
    EXPECT_EQ(sent_transactional_messages + sent_non_transactional_messages - expected_skipped_transactional_messages,
              get_call_count());
}

// \brief Test that if the max size threshold is exceeded, intermediate transactional (update) messages are dropped
//  Sends both non-transactions and transactional messages while on pause, expects all non-transactional, and any except
//  every forth transactional to be dropped
TEST_F(MessageQueueTest, test_clean_up_transactional_queue) {

    const int sent_non_transactional_messages = 10;
    const std::vector<int> transaction_update_messages{0, 4, 6,
                                                       2}; // meaning there are 4 transactions, each with a "start" and
                                                           // "stop" message and the provided number of updates;
    // in total 4*2 + 4+ 6 +2 = 20 messages
    config.queues_total_size_threshold = 13;
    /**
     *  Message IDs:
     *   non-transactional:  0 -  9
     *   Transaction I:     10 - 11
     *   Transaction II:    12 - 17
     *   Transaction III:   18 - 25
     *   Transaction IV:    26 - 29
     *
     *   Expected dropping behavior
     *   - adding msg 13-22 -> each drop 1 non-transactional (floored 10% of queue thresholds)
     *   - adding msg 23 (update of third transaction) -> drop 4 messages with ids 13,15,19,21
     *   - adding msg 27 (update of fourth transaction) -> drop 3 message with ids 14,20,23
     */
    const std::set<std::string> expected_dropped_transaction_messages = {
        "test_call_13", "test_call_15", "test_call_19", "test_call_21", "test_call_14", "test_call_20", "test_call_23",
    };
    const int expected_sent_messages = 13;
    config.queue_all_messages = true;
    restart_message_queue();

    EXPECT_CALL(*db, insert_message_queue_message(testing::_, testing::_)).Times(30);
    EXPECT_CALL(*db, remove_message_queue_message(testing::_, testing::_)).Times(30).WillRepeatedly(testing::Return());

    // go offline
    message_queue->pause();

    // Send messages / set up expected calls
    testing::Sequence s;
    for (int i = 0; i < sent_non_transactional_messages; i++) {
        push_message_call(TestMessageType::NON_TRANSACTIONAL);
    }

    for (int update_messages : transaction_update_messages) {
        // transaction "start"
        auto start_msg_id = push_message_call(TestMessageType::TRANSACTIONAL);
        EXPECT_CALL(send_callback_mock, Call(json{2, start_msg_id, to_string(TestMessageType::TRANSACTIONAL),
                                                  json{{"data", start_msg_id}}}))
            .InSequence(s)
            .WillOnce(MarkAndReturn(true, true));

        for (int i = 0; i < update_messages; i++) {
            auto update_msg_id = push_message_call(TestMessageType::TRANSACTIONAL_UPDATE);

            if (!expected_dropped_transaction_messages.count(update_msg_id)) {
                EXPECT_CALL(send_callback_mock,
                            Call(json{2, update_msg_id, to_string(TestMessageType::TRANSACTIONAL_UPDATE),
                                      json{{"data", update_msg_id}}}))
                    .InSequence(s)
                    .WillOnce(MarkAndReturn(true, true));
            }
        }

        auto stop_msg_id = push_message_call(TestMessageType::TRANSACTIONAL);
        // transaction "end"
        EXPECT_CALL(send_callback_mock,
                    Call(json{2, stop_msg_id, to_string(TestMessageType::TRANSACTIONAL), json{{"data", stop_msg_id}}}))
            .InSequence(s)
            .WillOnce(MarkAndReturn(true, true));
    }

    // Resume & verify
    message_queue->resume(std::chrono::seconds(0));

    wait_for_calls(expected_sent_messages);
}

// \brief Test that transactional retries and non-transactional messages are all delivered
// when a transactional message continuously fails.
//
// OCPP does not require non-transactional messages to wait for transactional retries.
// The RPC layer is synchronous (one in-flight at a time), but non-transactional messages
// like Heartbeats or StatusNotifications are allowed to be sent between retry attempts.
// This test verifies:
//   - The transactional message is retried the configured number of times
//   - The non-transactional message is still delivered (not starved by retries)
//   - Transactional messages maintain their own ordering
TEST_F(MessageQueueTest, test_transactional_order_strictness) {
    config.queues_total_size_threshold = 20; // Ensure no drops
    config.transaction_message_attempts = 2;
    config.transaction_message_retry_interval = 0;
    restart_message_queue();

    std::vector<std::string> call_order;
    std::mutex order_mutex;

    EXPECT_CALL(send_callback_mock, Call(testing::_))
        .WillRepeatedly(testing::Invoke([&, this](const json::array_t& msg) -> bool {
            // Identify by payload data since UUID changes on retry
            std::string data = msg.at(3).at("data").get<std::string>();

            {
                std::lock_guard<std::mutex> lock(order_mutex);
                call_order.push_back(data);
            }

            this->mark_call_sent();

            if (data == "TX1") {
                return false; // Trigger retry logic
            }

            reception_timer.timeout(
                [this, msg]() {
                    this->message_queue->receive(json{3, msg[1], json::object()}.dump());
                },
                std::chrono::milliseconds(0));
            return true;
        }));

    // 1. Push TX1 (blocks TX2)
    // 2. Push TX2 (queued behind TX1)
    // 3. Push Heartbeat (Non-transactional)
    push_message_call(TestMessageType::TRANSACTIONAL, "TX1");
    push_message_call(TestMessageType::TRANSACTIONAL, "TX2");
    push_message_call(TestMessageType::NON_TRANSACTIONAL, "Heartbeat");

    // Wait for: TX1 (fail) + TX1 (fail/drop) + TX2 (success) + Heartbeat (success) = 4 calls
    wait_for_calls(4);

    std::lock_guard<std::mutex> lock(order_mutex);

    // TX1 must have been attempted twice
    size_t tx1_count = std::count(call_order.begin(), call_order.end(), "TX1");
    EXPECT_EQ(tx1_count, 2);

    // TX2 must ONLY appear after all TX1 attempts are done.
    // We find the first occurrence of TX2 and ensure no TX1 exists after it.
    auto first_tx2 = std::find(call_order.begin(), call_order.end(), "TX2");
    ASSERT_NE(first_tx2, call_order.end()) << "TX2 was never sent!";

    auto tx1_after_tx2 = std::find(first_tx2, call_order.end(), "TX1");
    EXPECT_EQ(tx1_after_tx2, call_order.end()) << "TX1 was sent after TX2! Ordering violated.";

    // Heartbeat was delivered
    EXPECT_TRUE(std::find(call_order.begin(), call_order.end(), "Heartbeat") != call_order.end());
}

// \brief Test that once retry attempts are exceeded, the message is dropped and the next
// message in the queue is sent successfully.
TEST_F(MessageQueueTest, test_message_dropped_after_max_retries_then_next_message_sent) {

    config.transaction_message_attempts = 2;
    config.transaction_message_retry_interval = 0;
    restart_message_queue();

    std::vector<std::string> observed_actions;
    std::mutex actions_mutex;
    std::atomic<int> local_count{0};

    EXPECT_CALL(send_callback_mock, Call(testing::_))
        .WillRepeatedly(testing::Invoke([&, this](const json::array_t& msg) -> bool {
            std::string action = msg.at(2).get<std::string>();
            int current = ++local_count;
            {
                std::lock_guard<std::mutex> lk(actions_mutex);
                observed_actions.push_back(action);
            }
            this->mark_call_sent();
            if (action == "transactional" && current <= 2) {
                return false; // fail the first transactional message both times
            }
            // Succeed and respond to everything else
            reception_timer.timeout(
                [this, msg]() {
                    this->message_queue->receive(json{3, msg[1], json::object()}.dump());
                },
                std::chrono::milliseconds(0));
            return true;
        }));

    EXPECT_CALL(*db, insert_message_queue_message(testing::_, testing::_)).Times(2);
    EXPECT_CALL(*db, remove_message_queue_message(testing::_, testing::_)).Times(2).WillRepeatedly(testing::Return());

    // Push a transactional message (will be retried and dropped) and a second transactional
    push_message_call(TestMessageType::TRANSACTIONAL);
    push_message_call(TestMessageType::TRANSACTIONAL);

    // 2 failed attempts for first message + 1 successful for second
    wait_for_calls(3);

    std::lock_guard<std::mutex> lk(actions_mutex);
    ASSERT_GE(observed_actions.size(), 3u);
    // First two are the failed transactional, third is the next transactional succeeding
    EXPECT_EQ(observed_actions[0], "transactional");
    EXPECT_EQ(observed_actions[1], "transactional");
    EXPECT_EQ(observed_actions[2], "transactional");
}

// \brief Test handle_timeout_or_callerror when send_callback returns true but no response
// arrives (timeout fires). Verifies that the timeout timer triggers retry.
TEST_F(MessageQueueTest, test_timeout_triggers_retry_after_successful_send) {

    config.transaction_message_attempts = 2;
    config.transaction_message_retry_interval = 0;
    config.message_timeout_seconds = 1; // short timeout so test completes quickly
    restart_message_queue();

    std::mutex removal_mutex;
    std::condition_variable removal_cv;
    bool message_removed = false;

    // send_callback returns true both times, but we never send a CALLRESULT,
    // so the timeout fires and triggers handle_timeout_or_callerror
    EXPECT_CALL(send_callback_mock, Call(testing::_)).Times(2).WillRepeatedly(MarkAndReturn(true));
    EXPECT_CALL(*db, insert_message_queue_message(testing::_, QueueType::Transaction)).Times(1);
    EXPECT_CALL(*db, remove_message_queue_message(testing::_, QueueType::Transaction))
        .Times(1)
        .WillOnce(testing::Invoke([&](const std::string&, const QueueType) {
            std::lock_guard<std::mutex> lk(removal_mutex);
            message_removed = true;
            removal_cv.notify_one();
        }));

    push_message_call(TestMessageType::TRANSACTIONAL);

    // Wait for both send attempts (timeouts fire at ~1s each)
    wait_for_calls(2, std::chrono::seconds(5));
    EXPECT_EQ(2, get_call_count());

    // Wait for the DB removal that happens when the final timeout exhausts retries
    {
        std::unique_lock<std::mutex> lk(removal_mutex);
        EXPECT_TRUE(removal_cv.wait_for(lk, std::chrono::seconds(5), [&] { return message_removed; }));
    }
}

// \brief Test handle_timeout_or_callerror when send_callback returns false.
// Verifies that send failure followed by eventual success works correctly.
TEST_F(MessageQueueTest, test_send_failure_then_success) {

    config.transaction_message_attempts = 3;
    config.transaction_message_retry_interval = 0;
    restart_message_queue();

    testing::Sequence s;

    // Fail first two, succeed on third
    EXPECT_CALL(send_callback_mock, Call(testing::_)).InSequence(s).WillOnce(MarkAndReturn(false));
    EXPECT_CALL(send_callback_mock, Call(testing::_)).InSequence(s).WillOnce(MarkAndReturn(false));
    EXPECT_CALL(send_callback_mock, Call(testing::_)).InSequence(s).WillOnce(MarkAndReturn(true, true));

    EXPECT_CALL(*db, insert_message_queue_message(testing::_, QueueType::Transaction)).Times(1);
    EXPECT_CALL(*db, remove_message_queue_message(testing::_, QueueType::Transaction)).Times(1);

    push_message_call(TestMessageType::TRANSACTIONAL);
    wait_for_calls(3);

    EXPECT_EQ(3, get_call_count());
}

// \brief Test that non-transactional messages (without queue_all_messages) are dropped
// immediately on send failure and are NOT retried, regardless of transaction_message_attempts.
TEST_F(MessageQueueTest, test_non_transactional_not_retried_on_send_failure) {

    config.queue_all_messages = false;
    config.transaction_message_attempts = 5; // high, to confirm it's irrelevant
    config.transaction_message_retry_interval = 0;
    restart_message_queue();

    // Should only be called once - non-transactional messages without queue_all_messages are not retried
    EXPECT_CALL(send_callback_mock, Call(testing::_)).Times(1).WillOnce(MarkAndReturn(false));

    push_message_call(TestMessageType::NON_TRANSACTIONAL);
    wait_for_calls(1);

    // Wait to confirm no further retry
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_EQ(1, get_call_count());
}

// \brief Test that non-transactional messages WITH queue_all_messages=true are retried
// on send failure, just like transactional messages.
TEST_F(MessageQueueTest, test_non_transactional_retried_with_queue_all_messages) {

    config.queue_all_messages = true;
    config.transaction_message_attempts = 3;
    config.transaction_message_retry_interval = 0;
    restart_message_queue();

    // All 3 attempts fail
    EXPECT_CALL(send_callback_mock, Call(testing::_)).Times(3).WillRepeatedly(MarkAndReturn(false));
    EXPECT_CALL(*db, insert_message_queue_message(testing::_, QueueType::Normal)).Times(1);
    EXPECT_CALL(*db, remove_message_queue_message(testing::_, QueueType::Normal)).Times(1);

    push_message_call(TestMessageType::NON_TRANSACTIONAL);
    wait_for_calls(3);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(3, get_call_count());
}

// \brief Test that BootNotification is not dropped when the normal message queue overflows.
// Pushes a BootNotification plus enough messages to exceed the threshold, then verifies
// BootNotification survives the drop and is still sent.
TEST_F(MessageQueueTest, test_boot_notification_not_dropped_on_queue_overflow) {

    config.queues_total_size_threshold = 10;
    config.queue_all_messages = true;
    restart_message_queue();

    EXPECT_CALL(*db, insert_message_queue_message(testing::_, testing::_)).Times(testing::AnyNumber());
    EXPECT_CALL(*db, remove_message_queue_message(testing::_, testing::_))
        .Times(testing::AnyNumber())
        .WillRepeatedly(testing::Return());

    // go offline
    message_queue->pause();

    // Push BootNotification first (goes to front of normal queue)
    auto boot_id = push_message_call(TestMessageType::BootNotification);

    // Push enough non-transactional messages to fill normal queue
    const int non_transactional_count = 8;
    for (int i = 0; i < non_transactional_count; i++) {
        push_message_call(TestMessageType::NON_TRANSACTIONAL);
    }

    // Push transactional messages to exceed threshold and trigger drops
    const int transactional_count = 5;
    for (int i = 0; i < transactional_count; i++) {
        push_message_call(TestMessageType::TRANSACTIONAL);
    }

    // Set up expectations: BootNotification must be sent
    std::atomic<bool> boot_sent{false};
    EXPECT_CALL(send_callback_mock, Call(testing::_))
        .WillRepeatedly(testing::Invoke([&, this](const json::array_t& msg) -> bool {
            std::string data = msg.at(3).at("data").get<std::string>();
            if (data == boot_id) {
                boot_sent = true;
            }
            this->mark_call_sent();
            reception_timer.timeout(
                [this, msg]() {
                    this->message_queue->receive(json{3, msg[1], json::object()}.dump());
                },
                std::chrono::milliseconds(0));
            return true;
        }));

    // go online again
    message_queue->resume(std::chrono::seconds(0));

    // Wait for messages to be processed (BootNotification + surviving non-transactional + transactional)
    // Some non-transactional messages will have been dropped, but BootNotification must survive
    wait_for_calls(5, std::chrono::seconds(5));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_TRUE(boot_sent) << "BootNotification was dropped from the queue!";
}

// \brief Test that when BootNotification is the only message in the normal queue,
// check_queue_sizes() doesn't loop infinitely trying to drop it.
TEST_F(MessageQueueTest, test_boot_notification_survives_when_only_message_in_normal_queue) {

    config.queues_total_size_threshold = 10;
    config.queue_all_messages = true;
    restart_message_queue();

    EXPECT_CALL(*db, insert_message_queue_message(testing::_, testing::_)).Times(testing::AnyNumber());
    EXPECT_CALL(*db, remove_message_queue_message(testing::_, testing::_))
        .Times(testing::AnyNumber())
        .WillRepeatedly(testing::Return());

    // go offline
    message_queue->pause();

    // Push BootNotification as the only normal message
    auto boot_id = push_message_call(TestMessageType::BootNotification);

    // Fill transaction queue past threshold
    const int transactional_count = 15;
    for (int i = 0; i < transactional_count; i++) {
        push_message_call(TestMessageType::TRANSACTIONAL);
    }

    // Set up expectations: BootNotification must be sent
    std::atomic<bool> boot_sent{false};
    EXPECT_CALL(send_callback_mock, Call(testing::_))
        .WillRepeatedly(testing::Invoke([&, this](const json::array_t& msg) -> bool {
            std::string data = msg.at(3).at("data").get<std::string>();
            if (data == boot_id) {
                boot_sent = true;
            }
            this->mark_call_sent();
            reception_timer.timeout(
                [this, msg]() {
                    this->message_queue->receive(json{3, msg[1], json::object()}.dump());
                },
                std::chrono::milliseconds(0));
            return true;
        }));

    // go online - this must not hang (the break guard prevents infinite loop)
    message_queue->resume(std::chrono::seconds(0));

    // Wait for messages to be processed
    wait_for_calls(5, std::chrono::seconds(5));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_TRUE(boot_sent) << "BootNotification was dropped from the queue!";
}

// \brief Test that a transactional CALL in flight when the connection ends is retried with a new message ID after the
// reconnect, without waiting for its timeout
TEST_F(MessageQueueTest, test_transactional_in_flight_at_pause_is_retried_after_resume) {
    config.transaction_message_attempts = 2;
    config.transaction_message_retry_interval = 0;
    config.message_timeout_seconds = 10;
    restart_message_queue();

    std::vector<json::array_t> sent;
    std::mutex sent_mutex;
    EXPECT_CALL(send_callback_mock, Call(testing::_))
        .Times(2)
        .WillRepeatedly(testing::Invoke([&, this](const json::array_t& msg) -> bool {
            {
                std::lock_guard<std::mutex> lk(sent_mutex);
                sent.push_back(msg);
            }
            this->mark_call_sent();
            return true;
        }));
    EXPECT_CALL(*db, insert_message_queue_message(testing::_, QueueType::Transaction)).Times(1);
    EXPECT_CALL(*db, remove_message_queue_message(testing::_, testing::_)).Times(0);

    push_message_call(TestMessageType::TRANSACTIONAL);
    wait_for_calls(1);

    message_queue->pause();
    message_queue->resume(std::chrono::seconds(0));

    wait_for_calls(2);

    std::lock_guard<std::mutex> lk(sent_mutex);
    ASSERT_EQ(sent.size(), 2u);
    EXPECT_EQ(sent[1].at(2), "transactional");
    EXPECT_EQ(sent[1].at(3), sent[0].at(3));
    EXPECT_NE(sent[1].at(1), sent[0].at(1));
}

// \brief Test that a CALL that is not queued offline and is in flight when the connection ends is dropped and answered
// as offline by pause()
TEST_F(MessageQueueTest, test_non_queued_in_flight_at_pause_is_dropped_offline) {
    config.message_timeout_seconds = 10;
    restart_message_queue();

    EXPECT_CALL(send_callback_mock, Call(testing::_)).Times(1).WillOnce(MarkAndReturn(true));

    Call<TestRequest> call;
    call.msg.type = TestMessageType::NON_TRANSACTIONAL;
    call.uniqueId = "0";
    auto future = message_queue->push_call_async(call);
    wait_for_calls(1);

    message_queue->pause();

    ASSERT_EQ(future.wait_for(std::chrono::seconds(0)), std::future_status::ready);
    EXPECT_TRUE(future.get().offline);
}

// \brief Test that a BootNotification in flight when the connection ends is rescheduled instead of answered
TEST_F(MessageQueueTest, test_boot_notification_in_flight_at_pause_is_rescheduled) {
    config.message_timeout_seconds = 10;
    config.boot_notification_retry_interval_seconds = 0;
    restart_message_queue();

    std::vector<json::array_t> sent;
    std::mutex sent_mutex;
    EXPECT_CALL(send_callback_mock, Call(testing::_))
        .Times(2)
        .WillRepeatedly(testing::Invoke([&, this](const json::array_t& msg) -> bool {
            {
                std::lock_guard<std::mutex> lk(sent_mutex);
                sent.push_back(msg);
            }
            this->mark_call_sent();
            return true;
        }));

    Call<TestRequest> call;
    call.msg.type = TestMessageType::BootNotification;
    call.uniqueId = "0";
    auto future = message_queue->push_call_async(call);
    wait_for_calls(1);

    message_queue->pause();
    EXPECT_EQ(future.wait_for(std::chrono::seconds(0)), std::future_status::timeout);

    message_queue->resume(std::chrono::seconds(0));
    wait_for_calls(2);

    std::lock_guard<std::mutex> lk(sent_mutex);
    ASSERT_EQ(sent.size(), 2u);
    EXPECT_EQ(sent[1].at(2), "boot_notification");
    EXPECT_NE(sent[1].at(1), sent[0].at(1));
}

// \brief Test that a CALL queued behind the one in flight when the connection ends is sent after the reconnect without
// waiting for the timeout of the one in flight
TEST_F(MessageQueueTest, test_next_message_sent_after_resume_when_in_flight_was_settled) {
    config.message_timeout_seconds = 10;
    restart_message_queue();

    testing::Sequence s;
    EXPECT_CALL(send_callback_mock, Call(json{2, "first", "non_transactional", json{{"data", "first"}}}))
        .InSequence(s)
        .WillOnce(MarkAndReturn(true));
    EXPECT_CALL(send_callback_mock, Call(json{2, "second", "non_transactional", json{{"data", "second"}}}))
        .InSequence(s)
        .WillOnce(MarkAndReturn(true));

    push_message_call(TestMessageType::NON_TRANSACTIONAL, "first");
    wait_for_calls(1);
    push_message_call(TestMessageType::NON_TRANSACTIONAL, "second");

    message_queue->pause();
    message_queue->resume(std::chrono::seconds(0));

    wait_for_calls(2);
}

// \brief Test that the timeout of a CALL settled by pause() has no effect once it would have fired
TEST_F(MessageQueueTest, test_settled_in_flight_timer_does_nothing_later) {
    config.transaction_message_retry_interval = 0;
    config.message_timeout_seconds = 1;
    restart_message_queue();

    EXPECT_CALL(send_callback_mock, Call(testing::_)).Times(1).WillOnce(MarkAndReturn(true));
    EXPECT_CALL(*db, insert_message_queue_message(testing::_, QueueType::Transaction)).Times(1);
    EXPECT_CALL(*db, remove_message_queue_message(testing::_, QueueType::Transaction)).Times(1);

    Call<TestRequest> call;
    call.msg.type = TestMessageType::TRANSACTIONAL;
    call.uniqueId = "0";
    auto future = message_queue->push_call_async(call);
    wait_for_calls(1);

    message_queue->pause();
    ASSERT_EQ(future.wait_for(std::chrono::seconds(0)), std::future_status::ready);
    EXPECT_TRUE(future.get().offline);

    message_queue->resume(std::chrono::seconds(0));
    std::this_thread::sleep_for(std::chrono::seconds(2));

    EXPECT_EQ(1, get_call_count());
}

TEST_F(MessageQueueTest, test_old_timeout_cannot_settle_new_in_flight_after_pause) {
    constexpr int iterations = 200;
    config.transaction_message_retry_interval = 0;
    config.message_timeout_seconds = 0;
    restart_message_queue();

    EXPECT_CALL(send_callback_mock, Call(testing::_)).Times(2 * iterations).WillRepeatedly(MarkAndReturn(true));
    EXPECT_CALL(*db, insert_message_queue_message(testing::_, QueueType::Transaction)).Times(iterations);
    EXPECT_CALL(*db, remove_message_queue_message(testing::_, QueueType::Transaction)).Times(iterations);

    for (int i = 0; i < iterations; ++i) {
        message_queue->update_message_timeout(0);

        Call<TestRequest> first;
        first.msg.type = TestMessageType::NON_TRANSACTIONAL;
        first.uniqueId = "first_" + std::to_string(i);
        auto first_future = message_queue->push_call_async(first);
        wait_for_calls(2 * i + 1);

        message_queue->pause();
        ASSERT_EQ(first_future.wait_for(std::chrono::seconds(0)), std::future_status::ready);
        EXPECT_TRUE(first_future.get().offline);

        message_queue->update_message_timeout(10);
        Call<TestRequest> second;
        second.msg.type = TestMessageType::TRANSACTIONAL;
        second.uniqueId = "second_" + std::to_string(i);
        auto second_future = message_queue->push_call_async(second);
        message_queue->resume(std::chrono::seconds(0));
        wait_for_calls(2 * i + 2);

        EXPECT_EQ(second_future.wait_for(std::chrono::milliseconds(2)), std::future_status::timeout);
        message_queue->receive(json{3, second.uniqueId, json::object()}.dump());
        ASSERT_EQ(second_future.wait_for(std::chrono::milliseconds(100)), std::future_status::ready);
        EXPECT_FALSE(second_future.get().offline);
    }
}

TEST_F(MessageQueueTest, test_run_when_idle_waits_for_call_result) {
    EXPECT_CALL(send_callback_mock, Call(testing::_)).WillOnce(MarkAndReturn(true));
    push_message_call(TestMessageType::NON_TRANSACTIONAL, "first");
    wait_for_calls(1);

    std::promise<void> ran;
    auto ran_future = ran.get_future();
    message_queue->run_when_idle([&ran]() {
        ran.set_value();
        return true;
    });
    EXPECT_EQ(ran_future.wait_for(std::chrono::milliseconds(100)), std::future_status::timeout);

    message_queue->receive(json{3, "first", json::object()}.dump());
    EXPECT_EQ(ran_future.wait_for(std::chrono::seconds(1)), std::future_status::ready);
}

TEST_F(MessageQueueTest, test_run_when_idle_waits_for_in_flight_timeout) {
    config.message_timeout_seconds = 1;
    restart_message_queue();

    EXPECT_CALL(send_callback_mock, Call(testing::_)).WillOnce(MarkAndReturn(true));
    push_message_call(TestMessageType::NON_TRANSACTIONAL, "first");
    wait_for_calls(1);

    std::promise<void> ran;
    auto ran_future = ran.get_future();
    message_queue->run_when_idle([&ran]() {
        ran.set_value();
        return true;
    });
    EXPECT_EQ(ran_future.wait_for(std::chrono::milliseconds(100)), std::future_status::timeout);
    EXPECT_EQ(ran_future.wait_for(std::chrono::seconds(3)), std::future_status::ready);
}

TEST_F(MessageQueueTest, test_run_when_idle_runs_actions_once_in_order) {
    EXPECT_CALL(send_callback_mock, Call(testing::_)).WillOnce(MarkAndReturn(true));
    push_message_call(TestMessageType::NON_TRANSACTIONAL, "first");
    wait_for_calls(1);

    std::mutex order_mutex;
    std::vector<int> order;
    std::promise<void> second_ran;
    auto second_future = second_ran.get_future();
    message_queue->run_when_idle([&]() {
        const std::lock_guard<std::mutex> lk(order_mutex);
        order.push_back(1);
        return true;
    });
    message_queue->run_when_idle([&]() {
        const std::lock_guard<std::mutex> lk(order_mutex);
        order.push_back(2);
        second_ran.set_value();
        return true;
    });

    message_queue->receive(json{3, "first", json::object()}.dump());
    ASSERT_EQ(second_future.wait_for(std::chrono::seconds(1)), std::future_status::ready);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const std::lock_guard<std::mutex> lk(order_mutex);
    EXPECT_EQ(order, (std::vector<int>{1, 2}));
}

TEST_F(MessageQueueTest, test_no_call_sent_while_action_runs) {
    std::promise<void> started;
    auto started_future = started.get_future();
    std::promise<void> release;
    auto release_future = release.get_future();
    message_queue->run_when_idle([&]() {
        started.set_value();
        release_future.wait_for(std::chrono::seconds(3));
        return true;
    });
    ASSERT_EQ(started_future.wait_for(std::chrono::seconds(1)), std::future_status::ready);

    EXPECT_CALL(send_callback_mock, Call(testing::_)).WillOnce(MarkAndReturn(true));
    push_message_call(TestMessageType::TRANSACTIONAL);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_EQ(0, get_call_count());

    release.set_value();
    wait_for_calls(1);
}

TEST_F(MessageQueueTest, test_no_call_sent_after_closing_action_until_resume) {
    std::promise<void> ran;
    auto ran_future = ran.get_future();
    message_queue->run_when_idle([&ran]() {
        ran.set_value();
        return false;
    });
    ASSERT_EQ(ran_future.wait_for(std::chrono::seconds(1)), std::future_status::ready);

    EXPECT_CALL(send_callback_mock, Call(testing::_)).WillOnce(MarkAndReturn(true));
    push_message_call(TestMessageType::TRANSACTIONAL);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_EQ(0, get_call_count());

    message_queue->resume(std::chrono::seconds(0));
    wait_for_calls(1);
}

TEST_F(MessageQueueTest, test_calls_sent_after_action_left_socket_connected) {
    testing::Sequence s;
    EXPECT_CALL(send_callback_mock, Call(json{2, "first", "non_transactional", json{{"data", "first"}}}))
        .InSequence(s)
        .WillOnce(MarkAndReturn(true));
    EXPECT_CALL(send_callback_mock, Call(json{2, "second", "non_transactional", json{{"data", "second"}}}))
        .InSequence(s)
        .WillOnce(MarkAndReturn(true));

    push_message_call(TestMessageType::NON_TRANSACTIONAL, "first");
    wait_for_calls(1);
    message_queue->run_when_idle([]() { return true; });
    push_message_call(TestMessageType::NON_TRANSACTIONAL, "second");

    message_queue->receive(json{3, "first", json::object()}.dump());
    wait_for_calls(2);
}

TEST_F(MessageQueueTest, test_queue_paused_before_action_stays_paused) {
    message_queue->pause();
    std::promise<void> ran;
    auto ran_future = ran.get_future();
    message_queue->run_when_idle([&ran]() {
        ran.set_value();
        return true;
    });
    ASSERT_EQ(ran_future.wait_for(std::chrono::seconds(1)), std::future_status::ready);

    EXPECT_CALL(send_callback_mock, Call(testing::_)).WillOnce(MarkAndReturn(true));
    push_message_call(TestMessageType::TRANSACTIONAL);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_EQ(0, get_call_count());

    message_queue->resume(std::chrono::seconds(0));
    wait_for_calls(1);
}

TEST_F(MessageQueueTest, test_pause_and_resume_during_action_prevent_self_resume) {
    std::promise<void> paused;
    auto paused_future = paused.get_future();
    std::promise<void> release;
    auto release_future = release.get_future();
    message_queue->run_when_idle([&]() {
        message_queue->pause();
        paused.set_value();
        release_future.wait_for(std::chrono::seconds(3));
        return true;
    });
    ASSERT_EQ(paused_future.wait_for(std::chrono::seconds(1)), std::future_status::ready);
    message_queue->resume(std::chrono::seconds(1));
    release.set_value();

    EXPECT_CALL(send_callback_mock, Call(testing::_)).WillOnce(MarkAndReturn(true));
    push_message_call(TestMessageType::TRANSACTIONAL);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    EXPECT_EQ(0, get_call_count());

    wait_for_calls(1);
}

TEST_F(MessageQueueTest, test_stop_waits_for_running_action) {
    auto queue = std::make_unique<MessageQueue<TestMessageType>>([](json) { return true; }, config, db);
    queue->start();

    std::promise<void> started;
    auto started_future = started.get_future();
    std::promise<void> release;
    auto release_future = release.get_future();
    std::atomic<bool> finished{false};
    queue->run_when_idle([&]() {
        started.set_value();
        release_future.wait_for(std::chrono::seconds(3));
        finished = true;
        return true;
    });
    ASSERT_EQ(started_future.wait_for(std::chrono::seconds(1)), std::future_status::ready);

    std::promise<bool> stopped;
    auto stopped_future = stopped.get_future();
    std::thread stopper([&]() {
        queue->stop();
        stopped.set_value(finished);
    });
    EXPECT_EQ(stopped_future.wait_for(std::chrono::milliseconds(100)), std::future_status::timeout);

    release.set_value();
    EXPECT_EQ(stopped_future.wait_for(std::chrono::seconds(1)), std::future_status::ready);
    stopper.join();
    EXPECT_TRUE(stopped_future.get());
}

TEST_F(MessageQueueTest, test_destruction_without_stop_joins_worker) {
    auto queue = std::make_unique<MessageQueue<TestMessageType>>(send_callback_mock.AsStdFunction(), config, db);
    queue->start();
    queue->resume(std::chrono::seconds(0));

    // Destroyed on a detached thread, so a destructor that does not return fails the test instead of hanging it.
    std::promise<void> destroyed;
    auto destroyed_future = destroyed.get_future();
    std::thread([queue = std::move(queue), destroyed = std::move(destroyed)]() mutable {
        queue.reset();
        destroyed.set_value();
    }).detach();
    EXPECT_EQ(destroyed_future.wait_for(std::chrono::seconds(5)), std::future_status::ready);
}

TEST_F(MessageQueueTest, test_receive_rejects_too_deeply_nested_message) {
    // The CALLRESULT array is the first level, its payload adds the rest
    const auto nested = [](std::size_t levels) {
        return "[3,\"0\"," + std::string(levels - 1, '[') + std::string(levels - 1, ']') + "]";
    };

    EXPECT_NO_THROW(message_queue->receive(nested(MAX_JSON_NESTING_DEPTH)));
    EXPECT_THROW(message_queue->receive(nested(MAX_JSON_NESTING_DEPTH + 1)), MalformedRpcMessage);
    // Deep enough to overflow the stack when copied, small enough for the default message size limit
    EXPECT_THROW(message_queue->receive(nested(100000)), MalformedRpcMessage);
}

TEST(MessageLoggingTest, test_logging_a_too_deeply_nested_message) {
    MessageLogging logging(false, "", "", false, false, false, false, false, false, false, nullptr);
    const std::size_t levels = 100000;
    const auto message = "[3,\"0\"," + std::string(levels, '[') + std::string(levels, ']') + "]";
    // The message is logged as is instead of being parsed and pretty-printed
    logging.central_system("Unknown", message);
}

} // namespace ocpp
