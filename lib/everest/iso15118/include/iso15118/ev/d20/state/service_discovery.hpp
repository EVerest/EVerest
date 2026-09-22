// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message/service_discovery.hpp>

#include "../states.hpp"

namespace iso15118::ev::d20::state {

struct ServiceDiscovery : public StateBase {
public:
    ServiceDiscovery(Context& ctx) : StateBase(ctx, StateID::ServiceDiscovery) {
    }

    static message_20::ServiceDiscoveryRequest make_request(message_20::datatypes::ServiceCategory service);

    void enter() final;

    Result feed(Event) final;
};

} // namespace iso15118::ev::d20::state
