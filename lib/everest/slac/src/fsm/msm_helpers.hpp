// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#ifndef BOOST_MSM_DEBUG_SIGMASK
#define BOOST_MSM_DEBUG_SIGMASK
#endif
#ifndef BOOST_MPL_CFG_NO_PREPROCESSED_HEADERS
#define BOOST_MPL_CFG_NO_PREPROCESSED_HEADERS
#endif
#ifndef BOOST_MPL_LIMIT_VECTOR_SIZE
#define BOOST_MPL_LIMIT_VECTOR_SIZE 40
#elif BOOST_MPL_LIMIT_VECTOR_SIZE < 40
#error "BOOST_MPL_LIMIT_VECTOR_SIZE must be at least 40 before including SLAC MSM helpers"
#endif

#include <boost/msm/front/states.hpp>

#include <everest/slac/HomeplugMessage.hpp>
#include <everest/slac/timer.hpp>

#include <chrono>
#include <cstdint>
#include <exception>
#include <type_traits>
#include <utility>

namespace everest::lib::slac::msm {
using namespace boost::msm::front;

// How a throw gets out of the machines. Boost.MSM wraps process_event in a try/catch by default
// and hands a std::exception to the def's exception_caught hook, whose default asserts: abort in
// Debug, silent in Release with the machine continuing from the interrupted transition. Catching
// and continuing cannot be made safe here either: an anonymous row whose action keeps throwing is
// re-fired by the completion event until the stack is gone. So the contexts never let a consumer
// callback throw into the machine: a publisher, sender or logger that throws is parked in
// Context::caught_exception, the call counts as "not done", the transition completes, and the
// wrapper calls rethrow_recorded as soon as process_event has returned, so the failure surfaces
// from message()/update()/reset() on a machine that is still consistent and still takes the
// teardown's reset. Every def declares no_exception_thrown on top, so anything else that throws
// inside a guard or action (a bug) propagates instead of being swallowed; after that the back-end's
// "processing" flag may stay set and the machine is only good for being stopped, which is what the
// modules do.
template <class FSM> void rethrow_recorded(FSM& fsm) {
    if (fsm.ctx != nullptr && fsm.ctx->caught_exception) {
        std::exception_ptr caught;
        std::swap(caught, fsm.ctx->caught_exception);
        std::rethrow_exception(caught);
    }
}

template <class Guard> struct Not_ {
    template <class Evt, class Fsm, class SourceState, class TargetState>
    bool operator()(Evt const& evt, Fsm& fsm, SourceState& src, TargetState& tgt) {
        return !Guard()(evt, fsm, src, tgt);
    }
};

template <class G1, class G2> struct And_ {
    template <class Evt, class Fsm, class SourceState, class TargetState>
    bool operator()(Evt const& evt, Fsm& fsm, SourceState& src, TargetState& tgt) {
        return G1()(evt, fsm, src, tgt) && G2()(evt, fsm, src, tgt);
    }
};

// clang-format off

// Events
struct message {
    messages::HomeplugMessage payload;
};
struct reset {};
struct enter_bcd {};
struct leave_bcd {};
struct update {};

// Guards
// Rule for every transition table: rows sharing source and event must have mutually exclusive guards
// (or the same effect). Boost.MSM tries such rows in reverse table order; nothing here may rely on that.
struct timeout {
    template <class Fsm, class Evt, class SrcT, class TarT>
    bool operator()(Evt const&, Fsm& fsm, SrcT& src, TarT&) {
        return src.state_timeout(fsm.ctx->current_time);
    }
};

template <std::uint16_t MessageType>
struct is_message_of_type {
    template <class Fsm, class SrcT, class TarT>
    bool operator()(message const& e, Fsm&, SrcT&, TarT&) {
        if (not e.payload.is_valid()) {
            return false;
        }
        auto const mmtype = e.payload.get_mmtype();
        return mmtype == MessageType;
    }
};

// Actions
template <class MsgT>
struct send_default_msg {
    template <class Evt, class Fsm, class SrcT, class TarT>
    void operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
        MsgT msg{};
        if (not fsm.ctx->send_slac_message(fsm.ctx->slac_config.plc_peer_mac, msg)) {
            fsm.ctx->log_warn("Failed to send default SLAC message");
        }
    }
};

// States
// A state that arms `to` for `duration` on entry; deriving states set the duration first.
struct timeout_state : public state<> {
    template <class Event, class Fsm> void on_entry(Event const&, Fsm& fsm) {
        to.arm(fsm.ctx->current_time, duration);
    }

    timer to;
    bool state_timeout(timer::tp now) const {
        return to.expired(now);
    }
    void deadlines(earliest_deadline& next) const {
        next.offer(to);
    }

    std::chrono::milliseconds duration{0};
};
template <std::uint32_t TimeoutMS> struct timeout_ms_state : public timeout_state {
    template <class Event, class Fsm> void on_entry(Event const& e, Fsm& fsm) {
        duration = std::chrono::milliseconds(TimeoutMS);
        timeout_state::on_entry(e, fsm);
    }
};

// Whether a state or sub-machine reports its timers; a def that owns one asserts this, since a
// hook the trait does not see would leave the machine without its wake-up.
template <typename T, typename = void> struct has_deadlines : std::false_type {};
template <typename T>
struct has_deadlines<T, std::void_t<decltype(std::declval<T const&>().deadlines(std::declval<earliest_deadline&>()))>>
    : std::true_type {};
static_assert(has_deadlines<timeout_state>::value);

// clang-format on

} // namespace everest::lib::slac::msm
