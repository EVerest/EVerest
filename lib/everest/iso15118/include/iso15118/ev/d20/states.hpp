// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include "context.hpp"

#include <iso15118/ev/session/fsm.hpp>

namespace iso15118::ev::d20 {

// Kept separate from the SECC states: the Context and timeout events differ.
// Result carries a Disposition the Session enforces.

class Context;

using ev::session::Disposition;
using ev::session::Event;

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
    AC_DER_SAE_ChargeParameterDiscovery,
    AC_DER_SAE_ChargeLoop,
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
using Result = ev::session::BasicResult<StateBase>;

struct StateBase : ev::session::BasicStateBase<Context, StateID, StateBase> {
    using BasicStateBase::BasicStateBase;
};

} // namespace iso15118::ev::d20
