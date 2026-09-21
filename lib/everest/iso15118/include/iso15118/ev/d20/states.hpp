// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include "context.hpp"

#include <iso15118/ev/fsm_base.hpp>

namespace iso15118::ev::d20 {

// Mirrors iso15118/d20/states.hpp on purpose: the Context type differs and the EV has no TIMEOUT
// event. Kept as a separate header so either side can change its Result without touching the other.
// Result carries a Disposition the Session enforces.

class Context;

using ev::Disposition;
using ev::Event;

enum class StateID {
    SupportedAppProtocol,
    SessionSetup,
    AuthorizationSetup,
    Authorization,
    ServiceDetail,
    ServiceDiscovery,
    ServiceSelection,
    AC_ChargeParameterDiscovery,
    AC_ChargeLoop,
    AC_DER_IEC_ChargeParameterDiscovery,
    AC_DER_IEC_ChargeLoop,
    DC_ChargeParameterDiscovery,
    DC_PreCharge,
    DC_ChargeLoop,
    DC_WeldingDetection,
    DC_CableCheck,
    PowerDelivery,
    ScheduleExchange,
    SessionStop
};

struct StateBase;
using Result = ev::BasicResult<StateBase>;

struct StateBase : ev::BasicStateBase<Context, StateID, StateBase> {
    using BasicStateBase::BasicStateBase;
};

} // namespace iso15118::ev::d20
