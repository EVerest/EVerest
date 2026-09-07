// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
//
// Regression tests for the SLAC I/O receive dispatch in slacImpl::configure_slac_io_callbacks().
//
// The receive callback used to hand the frame to FSMController::signal_new_slac_message() through
// post_command(), which holds the lifecycle monitor across the call. Unlike the other signals,
// signal_new_slac_message() runs the FSM in place, and a request the FSM answers calls back into
// send_raw_slac, which takes the same monitor. The monitor is a plain std::mutex, so the loop
// thread deadlocked against itself on the first inbound frame that needed a reply
// (CM_SLAC_PARM.REQ) and the module never sent anything or read another frame.
//
// slacImpl itself needs the generated framework headers and cannot be constructed here, so these
// tests exercise the two halves the bug was made of, with the real FSMController, the real EVSE
// state machine and the real lifecycle monitor type:
//
//   * that answering an inbound request re-enters the context callbacks synchronously, on the
//     thread that dispatched the frame -- the reason a lock must not be held across the dispatch;
//   * that dispatch_to_controller_unlocked(), the helper slacImpl's receive callback and
//     handle_slac_io_error() both go through, lets that reply out, while post_command's shape
//     (monitor held across the call) loses it.
//
// The first test drives the same helper the module calls, so putting the dispatch back under the
// monitor in slacImpl.cpp fails it. What it cannot catch is the receive callback being rewritten
// to stop using the helper at all.
//
// The monitor is instantiated over std::timed_mutex rather than the production std::mutex purely
// so re-entering it surfaces as a timeout instead of hanging the test binary. Everything else is
// the production type.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <everest/io/event/fd_event_handler.hpp>
#include <everest/slac/HomeplugMessage.hpp>
#include <everest/slac/fsm/evse/context.hpp>
#include <everest/util/async/monitor.hpp>

#include "fsm_controller.hpp"
#include "lifecycle_gate.hpp"

namespace {

using namespace std::chrono_literals;
using everest::lib::slac::D3State;
using everest::lib::slac::SlacState;
using everest::lib::slac::fsm::evse::Context;
using everest::lib::slac::fsm::evse::ContextCallbacks;
namespace defs = everest::lib::slac::defs;
namespace messages = everest::lib::slac::messages;

using EvMac = messages::HomeplugMessage::MacAddress;
using RunId = std::array<std::uint8_t, defs::RUN_ID_LEN>;

// Production type, over a timed mutex so a re-entrant acquisition times out instead of hanging.
using LifecycleState = module::main::LifecycleStateT<FSMController>;
using LifecycleMonitor = everest::lib::util::monitor<LifecycleState, std::timed_mutex>;

// How long send_raw_slac waits for the monitor before calling it unavailable. Only has to
// outlast a scheduling hiccup; the re-entrant case can never succeed however long it waits.
constexpr auto MONITOR_ACQUIRE_TIMEOUT = 250ms;

messages::HomeplugMessage create_cm_set_key_cnf(std::uint8_t result) {
    messages::cm_set_key_cnf cnf{};
    cnf.result = result;
    messages::HomeplugMessage message;
    message.setup_payload(&cnf, sizeof(cnf), defs::MMTYPE_CM_SET_KEY | defs::MMTYPE_MODE_CNF, defs::MMV::AV_1_1);
    return message;
}

messages::HomeplugMessage create_cm_slac_parm_req(EvMac const& ev_mac, RunId const& run_id) {
    messages::cm_slac_parm_req req{};
    req.application_type = defs::COMMON_APPLICATION_TYPE;
    req.security_type = defs::COMMON_SECURITY_TYPE;
    std::copy(run_id.begin(), run_id.end(), req.run_id);

    messages::HomeplugMessage message;
    message.set_source(ev_mac);
    message.setup_payload(&req, sizeof(req), defs::MMTYPE_CM_SLAC_PARAM | defs::MMTYPE_MODE_REQ, defs::MMV::AV_1_1);
    return message;
}

// The rig: a real FSMController driven by a real fd_event_handler, the way slacImpl drives it.
// send_raw_slac mirrors the module's, which takes the lifecycle monitor before touching the wire.
struct Rig {
    ContextCallbacks callbacks{};
    LifecycleMonitor lifecycle;

    std::vector<messages::HomeplugMessage> sent;
    // Outcome of the monitor acquisition inside the last send_raw_slac call.
    std::atomic_bool send_attempted{false};
    std::atomic_bool monitor_acquired{false};
    std::atomic_bool sent_on_dispatch_thread{false};
    std::thread::id dispatch_thread{};

    std::unique_ptr<Context> ctx;
    std::unique_ptr<FSMController> ctrl;
    everest::lib::io::event::fd_event_handler handler;

    Rig() {
        callbacks.send_raw_slac = [this](messages::HomeplugMessage& msg) {
            send_attempted.store(true);
            sent_on_dispatch_thread.store(std::this_thread::get_id() == dispatch_thread);
            // What slacImpl::send_raw_slac does: consult the lifecycle flags under the monitor
            // before handing the frame to the socket.
            auto guard = lifecycle.handle(MONITOR_ACQUIRE_TIMEOUT);
            if (not guard.has_value()) {
                monitor_acquired.store(false);
                return false;
            }
            if (not(*guard)->slac_io_ready) {
                monitor_acquired.store(true);
                return false;
            }
            monitor_acquired.store(true);
            sent.push_back(msg);
            return true;
        };
        callbacks.log_debug = [](std::string const&) {};
        callbacks.log_info = [](std::string const&) {};
        callbacks.log_warn = [](std::string const&) {};
        callbacks.log_error = [](std::string const&) {};

        ctx = std::make_unique<Context>(callbacks);
        ctx->slac_config.request_info_delay = 1ms;
        ctx->slac_config.set_key_timeout = 5ms;
        ctx->slac_config.set_key_max_attempts = 3;
        ctx->slac_config.slac_init_timeout = 5000ms;
        ctx->slac_config.chip_reset.enabled = false;
        ctx->slac_config.reset_instead_of_fail = false;
        ctx->slac_config.ac_mode_five_percent = false;
        EvMac evse_mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
        std::copy(evse_mac.begin(), evse_mac.end(), std::begin(ctx->evse_mac));

        ctrl = std::make_unique<FSMController>(*ctx);
        {
            auto guard = lifecycle.handle();
            guard->worker = ctrl.get();
            guard->slac_io_ready = true;
            guard->ready_entered = true;
        }
    }

    ~Rig() {
        ctrl->stop();
        ctrl->unregister_events(handler);
    }

    // One turn of the module's loop: `poll(); run_actions();`.
    void pump(std::chrono::milliseconds budget) {
        auto const deadline = std::chrono::steady_clock::now() + budget;
        while (std::chrono::steady_clock::now() < deadline) {
            handler.poll(1ms);
            handler.run_actions();
        }
    }

    bool pump_until_state(SlacState wanted, std::chrono::milliseconds budget) {
        auto const deadline = std::chrono::steady_clock::now() + budget;
        while (std::chrono::steady_clock::now() < deadline) {
            if (ctx->status.match_state == wanted) {
                return true;
            }
            handler.poll(1ms);
            handler.run_actions();
        }
        return ctx->status.match_state == wanted;
    }

    // Drive Reset -> Idle -> Matching, so the next inbound CM_SLAC_PARM.REQ has to be answered.
    // Uses the safe dispatch shape; the tests below are about the frame after this.
    void reach_matching() {
        dispatch_thread = std::this_thread::get_id();
        ASSERT_TRUE(ctrl->register_events(handler));
        ctrl->init();
        ASSERT_TRUE(pump_until_state(SlacState::Reset, 500ms)) << "never entered Reset";

        ctrl->signal_new_slac_message(create_cm_set_key_cnf(defs::CM_SET_KEY_CNF_RESULT_MODEM_COMPAT_SUCCESS));
        ASSERT_TRUE(pump_until_state(SlacState::Idle, 500ms)) << "never reached Idle on CM_SET_KEY.CNF";

        ASSERT_TRUE(ctrl->signal_enter_bcd());
        ASSERT_TRUE(pump_until_state(SlacState::Matching, 500ms)) << "never reached Matching on enter_bcd";

        // Clear the setup traffic (CM_SET_KEY.REQ) and its bookkeeping.
        sent.clear();
        send_attempted.store(false);
        monitor_acquired.store(false);
    }

    std::size_t count_parm_cnf() const {
        std::size_t n = 0;
        for (auto const& msg : sent) {
            if (msg.get_mmtype() == (defs::MMTYPE_CM_SLAC_PARAM | defs::MMTYPE_MODE_CNF)) {
                ++n;
            }
        }
        return n;
    }
};

// The shape slacImpl uses now: look the controller up under the monitor, release it, dispatch.
TEST(RxDispatch, DispatchHelperLetsTheReplyOut) {
    Rig rig;
    rig.reach_matching();

    EvMac ev_mac = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
    RunId run_id{};
    for (std::size_t i = 0; i < run_id.size(); ++i) {
        run_id[i] = static_cast<std::uint8_t>(0x11 + i);
    }

    // Exactly what slacImpl's receive callback does with the frame.
    auto const req = create_cm_slac_parm_req(ev_mac, run_id);
    ASSERT_TRUE(module::main::dispatch_to_controller_unlocked(
        rig.lifecycle, [&req](FSMController& target) { target.signal_new_slac_message(req); }));

    // The FSM answers inside the dispatch call, on the dispatching thread: that synchronous
    // re-entry into the context callbacks is exactly why the monitor must be released first.
    EXPECT_TRUE(rig.send_attempted.load()) << "the FSM did not try to answer the request";
    EXPECT_TRUE(rig.sent_on_dispatch_thread.load()) << "the reply was not sent on the dispatching thread";
    EXPECT_TRUE(rig.monitor_acquired.load()) << "send_raw_slac could not take the lifecycle monitor";
    EXPECT_EQ(rig.count_parm_cnf(), 1U) << "no CM_SLAC_PARM.CNF was produced";
}

// The shape that shipped: post_command() held the monitor across the dispatch. In production, with
// a std::mutex, this is a permanent self-deadlock of the loop thread; here the timed mutex turns it
// into a failed acquisition so the failure mode is observable instead of a hang.
TEST(RxDispatch, HoldingTheMonitorAcrossDispatchLosesTheReply) {
    Rig rig;
    rig.reach_matching();

    EvMac ev_mac = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02};
    RunId run_id{};
    for (std::size_t i = 0; i < run_id.size(); ++i) {
        run_id[i] = static_cast<std::uint8_t>(0x21 + i);
    }

    {
        // post_command's invariant: the monitor is held across the call, not just the lookup.
        auto guard = rig.lifecycle.handle();
        auto* target = guard->live_worker();
        ASSERT_NE(target, nullptr);
        target->signal_new_slac_message(create_cm_slac_parm_req(ev_mac, run_id));
    }

    EXPECT_TRUE(rig.send_attempted.load()) << "the FSM did not try to answer the request";
    EXPECT_FALSE(rig.monitor_acquired.load())
        << "send_raw_slac took the monitor while the dispatcher held it -- is the monitor recursive now? "
           "if so, revisit the dispatch shape in slacImpl::configure_slac_io_callbacks()";
    EXPECT_EQ(rig.count_parm_cnf(), 0U) << "a reply escaped although the send path was blocked";
}

// The helper is also the gate that drops frames while the PLC I/O is down.
TEST(RxDispatch, DispatchHelperDropsTheFrameWhenTheIoIsNotReady) {
    Rig rig;
    rig.reach_matching();
    {
        auto guard = rig.lifecycle.handle();
        guard->slac_io_ready = false;
    }

    EvMac ev_mac = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x03};
    RunId run_id{};
    for (std::size_t i = 0; i < run_id.size(); ++i) {
        run_id[i] = static_cast<std::uint8_t>(0x31 + i);
    }
    auto const req = create_cm_slac_parm_req(ev_mac, run_id);

    EXPECT_FALSE(module::main::dispatch_to_controller_unlocked(
        rig.lifecycle, [&req](FSMController& target) { target.signal_new_slac_message(req); }));
    EXPECT_FALSE(rig.send_attempted.load()) << "the FSM ran although the PLC I/O was down";
}

} // namespace
