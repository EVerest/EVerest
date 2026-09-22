// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <vector>

#include <iso15118/message/supported_app_protocol.hpp>

#include "../context.hpp"
#include "../states.hpp"

namespace iso15118::ev::d20::state {

struct SupportedAppProtocol : public StateBase {
    SupportedAppProtocol(Context& ctx) : StateBase(ctx, StateID::SupportedAppProtocol) {
    }

    // Advertises the leading protocols that fit the request; the rest are left out.
    static message_20::SupportedAppProtocolRequest
    make_request(const std::vector<message_20::SupportedAppProtocol>& advertised);

    void enter() final;

    Result feed(Event) final;
};

} // namespace iso15118::ev::d20::state
