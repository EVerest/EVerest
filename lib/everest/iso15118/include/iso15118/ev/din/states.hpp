// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/ev/fsm_base.hpp>

namespace iso15118::ev::din {

using ev::Disposition;
using ev::Event;

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
using Result = ev::BasicResult<StateBase>;
using BasePointerType = std::unique_ptr<StateBase>;

struct StateBase : ev::BasicStateBase<Context, StateID, StateBase> {
    using BasicStateBase::BasicStateBase;
};

} // namespace iso15118::ev::din
