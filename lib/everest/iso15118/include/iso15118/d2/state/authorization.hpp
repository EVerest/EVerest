// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include "../states.hpp"

namespace iso15118::d2::state {

struct Authorization : public StateBase {
    // \p challenge is present only on the Plug-and-Charge path, handed straight over by PaymentDetails
    // to the one place [V2G2-475] needs it. An EIM session never runs PaymentDetails and so has none;
    // absent is the honest representation, and it also means a missing challenge never compares equal.
    Authorization(Context& ctx, std::optional<dt::GenChallenge> challenge = std::nullopt) :
        StateBase(ctx, StateID::Authorization), gen_challenge(std::move(challenge)) {
    }

    void enter() final;
    Result on_event(Event) final;
    Result on_request(const message_2::Variant& received) final;

private:
    const std::optional<dt::GenChallenge> gen_challenge;
    bool first_req_msg{true};
    bool timeout_ongoing_reached{false};
    // "Pending" (no AuthorizationResponse yet) is distinct from "rejected" (one arrived, saying false).
    bool auth_response_received{false};
    // Local to this state: Authorization is entered once per session and never re-entered.
    bool authorized{false};
    bool certificate_revoked{false};
};

} // namespace iso15118::d2::state
