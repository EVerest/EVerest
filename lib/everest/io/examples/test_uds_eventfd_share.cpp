// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
//
// Sharing an eventfd over a unix domain socket, then waking each other through it.
//
// Two ends, A and B, on one event loop. Each creates an eventfd and sends it to the other over a
// SOCK_SEQPACKET connection (send_fd). Each registers the eventfd it received with the loop. From
// then on the socket is idle: A notifies the eventfd it owns and B wakes, B notifies its own and A
// wakes. The same works across processes; the socket carries the descriptor, the kernel does the
// rest.

#include <everest/io/event/event_fd.hpp>
#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/uds/uds_seqpacket_client.hpp>
#include <everest/io/uds/uds_seqpacket_listener.hpp>
#include <everest/io/uds/uds_seqpacket_peer.hpp>
#include <everest/io/uds/uds_utils.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <unistd.h>

using namespace std::chrono_literals;
namespace event = everest::lib::io::event;
namespace uds = everest::lib::io::uds;

namespace {

constexpr int rounds = 5;

// One end: owns an eventfd to wake the other, holds the eventfd received from the other.
struct end {
    std::string name;
    event::fd_event_handler& loop;
    bool starter;                  // kicks off and answers one wake less
    event::event_fd wake_other;    // sent to the other end, notified to wake it
    uds::shared_fd woken_by_other; // received from the other end, registered with the loop
    int wakes{0};

    end(std::string n, event::fd_event_handler& l, bool starts) : name(std::move(n)), loop(l), starter(starts) {
    }

    // Called with the payload that carries the other end's eventfd.
    void take_eventfd(uds::uds_payload const& payload) {
        if (not payload.has_fds()) {
            return;
        }
        woken_by_other = payload.fds.front();
        loop.register_event_handler(
            static_cast<int>(*woken_by_other),
            [this](auto const&) {
                std::uint64_t count = 0;
                (void)::read(static_cast<int>(*woken_by_other), &count, sizeof(count));
                ++wakes;
                std::printf("%s: woken (%d)\n", name.c_str(), wakes);
                // The responder answers every wake; the starter, having kicked off, stops one early so
                // both end up woken exactly 'rounds' times.
                if (wakes < rounds or not starter) {
                    wake_other.notify();
                }
            },
            event::poll_events::read);
        std::printf("%s: registered the other end's eventfd\n", name.c_str());
    }

    ~end() {
        if (woken_by_other) {
            loop.unregister_event_handler(static_cast<int>(*woken_by_other));
        }
    }
};

} // namespace

int main() {
    const std::string socket_name = "uds_eventfd_share_" + std::to_string(::getpid());
    event::fd_event_handler loop;

    end a("A", loop, /*starts=*/true);
    end b("B", loop, /*starts=*/false);

    // A listens, B connects. Each side sends its eventfd once the connection is up.
    uds::uds_seqpacket_listener listener(socket_name);
    std::unique_ptr<uds::uds_seqpacket_peer> a_connection;
    listener.set_accept_callback([&](std::unique_ptr<uds::uds_seqpacket_peer> peer) {
        peer->set_rx_handler([&](uds::uds_payload const& payload, auto&) { a.take_eventfd(payload); });
        peer->set_on_ready_action([&]() { uds::send_fd(*a_connection, a.wake_other.get_raw_fd(), "A's eventfd"); });
        loop.register_event_handler(peer.get());
        a_connection = std::move(peer);
    });
    loop.register_event_handler(&listener);

    uds::uds_seqpacket_client b_connection(socket_name);
    b_connection.set_rx_handler([&](uds::uds_payload const& payload, auto&) { b.take_eventfd(payload); });
    b_connection.set_on_ready_action([&]() { uds::send_fd(b_connection, b.wake_other.get_raw_fd(), "B's eventfd"); });
    loop.register_event_handler(&b_connection);

    // Once both ends hold each other's eventfd, A starts the ping pong.
    bool started = false;
    auto const deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline and (a.wakes < rounds or b.wakes < rounds)) {
        loop.poll(50ms);
        loop.run_actions();
        if (not started and a.woken_by_other and b.woken_by_other) {
            started = true;
            std::printf("A: wakes B\n");
            a.wake_other.notify();
        }
    }

    loop.unregister_event_handler(&b_connection);
    if (a_connection) {
        loop.unregister_event_handler(a_connection.get());
    }
    loop.unregister_event_handler(&listener);

    std::printf("done: A woken %d times, B woken %d times\n", a.wakes, b.wakes);
    return (a.wakes == rounds and b.wakes == rounds) ? 0 : 1;
}
