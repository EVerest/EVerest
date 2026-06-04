// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

#include <everest/io/event/event_fd.hpp>
#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/event/fd_event_register_interface.hpp>
#include <everest/io/event/fd_event_sync_interface.hpp>
#include <everest/io/event/timer_fd.hpp>

using namespace everest::lib::io::event;
using namespace std::chrono_literals;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << "\n";
        std::exit(1);
    }
}

template <typename T> void require_eq(const T& actual, const T& expected, const char* message) {
    if (!(actual == expected)) {
        std::cerr << "FAILED: " << message << "\n";
        std::exit(1);
    }
}

class scoped_fd {
public:
    scoped_fd() = default;
    explicit scoped_fd(int fd) : m_fd(fd) {
    }
    scoped_fd(const scoped_fd&) = delete;
    scoped_fd& operator=(const scoped_fd&) = delete;
    scoped_fd(scoped_fd&& other) noexcept : m_fd(std::exchange(other.m_fd, -1)) {
    }
    scoped_fd& operator=(scoped_fd&& other) noexcept {
        if (this != &other) {
            close();
            m_fd = std::exchange(other.m_fd, -1);
        }
        return *this;
    }
    ~scoped_fd() {
        close();
    }

    int get() const {
        return m_fd;
    }
    void close() {
        if (m_fd != -1) {
            ::close(m_fd);
            m_fd = -1;
        }
    }

private:
    int m_fd{-1};
};

std::pair<scoped_fd, scoped_fd> make_pipe() {
    int fds[2] = {-1, -1};
    require(::pipe(fds) == 0, "pipe should be created");
    return {scoped_fd(fds[0]), scoped_fd(fds[1])};
}

std::pair<scoped_fd, scoped_fd> make_socket_pair() {
    int fds[2] = {-1, -1};
    require(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0, "socketpair should be created");
    return {scoped_fd(fds[0]), scoped_fd(fds[1])};
}

void drain_byte(int fd) {
    char buffer = 0;
    require(::read(fd, &buffer, sizeof(buffer)) == sizeof(buffer), "fd should provide one byte");
}

void write_byte(int fd) {
    const char value = 'x';
    require(::write(fd, &value, sizeof(value)) == sizeof(value), "fd should accept one byte");
}

void test_basic_raw_fd_registration_dispatch_and_timeout() {
    fd_event_handler handler;
    event_fd signal;
    int calls = 0;

    require(handler.register_event_handler(
                signal.get_raw_fd(),
                [&](const fd_event_handler::event_list& events) {
                    require(events & poll_events::read, "raw eventfd callback should receive read event");
                    require(signal.read().has_value(), "raw eventfd callback should manually drain event");
                    ++calls;
                },
                poll_events::read),
            "raw eventfd registration should succeed");

    require(!handler.poll(1ms), "poll should return false on timeout");
    require_eq(handler.last_ready_count(), std::size_t{0}, "timeout should reset last_ready_count");

    require(signal.notify(), "eventfd notify should succeed");
    require(handler.poll(20ms), "poll should return true when raw eventfd is readable");
    require_eq(calls, 1, "raw eventfd callback should be dispatched once");
    require_eq(handler.last_ready_count(), std::size_t{1}, "last_ready_count should report one ready fd");

    require(!handler.poll(1ms), "poll should return false after event is drained");
    require_eq(handler.last_ready_count(), std::size_t{0}, "last_ready_count should reset after no event");
}

void test_read_write_and_hangup_event_masks() {
    fd_event_handler handler;
    auto read_pipe = make_pipe();
    auto write_pipe = make_pipe();
    auto hangup_pipe = make_pipe();
    int read_calls = 0;
    int write_calls = 0;
    int hangup_calls = 0;

    require(handler.register_event_handler(
                read_pipe.first.get(),
                [&](const fd_event_handler::event_list& events) {
                    require(events & poll_events::read, "pipe read callback should receive read event");
                    drain_byte(read_pipe.first.get());
                    ++read_calls;
                },
                poll_events::read),
            "read pipe registration should succeed");

    require(handler.register_event_handler(
                write_pipe.second.get(),
                [&](const fd_event_handler::event_list& events) {
                    require(events & poll_events::write, "pipe write callback should receive write event");
                    ++write_calls;
                },
                poll_events::write),
            "write pipe registration should succeed");

    require(handler.register_event_handler(
                hangup_pipe.first.get(),
                [&](const fd_event_handler::event_list& events) {
                    require(events & poll_events::hungup, "pipe hangup callback should receive hungup event");
                    ++hangup_calls;
                },
                poll_events::hungup),
            "hangup pipe registration should succeed");

    write_byte(read_pipe.second.get());
    require(handler.poll(20ms), "read event should dispatch");
    require_eq(read_calls, 1, "read callback should run once");

    if (write_calls == 0) {
        require(handler.poll(20ms), "write event should dispatch");
    }
    require_eq(write_calls, 1, "write callback should run once");
    require(handler.remove_event_handler(write_pipe.second.get()), "write fd should be removable");

    hangup_pipe.second.close();
    require(handler.poll(20ms), "hangup event should dispatch");
    require_eq(hangup_calls, 1, "hangup callback should run once");
}

void test_wrapped_event_fd_and_timer_fd_drain_before_callback() {
    fd_event_handler event_handler;
    event_fd_base nonblocking_event(0, EFD_NONBLOCK);
    bool event_was_drained = false;

    require(event_handler.register_event_handler(&nonblocking_event,
                                                 [&](const fd_event_handler::event_list& events) {
                                                     require(events & poll_events::read,
                                                             "event_fd wrapper callback should receive read event");
                                                     event_was_drained = !nonblocking_event.read().has_value();
                                                 }),
            "event_fd wrapper registration should succeed");

    require(nonblocking_event.notify(), "event_fd wrapper notify should succeed");
    require(event_handler.poll(20ms), "event_fd wrapper should dispatch");
    require(event_was_drained, "event_fd wrapper should drain before invoking callback");

    fd_event_handler timer_handler;
    timer_fd timer;
    bool timer_was_drained = false;

    require(timer_handler.register_event_handler(
                &timer,
                [&](const fd_event_handler::event_list& events) {
                    require(events & poll_events::read, "timer_fd wrapper callback should receive read event");
                    pollfd poll_fd{timer.get_raw_fd(), POLLIN, 0};
                    require(::poll(&poll_fd, 1, 0) == 0, "timer_fd should be drained before invoking callback");
                    timer_was_drained = true;
                }),
            "timer_fd wrapper registration should succeed");

    require(timer.set_timeout(5ms), "timer should arm");
    require(timer_handler.poll(50ms), "timer_fd wrapper should dispatch");
    require(timer_was_drained, "timer_fd wrapper callback should run");
}

class sync_client : public fd_event_sync_interface {
public:
    explicit sync_client(event_fd& signal) : m_signal(signal) {
    }

    int get_poll_fd() override {
        return m_signal.get_raw_fd();
    }

    sync_status sync() override {
        ++sync_calls;
        m_signal.read();
        return sync_status::ok;
    }

    int sync_calls{0};

private:
    event_fd& m_signal;
};

void test_sync_interface_registration_calls_sync() {
    fd_event_handler handler;
    event_fd signal;
    sync_client client(signal);

    require(handler.register_event_handler(&client), "sync interface registration should succeed");
    require(signal.notify(), "sync client event should notify");
    require(handler.poll(20ms), "sync client event should dispatch");
    require_eq(client.sync_calls, 1, "sync client should call sync once");
}

void test_nested_handler_dispatches_child_events_and_actions() {
    fd_event_handler parent;
    fd_event_handler child;
    event_fd child_signal;
    int child_events = 0;
    int child_actions = 0;

    require(child.register_event_handler(&child_signal, [&] { ++child_events; }),
            "child event registration should succeed");
    child.add_action([&] { ++child_actions; });

    require(parent.register_event_handler(&child), "nested handler registration should succeed");
    require(child_signal.notify(), "child event should notify");
    require(parent.poll(20ms), "parent should dispatch nested child handler");
    require_eq(child_events, 1, "nested child event should dispatch");
    require_eq(child_actions, 1, "nested child action should run");
}

void test_add_action_wakes_handler_and_run_actions_executes_queue() {
    fd_event_handler handler;
    int actions = 0;

    handler.add_action([&] { ++actions; });
    require(handler.poll(20ms), "add_action should wake fd_event_handler");
    require_eq(actions, 0, "poll should not run actions directly");
    handler.run_actions();
    require_eq(actions, 1, "run_actions should execute queued action");
}

void test_modify_event_handler_read_write_interest() {
    fd_event_handler handler;
    auto sockets = make_socket_pair();
    int read_calls = 0;
    int write_calls = 0;

    require(handler.register_event_handler(
                sockets.first.get(),
                [&](const fd_event_handler::event_list& events) {
                    if (events & poll_events::read) {
                        drain_byte(sockets.first.get());
                        ++read_calls;
                    }
                    if (events & poll_events::write) {
                        ++write_calls;
                    }
                },
                poll_events::read),
            "modify target registration should succeed");

    require(handler.modify_event_handler(sockets.first.get(), poll_events::write, event_modification::add),
            "modify add write should succeed");
    require(handler.poll(20ms), "added write interest should dispatch");
    require_eq(write_calls, 1, "write interest should be observed after add");

    require(handler.modify_event_handler(sockets.first.get(), poll_events::write, event_modification::remove),
            "modify remove write should succeed");
    require(!handler.poll(1ms), "removed write interest should not dispatch");

    write_byte(sockets.second.get());
    require(handler.poll(20ms), "read interest should still dispatch");
    require_eq(read_calls, 1, "read interest should remain after remove write");

    require(handler.modify_event_handler(sockets.first.get(), poll_events::write, event_modification::replace),
            "modify replace with write should succeed");
    require(handler.poll(20ms), "replacement write interest should dispatch");
    require_eq(write_calls, 2, "write interest should be observed after replace");

    write_byte(sockets.second.get());
    require(handler.poll(20ms), "write-only replacement interest should still dispatch");
    require_eq(read_calls, 1, "read interest should be gone after replace");
}

class register_client : public fd_event_register_interface {
public:
    explicit register_client(event_fd& signal) : m_signal(signal) {
    }

    bool register_events(fd_event_handler& handler) override {
        ++register_calls;
        return handler.register_event_handler(&m_signal, [&] { ++events; });
    }

    bool unregister_events(fd_event_handler& handler) override {
        ++unregister_calls;
        return handler.unregister_event_handler(&m_signal);
    }

    int register_calls{0};
    int unregister_calls{0};
    int events{0};

private:
    event_fd& m_signal;
};

void test_registration_failures_unregister_remove_and_stale_remove() {
    fd_event_handler handler;
    event_fd signal;

    require(!handler.register_event_handler(
                -1, [](const fd_event_handler::event_list&) {}, poll_events::read),
            "invalid raw fd registration should fail");
    require(
        !handler.register_event_handler(signal.get_raw_fd(), fd_event_handler::event_handler_type{}, poll_events::read),
        "empty raw callback registration should fail");
    require(!handler.register_event_handler(static_cast<event_fd_base*>(nullptr), [] {}),
            "null event_fd registration should fail");
    require(!handler.register_event_handler(static_cast<timer_fd*>(nullptr), [] {}),
            "null timer_fd registration should fail");
    require(!handler.register_event_handler(static_cast<fd_event_sync_interface*>(nullptr)),
            "null sync interface registration should fail");
    require(!handler.register_event_handler(static_cast<fd_event_register_interface*>(nullptr)),
            "null register interface registration should fail");
    require(!handler.register_event_handler(static_cast<fd_event_handler*>(nullptr)),
            "null nested handler registration should fail");
    require(!handler.register_event_handler(&handler), "self nested handler registration should fail");

    require(handler.register_event_handler(&signal, [] {}), "first event_fd registration should succeed");
    require(!handler.register_event_handler(&signal, [] {}), "duplicate event_fd registration should fail");
    require(handler.unregister_event_handler(&signal), "event_fd unregister should succeed");
    require(!handler.unregister_event_handler(-1), "invalid raw fd unregister should fail");
    require(!handler.unregister_event_handler(static_cast<event_fd_base*>(nullptr)),
            "null event_fd unregister should fail");
    require(!handler.remove_event_handler(-1), "invalid raw fd remove should fail");
    require(handler.remove_event_handler(signal.get_raw_fd()),
            "stale remove currently reports success after unregister");

    event_fd client_signal;
    register_client client(client_signal);
    require(handler.register_event_handler(&client), "register interface registration should delegate");
    require_eq(client.register_calls, 1, "register interface should call register_events");
    require(client_signal.notify(), "register interface event should notify");
    require(handler.poll(20ms), "register interface event should dispatch");
    require_eq(client.events, 1, "register interface event should run");
    require(handler.unregister_event_handler(&client), "register interface unregister should delegate");
    require_eq(client.unregister_calls, 1, "register interface should call unregister_events");
}

void test_mutation_unregister_current_fd_during_callback() {
    fd_event_handler handler;
    event_fd signal;
    int calls = 0;

    require(handler.register_event_handler(&signal,
                                           [&] {
                                               ++calls;
                                               require(handler.unregister_event_handler(&signal),
                                                       "callback should unregister current fd");
                                           }),
            "current mutation registration should succeed");

    require(signal.notify(), "current mutation event should notify");
    require(handler.poll(20ms), "current mutation event should dispatch");
    require_eq(calls, 1, "current callback should run once");
    require(signal.notify(), "current mutation second notify should succeed");
    require(!handler.poll(1ms), "unregistered current fd should not dispatch again");
}

void test_mutation_unregister_another_ready_fd_during_callback() {
    fd_event_handler handler;
    event_fd first;
    event_fd second;
    std::vector<std::string> calls;

    require(handler.register_event_handler(&first,
                                           [&] {
                                               calls.push_back("first");
                                               require(handler.unregister_event_handler(&second),
                                                       "callback should unregister another ready fd");
                                           }),
            "first mutation registration should succeed");
    require(handler.register_event_handler(&second, [&] { calls.push_back("second"); }),
            "second mutation registration should succeed");

    require(first.notify(), "first event should notify");
    require(second.notify(), "second event should notify");
    require(handler.poll(20ms), "mutating callback should dispatch");
    require_eq(calls, std::vector<std::string>{"first"}, "stale ready fd should be skipped after unregister");
    require_eq(handler.last_ready_count(), std::size_t{2},
               "ready count should retain epoll_wait result before mutation");
}

void test_mutation_register_new_fd_during_callback() {
    fd_event_handler handler;
    event_fd first;
    event_fd second;
    std::vector<std::string> calls;

    require(handler.register_event_handler(
                &first,
                [&] {
                    calls.push_back("first");
                    require(handler.register_event_handler(&second, [&] { calls.push_back("second"); }),
                            "callback should register new fd");
                    require(second.notify(), "newly registered fd should notify during callback");
                }),
            "register-new mutation registration should succeed");

    require(first.notify(), "first event should notify");
    require(handler.poll(20ms), "first mutation event should dispatch");
    require_eq(calls, std::vector<std::string>{"first"}, "new fd should not dispatch in same poll snapshot");
    require(handler.poll(20ms), "new fd should dispatch on following poll");
    require_eq(calls, std::vector<std::string>{"first", "second"}, "new fd should dispatch on next poll");
}

void test_mutation_modify_interest_during_callback() {
    fd_event_handler handler;
    auto sockets = make_socket_pair();
    int calls = 0;

    require(handler.register_event_handler(
                sockets.first.get(),
                [&](const fd_event_handler::event_list& events) {
                    ++calls;
                    if (events & poll_events::write) {
                        require(handler.modify_event_handler(sockets.first.get(), poll_events::read,
                                                             event_modification::replace),
                                "callback should replace interest with read");
                    }
                    if (events & poll_events::read) {
                        drain_byte(sockets.first.get());
                    }
                },
                poll_events::write),
            "modify-interest mutation registration should succeed");

    require(handler.poll(20ms), "initial write interest should dispatch");
    require_eq(calls, 1, "write callback should run once");
    write_byte(sockets.second.get());
    require(handler.poll(20ms), "modified read interest should dispatch on following poll");
    require_eq(calls, 2, "read callback should run after interest modification");
}

void test_reentrant_poll_uses_independent_ready_snapshots() {
    fd_event_handler handler;
    event_fd outer;
    event_fd nested_one;
    event_fd nested_two;
    event_fd nested_three;
    std::vector<std::string> calls;

    require(handler.register_event_handler(&outer,
                                           [&] {
                                               calls.push_back("outer");
                                               require(nested_one.notify(), "first nested event should notify");
                                               require(nested_two.notify(), "second nested event should notify");
                                               require(nested_three.notify(), "third nested event should notify");
                                               require(handler.poll(20ms),
                                                       "reentrant poll should dispatch nested events");
                                           }),
            "outer reentrant registration should succeed");
    require(handler.register_event_handler(&nested_one, [&] { calls.push_back("nested_one"); }),
            "first nested registration should succeed");
    require(handler.register_event_handler(&nested_two, [&] { calls.push_back("nested_two"); }),
            "second nested registration should succeed");
    require(handler.register_event_handler(&nested_three, [&] { calls.push_back("nested_three"); }),
            "third nested registration should succeed");

    require(outer.notify(), "outer event should notify");
    require(handler.poll(20ms), "outer poll should dispatch");
    require_eq(calls, std::vector<std::string>{"outer", "nested_one", "nested_two", "nested_three"},
               "reentrant poll should not corrupt outer iteration state");
}

int main() {
    test_basic_raw_fd_registration_dispatch_and_timeout();
    test_read_write_and_hangup_event_masks();
    test_wrapped_event_fd_and_timer_fd_drain_before_callback();
    test_sync_interface_registration_calls_sync();
    test_nested_handler_dispatches_child_events_and_actions();
    test_add_action_wakes_handler_and_run_actions_executes_queue();
    test_modify_event_handler_read_write_interest();
    test_registration_failures_unregister_remove_and_stale_remove();
    test_mutation_unregister_current_fd_during_callback();
    test_mutation_unregister_another_ready_fd_during_callback();
    test_mutation_register_new_fd_during_callback();
    test_mutation_modify_interest_during_callback();
    test_reentrant_poll_uses_independent_ready_snapshots();

    return 0;
}
