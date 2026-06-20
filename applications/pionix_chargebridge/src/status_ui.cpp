// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include <charge_bridge/status_ui.hpp>
#include <charge_bridge/utilities/logging.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <map>
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
    explicit ScrollerBase(ftxui::Component child, bool stick_to_bottom = false) :
        stick_to_bottom_(stick_to_bottom) {
        Add(std::move(child));
    }

    ftxui::Element OnRender() override {
        using namespace ftxui;
        auto background = ComponentBase::OnRender();
        background->ComputeRequirement();
        size_ = background->requirement().min_y;
        // Tail-follow: while pinned to the bottom, keep the selection on the last line so newly
        // appended log lines stay visible without manual scrolling.
        if (stick_to_bottom_ && follow_) {
            selected_ = std::max(0, size_ - 1);
        }
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
        // Re-engage tail-follow only when the user scrolls back to the bottom; any scroll-up pins
        // the view so history stays put while new lines arrive.
        if (stick_to_bottom_) {
            follow_ = selected_ >= size_ - 1;
        }
        return selected_old != selected_;
    }

    bool Focusable() const override {
        return true;
    }

private:
    int selected_{0};
    int size_{0};
    bool stick_to_bottom_{false};
    bool follow_{true};
    ftxui::Box box_;
};

ftxui::Component Scroller(ftxui::Component child, bool stick_to_bottom = false) {
    return ftxui::Make<ScrollerBase>(std::move(child), stick_to_bottom);
}

// One plottable numeric series for an instance: a stable key, a human label, a unit and the
// current value. The key is used both to store the time-series history and to remember which
// series the user enabled for plotting.
struct series_desc {
    std::string key;
    std::string label;
    std::string unit;
    double value;
};

// Enumerate, in a stable order, every numeric value available for an instance: heartbeat
// telemetry, ADC channels, GPIO lines and the unstructured IO telemetry entries.
std::vector<series_desc>
collect_plottable_series(std::optional<charge_bridge::utilities::chargebridge_telemetry> const& telemetry,
                         std::optional<std::vector<int>> const& adc, std::optional<std::vector<int>> const& gpio,
                         std::optional<std::vector<std::pair<std::string, int>>> const& io_telemetry) {
    std::vector<series_desc> out;
    if (telemetry.has_value()) {
        out.push_back({"mcu_temp", "MCU temp", "degC", static_cast<double>(telemetry->temperature_mcu_C)});
        out.push_back({"cp_hi", "CP high", "mV", static_cast<double>(telemetry->cp_hi_mV)});
        out.push_back({"cp_lo", "CP low", "mV", static_cast<double>(telemetry->cp_lo_mV)});
        out.push_back({"pp", "PP", "mOhm", static_cast<double>(telemetry->pp_mOhm)});
        out.push_back({"vdd_12", "VDD 12V", "mV", static_cast<double>(telemetry->vdd_12V_mV)});
        out.push_back({"vdd_n12", "VDD -12V", "mV", static_cast<double>(telemetry->vdd_N12V_mV)});
        out.push_back({"vdd_3v3", "VDD 3.3V", "mV", static_cast<double>(telemetry->vdd_3v3_mV)});
    }
    if (adc.has_value()) {
        for (std::size_t i = 0; i < adc->size(); ++i) {
            out.push_back({"adc" + std::to_string(i), "ADC" + std::to_string(i), "", static_cast<double>((*adc)[i])});
        }
    }
    if (gpio.has_value()) {
        for (std::size_t i = 0; i < gpio->size(); ++i) {
            out.push_back(
                {"gpio" + std::to_string(i), "GPIO" + std::to_string(i), "", static_cast<double>((*gpio)[i])});
        }
    }
    if (io_telemetry.has_value()) {
        for (auto const& [name, value] : *io_telemetry) {
            out.push_back({"tlm:" + name, name, "", static_cast<double>(value)});
        }
    }
    return out;
}

// The one series that is always plotted and cannot be toggled off.
constexpr char const* k_always_plotted = "mcu_temp";

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
    row.network = status.network;
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

    // Record the latest value of every numeric series so any of them can be plotted on demand.
    for (auto const& series : collect_plottable_series(row.telemetry, row.adc, row.gpio, row.io_telemetry)) {
        auto& history = row.history[series.key];
        history.push_back(static_cast<float>(series.value));
        while (history.size() > k_telemetry_history) {
            history.pop_front();
        }
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

    // Plot-selection state, alive for the whole loop. plot_enabled[key] == true means the user
    // ticked that series for plotting. click_targets holds the on-screen boxes of the checkbox
    // glyphs (rebuilt every render); a deque keeps the Box references stable for reflect().
    std::map<std::string, bool> plot_enabled;
    // Enable the CPU load series by default so its graph shows on startup (still toggleable). Its
    // key matches collect_plottable_series()'s "tlm:" prefix for io_telemetry entries.
    plot_enabled["tlm:cpu_load_pm"] = true;
    std::deque<std::pair<Box, std::string>> click_targets;

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
            // Spread the available samples across the full plot width (oldest at the left, newest at
            // the right) instead of one-sample-per-column. This keeps the graph using the entire
            // width when the window is wider than the sample count (and downsamples when narrower),
            // rather than only filling the rightmost columns.
            const int last = static_cast<int>(data.size()) - 1;
            for (int x = 0; x < width; ++x) {
                const int idx =
                    width > 1 ? static_cast<int>(std::lround(static_cast<double>(x) * last / (width - 1))) : last;
                const float value = data[static_cast<std::size_t>(idx)];
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

        // Rebuilt every render; the click handler reads it to map clicks to series checkboxes.
        click_targets.clear();

        if (m_status_rows.empty()) {
            return window(text("Instance"), text("no instances configured")) | flex;
        }
        const std::size_t idx =
            (m_selected_row >= 0 && m_selected_row < static_cast<int>(m_status_rows.size()))
                ? static_cast<std::size_t>(m_selected_row)
                : 0;
        auto const& row = m_status_rows[idx];

        auto series = collect_plottable_series(row.telemetry, row.adc, row.gpio, row.io_telemetry);

        Elements sections;

        // CP state from the BSP bridge (if enabled).
        if (row.cp_state.has_value()) {
            sections.push_back(hbox({text("CP state: ") | bold, styled(*row.cp_state, Color::Yellow, true)}));
            sections.push_back(separator());
        }

        // User-entered name prefix (press 'n' / the button to change). Local-only until the protocol
        // to push it to the MCU is defined.
        if (row.name_prefix.has_value() && not row.name_prefix->empty()) {
            sections.push_back(hbox({text("Name prefix: ") | bold, styled(*row.name_prefix, Color::Cyan, true),
                                     text("  (pending — not yet sent)") | dim}));
            sections.push_back(separator());
        }

        // Read-only network identity (IP, MAC, mDNS). MAC is best-effort from the local ARP cache.
        if (row.network.has_value()) {
            auto const& net = *row.network;
            auto info_line = [&](std::string const& label, std::string const& value) {
                auto val = value.empty() ? (styled("N/A", Color::GrayDark, false) | dim) : text(value);
                return hbox({text(label) | dim | size(WIDTH, EQUAL, 10), std::move(val)});
            };
            Elements net_lines;
            net_lines.push_back(text("Network") | bold);
            net_lines.push_back(info_line("IP:", net.ip));
            if (not net.mdns_hostname.empty() || not net.mdns_service.empty() || not net.mdns_txt.empty()) {
                net_lines.push_back(info_line("mDNS:", net.mdns_hostname));
                if (not net.mdns_service.empty()) {
                    net_lines.push_back(info_line("service:", net.mdns_service));
                }
                for (auto const& [key, value] : net.mdns_txt) {
                    net_lines.push_back(info_line(key + ":", value));
                }
            }
            sections.push_back(vbox(std::move(net_lines)));
            sections.push_back(separator());
        }

        if (series.empty()) {
            sections.push_back(text("waiting for telemetry / IO data...") | dim);
        } else {
            sections.push_back(text("Values  (click the box to plot)") | bold);

            // One value row per series with a clickable [ ]/[x] checkbox, grouped by category.
            auto category_of = [](std::string const& key) -> std::string {
                if (key.rfind("adc", 0) == 0) {
                    return "ADC";
                }
                if (key.rfind("gpio", 0) == 0) {
                    return "GPIO";
                }
                if (key.rfind("tlm:", 0) == 0) {
                    return "Telemetry";
                }
                return "Heartbeat";
            };
            constexpr int k_cell_width = 26;
            constexpr std::size_t k_columns = 3;
            std::string category;
            Elements row_cells;
            auto flush_row = [&] {
                if (not row_cells.empty()) {
                    sections.push_back(hbox(std::move(row_cells)));
                    row_cells.clear();
                }
            };
            for (auto const& entry : series) {
                auto cat = category_of(entry.key);
                if (cat != category) {
                    flush_row(); // categories never share a row
                    category = cat;
                    sections.push_back(text(category) | dim);
                }
                const bool always = entry.key == k_always_plotted;
                const bool on = always || plot_enabled[entry.key];
                auto box_glyph = text(always ? "[*] " : (on ? "[x] " : "[ ] "));
                if (not m_options.no_color && on) {
                    box_glyph = box_glyph | color(Color::Green);
                }
                if (not always) {
                    click_targets.emplace_back(Box{}, entry.key);
                    box_glyph = box_glyph | reflect(click_targets.back().first);
                }
                auto value = std::to_string(static_cast<long>(std::lround(entry.value)));
                if (not entry.unit.empty()) {
                    value += " " + entry.unit;
                }
                row_cells.push_back(hbox({std::move(box_glyph), text(entry.label + ": " + value)}) |
                                    size(WIDTH, EQUAL, k_cell_width));
                if (row_cells.size() == k_columns) {
                    flush_row();
                }
            }
            flush_row();

            // Plots: MCU temperature always, plus every series the user enabled.
            sections.push_back(separator());
            sections.push_back(text("Plots") | bold);
            for (auto const& entry : series) {
                const bool plotted = (entry.key == k_always_plotted) || plot_enabled[entry.key];
                if (not plotted) {
                    continue;
                }
                auto it = row.history.find(entry.key);
                if (it != row.history.end()) {
                    sections.push_back(plot(entry.label, entry.unit, it->second));
                }
            }
        }

        return window(text("Instance: " + row.cb_name), vbox(std::move(sections))) | flex;
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
            // A message belongs to the instance if its device is exactly the cb name or a
            // "<cb_name>/<bridge>" child (e.g. "cb_eval" and "cb_eval/bsp", but not "cb_eval2").
            const std::string child_prefix = filter_name + "/";
            for (auto const& entry : m_log_messages) {
                auto const& device = entry.first;
                const bool matches = filter_name.empty() || device == filter_name ||
                                     device.rfind(child_prefix, 0) == 0;
                if (matches) {
                    shown.push_back(entry.second);
                }
            }
        }

        // Chronological order (oldest first, newest last) like a console log. The scroller sticks to
        // the bottom so the latest line stays visible; scroll up (wheel/PgUp) to see history.
        Elements lines;
        for (auto const& line : shown) {
            lines.push_back(text(line));
        }
        if (lines.empty()) {
            lines.push_back(text("(no messages)") | dim);
        }

        std::string title = "Messages [" + (filter_name.empty() ? std::string("all") : filter_name) +
                            "]   (wheel/PgUp/PgDn scroll, f filter)";
        return window(text(title), vbox(std::move(lines)));
    });

    // --- "Set name prefix" button + modal popup -----------------------------
    // A future protocol revision lets the MCU accept an 8-byte name prefix. The button (and the 'n'
    // key) open a modal text entry; the value is stored locally on the instance row for now. Pushing
    // it onto the UDP stack is deferred until the protocol is defined.
    constexpr std::size_t k_name_prefix_max = 8;

    auto selected_idx = [this]() -> int {
        if (m_status_rows.empty()) {
            return -1;
        }
        if (m_selected_row >= 0 && m_selected_row < static_cast<int>(m_status_rows.size())) {
            return m_selected_row;
        }
        return 0;
    };

    auto open_name_modal = [&] {
        const int idx = selected_idx();
        if (idx < 0) {
            return;
        }
        {
            std::scoped_lock lock(m_state_mutex);
            m_name_input_value = m_status_rows[static_cast<std::size_t>(idx)].name_prefix.value_or("");
        }
        m_name_modal_open = true;
    };

    auto commit_name = [&] {
        const int idx = selected_idx();
        if (idx >= 0) {
            std::scoped_lock lock(m_state_mutex);
            m_status_rows[static_cast<std::size_t>(idx)].name_prefix = m_name_input_value;
        }
        m_name_modal_open = false;
        // TODO(cornelius.claussen): once the protocol is defined, push the new name prefix to the MCU.
    };

    InputOption name_input_option;
    name_input_option.content = &m_name_input_value;
    name_input_option.placeholder = "name";
    name_input_option.multiline = false;
    name_input_option.on_change = [this] {
        if (m_name_input_value.size() > k_name_prefix_max) {
            m_name_input_value.resize(k_name_prefix_max);
        }
    };
    name_input_option.on_enter = [&] { commit_name(); };
    auto name_input = Input(name_input_option);

    auto set_button = Button(
        "Set", [&] { commit_name(); }, ButtonOption::Ascii());
    auto cancel_button = Button(
        "Cancel", [&] { m_name_modal_open = false; }, ButtonOption::Ascii());
    auto open_button = Button(
        "Set name prefix", [&] { open_name_modal(); }, ButtonOption::Ascii());

    auto modal_container = Container::Vertical({
        name_input,
        Container::Horizontal({set_button, cancel_button}),
    });
    auto name_modal = Renderer(modal_container, [&] {
        return vbox({
                   text("Set name prefix") | bold,
                   separator(),
                   hbox({text("Name (max 8 chars): "),
                         name_input->Render() | inverted | size(WIDTH, GREATER_THAN, 12)}),
                   text("Stored locally — not yet sent to the MCU (protocol pending).") | dim,
                   separator(),
                   hbox({filler(), set_button->Render(), text("  "), cancel_button->Render()}),
               }) |
               border | size(WIDTH, GREATER_THAN, 46) | clear_under | center;
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

    // Details are wrapped in a scroller (mouse wheel / keys scroll them) with the always-visible
    // "Set name prefix" button pinned above it.
    auto detail_scroller = Scroller(detail);
    auto detail_area = Container::Vertical({open_button, detail_scroller});
    auto detail_panel = Renderer(detail_area, [&] {
        return vbox({
            hbox({open_button->Render(), filler()}),
            separator(),
            detail_scroller->Render() | flex,
        });
    });
    auto split = ResizableSplitLeft(list_panel, detail_panel, &m_list_split_size);

    if (m_options.status_message_lines > 0) {
        m_msg_split_size = std::max(3, static_cast<int>(m_options.status_message_lines) + 2);
        split = ResizableSplitBottom(Scroller(messages, /*stick_to_bottom=*/true), split, &m_msg_split_size);
    }

    auto app = Renderer(split, [&] {
        return vbox({
            text("PIONIX ChargeBridge") | bold | hcenter,
            text("[ click/↑↓ select   wheel scroll   drag borders to resize   n set name   f filter log   q "
                 "quit ]") |
                dim | hcenter,
            separator(),
            split->Render() | flex,
        });
    });

    // Overlay the name-prefix modal on top of everything; ftxui routes events to it while open.
    auto with_modal = Modal(app, name_modal, &m_name_modal_open);

    // Only intercept the global shortcuts; selection, scrolling and border-dragging are handled by
    // the components themselves (menu, scrollers, resizable splits).
    auto root = CatchEvent(with_modal, [&](Event event) {
        // While the modal is open, let its input/buttons handle everything; Escape cancels.
        if (m_name_modal_open) {
            if (event == Event::Escape) {
                m_name_modal_open = false;
                return true;
            }
            return false;
        }
        // Toggle a series plot when its checkbox is clicked; other clicks fall through to the menu
        // / resizable borders.
        if (event.is_mouse() && event.mouse().button == Mouse::Left && event.mouse().motion == Mouse::Pressed) {
            for (auto const& [box, key] : click_targets) {
                if (box.Contain(event.mouse().x, event.mouse().y)) {
                    plot_enabled[key] = not plot_enabled[key];
                    return true;
                }
            }
            return false;
        }
        if (event == Event::Character('n')) {
            open_name_modal();
            return true;
        }
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
