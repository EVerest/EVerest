// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include "../states.hpp"

namespace iso15118::din::ev::state {

// Ongoing polling loop: resend the (empty) EIM ContractAuthenticationReq until the SECC reports
// EVSEProcessing=Finished. Guarded by V2G_SECC_SequenceTimeout.
struct ContractAuthentication : public StateBase {
    ContractAuthentication(Context& ctx) : StateBase(ctx, StateID::ContractAuthentication) {
    }

    void enter() final;
    Result feed(Event) final;

private:
    bool first_request{true};
    bool ongoing_timeout_reached{false};

    void send(Event ev);
};

} // namespace iso15118::din::ev::state
