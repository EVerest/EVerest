// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/ev/d2/context.hpp>
#include <iso15118/ev/d2/states.hpp>

namespace iso15118::ev::d2::state {

// Selects Contract (Plug & Charge) or ExternalPayment (EIM) together with the discovered ChargeService.
struct PaymentServiceSelection : public StateBase {
    PaymentServiceSelection(Context& ctx) : StateBase(ctx, StateID::PaymentServiceSelection) {
    }

    void enter() final;
    Result feed(Event) final;
};

// Requests certificate service details when installation is needed; otherwise selects payment.
BasePointerType to_service_detail_or_payment_selection(Context& ctx);

// Sends PaymentServiceSelectionReq, or SessionStopReq if no offered payment option is usable, and
// creates the state awaiting the response.
BasePointerType to_payment_service_selection(Context& ctx);

} // namespace iso15118::ev::d2::state
