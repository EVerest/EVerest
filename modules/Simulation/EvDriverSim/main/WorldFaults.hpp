// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <everest_api_types/ev_driver_sim/API.hpp>
#include <generated/types/board_support_common.hpp>
#include <generated/types/ev_board_support.hpp>
#include <optional>
#include <string>

namespace module {
namespace API_driver = everest::lib::API::V1_0::types::ev_driver_sim;

class WorldFaults {
public:
    struct Out {
        std::optional<bool> diode_fail;
        std::optional<double> rcd_mA;
        std::optional<types::ev_board_support::EvCpState> to_board_cp;
        std::optional<std::string> rejection;
    };
    Out inject(const API_driver::InjectFaultParams& params, bool plugged);
    types::ev_board_support::EvCpState on_car_set_cp(types::ev_board_support::EvCpState cp);
    Out on_car_diode_fail(bool value);
    Out on_car_set_rcd_error(double current_mA);
    Out clear();
    Out reapply();
    void on_board_event(const types::board_support_common::BspEvent& event);
    Out on_board_measurement() const;
    void unplug();
    std::optional<API_driver::WorldFault> active() const;

private:
    std::optional<API_driver::InjectFaultParams> m_active;
    types::ev_board_support::EvCpState m_car_cp{types::ev_board_support::EvCpState::A};
    bool m_car_diode_fail{false};
    // YetiSimulator drops CP E while it forces F, and only an applied E is echoed.
    bool m_e_echoed{false};
    // YetiSimulator's default RCD current.
    double m_car_rcd_mA{0.1};
};
} // namespace module
