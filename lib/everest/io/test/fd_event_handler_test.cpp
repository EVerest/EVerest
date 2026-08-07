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

/// Sequence numbers for the two events that must stay ordered: a callback finishing, and the
/// closure it was executing being destroyed.
struct closure_lifetime {
    int next{0};
    /// The probe belonging to the closure copy the running callback was invoked on.
    void const* running{nullptr};
    int callback_finished{-1};
    int closure_destroyed{-1};
};

/// Captured by value, so every closure copy carries one. Only the copy the running callback
/// claimed as its own records a sequence number, which is what distinguishes the executing
/// closure from the copy the handler map owns.
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

/// Lives on the stack of the callback invocation, not in the closure, so it can still record the
/// callback's completion after the closure has been destroyed.
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

// Removal reports success when either the epoll set or the handler map gave up an entry,
// and failure only when neither did.
TEST(fd_event_handler_test, unregister_reports_false_for_an_unknown_fd) {
    fd_event_handler handler;

    auto probe = std::make_unique<event_fd>();
    auto const raw = probe->get_raw_fd();
    ASSERT_TRUE(handler.register_event_handler(
        raw, [](fd_event_handler::event_list const&) {}, poll_events::read));

    probe.reset();
    // Closing the descriptor dropped it from the epoll set, so EPOLL_CTL_DEL now fails
    // EBADF. Erasing the handler map entry is still a removal.
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

// Unregistering the running descriptor erases the map entry that owns the std::function
// currently executing, so a dispatch loop holding a reference into the map destroys the
// callable under its own feet.
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

    // Both sequence numbers are recorded from a live object, so the ordering holds regardless of
    // what the allocator does with the freed closure.
    ASSERT_NE(order.callback_finished, -1);
    ASSERT_NE(order.closure_destroyed, -1);
    EXPECT_LT(order.callback_finished, order.closure_destroyed);
}

// A client recording a raw handler pointer would dereference a dangling one here. The record is a
// weak reference to a handler owned liveness block, so it goes inert with the handler.
TEST(fd_event_handler_test, sync_client_outliving_its_handler_is_inert) {
    fd_event_handler unrelated;
    minimal_sync_client client;

    {
        fd_event_handler handler;
        ASSERT_TRUE(handler.register_event_handler(&client));
        ASSERT_TRUE(handler.is_registered(client.get_poll_fd()));
    }

    // The record names the dead handler, not this one.
    EXPECT_FALSE(client.unregister_events(unrelated));
    EXPECT_FALSE(unrelated.is_registered(client.get_poll_fd()));

    // A record whose handler is gone is expired, not merely non-null, so it does not block a new
    // registration the way a stale raw pointer would.
    EXPECT_TRUE(unrelated.register_event_handler(&client));
    EXPECT_TRUE(unrelated.is_registered(client.get_poll_fd()));

    // Destroying client now must not reach the dead handler.
}

// Declaration order is not load bearing: neither destruction order may crash, and the surviving
// party must still report the truth about the registration.
TEST(fd_event_handler_test, either_destruction_order_of_handler_and_client_is_safe) {
    {
        // Handler destroyed first.
        minimal_sync_client client;
        fd_event_handler handler;
        ASSERT_TRUE(handler.register_event_handler(&client));
        ASSERT_TRUE(handler.is_registered(client.get_poll_fd()));
    }

    {
        // Client destroyed first.
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

// charge_bridge cycles register and unregister at runtime on reconnect, so dropping a record must
// leave the client able to take a new one.
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

// One record per client. A second registration would leave the first unrecorded and therefore
// unremovable.
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

// The constructor registers the handler's own action event on itself, so a handler always holds a
// registration against its own liveness block. Building and tearing that down must be clean.
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

// The handler captures a pointer to the registered object in its dispatch lambda, so moving one
// would leave the handler reading the descriptor at the old address.
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

// Recording must not disturb dispatch: the handler still reads the timer to acknowledge the
// expiry before calling the callback.
TEST(fd_event_handler_test, registered_timer_fires_and_is_acknowledged) {
    fd_event_handler handler;
    timer_fd timer;

    int calls = 0;
    ASSERT_TRUE(handler.register_event_handler(&timer, [&]() { ++calls; }));

    timer.set_single_shot(true);
    ASSERT_TRUE(timer.set_timeout_ms(1));

    EXPECT_TRUE(handler.poll(100ms));
    EXPECT_EQ(calls, 1);

    // Acknowledged, so nothing is pending any more.
    EXPECT_FALSE(handler.poll(100ms));
}

// Nothing removed a timer's map entry when the timer died, so the entry outlived it and blocked a
// later registration of the recycled descriptor number.
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

// Same defect for an event_fd, which is the descriptor every fd_event_client uses to arm write
// events on its socket.
TEST(fd_event_handler_test, destroying_registered_event_drops_its_map_entry) {
    fd_event_handler handler;
    int raw = -1;

    {
        event_fd event;
        raw = event.get_raw_fd();
        ASSERT_NE(raw, -1);
        ASSERT_TRUE(handler.register_event_handler(&event, []() {}));
        ASSERT_TRUE(handler.is_registered(raw));
    }

    EXPECT_FALSE(handler.is_registered(raw));
}

// Declaration order is not load bearing. The record is a weak reference to a handler owned block,
// so a timer that outlives its handler has no pointer left to dereference, and the surviving
// handler still reports the truth about the registration.
TEST(fd_event_handler_test, timer_outliving_its_handler_is_inert) {
    fd_event_handler unrelated;
    timer_fd timer;
    auto const raw = timer.get_raw_fd();

    {
        fd_event_handler handler;
        ASSERT_TRUE(handler.register_event_handler(&timer, []() {}));
        ASSERT_TRUE(handler.is_registered(raw));
    }

    // The record names the dead handler, not this one.
    EXPECT_FALSE(unrelated.unregister_event_handler(&timer));
    EXPECT_FALSE(unrelated.is_registered(raw));

    // A record whose handler is gone must not block a new registration.
    EXPECT_TRUE(unrelated.register_event_handler(&timer, []() {}));
    EXPECT_TRUE(unrelated.is_registered(raw));

    // Destroying timer now must reach unrelated and not the dead handler.
}

// Same for an event_fd. MQTTAbstractionImpl declares its two event_fd members before the handler
// they are registered on, so this is the order the framework actually tears down in.
TEST(fd_event_handler_test, event_outliving_its_handler_is_inert) {
    fd_event_handler unrelated;
    event_fd event;
    auto const raw = event.get_raw_fd();

    {
        fd_event_handler handler;
        ASSERT_TRUE(handler.register_event_handler(&event, []() {}));
        ASSERT_TRUE(handler.is_registered(raw));
    }

    EXPECT_FALSE(unrelated.unregister_event_handler(&event));
    EXPECT_FALSE(unrelated.is_registered(raw));

    EXPECT_TRUE(unrelated.register_event_handler(&event, []() {}));
    EXPECT_TRUE(unrelated.is_registered(raw));
}

// register_event_handler(fd_event_handler*) installs a lambda capturing the nested handler and
// nothing dropped its entry when it died, so the entry outlived the object it calls into.
TEST(fd_event_handler_test, destroying_nested_handler_drops_its_map_entry) {
    fd_event_handler outer;
    int raw = -1;

    {
        fd_event_handler nested;
        raw = nested.get_poll_fd();
        ASSERT_NE(raw, -1);
        ASSERT_TRUE(outer.register_event_handler(&nested));
        ASSERT_TRUE(outer.is_registered(raw));
    }

    EXPECT_FALSE(outer.is_registered(raw));
}

// The other order: the nested handler drops its own outward registration in its destructor, and
// that record is a weak reference, so an outer handler that died first is not touched.
TEST(fd_event_handler_test, nested_handler_outliving_its_outer_handler_is_inert) {
    fd_event_handler unrelated;
    fd_event_handler nested;
    auto const raw = nested.get_poll_fd();

    {
        fd_event_handler outer;
        ASSERT_TRUE(outer.register_event_handler(&nested));
        ASSERT_TRUE(outer.is_registered(raw));
    }

    EXPECT_FALSE(unrelated.unregister_event_handler(&nested));
    EXPECT_TRUE(unrelated.register_event_handler(&nested));
    EXPECT_TRUE(unrelated.is_registered(raw));
}

// Behavior change. Two handlers holding one descriptor in two epoll sets used to be legal, but
// only one registration can be recorded, so the second would be unremovable.
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

// Same narrowing for an event_fd and for a nested handler.
TEST(fd_event_handler_test, register_event_handler_refuses_a_second_event_registration) {
    fd_event_handler first;
    fd_event_handler second;
    event_fd event;
    fd_event_handler nested;

    ASSERT_TRUE(first.register_event_handler(&event, []() {}));
    EXPECT_FALSE(second.register_event_handler(&event, []() {}));
    EXPECT_FALSE(second.is_registered(event.get_raw_fd()));

    ASSERT_TRUE(first.register_event_handler(&nested));
    EXPECT_FALSE(second.register_event_handler(&nested));
    EXPECT_FALSE(second.is_registered(nested.get_poll_fd()));
}

// CanBus::close_device and mqtt_client cycle a timer registration at runtime, so dropping the
// record must leave the timer able to take a new one.
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

    // Nothing left to remove.
    EXPECT_FALSE(handler.unregister_event_handler(&timer));
}

// generic_fd_event_client_impl::reset_client drops its io event by descriptor and the next connect
// registers the object again. A record compared only against itself would call that a second
// registration and refuse it, leaving the client unable to arm write events.
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

// Removal reports whether a registration with this handler was dropped, so a handler that never
// held the timer must not claim to have removed it, nor clear the owner's record.
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
