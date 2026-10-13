// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <generated/types/board_support_common.hpp>
#include <generated/types/ev_board_support.hpp>
#include <optional>
#include <vector>

namespace module {

class PilotGate {
public:
    struct Out {
        std::vector<types::board_support_common::BspEvent> to_car_events;
        std::optional<types::ev_board_support::EvCpState> to_board_cp;
    };
    explicit PilotGate(bool plugged);
    Out plug();
    Out unplug();
    Out on_board_reset() const;
    Out on_board_event(const types::board_support_common::BspEvent& event);
    Out on_car_set_cp(types::ev_board_support::EvCpState cp);
    bool passed() const;

private:
    bool m_passed;
};

} // namespace module
