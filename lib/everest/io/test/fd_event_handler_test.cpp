// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include <everest/io/event/event_fd.hpp>
#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/event/fd_event_sync_interface.hpp>
#include <everest/io/event/timer_fd.hpp>

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
using everest::lib::io::event::timer_fd;

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

struct closure_lifetime {
    int next{0};
    void const* running{nullptr};
    int callback_finished{-1};
    int closure_destroyed{-1};
};

/// std::function copies the closure, so only the probe the running callback claimed may record.
class destruction_probe {
public:
    explicit destruction_probe(closure_lifetime& order) : m_order(&order) {
    }

    destruction_probe(destruction_probe const&) = default;
    destruction_probe& operator=(destruction_probe const&) = default;

    ~destruction_probe() {
        if (m_order->running == this) {
            m_order->closure_destroyed = ++m_order->next;
        }
    }

private:
    closure_lifetime* m_order;
};

/// On the callback's stack, not in the closure, so it records completion after the closure dies.
class completion_marker {
public:
    explicit completion_marker(closure_lifetime& order) : m_order(&order) {
    }

    completion_marker(completion_marker const&) = delete;
    completion_marker& operator=(completion_marker const&) = delete;

    ~completion_marker() {
        m_order->callback_finished = ++m_order->next;
    }

private:
    closure_lifetime* m_order;
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

TEST(fd_event_handler_test, sync_interface_cannot_be_copied_or_moved) {
    EXPECT_FALSE(std::is_copy_constructible_v<minimal_sync_client>);
    EXPECT_FALSE(std::is_copy_assignable_v<minimal_sync_client>);
    EXPECT_FALSE(std::is_move_constructible_v<minimal_sync_client>);
    EXPECT_FALSE(std::is_move_assignable_v<minimal_sync_client>);
}

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

TEST(fd_event_handler_test, unregister_reports_false_for_an_unknown_fd) {
    fd_event_handler handler;

    auto probe = std::make_unique<event_fd>();
    auto const raw = probe->get_raw_fd();
    ASSERT_TRUE(handler.register_event_handler(
        raw, [](fd_event_handler::event_list const&) {}, poll_events::read));

    probe.reset();
    EXPECT_TRUE(handler.unregister_event_handler(raw));
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

    // Every descriptor must be ready, the action event included, so the batch spans the whole array.
    handler.add_action([]() {});
    churn.notify();
    doomed.notify();
    victim.notify();

    handler.poll(100ms);

    EXPECT_EQ(victim_calls, 1);
}

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

    // Batch order is unspecified, so whichever handler runs first unregisters the other.
    EXPECT_EQ(first_calls + second_calls, 1);
}

// Unregistering the running descriptor destroys the std::function the dispatch loop is executing.
TEST(fd_event_handler_test, callback_may_unregister_its_own_fd) {
    fd_event_handler handler;

    event_fd self;
    auto const raw = self.get_raw_fd();

    int calls = 0;
    closure_lifetime order;

    ASSERT_TRUE(handler.register_event_handler(
        raw,
        [&, probe = destruction_probe(order)](fd_event_handler::event_list const&) {
            completion_marker done{order};
            order.running = &probe;
            ++calls;
            self.read();
            handler.unregister_event_handler(raw);
        },
        poll_events::read));

    self.notify();

    EXPECT_NO_THROW(handler.poll(100ms));

    EXPECT_EQ(calls, 1);
    EXPECT_FALSE(handler.is_registered(raw));

    ASSERT_NE(order.callback_finished, -1);
    ASSERT_NE(order.closure_destroyed, -1);
    EXPECT_LT(order.callback_finished, order.closure_destroyed);
}

TEST(fd_event_handler_test, sync_client_outliving_its_handler_is_inert) {
    fd_event_handler unrelated;
    minimal_sync_client client;

    {
        fd_event_handler handler;
        ASSERT_TRUE(handler.register_event_handler(&client));
        ASSERT_TRUE(handler.is_registered(client.get_poll_fd()));
    }

    EXPECT_FALSE(client.unregister_events(unrelated));
    EXPECT_FALSE(unrelated.is_registered(client.get_poll_fd()));

    EXPECT_TRUE(unrelated.register_event_handler(&client));
    EXPECT_TRUE(unrelated.is_registered(client.get_poll_fd()));
}

TEST(fd_event_handler_test, either_destruction_order_of_handler_and_client_is_safe) {
    {
        minimal_sync_client client;
        fd_event_handler handler;
        ASSERT_TRUE(handler.register_event_handler(&client));
        ASSERT_TRUE(handler.is_registered(client.get_poll_fd()));
    }

    {
        fd_event_handler handler;
        int raw = -1;
        {
            minimal_sync_client client;
            raw = client.get_poll_fd();
            ASSERT_TRUE(handler.register_event_handler(&client));
            ASSERT_TRUE(handler.is_registered(raw));
        }
        EXPECT_FALSE(handler.is_registered(raw));
    }
}

TEST(fd_event_handler_test, sync_client_can_register_again_after_unregistering) {
    fd_event_handler handler;
    minimal_sync_client client;
    auto const raw = client.get_poll_fd();

    ASSERT_TRUE(handler.register_event_handler(&client));
    ASSERT_TRUE(handler.is_registered(raw));

    ASSERT_TRUE(handler.unregister_event_handler(&client));
    ASSERT_FALSE(handler.is_registered(raw));

    ASSERT_TRUE(handler.register_event_handler(&client));
    EXPECT_TRUE(handler.is_registered(raw));

    EXPECT_TRUE(handler.unregister_event_handler(&client));
    EXPECT_FALSE(handler.is_registered(raw));
}

TEST(fd_event_handler_test, register_events_refuses_a_second_registration) {
    fd_event_handler first;
    fd_event_handler second;
    minimal_sync_client client;
    auto const raw = client.get_poll_fd();

    ASSERT_TRUE(first.register_event_handler(&client));

    EXPECT_FALSE(first.register_event_handler(&client));
    EXPECT_FALSE(second.register_event_handler(&client));
    EXPECT_FALSE(second.is_registered(raw));
    EXPECT_TRUE(first.is_registered(raw));
}

TEST(fd_event_handler_test, handler_self_registration_survives_destruction) {
    {
        fd_event_handler handler;
        handler.add_action([]() {});
        EXPECT_TRUE(handler.poll(100ms));
        handler.run_actions();
    }

    // A fresh handler recycles the descriptor numbers of the destroyed one and must start empty.
    fd_event_handler reused;
    EXPECT_FALSE(reused.poll(100ms));
}

TEST(fd_event_handler_test, timer_and_event_cannot_be_copied_or_moved) {
    EXPECT_FALSE(std::is_copy_constructible_v<timer_fd>);
    EXPECT_FALSE(std::is_copy_assignable_v<timer_fd>);
    EXPECT_FALSE(std::is_move_constructible_v<timer_fd>);
    EXPECT_FALSE(std::is_move_assignable_v<timer_fd>);

    EXPECT_FALSE(std::is_copy_constructible_v<event_fd>);
    EXPECT_FALSE(std::is_copy_assignable_v<event_fd>);
    EXPECT_FALSE(std::is_move_constructible_v<event_fd>);
    EXPECT_FALSE(std::is_move_assignable_v<event_fd>);
}

TEST(fd_event_handler_test, registered_timer_fires_and_is_acknowledged) {
    fd_event_handler handler;
    timer_fd timer;

    int calls = 0;
    ASSERT_TRUE(handler.register_event_handler(&timer, [&]() { ++calls; }));

    timer.set_single_shot(true);
    ASSERT_TRUE(timer.set_timeout_ms(1));

    EXPECT_TRUE(handler.poll(100ms));
    EXPECT_EQ(calls, 1);

    EXPECT_FALSE(handler.poll(100ms));
}

TEST(fd_event_handler_test, destroying_registered_timer_drops_its_map_entry) {
    fd_event_handler handler;
    int raw = -1;

    {
        timer_fd timer;
        raw = timer.get_raw_fd();
        ASSERT_NE(raw, -1);
        ASSERT_TRUE(handler.register_event_handler(&timer, []() {}));
        ASSERT_TRUE(handler.is_registered(raw));
    }

    EXPECT_FALSE(handler.is_registered(raw));
}

TEST(fd_event_handler_test, timer_outliving_its_handler_is_inert) {
    fd_event_handler unrelated;
    timer_fd timer;
    auto const raw = timer.get_raw_fd();

    {
        fd_event_handler handler;
        ASSERT_TRUE(handler.register_event_handler(&timer, []() {}));
        ASSERT_TRUE(handler.is_registered(raw));
    }

    EXPECT_FALSE(unrelated.unregister_event_handler(&timer));
    EXPECT_FALSE(unrelated.is_registered(raw));

    EXPECT_TRUE(unrelated.register_event_handler(&timer, []() {}));
    EXPECT_TRUE(unrelated.is_registered(raw));
}

// Behavior change: one descriptor in two epoll sets used to be legal, the second is now refused.
TEST(fd_event_handler_test, register_event_handler_refuses_a_second_timer_registration) {
    fd_event_handler first;
    fd_event_handler second;
    timer_fd timer;
    auto const raw = timer.get_raw_fd();

    ASSERT_TRUE(first.register_event_handler(&timer, []() {}));

    EXPECT_FALSE(second.register_event_handler(&timer, []() {}));
    EXPECT_FALSE(second.is_registered(raw));
    EXPECT_TRUE(first.is_registered(raw));

    // Twice on the same handler was already refused by the descriptor guard.
    EXPECT_FALSE(first.register_event_handler(&timer, []() {}));
    EXPECT_TRUE(first.is_registered(raw));
}

TEST(fd_event_handler_test, timer_can_register_again_after_unregistering) {
    fd_event_handler handler;
    timer_fd timer;
    auto const raw = timer.get_raw_fd();

    ASSERT_TRUE(handler.register_event_handler(&timer, []() {}));
    ASSERT_TRUE(handler.unregister_event_handler(&timer));
    ASSERT_FALSE(handler.is_registered(raw));

    ASSERT_TRUE(handler.register_event_handler(&timer, []() {}));
    EXPECT_TRUE(handler.is_registered(raw));

    EXPECT_TRUE(handler.unregister_event_handler(&timer));
    EXPECT_FALSE(handler.is_registered(raw));

    EXPECT_FALSE(handler.unregister_event_handler(&timer));
}

// event_fd records nothing, so this pins the descriptor guard alone.
TEST(fd_event_handler_test, registering_again_after_removal_by_descriptor_succeeds) {
    fd_event_handler handler;
    event_fd event;
    auto const raw = event.get_raw_fd();

    ASSERT_TRUE(handler.register_event_handler(&event, []() {}));
    ASSERT_TRUE(handler.unregister_event_handler(raw));
    ASSERT_FALSE(handler.is_registered(raw));

    EXPECT_TRUE(handler.register_event_handler(&event, []() {}));
    EXPECT_TRUE(handler.is_registered(raw));
}

// A record is honored only while the handler's map still holds the recorded descriptor.
TEST(fd_event_handler_test, registering_a_timer_again_after_removal_by_descriptor_succeeds) {
    fd_event_handler handler;
    timer_fd timer;
    auto const raw = timer.get_raw_fd();

    ASSERT_TRUE(handler.register_event_handler(&timer, []() {}));
    ASSERT_TRUE(handler.remove_event_handler(raw));
    ASSERT_FALSE(handler.is_registered(raw));

    EXPECT_TRUE(handler.register_event_handler(&timer, []() {}));
    EXPECT_TRUE(handler.is_registered(raw));
}

TEST(fd_event_handler_test, registering_a_sync_client_again_after_removal_by_descriptor_succeeds) {
    fd_event_handler handler;
    minimal_sync_client client;
    auto const raw = client.get_poll_fd();

    ASSERT_TRUE(handler.register_event_handler(&client));
    ASSERT_TRUE(handler.remove_event_handler(raw));
    ASSERT_FALSE(handler.is_registered(raw));

    EXPECT_TRUE(handler.register_event_handler(&client));
    EXPECT_TRUE(handler.is_registered(raw));
}

TEST(fd_event_handler_test, unregistering_a_timer_from_another_handler_reports_false) {
    fd_event_handler owner;
    fd_event_handler other;
    timer_fd timer;
    auto const raw = timer.get_raw_fd();

    ASSERT_TRUE(owner.register_event_handler(&timer, []() {}));

    EXPECT_FALSE(other.unregister_event_handler(&timer));
    EXPECT_TRUE(owner.is_registered(raw));

    EXPECT_TRUE(owner.unregister_event_handler(&timer));
    EXPECT_FALSE(owner.is_registered(raw));
}
