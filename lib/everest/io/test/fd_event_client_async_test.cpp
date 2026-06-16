// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include <everest/io/event/event_fd.hpp>
#include <everest/io/event/fd_event_client.hpp>
#include <everest/io/utilities/event_client_async_policy.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <future>

#include <gtest/gtest.h>

using namespace std::chrono_literals;
using namespace everest::lib::io;

using everest::lib::io::event::event_fd;
using everest::lib::io::event::fd_event_client;
using everest::lib::io::utilities::event_client_async_policy_v;

namespace {

struct connect_attempt_plan {
    bool ok;
    int error_code;
};

class async_connect_control {
public:
    explicit async_connect_control(std::vector<connect_attempt_plan> attempts = {}) :
        m_planned_attempts{std::move(attempts)} {
    }

    std::size_t register_attempt() {
        std::lock_guard lk(m_mutex);
        auto const idx = m_attempts.size();
        auto const configured_ok = idx < m_planned_attempts.size() ? m_planned_attempts[idx].ok : false;
        auto const configured_error = idx < m_planned_attempts.size() ? m_planned_attempts[idx].error_code : 0;
        m_attempts.push_back({configured_ok, configured_error, true, false, false, false});
        m_cv.notify_all();
        return idx;
    }

    bool should_succeed(std::size_t idx) const {
        std::lock_guard lk(m_mutex);
        if (idx >= m_attempts.size()) {
            return false;
        }
        return m_attempts[idx].ok;
    }

    int error_code(std::size_t idx) const {
        std::lock_guard lk(m_mutex);
        if (idx >= m_attempts.size()) {
            return 0;
        }
        return m_attempts[idx].error_code;
    }

    void release(std::size_t idx) {
        std::lock_guard lk(m_mutex);
        if (idx < m_attempts.size()) {
            m_attempts[idx].released = true;
            m_cv.notify_all();
        }
    }

    void release_all() {
        std::lock_guard lk(m_mutex);
        for (auto& item : m_attempts) {
            item.released = true;
        }
        m_cv.notify_all();
    }

    void wait_for_release(std::size_t idx) {
        std::unique_lock lk(m_mutex);
        m_cv.wait(lk, [&] { return idx < m_attempts.size() and m_attempts[idx].released; });
    }

    bool wait_for_attempt_count(std::size_t attempts, std::chrono::milliseconds timeout) const {
        std::unique_lock lk(m_mutex);
        return m_cv.wait_for(lk, timeout, [&] { return m_attempts.size() >= attempts; });
    }

    bool wait_for_attempt_started(std::size_t idx, std::chrono::milliseconds timeout) const {
        std::unique_lock lk(m_mutex);
        return m_cv.wait_for(lk, timeout, [&] { return idx < m_attempts.size() and m_attempts[idx].started; });
    }

    bool wait_for_attempt_completed(std::size_t idx, std::chrono::milliseconds timeout) const {
        std::unique_lock lk(m_mutex);
        return m_cv.wait_for(lk, timeout, [&] { return idx < m_attempts.size() and m_attempts[idx].completed; });
    }

    void mark_policy_destroyed(std::size_t idx) {
        std::lock_guard lk(m_mutex);
        if (idx < m_attempts.size()) {
            m_attempts[idx].policy_destroyed = true;
            m_cv.notify_all();
        }
    }

    bool wait_for_attempt_policy_destroyed(std::size_t idx, std::chrono::milliseconds timeout) const {
        std::unique_lock lk(m_mutex);
        return m_cv.wait_for(lk, timeout, [&] { return idx < m_attempts.size() and m_attempts[idx].policy_destroyed; });
    }

    bool is_attempt_policy_destroyed(std::size_t idx) const {
        std::lock_guard lk(m_mutex);
        return idx < m_attempts.size() and m_attempts[idx].policy_destroyed;
    }

    void mark_completed(std::size_t idx) {
        std::lock_guard lk(m_mutex);
        if (idx < m_attempts.size()) {
            m_attempts[idx].completed = true;
            m_cv.notify_all();
        }
    }

    std::size_t attempt_count() const {
        std::lock_guard lk(m_mutex);
        return m_attempts.size();
    }

private:
    struct attempt_state {
        bool ok;
        int error_code;
        bool started;
        bool released;
        bool completed;
        bool policy_destroyed;
    };

    std::vector<connect_attempt_plan> m_planned_attempts;
    mutable std::mutex m_mutex;
    mutable std::condition_variable m_cv;
    std::vector<attempt_state> m_attempts;
};

template <class Client, class Predicate>
bool pump_until(Client& client, std::chrono::milliseconds timeout, Predicate&& predicate) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        client.sync(1ms);
        if (predicate()) {
            return true;
        }
    }
    return predicate();
}

template <class Client, class Predicate>
bool pump_until_before_sync(Client& client, std::chrono::milliseconds timeout, Predicate&& predicate) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        client.sync(1ms);
    }
    return predicate();
}

template <class Client>
bool run_sync_and_expect_ready(Client& client, std::shared_ptr<async_connect_control> const& control,
                               std::chrono::milliseconds sync_timeout,
                               std::chrono::milliseconds wait_for_sync_ready = 100ms,
                               std::chrono::milliseconds wait_after_release = 200ms,
                               const char* label = "client.sync()") {
    auto sync_call = std::async(std::launch::async, [&] { client.sync(sync_timeout); });
    auto const status = sync_call.wait_for(wait_for_sync_ready);
    if (status != std::future_status::ready) {
        control->release_all();
        auto const release_status = sync_call.wait_for(wait_after_release);
        if (release_status == std::future_status::ready) {
            sync_call.get();
        }
        EXPECT_EQ(status, std::future_status::ready) << label << " is blocked while async connect is pending";
        return false;
    }

    sync_call.get();
    return true;
}

template <class Client> void pump_for(Client& client, std::chrono::milliseconds timeout) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        client.sync(1ms);
    }
}

class deterministic_async_policy {
public:
    using PayloadT = int;

    deterministic_async_policy() = default;
    explicit deterministic_async_policy(std::shared_ptr<async_connect_control> control) :
        m_control(std::move(control)) {
    }
    ~deterministic_async_policy() {
        if (m_control and m_attempt != std::numeric_limits<std::size_t>::max()) {
            m_control->mark_policy_destroyed(m_attempt);
        }
    }

    bool setup(std::shared_ptr<async_connect_control> control) {
        m_control = std::move(control);
        return static_cast<bool>(m_control);
    }

    void connect(std::function<void(bool, int)> const& cb) {
        if (not m_control) {
            m_last_error = -1;
            cb(false, -1);
            return;
        }

        auto const attempt = m_control->register_attempt();
        auto const ok = m_control->should_succeed(attempt);
        m_attempt = attempt;
        m_last_error = m_control->error_code(attempt);
        m_control->wait_for_release(attempt);
        cb(ok, ok ? m_ready_event.get_raw_fd() : -1);
        m_control->mark_completed(attempt);
    }

    bool tx(PayloadT const&) {
        return false;
    }

    bool rx(PayloadT&) {
        return false;
    }

    int get_fd() const {
        return m_ready_event.get_raw_fd();
    }

    int get_error() const {
        return m_last_error;
    }

private:
    std::shared_ptr<async_connect_control> m_control;
    event_fd m_ready_event;
    int m_last_error{0};
    std::size_t m_attempt{std::numeric_limits<std::size_t>::max()};
};

static_assert(event_client_async_policy_v<deterministic_async_policy>);

} // namespace

using deterministic_async_client = fd_event_client<deterministic_async_policy>::type;

// Verify reset and sync do not block when an async connect callback is still
// pending, protecting against deadlocks in the connect/reset path.
TEST(fd_event_client_async_test, reset_does_not_block_while_connect_is_pending) {
    auto control =
        std::make_shared<async_connect_control>(std::vector<connect_attempt_plan>{{true, 0}, {true, 0}, {true, 0}});
    deterministic_async_client client(control);

    ASSERT_TRUE(pump_until(client, 500ms, [&] { return control->attempt_count() >= 1; }));
    auto sync_call = std::async(std::launch::async, [&] { client.sync(5ms); });
    if (sync_call.wait_for(100ms) != std::future_status::ready) {
        control->release_all();
        EXPECT_TRUE(control->wait_for_attempt_completed(0, 500ms));
        auto const sync_completed_after_release = sync_call.wait_for(200ms);
        EXPECT_EQ(sync_completed_after_release, std::future_status::ready);
        if (sync_completed_after_release == std::future_status::ready) {
            sync_call.get();
        }
        FAIL() << "sync() blocked while async connect was still pending";
        return;
    }
    sync_call.get();

    auto reset_call = std::async(std::launch::async, [&] { client.reset(); });
    auto const reset_completed = reset_call.wait_for(200ms);
    if (reset_completed != std::future_status::ready) {
        control->release_all();
        EXPECT_TRUE(control->wait_for_attempt_completed(0, 500ms));
        auto const reset_completed_after_release = reset_call.wait_for(200ms);
        EXPECT_EQ(reset_completed_after_release, std::future_status::ready);
        if (reset_completed_after_release == std::future_status::ready) {
            reset_call.get();
        }
        FAIL() << "reset() blocked while async connect was still pending";
        return;
    }
    reset_call.get();

    // Keep connect operations pending long enough for the test to catch a blocking
    // regression while still remaining deterministic.
    ASSERT_TRUE(run_sync_and_expect_ready(client, control, 5ms));
    ASSERT_TRUE(pump_until(client, 300ms, [&] { return control->attempt_count() >= 2; }));
    control->release_all();
    ASSERT_TRUE(control->wait_for_attempt_completed(0, 300ms));
    auto const attempts = control->attempt_count();
    for (std::size_t i = 1; i < attempts; ++i) {
        ASSERT_TRUE(control->wait_for_attempt_completed(i, 300ms));
    }
}

// Ensure late callbacks from a pre-reset connect attempt are ignored so a stale
// generation cannot trigger duplicate ready or error signals after reset.
TEST(fd_event_client_async_test, stale_connect_result_is_ignored_after_reset) {
    auto control = std::make_shared<async_connect_control>(std::vector<connect_attempt_plan>{{false, 17}, {true, 0}});
    deterministic_async_client client(control);

    std::atomic<int> ready_calls{0};
    std::atomic<int> error_calls{0};
    client.set_on_ready_action([&] { ++ready_calls; });
    client.set_error_handler([&](int code, std::string const&) {
        if (code != 0) {
            ++error_calls;
        }
    });

    ASSERT_TRUE(pump_until(client, 500ms, [&] { return control->attempt_count() >= 1; }));
    client.reset();
    ASSERT_TRUE(run_sync_and_expect_ready(client, control, 5ms, 100ms, 200ms,
                                          "client.sync() after reset in stale_connect_result_is_ignored_after_reset"));
    ASSERT_TRUE(pump_until(client, 500ms, [&] { return control->attempt_count() >= 2; }));

    control->release(1); // Release current-generation connect before stale attempt.
    EXPECT_TRUE(pump_until(client, 500ms, [&] { return ready_calls.load() == 1; }));
    EXPECT_TRUE(pump_until(client, 300ms, [&] { return error_calls.load() == 0 && not client.on_error(); }));

    control->release(0); // Release stale attempt late; it must not create a second ready/error.
    ASSERT_TRUE(control->wait_for_attempt_completed(0, 200ms));
    ASSERT_TRUE(pump_until(client, 500ms, [&] { return control->is_attempt_policy_destroyed(0); }));
    EXPECT_EQ(ready_calls.load(), 1);
    EXPECT_EQ(error_calls.load(), 0);
    EXPECT_FALSE(client.on_error());
    ASSERT_TRUE(control->wait_for_attempt_completed(1, 300ms));
}

// Ensure tearing down a client while its async connect is pending cannot re-enter
// callbacks on a destroyed object and leaves no side effects.
TEST(fd_event_client_async_test, destroy_client_while_connect_is_pending) {
    auto control = std::make_shared<async_connect_control>(std::vector<connect_attempt_plan>{{true, 0}});
    std::atomic<int> ready_calls{0};
    std::atomic<int> error_calls{0};

    {
        deterministic_async_client client(control);
        client.set_on_ready_action([&] { ++ready_calls; });
        client.set_error_handler([&](int code, std::string const&) {
            if (code != 0) {
                ++error_calls;
            }
        });
        ASSERT_TRUE(pump_until(client, 500ms, [&] { return control->attempt_count() >= 1; }));
    }

    // Destroyed client must not be re-entered when the worker finishes.
    control->release(0);
    ASSERT_TRUE(control->wait_for_attempt_completed(0, 200ms));
    ASSERT_TRUE(control->wait_for_attempt_policy_destroyed(0, 200ms));
    EXPECT_EQ(ready_calls.load(), 0);
    EXPECT_EQ(error_calls.load(), 0);
}

// Ensure a successful async connect result that belongs to an older generation is
// not observed after reset, while current-generation failure still reports error.
TEST(fd_event_client_async_test, stale_connected_notification_is_not_observed) {
    auto control = std::make_shared<async_connect_control>(std::vector<connect_attempt_plan>{{true, 0}, {false, 42}});
    deterministic_async_client client(control);

    std::atomic<int> ready_calls{0};
    std::atomic<int> error_calls{0};
    client.set_on_ready_action([&] { ++ready_calls; });
    client.set_error_handler([&](int code, std::string const&) {
        if (code != 0) {
            ++error_calls;
        }
    });

    ASSERT_TRUE(pump_until(client, 500ms, [&] { return control->attempt_count() >= 1; }));
    control->release(0); // Current-generation attempt succeeds while still generation 1.
    ASSERT_TRUE(control->wait_for_attempt_completed(0, 200ms));
    ASSERT_TRUE(run_sync_and_expect_ready(client, control, 5ms, 100ms, 200ms, "client.sync() for first async result"));
    ASSERT_TRUE(pump_until_before_sync(client, 300ms,
                                       [&] { return client.get_raw_handler() != nullptr && ready_calls.load() == 0; }));

    client.reset(); // Advance generation after accepting attempt 0.
    ASSERT_TRUE(run_sync_and_expect_ready(client, control, 5ms, 100ms, 200ms,
                                          "client.sync() after reset with stale connected notification"));

    EXPECT_EQ(ready_calls.load(), 0);
    EXPECT_EQ(error_calls.load(), 0);
    EXPECT_TRUE(client.on_error());

    ASSERT_TRUE(pump_until(client, 500ms, [&] { return control->attempt_count() >= 2; }));
    control->release(1); // Current-generation failure after reset.
    EXPECT_TRUE(pump_until(client, 500ms, [&] { return error_calls.load() == 1; }));
    EXPECT_TRUE(client.on_error());
    EXPECT_EQ(ready_calls.load(), 0);

    ASSERT_TRUE(control->wait_for_attempt_completed(1, 300ms));
}
