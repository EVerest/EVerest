// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/ev/session/fsm.hpp>

namespace iso15118::ev::din {

using ev::session::Disposition;
using ev::session::Event;

class Context;

enum class StateID {
    SessionSetup,
    ServiceDiscovery,
    ServicePaymentSelection,
    ContractAuthentication,
    ChargeParameterDiscovery,
    CableCheck,
    PreCharge,
    PowerDelivery,
    CurrentDemand,
    WeldingDetection,
    SessionStop,
};

struct StateBase;
using Result = ev::session::BasicResult<StateBase>;
using BasePointerType = std::unique_ptr<StateBase>;

struct StateBase : ev::session::BasicStateBase<Context, StateID, StateBase> {
    using BasicStateBase::BasicStateBase;
};

} // namespace iso15118::ev::din
