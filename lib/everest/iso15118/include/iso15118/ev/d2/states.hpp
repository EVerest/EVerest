// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/ev/fsm_base.hpp>

namespace iso15118::ev::d2 {

using ev::Disposition;
using ev::Event;

class Context;

enum class StateID {
    SessionSetup,
    ServiceDiscovery,
    PaymentServiceSelection,
    CertificateInstallation,
    PaymentDetails,
    Authorization,
    ChargeParameterDiscovery,
    PowerDelivery,
    SessionStop,
    // DC branch
    CableCheck,
    PreCharge,
    CurrentDemand,
    WeldingDetection,
    // AC branch
    ChargingStatus,
    // Plug & Charge signed metering
    MeteringReceipt,
};

struct StateBase;
using Result = ev::BasicResult<StateBase>;
using BasePointerType = std::unique_ptr<StateBase>;

struct StateBase : ev::BasicStateBase<Context, StateID, StateBase> {
    using BasicStateBase::BasicStateBase;
};

} // namespace iso15118::ev::d2
