// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include "../states.hpp"
#include <iso15118/d20/ac_powers.hpp>

namespace iso15118::d20::state {
struct AC_DER_IEC_ChargeParameterDiscovery : public StateBase {
    AC_DER_IEC_ChargeParameterDiscovery(Context& ctx) : StateBase(ctx, StateID::AC_DER_IEC_ChargeParameterDiscovery) {
    }

    void enter() final;

    Result feed(Event) final;

private:
    AcPresentPower present_powers;
};

} // namespace iso15118::d20::state
