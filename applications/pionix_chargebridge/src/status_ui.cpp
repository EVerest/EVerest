// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include <charge_bridge/status_ui.hpp>
#include <charge_bridge/utilities/logging.hpp>

#include <array>
#include <chrono>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>

namespace {

constexpr char ANSI_RESET[] = "\033[0m";
constexpr char ANSI_GREEN[] = "\033[32m";
constexpr char ANSI_BOLD_BRIGHT_RED[] = "\033[1;91m";
constexpr char ANSI_BOLD_WHITE[] = "\033[1;97m";
constexpr char ANSI_BRIGHT_WHITE[] = "\033[97m";
constexpr char ANSI_GRAY[] = "\033[90m";
constexpr char ANSI_CURSOR_HOME[] = "\033[H";
constexpr char ANSI_RETURN_TO_START[] = "\r";
constexpr char ANSI_HIDE_CURSOR[] = "\033[?25l";
constexpr char ANSI_SHOW_CURSOR[] = "\033[?25h";
constexpr char ANSI_CLEAR_TO_END[] = "\033[0J";

constexpr std::size_t STATUS_FIELD_COUNT = 12;
constexpr std::size_t STATUS_FIELD_WIDTH = 12;
constexpr std::size_t STATUS_LINE_WIDTH = 1 + STATUS_FIELD_COUNT * (STATUS_FIELD_WIDTH + 1);
constexpr std::size_t STATUS_MESSAGE_WIDTH = STATUS_LINE_WIDTH - 2;
constexpr std::size_t STATUS_MESSAGE_MAX_LENGTH = 1024;

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
        }
    }

    if (m_terminal_active()) {
        utilities::set_print_error_sink([this](std::string message) { publish_message(std::move(message)); });
    }
}

status_ui::~status_ui() {
    stop();
}

void status_ui::publish(utilities::chargebridge_status status) {
    if (m_options.status_output == utilities::status_output_mode::off) {
        return;
    }

    m_status_queue.push(status_update_event{std::move(status)});
}

void status_ui::publish_message(std::string message) {
    if (m_options.status_output != utilities::status_output_mode::terminal || m_options.status_message_lines == 0) {
        return;
    }

    if (message.size() > STATUS_MESSAGE_MAX_LENGTH) {
        message.resize(STATUS_MESSAGE_MAX_LENGTH);
    }

    m_status_queue.push(log_message_event{std::move(message)});
}

void status_ui::run() {
    if (m_options.status_output == utilities::status_output_mode::off) {
        return;
    }

    bool expected = false;
    if (not m_running.compare_exchange_strong(expected, true)) {
        return;
    }

    m_thread = std::thread(&status_ui::run_internal, this);

    if (m_terminal_active()) {
        wait_for_terminal_ready();
    }
}

void status_ui::notify_terminal_ready() {
    if (not m_terminal_active()) {
        return;
    }

    {
        auto handle = m_initial_terminal_render_done.handle();
        *handle = true;
    }
    m_initial_terminal_render_done.notify_one();
}

void status_ui::wait_for_terminal_ready() {
    auto handle = m_initial_terminal_render_done.handle();
    handle.wait([&handle] { return *handle; });
}

void status_ui::stop() {
    m_status_queue.stop();
    auto const was_running = m_running.exchange(false);

    if (was_running && m_thread.joinable()) {
        m_thread.join();
    }

    if (m_terminal_cursor_hidden.exchange(false)) {
        std::cout << ANSI_SHOW_CURSOR << std::flush;
    }

    if (m_terminal_active()) {
        utilities::clear_print_error_sink();
    }
}

bool status_ui::m_terminal_active() const {
    return m_options.status_output == utilities::status_output_mode::terminal;
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
    row.gpio = status.gpio;
    row.mcu_resets = status.mcu_resets;
}

void status_ui::apply_log_message(std::string message) {
    m_log_messages.push_back(std::move(message));
    if (m_log_messages.size() > m_options.status_message_lines) {
        m_log_messages.pop_front();
    }
}

void status_ui::process_event(status_ui_event const& event) {
    if (auto* status_update = std::get_if<status_update_event>(&event)) {
        apply_status_row(status_update->status);
        return;
    }

    if (auto* log_message = std::get_if<log_message_event>(&event)) {
        apply_log_message(log_message->message);
    }
}

bool status_ui::has_message_event(status_ui_event const& event) {
    return std::holds_alternative<log_message_event>(event);
}

std::string status_ui::center(std::string text, std::size_t width) {
    if (text.size() >= width) {
        return text.substr(0, width);
    }

    const std::size_t left = (width - text.size()) / 2;
    const std::size_t right = width - text.size() - left;
    return std::string(left, ' ') + text + std::string(right, ' ');
}

void status_ui::render_border(std::size_t field_count, std::size_t width) {
    std::cout << '+';
    for (std::size_t i = 0; i < field_count; ++i) {
        std::cout << std::string(width, '-') << '+';
    }
    std::cout << '\n';
}

const char* status_ui::maybe_no_color(const char* color) const {
    return m_options.no_color ? "" : color;
}

void status_ui::render_field(std::string const& value, const char* color, std::size_t width) {
    std::cout << maybe_no_color(color) << center(value, width) << maybe_no_color(ANSI_RESET) << '|';
}

void status_ui::render_message_row(std::string const& text, std::size_t width) {
    std::cout << '|';
    if (text.size() >= width) {
        std::cout << text.substr(0, width);
    } else {
        std::cout << text << std::string(width - text.size(), ' ');
    }
    std::cout << "|\n";
}

void status_ui::render_terminal_status() {
    std::cout << ANSI_RETURN_TO_START << ANSI_CURSOR_HOME << ANSI_CLEAR_TO_END;

    static const std::array<char const*, STATUS_FIELD_COUNT> k_headers = {
        "cb_name", "discovered", "connected", "can0",      "serial1", "serial2",
        "serial3", "plc",        "bsp",       "heartbeat", "gpio",    "mcu_resets",
    };

    render_border(STATUS_FIELD_COUNT, STATUS_FIELD_WIDTH);

    auto render_bool_cell = [this](std::optional<bool> value) {
        if (!value.has_value()) {
            render_field("N/A", ANSI_GRAY, STATUS_FIELD_WIDTH);
            return;
        }

        if (*value) {
            render_field("OK", ANSI_GREEN, STATUS_FIELD_WIDTH);
        } else {
            render_field("ERROR", ANSI_BOLD_BRIGHT_RED, STATUS_FIELD_WIDTH);
        }
    };

    std::cout << '|';
    for (std::size_t col = 0; col < STATUS_FIELD_COUNT; ++col) {
        std::cout << maybe_no_color(ANSI_BOLD_WHITE) << center(k_headers[col], STATUS_FIELD_WIDTH)
                  << maybe_no_color(ANSI_RESET) << '|';
    }
    std::cout << '\n';

    for (auto const& row : m_status_rows) {
        std::cout << '|';
        std::cout << maybe_no_color(ANSI_BOLD_WHITE) << center(row.cb_name, STATUS_FIELD_WIDTH)
                  << maybe_no_color(ANSI_RESET) << '|';

        render_bool_cell(row.discovered);
        render_bool_cell(row.connected);
        render_bool_cell(row.can0);
        render_bool_cell(row.serial1);
        render_bool_cell(row.serial2);
        render_bool_cell(row.serial3);
        render_bool_cell(row.plc);
        render_bool_cell(row.bsp);
        render_bool_cell(row.heartbeat);
        render_bool_cell(row.gpio);

        if (row.mcu_resets.has_value()) {
            render_field(std::to_string(*row.mcu_resets), ANSI_BRIGHT_WHITE, STATUS_FIELD_WIDTH);
        } else {
            render_field("N/A", ANSI_GRAY, STATUS_FIELD_WIDTH);
        }

        std::cout << '\n';
    }

    render_border(STATUS_FIELD_COUNT, STATUS_FIELD_WIDTH);

    if (m_options.status_message_lines > 0) {
        const std::size_t blank_rows = m_options.status_message_lines > m_log_messages.size()
                                           ? m_options.status_message_lines - m_log_messages.size()
                                           : 0;

        std::cout << '+' << std::string(STATUS_MESSAGE_WIDTH, '-') << "+\n";
        std::ostringstream header;
        header << " Messages (showing last " << m_options.status_message_lines << ")";
        auto const header_text = header.str();
        if (header_text.size() >= STATUS_MESSAGE_WIDTH) {
            render_message_row(header_text.substr(0, STATUS_MESSAGE_WIDTH), STATUS_MESSAGE_WIDTH);
        } else {
            render_message_row(header_text, STATUS_MESSAGE_WIDTH);
        }

        for (std::size_t i = 0; i < blank_rows; ++i) {
            render_message_row("", STATUS_MESSAGE_WIDTH);
        }

        for (auto const& message : m_log_messages) {
            render_message_row(message, STATUS_MESSAGE_WIDTH);
        }
    }

    std::cout << std::flush;
}

void status_ui::run_internal() {
    const bool is_terminal = m_terminal_active();
    const bool terminal_with_refresh = is_terminal && (m_options.status_refresh_ms.count() > 0);

    if (is_terminal) {
        m_terminal_cursor_hidden.store(true);
        std::cout << ANSI_HIDE_CURSOR;
        render_terminal_status();
        notify_terminal_ready();
    }

    if (not is_terminal) {
        while (true) {
            auto event = m_status_queue.wait_and_pop();
            if (!event.has_value()) {
                return;
            }

            if (auto* status_event = std::get_if<status_update_event>(&*event)) {
                utilities::print_status(status_event->status, m_options.status_output);
            }

            if (!m_running.load(std::memory_order_acquire)) {
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

    if (is_terminal && not terminal_with_refresh) {
        while (true) {
            auto event = m_status_queue.wait_and_pop();
            if (!event.has_value()) {
                return;
            }

            process_event(*event);
            if (!m_running.load(std::memory_order_acquire)) {
                auto next_event = m_status_queue.try_pop(std::chrono::milliseconds{0});
                while (next_event.has_value()) {
                    process_event(*next_event);
                    next_event = m_status_queue.try_pop(std::chrono::milliseconds{0});
                }
                render_terminal_status();
                return;
            }

            render_terminal_status();
        }
    }

    while (true) {
        auto event = m_status_queue.wait_and_pop();
        if (!event.has_value()) {
            return;
        }

        process_event(*event);
        auto render_deadline = std::chrono::steady_clock::now() + m_options.status_refresh_ms;
        auto should_render_immediately = has_message_event(*event);

        while (m_running.load(std::memory_order_acquire) && !should_render_immediately) {
            const auto now = std::chrono::steady_clock::now();
            if (now >= render_deadline) {
                break;
            }

            const auto timeout = std::chrono::duration_cast<std::chrono::milliseconds>(render_deadline - now);
            const auto next_event = m_status_queue.try_pop(timeout);
            if (!next_event.has_value()) {
                break;
            }

            process_event(*next_event);
            if (has_message_event(*next_event)) {
                should_render_immediately = true;
            }
        }

        if (!m_running.load(std::memory_order_acquire)) {
            auto next_event = m_status_queue.try_pop(std::chrono::milliseconds{0});
            while (next_event.has_value()) {
                process_event(*next_event);
                next_event = m_status_queue.try_pop(std::chrono::milliseconds{0});
            }
            render_terminal_status();
            return;
        }

        render_terminal_status();
    }
}

} // namespace charge_bridge
