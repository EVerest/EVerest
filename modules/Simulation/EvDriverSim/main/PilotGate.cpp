// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "PilotGate.hpp"

namespace module {
PilotGate::PilotGate(bool plugged) : m_passed(plugged) {
}
bool PilotGate::passed() const {
    return m_passed;
}
PilotGate::Out PilotGate::on_car_set_cp(types::ev_board_support::EvCpState cp) {
    return {{}, m_passed ? std::make_optional(cp) : std::nullopt};
}
PilotGate::Out PilotGate::unplug() {
    m_passed = false;
    return {{{types::board_support_common::Event::Disconnected}}, types::ev_board_support::EvCpState::A};
}
PilotGate::Out PilotGate::on_board_reset() const {
    return {{}, m_passed ? std::nullopt : std::make_optional(types::ev_board_support::EvCpState::A)};
}
PilotGate::Out PilotGate::plug() {
    m_passed = true;
    return {{{types::board_support_common::Event::A}}, std::nullopt};
}
PilotGate::Out PilotGate::on_board_event(const types::board_support_common::BspEvent& event) {
    return {m_passed ? std::vector<types::board_support_common::BspEvent>{event}
                     : std::vector<types::board_support_common::BspEvent>{},
            std::nullopt};
}
} // namespace module
