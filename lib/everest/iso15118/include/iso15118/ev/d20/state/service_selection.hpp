// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include "../states.hpp"

namespace iso15118::ev::d20::state {

struct ServiceSelection : public StateBase {
public:
    ServiceSelection(Context& ctx, uint16_t parameter_set_id) :
        StateBase(ctx, StateID::ServiceSelection), m_parameter_set_id(parameter_set_id) {
    }

    void enter() final;

    Result feed(Event) final;

private:
    uint16_t m_parameter_set_id;
};

} // namespace iso15118::ev::d20::state
