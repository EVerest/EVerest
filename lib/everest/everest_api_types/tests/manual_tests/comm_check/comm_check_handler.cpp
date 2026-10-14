// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest_api_types/utilities/CommCheckHandler.hpp>

#include <utils/error/error_database_map.hpp>
#include <utils/error/error_type_map.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;
using everest::lib::API::CommCheckHandlerBase;

const std::string error_type{"test/CommunicationFault"};
const std::string default_sub_type{"Communication check timeout"};
const std::string init_sub_type{"Initial communication check"};

bool wait_until(const std::function<bool()>& pred, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) {
            return true;
        }
        std::this_thread::sleep_for(10ms);
    }
    return pred();
}

class CommCheckHandlerTest : public ::testing::Test {
private:
    void record(std::vector<std::string>& list, const Everest::error::Error& error) {
        std::scoped_lock lock(record_mutex);
        list.push_back(error.sub_type);
    }

    // Declared before the handler so they outlive its threads during teardown.
    std::mutex record_mutex;
    std::vector<std::string> raised;
    std::vector<std::string> cleared;

protected:
    CommCheckHandlerTest() {
        error_type_map->load_error_types_map({{error_type, "Test communication fault"}});
        auto database = std::make_shared<Everest::error::ErrorDatabaseMap>();
        error_state_monitor = std::make_shared<Everest::error::ErrorStateMonitor>(database);
        error_factory = std::make_shared<Everest::error::ErrorFactory>(error_type_map);
        error_manager = std::make_shared<Everest::error::ErrorManagerImpl>(
            error_type_map, database, std::list<Everest::error::ErrorType>{error_type},
            [this](const Everest::error::Error& error) { record(raised, error); },
            [this](const Everest::error::Error& error) { record(cleared, error); });
        handler = std::make_unique<CommCheckHandlerBase>(error_type, default_sub_type, error_state_monitor,
                                                         error_factory, error_manager);
    }

    bool is_active(const std::string& sub_type) const {
        return error_state_monitor->is_error_active(error_type, sub_type);
    }

    std::size_t raised_count() {
        std::scoped_lock lock(record_mutex);
        return raised.size();
    }

    std::size_t cleared_count() {
        std::scoped_lock lock(record_mutex);
        return cleared.size();
    }

    std::shared_ptr<Everest::error::ErrorTypeMap> error_type_map{std::make_shared<Everest::error::ErrorTypeMap>()};
    std::shared_ptr<Everest::error::ErrorStateMonitor> error_state_monitor;
    std::shared_ptr<Everest::error::ErrorFactory> error_factory;
    std::shared_ptr<Everest::error::ErrorManagerImpl> error_manager;
    std::unique_ptr<CommCheckHandlerBase> handler;
};

TEST_F(CommCheckHandlerTest, start_with_zero_timeout_disables_checks) {
    handler->start(0);
    std::this_thread::sleep_for(50ms);
    EXPECT_FALSE(is_active(init_sub_type));

    handler->set_error();
    EXPECT_FALSE(is_active(default_sub_type));
    EXPECT_EQ(raised_count(), 0);
}

TEST_F(CommCheckHandlerTest, set_error_before_start_raises_default_sub_type) {
    handler->set_error();
    EXPECT_TRUE(is_active(default_sub_type));
    EXPECT_TRUE(handler->check_error());
}

TEST_F(CommCheckHandlerTest, initial_error_raised_only_after_timeout) {
    handler->start(1);
    std::this_thread::sleep_for(500ms);
    EXPECT_FALSE(is_active(init_sub_type));
    EXPECT_EQ(raised_count(), 0);

    EXPECT_TRUE(wait_until([this] { return is_active(init_sub_type); }, 2s));
}

TEST_F(CommCheckHandlerTest, value_before_timeout_prevents_initial_error) {
    handler->start(1);
    handler->set_value(true);
    std::this_thread::sleep_for(800ms);
    EXPECT_FALSE(is_active(init_sub_type));
    EXPECT_EQ(raised_count(), 0);
}

TEST_F(CommCheckHandlerTest, value_clears_initial_error) {
    handler->start(1);
    ASSERT_TRUE(wait_until([this] { return is_active(init_sub_type); }, 2s));

    handler->set_value(true);
    EXPECT_TRUE(wait_until([this] { return not is_active(init_sub_type); }, 500ms));
    EXPECT_FALSE(handler->check_error());
}

TEST_F(CommCheckHandlerTest, timeout_raises_default_error) {
    handler->start(1);
    handler->set_value(true);

    EXPECT_TRUE(wait_until([this] { return is_active(default_sub_type); }, 3s));
    EXPECT_FALSE(is_active(init_sub_type));
}

TEST_F(CommCheckHandlerTest, value_after_timeout_clears_default_error) {
    handler->start(1);
    handler->set_value(true);
    ASSERT_TRUE(wait_until([this] { return is_active(default_sub_type); }, 3s));

    handler->set_value(true);
    EXPECT_TRUE(wait_until([this] { return not is_active(default_sub_type); }, 500ms));
}

TEST_F(CommCheckHandlerTest, false_value_stops_checks) {
    handler->start(1);
    handler->set_value(false);

    std::this_thread::sleep_for(1500ms);
    EXPECT_FALSE(handler->check_error());
    EXPECT_EQ(raised_count(), 0);

    handler->set_error();
    EXPECT_FALSE(is_active(default_sub_type));
}

TEST_F(CommCheckHandlerTest, heartbeat_rejects_non_positive_interval) {
    std::atomic_int calls{0};
    auto ftor = [&calls] {
        ++calls;
        return true;
    };
    EXPECT_FALSE(handler->heartbeat(0, ftor));
    EXPECT_FALSE(handler->heartbeat(-1, ftor));
    std::this_thread::sleep_for(50ms);
    EXPECT_EQ(calls.load(), 0);
}

TEST_F(CommCheckHandlerTest, heartbeat_calls_functor_repeatedly) {
    std::atomic_int calls{0};
    ASSERT_TRUE(handler->heartbeat(10, [&calls] {
        ++calls;
        return true;
    }));
    EXPECT_TRUE(wait_until([&calls] { return calls.load() >= 5; }, 1s));
}

TEST_F(CommCheckHandlerTest, heartbeat_stops_when_functor_fails) {
    std::atomic_int calls{0};
    ASSERT_TRUE(handler->heartbeat(10, [&calls] { return ++calls < 3; }));
    ASSERT_TRUE(wait_until([&calls] { return calls.load() == 3; }, 1s));

    std::this_thread::sleep_for(100ms);
    EXPECT_EQ(calls.load(), 3);
}

TEST_F(CommCheckHandlerTest, destruction_stops_running_threads) {
    handler->start(1);
    std::atomic_int calls{0};
    ASSERT_TRUE(handler->heartbeat(10, [&calls] {
        ++calls;
        return true;
    }));
    ASSERT_TRUE(wait_until([this] { return is_active(init_sub_type); }, 2s));

    const auto begin = std::chrono::steady_clock::now();
    handler.reset();
    EXPECT_LT(std::chrono::steady_clock::now() - begin, 500ms);

    EXPECT_TRUE(is_active(init_sub_type));
    EXPECT_EQ(cleared_count(), 0);

    const auto calls_after_reset = calls.load();
    std::this_thread::sleep_for(50ms);
    EXPECT_EQ(calls.load(), calls_after_reset);
}

} // namespace
