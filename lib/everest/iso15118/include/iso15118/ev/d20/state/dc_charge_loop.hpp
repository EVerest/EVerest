// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/ev/d20/energy_request.hpp>
#include <iso15118/ev/params/dc.hpp>
#include <iso15118/message/dc_charge_loop.hpp>

#include "../states.hpp"

namespace iso15118::ev::d20::state {

struct DC_ChargeLoop : public StateBase {
public:
    DC_ChargeLoop(Context& ctx) : StateBase(ctx, StateID::DC_ChargeLoop) {
    }

    static message_20::DC_ChargeLoopRequest make_request(const params::DcChargeParams& params,
                                                         message_20::datatypes::ServiceCategory service,
                                                         message_20::datatypes::ControlMode control_mode,
                                                         const EnergyRequest& energy);
    // Each round's request, read from the live params and the session's selection.
    static message_20::DC_ChargeLoopRequest make_request(const Context& ctx);

    void enter() final;
    Result feed(Event) final;
};

} // namespace iso15118::ev::d20::state
