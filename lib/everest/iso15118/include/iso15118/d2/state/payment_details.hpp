// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message_2/payment_details.hpp>

#include "../states.hpp"

namespace iso15118::d2::state {

// Validates the contract chain and generates the GenChallenge [V2G2-475]. Shared by the two nodes
// that accept a PaymentDetailsReq.
Result process_payment_details(Context& ctx, const message_2::PaymentDetailsRequest& req);

// [V2G2-554]/[V2G2-557]/[V2G2-558]: entered once the EV has used up its optional certificate excursion.
struct PaymentDetails : public StateBase {
    PaymentDetails(Context& ctx) : StateBase(ctx, StateID::PaymentDetails) {
    }

    void enter() final;
    Result on_request(const message_2::Variant& received) final;
};

} // namespace iso15118::d2::state
