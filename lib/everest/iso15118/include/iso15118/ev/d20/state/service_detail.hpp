// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message/service_detail.hpp>

#include "../states.hpp"

namespace iso15118::ev::d20::state {

struct ServiceDetail : public StateBase {
public:
    ServiceDetail(Context& ctx) : StateBase(ctx, StateID::ServiceDetail) {
    }

    static message_20::ServiceDetailRequest make_request(message_20::datatypes::ServiceCategory service);

    void enter() final;

    Result feed(Event) final;
};

} // namespace iso15118::ev::d20::state
