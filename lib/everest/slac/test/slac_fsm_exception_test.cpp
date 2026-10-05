// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
//
// A consumer callback (publisher, sender, logger) that throws from inside a transition used to hit
// Boost.MSM's default exception hook: abort in Debug, silent in Release with the machine carrying on
// from the interrupted transition. The contexts now park such a throw and report the call as not
// done, the transition completes, and the wrapper rethrows once process_event has returned (see
// msm_helpers.hpp). So the failure surfaces from the call that drove the event exactly like one from
// the post-processing publishers, and the machine stays usable: the reset the modules run on a
// fatal failure is processed, not lost.
#include <array>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <everest/slac/HomeplugMessage.hpp>
#include <everest/slac/ev_slac_fsm.hpp>
#include <everest/slac/fsm/ev/context.hpp>
#include <everest/slac/fsm/evse/context.hpp>
#include <everest/slac/slac_fsm.hpp>

#include "mock_clock.hpp"

using namespace everest::lib::slac;
using namespace std::chrono_literals;
using everest::lib::slac::D3State;

namespace {

bool assert_true(bool cond, char const* test_name, char const* details) {
    if (not cond) {
        std::printf("[%s] FAIL: %s\n", test_name, details);
        return false;
    }
    return true;
}

bool is_mmtype(messages::HomeplugMessage const& msg, std::uint16_t mmtype) {
    return msg.get_mmtype() == mmtype;
}

messages::HomeplugMessage create_cm_set_key_cnf() {
    messages::cm_set_key_cnf cnf{};
    cnf.result = defs::CM_SET_KEY_CNF_RESULT_MODEM_COMPAT_SUCCESS;
    messages::HomeplugMessage message;
    message.setup_payload(&cnf, sizeof(cnf), defs::MMTYPE_CM_SET_KEY | defs::MMTYPE_MODE_CNF, defs::MMV::AV_1_1);
    return message;
}

// Runs `step`; true if it threw the std::runtime_error with `expected_what`, false otherwise.
template <class Step> bool throws_runtime_error(Step&& step, char const* expected_what) {
    try {
        step();
    } catch (std::runtime_error const& e) {
        return std::string(e.what()) == expected_what;
    } catch (...) {
        return false;
    }
    return false;
}

// EVSE: Init times out into Reset, whose sub-machine sends CM_SET_KEY.REQ from a transition action.
// A send_raw_slac that throws there must surface from the update() that made the transition, and a
// reset() afterwards must still run the machine into Reset and send the request.
bool test_evse_throw_inside_transition_surfaces_and_machine_stays_usable() {
    char const* test_name = "evse_throw_inside_transition_surfaces_and_machine_stays_usable";
    test::MockClock clock;
    fsm::evse::ContextCallbacks callbacks{};
    bool throw_on_set_key{false};
    std::size_t set_key_requests{0};
    callbacks.send_raw_slac = [&](messages::HomeplugMessage& msg) {
        if (is_mmtype(msg, defs::MMTYPE_CM_SET_KEY | defs::MMTYPE_MODE_REQ)) {
            if (throw_on_set_key) {
                throw std::runtime_error("send exploded");
            }
            ++set_key_requests;
        }
        return true;
    };
    callbacks.now = clock.source();
    fsm::evse::Context ctx(callbacks);
    ctx.slac_config.request_info_delay = 1ms;
    ctx.slac_config.set_key_timeout = 5ms;
    ctx.slac_config.set_key_max_attempts = 3;
    ctx.slac_config.slac_init_timeout = 5ms;
    ctx.slac_config.chip_reset.enabled = false;
    ctx.slac_config.reset_instead_of_fail = false;
    ctx.slac_config.ac_mode_five_percent = false;

    slac_fsm machine(ctx);
    machine.restart_fsm();

    throw_on_set_key = true;
    bool threw = false;
    for (int round = 0; round < 50 and not threw; ++round) {
        clock.advance_ms(1);
        threw = throws_runtime_error([&] { machine.update(); }, "send exploded");
    }
    bool ok = assert_true(threw, test_name, "the throw from inside the transition did not surface from update()");
    ok &= assert_true(not ctx.caught_exception, test_name, "the exception stayed parked on the context");
    // The transition completed although its send failed: the machine is in Reset, consistent.
    ok &= assert_true(ctx.status.match_state == SlacState::Reset, test_name,
                      "the machine did not complete the transition whose send threw");

    throw_on_set_key = false;
    try {
        // Reset -> Reset re-enters the sub-machine, whose first row sends the request at once.
        machine.reset();
    } catch (...) {
        return assert_true(false, test_name, "the reset after the failure threw");
    }
    ok &= assert_true(ctx.status.match_state == SlacState::Reset, test_name,
                      "the reset after the failure did not reach Reset: the machine is stuck");
    ok &= assert_true(set_key_requests == 1, test_name, "the reset after the failure did not send CM_SET_KEY.REQ");
    return ok;
}

struct EvRig {
    test::MockClock clock;
    fsm::ev::ContextCallbacks callbacks{};
    bool throw_on_unmatched{false};
    bool throw_on_dlink{false};
    std::vector<std::string> states;
    std::vector<bool> dlink;
    std::vector<messages::HomeplugMessage> sent;
    fsm::ev::Context ctx;
    ev_slac_fsm machine;

    EvRig() : ctx(callbacks, fsm::ev::Context::EV_PLC_MAC), machine(ctx) {
        callbacks.now = clock.source();
        callbacks.send_raw_slac = [this](messages::HomeplugMessage& msg) {
            sent.push_back(msg);
            return true;
        };
        callbacks.signal_state = [this](std::string const& state) {
            if (throw_on_unmatched and state == "UNMATCHED") {
                throw std::runtime_error("publish exploded");
            }
            states.push_back(state);
        };
        callbacks.signal_dlink_ready = [this](bool value) {
            if (throw_on_dlink) {
                throw std::runtime_error("dlink exploded");
            }
            dlink.push_back(value);
        };
    }
    std::size_t parm_requests() const {
        std::size_t n = 0;
        for (auto const& msg : sent) {
            if (is_mmtype(msg, defs::MMTYPE_CM_SLAC_PARAM | defs::MMTYPE_MODE_REQ)) {
                ++n;
            }
        }
        return n;
    }
};

// EV: Reset's on_entry publishes UNMATCHED and then dlink_ready(false). From Idle, reset() enters
// it through a transition; the throw must surface from reset(), the dlink_ready(false) behind it
// must still reach the consumer, with two failures in one event the first one is the one reported,
// and trigger_matching() afterwards must still send CM_SLAC_PARM.REQ.
bool test_ev_throw_in_on_entry_surfaces_and_machine_stays_usable() {
    char const* test_name = "ev_throw_in_on_entry_surfaces_and_machine_stays_usable";
    EvRig rig;
    rig.machine.restart_fsm(); // Reset, then Idle through settle; both publish with the throw off
    auto const dlink_before = rig.dlink.size();
    rig.throw_on_unmatched = true;
    bool ok = assert_true(throws_runtime_error([&] { rig.machine.reset(); }, "publish exploded"), test_name,
                          "the throw from Reset's on_entry did not surface from reset()");
    ok &= assert_true(not rig.ctx.caught_exception, test_name, "the exception stayed parked on the context");
    ok &= assert_true(rig.dlink.size() == dlink_before + 1 and not rig.dlink.back(), test_name,
                      "dlink_ready(false) did not go out after the failed state publish in the same on_entry");
    // Both publishers fail: the first failure is the one reported.
    rig.throw_on_dlink = true;
    ok &= assert_true(throws_runtime_error([&] { rig.machine.reset(); }, "publish exploded"), test_name,
                      "with two failures in one event the first was not the one reported");
    ok &= assert_true(not rig.ctx.caught_exception, test_name, "the second failure stayed parked");

    rig.throw_on_unmatched = false;
    rig.throw_on_dlink = false;
    try {
        rig.machine.trigger_matching();
    } catch (...) {
        return assert_true(false, test_name, "trigger_matching after the failure threw");
    }
    ok &= assert_true(rig.parm_requests() == 1, test_name,
                      "trigger_matching after the failure sent no CM_SLAC_PARM.REQ: the machine is stuck");
    return ok;
}

// EV: a throw while settle() feeds update events. With one CM_SLAC_PARM.REQ attempt, the timeout
// moves WaitParmCnf to Failed, whose on_entry publishes UNMATCHED; that happens inside update() and
// must surface from it rather than being masked by further settle rounds.
bool test_ev_throw_during_settle_surfaces_from_update() {
    char const* test_name = "ev_throw_during_settle_surfaces_from_update";
    EvRig rig;
    rig.ctx.slac_config.parm_req_attempts = 1;
    rig.ctx.slac_config.parm_req_timeout = 10ms;
    rig.machine.restart_fsm();
    rig.machine.trigger_matching();
    bool ok = assert_true(rig.parm_requests() == 1, test_name, "trigger_matching sent no CM_SLAC_PARM.REQ");

    rig.throw_on_unmatched = true;
    bool threw = false;
    for (int round = 0; round < 10 and not threw; ++round) {
        rig.clock.advance_ms(11);
        threw = throws_runtime_error([&] { rig.machine.update(); }, "publish exploded");
    }
    ok &= assert_true(threw, test_name, "the throw from Failed's on_entry did not surface from update()");
    ok &= assert_true(not rig.ctx.caught_exception, test_name, "the exception stayed parked on the context");

    rig.throw_on_unmatched = false;
    auto const before = rig.states.size();
    try {
        rig.machine.reset();
    } catch (...) {
        return assert_true(false, test_name, "the reset after the failure threw");
    }
    ok &= assert_true(rig.states.size() > before and rig.states.back() == "UNMATCHED", test_name,
                      "the reset after the failure did not publish UNMATCHED: the machine is stuck");
    return ok;
}

// EV: the controllers log through the context between events. A logger that throws there parks
// the failure with no event to surface it; the next event must raise it before it runs, not after,
// so a failure is never blamed on an unrelated event that was then processed on top of it.
bool test_ev_failure_parked_outside_an_event_surfaces_before_the_next_event() {
    char const* test_name = "ev_failure_parked_outside_an_event_surfaces_before_the_next_event";
    EvRig rig;
    rig.machine.restart_fsm(); // Idle
    rig.callbacks.log_info = [](std::string const&) { throw std::runtime_error("logger outside event"); };
    rig.ctx.log_info("between events");
    bool ok = assert_true(static_cast<bool>(rig.ctx.caught_exception), test_name, "the logger failure was not parked");
    ok &= assert_true(throws_runtime_error([&] { rig.machine.trigger_matching(); }, "logger outside event"), test_name,
                      "the parked failure did not surface from the next event");
    ok &= assert_true(rig.parm_requests() == 0, test_name,
                      "the next event was processed although a parked failure preceded it");
    rig.callbacks.log_info = {};
    try {
        rig.machine.trigger_matching();
    } catch (...) {
        return assert_true(false, test_name, "trigger_matching after the surfaced failure threw");
    }
    return ok and
           assert_true(rig.parm_requests() == 1, test_name, "the machine did not run after the surfaced failure");
}

// EVSE: the public state is published once per logical transition, deduplicated against the last
// state handed over. A publish that threw must not count as handed over, or the consumer stays one
// state behind for good after a transient publisher failure.
bool test_evse_failed_state_publish_is_retried_on_the_next_event() {
    char const* test_name = "evse_failed_state_publish_is_retried_on_the_next_event";
    test::MockClock clock;
    fsm::evse::ContextCallbacks callbacks{};
    callbacks.send_raw_slac = [](messages::HomeplugMessage&) { return true; };
    callbacks.now = clock.source();
    int matching_explosions_left{1};
    std::vector<D3State> delivered;
    callbacks.signal_state = [&](D3State state) {
        if (state == D3State::Matching and matching_explosions_left > 0) {
            --matching_explosions_left;
            throw std::runtime_error("publisher exploded");
        }
        delivered.push_back(state);
    };
    fsm::evse::Context ctx(callbacks);
    ctx.slac_config.request_info_delay = 1ms;
    ctx.slac_config.set_key_timeout = 5ms;
    ctx.slac_config.set_key_max_attempts = 3;
    ctx.slac_config.slac_init_timeout = 5ms;
    ctx.slac_config.chip_reset.enabled = false;
    ctx.slac_config.reset_instead_of_fail = false;
    ctx.slac_config.ac_mode_five_percent = false;
    slac_fsm machine(ctx);
    machine.restart_fsm();
    for (int round = 0; round < 50 and ctx.status.match_state != SlacState::Reset; ++round) {
        clock.advance_ms(1);
        machine.update();
    }
    machine.message(create_cm_set_key_cnf());
    if (!assert_true(ctx.status.match_state == SlacState::Idle, test_name, "did not reach Idle")) {
        return false;
    }
    bool ok = assert_true(throws_runtime_error([&] { machine.enter_bcd(); }, "publisher exploded"), test_name,
                          "the failed MATCHING publish did not surface from enter_bcd()");
    ok &= assert_true(delivered.empty() or delivered.back() != D3State::Matching, test_name,
                      "MATCHING was delivered although the publisher threw");
    try {
        machine.update();
    } catch (...) {
        return assert_true(false, test_name, "the update after the failed publish threw");
    }
    return ok and assert_true(not delivered.empty() and delivered.back() == D3State::Matching, test_name,
                              "MATCHING was not published again on the next event");
}

// EVSE: the controller logs through the context before it starts the machine. A logger that
// throws there parks the failure, and restart_fsm() surfaces it before the machine is started;
// the fatal path then runs the teardown's reset. An event on a machine that was never started
// must be a no-op: its sub-machines were never entered and hold no context, so settling one
// dereferences null.
bool test_evse_events_on_a_machine_that_never_started_are_no_ops() {
    char const* test_name = "evse_events_on_a_machine_that_never_started_are_no_ops";
    test::MockClock clock;
    fsm::evse::ContextCallbacks callbacks{};
    callbacks.send_raw_slac = [](messages::HomeplugMessage&) { return true; };
    callbacks.now = clock.source();
    bool throw_on_log{true};
    callbacks.log_info = [&throw_on_log](std::string const&) {
        if (throw_on_log) {
            throw std::runtime_error("logger exploded");
        }
    };
    fsm::evse::Context ctx(callbacks);
    ctx.slac_config.request_info_delay = 1ms;
    ctx.slac_config.set_key_timeout = 5ms;
    ctx.slac_config.set_key_max_attempts = 3;
    ctx.slac_config.slac_init_timeout = 5ms;
    ctx.slac_config.chip_reset.enabled = false;
    ctx.slac_config.reset_instead_of_fail = false;
    ctx.slac_config.ac_mode_five_percent = false;
    slac_fsm machine(ctx);

    ctx.log_info("Starting the SLAC state machine"); // what FSMController::init does first
    bool ok = assert_true(throws_runtime_error([&] { machine.restart_fsm(); }, "logger exploded"), test_name,
                          "the parked logger failure did not surface from restart_fsm()");
    auto const state_before = ctx.status.match_state;
    // What the fatal path does next, and what a timer or frame might still do: all no-ops.
    try {
        machine.reset();
        machine.update();
        machine.enter_bcd();
        machine.leave_bcd();
        machine.message(create_cm_set_key_cnf());
    } catch (...) {
        return assert_true(false, test_name, "an event on the never-started machine threw");
    }
    ok &= assert_true(ctx.status.match_state == state_before, test_name,
                      "an event on the never-started machine moved it");
    throw_on_log = false;
    try {
        machine.restart_fsm();
    } catch (...) {
        return assert_true(false, test_name, "the clean restart threw");
    }
    for (int round = 0; round < 50 and ctx.status.match_state != SlacState::Reset; ++round) {
        clock.advance_ms(1);
        machine.update();
    }
    return ok and assert_true(ctx.status.match_state == SlacState::Reset, test_name,
                              "the machine did not run after the clean restart");
}

} // namespace

int main() {
    auto const tests = std::array<std::pair<char const*, bool (*)()>, 6>{
        std::make_pair("evse_throw_inside_transition_surfaces_and_machine_stays_usable",
                       test_evse_throw_inside_transition_surfaces_and_machine_stays_usable),
        std::make_pair("ev_throw_in_on_entry_surfaces_and_machine_stays_usable",
                       test_ev_throw_in_on_entry_surfaces_and_machine_stays_usable),
        std::make_pair("ev_throw_during_settle_surfaces_from_update", test_ev_throw_during_settle_surfaces_from_update),
        std::make_pair("ev_failure_parked_outside_an_event_surfaces_before_the_next_event",
                       test_ev_failure_parked_outside_an_event_surfaces_before_the_next_event),
        std::make_pair("evse_failed_state_publish_is_retried_on_the_next_event",
                       test_evse_failed_state_publish_is_retried_on_the_next_event),
        std::make_pair("evse_events_on_a_machine_that_never_started_are_no_ops",
                       test_evse_events_on_a_machine_that_never_started_are_no_ops),
    };
    int failed = 0;
    for (auto const& [name, test] : tests) {
        if (test()) {
            std::printf("[%s] OK\n", name);
        } else {
            ++failed;
        }
    }
    return failed == 0 ? 0 : 1;
}
