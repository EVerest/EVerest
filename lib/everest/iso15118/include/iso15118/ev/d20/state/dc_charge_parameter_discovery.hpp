// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/ev/params/dc.hpp>
#include <iso15118/message/dc_charge_parameter_discovery.hpp>

#include "../states.hpp"

namespace iso15118::ev::d20::state {

struct DC_ChargeParameterDiscovery : public StateBase {
public:
    DC_ChargeParameterDiscovery(Context& ctx) : StateBase(ctx, StateID::DC_ChargeParameterDiscovery) {
    }

    static message_20::DC_ChargeParameterDiscoveryRequest make_request(const params::DcChargeParams& params,
                                                                       message_20::datatypes::ServiceCategory service);

    void enter() final;

    Result feed(Event) final;
};

} // namespace iso15118::ev::d20::state
