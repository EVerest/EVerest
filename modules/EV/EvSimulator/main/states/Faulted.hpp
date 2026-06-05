// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include "../StateBase.hpp"

namespace module {

class Faulted final : public StateBase {
public:
    using StateBase::StateBase;
    void enter() override;
    Result feed(EventType ev) override;
    API_types::ev_simulator::FsmState get_id() const override {
        return API_types::ev_simulator::FsmState::Faulted;
    }

protected:
    void on_leave() override;
};

} // namespace module
