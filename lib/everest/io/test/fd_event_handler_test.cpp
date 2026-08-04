// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include <everest/io/event/event_fd.hpp>
#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/event/fd_event_sync_interface.hpp>

#include <chrono>
#include <cstdlib>
#include <memory>
#include <type_traits>

#include <unistd.h>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

using everest::lib::io::event::event_fd;
using everest::lib::io::event::fd_event_handler;
using everest::lib::io::event::fd_event_sync_interface;
using everest::lib::io::event::poll_events;
using everest::lib::io::event::sync_status;

namespace {

class minimal_sync_client : public fd_event_sync_interface {
public:
    int get_poll_fd() override {
        return m_event.get_raw_fd();
    }

    sync_status sync() override {
        m_event.read();
        return sync_status::ok;
    }

private:
    event_fd m_event;
};

/// Regular files are not pollable, so epoll_ctl rejects them with EPERM.
int open_unpollable_fd() {
    char path[] = "/tmp/everest_io_fd_event_handler_XXXXXX";
    auto const raw = ::mkstemp(path);
    if (raw != -1) {
        ::unlink(path);
    }
    return raw;
}

} // namespace

// Copying a registered client would duplicate the recorded handler and descriptor, so
// two objects would own one registration and the first destroyed would remove the live one.
TEST(fd_event_handler_test, sync_interface_cannot_be_copied_or_moved) {
    EXPECT_FALSE(std::is_copy_constructible_v<minimal_sync_client>);
    EXPECT_FALSE(std::is_copy_assignable_v<minimal_sync_client>);
    EXPECT_FALSE(std::is_move_constructible_v<minimal_sync_client>);
    EXPECT_FALSE(std::is_move_assignable_v<minimal_sync_client>);
}

// A sync client cannot self-unregister, so its handler map entry outlives it and
// blocks a later registration of the recycled descriptor number.
TEST(fd_event_handler_test, destroying_sync_client_drops_its_map_entry) {
    fd_event_handler handler;
    int raw = -1;

    {
        minimal_sync_client client;
        raw = client.get_poll_fd();
        ASSERT_NE(raw, -1);
        ASSERT_TRUE(handler.register_event_handler(&client));
        ASSERT_TRUE(handler.is_registered(raw));
    }

    EXPECT_FALSE(handler.is_registered(raw));
}

// EPOLL_CTL_DEL on a closed descriptor fails with EBADF. With the map entry
// already gone there is nothing left to remove, so the call must report failure.
TEST(fd_event_handler_test, unregister_reports_false_when_epoll_del_fails) {
    fd_event_handler handler;

    auto probe = std::make_unique<event_fd>();
    auto const raw = probe->get_raw_fd();
    ASSERT_TRUE(handler.register_event_handler(
        raw, [](fd_event_handler::event_list const&) {}, poll_events::read));

    probe.reset();
    handler.unregister_event_handler(raw);
    ASSERT_FALSE(handler.is_registered(raw));

    EXPECT_FALSE(handler.unregister_event_handler(raw));
}

TEST(fd_event_handler_test, register_reports_false_when_epoll_add_fails) {
    fd_event_handler handler;

    auto const raw = open_unpollable_fd();
    ASSERT_NE(raw, -1);

    EXPECT_FALSE(handler.register_event_handler(
        raw, [](fd_event_handler::event_list const&) {}, poll_events::read));
    EXPECT_FALSE(handler.is_registered(raw));

    ::close(raw);
}

// A handler that drops one registration and takes another one holds the number of
// registrations constant, so the epoll_wait batch of the running poll must survive it.
TEST(fd_event_handler_test, poll_keeps_the_batch_across_a_registration_exchange) {
    fd_event_handler handler;

    event_fd churn;
    event_fd doomed;
    event_fd replacement;
    event_fd victim;

    auto const doomed_raw = doomed.get_raw_fd();

    int victim_calls = 0;

    ASSERT_TRUE(handler.register_event_handler(&churn, [&](fd_event_handler::event_list const&) {
        handler.unregister_event_handler(doomed_raw);
        handler.register_event_handler(
            replacement.get_raw_fd(), [](fd_event_handler::event_list const&) {}, poll_events::read);
    }));
    ASSERT_TRUE(handler.register_event_handler(&doomed, [](fd_event_handler::event_list const&) {}));
    ASSERT_TRUE(handler.register_event_handler(&victim, [&](fd_event_handler::event_list const&) { ++victim_calls; }));

    // Every registered descriptor must be ready, including the internal action event, so that
    // the batch covers the whole poll array and the exchange can disturb an entry the dispatch
    // loop has not read yet.
    handler.add_action([]() {});
    churn.notify();
    doomed.notify();
    victim.notify();

    handler.poll(100ms);

    EXPECT_EQ(victim_calls, 1);
}

// A handler that unregisters a descriptor reported ready in the same epoll_wait
// batch erases the map entry a later iteration of the dispatch loop still looks up.
TEST(fd_event_handler_test, poll_survives_unregistration_from_within_a_handler) {
    fd_event_handler handler;

    event_fd first;
    event_fd second;
    event_fd idle;

    auto const first_raw = first.get_raw_fd();
    auto const second_raw = second.get_raw_fd();

    int first_calls = 0;
    int second_calls = 0;

    ASSERT_TRUE(handler.register_event_handler(
        first_raw,
        [&](fd_event_handler::event_list const&) {
            ++first_calls;
            first.read();
            handler.unregister_event_handler(second_raw);
        },
        poll_events::read));
    ASSERT_TRUE(handler.register_event_handler(
        second_raw,
        [&](fd_event_handler::event_list const&) {
            ++second_calls;
            second.read();
            handler.unregister_event_handler(first_raw);
        },
        poll_events::read));
    ASSERT_TRUE(handler.register_event_handler(
        idle.get_raw_fd(), [](fd_event_handler::event_list const&) {}, poll_events::read));

    first.notify();
    second.notify();

    EXPECT_NO_THROW(handler.poll(100ms));

    // epoll reports the batch in an unspecified order, so whichever handler runs
    // first unregisters the other one, which must then not be dispatched.
    EXPECT_EQ(first_calls + second_calls, 1);
}
