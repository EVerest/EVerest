// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
//
// Teardown tests for everest::lib::API::CommCheckHandlerBase.
//
// NOTE: intentionally pins upstream everest_api_types CommCheckHandlerBase
// teardown behavior here (sole repo coverage for it) — do not relocate.
//
// Before the fix, ~CommCheckHandlerBase only cleared the check_active flag —
// it never stopped or joined the heartbeat_handler thread. Destruction of a
// joinable std::thread member invokes std::terminate; even before that, the
// still-running heartbeat action observed half-destroyed enclosing state on
// every iteration. The fix adds a public stop_heartbeat() and joins both
// internal threads from the destructor. These tests exercise both paths.

#include <everest_api_types/utilities/CommCheckHandler.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

using everest::lib::API::CommCheckHandlerBase;

namespace {

// CommCheckHandlerBase's constructor stores error_state_monitor / error_factory
// / error_manager by reference to a shared_ptr. The heartbeat path never
// dereferences them, so empty shared_ptrs are safe as long as the test never
// triggers the comm-check error machinery (i.e. never calls start() or
// raise_error / clear_error / set_error / check_error).
struct ErrorMachineryStub {
    std::shared_ptr<Everest::error::ErrorStateMonitor> monitor;
    std::shared_ptr<Everest::error::ErrorFactory> factory;
    std::shared_ptr<Everest::error::ErrorManagerImpl> manager;
};

std::unique_ptr<CommCheckHandlerBase> make_handler(ErrorMachineryStub& stub) {
    return std::make_unique<CommCheckHandlerBase>("teardown_test/CommunicationFault", "default", stub.monitor,
                                                  stub.factory, stub.manager);
}

} // namespace

TEST_CASE("stop_heartbeat joins the heartbeat thread without dangling", "[evsim][comm_check]") {
    ErrorMachineryStub stub;
    auto handler = make_handler(stub);

    std::atomic<int> ticks{0};
    REQUIRE(handler->heartbeat(1, [&ticks]() {
        ++ticks;
        return true;
    }));

    // Let the heartbeat thread iterate at least once so we know it is alive.
    while (ticks.load() == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // stop_heartbeat must return only after the thread has been joined.
    handler->stop_heartbeat();

    const int snapshot = ticks.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    REQUIRE(ticks.load() == snapshot); // thread no longer running
}

TEST_CASE("stop_heartbeat is idempotent and safe to call twice", "[evsim][comm_check]") {
    ErrorMachineryStub stub;
    auto handler = make_handler(stub);

    REQUIRE(handler->heartbeat(1, []() { return true; }));
    handler->stop_heartbeat();
    REQUIRE_NOTHROW(handler->stop_heartbeat());
}

TEST_CASE("stop_heartbeat is a no-op when no heartbeat was ever started", "[evsim][comm_check]") {
    ErrorMachineryStub stub;
    auto handler = make_handler(stub);

    REQUIRE_NOTHROW(handler->stop_heartbeat());
}

TEST_CASE("destructor stops + joins an active heartbeat thread", "[evsim][comm_check]") {
    // Repeated ctor/dtor cycles with an active heartbeat. Before the fix this
    // either hung in the dtor (heartbeat thread spinning on a never-cleared
    // flag) or terminated the process (joinable std::thread member destructed
    // without join). After the fix all iterations complete cleanly.
    for (int i = 0; i < 100; ++i) {
        ErrorMachineryStub stub;
        auto handler = make_handler(stub);
        REQUIRE(handler->heartbeat(1, []() { return true; }));
        handler.reset();
    }
}

TEST_CASE("destructor honors heartbeat ftor that returns false", "[evsim][comm_check]") {
    // When the heartbeat action returns false the thread is expected to exit
    // on its own. The destructor must still complete (joining a thread that
    // already ran to completion is well-defined for std::thread::join).
    ErrorMachineryStub stub;
    auto handler = make_handler(stub);

    std::atomic<int> calls{0};
    REQUIRE(handler->heartbeat(1, [&calls]() {
        ++calls;
        return false;
    }));

    // Wait for the action to be invoked once and exit on its own.
    while (calls.load() == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    REQUIRE_NOTHROW(handler.reset());
}
