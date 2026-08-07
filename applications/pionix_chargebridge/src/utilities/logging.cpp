// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include <charge_bridge/utilities/logging.hpp>

#include <everest/util/async/monitor.hpp>

#include <atomic>
#include <iomanip>
#include <sstream>
#include <string>
#include <unistd.h>

namespace charge_bridge::utilities {

enum class color {
    error,
    success,
    warning,
    message,
    unit,
    standard,
    terminal,
};

std::ostream& operator<<(std::ostream& s, color c) {
    switch (c) {
    case color::error:
        s << "\033[31m";
        break;
    case color::success:
        s << "\033[32m";
        break;
    case color::warning:
        s << "\033[33m";
        break;
    case color::message:
        s << "\033[37m";
        break;
    case color::unit:
        s << "\033[1;37m";
        break;
    case color::terminal:
        s << "\033[m";
        break;
    case color::standard:
    default:
        s << "\033[39;49m";
        break;
    }
    return s;
}

namespace {

// Initialized before main() runs, so the very first diagnostic already gets it right; main()
// overrides it once the command line is parsed (--status-no-color).
std::atomic<bool> diagnostic_color{::isatty(STDOUT_FILENO) != 0};

using print_error_sink_storage = print_error_sink;

everest::lib::util::monitor<print_error_sink_storage> print_error_sink_monitor{print_error_sink{}};

print_error_sink current_print_error_sink() {
    auto handle = print_error_sink_monitor.handle();
    return *handle;
}

class print_error_capture_streambuf : public std::streambuf {
public:
    void reset(std::string const& device, std::string const& prefix) {
        publish_line();
        m_device = device;
        m_buffer = prefix;
    }

    int overflow(int c) override {
        if (c == traits_type::eof()) {
            return traits_type::not_eof(c);
        }

        if (m_buffer.size() < k_print_error_max_length) {
            m_buffer.push_back(static_cast<char>(c));
        }

        if (c == '\n') {
            publish_line();
        }

        return c;
    }

    int sync() override {
        publish_line();
        return 0;
    }

private:
    void publish_line() {
        if (m_buffer.empty()) {
            return;
        }

        if (m_buffer.back() == '\n') {
            m_buffer.pop_back();
        }

        // Take the line out of the buffer before publishing it: if the sink throws, the line is
        // dropped once instead of being retried (and throwing again) on every later reset().
        auto line = std::move(m_buffer);
        m_buffer.clear();

        if (line.empty()) {
            return;
        }

        auto sink = current_print_error_sink();
        if (sink) {
            sink(m_device, std::move(line));
            return;
        }

        // The sink can be cleared between print_error()'s sample and this flush - status_ui::stop()
        // does exactly that while the manager threads are still logging, which is where the shutdown
        // diagnostics live. The line is already fully formatted (prefix included), so write it where
        // print_error() would have written it instead of dropping it. This cannot double-print: in
        // log/off mode no sink is ever installed, so print_error() never routes through this capture
        // buffer in the first place.
        std::cout << line << std::endl;
    }

    static constexpr std::size_t k_print_error_max_length = 2048;
    std::string m_device;
    std::string m_buffer;
};

std::string print_error_prefix_plain(std::string const& device, std::string const& unit, int status) {
    std::ostringstream oss;
    oss << "[ " << std::left << std::setw(13) << unit << " ] " << std::left << std::setw(20) << device << " ";
    if (status == -1) {
        oss << "WARNING ";
    } else if (status != 0) {
        oss << "ERROR ( " << status << " ) ";
    }
    // status == 0 is a success/recovery line: just the prefix, no ERROR/WARNING marker.

    return oss.str();
}

std::string print_error_prefix_ansi(std::string const& device, std::string const& unit, color level, int status) {
    std::ostringstream oss;
    oss << "[ " << level << std::setw(13) << std::left << unit << color::terminal << " ] " << color::unit
        << std::setw(20) << device << color::terminal << " ";
    if (status == -1) {
        oss << color::standard << "WARNING ";
    } else if (status != 0) {
        oss << color::standard << "ERROR ( " << status << " ) ";
    }
    // status == 0 is a success/recovery line: the (green) prefix only, no ERROR/WARNING marker.

    return oss.str();
}

std::string print_info_prefix_plain(std::string const& device, std::string const& unit) {
    std::ostringstream oss;
    oss << "[ " << std::left << std::setw(13) << unit << " ] " << std::left << std::setw(20) << device << " ";
    return oss.str();
}

std::string print_info_prefix_ansi(std::string const& device, std::string const& unit) {
    std::ostringstream oss;
    oss << "[ " << color::message << std::setw(13) << std::left << unit << color::terminal << " ] " << color::unit
        << std::setw(20) << std::left << device << color::terminal << " ";
    return oss.str();
}

} // namespace

inline std::ostream& capture_print_error(std::string const& device, std::string const& unit, int status) {
    thread_local print_error_capture_streambuf capture_buffer;
    thread_local std::ostream capture_stream(&capture_buffer);

    auto const prefix = print_error_prefix_plain(device, unit, status);
    capture_buffer.reset(device, prefix);
    // Stream error state is sticky: one failed line (e.g. a sink that threw, which the stream turns
    // into badbit) would silence this thread for the rest of its life. Every call starts a new line.
    capture_stream.clear();
    return capture_stream;
}

inline std::ostream& capture_print_info(std::string const& device, std::string const& unit) {
    thread_local print_error_capture_streambuf capture_buffer;
    thread_local std::ostream capture_stream(&capture_buffer);

    capture_buffer.reset(device, print_info_prefix_plain(device, unit));
    // See capture_print_error(): clear any sticky error state from a previous line.
    capture_stream.clear();
    return capture_stream;
}

void set_diagnostic_color_enabled(bool enabled) {
    diagnostic_color.store(enabled);
}

bool diagnostic_color_enabled() {
    return diagnostic_color.load();
}

std::ostream& print_error(std::string const& device, std::string const& unit, int status) {
    if (current_print_error_sink()) {
        return capture_print_error(device, unit, status);
    }

    // Without a sink the line goes to stdout, which in log mode is regularly a file or the journal:
    // colorize it only when colors are wanted there, otherwise the escapes pollute the log and break
    // downstream parsing (print_status_log() makes the same distinction). The sink's own fallback path
    // (publish_line()) is unaffected: it writes the plain prefix.
    if (not diagnostic_color_enabled()) {
        return std::cout << print_error_prefix_plain(device, unit, status);
    }

    // clang-format off
    auto ctrl =
        status == 0 ? color::success :
        status == -1 ? color::warning:
        color::error;
    // clang-format on

    std::cout << print_error_prefix_ansi(device, unit, ctrl, status);
    return std::cout << color::standard;
}

std::ostream& print_info(std::string const& device, std::string const& unit) {
    if (current_print_error_sink()) {
        return capture_print_info(device, unit);
    }

    // See print_error(): no escapes unless colors are wanted on stdout.
    if (not diagnostic_color_enabled()) {
        return std::cout << print_info_prefix_plain(device, unit);
    }

    std::cout << print_info_prefix_ansi(device, unit);
    return std::cout << color::standard;
}

void set_print_error_sink(print_error_sink sink) {
    auto handle = print_error_sink_monitor.handle();
    *handle = std::move(sink);
}

void clear_print_error_sink() {
    auto handle = print_error_sink_monitor.handle();
    handle.operator*() = print_error_sink{};
}

} // namespace charge_bridge::utilities
