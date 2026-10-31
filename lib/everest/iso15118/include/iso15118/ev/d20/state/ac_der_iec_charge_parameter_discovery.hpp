// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/ev/params/ac.hpp>
#include <iso15118/message/ac_der_iec_charge_parameter_discovery.hpp>

#include "../states.hpp"

namespace iso15118::ev::d20::state {

struct AC_DER_IEC_ChargeParameterDiscovery : public StateBase {
public:
    AC_DER_IEC_ChargeParameterDiscovery(Context& ctx) : StateBase(ctx, StateID::AC_DER_IEC_ChargeParameterDiscovery) {
    }

    static message_20::DER_AC_ChargeParameterDiscoveryRequest
    make_request(const params::AcChargeParams& params, message_20::datatypes::AcConnector connector);

    void enter() final;

    Result feed(Event) final;
};

} // namespace iso15118::ev::d20::state
