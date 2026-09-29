// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message_2/payment_service_selection.hpp>
#include <iso15118/message_2/service_detail.hpp>

#include "../states.hpp"

namespace iso15118::d2::state {

// [V2G2-545]/[V2G2-548]: ServiceDetail is optional and repeatable, so the EV asks as often as it
// wants and ends the exchange by selecting. Both requests are answered in this node.
struct ServiceSelection : public StateBase {
    ServiceSelection(Context& ctx) : StateBase(ctx, StateID::ServiceSelection) {
    }

    void enter() final;
    Result on_request(const message_2::Variant& received) final;

private:
    Result process_service_detail(const message_2::ServiceDetailRequest& req);

    // Defined in payment_service_selection.cpp beside its builder.
    Result process_payment_selection(const message_2::PaymentServiceSelectionRequest& req);
};

} // namespace iso15118::d2::state
