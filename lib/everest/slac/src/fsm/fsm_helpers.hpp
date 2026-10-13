// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

// Building blocks shared by the EVSE and EV SLAC state machines, which are written on top of
// fsm::v2 (everest/util/fsm/fsm.hpp, namespace ::fsm::v2).
//
// Execution model (kept identical to the previous Boost.MSM implementation):
// - A sub-machine is a state that owns one or more regions, each a flat fsm::v2::FSM. An event is
//   offered to every region first, in region order; only when no region handles it does the
//   sub-machine's own (outer) transition table see it.
// - Rows sharing a source and an event are tried in reverse table order. Every table keeps them
//   mutually exclusive, so nothing may rely on that order.
// - The completion event stands for the anonymous (eventless) transitions: it is fed after every
//   handled event, and on entry of a sub-machine, until no further transition fires.
// - Entering an exit point queues its event at the top-level machine, which processes the queue
//   once the current event, completion transitions included, has been dealt with. The outer
//   table then picks up the exit through the exit point being the region's active state.
// - Data of a sub-machine lives outside the state objects and survives leaving and re-entering
//   the sub-machine; state objects only hold their own timers.

#pragma once

#include <everest/slac/HomeplugMessage.hpp>
#include <everest/slac/timer.hpp>
#include <everest/util/fsm/fsm.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace everest::lib::slac::sm {

// Events
enum class EventId : std::uint8_t {
    update,
    message,
    reset,
    enter_bcd,
    leave_bcd,
    trigger_matching,
    completion, // anonymous transitions, see above
};

struct Event {
    EventId id;
    messages::HomeplugMessage const* payload{nullptr}; // set for EventId::message

    bool is(EventId other) const {
        return id == other;
    }
};

// An event kept for later, owning its payload (exit point forwarding).
struct QueuedEvent {
    EventId id;
    std::optional<messages::HomeplugMessage> payload;

    explicit QueuedEvent(Event const& e) : id(e.id) {
        if (e.payload != nullptr) {
            payload = *e.payload;
        }
    }
    Event view() const {
        return {id, payload ? &*payload : nullptr};
    }
};

struct Node;

// Walks the active states, the sub-machines' regions and the dynamic sessions.
struct Visitor {
    virtual ~Visitor() = default;
    virtual void on_active_state(Node const&, std::size_t /*depth*/) {
    }
    virtual void on_submachine_start(std::size_t /*depth*/) {
    }
    virtual void on_submachine_end(std::size_t /*depth*/) {
    }
    virtual void on_dynamic_sessions_size(std::size_t) {
    }
    virtual void on_dynamic_session_start(int /*index*/, std::size_t /*depth*/) {
    }
    virtual void on_dynamic_session_end(int /*index*/, std::size_t /*depth*/) {
    }
};

// What every state exposes besides fsm::v2's enter/feed/leave.
struct Node {
    virtual ~Node() = default;
    // Short type name, as reported in the state printout and the FSM telemetry.
    virtual char const* name() const = 0;
    // Stable id per state type: equal ids mean the same state, also across a self-transition.
    virtual int id() const = 0;
    virtual void deadlines(earliest_deadline&) const {
    }
    // Whether the state has transitions for the event at all, taken or not (their guards may
    // reject it). Of a sub-machine, this includes its regions' active states. An event no active
    // state reacts to is reported as ignored; exit point transitions do not count.
    virtual bool reacts_to(EventId) const {
        return false;
    }
    virtual bool is_submachine() const {
        return false;
    }
    virtual void traverse_children(Visitor&, std::size_t /*depth*/) const {
    }
};

// fsm::v2 state base. Tag only keeps the state types of different machines apart.
template <class Tag> struct State : Node {
    using ContainerType = std::unique_ptr<State>;
    using EventType = Event;

    struct Result {
        bool unhandled{true};
        ContainerType new_state{nullptr};
    };

    static Result handled() {
        return {false, nullptr};
    }
    static Result transition(ContainerType target) {
        return {false, std::move(target)};
    }
    static Result unhandled() {
        return {};
    }

    virtual void enter() {
    }
    virtual Result feed(Event const&) {
        return unhandled();
    }
    virtual void leave() {
    }
    int get_id() const {
        return id();
    }
};

// One region of a (sub-)machine: an fsm::v2::FSM that can be entered and left as a whole.
template <class Base> class Region {
public:
    void start(typename Base::ContainerType initial) {
        fsm.reset();
        fsm.emplace(std::move(initial));
    }
    void stop() {
        fsm.reset();
    }
    bool active() const {
        return fsm.has_value();
    }
    // True if the event was handled (by a transition or an internal one).
    bool feed(Event const& e) {
        return static_cast<bool>(fsm->feed(e));
    }
    // Anonymous transitions, until none fires any more.
    void complete() {
        while (feed(Event{EventId::completion})) {
        }
    }
    int current_id() const {
        return fsm->get_current_state_id();
    }
    Base const& current() const {
        return fsm->get_current_state();
    }
    void traverse(Visitor& visitor, std::size_t depth) const {
        auto const& state = current();
        visitor.on_active_state(state, depth);
        if (state.is_submachine()) {
            visitor.on_submachine_start(depth);
            state.traverse_children(visitor, depth + 1);
            visitor.on_submachine_end(depth);
        }
    }

private:
    std::optional<::fsm::v2::FSM<Base>> fsm;
};

// A state's own timer, armed for `duration` on entry.
struct StateTimer {
    timer to;
    std::chrono::milliseconds duration{0};

    void arm(timer::tp now) {
        to.arm(now, duration);
    }
    bool state_timeout(timer::tp now) const {
        return to.expired(now);
    }
    void deadlines(earliest_deadline& next) const {
        next.offer(to);
    }
};

inline bool is_message_of_type(Event const& e, std::uint16_t mmtype) {
    return e.is(EventId::message) and e.payload->is_valid() and e.payload->get_mmtype() == mmtype;
}

// How a throw gets out of the machines: the contexts never let a consumer callback throw into
// the machine. A publisher, sender or logger that throws is parked in Context::caught_exception,
// the call counts as "not done", the transition completes, and the wrapper calls
// rethrow_recorded as soon as the event has been processed, so the failure surfaces from
// message()/update()/reset() on a machine that is still consistent and still takes the
// teardown's reset. Anything else that throws inside a transition (a bug) propagates.
template <class Ctx> void rethrow_recorded(Ctx& ctx) {
    if (ctx.caught_exception) {
        std::exception_ptr caught;
        std::swap(caught, ctx.caught_exception);
        std::rethrow_exception(caught);
    }
}

// Writes the signature over a stored one, entry by entry, and notes whether anything differed. The
// stored vector's capacity is reused, so comparing allocates nothing once it has grown.
struct SignatureUpdate : Visitor {
    explicit SignatureUpdate(std::vector<int>& sig_) : sig(sig_) {
    }
    void on_active_state(Node const& state, std::size_t) override {
        put(state.id());
    }
    void on_dynamic_sessions_size(std::size_t size) override {
        put(static_cast<int>(size));
    }
    bool finish() {
        if (pos != sig.size()) {
            sig.resize(pos);
            changed = true;
        }
        return changed;
    }

private:
    void put(int value) {
        if (pos == sig.size()) {
            sig.push_back(value);
            changed = true;
        } else if (sig[pos] != value) {
            sig[pos] = value;
            changed = true;
        }
        ++pos;
    }
    std::vector<int>& sig;
    std::size_t pos{0};
    bool changed{false};
};

struct DeadlineVisitor : Visitor {
    explicit DeadlineVisitor(timer::tp now) : next(now) {
    }
    earliest_deadline next;
    void on_active_state(Node const& state, std::size_t) override {
        state.deadlines(next);
    }
};

// The signature is the list of active-state ids of the machine, its sub-machines and sessions:
// equal signatures, same states. Brings `signature` up to date and tells whether it changed.
template <class Machine> bool update_signature(Machine const& machine, std::vector<int>& signature) {
    SignatureUpdate v{signature};
    machine.traverse(v);
    return v.finish();
}

// Wait from `now` until the earliest deadline an active state or its handlers holds, or nothing.
template <class Machine> std::optional<timer::tick> next_wakeup_of(Machine const& machine, timer::tp now) {
    DeadlineVisitor v{now};
    machine.traverse(v);
    return v.next.wait();
}

// Feeds `update` until it moves no state any more, so what the last event made true is acted on
// now instead of at a later wake-up. Time stays frozen meanwhile, which rules out timer-driven
// cycles; a guard-driven one would be a table bug and is reported rather than spun on.
template <class Machine> void settle(Machine& machine) {
    std::vector<int> signature;
    signature.reserve(16); // the active states plus the sessions fit, so one allocation per call
    update_signature(machine, signature);
    for (int round = 0; round < 32; ++round) {
        machine.process(Event{EventId::update});
        // A callback failure parked during that update surfaces now, before another round runs and
        // before a machine that then fails to settle could mask it with its own logic_error.
        rethrow_recorded(machine.ctx);
        if (not update_signature(machine, signature)) {
            return;
        }
    }
    throw std::logic_error("SLAC state machine does not settle");
}

} // namespace everest::lib::slac::sm
