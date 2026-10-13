// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <utils/message_handler.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

using namespace Everest;
using namespace std::chrono_literals;

namespace {

// Helper class to track execution order and timing
class ExecutionTracker {
public:
    struct Event {
        std::string topic;
        int sequence;
        std::chrono::steady_clock::time_point timestamp;
    };

    void record(const std::string& topic, int sequence) {
        std::lock_guard<std::mutex> lock(mutex_);
        events_.push_back({topic, sequence, std::chrono::steady_clock::now()});
        cv_.notify_all();
    }

    void wait_for_count(size_t count, std::chrono::milliseconds timeout = 5000ms) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait_for(lock, timeout, [this, count] { return events_.size() >= count; });
    }

    std::vector<Event> get_events() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return events_;
    }

    size_t count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return events_.size();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        events_.clear();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<Event> events_;
};

ParsedMessage create_message(const std::string& topic, const std::string& msg_type, const json& data = json{}) {
    ParsedMessage msg;
    msg.topic = topic;
    msg.data = {{"msg_type", msg_type}, {"data", data}};
    return msg;
}

ParsedMessage create_cmd_message(const std::string& topic, int sequence = 0) {
    json data = {{"sequence", sequence}};
    return create_message(topic, "Cmd", data);
}

ParsedMessage create_var_message(const std::string& topic, int sequence = 0) {
    json data = {{"data", {{"sequence", sequence}}}};
    ParsedMessage msg;
    msg.topic = topic;
    msg.data = {{"msg_type", "Var"}, {"data", data}};
    return msg;
}

ParsedMessage create_external_mqtt_message(const std::string& topic, int value = 0) {
    ParsedMessage msg;
    msg.topic = topic;
    msg.data = {{"value", value}}; // ExternalMQTT uses data directly, no msg_type wrapper
    return msg;
}

ParsedMessage create_raise_error_message(const std::string& topic, int error_code = 0) {
    json data = {{"error_code", error_code}};
    ParsedMessage msg;
    msg.topic = topic;
    msg.data = {{"msg_type", "RaiseError"}, {"data", data}};
    return msg;
}

ParsedMessage create_clear_error_message(const std::string& topic) {
    ParsedMessage msg;
    msg.topic = topic;
    msg.data = {{"msg_type", "ClearError"}, {"data", json{}}};
    return msg;
}

class MessageHandlerFixture {
public:
    MessageHandlerFixture() : handler(std::make_unique<MessageHandler>()) {
    }

    ~MessageHandlerFixture() {
        if (handler) {
            handler->stop();
        }
    }

    MessageHandler* operator->() {
        return handler.get();
    }

    MessageHandler& get() {
        return *handler;
    }

private:
    std::unique_ptr<MessageHandler> handler;
};

} // namespace

// ============================================================================
// Test: Basic Message Processing
// ============================================================================

TEST_CASE("MessageHandler processes single message", "[message_handler][basic]") {
    MessageHandlerFixture handler;
    ExecutionTracker tracker;

    auto handler_func = std::make_shared<Handler>(
        [&tracker](const std::string& topic, const json& data) { tracker.record(topic, data.value("sequence", 0)); });
    auto test_handler = std::make_shared<TypedHandler>(HandlerType::Call, handler_func);

    handler->register_handler("test/topic", test_handler);

    ParsedMessage msg = create_cmd_message("test/topic", 1);
    handler->add(msg);

    tracker.wait_for_count(1);

    auto events = tracker.get_events();
    REQUIRE(events.size() == 1);
    CHECK(events[0].topic == "test/topic");
    CHECK(events[0].sequence == 1);
}

// ============================================================================
// Test: Same Topic Ordering (Core Functionality)
// ============================================================================

TEST_CASE("MessageHandler processes same topic messages in order", "[message_handler][ordering]") {
    MessageHandlerFixture handler;
    ExecutionTracker tracker;
    std::atomic<bool> first_message_processing{false};
    std::atomic<bool> release_first_message{false};

    auto handler_func = std::make_shared<Handler>([&](const std::string& topic, const json& data) {
        int seq = data.value("sequence", 0);

        if (seq == 1) {
            first_message_processing = true;
            // Block first message until we release it
            while (!release_first_message) {
                std::this_thread::sleep_for(10ms);
            }
        }

        tracker.record(topic, seq);
    });
    auto test_handler = std::make_shared<TypedHandler>(HandlerType::Call, handler_func);

    handler->register_handler("test/topic", test_handler);

    // Send 3 messages to same topic
    handler->add(create_cmd_message("test/topic", 1));
    handler->add(create_cmd_message("test/topic", 2));
    handler->add(create_cmd_message("test/topic", 3));

    // Wait for first message to start processing
    while (!first_message_processing) {
        std::this_thread::sleep_for(10ms);
    }

    // Give some time for second/third messages to potentially be processed (they shouldn't be)
    std::this_thread::sleep_for(100ms);
    CHECK(tracker.count() == 0); // First message still blocked

    // Release first message
    release_first_message = true;

    // Wait for all messages
    tracker.wait_for_count(3);

    auto events = tracker.get_events();
    REQUIRE(events.size() == 3);

    // Verify order
    CHECK(events[0].sequence == 1);
    CHECK(events[1].sequence == 2);
    CHECK(events[2].sequence == 3);
}

// ============================================================================
// Test: Different Topics Concurrent Processing
// ============================================================================

TEST_CASE("MessageHandler processes different topics concurrently", "[message_handler][concurrency]") {
    MessageHandlerFixture handler;
    ExecutionTracker tracker;
    std::atomic<int> concurrent_count{0};
    std::atomic<int> max_concurrent{0};
    std::mutex concurrent_mutex;

    // Handler duration must exceed the latency threshold so that the first task is still
    // running when the second task has been queued long enough to trigger scaling.
    constexpr auto HANDLER_DURATION = std::chrono::milliseconds(THREAD_POOL_SCALING_LATENCY_THRESHOLD_MS * 3);

    auto handler_func = std::make_shared<Handler>([&](const std::string& topic, const json& data) {
        {
            std::lock_guard<std::mutex> lock(concurrent_mutex);
            concurrent_count++;
            if (concurrent_count > max_concurrent) {
                max_concurrent = concurrent_count.load();
            }
        }

        tracker.record(topic, data.value("sequence", 0));
        std::this_thread::sleep_for(HANDLER_DURATION);

        {
            std::lock_guard<std::mutex> lock(concurrent_mutex);
            concurrent_count--;
        }
    });
    auto test_handler = std::make_shared<TypedHandler>(HandlerType::Call, handler_func);

    handler->register_handler("topic/1", test_handler);
    handler->register_handler("topic/2", test_handler);
    handler->register_handler("topic/3", test_handler);

    // The pool starts with a single thread and uses LatencyScaling: it spawns a new thread
    // only when a queued task has been waiting longer than THREAD_POOL_SCALING_LATENCY_THRESHOLD_MS.
    //
    // Sequence to trigger scaling reliably:
    //  1. topic/1 submitted → single worker picks it up immediately (queue is empty)
    //  2. topic/2 submitted → queues up (worker is busy with topic/1)
    //  3. Sleep past the latency threshold → topic/2's wait time exceeds the threshold
    //  4. topic/3 submitted → queue size becomes 2 and oldest task (topic/2) has waited
    //     beyond the threshold → scaling fires, worker 2 is spawned → topic/1 and topic/2
    //     now run concurrently
    handler->add(create_cmd_message("topic/1", 1));
    handler->add(create_cmd_message("topic/2", 2));

    std::this_thread::sleep_for(std::chrono::milliseconds(THREAD_POOL_SCALING_LATENCY_THRESHOLD_MS + 10));

    handler->add(create_cmd_message("topic/3", 3));

    tracker.wait_for_count(3);

    INFO("max_concurrent was " << max_concurrent.load());
    CHECK(max_concurrent.load() > 1);
}

// ============================================================================
// Test: Multiple Topics with Queuing
// ============================================================================

TEST_CASE("MessageHandler handles multiple topics with queuing", "[message_handler][queuing]") {
    MessageHandlerFixture handler;
    ExecutionTracker tracker;
    std::atomic<bool> block_topic1{true};
    std::atomic<bool> block_topic2{true};

    auto handler_func = std::make_shared<Handler>([&](const std::string& topic, const json& data) {
        int seq = data.value("sequence", 0);

        if (topic == "topic/1" && seq == 1) {
            while (block_topic1) {
                std::this_thread::sleep_for(10ms);
            }
        } else if (topic == "topic/2" && seq == 1) {
            while (block_topic2) {
                std::this_thread::sleep_for(10ms);
            }
        }

        tracker.record(topic, seq);
    });
    auto test_handler = std::make_shared<TypedHandler>(HandlerType::Call, handler_func);

    handler->register_handler("topic/1", test_handler);
    handler->register_handler("topic/2", test_handler);

    // Queue multiple messages for each topic
    handler->add(create_cmd_message("topic/1", 1));
    handler->add(create_cmd_message("topic/1", 2));
    handler->add(create_cmd_message("topic/1", 3));
    handler->add(create_cmd_message("topic/2", 1));
    handler->add(create_cmd_message("topic/2", 2));
    handler->add(create_cmd_message("topic/2", 3));

    std::this_thread::sleep_for(100ms);

    // Release topic/1
    block_topic1 = false;
    tracker.wait_for_count(3);

    // Topic 1 should be complete, topic 2 still blocked
    auto events = tracker.get_events();
    for (const auto& event : events) {
        CHECK(event.topic == "topic/1");
    }

    // Release topic/2
    block_topic2 = false;
    tracker.wait_for_count(6);

    events = tracker.get_events();
    REQUIRE(events.size() == 6);

    // Verify ordering per topic
    std::vector<int> topic1_seqs;
    std::vector<int> topic2_seqs;

    for (const auto& event : events) {
        if (event.topic == "topic/1") {
            topic1_seqs.push_back(event.sequence);
        } else {
            // "topic/2"
            topic2_seqs.push_back(event.sequence);
        }
    }

    CHECK(topic1_seqs == std::vector<int>({1, 2, 3}));
    CHECK(topic2_seqs == std::vector<int>({1, 2, 3}));
}

// ============================================================================
// Test: High Load Stress Test
// ============================================================================

TEST_CASE("MessageHandler handles high load", "[message_handler][stress]") {
    MessageHandlerFixture handler;
    ExecutionTracker tracker;
    constexpr int TOPICS_COUNT = 5;
    constexpr int MESSAGES_PER_TOPIC = 20;

    auto handler_func = std::make_shared<Handler>([&](const std::string& topic, const json& data) {
        // Simulate variable processing time
        std::this_thread::sleep_for(std::chrono::milliseconds(1 + (rand() % 5)));
        tracker.record(topic, data.value("sequence", 0));
    });
    auto test_handler = std::make_shared<TypedHandler>(HandlerType::Call, handler_func);

    // Register handlers for all topics
    for (int t = 0; t < TOPICS_COUNT; ++t) {
        std::string topic = "topic/" + std::to_string(t);
        handler->register_handler(topic, test_handler);
    }

    // Send messages
    for (int t = 0; t < TOPICS_COUNT; ++t) {
        for (int m = 0; m < MESSAGES_PER_TOPIC; ++m) {
            std::string topic = "topic/" + std::to_string(t);
            handler->add(create_cmd_message(topic, m));
        }
    }

    tracker.wait_for_count(TOPICS_COUNT * MESSAGES_PER_TOPIC, 10s);

    auto events = tracker.get_events();
    REQUIRE(events.size() == TOPICS_COUNT * MESSAGES_PER_TOPIC);

    // Verify ordering per topic
    std::map<std::string, std::vector<int>> sequences_by_topic;
    for (const auto& event : events) {
        sequences_by_topic[event.topic].push_back(event.sequence);
    }

    for (const auto& [topic, sequences] : sequences_by_topic) {
        INFO("Checking topic: " << topic);
        REQUIRE(sequences.size() == MESSAGES_PER_TOPIC);

        // Verify ascending order
        for (size_t i = 0; i < sequences.size(); ++i) {
            INFO("Position: " << i);
            CHECK(sequences[i] == static_cast<int>(i));
        }
    }
}

// ============================================================================
// Test: Pending Queue Cleanup
// ============================================================================

TEST_CASE("MessageHandler cleans up pending queues", "[message_handler][cleanup]") {
    // This test verifies that empty queues are removed from pending_operation_messages_by_topic
    // We can't directly access the internal state, but we can verify behavior is consistent

    MessageHandlerFixture handler;
    ExecutionTracker tracker;
    std::atomic<bool> block{true};

    auto handler_func = std::make_shared<Handler>([&](const std::string& topic, const json& data) {
        int seq = data.value("sequence", 0);
        if (seq == 1) {
            while (block) {
                std::this_thread::sleep_for(10ms);
            }
        }
        tracker.record(topic, seq);
    });
    auto test_handler = std::make_shared<TypedHandler>(HandlerType::Call, handler_func);

    handler->register_handler("test/topic", test_handler);

    // Queue messages
    handler->add(create_cmd_message("test/topic", 1));
    handler->add(create_cmd_message("test/topic", 2));

    std::this_thread::sleep_for(100ms);

    // Release and process all
    block = false;
    tracker.wait_for_count(2);

    // Send another message - if cleanup didn't happen, behavior might be different
    handler->add(create_cmd_message("test/topic", 3));
    tracker.wait_for_count(3);

    auto events = tracker.get_events();
    CHECK(events.size() == 3);
    CHECK(events[2].sequence == 3);
}

// ============================================================================
// Test: Result Message Processing (different queue)
// ============================================================================

TEST_CASE("MessageHandler processes result messages separately", "[message_handler][result]") {
    MessageHandlerFixture handler;
    ExecutionTracker tracker;

    auto handler_func = std::make_shared<Handler>(
        [&](const std::string& topic, const json& data) { tracker.record(topic, data.value("sequence", 0)); });
    auto result_handler =
        std::make_shared<TypedHandler>("test-handler", "test-id-123", HandlerType::Result, handler_func);

    handler->register_handler("", result_handler);

    ParsedMessage msg;
    msg.topic = "test/result";
    msg.data = {{"msg_type", "CmdResult"}, {"data", {{"data", {{"id", "test-id-123"}, {"sequence", 42}}}}}};

    handler->add(msg);

    tracker.wait_for_count(1);

    auto events = tracker.get_events();
    REQUIRE(events.size() == 1);
    CHECK(events[0].sequence == 42);
}

// ============================================================================
// Test: Shutdown with Pending Messages
// ============================================================================

TEST_CASE("MessageHandler shuts down gracefully with pending messages", "[message_handler][shutdown]") {
    ExecutionTracker tracker;
    std::atomic<int> processing_count{0};
    std::atomic<bool> handler_started{false};

    {
        MessageHandlerFixture handler;

        auto handler_func = std::make_shared<Handler>([&](const std::string& topic, const json& data) {
            processing_count++;
            handler_started.store(true);
            // Simulate some processing time but don't block indefinitely
            std::this_thread::sleep_for(20ms);
            tracker.record(topic, data.value("sequence", 0));
        });
        auto test_handler = std::make_shared<TypedHandler>(HandlerType::Call, handler_func);

        handler->register_handler("test/topic", test_handler);

        // Queue multiple messages
        handler->add(create_cmd_message("test/topic", 1));
        handler->add(create_cmd_message("test/topic", 2));
        handler->add(create_cmd_message("test/topic", 3));
        handler->add(create_cmd_message("test/topic", 4));
        handler->add(create_cmd_message("test/topic", 5));

        // Give time for first message to start processing but not finish
        std::this_thread::sleep_for(10ms);

        // At this point, message 1 should be processing and 2-5 should be queued
        REQUIRE(handler_started.load()); // Verify first message started
    }
    // Destructor calls stop(), which sets running=false

    // Verify that pending messages (2-5) were NOT processed after shutdown was initiated.
    // Only message 1 (in-flight) and any that completed before running=false should be in the tracker.
    // Since we sleep 10ms and give the handler ~20ms, at most message 1 completes.
    // Messages 2-5 should be abandoned when shutdown is requested.
    CHECK(tracker.count() <= 1);
    INFO("Processed " << processing_count.load() << " messages total");
}

// ============================================================================
// Test: Thread Safety - Concurrent Adds
// ============================================================================

TEST_CASE("MessageHandler handles concurrent message addition", "[message_handler][thread_safety]") {
    MessageHandlerFixture handler;
    ExecutionTracker tracker;

    auto handler_func = std::make_shared<Handler>(
        [&](const std::string& topic, const json& data) { tracker.record(topic, data.value("sequence", 0)); });
    auto test_handler = std::make_shared<TypedHandler>(HandlerType::Call, handler_func);

    handler->register_handler("topic/1", test_handler);
    handler->register_handler("topic/2", test_handler);

    constexpr int THREADS = 4;
    constexpr int MESSAGES_PER_THREAD = 25;

    std::vector<std::thread> threads;
    for (int t = 0; t < THREADS; ++t) {
        threads.emplace_back([&handler, t]() {
            std::string topic = (t % 2 == 0) ? "topic/1" : "topic/2";
            for (int m = 0; m < MESSAGES_PER_THREAD; ++m) {
                handler->add(create_cmd_message(topic, t * 1000 + m));
                std::this_thread::sleep_for(1ms);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    tracker.wait_for_count(THREADS * MESSAGES_PER_THREAD, 10s);

    auto events = tracker.get_events();
    CHECK(events.size() == THREADS * MESSAGES_PER_THREAD);
}

// ============================================================================
// Test: Var Message Processing
// ============================================================================

TEST_CASE("MessageHandler processes Var messages", "[message_handler][var]") {
    MessageHandlerFixture handler;
    ExecutionTracker tracker;

    auto handler_func = std::make_shared<Handler>(
        [&](const std::string& topic, const json& data) { tracker.record(topic, data.value("sequence", 0)); });
    auto test_handler = std::make_shared<TypedHandler>(HandlerType::SubscribeVar, handler_func);

    handler->register_handler("module/impl/var_name", test_handler);

    // Create Var message
    ParsedMessage msg;
    msg.topic = "module/impl/var_name";
    msg.data = {{"msg_type", "Var"}, {"data", {{"data", {{"sequence", 42}}}}}};

    handler->add(msg);

    tracker.wait_for_count(1);

    auto events = tracker.get_events();
    REQUIRE(events.size() == 1);
    CHECK(events[0].topic == "module/impl/var_name");
    CHECK(events[0].sequence == 42);
}

TEST_CASE("MessageHandler processes multiple Var messages on same topic in order", "[message_handler][var][ordering]") {
    MessageHandlerFixture handler;
    ExecutionTracker tracker;
    std::atomic<bool> first_var_processing{false};
    std::atomic<bool> release_first_var{false};

    auto handler_func = std::make_shared<Handler>([&](const std::string& topic, const json& data) {
        int seq = data.value("sequence", 0);

        if (seq == 1) {
            first_var_processing = true;
            while (!release_first_var) {
                std::this_thread::sleep_for(10ms);
            }
        }

        tracker.record(topic, seq);
    });
    auto test_handler = std::make_shared<TypedHandler>(HandlerType::SubscribeVar, handler_func);

    handler->register_handler("module/impl/var_name", test_handler);

    // Send 3 var messages to same topic
    for (int i = 1; i <= 3; ++i) {
        ParsedMessage msg;
        msg.topic = "module/impl/var_name";
        msg.data = {{"msg_type", "Var"}, {"data", {{"data", {{"sequence", i}}}}}};
        handler->add(msg);
    }

    // Wait for first message to start processing
    while (!first_var_processing) {
        std::this_thread::sleep_for(10ms);
    }

    // Give time for potential out-of-order processing (shouldn't happen)
    std::this_thread::sleep_for(100ms);
    CHECK(tracker.count() == 0); // First message still blocked

    // Release first message
    release_first_var = true;

    // Wait for all messages
    tracker.wait_for_count(3);

    auto events = tracker.get_events();
    REQUIRE(events.size() == 3);

    // Verify order
    CHECK(events[0].sequence == 1);
    CHECK(events[1].sequence == 2);
    CHECK(events[2].sequence == 3);
}

TEST_CASE("MessageHandler handles multiple Var subscribers to same topic", "[message_handler][var]") {
    MessageHandlerFixture handler;
    ExecutionTracker tracker1;
    ExecutionTracker tracker2;

    auto handler_func1 = std::make_shared<Handler>(
        [&](const std::string& topic, const json& data) { tracker1.record(topic, data.value("sequence", 0)); });
    auto test_handler1 = std::make_shared<TypedHandler>(HandlerType::SubscribeVar, handler_func1);

    auto handler_func2 = std::make_shared<Handler>(
        [&](const std::string& topic, const json& data) { tracker2.record(topic, data.value("sequence", 0)); });
    auto test_handler2 = std::make_shared<TypedHandler>(HandlerType::SubscribeVar, handler_func2);

    handler->register_handler("module/impl/var_name", test_handler1);
    handler->register_handler("module/impl/var_name", test_handler2);

    ParsedMessage msg;
    msg.topic = "module/impl/var_name";
    msg.data = {{"msg_type", "Var"}, {"data", {{"data", {{"sequence", 99}}}}}};

    handler->add(msg);

    tracker1.wait_for_count(1);
    tracker2.wait_for_count(1);

    auto events1 = tracker1.get_events();
    auto events2 = tracker2.get_events();

    REQUIRE(events1.size() == 1);
    REQUIRE(events2.size() == 1);
    CHECK(events1[0].sequence == 99);
    CHECK(events2[0].sequence == 99);
}

// ============================================================================
// Test: ExternalMQTT Message Processing
// ============================================================================

TEST_CASE("MessageHandler processes ExternalMQTT messages", "[message_handler][external_mqtt]") {
    MessageHandlerFixture handler;
    ExecutionTracker tracker;

    auto handler_func = std::make_shared<Handler>([&](const std::string& topic, const json& data) {
        // ExternalMQTT passes data directly, not nested
        tracker.record(topic, data.value("value", 0));
    });
    auto test_handler = std::make_shared<TypedHandler>(HandlerType::ExternalMQTT, handler_func);

    handler->register_handler("external/topic/+", test_handler);

    ParsedMessage msg;
    msg.topic = "external/topic/sensor1";
    msg.data = {{"value", 123}}; // ExternalMQTT uses data directly, no msg_type

    handler->add(msg);

    tracker.wait_for_count(1);

    auto events = tracker.get_events();
    REQUIRE(events.size() == 1);
    CHECK(events[0].topic == "external/topic/sensor1");
    CHECK(events[0].sequence == 123);
}

TEST_CASE("MessageHandler handles ExternalMQTT with wildcard topics", "[message_handler][external_mqtt]") {
    MessageHandlerFixture handler;
    ExecutionTracker tracker;

    auto handler_func = std::make_shared<Handler>(
        [&](const std::string& topic, const json& data) { tracker.record(topic, data.value("value", 0)); });
    auto test_handler = std::make_shared<TypedHandler>(HandlerType::ExternalMQTT, handler_func);

    // Register with wildcard
    handler->register_handler("sensors/#", test_handler);

    // Send messages to different sub-topics
    for (int i = 1; i <= 3; ++i) {
        ParsedMessage msg;
        msg.topic = "sensors/floor" + std::to_string(i) + "/temp";
        msg.data = {{"value", i * 10}}; // ExternalMQTT uses data directly
        handler->add(msg);
    }

    tracker.wait_for_count(3);

    auto events = tracker.get_events();
    REQUIRE(events.size() == 3);

    // Verify all different topics were received
    std::set<std::string> topics;
    for (const auto& event : events) {
        topics.insert(event.topic);
    }
    CHECK(topics.size() == 3);
}

// ============================================================================
// Test: Error Message Processing
// ============================================================================

TEST_CASE("MessageHandler processes RaiseError messages", "[message_handler][error]") {
    MessageHandlerFixture handler;
    ExecutionTracker tracker;

    auto handler_func = std::make_shared<Handler>(
        [&](const std::string& topic, const json& data) { tracker.record(topic, data.value("error_code", 0)); });
    auto test_handler = std::make_shared<TypedHandler>(HandlerType::SubscribeError, handler_func);

    handler->register_handler("module/impl/error/#", test_handler);

    ParsedMessage msg;
    msg.topic = "module/impl/error/critical";
    msg.data = {{"msg_type", "RaiseError"}, {"data", {{"error_code", 500}}}};

    handler->add(msg);

    tracker.wait_for_count(1);

    auto events = tracker.get_events();
    REQUIRE(events.size() == 1);
    CHECK(events[0].sequence == 500);
}

TEST_CASE("MessageHandler processes ClearError messages", "[message_handler][error]") {
    MessageHandlerFixture handler;
    ExecutionTracker tracker;

    auto handler_func =
        std::make_shared<Handler>([&](const std::string& topic, const json& data) { tracker.record(topic, 0); });
    auto test_handler = std::make_shared<TypedHandler>(HandlerType::SubscribeError, handler_func);

    handler->register_handler("module/impl/error/#", test_handler);

    ParsedMessage msg;
    msg.topic = "module/impl/error/critical";
    msg.data = {{"msg_type", "ClearError"}, {"data", json{}}};

    handler->add(msg);

    tracker.wait_for_count(1);

    auto events = tracker.get_events();
    CHECK(events.size() == 1);
}

// ============================================================================
// Test: ConfigurationRequest Message Processing
// ============================================================================

TEST_CASE("MessageHandler processes ConfigurationRequest messages", "[message_handler][config]") {
    MessageHandlerFixture handler;
    ExecutionTracker tracker;

    auto handler_func =
        std::make_shared<Handler>([&](const std::string& topic, const json& data) { tracker.record(topic, 1); });
    auto test_handler = std::make_shared<TypedHandler>(HandlerType::ConfigurationRequest, handler_func);

    handler->register_handler("config/request", test_handler);

    ParsedMessage msg;
    msg.topic = "config/request";
    msg.data = {{"msg_type", "ConfigurationRequest"}, {"data", json{}}};

    handler->add(msg);

    tracker.wait_for_count(1);

    auto events = tracker.get_events();
    CHECK(events.size() == 1);
}

// ============================================================================
// Test: Mixed Message Types
// ============================================================================

TEST_CASE("MessageHandler handles mixed message types concurrently", "[message_handler][mixed]") {
    MessageHandlerFixture handler;
    ExecutionTracker cmd_tracker;
    ExecutionTracker var_tracker;
    ExecutionTracker ext_tracker;

    auto cmd_handler_func = std::make_shared<Handler>(
        [&](const std::string& topic, const json& data) { cmd_tracker.record(topic, data.value("sequence", 0)); });
    auto cmd_handler = std::make_shared<TypedHandler>(HandlerType::Call, cmd_handler_func);

    auto var_handler_func = std::make_shared<Handler>(
        [&](const std::string& topic, const json& data) { var_tracker.record(topic, data.value("sequence", 0)); });
    auto var_handler = std::make_shared<TypedHandler>(HandlerType::SubscribeVar, var_handler_func);

    auto ext_handler_func = std::make_shared<Handler>(
        [&](const std::string& topic, const json& data) { ext_tracker.record(topic, data.value("sequence", 0)); });
    auto ext_handler = std::make_shared<TypedHandler>(HandlerType::ExternalMQTT, ext_handler_func);

    handler->register_handler("cmd/topic", cmd_handler);
    handler->register_handler("var/topic", var_handler);
    handler->register_handler("ext/topic", ext_handler);

    // Send mixed message types
    ParsedMessage cmd_msg = create_cmd_message("cmd/topic", 1);

    ParsedMessage var_msg;
    var_msg.topic = "var/topic";
    var_msg.data = {{"msg_type", "Var"}, {"data", {{"data", {{"sequence", 2}}}}}};

    ParsedMessage ext_msg;
    ext_msg.topic = "ext/topic";
    ext_msg.data = {{"sequence", 3}}; // ExternalMQTT uses data directly

    handler->add(cmd_msg);
    handler->add(var_msg);
    handler->add(ext_msg);

    cmd_tracker.wait_for_count(1);
    var_tracker.wait_for_count(1);
    ext_tracker.wait_for_count(1);

    CHECK(cmd_tracker.get_events().size() == 1);
    CHECK(var_tracker.get_events().size() == 1);
    CHECK(ext_tracker.get_events().size() == 1);
}

TEST_CASE("MessageHandler: GlobalReady arrives before register_handler") {
    auto handler = std::make_unique<MessageHandler>();

    ExecutionTracker tracker;
    bool handler_called = false;

    // Send GlobalReady BEFORE registering the handler (critical race condition)
    ParsedMessage ready_msg;
    ready_msg.topic = "global";
    ready_msg.data = {{"msg_type", "GlobalReady"}, {"data", {{"ready_data", true}}}};
    handler->add(ready_msg);

    // Give the ready_thread a moment to execute (it should find no handler registered)
    std::this_thread::sleep_for(100ms);

    // Now register the handler - too late, the ready_thread has already exited
    auto ready_handler_func = std::make_shared<Handler>([&](const std::string& topic, const json& data) {
        handler_called = true;
        tracker.record(topic, 1);
    });
    auto ready_handler = std::make_shared<TypedHandler>(HandlerType::GlobalReady, ready_handler_func);
    handler->register_handler("global", ready_handler);

    // Wait briefly to ensure no deferred execution
    std::this_thread::sleep_for(100ms);

    // Handler should NOT have been called since it was registered after the message arrived
    CHECK(handler_called == false);
    CHECK(tracker.count() == 0);

    handler->stop();
}

TEST_CASE("MessageHandler: Per-topic mutual exclusion (at most one in-flight per topic)") {
    auto handler = std::make_unique<MessageHandler>();

    // Track concurrent execution for each topic
    std::map<std::string, std::atomic<int>> in_flight_count;
    std::map<std::string, int> max_concurrent;
    std::mutex concurrent_mutex;

    ExecutionTracker tracker;

    auto create_blocking_handler = [&](const std::string& topic) {
        auto handler_func = std::make_shared<Handler>([&, topic](const std::string& msg_topic, const json& data) {
            // Increment in-flight count for this topic
            in_flight_count[topic]++;
            int current = in_flight_count[topic];

            // Record the start of execution
            tracker.record(msg_topic, data.value("sequence", 0));

            {
                std::lock_guard<std::mutex> lock(concurrent_mutex);
                max_concurrent[topic] = std::max(max_concurrent[topic], current);
            }

            // Simulate work with a delay
            std::this_thread::sleep_for(50ms);

            // Decrement in-flight count
            in_flight_count[topic]--;
        });
        return std::make_shared<TypedHandler>(HandlerType::SubscribeVar, handler_func);
    };

    // Register handlers for multiple topics
    handler->register_handler("topic/A", create_blocking_handler("topic/A"));
    handler->register_handler("topic/B", create_blocking_handler("topic/B"));

    // Send 3 messages for topic A (should be queued, not concurrent)
    for (int i = 1; i <= 3; ++i) {
        ParsedMessage msg = create_var_message("topic/A", i);
        handler->add(msg);
    }

    // Send 3 messages for topic B (should also not be concurrent with each other)
    for (int i = 1; i <= 3; ++i) {
        ParsedMessage msg = create_var_message("topic/B", i);
        handler->add(msg);
    }

    // Wait for all messages to be processed
    tracker.wait_for_count(6, 10000ms);

    // Verify both topics processed all 3 messages
    auto events = tracker.get_events();
    std::map<std::string, int> processed_count;
    for (const auto& event : events) {
        processed_count[event.topic]++;
    }

    CHECK(processed_count["topic/A"] == 3);
    CHECK(processed_count["topic/B"] == 3);

    // Verify at most 1 message in-flight per topic at any time
    CHECK(max_concurrent["topic/A"] <= 1);
    CHECK(max_concurrent["topic/B"] <= 1);

    // Verify ordering is preserved within each topic
    int last_seq_A = 0;
    int last_seq_B = 0;
    for (const auto& event : events) {
        if (event.topic == "topic/A") {
            CHECK(event.sequence > last_seq_A);
            last_seq_A = event.sequence;
        } else if (event.topic == "topic/B") {
            CHECK(event.sequence > last_seq_B);
            last_seq_B = event.sequence;
        }
    }

    handler->stop();
}
