// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
//
// Sanity tests for the flush-after-disarm semantics relied on by
// EvSimRuntime::disarm_tick and the state-timer cancel lambda. A non-blocking timer_fd retains a pending fire until
// read(); set_timeout_ms(0) cancels future fires but does not consume an
// already-pending one. The runtime must call read() after disarm to avoid
// dispatching a stale handler against an unrelated state.
//
// disarm_and_flush (anonymous-namespace internal to EvSimRuntime.cpp, so
// only reachable here through the timer_fd primitive it wraps) classifies
// the post-disarm read() by errno: EAGAIN/EWOULDBLOCK is the expected
// "nothing pending" outcome; any other errno is a genuinely broken fd and
// is logged at error rather than looking identical to an empty flush. The
// errno==EAGAIN leg of that contract is pinned by the cases below; the
// log-on-other-errno leg and the EvSimRuntime composition (on_wake total
// exception isolation, ~EvSimRuntime token-detach ordering, real
// build_peer_actions) are exercised by the SIL smokes — EvSimRuntime is
// not unit-constructible without a live framework EvSimulator&, the same
// constraint documented in RuntimeTickTimerTest.cpp.

#include <catch2/catch_test_macros.hpp>
#include <everest/io/event/event_fd.hpp>
#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/event/timer_fd.hpp>

#include <cerrno>
#include <chrono>
#include <thread>

using everest::lib::io::event::event_fd;
using everest::lib::io::event::fd_event_handler;
using everest::lib::io::event::timer_fd;

namespace {

void wait_for_fire(timer_fd& fd, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        // poll-by-read would consume; we just spin until the deadline.
        // Callers verify with a read() afterward.
        (void)fd;
    }
}

} // namespace

TEST_CASE("timer_fd reports a pending fire after the timeout elapses", "[evsim][timer_fd]") {
    timer_fd fd;
    REQUIRE(fd.valid());

    REQUIRE(fd.set_timeout_ms(1));
    wait_for_fire(fd, std::chrono::milliseconds(20));

    const int n = fd.read();
    REQUIRE(n > 0); // ::read returns bytes; the underlying counter is >= 1
}

TEST_CASE("timer_fd read after disarm returns EAGAIN when nothing is pending", "[evsim][timer_fd]") {
    timer_fd fd;
    REQUIRE(fd.valid());

    // No timeout ever armed.
    errno = 0;
    REQUIRE(fd.read() < 0);
    REQUIRE(errno == EAGAIN);
}

TEST_CASE("disarm-then-read consumes a stale pending fire", "[evsim][timer_fd]") {
    timer_fd fd;
    REQUIRE(fd.valid());

    REQUIRE(fd.set_timeout_ms(1));
    wait_for_fire(fd, std::chrono::milliseconds(20));

    // Mirror the disarm path: cancel future fires, then flush.
    REQUIRE(fd.set_timeout_ms(0));
    errno = 0;
    const int flushed = fd.read();
    // The flush may either return the pending count (>0) or, if the kernel
    // already absorbed the cancelled fire, EAGAIN. Either outcome means the
    // fd is now empty.
    if (flushed < 0) {
        REQUIRE(errno == EAGAIN);
    }

    // A second read must always be EAGAIN — there is nothing left.
    errno = 0;
    REQUIRE(fd.read() < 0);
    REQUIRE(errno == EAGAIN);
}

TEST_CASE("fd_event_handler rejects duplicate registration of the same fd", "[evsim][fd_event_handler]") {
    // Documents the contract relied on by EvSimRuntime::run: register_event_handler
    // returns false when the underlying epoll_ctl(ADD) fails. Re-adding an fd that
    // is already in the epoll set yields EEXIST and must surface as a false return,
    // so the caller can abort startup instead of running with a dead event source.
    fd_event_handler loop;
    event_fd ev;
    REQUIRE(ev.valid());

    REQUIRE(loop.register_event_handler(&ev, [](auto&&) {}));
    REQUIRE_FALSE(loop.register_event_handler(&ev, [](auto&&) {}));
}
