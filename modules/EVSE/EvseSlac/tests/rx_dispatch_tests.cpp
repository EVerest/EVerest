// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
//
// Every dispatch into the state machine (interface commands, received frames, the I/O error
// teardown) runs with the lifecycle monitor held: shutdown() destroys the controller under that
// same monitor, so holding it is what keeps the pointer valid across the call. That is only safe
// because nothing reachable from the machine takes the monitor again: in particular send_raw_slac,
// which the machine calls synchronously to answer a request, reads only the I/O object.
//
// An earlier shape had send_raw_slac take the monitor, so the receive path had to release it before
// dispatching and the module carried two dispatch helpers with a deadlock rule in comments. These
// tests pin the current invariant with the real FSMController, the real EVSE machine and the real
// lifecycle monitor type. slacImpl itself needs the generated framework headers and cannot be
// constructed here, so the rig mirrors its send_raw_slac.
//
// The monitor is instantiated over std::timed_mutex rather than the production std::mutex purely
// so a regression that re-enters it surfaces as a failed acquisition instead of a hung test binary.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
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
using everest::lib::slac::SlacState;
using everest::lib::slac::fsm::evse::Context;
using everest::lib::slac::fsm::evse::ContextCallbacks;
namespace defs = everest::lib::slac::defs;
namespace messages = everest::lib::slac::messages;
using EvMac = messages::HomeplugMessage::MacAddress;
using RunId = std::array<std::uint8_t, defs::RUN_ID_LEN>;

using LifecycleState = module::main::LifecycleStateT<FSMController>;
using LifecycleMonitor = everest::lib::util::monitor<LifecycleState, std::timed_mutex>;

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

// A real FSMController driven by a real fd_event_handler, the way slacImpl drives it. send_raw_slac
// mirrors the module's: it does not touch the lifecycle monitor.
struct Rig {
    ContextCallbacks callbacks{};
    LifecycleMonitor lifecycle;
    std::vector<messages::HomeplugMessage> sent;
    std::atomic_bool send_attempted{false};
    std::atomic_bool sent_on_dispatch_thread{false};
    std::thread::id dispatch_thread{};
    std::unique_ptr<Context> ctx;
    std::unique_ptr<FSMController> ctrl;
    everest::lib::io::event::fd_event_handler handler;

    Rig() {
        callbacks.send_raw_slac = [this](messages::HomeplugMessage& msg) {
            send_attempted.store(true);
            sent_on_dispatch_thread.store(std::this_thread::get_id() == dispatch_thread);
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
    void reach_matching() {
        dispatch_thread = std::this_thread::get_id();
        ASSERT_TRUE(ctrl->register_events(handler));
        ASSERT_TRUE(ctrl->init());
        ASSERT_TRUE(pump_until_state(SlacState::Reset, 500ms)) << "never entered Reset";
        ctrl->signal_new_slac_message(create_cm_set_key_cnf(defs::CM_SET_KEY_CNF_RESULT_MODEM_COMPAT_SUCCESS));
        ASSERT_TRUE(pump_until_state(SlacState::Idle, 500ms)) << "never reached Idle on CM_SET_KEY.CNF";
        ASSERT_TRUE(ctrl->signal_enter_bcd());
        ASSERT_TRUE(pump_until_state(SlacState::Matching, 500ms)) << "never reached Matching on enter_bcd";
        sent.clear();
        send_attempted.store(false);
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

RunId make_run_id(std::uint8_t base) {
    RunId run_id{};
    for (std::size_t i = 0; i < run_id.size(); ++i) {
        run_id[i] = static_cast<std::uint8_t>(base + i);
    }
    return run_id;
}

// The one dispatch shape: monitor held across the call, reply produced synchronously inside it.
TEST(RxDispatch, DispatchUnderTheMonitorLetsTheReplyOut) {
    Rig rig;
    rig.reach_matching();
    EvMac ev_mac = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
    auto const req = create_cm_slac_parm_req(ev_mac, make_run_id(0x11));
    ASSERT_TRUE(module::main::dispatch_to_controller(
        rig.lifecycle, [&req](FSMController& target) { target.signal_new_slac_message(req); }));
    EXPECT_TRUE(rig.send_attempted.load()) << "the FSM did not try to answer the request";
    EXPECT_TRUE(rig.sent_on_dispatch_thread.load()) << "the reply was not sent on the dispatching thread";
    EXPECT_EQ(rig.count_parm_cnf(), 1U) << "no CM_SLAC_PARM.CNF was produced";
}

// The invariant that makes holding the monitor safe: the FSM's send path must not take it. If a
// future send_raw_slac did, this rig would deadlock in production; here the timed mutex makes the
// re-entrant acquisition observable.
TEST(RxDispatch, TheSendPathDoesNotTakeTheMonitor) {
    Rig rig;
    rig.reach_matching();
    std::atomic_bool reentered{false};
    rig.callbacks.send_raw_slac = [&rig, &reentered](messages::HomeplugMessage& msg) {
        // Would a send_raw_slac that takes the monitor get it while the dispatcher holds it?
        auto guard = rig.lifecycle.handle(50ms);
        reentered.store(guard.has_value());
        rig.sent.push_back(msg);
        return true;
    };
    EvMac ev_mac = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02};
    auto const req = create_cm_slac_parm_req(ev_mac, make_run_id(0x21));
    ASSERT_TRUE(module::main::dispatch_to_controller(
        rig.lifecycle, [&req](FSMController& target) { target.signal_new_slac_message(req); }));
    EXPECT_FALSE(reentered.load()) << "the monitor was available inside the FSM's send path -- it is held by the "
                                      "dispatcher, so a send_raw_slac that takes it deadlocks in production";
    EXPECT_EQ(rig.count_parm_cnf(), 1U);
}

// The dispatch is also the gate that drops frames while the PLC I/O is down or shutdown started.
TEST(RxDispatch, DispatchDropsTheFrameWhenTheIoIsNotReadyOrShuttingDown) {
    Rig rig;
    rig.reach_matching();
    EvMac ev_mac = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x03};
    auto const req = create_cm_slac_parm_req(ev_mac, make_run_id(0x31));
    {
        auto guard = rig.lifecycle.handle();
        guard->slac_io_ready = false;
    }
    EXPECT_FALSE(module::main::dispatch_to_controller(
        rig.lifecycle, [&req](FSMController& target) { target.signal_new_slac_message(req); }));
    {
        auto guard = rig.lifecycle.handle();
        guard->slac_io_ready = true;
        guard->shutting_down = true;
    }
    EXPECT_FALSE(module::main::dispatch_to_controller(
        rig.lifecycle, [&req](FSMController& target) { target.signal_new_slac_message(req); }));
    EXPECT_FALSE(rig.send_attempted.load()) << "the FSM ran although the dispatch should have dropped the frame";
}

} // namespace
