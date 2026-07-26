// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <dc_hlc_limits.hpp>

namespace module {

using EvseState = Charger::EvseState;
using HlcTerminatePause = Charger::HlcTerminatePause;

TEST(DcHlcLimitsTest, update_allowed_during_active_session) {
    for (const auto state : {EvseState::WaitingForAuthentication, EvseState::PrepareCharging, EvseState::Charging,
                             EvseState::ChargingPausedEV, EvseState::ChargingPausedEVSE}) {
        EXPECT_TRUE(should_update_dc_hlc_limits(state, HlcTerminatePause::Unknown));
        EXPECT_TRUE(should_update_dc_hlc_limits(state, HlcTerminatePause::Pause));
    }
}

// Regression test: after SessionStop the HLC stack sends D-LINK_TERMINATE while the car may stay
// plugged in for a long time with the charger in ChargingPausedEV. Stale limits (e.g. an old
// target_voltage) must not be pushed to the HLC stack anymore in this phase.
TEST(DcHlcLimitsTest, no_update_after_dlink_terminate) {
    for (const auto state : {EvseState::WaitingForAuthentication, EvseState::PrepareCharging, EvseState::Charging,
                             EvseState::ChargingPausedEV, EvseState::ChargingPausedEVSE, EvseState::StoppingCharging,
                             EvseState::Finished, EvseState::Idle}) {
        EXPECT_FALSE(should_update_dc_hlc_limits(state, HlcTerminatePause::Terminate));
    }
}

TEST(DcHlcLimitsTest, no_update_outside_of_session) {
    for (const auto state : {EvseState::Disabled, EvseState::Idle, EvseState::StoppingCharging, EvseState::Finished,
                             EvseState::T_step_EF, EvseState::T_step_X1, EvseState::SwitchPhases}) {
        EXPECT_FALSE(should_update_dc_hlc_limits(state, HlcTerminatePause::Unknown));
    }
}

} // namespace module
