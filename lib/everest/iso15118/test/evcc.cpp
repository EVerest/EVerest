// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

// EV (-20) SIL harness: builds an ev::Controller from an EvConfig and runs its
// reactor loop. SDP discovery resolves the SECC endpoint on the configured
// interface; the loop returns once the session finishes or the setup timeout
// elapses. The optional first argument overrides the egress interface (default
// "lo" for a loopback SIL run).

#include <cstdio>
#include <string>

#include <iso15118/detail/helper.hpp>
#include <iso15118/io/logging.hpp>
#include <iso15118/message/type.hpp>
#include <iso15118/session/logger.hpp>

#include <iso15118/ev/config.hpp>
#include <iso15118/ev/controller.hpp>
#include <iso15118/ev/session/feedback.hpp>

int main(int argc, char* argv[]) {
    const std::string interface = (argc > 1) ? argv[1] : "lo";

    iso15118::io::set_logging_callback([](iso15118::LogLevel, std::string msg) { printf("%s\n", msg.c_str()); });
    iso15118::session::logging::set_session_log_callback([](std::size_t, const iso15118::session::logging::Event&) {});

    iso15118::ev::EvConfig config{};
    config.interface_name = interface;
    config.discover = true;
    config.evcc_id = "02:00:00:00:00:01";

    iso15118::ev::feedback::Callbacks callbacks{};
    callbacks.connected = [](const iso15118::io::Ipv6EndPoint&) {
        iso15118::logf_info("EV: connected to SECC data endpoint");
    };
    callbacks.v2g_message = [](iso15118::message_20::Type type) {
        iso15118::logf_info("EV: received V2G message type %d", static_cast<int>(type));
    };
    callbacks.stopped = []() { iso15118::logf_info("EV: session stopped"); };

    iso15118::ev::Controller controller{config, callbacks};
    controller.loop();

    return 0;
}
