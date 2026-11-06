// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/ev/d20/energy_request.hpp>
#include <iso15118/ev/params/ac.hpp>
#include <iso15118/message/ac_charge_loop.hpp>

#include "../states.hpp"

namespace iso15118::ev::d20::state {

struct AC_ChargeLoop : public StateBase {
public:
    AC_ChargeLoop(Context& ctx) : StateBase(ctx, StateID::AC_ChargeLoop) {
    }

    static message_20::AC_ChargeLoopRequest make_request(const params::AcChargeParams& params,
                                                         message_20::datatypes::ServiceCategory service,
                                                         message_20::datatypes::AcConnector connector,
                                                         message_20::datatypes::ControlMode control_mode,
                                                         const EnergyRequest& energy);
    // Each round's request, read from the live params and the session's selection.
    static message_20::AC_ChargeLoopRequest make_request(const Context& ctx);

    void enter() final;
    Result feed(Event) final;
};

} // namespace iso15118::ev::d20::state
