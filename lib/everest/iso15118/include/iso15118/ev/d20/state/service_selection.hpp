// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>

#include <iso15118/message/service_selection.hpp>

#include "../states.hpp"

namespace iso15118::ev::d20::state {

struct ServiceSelection : public StateBase {
public:
    ServiceSelection(Context& ctx, uint16_t parameter_set_id) :
        StateBase(ctx, StateID::ServiceSelection), m_parameter_set_id(parameter_set_id) {
    }

    static message_20::ServiceSelectionRequest make_request(message_20::datatypes::ServiceCategory service,
                                                            uint16_t parameter_set_id);

    void enter() final;

    Result feed(Event) final;

private:
    uint16_t m_parameter_set_id;
};

// Sends the ChargeParameterDiscoveryRequest of the selected service and enters its state.
Result to_charge_parameter_discovery(Context& ctx);

} // namespace iso15118::ev::d20::state
