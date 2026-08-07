// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <variant>
#include <vector>

#include <charge_bridge/utilities/print_status.hpp>
#include <everest/util/async/monitor.hpp>
#include <everest/util/queue/thread_safe_queue.hpp>

namespace charge_bridge {

struct status_ui_options {
    utilities::status_output_mode status_output{utilities::status_output_mode::auto_mode};
    std::chrono::milliseconds status_refresh_ms{std::chrono::milliseconds(100)};
    std::size_t status_message_lines{10};
    bool no_color{false};
};

class status_ui {
public:
    explicit status_ui(status_ui_options options, std::vector<std::string> cb_names);
    ~status_ui();

    void publish(utilities::chargebridge_status status);
    void publish_message(std::string message);
    void run();
    void stop();

private:
    void run_internal();

    struct status_row {
        std::string cb_name;
        std::optional<bool> discovered;
        std::optional<bool> connected;
        std::optional<bool> can0;
        std::optional<bool> serial1;
        std::optional<bool> serial2;
        std::optional<bool> serial3;
        std::optional<bool> plc;
        std::optional<bool> bsp;
        std::optional<bool> heartbeat;
        std::optional<bool> gpio;
        std::optional<int> mcu_resets;
    };

    struct status_update_event {
        utilities::chargebridge_status status;
    };

    struct log_message_event {
        std::string message;
    };

    using status_ui_event = std::variant<status_update_event, log_message_event>;

    void apply_status_row(utilities::chargebridge_status const& status);
    void apply_log_message(std::string message);
    void process_event(status_ui_event const& event);
    bool has_message_event(status_ui_event const& event);
    void render_terminal_status();

    void render_field(std::string const& value, const char* color, std::size_t width);
    const char* maybe_no_color(const char* color) const;
    void render_message_row(std::string const& text, std::size_t width);
    std::string center(std::string text, std::size_t width);
    void render_border(std::size_t field_count, std::size_t width);

    status_ui_options m_options;
    everest::lib::util::thread_safe_queue<status_ui_event> m_status_queue;
    std::vector<status_row> m_status_rows;
    std::unordered_map<std::string, std::size_t> m_cb_name_to_row;
    std::deque<std::string> m_log_messages;
    std::atomic_bool m_running{false};
    std::thread m_thread;
    std::atomic_bool m_terminal_cursor_hidden{false};

    bool m_terminal_active() const;
    void notify_terminal_ready();
    void wait_for_terminal_ready();

    everest::lib::util::monitor<bool> m_initial_terminal_render_done{false};
};

} // namespace charge_bridge
