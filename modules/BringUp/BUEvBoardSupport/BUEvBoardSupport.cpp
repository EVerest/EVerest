// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "BUEvBoardSupport.hpp"

#include "ftxui/dom/table.hpp"
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <fmt/core.h>

using namespace ftxui;

namespace module {

void BUEvBoardSupport::init() {
}

void BUEvBoardSupport::ready() {
    auto screen = ScreenInteractive::Fullscreen();

    r_bsp->subscribe_bsp_event([this, &screen](const types::board_support_common::BspEvent e) {
        {
            std::scoped_lock lock(data_mutex);
            bsp_event = types::board_support_common::event_to_string(e.event);
        }
        screen.PostEvent(Event::Custom);
    });

    r_bsp->subscribe_bsp_measurement([this, &screen](const types::board_support_common::BspMeasurement m) {
        {
            std::scoped_lock lock(data_mutex);
            cp_pwm_duty_cycle = fmt::format("{:.2f} %", m.cp_pwm_duty_cycle);
            proximity_pilot = types::board_support_common::ampacity_to_string(m.proximity_pilot.ampacity);
            if (m.rcd_current_mA.has_value()) {
                rcd_current = fmt::format("{:.1f} mA", m.rcd_current_mA.value());
            }
        }
        screen.PostEvent(Event::Custom);
    });

    auto error_handler = [this, &screen](const Everest::error::Error& error) {
        {
            std::scoped_lock lock(this->data_mutex);
            this->last_error_raised = fmt::format("{}, {}", error.type, error.sub_type);
        }
        screen.PostEvent(Event::Custom);
    };

    auto error_cleared_handler = [this, &screen](const Everest::error::Error& error) {
        {
            std::scoped_lock lock(this->data_mutex);
            this->last_error_cleared = fmt::format("{}, {}", error.type, error.sub_type);
        }
        screen.PostEvent(Event::Custom);
    };

    r_bsp->subscribe_all_errors(error_handler, error_cleared_handler);

    std::string last_command = "None";

    // -------------------------------------------------------------------
    // Left column (Var Display)
    // -------------------------------------------------------------------
    auto data_renderer = Renderer([&] {
        std::vector<std::vector<std::string>> table_content;

        {
            std::scoped_lock lock(data_mutex);
            table_content = {
                {"BSP Event (CP State)", bsp_event},
                {"CP PWM Duty Cycle", cp_pwm_duty_cycle},
                {"Proximity Pilot", proximity_pilot},
                {"RCD Current", rcd_current},
                {"Last error raised", last_error_raised},
                {"Last error cleared", last_error_cleared},
            };
        }

        auto table = Table(table_content);

        table.SelectAll().Border(LIGHT);
        table.SelectColumn(0).Border(LIGHT);
        for (int i = 0; i < (int)table_content.size(); ++i)
            table.SelectRow(i).Border(LIGHT);

        return vbox({
                   window(text("Module Data"), vbox({
                                                   text("Last command: " + last_command),
                                                   table.Render(),
                                               })) |
                       size(WIDTH, EQUAL, 55),
               }) |
               flex_grow;
    });

    // -------------------------------------------------------------------
    // Right column (Command Buttons)
    // -------------------------------------------------------------------

    auto make_cp_state_button = [&](std::string label, types::ev_board_support::EvCpState cp_state) {
        return Button("CP State " + label, [&, label, cp_state] {
            last_command = "CP State " + label;
            r_bsp->call_set_cp_state(cp_state);
        });
    };

    auto button_cp_a = make_cp_state_button("A", types::ev_board_support::EvCpState::A);
    auto button_cp_b = make_cp_state_button("B", types::ev_board_support::EvCpState::B);
    auto button_cp_c = make_cp_state_button("C", types::ev_board_support::EvCpState::C);
    auto button_cp_d = make_cp_state_button("D", types::ev_board_support::EvCpState::D);
    auto button_cp_e = make_cp_state_button("E", types::ev_board_support::EvCpState::E);

    auto button_enable = Button("Enable", [&] {
        last_command = "Enable";
        r_bsp->call_enable(true);
    });
    auto button_disable = Button("Disable", [&] {
        last_command = "Disable";
        r_bsp->call_enable(false);
    });

    auto button_power_on = Button("Allow power on", [&] {
        last_command = "Allow power on";
        r_bsp->call_allow_power_on(true);
    });
    auto button_power_off = Button("Disallow power on", [&] {
        last_command = "Disallow power on";
        r_bsp->call_allow_power_on(false);
    });

    auto button_diode_fail = Button("Diode fail", [&] {
        last_command = "Diode fail";
        r_bsp->call_diode_fail(true);
    });
    auto button_diode_ok = Button("Diode ok", [&] {
        last_command = "Diode ok";
        r_bsp->call_diode_fail(false);
    });

    std::string max_current_str{"16.0"};
    InputOption max_current_opt;
    max_current_opt.multiline = false;
    max_current_opt.on_enter = [&]() {
        if (max_current_str.empty())
            max_current_str = "16.0";
        last_command = "Set AC max current (" + max_current_str + " A)";
        r_bsp->call_set_ac_max_current(std::stod(max_current_str));
    };
    auto max_current_input = Input(&max_current_str, "16.0", max_current_opt);
    auto button_max_current = Button("Set AC max current", [&] {
        if (max_current_str.empty())
            max_current_str = "16.0";
        last_command = "Set AC max current (" + max_current_str + " A)";
        r_bsp->call_set_ac_max_current(std::stod(max_current_str));
    });

    auto button_three_phases = Button("Three phases", [&] {
        last_command = "Three phases";
        r_bsp->call_set_three_phases(true);
    });
    auto button_single_phase = Button("Single phase", [&] {
        last_command = "Single phase";
        r_bsp->call_set_three_phases(false);
    });

    auto command_container = Container::Vertical({
        Container::Horizontal({button_cp_a, button_cp_b, button_cp_c, button_cp_d, button_cp_e}),
        Container::Horizontal({button_enable, button_disable}),
        Container::Horizontal({button_power_on, button_power_off}),
        Container::Horizontal({button_diode_fail, button_diode_ok}),
        Container::Horizontal({button_max_current, max_current_input}),
        Container::Horizontal({button_three_phases, button_single_phase}),
    });

    auto command_renderer = Renderer(command_container, [&] {
        return vbox({
                   text("Commands") | bold | center,
                   separator(),
                   hbox({button_cp_a->Render(), button_cp_b->Render(), button_cp_c->Render(), button_cp_d->Render(),
                         button_cp_e->Render()}),
                   hbox({button_enable->Render(), button_disable->Render()}),
                   hbox({button_power_on->Render(), button_power_off->Render()}),
                   hbox({button_diode_fail->Render(), button_diode_ok->Render()}),
                   hbox({button_max_current->Render(), max_current_input->Render() | size(WIDTH, EQUAL, 10) | border}),
                   hbox({button_three_phases->Render(), button_single_phase->Render()}),
               }) |
               border | size(WIDTH, EQUAL, 60);
    });

    // -------------------------------------------------------------------
    // Combine columns
    // -------------------------------------------------------------------
    auto layout = Container::Horizontal({
        data_renderer,
        command_renderer,
    });

    auto main_renderer = Renderer(layout, [&] {
        return vbox({
            text("EV Board Support BringUp") | bold | center,
            separator(),
            hbox({
                data_renderer->Render(),
                command_renderer->Render(),
            }),
        });
    });

    screen.Loop(main_renderer);
}

void BUEvBoardSupport::shutdown() {
}

} // namespace module
