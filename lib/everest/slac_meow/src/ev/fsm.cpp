// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/slac/ev/fsm.hpp>

#include <optional>

#include <everest/util/fsm/fsm.hpp>

#include <everest/slac/ev/event.hpp>
#include <everest/slac/ev/state/reset.hpp>
#include <everest/slac/ev/states.hpp>

namespace everest::slac::ev {

struct EvFSM::Impl {
    explicit Impl(Context& context) : ctx(context) {
    }

    /// Stamp the instant this event happened, then hand it to the machine. Every public entry
    /// point goes through here, so nothing is ever handled without a current_time.
    void feed(SlacEvent const& event) {
        ctx.current_time = ctx.now();
        if (machine.has_value()) {
            machine->feed(event);
        }
        // one signal per logical transition, the same contract the EVSE facade has
        ctx.publish_slac_state();
    }

    Context& ctx;
    // fsm::v2::FSM enters its initial state in the constructor, but the machine is only started by
    // restart_fsm(), so it cannot be built until then.
    std::optional<fsm::v2::FSM<ev::StateBase>> machine;
};

EvFSM::EvFSM(Context& ctx) : impl(std::make_unique<Impl>(ctx)) {
}

EvFSM::~EvFSM() = default;

void EvFSM::restart_fsm() {
    impl->ctx.current_time = impl->ctx.now();
    impl->machine.reset();
    impl->machine.emplace(impl->ctx.create_state<ev::state::Reset>());
}

void EvFSM::reset() {
    impl->feed(event::Reset{});
}

void EvFSM::trigger_matching() {
    impl->feed(event::TriggerMatching{});
}

void EvFSM::message(messages::HomeplugMessage msg) {
    impl->feed(event::Message{msg});
}

void EvFSM::update() {
    impl->feed(event::Update{});
}

} // namespace everest::slac::ev
