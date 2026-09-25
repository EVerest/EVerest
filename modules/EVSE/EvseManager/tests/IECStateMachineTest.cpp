// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "evse_board_supportIntfStub.hpp"
#include <EventQueue.hpp>
#include <IECStateMachine.hpp>
#include <backtrace.hpp>
#include <chrono>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <thread>

namespace {

using BspEvent = types::board_support_common::BspEvent;
using Event = types::board_support_common::Event;
using Reason = types::evse_board_support::Reason;

using namespace std::chrono_literals;

struct BspStub : public module::stub::ModuleAdapterStub {
    typedef Result (BspStub::*bsp_fn)(Parameters p);
    ValueCallback event_cb;
    std::map<std::string, bsp_fn> _bsp;

    BspStub() : module::stub::ModuleAdapterStub() {
        _bsp["allow_power_on"] = &BspStub::call_allow_power_on;
        _bsp["enable"] = &BspStub::call_enable;
        _bsp["cp_state_X1"] = &BspStub::call_cp_state_X1;
        _bsp["cp_state_F"] = &BspStub::call_cp_state_F;
        _bsp["pwm_on"] = &BspStub::call_pwm_on;
    }

    // ------------------------------------------------------------------------
    // test interface (calls from BSP)

    void raise_event(Event event) {
        if (event_cb != nullptr) {
            BspEvent bsp_event;
            bsp_event.event = event;
            event_cb(bsp_event);
        }
    };

    // ------------------------------------------------------------------------
    // BSP calls

    virtual Result call_allow_power_on(Parameters p) {
        std::cout << "call_allow_power_on(" << p << ")" << std::endl;
        return std::nullopt;
    }

    virtual Result call_enable(Parameters p) {
        std::cout << "call_enable(" << p << ")" << std::endl;
        return std::nullopt;
    }

    virtual Result call_cp_state_X1(Parameters p) {
        std::cout << "call_cp_state_X1(" << p << ")" << std::endl;
        return std::nullopt;
    }

    virtual Result call_cp_state_F(Parameters p) {
        std::cout << "call_cp_state_F(" << p << ")" << std::endl;
        return std::nullopt;
    }

    virtual Result call_pwm_on(Parameters p) {
        std::cout << "call_pwm_on(" << p << ")" << std::endl;
        return std::nullopt;
    }

    // ------------------------------------------------------------------------
    // internal interface
    virtual void subscribe_fn(const Requirement&, const std::string& fn, ValueCallback cb) override {
        std::printf("subscribe_fn(%s)\n", fn.c_str());
        event_cb = cb;
    }

    virtual Result call_fn(const Requirement& req, const std::string& fn, Parameters p) override {
        if (auto itt = _bsp.find(fn); itt == _bsp.end()) {
            std::cout << "<missing> call_fn(" << fn << "," << p << ")" << std::endl;
            return std::nullopt;
        } else {
            return std::invoke(itt->second, this, p);
        }
    }
};

TEST(IECStateMachine, init) {
    module::stub::ModuleAdapterStub module_adapter = module::stub::ModuleAdapterStub();
    std::unique_ptr<evse_board_supportIntf> bsp_if =
        std::make_unique<module::stub::evse_board_supportIntfStub>(module_adapter);
    module::IECStateMachine state_machine(std::move(bsp_if), true, false, false, 0);
}

#if 0
// test to demonstrate the output from backtrace

struct BspStubTimeout : public BspStub {
    Result call_allow_power_on(Parameters p) {
        std::cout << "call_allow_power_on(" << p << ")" << std::endl;
        std::cout << "call_allow_power_on: sleep (" << std::this_thread::get_id() << ")" << std::endl;
        std::this_thread::sleep_for(200s);
        std::cout << "call_allow_power_on: finished" << std::endl;
        return std::nullopt;
    }
};

TEST(IECStateMachine, init_subscribe) {
#ifdef EVEREST_USE_BACKTRACES
    Everest::install_backtrace_handler();
#endif

    BspStubTimeout bsp;
    std::unique_ptr<evse_board_supportIntf> bsp_if = std::make_unique<module::stub::evse_board_supportIntfStub>(bsp);
    module::IECStateMachine state_machine(std::move(bsp_if), true);

    state_machine.enable(true);
    state_machine.allow_power_on(true, Reason::FullPowerCharging);
    bsp.raise_event(Event::A);
    bsp.raise_event(Event::B);
    bsp.raise_event(Event::PowerOn);
    bsp.raise_event(Event::C);
    std::cout << "main: sleep (" << std::this_thread::get_id() << ")" << std::endl;
    std::this_thread::sleep_for(300s);
}
#endif

struct BspStubDeadlock : public BspStub {
    std::vector<std::chrono::time_point<std::chrono::steady_clock>> call_cp_state_X1_times;
    std::vector<std::chrono::time_point<std::chrono::steady_clock>> call_allow_power_on_times;
    int count = 0;
    enum class events_t : std::uint8_t {
        call_allow_power_on,
        call_enable,
        call_cp_state_X1,
        call_pwm_on,
        signal_lock,
        sleeping,
    };
    module::EventQueue<events_t> events;

    std::string to_string(module::EventQueue<events_t>::events_t e) {
        std::string result;
        for (const auto& i : e) {
            result += std::to_string(static_cast<std::uint8_t>(i));
            result += " ";
        }
        return result;
    }

    bool contains(module::EventQueue<events_t>::events_t e, events_t event) {
        for (const auto& i : e) {
            if (i == event) {
                return true;
            }
        }
        return false;
    }

    auto wait() {
        return events.wait();
    }

    virtual Result call_allow_power_on(Parameters p) {
        std::cout << "call_allow_power_on(" << p << ")" << std::endl;
        call_allow_power_on_times.push_back(std::chrono::steady_clock::now());
        events.push(events_t::call_allow_power_on);
        return std::nullopt;
    }

    virtual Result call_enable(Parameters p) {
        std::cout << "call_enable(" << p << ")" << std::endl;
        events.push(events_t::call_enable);
        return std::nullopt;
    }

    virtual Result call_cp_state_X1(Parameters p) {
        std::cout << "call_cp_state_X1(" << p << ")" << std::endl;
        call_cp_state_X1_times.push_back(std::chrono::steady_clock::now());
        if (count == 2) {
            std::cout << "sleeping ..." << std::endl;
            events.push(events_t::sleeping);
            std::this_thread::sleep_for(7s);
        }
        count++;
        events.push(events_t::call_cp_state_X1);
        return std::nullopt;
    }

    virtual Result call_pwm_on(Parameters p) {
        std::cout << "call_pwm_on(" << p << ")" << std::endl;
        events.push(events_t::call_pwm_on);
        return std::nullopt;
    }

    auto elapsed() const {
        auto last_call_cp_state_X1_time = call_cp_state_X1_times.back();
        auto last_call_allow_power_on_time = call_allow_power_on_times.back();
        return std::chrono::duration_cast<std::chrono::milliseconds>(last_call_allow_power_on_time -
                                                                     last_call_cp_state_X1_time)
            .count();
    }
};

TEST(IECStateMachine, deadlock_test) {
    GTEST_SKIP() << "relies on thread timing so occasionally fails";

    /*
     * a deadlock was caused by timeout_state_c1 timing out and
     * trying to raise an event while state_machine() was running
     * and wanting to stop the timer
     * 61851-1 state C starts the timer (from x2)
     *
     * check the timer runs for 6 seconds
     */
#ifdef EVEREST_USE_BACKTRACES
    Everest::install_backtrace_handler();
#endif

    BspStubDeadlock bsp;
    std::unique_ptr<evse_board_supportIntf> bsp_if = std::make_unique<module::stub::evse_board_supportIntfStub>(bsp);
    module::IECStateMachine state_machine(std::move(bsp_if), true, false, false, 0);

    std::uint8_t signal_lock_count = 0;

    state_machine.signal_lock.connect([&signal_lock_count, &bsp]() {
        signal_lock_count++;
        std::cout << "signal_lock" << std::endl;
        // bsp.events.push(BspStubDeadlock::events_t::signal_lock);
    });

    state_machine.enable(true);
    auto actions = bsp.wait();
    ASSERT_EQ(actions.size(), 1);
    ASSERT_EQ(actions[0], BspStubDeadlock::events_t::call_enable);

    bsp.raise_event(Event::A);
    actions = bsp.wait();
    ASSERT_TRUE(bsp.contains(actions, BspStubDeadlock::events_t::call_cp_state_X1));

    bsp.raise_event(Event::B);
    actions = bsp.wait();
    // std::cout << bsp.to_string(actions) << std::endl;
    // call_allow_power_on and/or signal_lock
    // ASSERT_TRUE(bsp.contains(actions, BspStubDeadlock::events_t::call_allow_power_on));

    state_machine.set_pwm(0.5);
    actions = bsp.wait();
    // std::cout << bsp.to_string(actions) << std::endl;
    ASSERT_TRUE(bsp.contains(actions, BspStubDeadlock::events_t::call_pwm_on));

    state_machine.allow_power_on(true, types::evse_board_support::Reason::FullPowerCharging);
    std::this_thread::sleep_for(1s);
    // actions = bsp.wait();
    // ASSERT_TRUE(bsp.contains(actions, BspStubDeadlock::events_t::signal_lock));

    bsp.raise_event(Event::C);
    actions = bsp.wait();
    // std::cout << bsp.to_string(actions) << std::endl;
    ASSERT_TRUE(bsp.contains(actions, BspStubDeadlock::events_t::call_allow_power_on));

    state_machine.set_cp_state_X1();
    // expect state_machine to run once and then again when the
    // timer expires

    std::this_thread::sleep_for(8s);
    auto elapsed = bsp.elapsed();
    std::cout << "Elapsed: " << elapsed << std::endl;
    EXPECT_GT(elapsed, 6000);
    EXPECT_LT(elapsed, 6500);
}

TEST(IECStateMachine, deadlock_fix) {
    GTEST_SKIP() << "relies on thread timing so occasionally fails";

    /*
     * a deadlock was caused by timeout_state_c1 timing out and
     * trying to raise an event while state_machine() was running
     * and wanting to stop the timer
     * 61851-1 state C starts the timer (from X2)
     */
#ifdef EVEREST_USE_BACKTRACES
    Everest::install_backtrace_handler();
#endif

    BspStubDeadlock bsp;
    std::unique_ptr<evse_board_supportIntf> bsp_if = std::make_unique<module::stub::evse_board_supportIntfStub>(bsp);
    module::IECStateMachine state_machine(std::move(bsp_if), true, false, false, 0);

    std::uint8_t signal_lock_count = 0;

    state_machine.signal_lock.connect([&signal_lock_count, &bsp]() {
        signal_lock_count++;
        std::cout << "signal_lock" << std::endl;
        // bsp.events.push(BspStubDeadlock::events_t::signal_lock);
    });

    state_machine.enable(true);
    auto actions = bsp.wait();
    ASSERT_EQ(actions.size(), 1);
    ASSERT_EQ(actions[0], BspStubDeadlock::events_t::call_enable);

    bsp.raise_event(Event::A);
    actions = bsp.wait();
    // std::cout << bsp.to_string(actions) << std::endl;
    ASSERT_TRUE(bsp.contains(actions, BspStubDeadlock::events_t::call_cp_state_X1));

    bsp.raise_event(Event::B);
    actions = bsp.wait();
    // call_allow_power_on and/or signal_lock
    // ASSERT_TRUE(bsp.contains(actions, BspStubDeadlock::events_t::call_allow_power_on));

    state_machine.set_pwm(0.5);
    actions = bsp.wait();
    ASSERT_TRUE(bsp.contains(actions, BspStubDeadlock::events_t::call_pwm_on));

    state_machine.allow_power_on(true, types::evse_board_support::Reason::FullPowerCharging);
    std::this_thread::sleep_for(1s);

    bsp.raise_event(Event::C);
    actions = bsp.wait();
    ASSERT_TRUE(bsp.contains(actions, BspStubDeadlock::events_t::call_allow_power_on));

    state_machine.set_cp_state_X1();
    // expect state_machine to run once and then again when the
    // timer expires

    actions = bsp.wait();
    ASSERT_TRUE(bsp.contains(actions, BspStubDeadlock::events_t::call_cp_state_X1));

    // this would previously trigger the deadlock
    bsp.raise_event(Event::A);
    actions = bsp.wait();
    // std::cout << bsp.to_string(actions) << std::endl;
    ASSERT_TRUE(bsp.contains(actions, BspStubDeadlock::events_t::sleeping));

    std::this_thread::sleep_for(10s);
    // if there is a deadlock the test won't finish
}

// ---------------------------------------------------------------------------
// Tests for auth-aware connector locking
//
// When use_authorized is true, the connector should only lock in State B/C
// once the session is authorized. This prevents trapping cables before
// authorization while still keeping them locked during BMS pauses.

struct ConnectorLockTest : public testing::Test {
    BspStub bsp;
    std::unique_ptr<evse_board_supportIntf> bsp_if;
    int lock_count{0};
    int unlock_count{0};

    void reset_counts() {
        lock_count = 0;
        unlock_count = 0;
    }

    std::unique_ptr<module::IECStateMachine> create_state_machine(bool use_authorized, bool keep_cable_locked = false,
                                                                  int keep_cable_locked_lock_delay_ms = 0) {
        bsp_if = std::make_unique<module::stub::evse_board_supportIntfStub>(bsp);
        auto sm = std::make_unique<module::IECStateMachine>(bsp_if, true, use_authorized, keep_cable_locked,
                                                            keep_cable_locked_lock_delay_ms);
        sm->signal_lock.connect([this]() { lock_count++; });
        sm->signal_unlock.connect([this]() { unlock_count++; });
        sm->enable(true);
        return sm;
    }

    void set_pp(module::IECStateMachine& sm, types::board_support_common::Ampacity ampacity) {
        sm.set_pp_ampacity(types::board_support_common::ProximityPilot{ampacity});
    }

    // Drive the state machine to State B via A→B (simulates plug-in)
    void plug_in() {
        bsp.raise_event(Event::A);
        bsp.raise_event(Event::B);
    }

    void plug_out() {
        bsp.raise_event(Event::A);
    }
};

TEST_F(ConnectorLockTest, use_authorized_false_always_locks) {
    // Default behavior: use_authorized=false locks immediately in B
    auto sm = create_state_machine(false);

    plug_in();

    EXPECT_GT(lock_count, 0) << "connector should lock in State B when use_authorized is false";
}

TEST_F(ConnectorLockTest, use_authorized_true_no_auth_stays_unlocked) {
    // With use_authorized=true and no authorization,
    // State B should NOT lock the connector (cable free to unplug)
    auto sm = create_state_machine(true);

    plug_in();

    EXPECT_EQ(lock_count, 0) << "connector should not lock without authorization";

    // Re-enter B to re-trigger the state machine evaluation
    bsp.raise_event(Event::B);

    EXPECT_EQ(lock_count, 0) << "connector should not lock in State B without authorization";
}

TEST_F(ConnectorLockTest, set_authorized_in_state_b_locks) {
    // Car plugs in → State B → no lock → authorize → lock engages
    auto sm = create_state_machine(true);

    plug_in();

    EXPECT_EQ(lock_count, 0) << "connector should not lock without authorization";

    sm->set_authorized(true);

    EXPECT_GT(lock_count, 0) << "connector should lock when authorized in State B";
}

TEST_F(ConnectorLockTest, set_authorized_first) {
    // Car plugs in → State B → no lock → authorize → lock engages
    auto sm = create_state_machine(true);

    sm->set_authorized(true);

    EXPECT_EQ(lock_count, 0) << "connector should not lock without authorization";

    plug_in();

    EXPECT_GT(lock_count, 0) << "connector should lock when authorized in State B";
}

TEST_F(ConnectorLockTest, set_deauthorized_in_state_b_unlocks) {
    // After authorization, deauthorizing in State B should unlock
    auto sm = create_state_machine(true);

    sm->set_authorized(true);
    plug_in();
    reset_counts();

    sm->set_authorized(false);

    EXPECT_GT(unlock_count, 0) << "connector should unlock when deauthorized in State B";
}

TEST_F(ConnectorLockTest, bms_pause_stays_locked) {
    // Authorized session: C→B (BMS pause) should keep the connector locked
    auto sm = create_state_machine(true);

    plug_in();
    sm->set_authorized(true);

    // Transition to State C (car requests power)
    bsp.raise_event(Event::C);
    reset_counts();

    // BMS pause: car goes back to B
    bsp.raise_event(Event::B);

    // Lock should re-engage (or stay engaged), no unlock should fire
    EXPECT_EQ(unlock_count, 0) << "connector should not unlock during BMS pause (C->B) while authorized";
    EXPECT_EQ(lock_count, 0) << "connector already locked";
}

TEST_F(ConnectorLockTest, set_authorized_outside_state_b_no_lock_change) {
    // Setting authorized while in State A should not trigger lock/unlock
    auto sm = create_state_machine(true);

    bsp.raise_event(Event::A);
    reset_counts();

    sm->set_authorized(true);

    EXPECT_EQ(lock_count, 0) << "set_authorized in State A should not trigger lock";
    EXPECT_EQ(unlock_count, 0) << "set_authorized in State A should not trigger unlock";
}

TEST_F(ConnectorLockTest, use_authorized_false_ignores_auth_state) {
    // With use_authorized=false, authorization state is irrelevant
    auto sm = create_state_machine(false);

    plug_in();
    EXPECT_GT(lock_count, 0);
    reset_counts();

    // Deauthorize should not unlock when use_authorized is false
    sm->set_authorized(false);

    EXPECT_EQ(unlock_count, 0) << "use_authorized=false should keep lock regardless of auth state";

    // Authorize should also be a no-op (and not double-fire signal_lock)
    sm->set_authorized(true);

    EXPECT_EQ(lock_count, 0) << "use_authorized=false: set_authorized(true) should not re-fire lock";
    EXPECT_EQ(unlock_count, 0) << "use_authorized=false: set_authorized(true) should not unlock";

    plug_out();
    EXPECT_GT(unlock_count, 0);
}

TEST_F(ConnectorLockTest, force_unlock_overrides_authorized) {
    // connector_force_unlock must release the connector even while authorized
    auto sm = create_state_machine(true);

    plug_in();
    sm->set_authorized(true);
    ASSERT_GT(lock_count, 0) << "precondition: connector locked when authorized in State B";
    reset_counts();

    sm->connector_force_unlock();

    EXPECT_GT(unlock_count, 0) << "connector_force_unlock should unlock even when authorized";
}

// ---------------------------------------------------------------------------
// Tests for captive cable mode (keep_cable_locked)
//
// The cable stays locked in the socket whenever a plug is present (detected
// via Proximity Pilot), in every CP state including A. Only a force unlock
// releases it, and only until the cable is removed: the next insertion locks
// again. An engaged lock is latched: PP loss without a force unlock (plug
// pulled against the lock pin) does not release it.

using Ampacity = types::board_support_common::Ampacity;

TEST_F(ConnectorLockTest, captive_locks_on_plug_present_without_car) {
    auto sm = create_state_machine(false, true);

    // No CP activity at all: plug the cable into the socket only
    set_pp(*sm, Ampacity::A_32);

    EXPECT_GT(lock_count, 0) << "captive mode should lock as soon as a plug is present, even in state A";
}

// The AsyncTimeout poll resolution is 500ms, so a configured delay of D fires in [D, D+500)ms.
// With a 200ms delay that is up to ~700ms; assertions wait past 1000ms to stay robust.
TEST_F(ConnectorLockTest, captive_lock_debounce_waits_before_locking) {
    auto sm = create_state_machine(false, true, 200);

    // Plug detected: must not lock immediately, the plug is not yet seated.
    set_pp(*sm, Ampacity::A_32);
    EXPECT_EQ(lock_count, 0) << "captive mode must not lock before the seating debounce elapses";

    // After the debounce, the seated plug locks.
    std::this_thread::sleep_for(std::chrono::milliseconds(1300));
    EXPECT_GT(lock_count, 0) << "captive mode should lock once the seating debounce has elapsed";
}

TEST_F(ConnectorLockTest, captive_lock_debounce_cancelled_if_plug_pulled_back) {
    auto sm = create_state_machine(false, true, 200);

    set_pp(*sm, Ampacity::A_32); // plug touches PP
    set_pp(*sm, Ampacity::None); // pulled back out before it seats

    std::this_thread::sleep_for(std::chrono::milliseconds(1300));
    EXPECT_EQ(lock_count, 0) << "a plug pulled back out within the debounce must not lock";
}

TEST_F(ConnectorLockTest, captive_lock_debounce_removal_racing_expiry_leaves_socket_unlocked) {
    auto sm = create_state_machine(false, true, 200);

    // Remove while the debounce is still pending: the expiry must not lock the now empty socket,
    // and a later insertion must re-arm the debounce (not lock immediately).
    set_pp(*sm, Ampacity::A_32);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    set_pp(*sm, Ampacity::None);
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    EXPECT_EQ(lock_count, 0) << "a removal before the debounce expiry must not lock the empty socket";

    reset_counts();
    set_pp(*sm, Ampacity::A_32);
    EXPECT_EQ(lock_count, 0) << "reinsertion after such a removal must re-arm the debounce, not lock at once";
    std::this_thread::sleep_for(std::chrono::milliseconds(1300));
    EXPECT_GT(lock_count, 0) << "and then lock once the fresh debounce elapses";
}

TEST_F(ConnectorLockTest, captive_empty_socket_stays_unlocked) {
    auto sm = create_state_machine(false, true);

    set_pp(*sm, Ampacity::None);

    EXPECT_EQ(lock_count, 0) << "captive mode must not lock an empty socket";

    // Negative control: the same state machine does lock once a plug shows up
    set_pp(*sm, Ampacity::A_32);
    EXPECT_GT(lock_count, 0) << "captive mode should lock on insertion";
}

TEST_F(ConnectorLockTest, captive_force_unlock_on_empty_socket_does_not_defeat_next_lock) {
    auto sm = create_state_machine(false, true);

    set_pp(*sm, Ampacity::None);
    // e.g. a CSMS (re)sending UnlockConnector after the cable was already taken out
    sm->connector_force_unlock();
    reset_counts();

    set_pp(*sm, Ampacity::A_32);

    EXPECT_GT(lock_count, 0) << "a force unlock on an empty socket must not leave the next cable unlocked";
}

TEST_F(ConnectorLockTest, captive_stays_locked_across_full_session) {
    auto sm = create_state_machine(false, true);

    set_pp(*sm, Ampacity::A_32);
    ASSERT_GT(lock_count, 0);
    reset_counts();

    plug_in();  // A -> B
    plug_out(); // back to A: normally this unlocks

    EXPECT_EQ(unlock_count, 0) << "captive mode should stay locked when the car unplugs (plug still in socket)";
}

TEST_F(ConnectorLockTest, captive_pp_loss_without_force_unlock_stays_locked) {
    auto sm = create_state_machine(false, true);

    set_pp(*sm, Ampacity::A_32);
    ASSERT_GT(lock_count, 0);
    reset_counts();

    // Pulling the plug against the engaged lock pin opens the PP contact before the plug
    // can leave the socket; the lock is latched and must not release on the PP loss.
    set_pp(*sm, Ampacity::None);
    EXPECT_EQ(unlock_count, 0) << "PP loss without force unlock must not release the lock";

    // Plug pushed back in: still locked
    set_pp(*sm, Ampacity::A_32);
    EXPECT_EQ(unlock_count, 0);
}

TEST_F(ConnectorLockTest, captive_force_unlock_holds_until_reinsertion) {
    auto sm = create_state_machine(false, true);

    set_pp(*sm, Ampacity::A_32);
    ASSERT_GT(lock_count, 0);
    reset_counts();

    sm->connector_force_unlock();
    EXPECT_GT(unlock_count, 0) << "force unlock should release the cable";
    reset_counts();

    // Cable not yet removed; CP activity must not re-lock while the window is open
    plug_in();
    plug_out();
    EXPECT_EQ(lock_count, 0) << "must stay unlocked until the cable is removed";

    // Cable removed: socket empty, still unlocked
    set_pp(*sm, Ampacity::None);
    EXPECT_EQ(lock_count, 0) << "empty socket must stay unlocked";

    // Next insertion locks again
    set_pp(*sm, Ampacity::A_20);
    EXPECT_GT(lock_count, 0) << "next insertion should lock again";
}

TEST_F(ConnectorLockTest, captive_force_unlock_blocked_while_relais_on) {
    auto sm = create_state_machine(false, true);

    set_pp(*sm, Ampacity::A_32);
    plug_in();
    bsp.raise_event(Event::PowerOn);
    reset_counts();

    sm->connector_force_unlock();
    EXPECT_EQ(unlock_count, 0) << "force unlock must not release the connector while relais are on";

    // Once the relais open, the pending window takes effect
    bsp.raise_event(Event::PowerOff);
    EXPECT_GT(unlock_count, 0) << "pending force unlock should release once the relais are off";
}

TEST_F(ConnectorLockTest, captive_ignores_authorization) {
    // captive mode takes precedence over use_authorized
    auto sm = create_state_machine(true, true);

    set_pp(*sm, Ampacity::A_32);

    EXPECT_GT(lock_count, 0) << "captive mode should lock without any authorization";
}

TEST_F(ConnectorLockTest, captive_runtime_toggle) {
    auto sm = create_state_machine(false, false);

    // Normal mode, plug in socket, no car: unlocked
    set_pp(*sm, Ampacity::A_32);
    EXPECT_EQ(lock_count, 0);

    sm->set_keep_cable_locked(true);
    EXPECT_GT(lock_count, 0) << "enabling captive mode at runtime should lock immediately";
    reset_counts();

    sm->set_keep_cable_locked(false);
    EXPECT_GT(unlock_count, 0) << "disabling captive mode at runtime should fall back to CP-state logic";
}

// ---------------------------------------------------------------------------
// Tests for CP state F persistence
//
// The high level state machine commands state F on fatal errors. A BSP state
// event (e.g. the initial A published around startup) arriving after that
// command must not trigger the automatic X1 reset, which would undo the F
// and unmask the fault.

struct CpStateFTest : public testing::Test {
    struct CpCommandRecorder : public BspStub {
        std::vector<std::string> cp_commands;

        Result call_cp_state_X1(Parameters p) override {
            cp_commands.push_back("X1");
            return BspStub::call_cp_state_X1(p);
        }

        Result call_cp_state_F(Parameters p) override {
            cp_commands.push_back("F");
            return BspStub::call_cp_state_F(p);
        }
    };

    CpCommandRecorder bsp;
    std::unique_ptr<evse_board_supportIntf> bsp_if;

    std::unique_ptr<module::IECStateMachine> create_state_machine() {
        bsp_if = std::make_unique<module::stub::evse_board_supportIntfStub>(bsp);
        auto sm = std::make_unique<module::IECStateMachine>(bsp_if, true, false, false, 0);
        sm->enable(true);
        return sm;
    }
};

TEST_F(CpStateFTest, state_events_do_not_undo_requested_state_f) {
    auto sm = create_state_machine();

    sm->set_cp_state_F();
    ASSERT_EQ(bsp.cp_commands, std::vector<std::string>{"F"});

    // BSP still reports A, the F command has not taken effect there yet
    bsp.raise_event(Event::A);
    EXPECT_EQ(bsp.cp_commands, std::vector<std::string>{"F"}) << "A event must not reset the requested state F to X1";

    bsp.raise_event(Event::E);
    EXPECT_EQ(bsp.cp_commands, std::vector<std::string>{"F"}) << "E event must not reset the requested state F to X1";
}

TEST_F(CpStateFTest, x1_request_reenables_automatic_reset) {
    auto sm = create_state_machine();

    sm->set_cp_state_F();
    bsp.raise_event(Event::F);
    sm->set_cp_state_X1();
    bsp.cp_commands.clear();

    bsp.raise_event(Event::A);
    EXPECT_EQ(bsp.cp_commands, std::vector<std::string>{"X1"})
        << "after an X1 request the A event should reset the CP state again";
}

TEST_F(CpStateFTest, pwm_request_reenables_automatic_reset) {
    auto sm = create_state_machine();

    sm->set_cp_state_F();
    bsp.raise_event(Event::F);
    sm->set_pwm(0.05);
    bsp.cp_commands.clear();

    bsp.raise_event(Event::A);
    EXPECT_EQ(bsp.cp_commands, std::vector<std::string>{"X1"})
        << "after a PWM request the A event should reset the CP state again";
}

} // namespace
