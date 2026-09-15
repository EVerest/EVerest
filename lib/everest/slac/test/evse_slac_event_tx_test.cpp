// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 - 2026 Pionix GmbH and Contributors to EVerest
//
// SlacEvent::send on a socket that never accepts a frame: after TX_FAILURE_THRESHOLD consecutive
// rejections the error callback must report the link as failing, once, so the module raises a
// CommunicationFault instead of waiting forever for confirmations.
//
// The report must never come out of send() itself. send() runs inside the consumer's state machine,
// which the module dispatches with its lifecycle monitor held, and the consumer's error handler takes
// that monitor: a synchronous report deadlocks the loop thread. So the report goes through the action
// queue of the event handler SlacEvent is registered with and arrives on the next loop turn.
//
// Two rigs. A nonexistent interface never opens, so every send is rejected and the socket error path
// runs alongside; that covers the deferral. The reopen and the per-connection record need a socket
// that is up and merely stops draining: `lo` with the loop not pumped, which requires CAP_NET_RAW.
// That test skips without it; run it as root, in CI's container, or locally under
// `unshare -rn sh -c 'ip link set lo up && <test>'`.
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include <everest/io/event/fd_event_handler.hpp>
#include <everest/slac/HomeplugMessage.hpp>
#include <everest/slac/slac_event.hpp>

using namespace everest::lib::slac;
using namespace std::chrono_literals;

namespace {

bool assert_true(bool cond, const char* test_name, const char* details) {
    if (not cond) {
        std::printf("[%s] FAIL: %s\n", test_name, details);
        return false;
    }
    return true;
}

// The interface that does not exist: the socket never opens, so tx() rejects every frame. The open
// failure is reported through the socket error path once the loop turns; the transmit report must
// arrive on top of that, so the tests count reports naming the transmit failure.
constexpr char const* NO_SUCH_INTERFACE = "slactestnone0";

struct Rig {
    SlacEvent io;
    everest::lib::io::event::fd_event_handler handler;
    std::vector<std::pair<bool, std::string>> reports;
    unsigned ready_count{0};
    messages::HomeplugMessage msg;

    explicit Rig(char const* if_name) : io(if_name) {
        io.set_error_callback(
            [this](bool on_error, std::string const& detail) { reports.emplace_back(on_error, detail); });
        io.set_ready_callback([this]() { ++ready_count; });
        messages::cm_set_key_req req{};
        msg.setup_payload(&req, sizeof(req), defs::MMTYPE_CM_SET_KEY | defs::MMTYPE_MODE_REQ, defs::MMV::AV_1_1);
    }
    ~Rig() {
        io.unregister_events(handler);
    }
    // One turn of the module's loop: `poll(); run_actions();`.
    void pump(std::chrono::milliseconds budget) {
        auto const deadline = std::chrono::steady_clock::now() + budget;
        while (std::chrono::steady_clock::now() < deadline) {
            handler.poll(1ms);
            handler.run_actions();
        }
    }
    // Pump until \p done or the budget is spent; true if \p done became true.
    template <typename Done> bool pump_until(std::chrono::milliseconds budget, Done&& done) {
        auto const deadline = std::chrono::steady_clock::now() + budget;
        while (std::chrono::steady_clock::now() < deadline) {
            if (done()) {
                return true;
            }
            handler.poll(1ms);
            handler.run_actions();
        }
        return done();
    }
    std::size_t tx_fault_reports() const {
        std::size_t n = 0;
        for (auto const& [on_error, detail] : reports) {
            if (on_error and detail.find("transmission failing") != std::string::npos) {
                ++n;
            }
        }
        return n;
    }
    // Sends until the socket rejects: the transmit buffer is full. Bounded so a socket that
    // drains without being polled cannot loop forever.
    std::size_t fill_transmit_buffer() {
        std::size_t accepted = 0;
        while (accepted < 100000 and io.send(msg)) {
            ++accepted;
        }
        return accepted;
    }
};

bool test_persistent_tx_failure_is_reported_once_from_the_loop() {
    const char* test_name = "persistent_tx_failure_is_reported_once_from_the_loop";
    Rig rig(NO_SUCH_INTERFACE);
    if (!assert_true(rig.io.register_events(rig.handler), test_name, "could not register with the handler")) {
        return false;
    }
    for (unsigned i = 0; i < SlacEvent::TX_FAILURE_THRESHOLD - 1; ++i) {
        if (!assert_true(not rig.io.send(rig.msg), test_name, "send unexpectedly succeeded on a closed socket")) {
            return false;
        }
    }
    if (!assert_true(rig.reports.empty(), test_name, "reported before the threshold was reached")) {
        return false;
    }
    (void)rig.io.send(rig.msg);
    // The threshold send returns without having called back: the report is queued, not made.
    if (!assert_true(rig.reports.empty(), test_name, "send() reported synchronously from inside the call")) {
        return false;
    }
    rig.pump(50ms);
    if (!assert_true(rig.tx_fault_reports() == 1, test_name,
                     "the loop turn did not deliver exactly one transmit-failure report")) {
        return false;
    }
    for (unsigned i = 0; i < 2 * SlacEvent::TX_FAILURE_THRESHOLD; ++i) {
        (void)rig.io.send(rig.msg);
    }
    rig.pump(20ms);
    return assert_true(rig.tx_fault_reports() == 1, test_name, "reported the same failing link more than once");
}

bool test_rejections_without_a_handler_are_only_counted() {
    const char* test_name = "rejections_without_a_handler_are_only_counted";
    Rig rig(NO_SUCH_INTERFACE);
    for (unsigned i = 0; i < 2 * SlacEvent::TX_FAILURE_THRESHOLD; ++i) {
        (void)rig.io.send(rig.msg);
    }
    if (!assert_true(rig.reports.empty(), test_name, "reported although nothing can run the report")) {
        return false;
    }
    // Nothing was marked as reported either: once a handler is there, the next rejection reports.
    if (!assert_true(rig.io.register_events(rig.handler), test_name, "could not register with the handler")) {
        return false;
    }
    (void)rig.io.send(rig.msg);
    rig.pump(50ms);
    return assert_true(rig.tx_fault_reports() == 1, test_name,
                       "the fault counted without a handler was never reported");
}

// The full recovery chain on the real client: a socket that is up but stops draining rejects once
// its transmit buffer is full; the deferred report reopens it, the new connection's ready callback
// starts the record over, and the socket accepts again. A second stall on the new connection is
// reported again.
bool test_transmit_fault_reopens_the_socket_and_the_record_starts_over() {
    const char* test_name = "transmit_fault_reopens_the_socket_and_the_record_starts_over";
    Rig rig("lo");
    if (!assert_true(rig.io.register_events(rig.handler), test_name, "could not register with the handler")) {
        return false;
    }
    (void)rig.pump_until(500ms, [&] { return rig.ready_count > 0 or not rig.reports.empty(); });
    if (rig.ready_count == 0) {
        std::printf("[SKIP] %s: raw socket on lo not available (%s); needs CAP_NET_RAW, e.g. "
                    "`unshare -rn sh -c 'ip link set lo up && %s'`\n",
                    test_name, rig.reports.empty() ? "no ready within 500 ms" : rig.reports.front().second.c_str(),
                    "evse_slac_event_tx_test");
        return true;
    }

    auto const accepted = rig.fill_transmit_buffer();
    if (!assert_true(accepted > 0 and accepted < 100000, test_name, "the transmit buffer did not fill up")) {
        return false;
    }
    // The rejection that filled it counted as one; TX_FAILURE_THRESHOLD - 1 more reach the threshold.
    for (unsigned i = 0; i < SlacEvent::TX_FAILURE_THRESHOLD - 1; ++i) {
        (void)rig.io.send(rig.msg);
    }
    if (!assert_true(rig.reports.empty(), test_name, "reported synchronously or before the threshold")) {
        return false;
    }
    if (!assert_true(rig.pump_until(1000ms, [&] { return rig.ready_count >= 2; }), test_name,
                     "the socket was not reopened after the transmit fault (no second ready)")) {
        return false;
    }
    if (!assert_true(rig.tx_fault_reports() == 1, test_name, "the transmit fault was not reported exactly once")) {
        return false;
    }
    if (!assert_true(rig.io.send(rig.msg), test_name, "the reopened socket does not accept a frame")) {
        return false;
    }

    // Same stall on the new connection: the record started over, so it is reported again.
    (void)rig.fill_transmit_buffer();
    for (unsigned i = 0; i < SlacEvent::TX_FAILURE_THRESHOLD - 1; ++i) {
        (void)rig.io.send(rig.msg);
    }
    if (!assert_true(rig.pump_until(1000ms, [&] { return rig.ready_count >= 3; }), test_name,
                     "the second transmit fault did not reopen the socket")) {
        return false;
    }
    return assert_true(rig.tx_fault_reports() == 2, test_name, "the second connection's fault was not reported");
}

} // namespace

int main() {
    int failed = 0;
    struct {
        const char* name;
        bool (*fn)();
    } const tests[] = {
        {"persistent_tx_failure_is_reported_once_from_the_loop",
         test_persistent_tx_failure_is_reported_once_from_the_loop},
        {"rejections_without_a_handler_are_only_counted", test_rejections_without_a_handler_are_only_counted},
        {"transmit_fault_reopens_the_socket_and_the_record_starts_over",
         test_transmit_fault_reopens_the_socket_and_the_record_starts_over},
    };
    for (auto const& t : tests) {
        if (t.fn()) {
            std::printf("[PASS] %s\n", t.name);
        } else {
            std::printf("[FAIL] %s\n", t.name);
            ++failed;
        }
    }
    return failed == 0 ? 0 : 1;
}
