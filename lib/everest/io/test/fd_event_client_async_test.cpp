// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include <everest/io/event/event_fd.hpp>
#include <everest/io/event/fd_event_client.hpp>
#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/tcp/tcp_client.hpp>
#include <everest/io/udp/udp_client.hpp>
#include <everest/io/utilities/event_client_async_policy.hpp>
#include <everest/io/utilities/generic_error_state.hpp>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <future>

#include <gtest/gtest.h>

using namespace std::chrono_literals;
using namespace everest::lib::io;

using everest::lib::io::event::event_fd;
using everest::lib::io::event::fd_event_client;
using everest::lib::io::event::poll_events;
using everest::lib::io::event::semaphore_fd;
using everest::lib::io::utilities::event_client_async_policy_v;
using everest::lib::io::utilities::event_client_handshake_policy_v;
using everest::lib::io::utilities::has_member_get_error_string_v;
using everest::lib::io::utilities::has_member_handshake_timeout_v;

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

// Stands for "written by a policy that has no connect attempt of its own".
constexpr std::size_t no_attempt{std::numeric_limits<std::size_t>::max()};

// Records what a policy actually handed to the wire, shared across the policy instances a
// client creates on every reset.
struct tx_record {
    std::vector<int> values;
    // The connect attempt the policy instance that wrote each value belongs to, so a payload can
    // be attributed to a peer rather than only to the wire.
    std::vector<std::size_t> attempts;

    void add(int value, std::size_t attempt) {
        values.push_back(value);
        attempts.push_back(attempt);
    }

    std::vector<std::size_t> attempts_of(int value) const {
        std::vector<std::size_t> result;
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (values[i] == value) {
                result.push_back(attempts[i]);
            }
        }
        return result;
    }
};

// A payload source the test makes readable on demand. The descriptor lives here rather than in
// the policy, so it survives the policy instances a client replaces on every reset. A semaphore
// descriptor keeps the counter equal to pending, so a read can never consume more than one push
// and leave a later read blocking on an empty descriptor.
struct rx_source {
    semaphore_fd ready;
    std::atomic<int> pending{0};
    int value{0};

    void push(int item) {
        value = item;
        ++pending;
        ready.notify();
    }

    bool take(int& out) {
        if (pending.load() <= 0) {
            return false;
        }
        --pending;
        ready.read();
        out = value;
        return true;
    }
};

class deterministic_async_policy {
public:
    using PayloadT = int;

    // Stands in for a payload that survives a replay, so it asks for the pre-connect buffer.
    static constexpr bool buffer_tx_before_connect{true};

    deterministic_async_policy() = default;
    explicit deterministic_async_policy(std::shared_ptr<async_connect_control> control) :
        m_control(std::move(control)) {
    }
    ~deterministic_async_policy() {
        if (m_control and m_attempt != no_attempt) {
            m_control->mark_policy_destroyed(m_attempt);
        }
    }

    // The source is optional. Without one the policy keeps its own descriptor, which never becomes
    // readable, so a client built without a source behaves exactly as before.
    bool setup(std::shared_ptr<async_connect_control> control, std::shared_ptr<tx_record> record = nullptr,
               std::shared_ptr<rx_source> source = nullptr) {
        m_control = std::move(control);
        m_tx_record = std::move(record);
        m_source = std::move(source);
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
        cb(ok, ok ? get_fd() : -1);
        m_control->mark_completed(attempt);
    }

    bool tx(PayloadT const& payload) {
        if (not m_tx_record) {
            return false;
        }
        m_tx_record->add(payload, m_attempt);
        return true;
    }

    bool rx(PayloadT& data) {
        return m_source and m_source->take(data);
    }

    int get_fd() const {
        return m_source ? m_source->ready.get_raw_fd() : m_ready_event.get_raw_fd();
    }

    int get_error() const {
        return m_last_error;
    }

    // The connect attempt this instance belongs to. A client replaces the instance on every reset,
    // so this is the identity of the peer a read was served from.
    std::size_t attempt() const {
        return m_attempt;
    }

private:
    std::shared_ptr<async_connect_control> m_control;
    std::shared_ptr<tx_record> m_tx_record;
    std::shared_ptr<rx_source> m_source;
    event_fd m_ready_event;
    int m_last_error{0};
    std::size_t m_attempt{no_attempt};
};

static_assert(event_client_async_policy_v<deterministic_async_policy>);

// Stands in for the seven aliases that resolve open() synchronously and must keep rejecting
// until they are up.
class deterministic_sync_policy {
public:
    using PayloadT = int;

    deterministic_sync_policy() = default;

    bool open(std::shared_ptr<tx_record> record) {
        m_tx_record = std::move(record);
        return static_cast<bool>(m_tx_record);
    }

    bool tx(PayloadT const& payload) {
        if (not m_tx_record) {
            return false;
        }
        // A synchronous policy has no connect attempt to attribute the payload to.
        m_tx_record->add(payload, no_attempt);
        return true;
    }

    bool rx(PayloadT&) {
        return false;
    }

    int get_fd() const {
        return m_ready_event.get_raw_fd();
    }

    int get_error() const {
        return 0;
    }

private:
    std::shared_ptr<tx_record> m_tx_record;
    event_fd m_ready_event;
};

static_assert(not event_client_async_policy_v<deterministic_sync_policy>);

// Synchronous open() plus a working rx, so a reset can be queued in the same poll cycle as a
// successful receive.
class rx_capable_sync_policy {
public:
    using PayloadT = int;

    rx_capable_sync_policy() = default;

    bool open(std::shared_ptr<tx_record> record, std::shared_ptr<rx_source> source) {
        m_tx_record = std::move(record);
        m_source = std::move(source);
        return static_cast<bool>(m_tx_record) and static_cast<bool>(m_source);
    }

    bool tx(PayloadT const& payload) {
        if (not m_tx_record) {
            return false;
        }
        // A synchronous policy has no connect attempt to attribute the payload to.
        m_tx_record->add(payload, no_attempt);
        return true;
    }

    bool rx(PayloadT& data) {
        return m_source and m_source->take(data);
    }

    int get_fd() const {
        return m_source ? m_source->ready.get_raw_fd() : -1;
    }

    int get_error() const {
        return 0;
    }

private:
    std::shared_ptr<tx_record> m_tx_record;
    std::shared_ptr<rx_source> m_source;
};

static_assert(not event_client_async_policy_v<rx_capable_sync_policy>);

// Steers a read that fails on a readable descriptor and the errno the policy then reports for it.
struct rx_fault_plan {
    bool rx_fails{false};
    int error_code{0};
};

// Synchronous open() whose read can be made to fail while the descriptor is readable, with an
// errno of the test's choosing. rx_capable_sync_policy cannot express this: its rx only fails on
// an empty source and its get_error is hardwired to 0, so nothing there reaches the route that
// turns a dead socket into a reported error.
class faulting_rx_sync_policy {
public:
    using PayloadT = int;

    faulting_rx_sync_policy() = default;

    bool open(std::shared_ptr<rx_source> source, std::shared_ptr<rx_fault_plan> fault) {
        m_source = std::move(source);
        m_fault = std::move(fault);
        return static_cast<bool>(m_source) and static_cast<bool>(m_fault);
    }

    bool tx(PayloadT const&) {
        return true;
    }

    bool rx(PayloadT& data) {
        int value{0};
        if (not m_source or not m_source->take(value)) {
            return false;
        }
        // The readiness is taken either way. A socket read that fails has consumed its event too,
        // so leaving the descriptor readable would spin the poll loop instead.
        if (m_fault and m_fault->rx_fails) {
            return false;
        }
        data = value;
        return true;
    }

    int get_fd() const {
        return m_source ? m_source->ready.get_raw_fd() : -1;
    }

    int get_error() const {
        return m_fault ? m_fault->error_code : 0;
    }

private:
    std::shared_ptr<rx_source> m_source;
    std::shared_ptr<rx_fault_plan> m_fault;
};

static_assert(not event_client_async_policy_v<faulting_rx_sync_policy>);

// A loopback listener whose accept queue is full. Further connects stay unfinished instead of
// being refused, and nothing is ever accepted.
class unreachable_peer {
public:
    unreachable_peer() = default;
    unreachable_peer(unreachable_peer const&) = delete;
    unreachable_peer& operator=(unreachable_peer const&) = delete;

    ~unreachable_peer() {
        for (auto fd : m_pending) {
            ::close(fd);
        }
        if (m_listen_fd >= 0) {
            ::close(m_listen_fd);
        }
    }

    bool start() {
        m_listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (m_listen_fd < 0) {
            return false;
        }
        auto addr = loopback_address(0);
        if (::bind(m_listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            return false;
        }
        if (::listen(m_listen_fd, 1) != 0) {
            return false;
        }
        socklen_t len = sizeof(addr);
        if (::getsockname(m_listen_fd, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
            return false;
        }
        m_port = ::ntohs(addr.sin_port);

        for (std::size_t i = 0; i < backlog_fill; ++i) {
            auto fd = connect_without_waiting();
            if (fd < 0) {
                return false;
            }
            m_pending.push_back(fd);
        }
        return is_saturated();
    }

    std::uint16_t port() const {
        return m_port;
    }

private:
    static constexpr std::size_t backlog_fill{32};
    static constexpr int saturation_probe_ms{200};

    static sockaddr_in loopback_address(std::uint16_t port) {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
        addr.sin_port = ::htons(port);
        return addr;
    }

    int connect_without_waiting() const {
        auto fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
        if (fd < 0) {
            return -1;
        }
        auto addr = loopback_address(m_port);
        if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 and errno != EINPROGRESS) {
            ::close(fd);
            return -1;
        }
        return fd;
    }

    // A saturated queue leaves a fresh connect unfinished for the whole probe window.
    bool is_saturated() {
        auto fd = connect_without_waiting();
        if (fd < 0) {
            return false;
        }
        m_pending.push_back(fd);
        pollfd item{fd, POLLOUT, 0};
        return ::poll(&item, 1, saturation_probe_ms) == 0;
    }

    int m_listen_fd{-1};
    std::uint16_t m_port{0};
    std::vector<int> m_pending;
};

class error_state_probe : public utilities::generic_error_state {
public:
    using generic_error_state::current_connection_state;
    using generic_error_state::on_error;
    using generic_error_state::set_error_status;
};

char const* state_name(utilities::connection_state state) {
    switch (state) {
    case utilities::connection_state::fresh:
        return "fresh";
    case utilities::connection_state::connected:
        return "connected";
    case utilities::connection_state::failed:
        return "failed";
    }
    return "unknown";
}

static_assert(not event_client_handshake_policy_v<deterministic_async_policy>);
static_assert(not has_member_get_error_string_v<deterministic_async_policy>);

enum class handshake_step_result {
    want_read,
    want_write,
    want_invalid,
    complete,
    fail
};

class handshake_script {
public:
    explicit handshake_script(std::vector<handshake_step_result> steps, int fail_error_code = 0,
                              std::chrono::milliseconds handshake_timeout = 5s) :
        m_steps{std::move(steps)}, m_fail_error_code{fail_error_code}, m_handshake_timeout{handshake_timeout} {
        int fds[2]{-1, -1};
        if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, fds) == 0) {
            m_local_fd = fds[0];
            m_peer_fd = fds[1];
        }
    }

    handshake_script(handshake_script const&) = delete;
    handshake_script& operator=(handshake_script const&) = delete;
    handshake_script(handshake_script&&) = delete;
    handshake_script& operator=(handshake_script&&) = delete;

    ~handshake_script() {
        if (m_local_fd >= 0) {
            ::close(m_local_fd);
        }
        if (m_peer_fd >= 0) {
            ::close(m_peer_fd);
        }
    }

    int local_fd() const {
        return m_local_fd;
    }

    int fail_error_code() const {
        return m_fail_error_code;
    }

    std::chrono::milliseconds handshake_timeout() const {
        return m_handshake_timeout;
    }

    void deliver_peer_record() const {
        deliver_byte(1);
    }

    void deliver_byte(char value) const {
        (void)::send(m_peer_fd, &value, 1, MSG_DONTWAIT);
    }

    void close_peer() {
        if (m_peer_fd >= 0) {
            ::close(m_peer_fd);
            m_peer_fd = -1;
        }
    }

    // Without this a want_read state keeps refiring on the record the step already reacted to.
    void consume_pending_record() const {
        char byte{0};
        (void)::recv(m_local_fd, &byte, 1, MSG_DONTWAIT);
    }

    handshake_step_result advance() {
        ++m_step_count;
        auto const result = m_steps[m_cursor];
        if (m_cursor + 1 < m_steps.size()) {
            ++m_cursor;
        }
        return result;
    }

    std::size_t step_count() const {
        return m_step_count;
    }

    void register_connect() {
        ++m_connect_count;
    }

    std::size_t connect_count() const {
        return m_connect_count.load();
    }

    void record_tx(int payload) {
        m_tx_payloads.push_back(payload);
    }

    std::vector<int> const& tx_payloads() const {
        return m_tx_payloads;
    }

private:
    std::vector<handshake_step_result> m_steps;
    int m_fail_error_code{0};
    std::chrono::milliseconds m_handshake_timeout{5s};
    std::size_t m_cursor{0};
    std::size_t m_step_count{0};
    std::atomic<std::size_t> m_connect_count{0};
    std::vector<int> m_tx_payloads;
    int m_local_fd{-1};
    int m_peer_fd{-1};
};

class scripted_handshake_policy {
public:
    using PayloadT = int;

    scripted_handshake_policy() = default;

    bool setup(std::shared_ptr<handshake_script> script) {
        m_script = std::move(script);
        return static_cast<bool>(m_script) and m_script->local_fd() >= 0;
    }

    void connect(std::function<void(bool, int)> const& cb) {
        if (not m_script) {
            cb(false, -1);
            return;
        }
        m_script->register_connect();
        cb(true, m_script->local_fd());
    }

    bool handshake_complete() const {
        return m_complete;
    }

    bool handshake_step() {
        if (not m_script) {
            return false;
        }
        m_script->consume_pending_record();
        switch (m_script->advance()) {
        case handshake_step_result::want_read:
            m_desired = poll_events::read;
            return true;
        case handshake_step_result::want_write:
            m_desired = poll_events::write;
            return true;
        case handshake_step_result::want_invalid:
            m_desired = poll_events::priority;
            return true;
        case handshake_step_result::complete:
            m_complete = true;
            // Poisoned: a completed handshake is monitored for reading, never from this request.
            m_desired = poll_events::write;
            return true;
        case handshake_step_result::fail:
        default:
            m_error = m_script->fail_error_code();
            m_error_string = "scripted handshake failure";
            return false;
        }
    }

    poll_events desired_events() const {
        return m_desired;
    }

    std::chrono::milliseconds handshake_timeout() const {
        return m_script ? m_script->handshake_timeout() : std::chrono::milliseconds{5s};
    }

    bool tx(PayloadT const& payload) {
        if (not m_script) {
            return false;
        }
        m_script->record_tx(payload);
        return true;
    }

    bool rx(PayloadT& data) {
        if (not m_script) {
            return false;
        }
        char byte{0};
        if (::recv(m_script->local_fd(), &byte, 1, MSG_DONTWAIT) != 1) {
            return false;
        }
        data = static_cast<int>(byte);
        return true;
    }

    int get_fd() const {
        return m_script ? m_script->local_fd() : -1;
    }

    int get_error() const {
        return m_error;
    }

    std::string const& get_error_string() const {
        return m_error_string;
    }

private:
    std::shared_ptr<handshake_script> m_script;
    poll_events m_desired{poll_events::read};
    bool m_complete{false};
    int m_error{0};
    std::string m_error_string;
};

static_assert(event_client_async_policy_v<scripted_handshake_policy>);
static_assert(event_client_handshake_policy_v<scripted_handshake_policy>);
static_assert(has_member_get_error_string_v<scripted_handshake_policy>);
// The true side of the trait the handshake_timeout() static_assert keys on. Its false side is
// what that assert lets through, which no compiling translation unit can pin.
static_assert(has_member_handshake_timeout_v<scripted_handshake_policy>);

class poll_error_script {
public:
    explicit poll_error_script(int fd) : m_fd{fd} {
    }

    poll_error_script(poll_error_script const&) = delete;
    poll_error_script& operator=(poll_error_script const&) = delete;

    ~poll_error_script() {
        if (m_fd >= 0) {
            ::close(m_fd);
        }
    }

    int fd() const {
        return m_fd;
    }

    void set_policy_error(int code) {
        m_policy_error.store(code);
    }

    int policy_error() const {
        return m_policy_error.load();
    }

private:
    int m_fd{-1};
    std::atomic<int> m_policy_error{0};
};

class poll_error_policy {
public:
    using PayloadT = int;

    poll_error_policy() = default;

    bool setup(std::shared_ptr<poll_error_script> script) {
        m_script = std::move(script);
        return static_cast<bool>(m_script) and m_script->fd() >= 0;
    }

    void connect(std::function<void(bool, int)> const& cb) {
        if (not m_script) {
            cb(false, -1);
            return;
        }
        cb(true, m_script->fd());
    }

    bool tx(PayloadT const&) {
        return false;
    }

    bool rx(PayloadT&) {
        return false;
    }

    int get_fd() const {
        return m_script ? m_script->fd() : -1;
    }

    int get_error() const {
        return m_script ? m_script->policy_error() : 0;
    }

private:
    std::shared_ptr<poll_error_script> m_script;
};

static_assert(event_client_async_policy_v<poll_error_policy>);
static_assert(not event_client_handshake_policy_v<poll_error_policy>);

// The port is bound and released so nothing listens on it. poll() does not consume SO_ERROR, so
// ECONNREFUSED is left unread for the client to resolve.
int make_refused_tcp_fd() {
    int probe = ::socket(AF_INET, SOCK_STREAM, 0);
    if (probe < 0) {
        return -1;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ::inet_addr("127.0.0.1");
    addr.sin_port = 0;
    socklen_t addr_len = sizeof(addr);
    if (::bind(probe, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 or
        ::getsockname(probe, reinterpret_cast<sockaddr*>(&addr), &addr_len) != 0) {
        ::close(probe);
        return -1;
    }
    ::close(probe);

    int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (fd < 0) {
        return -1;
    }
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
        // Something claimed the released port in the meantime.
        ::close(fd);
        return -1;
    }
    pollfd pfd{fd, POLLOUT, 0};
    if (::poll(&pfd, 1, 2000) != 1 or (pfd.revents & POLLERR) == 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

int make_hungup_pipe_read_fd() {
    int fds[2]{-1, -1};
    if (::pipe2(fds, O_NONBLOCK) != 0) {
        return -1;
    }
    ::close(fds[1]);
    return fds[0];
}

int make_errored_pipe_write_fd() {
    int fds[2]{-1, -1};
    if (::pipe2(fds, O_NONBLOCK) != 0) {
        return -1;
    }
    ::close(fds[0]);
    return fds[1];
}

} // namespace

using deterministic_async_client = fd_event_client<deterministic_async_policy>::type;
using deterministic_sync_client = fd_event_client<deterministic_sync_policy>::type;
using rx_capable_sync_client = fd_event_client<rx_capable_sync_policy>::type;
using faulting_rx_sync_client = fd_event_client<faulting_rx_sync_policy>::type;
using scripted_handshake_client = fd_event_client<scripted_handshake_policy>::type;
using poll_error_client = fd_event_client<poll_error_policy>::type;

namespace {

std::string describe_codes(std::vector<int> const& codes) {
    std::string result;
    for (auto code : codes) {
        if (not result.empty()) {
            result += ", ";
        }
        result += std::to_string(code) + " (" + std::strerror(code) + ")";
    }
    return result.empty() ? std::string{"none"} : result;
}

// The handler runs on the loop thread, which is the thread pumping in the tests below.
template <class Client> void collect_error_codes(Client& client, std::vector<int>& codes) {
    client.set_error_handler([&codes](int code, std::string const&) {
        if (code != 0) {
            codes.push_back(code);
        }
    });
}

} // namespace

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

// A client that has never connected must be distinguishable from one whose connection
// failed. on_error() keeps covering both as "not up".
TEST(connection_state_test, a_never_connected_state_is_fresh) {
    error_state_probe state;

    EXPECT_TRUE(state.on_error());
    EXPECT_EQ(state.current_connection_state(), utilities::connection_state::fresh)
        << "a state that never saw a connection attempt reports " << state_name(state.current_connection_state());
}

TEST(connection_state_test, a_clean_status_reports_connected) {
    error_state_probe state;

    state.set_error_status(0);

    EXPECT_FALSE(state.on_error());
    EXPECT_EQ(state.current_connection_state(), utilities::connection_state::connected)
        << "a state with no pending error reports " << state_name(state.current_connection_state());
}

TEST(connection_state_test, an_error_status_reports_failed) {
    error_state_probe state;

    state.set_error_status(ECONNREFUSED);

    EXPECT_TRUE(state.on_error());
    EXPECT_EQ(state.current_connection_state(), utilities::connection_state::failed)
        << "a state with a pending error reports " << state_name(state.current_connection_state());
}

// The live case behind the tri-state: the writer is ready while the async connect is still
// in flight, so an early payload has to survive instead of being dropped without a
// diagnostic.
TEST(fd_event_client_tx_test, tx_while_the_connect_is_pending_is_buffered_and_delivered) {
    auto control = std::make_shared<async_connect_control>(std::vector<connect_attempt_plan>{{true, 0}});
    auto record = std::make_shared<tx_record>();
    deterministic_async_client client(control, record);

    ASSERT_TRUE(pump_until(client, 500ms, [&] { return control->attempt_count() >= 1; }));
    ASSERT_TRUE(client.on_error()) << "the connect already resolved, so there is no pending window to test";

    EXPECT_TRUE(client.tx(7)) << "a payload sent while the async connect was pending was rejected";
    EXPECT_TRUE(record->values.empty()) << "a payload was written before the client was up";

    control->release(0);
    ASSERT_TRUE(control->wait_for_attempt_completed(0, 500ms));
    ASSERT_TRUE(pump_until(client, 500ms, [&] { return not client.on_error(); }));
    EXPECT_TRUE(pump_until(client, 500ms, [&] { return record->values == std::vector<int>{7}; }))
        << "the buffered payload was not delivered after the connect completed, " << record->values.size()
        << " payloads reached the wire";
}

// The window opens at construction, before the queued connect action has run at all.
TEST(fd_event_client_tx_test, tx_before_the_first_sync_is_not_silently_dropped) {
    auto control = std::make_shared<async_connect_control>(std::vector<connect_attempt_plan>{{true, 0}});
    auto record = std::make_shared<tx_record>();
    deterministic_async_client client(control, record);

    EXPECT_TRUE(client.tx(11)) << "a payload sent before the first sync was rejected";

    ASSERT_TRUE(pump_until(client, 500ms, [&] { return control->attempt_count() >= 1; }));
    control->release(0);
    ASSERT_TRUE(control->wait_for_attempt_completed(0, 500ms));
    ASSERT_TRUE(pump_until(client, 500ms, [&] { return not client.on_error(); }));
    EXPECT_TRUE(pump_until(client, 500ms, [&] { return record->values == std::vector<int>{11}; }))
        << "tx() reported success and the payload never reached the wire, " << record->values.size()
        << " payloads delivered";
}

// A failed connect keeps rejecting: there is no peer to buffer for.
TEST(fd_event_client_tx_test, tx_on_a_failed_client_is_rejected) {
    auto control = std::make_shared<async_connect_control>(std::vector<connect_attempt_plan>{{false, ECONNREFUSED}});
    auto record = std::make_shared<tx_record>();
    deterministic_async_client client(control, record);

    std::atomic<int> error_calls{0};
    client.set_error_handler([&](int code, std::string const&) {
        if (code != 0) {
            ++error_calls;
        }
    });

    ASSERT_TRUE(pump_until(client, 500ms, [&] { return control->attempt_count() >= 1; }));
    control->release(0);
    ASSERT_TRUE(control->wait_for_attempt_completed(0, 500ms));
    ASSERT_TRUE(pump_until(client, 500ms, [&] { return error_calls.load() == 1; }));
    ASSERT_TRUE(client.on_error());

    EXPECT_FALSE(client.tx(13)) << "a payload was accepted after the connect failed";
    EXPECT_TRUE(record->values.empty());

    control->release_all();
}

// A reset returns the client to fresh, so the buffering window opens on every reconnect and not
// only on the first attempt. Retrying against a peer that is not up yet is the normal startup
// path, so a client stuck in failed drops every payload for the rest of its life.
TEST(fd_event_client_tx_test, a_reset_after_a_failure_buffers_again) {
    auto control =
        std::make_shared<async_connect_control>(std::vector<connect_attempt_plan>{{false, ECONNREFUSED}, {true, 0}});
    auto record = std::make_shared<tx_record>();
    deterministic_async_client client(control, record);

    std::atomic<int> error_calls{0};
    client.set_error_handler([&](int code, std::string const&) {
        if (code != 0) {
            ++error_calls;
        }
    });

    ASSERT_TRUE(pump_until(client, 500ms, [&] { return control->attempt_count() >= 1; }));
    control->release(0);
    ASSERT_TRUE(control->wait_for_attempt_completed(0, 500ms));
    ASSERT_TRUE(pump_until(client, 500ms, [&] { return error_calls.load() == 1; }));
    ASSERT_FALSE(client.tx(23)) << "the failed client accepted a payload, so the reset below proves nothing";

    // Attempt 1 stays pending, so the payload below is written inside the reconnect window.
    client.reset();
    ASSERT_TRUE(pump_until(client, 500ms, [&] { return control->attempt_count() >= 2; }));
    EXPECT_TRUE(client.tx(29)) << "a payload was rejected while the reconnect was still pending";
    EXPECT_TRUE(record->values.empty()) << "a payload was written before the reconnect completed";

    control->release(1);
    ASSERT_TRUE(control->wait_for_attempt_completed(1, 500ms));
    ASSERT_TRUE(pump_until(client, 500ms, [&] { return not client.on_error(); }));
    EXPECT_TRUE(pump_until(client, 500ms, [&] { return record->values == std::vector<int>{29}; }))
        << "the buffered payload never reached the reconnected peer, " << record->values.size() << " delivered";
}

// A reset opens another peer, so a payload written for the previous one is stale and may not be
// replayed onto the new connection.
TEST(fd_event_client_tx_test, a_reset_discards_what_was_buffered_for_the_previous_peer) {
    auto control = std::make_shared<async_connect_control>(std::vector<connect_attempt_plan>{{true, 0}, {true, 0}});
    auto record = std::make_shared<tx_record>();
    deterministic_async_client client(control, record);

    ASSERT_TRUE(pump_until(client, 500ms, [&] { return control->attempt_count() >= 1; }));
    ASSERT_TRUE(client.tx(31)) << "the payload under test was not buffered, so the reset below proves nothing";

    client.reset();
    ASSERT_TRUE(pump_until(client, 500ms, [&] { return control->attempt_count() >= 2; }));
    control->release_all();
    ASSERT_TRUE(control->wait_for_attempt_completed(1, 500ms));
    ASSERT_TRUE(pump_until(client, 500ms, [&] { return not client.on_error(); }));

    // A payload the reconnected client does write proves the wire is live, so a missing 31 is a
    // discard and not a client that never wrote anything at all.
    ASSERT_TRUE(client.tx(37));
    ASSERT_TRUE(pump_until(client, 500ms, [&] { return not record->values.empty(); }))
        << "the reconnected client never wrote anything";
    pump_for(client, 100ms);
    EXPECT_EQ(record->values, std::vector<int>{37}) << "a payload buffered for the previous peer was replayed";
}

// The reconnect window has to open when reset() returns, not when its queued action runs. A
// payload written in between belongs to the peer the reset opens, so it may be neither discarded
// with the old connection nor written to it.
TEST(fd_event_client_tx_test, a_reset_from_a_connected_client_buffers_for_the_new_peer) {
    auto control = std::make_shared<async_connect_control>(std::vector<connect_attempt_plan>{{true, 0}, {true, 0}});
    auto record = std::make_shared<tx_record>();
    deterministic_async_client client(control, record);

    ASSERT_TRUE(pump_until(client, 500ms, [&] { return control->attempt_count() >= 1; }));
    control->release(0);
    ASSERT_TRUE(control->wait_for_attempt_completed(0, 500ms));
    ASSERT_TRUE(pump_until(client, 500ms, [&] { return not client.on_error(); })) << "the client never came up";

    client.reset();
    EXPECT_TRUE(client.tx(41)) << "a payload written right after reset() was rejected";

    ASSERT_TRUE(pump_until(client, 500ms, [&] { return control->attempt_count() >= 2; }));
    control->release(1);
    ASSERT_TRUE(control->wait_for_attempt_completed(1, 500ms));
    ASSERT_TRUE(pump_until(client, 500ms, [&] { return not client.on_error(); }));

    EXPECT_TRUE(pump_until(client, 500ms, [&] { return record->values == std::vector<int>{41}; }))
        << "the payload written after reset() never reached the wire, " << record->values.size() << " delivered";
    EXPECT_EQ(record->attempts_of(41), std::vector<std::size_t>{1})
        << "the payload was written to the peer the reset replaced";
}

// The old handle is still live and still registered for writing when reset() returns, so the
// flush path has to stay shut until the new connection is up. Otherwise the very payload the
// reconnect window accepted is written to the peer it was buffered away from.
TEST(fd_event_client_tx_test, a_reset_does_not_flush_the_new_payload_to_the_old_peer) {
    auto control = std::make_shared<async_connect_control>(std::vector<connect_attempt_plan>{{true, 0}, {true, 0}});
    auto record = std::make_shared<tx_record>();
    deterministic_async_client client(control, record);

    ASSERT_TRUE(pump_until(client, 500ms, [&] { return control->attempt_count() >= 1; }));
    control->release(0);
    ASSERT_TRUE(control->wait_for_attempt_completed(0, 500ms));
    ASSERT_TRUE(pump_until(client, 500ms, [&] { return not client.on_error(); })) << "the client never came up";

    // One payload out and one still queued leaves the old descriptor registered for writing, so
    // the next poll pass reaches the flush path before any queued action runs.
    ASSERT_TRUE(client.tx(51));
    ASSERT_TRUE(client.tx(53));
    ASSERT_TRUE(pump_until(client, 500ms, [&] { return record->values.size() == 1; }));
    ASSERT_EQ(record->values, std::vector<int>{51});

    client.reset();
    ASSERT_TRUE(client.tx(59));
    client.sync(1ms); // The pass where the old descriptor is still writable.

    ASSERT_TRUE(pump_until(client, 500ms, [&] { return control->attempt_count() >= 2; }));
    control->release(1);
    ASSERT_TRUE(control->wait_for_attempt_completed(1, 500ms));
    ASSERT_TRUE(pump_until(client, 500ms, [&] { return not client.on_error(); }));

    EXPECT_TRUE(pump_until(client, 500ms, [&] { return record->attempts_of(59) == std::vector<std::size_t>{1}; }))
        << "the payload written after reset() did not reach the new peer exactly once";
    pump_for(client, 100ms);
    EXPECT_EQ(record->values, (std::vector<int>{51, 59}))
        << "a payload buffered for the previous peer was written after the reset";
}

// The read and the write branch of one descriptor are dispatched in the same pass. A read that
// succeeds reports the connection as up, which happens after the rx callback has already retired
// it, so the flush path may not take that report for the identity of the handle it is about to
// write to.
TEST(fd_event_client_tx_test, a_reset_from_the_rx_callback_does_not_flush_to_the_old_peer) {
    auto control = std::make_shared<async_connect_control>(std::vector<connect_attempt_plan>{{true, 0}, {true, 0}});
    auto record = std::make_shared<tx_record>();
    auto source = std::make_shared<rx_source>();
    deterministic_async_client client(control, record, source);

    std::atomic<int> rx_calls{0};
    std::atomic<bool> tx_accepted{false};
    client.set_rx_handler([&](int const&, auto&) {
        if (++rx_calls == 1) {
            client.reset();
            tx_accepted.store(client.tx(73));
        }
    });

    ASSERT_TRUE(pump_until(client, 500ms, [&] { return control->attempt_count() >= 1; }));
    control->release(0);
    ASSERT_TRUE(control->wait_for_attempt_completed(0, 500ms));
    ASSERT_TRUE(pump_until(client, 500ms, [&] { return not client.on_error(); })) << "the client never came up";

    // Nothing is readable yet, so this pass only arms write interest on the live descriptor. The
    // payload stays queued and the next pass reports the descriptor readable and writable at once.
    ASSERT_TRUE(client.tx(71));
    client.sync(1ms);
    ASSERT_TRUE(record->values.empty()) << "the queued payload drained before the read pass";

    source->push(5);
    client.sync(1ms); // The pass that reports read and write together.

    ASSERT_EQ(rx_calls.load(), 1) << "the read under test never reached the callback";
    ASSERT_TRUE(tx_accepted.load()) << "the payload written from the rx callback was rejected";
    EXPECT_TRUE(record->attempts_of(73).empty())
        << "the payload buffered by the rx callback was written to the peer the reset retired";

    ASSERT_TRUE(pump_until(client, 500ms, [&] { return control->attempt_count() >= 2; }));
    control->release(1);
    ASSERT_TRUE(control->wait_for_attempt_completed(1, 500ms));
    ASSERT_TRUE(pump_until(client, 500ms, [&] { return not client.on_error(); }));

    EXPECT_TRUE(pump_until(client, 500ms, [&] { return record->attempts_of(73) == std::vector<std::size_t>{1}; }))
        << "the payload did not reach the peer the reset opened exactly once";
    pump_for(client, 100ms);
    EXPECT_EQ(record->values, std::vector<int>{73}) << "a payload buffered for the retired peer reached the wire";
}

// Resetting from inside the error callback and sending straight away is the natural consumer
// shape for reconnect handling, so the buffering window has to be open by the time the callback
// returns from reset().
TEST(fd_event_client_tx_test, a_reset_inside_the_error_callback_accepts_tx_at_once) {
    auto control =
        std::make_shared<async_connect_control>(std::vector<connect_attempt_plan>{{false, ECONNREFUSED}, {true, 0}});
    auto record = std::make_shared<tx_record>();
    deterministic_async_client client(control, record);

    std::atomic<int> error_calls{0};
    std::atomic<bool> tx_accepted{false};
    client.set_error_handler([&](int code, std::string const&) {
        if (code == 0) {
            return;
        }
        if (++error_calls == 1) {
            client.reset();
            tx_accepted.store(client.tx(61));
        }
    });

    ASSERT_TRUE(pump_until(client, 500ms, [&] { return control->attempt_count() >= 1; }));
    control->release(0);
    ASSERT_TRUE(control->wait_for_attempt_completed(0, 500ms));
    ASSERT_TRUE(pump_until(client, 500ms, [&] { return error_calls.load() == 1; })) << "the connect never failed";

    EXPECT_TRUE(tx_accepted.load()) << "a payload written right after reset() inside the error callback was rejected";

    ASSERT_TRUE(pump_until(client, 500ms, [&] { return control->attempt_count() >= 2; }));
    control->release(1);
    ASSERT_TRUE(control->wait_for_attempt_completed(1, 500ms));
    ASSERT_TRUE(pump_until(client, 500ms, [&] { return not client.on_error(); }));

    EXPECT_TRUE(pump_until(client, 500ms, [&] { return record->values == std::vector<int>{61}; }))
        << "the payload never reached the reconnected peer, " << record->values.size() << " delivered";
    EXPECT_EQ(record->attempts_of(61), std::vector<std::size_t>{1});
}

// The error status handler and the socket are dispatched in the same poll pass, error status
// first. A reset issued from the error callback retires the handle before the read branch of that
// pass runs, so the inbound direction has to close with the outbound one. Otherwise the consumer
// is handed one more payload from the peer it just abandoned, which a control plane consumer can
// mistake for a response on the connection it is now waiting on.
TEST(fd_event_client_rx_test, a_reset_from_the_error_callback_drops_the_retired_peers_read) {
    // Attempt 0 connects and reports an errno at once, so the pass that registers the socket also
    // reports the failure. That is what puts the error status event ahead of the socket in the
    // next batch.
    auto control =
        std::make_shared<async_connect_control>(std::vector<connect_attempt_plan>{{true, ECONNRESET}, {true, 0}});
    auto record = std::make_shared<tx_record>();
    auto source = std::make_shared<rx_source>();
    deterministic_async_client client(control, record, source);

    // Readable before the descriptor joins the poll set, so it is ready the moment the connected
    // handler registers it, in the same pass that notifies the error status event.
    source->push(5);

    std::vector<std::size_t> rx_attempts;
    std::vector<std::size_t> retired_attempts;
    client.set_error_handler([&](int code, std::string const&) {
        if (code == 0 or not retired_attempts.empty()) {
            return;
        }
        auto const& handle = client.get_raw_handler();
        retired_attempts.push_back(handle ? handle->attempt() : no_attempt);
        client.reset();
    });
    client.set_rx_handler([&](int const&, auto&) {
        auto const& handle = client.get_raw_handler();
        rx_attempts.push_back(handle ? handle->attempt() : no_attempt);
    });

    ASSERT_TRUE(pump_until(client, 500ms, [&] { return control->attempt_count() >= 1; }));
    control->release(0);
    ASSERT_TRUE(control->wait_for_attempt_completed(0, 500ms));
    ASSERT_TRUE(pump_until(client, 500ms, [&] { return not retired_attempts.empty(); }))
        << "the failing connect never reached the error callback";
    ASSERT_EQ(retired_attempts, std::vector<std::size_t>{0});

    EXPECT_EQ(std::count(rx_attempts.begin(), rx_attempts.end(), std::size_t{0}), 0)
        << "the rx handler was handed a payload from the peer the error callback retired";

    // The skipped read must not wedge the client: it comes back up and serves reads from the
    // connection the reset opened. How many reads arrive is not asserted. This double keeps the
    // descriptor and the pending payload in rx_source, which outlives the policy instances, so the
    // payload pushed before the reset survives it here. A real socket is closed by the reset and
    // its unread payload goes with it, so that redelivery is an artifact of the double rather than
    // a guarantee of the client.
    ASSERT_TRUE(pump_until(client, 500ms, [&] { return control->attempt_count() >= 2; }));
    control->release(1);
    ASSERT_TRUE(control->wait_for_attempt_completed(1, 500ms));
    ASSERT_TRUE(pump_until(client, 500ms, [&] { return not client.on_error(); })) << "the client never came back up";

    source->push(6);
    ASSERT_TRUE(pump_until(client, 500ms, [&] { return not rx_attempts.empty(); }))
        << "no read reached the rx handler after the reset reopened the client";
    EXPECT_TRUE(std::all_of(rx_attempts.begin(), rx_attempts.end(), [](std::size_t a) { return a == 1; }))
        << "a read was attributed to a connection other than the one the reset opened";
}

// Buffering a peer that never comes up must not grow without limit. tx_attempts is the
// upper bound the cap has to stay below, not the cap itself.
TEST(fd_event_client_tx_test, the_tx_buffer_is_bounded_while_the_peer_never_connects) {
    constexpr std::size_t tx_attempts{100000};
    auto control = std::make_shared<async_connect_control>(std::vector<connect_attempt_plan>{{true, 0}});
    auto record = std::make_shared<tx_record>();
    deterministic_async_client client(control, record);

    // Attempt 0 is never released, so the peer stays unreachable for the whole test.
    ASSERT_TRUE(pump_until(client, 500ms, [&] { return control->attempt_count() >= 1; }));
    ASSERT_TRUE(client.on_error());

    std::size_t accepted{0};
    bool rejected{false};
    for (std::size_t i = 0; i < tx_attempts; ++i) {
        if (client.tx(static_cast<int>(i))) {
            ++accepted;
            continue;
        }
        rejected = true;
        break;
    }

    EXPECT_GT(accepted, 0u) << "no payload was buffered while the peer was unreachable";
    EXPECT_TRUE(rejected) << "the buffer accepted all " << tx_attempts << " payloads, so it is unbounded";
    EXPECT_TRUE(record->values.empty()) << "payloads reached the wire without a connected peer";
}

// A reset from the rx callback has to reopen the client just like one issued from outside a
// callback, so a payload written once that reopen is confirmed reaches the wire.
TEST(fd_event_client_tx_test, a_reset_from_the_rx_callback_reopens_the_client) {
    auto record = std::make_shared<tx_record>();
    auto source = std::make_shared<rx_source>();
    rx_capable_sync_client client(record, source);

    std::atomic<int> rx_calls{0};
    client.set_rx_handler([&](int const&, auto&) {
        if (++rx_calls == 1) {
            client.reset();
        }
    });
    ASSERT_TRUE(pump_until(client, 500ms, [&] { return not client.on_error(); })) << "the client never came up";

    source->push(3);
    ASSERT_TRUE(pump_until(client, 500ms, [&] { return rx_calls.load() == 1; })) << "the read under test never ran";

    EXPECT_TRUE(pump_until(client, 500ms, [&] {
        return client.get_raw_handler() != nullptr and not client.on_error();
    })) << "the client never came back up after the reset from the rx callback";
    EXPECT_TRUE(client.tx(83)) << "a payload was rejected after the reset from the rx callback";
    EXPECT_TRUE(pump_until(client, 500ms, [&] { return record->values == std::vector<int>{83}; }))
        << "the payload never reached the wire, " << record->values.size() << " payloads delivered";
}

// Buffering is scoped to async connect policies. A synchronous open() client keeps rejecting
// until it is up, because replaying a stale control plane payload is worse than dropping it.
TEST(fd_event_client_tx_test, a_synchronous_client_rejects_tx_before_it_is_up) {
    auto record = std::make_shared<tx_record>();
    deterministic_sync_client client(record);

    ASSERT_TRUE(client.on_error()) << "the client reports itself up before its first sync";
    EXPECT_FALSE(client.tx(17)) << "a synchronous open() client accepted a payload before it was up";

    ASSERT_TRUE(pump_until(client, 500ms, [&] { return not client.on_error(); }));
    EXPECT_TRUE(client.tx(19));
    EXPECT_TRUE(pump_until(client, 500ms, [&] { return record->values == std::vector<int>{19}; }))
        << "the payload sent while up was not delivered, " << record->values.size() << " payloads reached the wire";
}

// The reconnect window is an async policy feature. A synchronous client is fresh between reset()
// and the reopen its queued action performs, and fresh rejects, so the window is closed for it.
TEST(fd_event_client_tx_test, a_synchronous_client_rejects_tx_in_the_reset_window) {
    auto record = std::make_shared<tx_record>();
    deterministic_sync_client client(record);

    ASSERT_TRUE(pump_until(client, 500ms, [&] { return not client.on_error(); })) << "the client never came up";
    ASSERT_TRUE(client.tx(101));
    ASSERT_TRUE(pump_until(client, 500ms, [&] { return record->values == std::vector<int>{101}; }));

    client.reset();
    EXPECT_TRUE(client.on_error()) << "a synchronous client reports itself up inside the reset window";
    EXPECT_FALSE(client.tx(103)) << "a synchronous client accepted a payload inside the reset window";

    ASSERT_TRUE(pump_until(client, 500ms, [&] { return not client.on_error(); })) << "the client never came back up";
    EXPECT_TRUE(client.tx(107)) << "a payload was rejected after the reset completed";
    EXPECT_TRUE(pump_until(client, 500ms, [&] {
        return record->values == (std::vector<int>{101, 107});
    })) << "the payload sent after the reset never reached the wire";
}

// Buffering is requested, never inherited from a transport that connects asynchronously.
TEST(fd_event_client_tx_test, a_tcp_client_rejects_tx_while_its_connect_is_pending_by_default) {
    unreachable_peer peer;
    ASSERT_TRUE(peer.start()) << "could not saturate a loopback accept queue, "
                                 "so the connect under test would not stay pending";

    constexpr int connect_timeout_ms{500};
    tcp::tcp_client client("127.0.0.1", peer.port(), connect_timeout_ms);

    pump_for(client, 100ms);
    ASSERT_TRUE(client.on_error()) << "the connect resolved against an unreachable peer";

    EXPECT_FALSE(client.tx(std::vector<std::uint8_t>{1, 2, 3}))
        << "a payload was buffered while the TCP connect was still pending, "
           "although buffering was never requested";
}

// A UDP client resolves its connect on the first sync, so its window is everything the caller
// does between construction and that sync.
TEST(fd_event_client_tx_test, a_udp_client_rejects_tx_before_its_connect_runs_by_default) {
    constexpr std::uint16_t unused_remote_port{47500};
    udp::udp_client client("127.0.0.1", unused_remote_port);

    ASSERT_TRUE(client.on_error()) << "the client reports itself up before its first sync";

    EXPECT_FALSE(client.tx(udp::udp_payload{"early"}))
        << "a payload was buffered before the UDP connect ran, although buffering was never requested";
}

TEST(fd_event_client_tx_test, a_tcp_client_asked_to_buffer_accepts_tx_while_its_connect_is_pending) {
    unreachable_peer peer;
    ASSERT_TRUE(peer.start()) << "could not saturate a loopback accept queue, "
                                 "so the connect under test would not stay pending";

    constexpr int connect_timeout_ms{500};
    tcp::tcp_client client(utilities::tx_buffering::buffer, "127.0.0.1", peer.port(), connect_timeout_ms);

    pump_for(client, 100ms);
    ASSERT_TRUE(client.on_error()) << "the connect resolved against an unreachable peer";

    EXPECT_TRUE(client.tx(std::vector<std::uint8_t>{1, 2, 3}))
        << "a payload was rejected while the TCP connect was pending, although buffering was requested";
}

// Naming discard explicitly rejects just like naming nothing at all.
TEST(fd_event_client_tx_test, a_tcp_client_asked_to_discard_rejects_tx_while_its_connect_is_pending) {
    unreachable_peer peer;
    ASSERT_TRUE(peer.start()) << "could not saturate a loopback accept queue, "
                                 "so the connect under test would not stay pending";

    constexpr int connect_timeout_ms{500};
    tcp::tcp_client client(utilities::tx_buffering::discard, "127.0.0.1", peer.port(), connect_timeout_ms);

    pump_for(client, 100ms);
    ASSERT_TRUE(client.on_error()) << "the connect resolved against an unreachable peer";

    EXPECT_FALSE(client.tx(std::vector<std::uint8_t>{1, 2, 3}))
        << "a payload was buffered while the TCP connect was pending, although discard was requested";
}

TEST(fd_event_client_tx_test, an_instance_can_discard_although_its_policy_declares_buffering) {
    auto control = std::make_shared<async_connect_control>(std::vector<connect_attempt_plan>{{true, 0}});
    auto record = std::make_shared<tx_record>();
    deterministic_async_client client(utilities::tx_buffering::discard, control, record);

    ASSERT_TRUE(pump_until(client, 500ms, [&] { return control->attempt_count() >= 1; }));
    ASSERT_TRUE(client.on_error()) << "the connect already resolved, so there is no pending window to test";

    EXPECT_FALSE(client.tx(41)) << "a payload was buffered while the connect was pending, although the instance "
                                   "asked to discard and only its policy asked to buffer";

    control->release_all();
    ASSERT_TRUE(control->wait_for_attempt_completed(0, 500ms));
    ASSERT_TRUE(pump_until(client, 500ms, [&] { return not client.on_error(); })) << "the client never came up";
    EXPECT_TRUE(client.tx(43)) << "a payload was rejected once the client was up";
    EXPECT_TRUE(pump_until(client, 500ms, [&] { return record->values == std::vector<int>{43}; }))
        << "the discarded payload was delivered too, " << record->values.size() << " payloads reached the wire";
}

// A synchronous policy has no pre-connect window, so asking for the buffer cannot open one.
TEST(fd_event_client_tx_test, a_synchronous_client_asked_to_buffer_still_rejects_tx_before_it_is_up) {
    auto record = std::make_shared<tx_record>();
    deterministic_sync_client client(utilities::tx_buffering::buffer, record);

    ASSERT_TRUE(client.on_error()) << "the client reports itself up before its first sync";
    EXPECT_FALSE(client.tx(47)) << "a synchronous open() client buffered a payload before it was up, "
                                   "although it has no pre-connect window to buffer for";

    ASSERT_TRUE(pump_until(client, 500ms, [&] { return not client.on_error(); })) << "the client never came up";
    EXPECT_TRUE(client.tx(53)) << "a payload was rejected once the client was up";
    EXPECT_TRUE(pump_until(client, 500ms, [&] { return record->values == std::vector<int>{53}; }))
        << "the payload sent while up was not delivered, " << record->values.size() << " payloads reached the wire";
}

// The trait is the declaration side of the default. Absent means off.
TEST(fd_event_client_tx_test, the_buffering_trait_is_off_unless_a_policy_declares_it) {
    static_assert(not utilities::policy_buffers_tx_before_connect_v<tcp::tcp_socket>,
                  "tcp_socket declares no buffer_tx_before_connect, so it must not buffer");
    static_assert(not utilities::policy_buffers_tx_before_connect_v<deterministic_sync_policy>,
                  "a policy that declares nothing must not buffer");
    static_assert(utilities::policy_buffers_tx_before_connect_v<deterministic_async_policy>,
                  "a policy that declares buffer_tx_before_connect true must buffer");

    EXPECT_FALSE(utilities::policy_buffers_tx_before_connect_v<tcp::tcp_socket>)
        << "an async transport gained buffering without asking for it";
    EXPECT_TRUE(utilities::policy_buffers_tx_before_connect_v<deterministic_async_policy>)
        << "a declared opt in was not read off the policy";
}

// A completed rx notifies the error status event, which is only observed on the next poll cycle,
// after a reset queued in the same cycle has already moved the client to fresh. Reading fresh as a
// failure tears the client down without reopening it, and nothing reaches the wire afterwards.
TEST(fd_event_client_tx_test, a_reset_racing_a_completed_rx_keeps_the_client_open) {
    auto record = std::make_shared<tx_record>();
    auto source = std::make_shared<rx_source>();
    rx_capable_sync_client client(record, source);

    std::atomic<int> rx_calls{0};
    client.set_rx_handler([&](int const&, auto&) { ++rx_calls; });
    ASSERT_TRUE(pump_until(client, 500ms, [&] { return not client.on_error(); })) << "the client never came up";
    ASSERT_NE(client.get_raw_handler(), nullptr);

    source->push(5);
    client.reset();
    ASSERT_TRUE(pump_until(client, 500ms, [&] { return rx_calls.load() == 1; })) << "the rx under test never completed";
    pump_for(client, 100ms);

    EXPECT_NE(client.get_raw_handler(), nullptr) << "the client was torn down and never reopened";
    ASSERT_TRUE(pump_until(client, 500ms, [&] { return not client.on_error(); }))
        << "the client never came back up after the reset";
    EXPECT_TRUE(client.tx(23)) << "a payload was rejected after the reset";
    EXPECT_TRUE(pump_until(client, 500ms, [&] { return record->values == std::vector<int>{23}; }))
        << "the payload never reached the wire, " << record->values.size() << " payloads delivered";
}

// Code 0 is an up-edge on the connection, not merely "an error you saw is gone". A client that
// never reported a failure still signals the connection it just established.
TEST(fd_event_client_error_signal_test, a_first_successful_connect_reports_code_zero_once) {
    auto control = std::make_shared<async_connect_control>(std::vector<connect_attempt_plan>{{true, 0}});
    deterministic_async_client client(control);

    std::vector<int> codes;
    client.set_error_handler([&](int code, std::string const&) { codes.push_back(code); });

    ASSERT_TRUE(pump_until(client, 500ms, [&] { return control->attempt_count() >= 1; }));
    control->release(0);
    ASSERT_TRUE(control->wait_for_attempt_completed(0, 500ms));
    ASSERT_TRUE(pump_until(client, 500ms, [&] { return not client.on_error(); })) << "the client never came up";

    pump_for(client, 100ms);
    EXPECT_EQ(codes, std::vector<int>{0})
        << "a first connect that never failed did not report its up-edge exactly once";
}

// A reset on a healthy client reports the connection it opens, even though no failure was ever
// observed on the one it replaced.
TEST(fd_event_client_error_signal_test, a_clean_reset_reports_code_zero_once_per_reconnect) {
    auto control = std::make_shared<async_connect_control>(std::vector<connect_attempt_plan>{{true, 0}, {true, 0}});
    deterministic_async_client client(control);

    std::vector<int> codes;
    client.set_error_handler([&](int code, std::string const&) { codes.push_back(code); });

    ASSERT_TRUE(pump_until(client, 500ms, [&] { return control->attempt_count() >= 1; }));
    control->release(0);
    ASSERT_TRUE(control->wait_for_attempt_completed(0, 500ms));
    // The status event is only observed on the poll pass after the state moves, so the callback
    // trails on_error() by one cycle.
    ASSERT_TRUE(pump_until(client, 500ms, [&] { return codes == std::vector<int>{0}; }))
        << "the first connect did not report its up-edge";

    client.reset();
    ASSERT_TRUE(pump_until(client, 500ms, [&] { return control->attempt_count() >= 2; }));
    control->release(1);
    ASSERT_TRUE(control->wait_for_attempt_completed(1, 500ms));
    ASSERT_TRUE(pump_until(client, 500ms, [&] { return not client.on_error(); })) << "the client never came back up";

    pump_for(client, 100ms);
    EXPECT_EQ(codes, (std::vector<int>{0, 0})) << "a clean reset did not report the reconnect exactly once";
}

// A successful read reports code 0 through the same path a successful connect does. It is not an
// up-edge on a connection, so interleaving reads with a reset may not add one.
TEST(fd_event_client_error_signal_test, a_read_interleaved_with_a_reset_reports_one_up_edge_per_connect) {
    auto control = std::make_shared<async_connect_control>(std::vector<connect_attempt_plan>{{true, 0}, {true, 0}});
    auto record = std::make_shared<tx_record>();
    auto source = std::make_shared<rx_source>();
    deterministic_async_client client(control, record, source);

    std::vector<int> codes;
    std::atomic<int> rx_calls{0};
    client.set_error_handler([&](int code, std::string const&) { codes.push_back(code); });
    client.set_rx_handler([&](int const&, auto&) {
        if (++rx_calls == 1) {
            client.reset();
            client.tx(89);
        }
    });

    ASSERT_TRUE(pump_until(client, 500ms, [&] { return control->attempt_count() >= 1; }));
    control->release(0);
    ASSERT_TRUE(control->wait_for_attempt_completed(0, 500ms));
    ASSERT_TRUE(pump_until(client, 500ms, [&] { return codes == std::vector<int>{0}; }))
        << "the first connect did not report its up-edge";

    // Same shape as the flush test: one pass arms write interest, the next reports read and write
    // together, so the read lands on a connection the callback has already retired.
    ASSERT_TRUE(client.tx(87));
    client.sync(1ms);
    ASSERT_TRUE(record->values.empty()) << "the queued payload drained before the read pass";
    source->push(5);
    client.sync(1ms);
    ASSERT_EQ(rx_calls.load(), 1) << "the read under test never reached the callback";

    ASSERT_TRUE(pump_until(client, 500ms, [&] { return control->attempt_count() >= 2; }));
    control->release(1);
    ASSERT_TRUE(control->wait_for_attempt_completed(1, 500ms));
    ASSERT_TRUE(pump_until(client, 500ms, [&] { return not client.on_error(); })) << "the client never came back up";

    pump_for(client, 100ms);
    EXPECT_EQ(codes, (std::vector<int>{0, 0})) << "a successful read added an up-edge of its own";
}

// The failure the caller has to act on stays visible, and the reconnect that follows is reported
// once, so a consumer can pair the two.
TEST(fd_event_client_error_signal_test, a_failure_then_a_reset_reports_the_error_then_code_zero) {
    auto control =
        std::make_shared<async_connect_control>(std::vector<connect_attempt_plan>{{false, ECONNREFUSED}, {true, 0}});
    deterministic_async_client client(control);

    std::vector<int> codes;
    client.set_error_handler([&](int code, std::string const&) { codes.push_back(code); });

    ASSERT_TRUE(pump_until(client, 500ms, [&] { return control->attempt_count() >= 1; }));
    control->release(0);
    ASSERT_TRUE(control->wait_for_attempt_completed(0, 500ms));
    ASSERT_TRUE(pump_until(client, 500ms, [&] { return codes == std::vector<int>{ECONNREFUSED}; }))
        << "the failed connect did not report its error";

    client.reset();
    ASSERT_TRUE(pump_until(client, 500ms, [&] { return control->attempt_count() >= 2; }));
    control->release(1);
    ASSERT_TRUE(control->wait_for_attempt_completed(1, 500ms));
    ASSERT_TRUE(pump_until(client, 500ms, [&] { return not client.on_error(); })) << "the client never came up";

    pump_for(client, 100ms);
    EXPECT_EQ(codes, (std::vector<int>{ECONNREFUSED, 0})) << "the reconnect after a failure was not reported once";
}

// A read that fails on an established connection is the only route that turns a dead socket into a
// reported errno. Folding the read status into the guard that suppresses a retired handle would
// report nothing instead, which leaves the client wedged while it still claims to be up.
TEST(fd_event_client_error_signal_test, a_failed_read_reports_the_socket_error) {
    auto source = std::make_shared<rx_source>();
    auto fault = std::make_shared<rx_fault_plan>();
    faulting_rx_sync_client client(source, fault);

    std::vector<int> codes;
    client.set_error_handler([&](int code, std::string const&) { codes.push_back(code); });
    ASSERT_TRUE(pump_until(client, 500ms, [&] { return codes == std::vector<int>{0}; }))
        << "the client never reported its up-edge";

    fault->rx_fails = true;
    fault->error_code = ECONNRESET;
    source->push(7);

    ASSERT_TRUE(pump_until(client, 500ms, [&] { return codes.size() > 1; }))
        << "a read that failed on an established connection reported nothing";
    EXPECT_EQ(codes, (std::vector<int>{0, ECONNRESET})) << "the failed read did not report the socket error";
    EXPECT_TRUE(client.on_error()) << "the client still reports itself up after a failed read";
}

// Without a handshake phase the client is ready the moment the transport connects, so a
// registration landing in the publish window and the connected handler would each release.
TEST(fd_event_client_async_test, ready_action_registered_in_publish_window_fires_once) {
    auto control = std::make_shared<async_connect_control>(std::vector<connect_attempt_plan>{{true, 0}});
    deterministic_async_client client(control);

    ASSERT_TRUE(pump_until(client, 500ms, [&] { return control->attempt_count() >= 1; }));
    control->release(0);
    ASSERT_TRUE(control->wait_for_attempt_completed(0, 500ms));
    // Settle without pumping, so the published result is queued but not yet observed.
    std::this_thread::sleep_for(50ms);

    std::atomic<int> ready_calls{0};
    client.set_on_ready_action([&] { ++ready_calls; });

    pump_for(client, 200ms);
    EXPECT_EQ(ready_calls.load(), 1) << "on_ready was delivered more than once for one connection";
}

TEST(fd_event_client_async_test, ready_action_registered_after_ready_fires_immediately) {
    auto control = std::make_shared<async_connect_control>(std::vector<connect_attempt_plan>{{true, 0}});
    deterministic_async_client client(control);

    std::atomic<int> first_calls{0};
    client.set_on_ready_action([&] { ++first_calls; });

    ASSERT_TRUE(pump_until(client, 500ms, [&] { return control->attempt_count() >= 1; }));
    control->release(0);
    ASSERT_TRUE(pump_until(client, 500ms, [&] { return first_calls.load() >= 1; }));

    std::atomic<int> second_calls{0};
    client.set_on_ready_action([&] { ++second_calls; });

    EXPECT_TRUE(pump_until(client, 500ms, [&] { return second_calls.load() >= 1; }))
        << "the late registration was dropped on an already ready connection";
    pump_for(client, 30ms);
    EXPECT_EQ(second_calls.load(), 1);
}

TEST(fd_event_client_handshake_test, first_handshake_step_runs_without_fd_readiness) {
    auto script = std::make_shared<handshake_script>(
        std::vector<handshake_step_result>{handshake_step_result::want_read, handshake_step_result::complete});
    scripted_handshake_client client(script);

    std::atomic<int> ready_calls{0};
    client.set_on_ready_action([&] { ++ready_calls; });

    ASSERT_TRUE(pump_until(client, 500ms, [&] { return script->step_count() >= 1; }));
    pump_for(client, 30ms);
    EXPECT_EQ(script->step_count(), 1u) << "handshake must not advance without a peer record";
    EXPECT_EQ(ready_calls.load(), 0);

    script->deliver_peer_record();
    EXPECT_TRUE(pump_until(client, 500ms, [&] { return ready_calls.load() == 1; }));
    EXPECT_EQ(script->step_count(), 2u);
    EXPECT_FALSE(client.on_error());
}

TEST(fd_event_client_handshake_test, handshake_completing_on_first_step_fires_ready) {
    auto script =
        std::make_shared<handshake_script>(std::vector<handshake_step_result>{handshake_step_result::complete});
    scripted_handshake_client client(script);

    std::atomic<int> ready_calls{0};
    client.set_on_ready_action([&] { ++ready_calls; });

    EXPECT_TRUE(pump_until(client, 500ms, [&] { return ready_calls.load() == 1; }));
    EXPECT_EQ(script->step_count(), 1u);
    EXPECT_EQ(script->connect_count(), 1u);
}

TEST(fd_event_client_handshake_test, on_ready_action_waits_for_handshake_completion) {
    auto script = std::make_shared<handshake_script>(std::vector<handshake_step_result>{
        handshake_step_result::want_read, handshake_step_result::want_read, handshake_step_result::complete});
    scripted_handshake_client client(script);

    std::atomic<int> ready_calls{0};
    client.set_on_ready_action([&] { ++ready_calls; });

    ASSERT_TRUE(pump_until(client, 500ms, [&] { return script->step_count() >= 1; }));
    EXPECT_EQ(ready_calls.load(), 0);

    script->deliver_peer_record();
    ASSERT_TRUE(pump_until(client, 500ms, [&] { return script->step_count() >= 2; }));
    pump_for(client, 30ms);
    EXPECT_EQ(ready_calls.load(), 0) << "on_ready fired before the handshake completed";

    script->deliver_peer_record();
    EXPECT_TRUE(pump_until(client, 500ms, [&] { return ready_calls.load() == 1; }));
    EXPECT_EQ(script->step_count(), 3u) << "the handshake was stepped for an event it did not ask for";
}

TEST(fd_event_client_handshake_test, payloads_queued_during_handshake_flush_in_order) {
    auto script = std::make_shared<handshake_script>(
        std::vector<handshake_step_result>{handshake_step_result::want_read, handshake_step_result::complete});
    scripted_handshake_client client(script);

    std::atomic<int> ready_calls{0};
    client.set_on_ready_action([&] { ++ready_calls; });

    ASSERT_TRUE(pump_until(client, 500ms, [&] { return script->step_count() >= 1; }));
    ASSERT_EQ(ready_calls.load(), 0);

    EXPECT_TRUE(client.tx(1));
    EXPECT_TRUE(client.tx(2));
    EXPECT_TRUE(client.tx(3));
    pump_for(client, 30ms);
    EXPECT_TRUE(script->tx_payloads().empty()) << "payloads must not be written during the handshake";
    EXPECT_EQ(script->step_count(), 1u) << "a queued payload must not drive a handshake step";

    script->deliver_peer_record();
    EXPECT_TRUE(pump_until(client, 500ms, [&] { return script->tx_payloads().size() == 3; }));
    EXPECT_EQ(script->tx_payloads(), std::vector<int>({1, 2, 3}));
    EXPECT_EQ(ready_calls.load(), 1);
}

TEST(fd_event_client_handshake_test, the_tx_buffer_is_bounded_during_the_handshake) {
    auto script = std::make_shared<handshake_script>(
        std::vector<handshake_step_result>{handshake_step_result::want_read, handshake_step_result::complete});
    scripted_handshake_client client(script);

    ASSERT_TRUE(pump_until(client, 500ms, [&] { return script->step_count() >= 1; }));

    constexpr auto cap = scripted_handshake_client::max_buffered_tx_payloads;
    std::size_t accepted{0};
    for (std::size_t i = 0; i < cap + 100; ++i) {
        if (not client.tx(static_cast<int>(i))) {
            break;
        }
        ++accepted;
    }

    EXPECT_EQ(accepted, cap) << "the handshake buffered " << accepted << " payloads against a cap of " << cap;
    EXPECT_FALSE(client.tx(0)) << "the buffer accepted a payload past its cap";

    // tx only notifies: without a loop pass the pending handshake is never asked to hold back.
    pump_for(client, 100ms);
    EXPECT_EQ(script->step_count(), 1u) << "a buffered payload stepped the handshake";
    EXPECT_TRUE(script->tx_payloads().empty()) << "payloads must not be written during the handshake";
}

// The policy reports errno 0 here (a protocol level failure), so the client has to substitute a
// code that consumers filtering on 'code != 0' actually see. The cleared-error callback is
// recorded too: 'error cleared' after a fatal failure passes a dead connection off as healthy.
TEST(fd_event_client_handshake_test, handshake_failure_reports_one_nonzero_error) {
    auto script = std::make_shared<handshake_script>(
        std::vector<handshake_step_result>{handshake_step_result::want_read, handshake_step_result::fail}, 0);
    scripted_handshake_client client(script);

    std::atomic<int> ready_calls{0};
    std::vector<std::pair<int, std::string>> error_reports;
    client.set_on_ready_action([&] { ++ready_calls; });
    client.set_error_handler([&](int code, std::string const& msg) { error_reports.emplace_back(code, msg); });

    ASSERT_TRUE(pump_until(client, 500ms, [&] { return script->step_count() >= 1; }));

    // The failing step consumes one, so the fd stays readable and would step a dead handshake.
    script->deliver_peer_record();
    script->deliver_peer_record();
    EXPECT_TRUE(pump_until(client, 500ms, [&] { return error_reports.size() >= 2; }));
    pump_for(client, 50ms);

    ASSERT_EQ(error_reports.size(), 2u);
    EXPECT_EQ(error_reports[0].first, 0);
    EXPECT_EQ(error_reports[1].first, ECONNRESET);
    EXPECT_EQ(error_reports[1].second, "scripted handshake failure");
    EXPECT_EQ(ready_calls.load(), 0);
    EXPECT_TRUE(client.on_error());
}

// A socketpair is always writable, so a want_write state advances with no peer record and a
// want_read state must not. That only holds if monitoring one event removes the other.
TEST(fd_event_client_handshake_test, handshake_monitors_write_and_read_exclusively) {
    auto script = std::make_shared<handshake_script>(std::vector<handshake_step_result>{
        handshake_step_result::want_write, handshake_step_result::want_read, handshake_step_result::complete});
    scripted_handshake_client client(script);

    std::atomic<int> ready_calls{0};
    client.set_on_ready_action([&] { ++ready_calls; });

    ASSERT_TRUE(pump_until(client, 500ms, [&] { return script->step_count() >= 2; }));

    pump_for(client, 30ms);
    EXPECT_EQ(script->step_count(), 2u) << "writing is still monitored while the handshake waits for reading";
    EXPECT_EQ(ready_calls.load(), 0);

    script->deliver_peer_record();
    EXPECT_TRUE(pump_until(client, 500ms, [&] { return ready_calls.load() == 1; }));
    EXPECT_EQ(script->step_count(), 3u);
}

TEST(fd_event_client_handshake_test, ready_action_registered_mid_handshake_waits_for_completion) {
    auto script = std::make_shared<handshake_script>(
        std::vector<handshake_step_result>{handshake_step_result::want_read, handshake_step_result::complete});
    scripted_handshake_client client(script);

    ASSERT_TRUE(pump_until(client, 500ms, [&] { return script->step_count() >= 1; }));

    std::atomic<int> ready_calls{0};
    client.set_on_ready_action([&] { ++ready_calls; });

    pump_for(client, 30ms);
    EXPECT_EQ(ready_calls.load(), 0) << "on_ready fired on a connection whose handshake is still pending";

    script->deliver_peer_record();
    EXPECT_TRUE(pump_until(client, 500ms, [&] { return ready_calls.load() >= 1; }));
    pump_for(client, 30ms);
    EXPECT_EQ(ready_calls.load(), 1) << "on_ready was delivered more than once for one connection";
}

TEST(fd_event_client_handshake_test, completed_handshake_delivers_application_payloads) {
    auto script = std::make_shared<handshake_script>(
        std::vector<handshake_step_result>{handshake_step_result::want_write, handshake_step_result::complete});
    scripted_handshake_client client(script);

    std::atomic<int> ready_calls{0};
    std::atomic<int> rx_calls{0};
    std::atomic<int> last_payload{0};
    client.set_on_ready_action([&] { ++ready_calls; });
    client.set_rx_handler([&](int const& payload, auto&) {
        ++rx_calls;
        last_payload = payload;
    });

    ASSERT_TRUE(pump_until(client, 500ms, [&] { return ready_calls.load() == 1; }));
    ASSERT_EQ(script->step_count(), 2u);
    ASSERT_EQ(rx_calls.load(), 0);

    script->deliver_byte(7);
    EXPECT_TRUE(pump_until(client, 500ms, [&] { return rx_calls.load() >= 1; }))
        << "the completed handshake left the connection unmonitored for reading";
    EXPECT_EQ(last_payload.load(), 7);
}

TEST(fd_event_client_handshake_test, ready_action_fires_again_after_reset) {
    auto script =
        std::make_shared<handshake_script>(std::vector<handshake_step_result>{handshake_step_result::complete});
    scripted_handshake_client client(script);

    std::atomic<int> ready_calls{0};
    client.set_on_ready_action([&] { ++ready_calls; });

    ASSERT_TRUE(pump_until(client, 500ms, [&] { return ready_calls.load() == 1; }));

    ASSERT_TRUE(client.reset());
    EXPECT_TRUE(pump_until(client, 500ms, [&] { return ready_calls.load() >= 2; }))
        << "the ready action stayed set from the previous connection";
    pump_for(client, 30ms);
    EXPECT_EQ(ready_calls.load(), 2);
    EXPECT_GE(script->connect_count(), 2u);
}

TEST(fd_event_client_handshake_test, unsupported_desired_event_fails_the_handshake) {
    auto script = std::make_shared<handshake_script>(
        std::vector<handshake_step_result>{handshake_step_result::want_invalid}, ECONNREFUSED);
    scripted_handshake_client client(script);

    std::atomic<int> ready_calls{0};
    std::atomic<int> error_calls{0};
    int last_code{0};
    std::string last_message;
    client.set_on_ready_action([&] { ++ready_calls; });
    client.set_error_handler([&](int code, std::string const& msg) {
        if (code != 0) {
            ++error_calls;
            last_code = code;
            last_message = msg;
        }
    });

    EXPECT_TRUE(pump_until(client, 500ms, [&] { return error_calls.load() >= 1; }));
    pump_for(client, 30ms);
    EXPECT_EQ(error_calls.load(), 1);
    EXPECT_EQ(last_code, EPROTO) << "a local policy error was reported as " << std::strerror(last_code);
    EXPECT_EQ(last_message, "handshake policy requested an event that is neither read nor write");
    EXPECT_EQ(ready_calls.load(), 0);
    EXPECT_TRUE(client.on_error());
}

TEST(fd_event_client_handshake_test, handshake_failure_keeps_the_policy_error_code) {
    auto script = std::make_shared<handshake_script>(
        std::vector<handshake_step_result>{handshake_step_result::want_read, handshake_step_result::fail},
        ECONNREFUSED);
    scripted_handshake_client client(script);

    std::atomic<int> error_calls{0};
    std::atomic<int> last_code{0};
    std::string last_message;
    client.set_error_handler([&](int code, std::string const& msg) {
        if (code != 0) {
            ++error_calls;
            last_code = code;
            last_message = msg;
        }
    });

    ASSERT_TRUE(pump_until(client, 500ms, [&] { return script->step_count() >= 1; }));
    script->deliver_peer_record();
    EXPECT_TRUE(pump_until(client, 500ms, [&] { return error_calls.load() >= 1; }));

    EXPECT_EQ(last_code.load(), ECONNREFUSED);
    EXPECT_EQ(last_message, "scripted handshake failure");
}

TEST(fd_event_client_handshake_test, reset_after_handshake_failure_steps_again) {
    auto script = std::make_shared<handshake_script>(
        std::vector<handshake_step_result>{handshake_step_result::want_read, handshake_step_result::fail},
        ECONNREFUSED);
    scripted_handshake_client client(script);

    std::atomic<int> error_calls{0};
    client.set_error_handler([&](int code, std::string const&) {
        if (code != 0) {
            ++error_calls;
        }
    });

    ASSERT_TRUE(pump_until(client, 500ms, [&] { return script->step_count() >= 1; }));
    script->deliver_peer_record();
    ASSERT_TRUE(pump_until(client, 500ms, [&] { return error_calls.load() >= 1; }));
    auto const steps_before_reset = script->step_count();

    ASSERT_TRUE(client.reset());
    EXPECT_TRUE(pump_until(client, 500ms,
                           [&] { return script->connect_count() >= 2 and script->step_count() > steps_before_reset; }));
    EXPECT_GE(script->connect_count(), 2u);
    EXPECT_GT(script->step_count(), steps_before_reset) << "the handshake stayed dead across reset";
}

TEST(fd_event_client_handshake_test, ready_action_registered_after_ready_fires_immediately) {
    auto script =
        std::make_shared<handshake_script>(std::vector<handshake_step_result>{handshake_step_result::complete});
    scripted_handshake_client client(script);

    std::atomic<int> first_calls{0};
    client.set_on_ready_action([&] { ++first_calls; });
    ASSERT_TRUE(pump_until(client, 500ms, [&] { return first_calls.load() == 1; }));

    std::atomic<int> second_calls{0};
    client.set_on_ready_action([&] { ++second_calls; });

    EXPECT_TRUE(pump_until(client, 500ms, [&] { return second_calls.load() >= 1; }))
        << "the late registration was dropped on an already ready connection";
    pump_for(client, 30ms);
    EXPECT_EQ(second_calls.load(), 1);
    EXPECT_EQ(first_calls.load(), 1);
}

// The connected status of a failed connection survives until the queued teardown runs.
TEST(fd_event_client_handshake_test, ready_action_registered_after_failure_stays_silent) {
    auto script =
        std::make_shared<handshake_script>(std::vector<handshake_step_result>{handshake_step_result::complete});
    scripted_handshake_client client(script);

    std::atomic<int> ready_calls{0};
    client.set_on_ready_action([&] { ++ready_calls; });
    ASSERT_TRUE(pump_until(client, 500ms, [&] { return ready_calls.load() == 1; }));

    // Recorded by the fd handler, which runs before the registration queued below.
    script->close_peer();
    std::atomic<int> late_calls{0};
    client.set_on_ready_action([&] { ++late_calls; });

    pump_for(client, 100ms);
    EXPECT_EQ(late_calls.load(), 0) << "a failed connection released the ready action";
    EXPECT_TRUE(client.on_error());
}

// A peer that connects and then goes silent produces no notification of any kind, so only a
// deadline turns that into something the consumer can act on.
TEST(fd_event_client_handshake_test, a_stalled_handshake_fails_on_its_deadline) {
    auto script = std::make_shared<handshake_script>(
        std::vector<handshake_step_result>{handshake_step_result::want_read}, 0, 60ms);
    scripted_handshake_client client(script);

    std::atomic<int> ready_calls{0};
    std::vector<int> codes;
    client.set_on_ready_action([&] { ++ready_calls; });
    collect_error_codes(client, codes);

    ASSERT_TRUE(pump_until(client, 500ms, [&] { return script->step_count() >= 1; }));
    EXPECT_TRUE(pump_until(client, 500ms, [&] { return not codes.empty(); }))
        << "the stalled handshake was never reported";
    pump_for(client, 50ms);

    ASSERT_EQ(codes.size(), 1u) << "reported: " << describe_codes(codes);
    EXPECT_EQ(codes[0], ETIMEDOUT) << "the stalled handshake was reported as " << std::strerror(codes[0]);
    EXPECT_EQ(ready_calls.load(), 0) << "a client that never negotiated anything was published as ready";
    EXPECT_EQ(script->step_count(), 1u) << "the handshake was stepped past its deadline";
    EXPECT_TRUE(client.on_error());

    // A client bounding only its first handshake leaves every reconnect unbounded.
    ASSERT_TRUE(client.reset());
    EXPECT_TRUE(pump_until(client, 500ms, [&] { return codes.size() >= 2; }))
        << "the handshake after reset was never bounded, reported: " << describe_codes(codes);

    ASSERT_EQ(codes.size(), 2u) << "reported: " << describe_codes(codes);
    EXPECT_EQ(codes[1], ETIMEDOUT) << "the stalled handshake after reset was reported as " << std::strerror(codes[1]);
    EXPECT_EQ(ready_calls.load(), 0) << "a client that never negotiated anything was published as ready";
}

TEST(fd_event_client_handshake_test, a_dribbling_handshake_fails_on_its_first_deadline) {
    auto script = std::make_shared<handshake_script>(
        std::vector<handshake_step_result>{handshake_step_result::want_read, handshake_step_result::want_read,
                                           handshake_step_result::want_read},
        0, 200ms);
    scripted_handshake_client client(script);

    std::atomic<int> ready_calls{0};
    std::vector<int> codes;
    client.set_on_ready_action([&] { ++ready_calls; });
    collect_error_codes(client, codes);

    ASSERT_TRUE(pump_until(client, 500ms, [&] { return script->step_count() >= 1; }));

    // Stepping every 40ms inside a 200ms bound, for longer than that bound: a deadline restarted
    // per step would never expire while this runs.
    auto const stop = std::chrono::steady_clock::now() + 700ms;
    while (std::chrono::steady_clock::now() < stop and codes.empty()) {
        script->deliver_peer_record();
        pump_for(client, 40ms);
    }

    ASSERT_FALSE(codes.empty()) << "a handshake still stepping was never bounded, after " << script->step_count()
                                << " steps";
    ASSERT_EQ(codes.size(), 1u) << "reported: " << describe_codes(codes);
    EXPECT_EQ(codes[0], ETIMEDOUT) << "the unfinished handshake was reported as " << std::strerror(codes[0]);
    EXPECT_GT(script->step_count(), 1u) << "the handshake never took a second step, so nothing bounded it";
    EXPECT_EQ(ready_calls.load(), 0) << "a client that never negotiated anything was published as ready";
    EXPECT_TRUE(client.on_error());
}

// The terminal notification arrives on the same descriptor the pending handshake waits on.
// Handling the handshake first steps a dead connection over and over and reports nothing.
TEST(fd_event_client_handshake_test, a_peer_drop_during_the_handshake_is_reported_once) {
    auto script =
        std::make_shared<handshake_script>(std::vector<handshake_step_result>{handshake_step_result::want_read});
    scripted_handshake_client client(script);

    std::atomic<int> ready_calls{0};
    std::vector<int> codes;
    client.set_on_ready_action([&] { ++ready_calls; });
    collect_error_codes(client, codes);

    ASSERT_TRUE(pump_until(client, 500ms, [&] { return script->step_count() >= 1; }));
    auto const steps_before_drop = script->step_count();

    script->close_peer();
    EXPECT_TRUE(pump_until(client, 500ms, [&] { return not codes.empty(); }))
        << "the peer drop was never reported, the handshake was stepped " << script->step_count() << " times";
    pump_for(client, 50ms);

    ASSERT_EQ(codes.size(), 1u) << "reported: " << describe_codes(codes);
    EXPECT_EQ(codes[0], ENOTCONN) << "the dropped peer was reported as " << std::strerror(codes[0]);
    EXPECT_EQ(script->step_count(), steps_before_drop) << "the handshake was stepped after the peer was gone";
    EXPECT_EQ(ready_calls.load(), 0) << "a client whose peer dropped mid handshake was published as ready";
    EXPECT_TRUE(client.on_error());
}

TEST(fd_event_client_handshake_test, a_completed_handshake_outlives_its_deadline) {
    auto script = std::make_shared<handshake_script>(
        std::vector<handshake_step_result>{handshake_step_result::complete}, 0, 60ms);
    scripted_handshake_client client(script);

    std::atomic<int> ready_calls{0};
    std::vector<int> codes;
    client.set_on_ready_action([&] { ++ready_calls; });
    collect_error_codes(client, codes);

    ASSERT_TRUE(pump_until(client, 500ms, [&] { return ready_calls.load() == 1; }));
    pump_for(client, 250ms);

    EXPECT_TRUE(codes.empty()) << "the deadline of the finished handshake failed the connection: "
                               << describe_codes(codes);
    EXPECT_FALSE(client.on_error());
    EXPECT_EQ(ready_calls.load(), 1);
}

// A terminal notification is level triggered: the descriptor keeps notifying until the queued
// teardown removes it. Resolving the code reads SO_ERROR, which consumes it, so a re-dispatch
// would replace the real reason with the fallback.
TEST(fd_event_client_poll_error_test, socket_error_is_reported_once_and_not_overwritten) {
    const int fd = make_refused_tcp_fd();
    ASSERT_GE(fd, 0) << "could not produce a refused loopback connect";
    auto script = std::make_shared<poll_error_script>(fd);
    poll_error_client client(script);

    std::vector<int> codes;
    collect_error_codes(client, codes);

    EXPECT_TRUE(pump_until(client, 500ms, [&] { return not codes.empty(); }));
    pump_for(client, 50ms);

    ASSERT_FALSE(codes.empty()) << "the terminal notification was never reported";
    EXPECT_EQ(codes[0], ECONNREFUSED) << "the socket error was consumed and then reported as "
                                      << std::strerror(codes[0]);
    EXPECT_EQ(codes.size(), 1u) << "the terminal notification was reported more than once: " << describe_codes(codes);
    EXPECT_TRUE(client.on_error());
}

// Injected from the ready action, which runs after the descriptor is monitored and before the
// loop can dispatch it, so the connect path itself stays clean.
TEST(fd_event_client_poll_error_test, policy_error_wins_over_the_socket_error) {
    const int fd = make_refused_tcp_fd();
    ASSERT_GE(fd, 0) << "could not produce a refused loopback connect";
    auto script = std::make_shared<poll_error_script>(fd);
    poll_error_client client(script);

    std::vector<int> codes;
    collect_error_codes(client, codes);
    client.set_on_ready_action([&script] { script->set_policy_error(ETIMEDOUT); });

    EXPECT_TRUE(pump_until(client, 500ms, [&] { return not codes.empty(); }));
    pump_for(client, 50ms);

    ASSERT_FALSE(codes.empty()) << "the terminal notification was never reported";
    EXPECT_EQ(codes[0], ETIMEDOUT) << "the policy error was discarded in favor of " << std::strerror(codes[0]);
    EXPECT_EQ(codes.size(), 1u) << "the terminal notification was reported more than once: " << describe_codes(codes);
}

// A hangup on something that is not a socket has no code to read back: getsockopt fails on a
// pipe, a serial line and a tun/tap device alike.
TEST(fd_event_client_poll_error_test, hangup_without_a_code_reports_not_connected) {
    const int fd = make_hungup_pipe_read_fd();
    ASSERT_GE(fd, 0) << "could not produce a hung up pipe";
    auto script = std::make_shared<poll_error_script>(fd);
    poll_error_client client(script);

    std::vector<int> codes;
    collect_error_codes(client, codes);

    EXPECT_TRUE(pump_until(client, 500ms, [&] { return not codes.empty(); }));
    pump_for(client, 50ms);

    ASSERT_FALSE(codes.empty()) << "the terminal notification was never reported";
    EXPECT_EQ(codes[0], ENOTCONN) << "a bare hangup was reported as " << std::strerror(codes[0]);
    EXPECT_EQ(codes.size(), 1u) << "the terminal notification was reported more than once: " << describe_codes(codes);
}

TEST(fd_event_client_poll_error_test, error_without_a_code_reports_io_failure) {
    const int fd = make_errored_pipe_write_fd();
    ASSERT_GE(fd, 0) << "could not produce an errored pipe";
    auto script = std::make_shared<poll_error_script>(fd);
    poll_error_client client(script);

    std::vector<int> codes;
    collect_error_codes(client, codes);

    EXPECT_TRUE(pump_until(client, 500ms, [&] { return not codes.empty(); }));
    pump_for(client, 50ms);

    ASSERT_FALSE(codes.empty()) << "the terminal notification was never reported";
    EXPECT_EQ(codes[0], EIO) << "a bare error notification was reported as " << std::strerror(codes[0]);
    EXPECT_EQ(codes.size(), 1u) << "the terminal notification was reported more than once: " << describe_codes(codes);
}
