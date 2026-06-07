// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "BUEvBoardSupport.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/table.hpp"

using namespace ftxui;

namespace module {

static std::vector<std::vector<std::string>> to_table(const types::evse_manager::EVInfo& i) {
    std::vector<std::vector<std::string>> ev_info;

    if (i.soc.has_value()) {
        ev_info.push_back({"SoC", fmt::format("{:.1f} %", i.soc.value())});
    }
    if (i.present_voltage.has_value()) {
        ev_info.push_back({"Present Voltage", fmt::format("{:.1f} V", i.present_voltage.value())});
    }
    if (i.present_current.has_value()) {
        ev_info.push_back({"Present Current", fmt::format("{:.1f} A", i.present_current.value())});
    }
    if (i.target_voltage.has_value()) {
        ev_info.push_back({"Target Voltage", fmt::format("{:.1f} V", i.target_voltage.value())});
    }
    if (i.target_current.has_value()) {
        ev_info.push_back({"Target Current", fmt::format("{:.1f} A", i.target_current.value())});
    }
    if (i.minimum_current_limit.has_value()) {
        ev_info.push_back({"Minimum Current", fmt::format("{:.1f} A", i.minimum_current_limit.value())});
    }
    if (i.maximum_current_limit.has_value()) {
        ev_info.push_back({"Maximum Current", fmt::format("{:.1f} A", i.maximum_current_limit.value())});
    }
    if (i.maximum_voltage_limit.has_value()) {
        ev_info.push_back({"Maximum Voltage", fmt::format("{:.1f} V", i.maximum_voltage_limit.value())});
    }
    if (i.maximum_power_limit.has_value()) {
        ev_info.push_back({"Maximum Power", fmt::format("{:.1f} W", i.maximum_power_limit.value())});
    }
    if (i.estimated_time_full.has_value()) {
        ev_info.push_back({"ETA Fully Charged", i.estimated_time_full.value()});
    }
    if (i.departure_time.has_value()) {
        ev_info.push_back({"Departure Time", i.departure_time.value()});
    }
    if (i.estimated_time_bulk.has_value()) {
        ev_info.push_back({"ETA Bulk Charged", i.estimated_time_bulk.value()});
    }
    if (i.evcc_id.has_value()) {
        ev_info.push_back({"EVCC ID", i.evcc_id.value()});
    }
    if (i.remaining_energy_needed.has_value()) {
        ev_info.push_back({"Remaining Energy Needed", fmt::format("{:.1f} Wh", i.remaining_energy_needed.value())});
    }
    if (i.battery_capacity.has_value()) {
        ev_info.push_back({"Battery Capacity", fmt::format("{:.1f} Wh", i.battery_capacity.value())});
    }
    if (i.battery_full_soc.has_value()) {
        ev_info.push_back({"SoC Fully Charged", fmt::format("{:.1f}%", i.battery_full_soc.value())});
    }
    if (i.battery_bulk_soc.has_value()) {
        ev_info.push_back({"SoC Bulkd Charged", fmt::format("{:.1f}%", i.battery_bulk_soc.value())});
    }
    if (i.target_energy_request.has_value()) {
        ev_info.push_back({"Energy Requested", fmt::format("{:.1f} Wh", i.target_energy_request.value())});
    }
    if (i.max_energy_request.has_value()) {
        ev_info.push_back({"Maximum Acceptable Energy", fmt::format("{:.1f} Wh", i.max_energy_request.value())});
    }
    if (i.min_energy_request.has_value()) {
        ev_info.push_back({"Energy Requested for Minimum SoC", fmt::format("{:.1f} Wh", i.min_energy_request.value())});
    }
    if (i.ac_min_charge_power.has_value()) {
        ev_info.push_back({"AC Minimum Power", fmt::format("{:.1f} W", i.ac_min_charge_power.value())});
    }
    if (i.ac_max_charge_power.has_value()) {
        ev_info.push_back({"AC Maximum Power", fmt::format("{:.1f} W", i.ac_max_charge_power.value())});
    }
    if (i.ac_min_discharge_power.has_value()) {
        ev_info.push_back({"Ac Minimum discharge power", fmt::format("{:.1f} W", i.ac_min_discharge_power.value())});
    }
    if (i.ac_max_discharge_power.has_value()) {
        ev_info.push_back({"AC Maximum Discharge Power", fmt::format("{:.1f} W", i.ac_max_discharge_power.value())});
    }
    if (i.ac_present_active_power.has_value()) {
        ev_info.push_back(
            {"AC Present Total Active Power", fmt::format("{:.1f} W", i.ac_present_active_power.value())});
    }
    if (i.ac_present_reactive_power.has_value()) {
        ev_info.push_back(
            {"AC Present Total Reactive Power", fmt::format("{:.1f} W", i.ac_present_reactive_power.value())});
    }

    return ev_info;
}

void BUEvBoardSupport::init() {
}

void BUEvBoardSupport::ready() {
    auto screen = ScreenInteractive::Fullscreen();

    r_bsp->subscribe_bsp_event([this, &screen](const types::board_support_common::BspEvent& e) {
        {
            std::scoped_lock lock(data_mutex);
            switch (e.event) {
            case types::board_support_common::Event::A:
                cp_state = "A";
                break;
            case types::board_support_common::Event::B:
                cp_state = "B";
                break;
            case types::board_support_common::Event::C:
                cp_state = "C";
                break;
            case types::board_support_common::Event::D:
                cp_state = "D";
                break;
            case types::board_support_common::Event::E:
                cp_state = "E";
                break;
            case types::board_support_common::Event::F:
                cp_state = "F";
                break;
            case types::board_support_common::Event::PowerOn:
                relais_feedback = fmt::format("PowerOn (after {} ms)",
                                              std::chrono::duration_cast<std::chrono::milliseconds>(
                                                  std::chrono::steady_clock::now() - last_allow_power_on_time_point)
                                                  .count());
                break;
            case types::board_support_common::Event::PowerOff:
                relais_feedback = fmt::format("PowerOff (after {} ms)",
                                              std::chrono::duration_cast<std::chrono::milliseconds>(
                                                  std::chrono::steady_clock::now() - last_allow_power_on_time_point)
                                                  .count());
                break;
            case types::board_support_common::Event::Disconnected:
                cp_state = "Disconnected";
                break;
            }
        }
        screen.Post(Event::Custom);
    });

    r_bsp->subscribe_bsp_measurement([this, &screen](const types::board_support_common::BspMeasurement& m) {
        {
            std::scoped_lock lock(data_mutex);
            proximity_pilot = types::board_support_common::ampacity_to_string(m.proximity_pilot.ampacity);

            duty_cycle = fmt::format("{:.1f}%", m.cp_pwm_duty_cycle);

            if (m.rcd_current_mA.has_value()) {
                rcd_current = fmt::format("{:.1f} mA", m.rcd_current_mA.value());
            } else {
                rcd_current = "-n/a-";
            }
        }
        screen.Post(Event::Custom);
    });

    r_bsp->subscribe_ev_info([this, &screen](const types::evse_manager::EVInfo& i) {
        {
            std::scoped_lock lock(data_mutex);
            ev_info = to_table(i);
        }
        screen.Post(Event::Custom);
    });

    auto error_handler = [this, &screen](const Everest::error::Error& error) {
        {
            std::scoped_lock lock(this->data_mutex);
            this->last_error_raised = fmt::format("Fault raised: {}, {}", error.type, error.sub_type);
        }
        screen.Post(Event::Custom);
    };

    auto error_cleared_handler = [this, &screen](const Everest::error::Error& error) {
        {
            std::scoped_lock lock(this->data_mutex);
            this->last_error_cleared = fmt::format("Fault cleared: {}, {}", error.type, error.sub_type);
        }
        screen.Post(Event::Custom);
    };

    r_bsp->subscribe_all_errors(error_handler, error_cleared_handler);

    std::string last_command = "None";

    // ---------------------------------------------------------------------------
    // Enable/Disable
    // ---------------------------------------------------------------------------

    Component enable_on_button = Button(
        "Enable",
        [&] {
            last_command = "Enable";
            r_bsp->call_enable(true);
        },
        ButtonOption::Animated(Color::Blue, Color::White, Color::BlueLight, Color::White));

    Component enable_off_button = Button(
        "Disable",
        [&] {
            last_command = "Disable";
            r_bsp->call_enable(false);
        },
        ButtonOption::Animated(Color::Red, Color::White, Color::RedLight, Color::White));

    auto enable_component = Container::Vertical({
        enable_on_button,
        enable_off_button,
    });

    auto enable_renderer = Renderer(
        enable_component, [&] { return hbox({window(text("Enable"), enable_component->Render())}) | flex_grow; });

    // ---------------------------------------------------------------------------
    // CP State Control
    // ---------------------------------------------------------------------------

    Component cp_state_A = Button(
        "CP State A",
        [&] {
            last_command = "CP State A";
            r_bsp->call_set_cp_state(types::ev_board_support::EvCpState::A);
        },
        ButtonOption::Animated(Color::Red, Color::White, Color::RedLight, Color::White));

    Component cp_state_B = Button(
        "CP State B",
        [&] {
            last_command = "CP State B";
            r_bsp->call_set_cp_state(types::ev_board_support::EvCpState::B);
        },
        ButtonOption::Animated(Color::Green, Color::White, Color::GreenLight, Color::White));

    Component cp_state_C = Button(
        "CP State C",
        [&] {
            last_command = "CP State C";
            r_bsp->call_set_cp_state(types::ev_board_support::EvCpState::C);
        },
        ButtonOption::Animated(Color::Blue, Color::White, Color::BlueLight, Color::White));

    Component cp_state_D = Button(
        "CP State D",
        [&] {
            last_command = "CP State D";
            r_bsp->call_set_cp_state(types::ev_board_support::EvCpState::D);
        },
        ButtonOption::Animated(Color::Yellow, Color::White, Color::YellowLight, Color::White));

    Component cp_state_E = Button(
        "CP State E",
        [&] {
            last_command = "CP State E";
            r_bsp->call_set_cp_state(types::ev_board_support::EvCpState::E);
        },
        ButtonOption::Animated(Color::Magenta, Color::White, Color::MagentaLight, Color::White));

    auto cp_component = Container::Horizontal({
        cp_state_A,
        cp_state_B,
        cp_state_C,
        cp_state_D,
        cp_state_E,
    });

    auto cp_renderer = Renderer(
        cp_component, [&] { return hbox({window(text("Control Pilot"), cp_component->Render())}) | flex_grow; });

    // ---------------------------------------------------------------------------
    // Relais allow power on
    // ---------------------------------------------------------------------------

    Component relais_on_button = Button(
        "Allow power on",
        [&] {
            last_command = "Allow power on";
            last_allow_power_on_time_point = std::chrono::steady_clock::now();
            r_bsp->call_allow_power_on(true);
        },
        ButtonOption::Animated(Color::Blue, Color::White, Color::BlueLight, Color::White));

    Component relais_off_button = Button(
        "Force power off",
        [&] {
            last_command = "Force power off";
            last_allow_power_on_time_point = std::chrono::steady_clock::now();
            r_bsp->call_allow_power_on(false);
        },
        ButtonOption::Animated(Color::Red, Color::White, Color::RedLight, Color::White));

    auto relais_component = Container::Vertical({
        relais_on_button,
        relais_off_button,
    });

    auto relais_renderer = Renderer(
        relais_component, [&] { return hbox({window(text("Relais"), relais_component->Render())}) | flex_grow; });

    // ---------------------------------------------------------------------------
    // AC switch phases while charging
    // ---------------------------------------------------------------------------

    Component three_phase_button = Button(
        "ThreePhases",
        [&] {
            last_command = "Switch ThreePhases";
            r_bsp->call_set_three_phases(true);
        },
        ButtonOption::Animated(Color::Blue, Color::White, Color::BlueLight, Color::White));

    Component single_phase_button = Button(
        "SinglePhase",
        [&] {
            last_command = "Switch SinglePhase";
            r_bsp->call_set_three_phases(false);
        },
        ButtonOption::Animated(Color::Red, Color::White, Color::RedLight, Color::White));

    auto phase_switch_component = Container::Vertical({
        three_phase_button,
        single_phase_button,
    });

    auto phase_switch_renderer = Renderer(
        phase_switch_component, [&] { return window(text("AC Phase Switch"), phase_switch_component->Render()); });

    // ---------------------------------------------------------------------------
    // Diode Failure
    // ---------------------------------------------------------------------------

    Component diode_failure_on_button = Button(
        "Enable",
        [&] {
            last_command = "Diode Failure On";
            r_bsp->call_diode_fail(true);
        },
        ButtonOption::Animated(Color::Red, Color::White, Color::RedLight, Color::White));

    Component diode_failure_off_button = Button(
        "Disable",
        [&] {
            last_command = "Diode Failure Off";
            r_bsp->call_diode_fail(false);
        },
        ButtonOption::Animated(Color::Green, Color::White, Color::GreenLight, Color::White));

    auto diode_failure_component = Container::Vertical({
        diode_failure_on_button,
        diode_failure_off_button,
    });

    auto diode_failure_renderer = Renderer(diode_failure_component, [&] {
        return hbox({window(text("Diode Failure"), diode_failure_component->Render())}) | flex_grow;
    });

    // ---------------------------------------------------------------------------
    // Vars
    // ---------------------------------------------------------------------------

    auto vars_renderer = Renderer([&] {
        std::vector<std::vector<std::string>> table_content;
        {
            std::scoped_lock lock(data_mutex);
            table_content = {{"CP State", cp_state},
                             {"Duty Cycle", duty_cycle},
                             {"Proximity Pilot", proximity_pilot},
                             {"Relais", relais_feedback},
                             {"RCD Current", rcd_current}};
        }
        auto vars = Table(table_content);

        vars.SelectColumn(0).Border(LIGHT);
        for (int i = 0; i < table_content.size(); i++) {
            vars.SelectRow(i).Border(LIGHT);
        }
        return vbox({
                   hbox({
                       window(
                           text("Information"),
                           vbox({text("Last Command: " + last_command), text("Last Error Raised: " + last_error_raised),
                                 text("Last Error Cleared: " + last_error_cleared), vars.Render()})),
                   }),
               }) |
               flex_grow;
    });

    // ---------------------------------------------------------------------------
    // EV Info
    // ---------------------------------------------------------------------------

    types::evse_manager::EVInfo dummy_evinfo;
    ev_info = to_table(dummy_evinfo);

    auto evinfo_renderer = Renderer([&] {
        std::vector<std::vector<std::string>> table_content;
        {
            std::scoped_lock lock(data_mutex);
            table_content = ev_info;
        }
        Table evinfo;
        Element renderer;
        if (table_content.size()) {
            evinfo = Table(table_content);

            evinfo.SelectColumn(0).Border(LIGHT);
            for (int i = 0; i < table_content.size(); i++) {
                evinfo.SelectRow(i).Border(LIGHT);
            }
            renderer = evinfo.Render();
        } else {
            renderer = text("No Data");
        }

        return vbox({
                   hbox({
                       window(text("EV Info"), renderer),
                   }),
               }) |
               flex_grow;
    });

    // ---------------------------------------------------------------------------
    // Main layout
    // ---------------------------------------------------------------------------

    auto command_row = Container::Horizontal({
        Container::Vertical({cp_renderer, Container::Horizontal({enable_renderer, relais_renderer,
                                                                 phase_switch_renderer, diode_failure_renderer})}),
    });

    auto info_row = Container::Horizontal({
        vars_renderer,
        evinfo_renderer,
    });

    auto main_container = Container::Vertical({command_row, info_row});

    auto main_renderer = Renderer(main_container, [&] {
        return vbox(text("EV Board Support") | bold | hcenter, separator(), command_row->Render(), separator(),
                    info_row->Render());
    });

    screen.Loop(main_renderer);
}

} // namespace module
