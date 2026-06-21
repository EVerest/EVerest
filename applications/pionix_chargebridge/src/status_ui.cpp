// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include <charge_bridge/status_ui.hpp>
#include <charge_bridge/utilities/logging.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/table.hpp>
#include <ftxui/screen/box.hpp>

namespace {

constexpr std::size_t STATUS_MESSAGE_MAX_LENGTH = 1024;

// A component that makes its (non-interactive) child vertically scrollable with the mouse wheel,
// arrow keys and PageUp/PageDown/Home/End. Standard ftxui pattern. Mouse events only act while the
// cursor is over the component, so several scrollers can coexist.
class ScrollerBase : public ftxui::ComponentBase {
public:
    explicit ScrollerBase(ftxui::Component child) {
        Add(std::move(child));
    }

    ftxui::Element OnRender() override {
        using namespace ftxui;
        auto background = ComponentBase::OnRender();
        background->ComputeRequirement();
        size_ = background->requirement().min_y;
        auto marker = Focused() ? focus(text("")) : select(text(""));
        return dbox({
                   std::move(background),
                   vbox({
                       text("") | size(HEIGHT, EQUAL, selected_),
                       std::move(marker),
                   }),
               }) |
               vscroll_indicator | yframe | reflect(box_);
    }

    bool OnEvent(ftxui::Event event) override {
        using namespace ftxui;
        if (event.is_mouse()) {
            if (not box_.Contain(event.mouse().x, event.mouse().y)) {
                return false;
            }
            TakeFocus();
        }

        const int viewport = box_.y_max - box_.y_min;
        const int selected_old = selected_;
        if (event == Event::ArrowUp || (event.is_mouse() && event.mouse().button == Mouse::WheelUp)) {
            selected_ -= 3;
        } else if (event == Event::ArrowDown || (event.is_mouse() && event.mouse().button == Mouse::WheelDown)) {
            selected_ += 3;
        } else if (event == Event::PageUp) {
            selected_ -= viewport;
        } else if (event == Event::PageDown) {
            selected_ += viewport;
        } else if (event == Event::Home) {
            selected_ = 0;
        } else if (event == Event::End) {
            selected_ = size_;
        } else {
            return false;
        }

        selected_ = std::max(0, std::min(size_ - 1, selected_));
        return selected_old != selected_;
    }

    bool Focusable() const override {
        return true;
    }

private:
    int selected_{0};
    int size_{0};
    ftxui::Box box_;
};

ftxui::Component Scroller(ftxui::Component child) {
    return ftxui::Make<ScrollerBase>(std::move(child));
}

} // namespace

namespace charge_bridge {

status_ui::status_ui(status_ui_options options, std::vector<std::string> cb_names) : m_options(std::move(options)) {
    m_status_rows.reserve(cb_names.size());
    for (auto const& cb_name : cb_names) {
        status_row row;
        row.cb_name = cb_name;
        auto insertion = m_cb_name_to_row.try_emplace(cb_name, m_status_rows.size());
        if (insertion.second) {
            m_status_rows.push_back(std::move(row));
            m_cb_names.push_back(cb_name);
        }
    }

    // Keep a scrollback buffer larger than the visible window so the log can be scrolled back.
    constexpr std::size_t k_min_scrollback = 1000;
    m_log_capacity = std::max(m_options.status_message_lines, k_min_scrollback);

    if (m_terminal_active()) {
        utilities::set_print_error_sink(
            [this](std::string device, std::string message) { publish_message(std::move(device), std::move(message)); });
    }
}

status_ui::~status_ui() {
    stop();
}

void status_ui::set_quit_handler(std::function<void()> handler) {
    m_quit_handler = std::move(handler);
}

bool status_ui::m_terminal_active() const {
    return m_options.status_output == utilities::status_output_mode::terminal;
}

void status_ui::publish(utilities::chargebridge_status status) {
    switch (m_options.status_output) {
    case utilities::status_output_mode::off:
        return;
    case utilities::status_output_mode::terminal: {
        {
            std::scoped_lock lock(m_state_mutex);
            apply_status_row(status);
        }
        request_redraw();
        return;
    }
    default:
        // log / auto (auto is resolved to a concrete mode before construction)
        m_status_queue.push(status_update_event{std::move(status)});
        return;
    }
}

void status_ui::publish_message(std::string device, std::string message) {
    if (not m_terminal_active() || m_options.status_message_lines == 0) {
        return;
    }

    if (message.size() > STATUS_MESSAGE_MAX_LENGTH) {
        message.resize(STATUS_MESSAGE_MAX_LENGTH);
    }

    {
        std::scoped_lock lock(m_state_mutex);
        apply_log_message(std::move(device), std::move(message));
    }
    request_redraw();
}

void status_ui::run() {
    if (m_options.status_output == utilities::status_output_mode::off) {
        return;
    }

    bool expected = false;
    if (not m_running.compare_exchange_strong(expected, true)) {
        return;
    }

    if (m_terminal_active()) {
        m_thread = std::thread(&status_ui::run_terminal_loop, this);
    } else {
        m_thread = std::thread(&status_ui::run_log_loop, this);
    }
}

void status_ui::stop() {
    auto const was_running = m_running.exchange(false);

    // Wake whichever loop is running.
    m_status_queue.stop();
    {
        std::scoped_lock lock(m_screen_mutex);
        if (m_screen != nullptr) {
            m_screen->Exit();
        }
    }

    if (was_running && m_thread.joinable()) {
        m_thread.join();
    }

    if (m_terminal_active()) {
        utilities::clear_print_error_sink();
    }
}

void status_ui::apply_status_row(utilities::chargebridge_status const& status) {
    auto row_it = m_cb_name_to_row.find(status.cb_name);
    if (row_it == m_cb_name_to_row.end()) {
        return;
    }

    auto& row = m_status_rows[row_it->second];
    row.discovered = status.discovered;
    row.connected = status.connected;
    row.can0 = status.can0;
    row.serial1 = status.serial1;
    row.serial2 = status.serial2;
    row.serial3 = status.serial3;
    row.plc = status.plc;
    row.bsp = status.bsp;
    row.heartbeat = status.heartbeat;
    row.io = status.io;
    row.mcu_resets = status.mcu_resets;
    row.telemetry = status.telemetry;
    row.cp_state = status.cp_state;
    row.gpio = status.gpio;
    row.adc = status.adc;
    row.io_telemetry = status.io_telemetry;

    if (status.telemetry.has_value()) {
        auto push = [](std::deque<float>& history, float value) {
            history.push_back(value);
            while (history.size() > k_telemetry_history) {
                history.pop_front();
            }
        };
        push(row.mcu_temp_history, static_cast<float>(status.telemetry->temperature_mcu_C));
        push(row.cp_lo_history, static_cast<float>(status.telemetry->cp_lo_mV));
        push(row.cp_hi_history, static_cast<float>(status.telemetry->cp_hi_mV));
    }
}

void status_ui::apply_log_message(std::string device, std::string message) {
    m_log_messages.emplace_back(std::move(device), std::move(message));
    while (m_log_messages.size() > m_log_capacity) {
        m_log_messages.pop_front();
    }
}

void status_ui::request_redraw() {
    std::scoped_lock lock(m_screen_mutex);
    if (m_screen != nullptr) {
        m_screen->PostEvent(ftxui::Event::Custom);
    }
}

void status_ui::run_log_loop() {
    while (true) {
        auto event = m_status_queue.wait_and_pop();
        if (not event.has_value()) {
            return;
        }

        if (auto* status_event = std::get_if<status_update_event>(&*event)) {
            utilities::print_status(status_event->status, m_options.status_output);
        }

        if (not m_running.load(std::memory_order_acquire)) {
            auto next_event = m_status_queue.try_pop(std::chrono::milliseconds{0});
            while (next_event.has_value()) {
                if (auto* status_event = std::get_if<status_update_event>(&*next_event)) {
                    utilities::print_status(status_event->status, m_options.status_output);
                }
                next_event = m_status_queue.try_pop(std::chrono::milliseconds{0});
            }
            return;
        }
    }
}

void status_ui::run_terminal_loop() {
    using namespace ftxui;

    auto screen = ScreenInteractive::Fullscreen();
    {
        std::scoped_lock lock(m_screen_mutex);
        m_screen = &screen;
    }

    // --- cell builders -------------------------------------------------------
    // With colors disabled we drop both color and bold to mirror the previous --status-no-color behavior.
    auto styled = [this](std::string s, Color c, bool bold_it) -> Element {
        auto e = text(std::move(s));
        if (m_options.no_color) {
            return e;
        }
        e = e | color(c);
        if (bold_it) {
            e = e | bold;
        }
        return e;
    };

    // Sparkline of a numeric history against an explicit [vmin, vmax] range (newest at the right).
    constexpr int k_spark_height = 5;
    auto spark = [](std::deque<float> data, float vmin, float vmax) -> Element {
        auto fn = [data = std::move(data), vmin, vmax](int width, int height) {
            std::vector<int> out(width > 0 ? static_cast<std::size_t>(width) : 0, 0);
            if (data.empty() || width <= 0 || height <= 0) {
                return out;
            }
            const float range = (vmax - vmin) > 0.0F ? (vmax - vmin) : 1.0F;
            for (int x = 0; x < width; ++x) {
                const int idx = static_cast<int>(data.size()) - width + x;
                const float value = idx >= 0 ? data[static_cast<std::size_t>(idx)] : data.front();
                const float norm = std::clamp((value - vmin) / range, 0.0F, 1.0F);
                out[static_cast<std::size_t>(x)] = static_cast<int>(std::lround(norm * height));
            }
            return out;
        };
        return graph(std::move(fn)) | ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, k_spark_height);
    };

    // A small plot with a title (+ unit and latest value), a labeled y-axis (min/max) and an x-axis
    // caption. The graph is auto-scaled to the data's range.
    auto plot = [&](std::string const& title, std::string const& unit, std::deque<float> const& data) -> Element {
        constexpr int k_axis_width = 7;
        float vmin = 0.0F;
        float vmax = 0.0F;
        std::string now = "-";
        if (not data.empty()) {
            vmin = data.front();
            vmax = data.front();
            for (float value : data) {
                vmin = std::min(vmin, value);
                vmax = std::max(vmax, value);
            }
            now = std::to_string(static_cast<long>(std::lround(static_cast<double>(data.back()))));
        }
        if (vmax - vmin < 1.0F) { // avoid a degenerate flat range
            vmax += 1.0F;
            vmin -= 1.0F;
        }
        auto axis_label = [](float value) {
            return std::to_string(static_cast<long>(std::lround(static_cast<double>(value))));
        };
        auto header =
            hbox({text(title) | bold, text(" [" + unit + "]") | dim, filler(), text("now=" + now) | bold});
        auto yaxis = vbox({
                         text(axis_label(vmax)) | dim | align_right,
                         filler(),
                         text(axis_label(vmin)) | dim | align_right,
                     }) |
                     size(HEIGHT, EQUAL, k_spark_height) | size(WIDTH, EQUAL, k_axis_width);
        auto body = hbox({yaxis, separator(), spark(data, vmin, vmax) | flex});
        auto footer = hbox({text("") | size(WIDTH, EQUAL, k_axis_width + 1), text("oldest") | dim, filler(),
                            text("now") | dim});
        return vbox({header, body, footer});
    };

    // Compact, bold status glyph for the instance list (distinct shapes even without color).
    auto glyph = [&](std::optional<bool> v) -> Element {
        if (not v.has_value()) {
            auto e = text("N/A") | dim;
            return m_options.no_color ? e : (e | color(Color::GrayDark));
        }
        auto e = text(*v ? "●" : "✖") | bold;
        return m_options.no_color ? e : (e | color(*v ? Color::Green : Color::Red));
    };

    // Interleave vertical separators between a row's cells so columns are easy to read.
    auto row_with_separators = [](std::vector<Element> cells) -> Element {
        Elements out;
        for (std::size_t i = 0; i < cells.size(); ++i) {
            if (i != 0) {
                out.push_back(separator());
            }
            out.push_back(std::move(cells[i]));
        }
        return hbox(std::move(out));
    };

    constexpr int k_name_width = 16;
    constexpr int k_glyph_width = 4;
    constexpr int k_rst_width = 4;
    static const std::array<char const*, 10> k_bridge_cols = {
        "disc", "conn", "can0", "ser1", "ser2", "ser3", "plc", "bsp", "hbt", "io",
    };

    auto list_header = [&]() -> Element {
        std::vector<Element> cells;
        cells.push_back(text("instance") | bold | size(WIDTH, EQUAL, k_name_width));
        for (auto const* col : k_bridge_cols) {
            cells.push_back(text(col) | bold | hcenter | size(WIDTH, EQUAL, k_glyph_width));
        }
        cells.push_back(text("rst") | bold | hcenter | size(WIDTH, EQUAL, k_rst_width));
        return row_with_separators(std::move(cells));
    };

    // --- instance list (left): scrollable, mouse + keyboard selectable -------
    MenuOption menu_option;
    menu_option.entries = &m_cb_names;
    menu_option.selected = &m_selected_row;
    menu_option.entries_option.transform = [&](const EntryState& entry) -> Element {
        std::scoped_lock lock(m_state_mutex);
        if (entry.index < 0 || entry.index >= static_cast<int>(m_status_rows.size())) {
            return text(entry.label);
        }
        auto const& row = m_status_rows[static_cast<std::size_t>(entry.index)];
        std::vector<Element> cells;
        cells.push_back(text(row.cb_name) | size(WIDTH, EQUAL, k_name_width));
        cells.push_back(glyph(row.discovered) | hcenter | size(WIDTH, EQUAL, k_glyph_width));
        cells.push_back(glyph(row.connected) | hcenter | size(WIDTH, EQUAL, k_glyph_width));
        cells.push_back(glyph(row.can0) | hcenter | size(WIDTH, EQUAL, k_glyph_width));
        cells.push_back(glyph(row.serial1) | hcenter | size(WIDTH, EQUAL, k_glyph_width));
        cells.push_back(glyph(row.serial2) | hcenter | size(WIDTH, EQUAL, k_glyph_width));
        cells.push_back(glyph(row.serial3) | hcenter | size(WIDTH, EQUAL, k_glyph_width));
        cells.push_back(glyph(row.plc) | hcenter | size(WIDTH, EQUAL, k_glyph_width));
        cells.push_back(glyph(row.bsp) | hcenter | size(WIDTH, EQUAL, k_glyph_width));
        cells.push_back(glyph(row.heartbeat) | hcenter | size(WIDTH, EQUAL, k_glyph_width));
        cells.push_back(glyph(row.io) | hcenter | size(WIDTH, EQUAL, k_glyph_width));
        cells.push_back((row.mcu_resets.has_value() ? styled(std::to_string(*row.mcu_resets), Color::White, false)
                                                     : styled("-", Color::GrayDark, false)) |
                        hcenter | size(WIDTH, EQUAL, k_rst_width));
        auto line = row_with_separators(std::move(cells));
        if (entry.active) {
            line = line | (m_options.no_color ? inverted : bgcolor(Color::Blue));
        } else if (entry.focused) {
            line = line | bold;
        }
        return line;
    };
    auto instance_menu = Menu(menu_option);

    // --- per-instance detail panel (right) ----------------------------------
    auto detail = Renderer([&] {
        std::scoped_lock lock(m_state_mutex);

        if (m_status_rows.empty()) {
            return window(text("Instance"), text("no instances configured")) | flex;
        }
        const std::size_t idx =
            (m_selected_row >= 0 && m_selected_row < static_cast<int>(m_status_rows.size()))
                ? static_cast<std::size_t>(m_selected_row)
                : 0;
        auto const& row = m_status_rows[idx];

        Elements sections;

        // CP state from the BSP bridge (if enabled).
        if (row.cp_state.has_value()) {
            sections.push_back(hbox({text("CP state: ") | bold, styled(*row.cp_state, Color::Yellow, true)}));
        }

        // Heartbeat telemetry readouts + sparklines (available once heartbeat packets arrive).
        if (row.telemetry.has_value()) {
            auto const& telemetry = *row.telemetry;
            sections.push_back(text("MCU temp:       " + std::to_string(telemetry.temperature_mcu_C) + " degC"));
            sections.push_back(text("CP hi/lo:       " + std::to_string(telemetry.cp_hi_mV) + " / " +
                                    std::to_string(telemetry.cp_lo_mV) + " mV"));
            sections.push_back(text("PP:             " + std::to_string(telemetry.pp_mOhm) + " mOhm"));
            sections.push_back(text("VDD 12/-12/3.3: " + std::to_string(telemetry.vdd_12V_mV) + " / " +
                                    std::to_string(telemetry.vdd_N12V_mV) + " / " +
                                    std::to_string(telemetry.vdd_3v3_mV) + " mV"));
            sections.push_back(separator());
            sections.push_back(plot("MCU temp", "degC", row.mcu_temp_history));
            sections.push_back(plot("CP low", "mV", row.cp_lo_history));
            sections.push_back(plot("CP high", "mV", row.cp_hi_history));
        } else {
            sections.push_back(text("waiting for heartbeat telemetry...") | dim);
        }

        // GPIO states (wrapped into rows).
        if (row.gpio.has_value() && not row.gpio->empty()) {
            sections.push_back(separator());
            sections.push_back(text("GPIO") | bold);
            constexpr std::size_t k_per_row = 7;
            Elements gpio_rows;
            Elements current;
            for (std::size_t i = 0; i < row.gpio->size(); ++i) {
                current.push_back(text(std::to_string(i) + ":" + std::to_string((*row.gpio)[i])) |
                                  size(WIDTH, EQUAL, 9));
                if (current.size() == k_per_row) {
                    gpio_rows.push_back(hbox(std::move(current)));
                    current.clear();
                }
            }
            if (not current.empty()) {
                gpio_rows.push_back(hbox(std::move(current)));
            }
            sections.push_back(vbox(std::move(gpio_rows)));
        }

        // ADC values.
        if (row.adc.has_value() && not row.adc->empty()) {
            sections.push_back(text("ADC") | bold);
            Elements adc_cells;
            for (std::size_t i = 0; i < row.adc->size(); ++i) {
                adc_cells.push_back(text(std::to_string(i) + ":" + std::to_string((*row.adc)[i])) |
                                    size(WIDTH, EQUAL, 12));
            }
            sections.push_back(hbox(std::move(adc_cells)));
        }

        // Unstructured telemetry (i2c temperature sensor, etc.).
        if (row.io_telemetry.has_value() && not row.io_telemetry->empty()) {
            sections.push_back(separator());
            sections.push_back(text("Telemetry (unstructured)") | bold);
            for (auto const& [name, value] : *row.io_telemetry) {
                sections.push_back(text("  " + name + " = " + std::to_string(value)));
            }
        }

        return window(text("Instance: " + row.cb_name), vbox(std::move(sections)) | vscroll_indicator | yframe) |
               flex;
    });

    // --- message panel (scrollable, optionally filtered to the selected instance) -----------
    auto messages = Renderer([&] {
        std::string filter_name;
        std::vector<std::string> shown;
        {
            std::scoped_lock lock(m_state_mutex);
            if (m_log_filter_selected && m_selected_row >= 0 &&
                m_selected_row < static_cast<int>(m_status_rows.size())) {
                filter_name = m_status_rows[static_cast<std::size_t>(m_selected_row)].cb_name;
            }
            for (auto const& entry : m_log_messages) {
                if (filter_name.empty() || entry.first == filter_name) {
                    shown.push_back(entry.second);
                }
            }
        }

        // Newest first, so the latest message is visible without scrolling; wheel/keys scroll back.
        Elements lines;
        for (auto it = shown.rbegin(); it != shown.rend(); ++it) {
            lines.push_back(text(*it));
        }
        if (lines.empty()) {
            lines.push_back(text("(no messages)") | dim);
        }

        std::string title = "Messages [" + (filter_name.empty() ? std::string("all") : filter_name) +
                            "]   (wheel/PgUp/PgDn scroll, f filter)";
        return window(text(title), vbox(std::move(lines)));
    });

    // --- overall layout: list (left) | details (right), messages (bottom), with draggable borders.
    // The menu is the interactive child of the list panel so it receives keyboard/mouse events.
    auto list_panel = Renderer(instance_menu, [&] {
        return window(text("ChargeBridges (" + std::to_string(m_status_rows.size()) + ")"),
                      vbox({
                          list_header(),
                          separator(),
                          instance_menu->Render() | vscroll_indicator | yframe | flex,
                      }));
    });

    // Details and messages are wrapped in scrollers so the mouse wheel / keys scroll them.
    auto detail_panel = Scroller(detail);
    auto split = ResizableSplitLeft(list_panel, detail_panel, &m_list_split_size);

    if (m_options.status_message_lines > 0) {
        m_msg_split_size = std::max(3, static_cast<int>(m_options.status_message_lines) + 2);
        split = ResizableSplitBottom(Scroller(messages), split, &m_msg_split_size);
    }

    auto app = Renderer(split, [&] {
        return vbox({
            text("PIONIX ChargeBridge") | bold | hcenter,
            text("[ click/↑↓ select   wheel scroll   drag borders to resize   f filter log   q quit ]") | dim |
                hcenter,
            separator(),
            split->Render() | flex,
        });
    });

    // Only intercept the global shortcuts; selection, scrolling and border-dragging are handled by
    // the components themselves (menu, scrollers, resizable splits).
    auto root = CatchEvent(app, [&](const Event& event) {
        if (event == Event::Character('f')) {
            m_log_filter_selected = not m_log_filter_selected;
            return true;
        }
        if (event == Event::Character('q') || event == Event::Escape) {
            screen.Exit();
            return true;
        }
        return false;
    });

    screen.Loop(root);

    {
        std::scoped_lock lock(m_screen_mutex);
        m_screen = nullptr;
    }

    // If the loop exited because the user quit (rather than stop() being called), notify the app.
    if (m_running.load(std::memory_order_acquire) && m_quit_handler) {
        m_quit_handler();
    }
}

} // namespace charge_bridge
