// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include "../StateBase.hpp"

namespace module {

class SlacMatching final : public StateBase {
public:
    using StateBase::StateBase;
    void enter() override;
    API_types::ev_controller::FsmState get_id() const override {
        return API_types::ev_controller::FsmState::SlacMatching;
    }

protected:
    Result on_event(EventType ev) override;

private:
    void start_matching();
    bool m_wake_pulse{false};
    // The provider reported MATCHING since the last trigger, so its next
    // UNMATCHED is a failed match rather than the echo of a reset.
    bool m_matching_seen{false};
    // The pilot is B, C or D, where [V2G3-A09-123] allows matching again.
    bool m_pilot_allows_matching{true};
    // A match failed while the pilot did not allow another; retry on its return.
    bool m_retry_pending{false};
};

} // namespace module
