// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <variant>
#include <vector>

#include <charge_bridge/utilities/print_status.hpp>
#include <everest/util/queue/thread_safe_queue.hpp>

namespace ftxui {
class ScreenInteractive;
} // namespace ftxui

namespace charge_bridge {

struct status_ui_options {
    utilities::status_output_mode status_output{utilities::status_output_mode::auto_mode};
    // Deprecated: terminal redraws are event-driven via ftxui, this value is ignored in terminal mode.
    std::chrono::milliseconds status_refresh_ms{std::chrono::milliseconds(100)};
    std::size_t status_message_lines{10};
    bool no_color{false};
};

class status_ui {
public:
    explicit status_ui(status_ui_options options, std::vector<std::string> cb_names);
    ~status_ui();

    void publish(utilities::chargebridge_status status);
    void publish_message(std::string device, std::string message);

    // Optional callback invoked when the user quits the terminal UI (e.g. 'q' or Ctrl-C). Used to
    // trigger application shutdown. Must be set before run().
    void set_quit_handler(std::function<void()> handler);

    void run();
    void stop();

private:
    struct status_row {
        std::string cb_name;
        std::optional<utilities::chargebridge_network_info> network;
        std::optional<bool> discovered;
        std::optional<bool> connected;
        std::optional<bool> can0;
        std::optional<bool> serial1;
        std::optional<bool> serial2;
        std::optional<bool> serial3;
        std::optional<bool> plc;
        std::optional<bool> bsp;
        std::optional<bool> heartbeat;
        std::optional<bool> io;
        std::optional<int> mcu_resets;
        std::optional<utilities::chargebridge_telemetry> telemetry;
        std::optional<std::string> cp_state;
        std::optional<std::vector<int>> gpio;
        std::optional<std::vector<int>> adc;
        std::optional<std::vector<std::pair<std::string, int>>> io_telemetry;
        // User-entered name prefix (max 8 bytes), set via the detail panel. Stored locally only for
        // now; the protocol to push it to the MCU is not yet defined (see status_ui.cpp).
        std::optional<std::string> name_prefix;
        // Time-series history for every numeric series, keyed by a stable series id (see
        // collect_plottable_series in the .cpp). Used to render the toggleable plots.
        std::map<std::string, std::deque<float>> history;
    };

    static constexpr std::size_t k_telemetry_history = 120;

    // Events are only used by the non-TTY log loop.
    struct status_update_event {
        utilities::chargebridge_status status;
    };

    struct log_message_event {
        std::string message;
    };

    using status_ui_event = std::variant<status_update_event, log_message_event>;

    bool m_terminal_active() const;

    // Non-TTY key=value logging loop (unchanged behavior).
    void run_log_loop();

    // ftxui-based interactive terminal loop.
    void run_terminal_loop();
    void request_redraw();

    void apply_status_row(utilities::chargebridge_status const& status);
    void apply_log_message(std::string device, std::string message);

    status_ui_options m_options;

    // Producer/consumer queue used only in log mode.
    everest::lib::util::thread_safe_queue<status_ui_event> m_status_queue;

    // Shared UI state, written by publish*/read by the render thread.
    std::mutex m_state_mutex;
    std::vector<status_row> m_status_rows;
    std::unordered_map<std::string, std::size_t> m_cb_name_to_row;
    // Combined chronological log; each entry pairs the originating cb_name (may be empty) with the
    // formatted message line. Capacity is a scrollback buffer larger than the visible window.
    std::deque<std::pair<std::string, std::string>> m_log_messages;
    std::size_t m_log_capacity{0};

    // Instance names, parallel to m_status_rows; bound to the selection Menu (stable for its lifetime).
    std::vector<std::string> m_cb_names;

    // Terminal UI navigation state; only touched on the ftxui loop thread.
    int m_selected_row{0};             // index into m_status_rows / m_cb_names (bound to the Menu)
    bool m_log_filter_selected{false}; // when true, show only the selected instance's messages
    int m_list_split_size{72};         // width of the instance list panel (mouse-draggable)
    int m_msg_split_size{12};          // height of the message panel (mouse-draggable)

    // "Set name prefix" modal state; only touched on the ftxui loop thread.
    bool m_name_modal_open{false};
    std::string m_name_input_value;

    // Screen pointer is valid only while the terminal loop runs; guarded for cross-thread access.
    std::mutex m_screen_mutex;
    ftxui::ScreenInteractive* m_screen{nullptr};

    std::function<void()> m_quit_handler;

    std::atomic_bool m_running{false};
    std::thread m_thread;
};

} // namespace charge_bridge
