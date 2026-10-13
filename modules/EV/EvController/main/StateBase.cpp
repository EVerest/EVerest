// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "StateBase.hpp"

#include "FsmContext.hpp"
#include "states/ChargerFault.hpp"
#include "states/Unplugged.hpp"

#include <generated/types/board_support_common.hpp>

#include <optional>
#include <string>
#include <utility>

namespace module {

StateBase::Result StateBase::feed(EventType ev) {
    std::optional<bool> presence;
    if (const auto* p = std::get_if<BspEventPayload>(&ev.payload)) {
        using Pilot = ::types::board_support_common::Event;
        switch (p->bsp_event.event) {
        case Pilot::A:
        case Pilot::B:
        case Pilot::C:
        case Pilot::D:
            presence = true;
            break;
        case Pilot::Disconnected:
            presence = false;
            break;
        // F is the charger signalling a fault; E is a pilot at 0 V, the charger
        // off or the pilot shorted. Neither says whether the EV is plugged, so
        // presence stays as it was.
        case Pilot::E:
        case Pilot::F:
        case Pilot::PowerOn:
        case Pilot::PowerOff:
            break;
        }
    }
    // The handler sees the previous presence for edge detection; the next
    // state's enter sees the updated presence, including on disconnect.
    auto result = on_event(std::move(ev));
    if (presence) {
        ctx.pilot_present = *presence;
    }
    return result;
}

void StateBase::leave() {
    on_leave();
    ctx.cancel_timer();
}

StateBase::Result StateBase::reject(const Event& ev, std::string_view reason) {
    ctx.publish_e2m_command_ack(std::string{command_verb(kind_of(ev))}, std::string{reason});
    return {false, nullptr};
}

StateBase::Result StateBase::handle_pilot_event(const Event& ev) {
    const auto& p = std::get<BspEventPayload>(ev.payload);
    if (::types::board_support_common::event_to_string(p.bsp_event.event) == "Disconnected") {
        return {false, std::make_unique<Unplugged>(ctx)};
    }
    if (is_charger_fault(p.bsp_event.event)) {
        return handle_charger_fault(p.bsp_event.event);
    }
    return {true, nullptr};
}

bool StateBase::is_charger_fault(::types::board_support_common::Event pilot) {
    return pilot == ::types::board_support_common::Event::E || pilot == ::types::board_support_common::Event::F;
}

StateBase::Result StateBase::handle_charger_fault(::types::board_support_common::Event pilot) {
    ctx.iso_abort_charging();
    ctx.allow_power_on(false);
    ctx.set_cp(::types::ev_board_support::EvCpState::B);
    ctx.slac_reset();
    ctx.publish_e2m_fault(API_types::ev_controller::FaultReport{
        API_types::ev_controller::FaultType::ChargerFault,
        "pilot " + ::types::board_support_common::event_to_string(pilot), std::nullopt});
    return {false, std::make_unique<ChargerFault>(ctx)};
}

} // namespace module
