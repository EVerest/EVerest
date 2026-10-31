// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/ev/session/fsm.hpp>

namespace iso15118::ev::d2 {

using ev::session::Disposition;
using ev::session::Event;

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
    ServiceDetail,
};

struct StateBase;
using Result = ev::session::BasicResult<StateBase>;
using BasePointerType = std::unique_ptr<StateBase>;

struct StateBase : ev::session::BasicStateBase<Context, StateID, StateBase> {
    using BasicStateBase::BasicStateBase;
};

} // namespace iso15118::ev::d2
