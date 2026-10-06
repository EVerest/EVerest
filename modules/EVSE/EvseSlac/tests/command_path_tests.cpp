// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
//
// Commands reach the state machine through fd_event_handler::add_action, whose run_actions swallows
// exceptions. FSMController therefore runs every posted command guarded: a throw is reported through
// the fatal handler with the command's name, instead of leaving a half-transitioned machine and an
// empty log. The controller is still active when the handler runs, so the handler can run the reset
// path for the consumer before it stops the controller.
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include <everest/io/event/fd_event_handler.hpp>
#include <everest/slac/HomeplugMessage.hpp>
#include <everest/slac/fsm/evse/context.hpp>

#include "fsm_controller.hpp"

namespace {

using namespace std::chrono_literals;
using everest::lib::slac::D3State;
using everest::lib::slac::SlacState;
using everest::lib::slac::fsm::evse::Context;
using everest::lib::slac::fsm::evse::ContextCallbacks;
namespace defs = everest::lib::slac::defs;
namespace messages = everest::lib::slac::messages;

messages::HomeplugMessage create_cm_set_key_cnf() {
    messages::cm_set_key_cnf cnf{};
    cnf.result = defs::CM_SET_KEY_CNF_RESULT_MODEM_COMPAT_SUCCESS;
    messages::HomeplugMessage message;
    message.setup_payload(&cnf, sizeof(cnf), defs::MMTYPE_CM_SET_KEY | defs::MMTYPE_MODE_CNF, defs::MMV::AV_1_1);
    return message;
}

struct Rig {
    ContextCallbacks callbacks{};
    bool throw_on_matching{false};
    std::string fatal_reason;
    int fatal_calls{0};
    bool active_when_reported{false};
    std::unique_ptr<Context> ctx;
    std::unique_ptr<FSMController> ctrl;
    everest::lib::io::event::fd_event_handler handler;

    Rig() {
        callbacks.send_raw_slac = [](messages::HomeplugMessage&) { return true; };
        callbacks.log_debug = [](std::string const&) {};
        callbacks.log_info = [](std::string const&) {};
        callbacks.log_warn = [](std::string const&) {};
        callbacks.log_error = [](std::string const&) {};
        // The public state is published after every event; entering Matching publishes MATCHING.
        callbacks.signal_state = [this](D3State state) {
            if (throw_on_matching and state == D3State::Matching) {
                throw std::runtime_error("publisher exploded");
            }
        };
        ctx = std::make_unique<Context>(callbacks);
        ctx->slac_config.request_info_delay = 1ms;
        ctx->slac_config.set_key_timeout = 5ms;
        ctx->slac_config.set_key_max_attempts = 3;
        ctx->slac_config.slac_init_timeout = 5000ms;
        ctx->slac_config.chip_reset.enabled = false;
        ctx->slac_config.reset_instead_of_fail = false;
        ctx->slac_config.ac_mode_five_percent = false;
        ctrl = std::make_unique<FSMController>(*ctx);
        ctrl->set_fatal_handler([this](std::string const& reason) {
            fatal_reason = reason;
            ++fatal_calls;
            // What the module does here: tear down through the machine, then stop. A controller
            // that had already stopped itself would refuse the post and make the teardown a no-op.
            active_when_reported = ctrl->signal_reset();
            ctrl->stop();
        });
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
    void pump(std::chrono::milliseconds budget) {
        auto const deadline = std::chrono::steady_clock::now() + budget;
        while (std::chrono::steady_clock::now() < deadline) {
            handler.poll(1ms);
            handler.run_actions();
        }
    }
    // Drive the real machine into Idle: Init times out into Reset, the CM_SET_KEY.CNF completes it.
    bool reach_idle() {
        if (not ctrl->register_events(handler)) {
            return false;
        }
        if (not ctrl->init()) {
            return false;
        }
        if (not pump_until_state(SlacState::Reset, 2s)) {
            return false;
        }
        ctrl->signal_new_slac_message(create_cm_set_key_cnf());
        return pump_until_state(SlacState::Idle, 2s);
    }
};

TEST(CommandPath, AThrowingCommandIsReportedWhileStillActiveAndTheHandlerStopsIt) {
    Rig rig;
    ASSERT_TRUE(rig.reach_idle());

    rig.throw_on_matching = true;
    ASSERT_TRUE(rig.ctrl->signal_enter_bcd());
    rig.pump(200ms);

    EXPECT_EQ(rig.fatal_calls, 1);
    EXPECT_NE(rig.fatal_reason.find("enter_bcd"), std::string::npos) << rig.fatal_reason;
    EXPECT_NE(rig.fatal_reason.find("publisher exploded"), std::string::npos) << rig.fatal_reason;
    EXPECT_TRUE(rig.active_when_reported) << "the controller stopped itself before the handler could tear down";
    // Stopped by the handler: further commands are refused rather than run on a dead machine.
    EXPECT_FALSE(rig.ctrl->signal_leave_bcd());
}

TEST(CommandPath, CommandsAreRefusedBeforeInitAndAcceptedAfter) {
    Rig rig;
    // Not registered, not active: the caller learns that the command went nowhere.
    EXPECT_FALSE(rig.ctrl->signal_reset());
    EXPECT_FALSE(rig.ctrl->signal_enter_bcd());
    ASSERT_TRUE(rig.reach_idle());
    EXPECT_TRUE(rig.ctrl->signal_reset());
    rig.pump(50ms);
    EXPECT_EQ(rig.fatal_calls, 0);
}

} // namespace
