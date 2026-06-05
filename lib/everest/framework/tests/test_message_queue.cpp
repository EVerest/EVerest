// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <utils/message_queue.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <everest/io/event/fd_event_handler.hpp>

using namespace std::chrono_literals;

TEST_CASE("TransportCallbackQueue direct drain delivers FIFO on caller thread") {
    Everest::TransportCallbackQueue queue;
    const auto caller_thread = std::this_thread::get_id();

    REQUIRE(queue.enqueue(Everest::Message("topic/one", "one")));
    REQUIRE(queue.enqueue(Everest::Message("topic/two", "two")));
    REQUIRE(queue.enqueue(Everest::Message("topic/three", "three")));

    std::vector<std::string> topics;

    std::thread::id callback_thread;
    const auto drained = queue.drain([&](const Everest::Message& message) {
        callback_thread = std::this_thread::get_id();
        topics.push_back(message.topic);
    });

    REQUIRE(drained == 3);
    REQUIRE(callback_thread == caller_thread);
    REQUIRE(topics == std::vector<std::string>{"topic/one", "topic/two", "topic/three"});
}

TEST_CASE("TransportCallbackQueue coalesces wakes and direct drain clears wake fd") {
    Everest::TransportCallbackQueue queue;

    REQUIRE(queue.enqueue(Everest::Message("topic/one", "one")));
    REQUIRE(queue.enqueue(Everest::Message("topic/two", "two")));
    REQUIRE(queue.enqueue(Everest::Message("topic/three", "three")));

    const auto wake_count = queue.event_fd()->read();
    REQUIRE(wake_count);
    REQUIRE(*wake_count == 1);
    REQUIRE_FALSE(queue.event_fd()->read());

    std::size_t callbacks = 0;
    REQUIRE(queue.drain([&](const Everest::Message&) { ++callbacks; }) == 3);
    REQUIRE(callbacks == 3);

    REQUIRE(queue.enqueue(Everest::Message("topic/four", "four")));
    REQUIRE(queue.drain([&](const Everest::Message&) { ++callbacks; }) == 1);
    REQUIRE_FALSE(queue.event_fd()->read());
}

TEST_CASE("TransportCallbackQueue drains from fd_event_handler registration") {
    Everest::TransportCallbackQueue queue;
    everest::lib::io::event::fd_event_handler handler;

    std::vector<std::string> topics;
    REQUIRE(queue.register_events(handler, [&](const Everest::Message& message) { topics.push_back(message.topic); }));

    REQUIRE(queue.enqueue(Everest::Message("topic/one", "one")));
    REQUIRE(queue.enqueue(Everest::Message("topic/two", "two")));

    REQUIRE(handler.poll(1s));
    REQUIRE(topics == std::vector<std::string>{"topic/one", "topic/two"});
    REQUIRE_FALSE(handler.poll(0ms));

    REQUIRE(queue.unregister_events(handler));
}

TEST_CASE("TransportCallbackQueue recovers when drain callback throws") {
    Everest::TransportCallbackQueue queue;

    REQUIRE(queue.enqueue(Everest::Message("topic/throws", "{}")));

    REQUIRE_THROWS_AS(queue.drain([](const Everest::Message&) { throw std::runtime_error("callback failed"); }),
                      std::runtime_error);

    REQUIRE(queue.enqueue(Everest::Message("topic/after-throw", "{}")));

    std::vector<std::string> topics;
    REQUIRE(queue.drain([&](const Everest::Message& message) { topics.push_back(message.topic); }) == 1);
    REQUIRE(topics == std::vector<std::string>{"topic/after-throw"});
}

TEST_CASE("TransportCallbackQueue close rejects new messages and wakes event loop") {
    Everest::TransportCallbackQueue queue;

    REQUIRE(queue.enqueue(Everest::Message("topic/before-close", "{}")));
    queue.close();

    REQUIRE(queue.closed());
    REQUIRE_FALSE(queue.enqueue(Everest::Message("topic/after-close", "{}")));

    auto wake_count = queue.event_fd()->read();
    REQUIRE(wake_count);
    REQUIRE(*wake_count >= 1);

    std::vector<std::string> topics;
    REQUIRE(queue.drain([&](const Everest::Message& message) { topics.push_back(message.topic); }) == 1);
    REQUIRE(topics == std::vector<std::string>{"topic/before-close"});
}

TEST_CASE("TransportCallbackQueue drains messages enqueued during callback") {
    Everest::TransportCallbackQueue queue;
    std::vector<std::string> topics;

    REQUIRE(queue.enqueue(Everest::Message("topic/one", "one")));

    const auto drained = queue.drain([&](const Everest::Message& message) {
        topics.push_back(message.topic);

        if (message.topic == "topic/one") {
            REQUIRE(queue.enqueue(Everest::Message("topic/two", "two")));
            REQUIRE(queue.enqueue(Everest::Message("topic/three", "three")));
        }
    });

    REQUIRE(drained == 3);
    REQUIRE(topics == std::vector<std::string>{"topic/one", "topic/two", "topic/three"});
    REQUIRE_FALSE(queue.event_fd()->read());
}
