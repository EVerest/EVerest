// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include "../StateBase.hpp"

namespace module {

class Plugged final : public StateBase {
public:
    // charger_restart: the charger ended the session and will wake the EV with
    // PWM, which restarts it with SLAC.
    explicit Plugged(FsmContext& ctx_, bool charger_restart = false) :
        StateBase(ctx_), m_restart(charger_restart ? ChargerRestart::Settle : ChargerRestart::None) {
    }
    void enter() override;
    API_types::ev_controller::FsmState get_id() const override {
        return API_types::ev_controller::FsmState::Plugged;
    }

protected:
    Result on_event(EventType ev) override;

private:
    bool m_iso_pwm_waiting{false};
    // The steps of EvManager's arm_restart_after_charger_stop
    // (car_simulation.cpp:720): settle 2 s, then wait for running PWM.
    enum class ChargerRestart {
        None,
        Settle,
        AwaitPwm,
    };
    ChargerRestart m_restart;
    bool restarts_iso() const;
};

} // namespace module
