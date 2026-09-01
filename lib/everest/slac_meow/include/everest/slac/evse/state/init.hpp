// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <everest/slac/evse/states.hpp>
#include <everest/slac/time.hpp>

namespace everest::slac::evse::state {

// Modem discovery: probe for a Qualcomm chip, then for a Lumissil one, then move on. The probe
// sequence is a step counter and the vendor a latch, both members of this state.
struct Init : public StateBase {
    explicit Init(Context& ctx) : StateBase(ctx, StateID::Init) {
    }

    void enter() final;
    Result feed(SlacEvent const&) final;
    void describe(StateTree& out) const final;
    void signature(std::string& out) const final;

private:
    /// At most once.
    void latch_modem_vendor(messages::HomeplugMessage const& frame);
    void arm_step_timer();

    enum class Step {
        BeforeOpAttr,
        BeforeGetVersion,
        AwaitingAnswer,
    };

    Step m_step{Step::BeforeOpAttr};
    bool m_vendor_latched{false};
    Timer m_step_timer{};
};

} // namespace everest::slac::evse::state
