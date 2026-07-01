// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest

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

namespace everest::lib::slac::msm {
using namespace boost::msm::front;

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
    messages::HomeplugMessage payload; //TODO: proper type
};
struct reset {};
struct enter_bcd {};
struct leave_bcd {};
struct update {};

// Guards
struct timeout {
    template <class Fsm, class Evt, class SrcT, class TarT>
    bool operator()(Evt const&, Fsm&, SrcT& src, TarT&) {
        return src.state_timeout();
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
struct trigger_update {
    template <class Fsm, class Evt, class SrcT, class TarT>
    void operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
        fsm.process_event(update{});
    }
};

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
template <std::uint32_t TimeoutMS> struct timeout_ms_state : public state<> {
    template <class Event, class Fsm>
    void on_entry(Event const&, Fsm&) {
        to.setDuration(std::chrono::milliseconds(TimeoutMS));
        to.reset();
    }

    timer to;
    bool state_timeout() {
        return to.timeout();
    }
};
struct timeout_state : public state<> {
    template <class Event, class Fsm> void on_entry(Event const&, Fsm&) {
        to.setDuration(std::chrono::milliseconds(state_timeout_ms));
        to.reset();
    }

    timer to;
    bool state_timeout() {
        return to.timeout();
    }

    std::uint32_t state_timeout_ms{0};
};

// clang-format on

} // namespace everest::lib::slac::msm
