// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include "../StateBase.hpp"

#include <chrono>

namespace module {

class Unplugged final : public StateBase {
public:
    // How long CP stays A before a replug sets B, so a BSP that samples CP
    // periodically (YetiSimulator does) reports the unplug to the EVSE.
    static constexpr std::chrono::milliseconds replug_dwell{1000};

    // `replug`: plug again once replug_dwell has passed. A restore of a
    // persisted plug replugs the same way without being asked.
    explicit Unplugged(FsmContext& ctx, bool replug = false) : StateBase(ctx), replug_(replug) {
    }
    void enter() override;
    Result feed(EventType ev) override;
    API_types::ev_simulator::FsmState get_id() const override {
        return API_types::ev_simulator::FsmState::Unplugged;
    }

private:
    bool replug_;
};

} // namespace module
