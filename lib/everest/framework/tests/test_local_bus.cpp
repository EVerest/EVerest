// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <framework/local_bus.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

using namespace Everest;
using namespace std::chrono_literals;

namespace {

struct Reading {
    int sequence;
    double value;
};

Fulfillment fulfillment_for(std::string_view module_id, std::string_view requirement_id) {
    if (module_id == "consumer" and requirement_id == "meter") {
        return Fulfillment{"provider", "main", Requirement{"meter", 0}};
    }
    if (module_id == "consumer" and requirement_id == "remote") {
        return Fulfillment{"remote_module", "main", Requirement{"remote", 0}};
    }
    return Fulfillment{"", "", Requirement{std::string(requirement_id), 0}};
}

LocalBus::Resolver resolver() {
    return [](std::string_view module_id, std::string_view requirement_id) {
        const auto f = fulfillment_for(module_id, requirement_id);
        if (f.module_id.empty()) {
            return std::vector<Fulfillment>{};
        }
        return std::vector<Fulfillment>{f};
    };
}

class Collector {
public:
    void add(const Reading& reading, const Reading* address) {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_sequences.push_back(reading.sequence);
        m_addresses.push_back(address);
        m_threads.push_back(std::this_thread::get_id());
        m_cv.notify_all();
    }
    bool wait_for(std::size_t count, std::chrono::milliseconds timeout = 5000ms) {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_cv.wait_for(lock, timeout, [&] { return m_sequences.size() >= count; });
    }
    std::vector<int> sequences() const {
        const std::lock_guard<std::mutex> lock(m_mutex);
        return m_sequences;
    }
    std::vector<const Reading*> addresses() const {
        const std::lock_guard<std::mutex> lock(m_mutex);
        return m_addresses;
    }
    std::vector<std::thread::id> threads() const {
        const std::lock_guard<std::mutex> lock(m_mutex);
        return m_threads;
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::vector<int> m_sequences;
    std::vector<const Reading*> m_addresses;
    std::vector<std::thread::id> m_threads;
};

} // namespace

TEST_CASE("LocalBus delivers variables in order, off the publisher thread, without copies", "[local_bus]") {
    LocalBus bus(resolver());
    bus.register_local_module("provider");
    bus.register_local_module("consumer");

    Collector first;
    Collector second;
    const Requirement req{"meter", 0};
    REQUIRE(bus.subscribe<Reading>("consumer", req, "reading", [&](const Reading& r) { first.add(r, &r); }));
    REQUIRE(bus.subscribe<Reading>("consumer", req, "reading", [&](const Reading& r) { second.add(r, &r); }));

    constexpr int count = 10000;
    for (int i = 0; i < count; i++) {
        bus.publish<Reading>("provider", "main", "reading", std::make_shared<const Reading>(Reading{i, i * 0.5}));
    }

    REQUIRE(first.wait_for(count));
    REQUIRE(second.wait_for(count));

    const auto sequences = first.sequences();
    for (int i = 0; i < count; i++) {
        REQUIRE(sequences.at(i) == i);
    }
    REQUIRE(first.addresses() == second.addresses());
    for (const auto& id : first.threads()) {
        REQUIRE(id != std::this_thread::get_id());
    }
}

TEST_CASE("LocalBus subscribe reports non-local providers", "[local_bus]") {
    LocalBus bus(resolver());
    bus.register_local_module("consumer");

    REQUIRE_FALSE(bus.subscribe<Reading>("consumer", Requirement{"remote", 0}, "reading", [](const Reading&) {}));
    REQUIRE_FALSE(bus.subscribe<Reading>("consumer", Requirement{"unknown", 0}, "reading", [](const Reading&) {}));
    REQUIRE_FALSE(bus.subscribe<Reading>("consumer", Requirement{"meter", 3}, "reading", [](const Reading&) {}));
}

TEST_CASE("LocalBus commands run on the caller thread and wrap exceptions", "[local_bus]") {
    LocalBus bus(resolver());
    bus.register_local_module("provider");
    bus.register_local_module("consumer");
    const Requirement req{"meter", 0};

    REQUIRE(bus.find_cmd<int, int>("consumer", req, "add_one") == nullptr);

    std::thread::id handler_thread;
    bus.provide("provider", "main", "add_one", LocalBus::CmdFn<int, int>([&](const int& value) {
                    handler_thread = std::this_thread::get_id();
                    return value + 1;
                }));
    bus.provide("provider", "main", "fail",
                LocalBus::CmdFn<void, std::string>([](const std::string& what) { throw std::runtime_error(what); }));
    bus.provide("provider", "main", "not_ready", LocalBus::CmdFn<void>([]() { throw NotReady("later"); }));

    const auto add_one = bus.find_cmd<int, int>("consumer", req, "add_one");
    REQUIRE(add_one != nullptr);
    REQUIRE((*add_one)(41) == 42);
    REQUIRE(handler_thread == std::this_thread::get_id());

    const auto fail = bus.find_cmd<void, std::string>("consumer", req, "fail");
    REQUIRE(fail != nullptr);
    REQUIRE_THROWS_AS((*fail)("boom"), HandlerException);

    const auto not_ready = bus.find_cmd<void>("consumer", req, "not_ready");
    REQUIRE(not_ready != nullptr);
    REQUIRE_THROWS_AS((*not_ready)(), NotReady);

    REQUIRE(bus.find_cmd<int, int>("consumer", Requirement{"remote", 0}, "add_one") == nullptr);
}

TEST_CASE("LocalBus stop drops pending deliveries", "[local_bus]") {
    LocalBus bus(resolver());
    bus.register_local_module("provider");
    bus.register_local_module("consumer");

    std::atomic<int> delivered{0};
    REQUIRE(bus.subscribe<Reading>("consumer", Requirement{"meter", 0}, "reading", [&](const Reading&) {
        std::this_thread::sleep_for(50ms);
        delivered++;
    }));
    for (int i = 0; i < 20; i++) {
        bus.publish<Reading>("provider", "main", "reading", std::make_shared<const Reading>(Reading{i, 0.0}));
    }
    bus.stop();
    bus.publish<Reading>("provider", "main", "reading", std::make_shared<const Reading>(Reading{99, 0.0}));
    std::this_thread::sleep_for(200ms);
    REQUIRE(delivered < 20);
}

TEST_CASE("LocalBus routes errors to matching and global subscribers", "[local_bus]") {
    LocalBus bus(resolver());
    bus.register_local_module("provider");
    bus.register_local_module("consumer");

    std::mutex mutex;
    std::condition_variable cv;
    std::vector<std::string> events;
    const auto record = [&](const std::string& prefix) {
        return [&, prefix](const error::Error& e) {
            const std::lock_guard<std::mutex> lock(mutex);
            events.push_back(prefix + e.type);
            cv.notify_all();
        };
    };
    bus.subscribe_error("consumer", "provider", "main", "evse_board_support/MREC2GroundFailure", record("raise:"),
                        record("clear:"));
    bus.subscribe_all_errors("consumer", record("global-raise:"), record("global-clear:"));

    error::Error raised;
    raised.type = "evse_board_support/MREC2GroundFailure";
    raised.state = error::State::Active;
    bus.publish_error("provider", "main", raised);
    error::Error other = raised;
    other.type = "evse_board_support/VendorError";
    bus.publish_error("provider", "main", other);
    error::Error cleared = raised;
    cleared.state = error::State::ClearedByModule;
    bus.publish_error("provider", "main", cleared);

    std::unique_lock<std::mutex> lock(mutex);
    REQUIRE(cv.wait_for(lock, 5s, [&] { return events.size() == 5; }));
    const auto count = [&](const std::string& event) { return std::count(events.begin(), events.end(), event); };
    REQUIRE(count("raise:evse_board_support/MREC2GroundFailure") == 1);
    REQUIRE(count("clear:evse_board_support/MREC2GroundFailure") == 1);
    REQUIRE(count("global-raise:evse_board_support/MREC2GroundFailure") == 1);
    REQUIRE(count("global-raise:evse_board_support/VendorError") == 1);
    REQUIRE(count("global-clear:evse_board_support/MREC2GroundFailure") == 1);
}
