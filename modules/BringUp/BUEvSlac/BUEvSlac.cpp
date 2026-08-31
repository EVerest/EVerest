// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "BUEvSlac.hpp"

#include "../common/link_echo.hpp"
#include "ftxui/dom/table.hpp"
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

using namespace ftxui;

namespace module {

void BUEvSlac::init() {
}

void BUEvSlac::ready() {
    auto screen = ScreenInteractive::Fullscreen();

    // Link echo: raw-ethernet ping/pong over the configured device, proving payload actually
    // crosses the SPE link (IP between two taps on one host never touches the wire). Declared
    // after `screen` so it is torn down first - its threads post events at `screen`.
    bringup::link_echo echo(config.echo_device, [&screen] { screen.PostEvent(Event::Custom); });

    r_slac->subscribe_state([this, &screen](const types::slac::State new_state) {
        {
            std::scoped_lock lock(data_mutex);
            state = types::slac::state_to_string(new_state);
        }
        screen.PostEvent(Event::Custom);
    });

    r_slac->subscribe_dlink_ready([this, &screen](const bool& is_ready) {
        {
            std::scoped_lock lock(data_mutex);
            dlink = is_ready ? "true" : "false";
        }
        screen.PostEvent(Event::Custom);
    });

    r_slac->subscribe_ev_mac_address([this, &screen](const std::string& mac) {
        {
            std::scoped_lock lock(data_mutex);
            ev_mac_address = mac;
        }
        screen.PostEvent(Event::Custom);
    });

    // -------------------------------------------------------------------
    // Left column (Var Display)
    // -------------------------------------------------------------------
    auto data_renderer = Renderer([&] {
        std::vector<std::vector<std::string>> table_content;

        {
            std::scoped_lock lock(data_mutex);
            table_content = {
                {"State", state},
                {"DLink Ready", dlink},
                {"Connector MAC Address", ev_mac_address},
                {"Last trigger_matching", last_trigger_matching_result},
            };
        }
        {
            auto const s = echo.stats();
            table_content.push_back({"Echo [" + s.device + "]", s.armed ? "armed" : s.error});
            if (s.armed) {
                table_content.push_back({"Echo own MAC", s.own_mac});
                table_content.push_back({"Echo peer MAC", s.peer_mac});
                table_content.push_back({"Echo tx ping / rx pong",
                                         std::to_string(s.tx_ping) + " / " + std::to_string(s.rx_pong) +
                                             (s.rx_pong_bad ? " (BAD " + std::to_string(s.rx_pong_bad) + ")" : "")});
                table_content.push_back({"Echo RTT last / avg us",
                                         std::to_string(s.last_rtt_us) + " / " + std::to_string(s.avg_rtt_us)});
                table_content.push_back({"Echo answered (rx ping)", std::to_string(s.rx_ping)});
                table_content.push_back({"Echo auto 1 Hz", s.auto_ping ? "on" : "off"});
            }
        }

        auto table = Table(table_content);

        table.SelectAll().Border(LIGHT);
        table.SelectColumn(0).Border(LIGHT);
        for (int i = 0; i < (int)table_content.size(); ++i)
            table.SelectRow(i).Border(LIGHT);

        return vbox({
                   window(text("Module Data"), vbox({
                                                   table.Render(),
                                               })) |
                       size(WIDTH, EQUAL, 52),
               }) |
               flex_grow;
    });

    // -------------------------------------------------------------------
    // Right column (Command Buttons)
    // -------------------------------------------------------------------

    auto button_trigger_matching = Button("Trigger Matching", [&] {
        bool accepted = r_slac->call_trigger_matching();
        {
            std::scoped_lock lock(data_mutex);
            last_trigger_matching_result = accepted ? "accepted" : "rejected";
        }
        screen.PostEvent(Event::Custom);
    });
    auto button_reset = Button("Reset", [&] { r_slac->call_reset(); });
    // Payload sizes: 32 B is a minimal frame, 1400 B rides just under the tap MTU (1438) - both
    // shapes crossing the wire is what "data actually flows over the datalink" means here.
    auto button_echo_small = Button("Echo: 10 pings (small)", [&] { echo.send_burst(10, 32); });
    auto button_echo_big = Button("Echo: 10 pings (1400 B)", [&] { echo.send_burst(10, 1400); });
    auto button_echo_auto = Button("Echo: toggle 1 Hz", [&] {
        echo.toggle_auto_ping();
        screen.PostEvent(Event::Custom);
    });

    auto command_container = Container::Vertical({
        button_trigger_matching,
        button_reset,
        button_echo_small,
        button_echo_big,
        button_echo_auto,
    });

    auto command_renderer = Renderer(command_container, [&] {
        return vbox({
                   text("Commands") | bold | center,
                   separator(),
                   button_trigger_matching->Render(),
                   button_reset->Render(),
                   separator(),
                   button_echo_small->Render(),
                   button_echo_big->Render(),
                   button_echo_auto->Render(),
               }) |
               border | size(WIDTH, EQUAL, 40);
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
            text("EV SLAC BringUp") | bold | center,
            separator(),
            hbox({
                data_renderer->Render(),
                command_renderer->Render(),
            }),
        });
    });

    screen.Loop(main_renderer);
}

void BUEvSlac::shutdown() {
}

} // namespace module
