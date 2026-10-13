// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

// EVSE SLAC state machine on fsm::v2 (see fsm_helpers.hpp for the execution model).
//
// Top level:  Init -> Reset -> [ResetChip ->] Idle -> Matching -> [WaitForLink ->] Matched
//             with Failed reachable from Reset, Matching and WaitForLink, and reset / leave_bcd
//             leading back to Reset or Idle.
// Each top-level state except Idle and Failed is a sub-machine defined in its own source file;
// they create each other through the make_* factories below.

#pragma once

#include "../fsm_helpers.hpp"
#include "amp_map_handler.hpp"
#include "validate_handler.hpp"

#include <everest/slac/MatchingSessionData.hpp>
#include <everest/slac/fsm/evse/context.hpp>
#include <everest/slac/slac_types.hpp>
#include <everest/slac/timer.hpp>

#include <chrono>
#include <deque>
#include <initializer_list>
#include <memory>
#include <vector>

namespace everest::lib::slac::sm::evse {

// Ids of every state of the EVSE machines, sub-machine states included.
enum class StateId : int {
    // top level
    Init,
    Reset,
    ResetChip,
    Idle,
    Matching,
    WaitForLink,
    Matched,
    Failed,
    // Init
    InitQuery,
    InitOpAttr,
    InitGetVersion,
    InitDone,
    InitOther,
    InitLumissil,
    InitQualcomm,
    // Reset
    ResetInit,
    ResetMsgSent,
    ResetMsgValid,
    ResetExitResetChip,
    ResetExitIdle,
    // ResetChip
    ResetChipDelay,
    ResetChipSent,
    ResetChipReceived,
    ResetChipDone,
    // Matching
    MatchingInit,
    MatchingListen,
    MatchingPipe,
    MatchingExitMatched,
    MatchingExitFailed,
    // Session (one per CM_SLAC_PARM.REQ in Matching)
    SessionWaitStartAtten,
    SessionSounding,
    SessionFinalizeSounding,
    SessionWaitAttenRsp,
    SessionWaitSlacMatch,
    SessionMatchComplete,
    SessionFailed,
    // WaitForLink
    WaitForLinkInit,
    WaitForLinkLumissil,
    WaitForLinkQualcomm,
    WaitForLinkExitFailed,
    WaitForLinkExitMatched,
    // Matched
    MatchedInit,
    MatchedLumissil,
    MatchedQualcomm,
    MatchedNoDetect,
    MatchedExitFailed,
};

class SlacMachine;

struct TopTag {};
using TopState = State<TopTag>;
using TopStatePtr = TopState::ContainerType;

// Inner states of the sub-machines all share one base type; their ids keep them apart.
struct InnerTag {};
using InnerState = State<InnerTag>;
using InnerRegion = Region<InnerState>;

// A state of the given id and name with no behaviour of its own.
template <class Base, StateId Id> struct NamedState : Base {
    explicit NamedState(char const* name_) : state_name(name_) {
    }
    char const* name() const override {
        return state_name;
    }
    int id() const override {
        return static_cast<int>(Id);
    }

private:
    char const* state_name;
};

// Exit point of a sub-machine: entering it forwards the triggering event, as the event type the
// exit point declares, to the top-level machine (see fsm_helpers.hpp).
template <StateId Id> struct ExitPoint : NamedState<InnerState, Id> {
    ExitPoint(SlacMachine& machine_, char const* name_, EventId forwarded_, Event const& trigger_) :
        NamedState<InnerState, Id>(name_), machine(machine_), forwarded{trigger_} {
        forwarded.id = forwarded_;
    }
    void enter() override;

private:
    SlacMachine& machine;
    QueuedEvent forwarded;
};

// Sub-machine data that lives as long as the machine (it is kept across leaving and re-entering
// the sub-machine, like the members of a sub-machine definition).
struct ResetData {
    Nmk pending_nmk{};
    int set_key_attempts{0};
    timer set_key_timer;
};

class Session;

struct MatchingData {
    timer to;
    bool failed_matching_reset_once{false};
    fsm::evse::ValidateHandler validate; // CM_VALIDATE (ISO 15118-3 9.4) BCB-toggle validation
    std::vector<std::unique_ptr<Session>> sessions;

    MatchingData();
    ~MatchingData();
};

struct WaitForLinkData {
    std::chrono::milliseconds link_check_to{0};
    timer to;
};

struct MatchedData {
    std::chrono::milliseconds link_check_to{0};
    int consecutive_neg_link_status{0};
    int neg_link_status_threshold{1};
    fsm::evse::AmpMapHandler amp_map; // SECC-initiated CM_AMP_MAP exchange (ISO 15118-3 A.9.6)
    // Set when Matched is left because the link was lost (the Matched_Fail transition), whose
    // action runs once the sub-machine has been exited.
    bool leaving_on_link_loss{false};
};

class SlacMachine {
public:
    explicit SlacMachine(fsm::evse::Context& ctx);
    // Destroying the machine leaves no state: the exit hooks see shutting_down and skip their
    // side effects (only stop() leaves the active states).
    ~SlacMachine();
    bool shutting_down() const {
        return destroying;
    }

    // Enters the initial state; stop() leaves the active states, sub-machines first.
    void start();
    void stop();

    // Processes one event to completion, including the events queued by exit points meanwhile.
    void process(Event const& e);

    // Exit points queue their event here.
    void forward(QueuedEvent e);

    void traverse(Visitor& visitor) const;

    fsm::evse::Context& ctx;
    ResetData reset_data;
    MatchingData matching_data;
    WaitForLinkData wait_for_link_data;
    MatchedData matched_data;

private:
    void process_one(Event const& e);
    void log_no_transition(Event const& e);

    std::deque<QueuedEvent> queue;
    bool destroying{false};
    Region<TopState> top; // last, so that it is destroyed first
};

inline bool reacts_to_any(EventId e, std::initializer_list<EventId> events) {
    for (auto const event : events) {
        if (event == e) {
            return true;
        }
    }
    return false;
}

template <StateId Id> void ExitPoint<Id>::enter() {
    machine.forward(forwarded);
}

// Top-level states, one factory each.
TopStatePtr make_init(SlacMachine& machine);
TopStatePtr make_reset(SlacMachine& machine);
TopStatePtr make_reset_chip(SlacMachine& machine);
TopStatePtr make_idle(SlacMachine& machine);
TopStatePtr make_matching(SlacMachine& machine);
TopStatePtr make_wait_for_link(SlacMachine& machine);
TopStatePtr make_matched(SlacMachine& machine);
TopStatePtr make_failed(SlacMachine& machine);

// Common base of the top-level states.
template <StateId Id> struct TopLevelState : NamedState<TopState, Id> {
    TopLevelState(SlacMachine& machine_, char const* name_) :
        NamedState<TopState, Id>(name_), machine(machine_), ctx(machine_.ctx) {
    }

protected:
    SlacMachine& machine;
    fsm::evse::Context& ctx;
};

// Common base of the inner states of a sub-machine.
template <StateId Id> struct InnerLevelState : NamedState<InnerState, Id> {
    InnerLevelState(SlacMachine& machine_, char const* name_) :
        NamedState<InnerState, Id>(name_), machine(machine_), ctx(machine_.ctx) {
    }

protected:
    SlacMachine& machine;
    fsm::evse::Context& ctx;
};

} // namespace everest::lib::slac::sm::evse
