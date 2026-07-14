// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "StateBase.hpp"

#include "FsmContext.hpp"
#include "states/Unplugged.hpp"

#include <generated/types/board_support_common.hpp>

#include <string>

namespace module {

void StateBase::leave() {
    on_leave();
    ctx.cancel_timer();
}

StateBase::Result StateBase::reject(const Event& ev, std::string_view reason) {
    ctx.publish_e2m_command_ack(std::string{command_verb(kind_of(ev))}, std::string{reason});
    return {false, nullptr};
}

StateBase::Result StateBase::handle_disconnect(const Event& ev) {
    const auto& p = std::get<BspEventPayload>(ev.payload);
    if (::types::board_support_common::event_to_string(p.bsp_event.event) == "Disconnected") {
        return {false, std::make_unique<Unplugged>(ctx)};
    }
    return {true, nullptr};
}

} // namespace module
