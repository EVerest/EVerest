// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include <signal.h>

#include <everest/io/shm/shm_client.hpp>
#include <everest/io/shm/shm_server.hpp>

namespace everest::lib::io::examples::shm_ping_pong {

constexpr auto ping_topic = "ping_pong/ping";
constexpr auto pong_topic = "ping_pong/pong";
constexpr auto default_shm_name = "/everest-shm-ping-pong";
constexpr auto default_control_socket = "/tmp/everest-shm-ping-pong.sock";

inline volatile sig_atomic_t g_running = 1;

inline void signal_handler(int signum) {
    (void)signum;
    g_running = 0;
}

inline void install_signal_handlers() {
    struct sigaction action;
    std::memset(&action, 0, sizeof(action));
    action.sa_handler = signal_handler;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);
}

struct runtime_options {
    std::string shm_name{default_shm_name};
    std::string control_socket{default_control_socket};
};

inline void print_usage(std::string_view binary, std::ostream& out) {
    out << "USAGE:\n";
    out << "  " << binary << " [--control-socket PATH] [--shm-name NAME]\n\n";
    out << "Defaults:\n";
    out << "  --control-socket " << default_control_socket << "\n";
    out << "  --shm-name " << default_shm_name << "\n";
}

inline bool parse_args(int argc, char* argv[], runtime_options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0], std::cout);
            return false;
        }
        if (arg == "--control-socket") {
            if (++i >= argc) {
                std::cerr << "ERROR: --control-socket requires a path\n";
                return false;
            }
            options.control_socket = argv[i];
            continue;
        }
        if (arg == "--shm-name") {
            if (++i >= argc) {
                std::cerr << "ERROR: --shm-name requires a POSIX SHM name\n";
                return false;
            }
            options.shm_name = argv[i];
            continue;
        }

        std::cerr << "ERROR: unknown argument '" << arg << "'\n";
        print_usage(argv[0], std::cerr);
        return false;
    }

    if (options.control_socket.empty()) {
        std::cerr << "ERROR: control socket path must not be empty\n";
        return false;
    }
    if (options.shm_name.empty() || options.shm_name.front() != '/') {
        std::cerr << "ERROR: SHM name must be a non-empty POSIX SHM name starting with '/'\n";
        return false;
    }
    return true;
}

inline everest::lib::io::shm::server_options make_server_options(const runtime_options& runtime) {
    everest::lib::io::shm::server_options options;
    options.shm_name = runtime.shm_name;
    options.control_socket_name = runtime.control_socket;
    options.control_socket_abstract_namespace = false;
    options.unlink_shm_on_close = true;
    options.unlink_control_socket_on_close = true;
    options.topics = {
        {ping_topic, 128, 1024},
        {pong_topic, 128, 1024},
    };
    return options;
}

inline everest::lib::io::shm::client_options make_client_options(const runtime_options& runtime,
                                                                 std::string client_id) {
    everest::lib::io::shm::client_options options;
    options.client_id = std::move(client_id);
    options.control.server_name = runtime.control_socket;
    options.control.server_abstract_namespace = false;
    return options;
}

inline bool uds_bind_unavailable(const everest::lib::io::shm::io_result& result) {
    return result.status == everest::lib::io::shm::io_status::resource_error &&
           result.message.find("Operation not permitted") != std::string::npos &&
           result.message.find("bind UDS server") != std::string::npos;
}

inline bool log_io_error(const everest::lib::io::shm::io_result& result, std::string_view context) {
    if (result.status == everest::lib::io::shm::io_status::ok) {
        return false;
    }

    std::cerr << "ERROR: " << context << ": " << everest::lib::io::shm::to_string(result.status);
    if (!result.message.empty()) {
        std::cerr << ": " << result.message;
    }
    std::cerr << "\n";
    return true;
}

inline std::uint64_t now_ms() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

inline std::string make_ping_payload(std::uint64_t seq) {
    std::ostringstream os;
    os << R"({"type":"ping","from":"client_a","seq":)" << seq << R"(,"ts_ms":)" << now_ms() << "}";
    return os.str();
}

inline std::string make_pong_payload(std::uint64_t seq, std::uint64_t reply_to) {
    std::ostringstream os;
    os << R"({"type":"pong","from":"client_b","seq":)" << seq << R"(,"reply_to":)" << reply_to << R"(,"ts_ms":)"
       << now_ms() << "}";
    return os.str();
}

inline std::uint64_t extract_json_uint(std::string_view payload, std::string_view key, std::uint64_t fallback) {
    const std::string needle = "\"" + std::string(key) + "\":";
    const auto key_pos = payload.find(needle);
    if (key_pos == std::string_view::npos) {
        return fallback;
    }

    auto value_pos = key_pos + needle.size();
    while (value_pos < payload.size() && payload[value_pos] == ' ') {
        ++value_pos;
    }

    std::uint64_t value = 0;
    bool saw_digit = false;
    while (value_pos < payload.size() && payload[value_pos] >= '0' && payload[value_pos] <= '9') {
        saw_digit = true;
        const auto digit = static_cast<std::uint64_t>(payload[value_pos] - '0');
        if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U) {
            return fallback;
        }
        value = value * 10U + digit;
        ++value_pos;
    }

    return saw_digit ? value : fallback;
}

} // namespace everest::lib::io::examples::shm_ping_pong
