// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include "../StateBase.hpp"

namespace module {

class V2GNegotiating final : public StateBase {
public:
    using StateBase::StateBase;
    void enter() override;
    Result feed(EventType ev) override;
    API_types::ev_simulator::FsmState get_id() const override {
        return API_types::ev_simulator::FsmState::V2GNegotiating;
    }
};

} // namespace module
