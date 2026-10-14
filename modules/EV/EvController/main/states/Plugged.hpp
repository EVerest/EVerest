// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include "../StateBase.hpp"

namespace module {

class Plugged final : public StateBase {
public:
    using StateBase::StateBase;
    void enter() override;
    API_types::ev_controller::FsmState get_id() const override {
        return API_types::ev_controller::FsmState::Plugged;
    }

protected:
    Result on_event(EventType ev) override;

private:
    bool m_iso_pwm_waiting{false};
};

} // namespace module
