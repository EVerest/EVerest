// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/ev/params/ac.hpp>
#include <iso15118/message/ac_charge_parameter_discovery.hpp>

#include "../states.hpp"

namespace iso15118::ev::d20::state {

struct AC_ChargeParameterDiscovery : public StateBase {
public:
    AC_ChargeParameterDiscovery(Context& ctx) : StateBase(ctx, StateID::AC_ChargeParameterDiscovery) {
    }

    static message_20::AC_ChargeParameterDiscoveryRequest make_request(const params::AcChargeParams& params,
                                                                       message_20::datatypes::AcConnector connector,
                                                                       message_20::datatypes::ServiceCategory service);

    void enter() final;

    Result feed(Event) final;
};

} // namespace iso15118::ev::d20::state
