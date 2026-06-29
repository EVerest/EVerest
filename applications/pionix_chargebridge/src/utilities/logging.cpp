// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#include <charge_bridge/utilities/logging.hpp>

#include <everest/util/async/monitor.hpp>

#include <iomanip>
#include <sstream>
#include <string>

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

class null_buffer : public std::streambuf {
public:
    int overflow(int c) override {
        return c;
    }
};

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
            m_buffer.clear();
            return;
        }

        if (!m_buffer.empty() && m_buffer.back() == '\n') {
            m_buffer.pop_back();
        }

        if (!m_buffer.empty()) {
            auto sink = current_print_error_sink();
            if (sink) {
                sink(m_device, std::move(m_buffer));
            }
        }

        m_buffer.clear();
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
    } else {
        oss << "ERROR ( " << status << " ) ";
    }

    return oss.str();
}

std::string print_error_prefix_ansi(std::string const& device, std::string const& unit, color level, int status) {
    std::ostringstream oss;
    if (status == 0) {
        return {};
    }

    oss << "[ " << level << std::setw(13) << std::left << unit << color::terminal << " ] " << color::unit
        << std::setw(20) << device << color::terminal << " ";
    if (status == -1) {
        oss << color::standard << "WARNING ";
    } else {
        oss << color::standard << "ERROR ( " << status << " ) ";
    }

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
    return capture_stream;
}

inline std::ostream& capture_print_info(std::string const& device, std::string const& unit) {
    thread_local print_error_capture_streambuf capture_buffer;
    thread_local std::ostream capture_stream(&capture_buffer);

    capture_buffer.reset(device, print_info_prefix_plain(device, unit));
    return capture_stream;
}

inline std::ostream& null_stream() {
    static null_buffer buffer;
    static std::ostream stream(&buffer);
    return stream;
}

std::ostream& print_error(std::string const& device, std::string const& unit, int status) {
    // clang-format off
    auto ctrl =
        status == 0 ? color::success :
        status == -1 ? color::warning:
        color::error;

    if (status == 0 ){ return null_stream(); }

    if (current_print_error_sink()) {
        return capture_print_error(device, unit, status);
    }

    std::cout << print_error_prefix_ansi(device, unit, ctrl, status);
    return std::cout << color::standard;
    // clang-format on
}

std::ostream& print_info(std::string const& device, std::string const& unit) {
    if (current_print_error_sink()) {
        return capture_print_info(device, unit);
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
