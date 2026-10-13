// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include "../StateBase.hpp"

namespace module {

class Faulted final : public StateBase {
public:
    using StateBase::StateBase;
    void enter() override;
    API_types::ev_controller::FsmState get_id() const override {
        return API_types::ev_controller::FsmState::Faulted;
    }

protected:
    Result on_event(EventType ev) override;
};

} // namespace module
