// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

#include <catch2/catch_test_macros.hpp>

#include <iso15118/d20/config.hpp>
#include <iso15118/io/sdp.hpp>
#include <iso15118/io/sdp_packet.hpp>
#include <iso15118/io/stream_view.hpp>
#include <iso15118/message/supported_app_protocol.hpp>
#include <iso15118/message/variant.hpp>
#include <iso15118/session/feedback.hpp>
#include <iso15118/session/iso.hpp>
#include <iso15118/tbd_controller.hpp>

namespace {

iso15118::TbdController make_controller(bool enable_sdp_server,
                                        const iso15118::session::feedback::Callbacks& callbacks) {
    return iso15118::TbdController{
        iso15118::TbdConfig{{}, "lo", iso15118::config::TlsNegotiationStrategy::ACCEPT_CLIENT_OFFER, enable_sdp_server},
        callbacks, iso15118::d20::EvseSetupConfig{}};
}

std::array<int, 2> make_nonblocking_socketpair() {
    std::array<int, 2> fds{-1, -1};

    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, fds.data()) == -1) {
        throw std::runtime_error(std::string("socketpair() failed: ") + std::strerror(errno));
    }

    return fds;
}

bool fd_is_open(int fd) {
    return fd >= 0 and fcntl(fd, F_GETFD) != -1;
}

// Captured SupportedAppProtocolReq offering the -20:AC namespace.
constexpr uint8_t sap_req[] = {0x80, 0x00, 0xf3, 0xab, 0x93, 0x71, 0xd3, 0x4b, 0x9b, 0x79, 0xd3, 0x9b, 0xa3,
                               0x21, 0xd3, 0x4b, 0x9b, 0x79, 0xd1, 0x89, 0xa9, 0x89, 0x89, 0xc1, 0xd1, 0x69,
                               0x91, 0x81, 0xd2, 0x0a, 0x18, 0x01, 0x00, 0x00, 0x04, 0x00, 0x40};

// Wraps an EXI payload in a V2GTP frame (8-byte header + payload), mirroring the framing in
// MockConnection::queue_v2gtp_packet.
std::vector<uint8_t> make_v2gtp_frame(iso15118::io::v2gtp::PayloadType payload_type, const uint8_t* payload,
                                      std::size_t payload_len) {
    std::vector<uint8_t> frame(iso15118::io::SdpPacket::V2GTP_HEADER_SIZE + payload_len);
    frame[0] = iso15118::io::SDP_PROTOCOL_VERSION;
    frame[1] = iso15118::io::SDP_INVERSE_PROTOCOL_VERSION;

    const uint16_t type = htons(static_cast<uint16_t>(payload_type));
    std::memcpy(frame.data() + 2, &type, sizeof(type));

    const uint32_t len = htonl(static_cast<uint32_t>(payload_len));
    std::memcpy(frame.data() + 4, &len, sizeof(len));

    std::memcpy(frame.data() + iso15118::io::SdpPacket::V2GTP_HEADER_SIZE, payload, payload_len);
    return frame;
}

// Reads one full V2GTP frame (8-byte header + declared payload) from a non-blocking fd, retrying
// until the frame is complete or the timeout elapses. Returns std::nullopt on timeout/peer close.
std::optional<std::vector<uint8_t>> read_v2gtp_frame(int fd, std::chrono::milliseconds timeout) {
    using clock = std::chrono::steady_clock;
    constexpr std::size_t header_size = iso15118::io::SdpPacket::V2GTP_HEADER_SIZE;
    const auto deadline = clock::now() + timeout;

    std::vector<uint8_t> buffer;
    std::size_t expected = header_size; // grows once the header is parsed

    while (buffer.size() < expected) {
        uint8_t chunk[256];
        const auto n = ::read(fd, chunk, sizeof(chunk));

        if (n > 0) {
            buffer.insert(buffer.end(), chunk, chunk + n);

            // Parse the declared payload length as soon as the header is complete.
            if (expected == header_size and buffer.size() >= header_size) {
                uint32_t payload_len{};
                std::memcpy(&payload_len, buffer.data() + 4, sizeof(payload_len));
                expected = header_size + ntohl(payload_len);
            }
            continue;
        }

        if (n == 0) {
            return std::nullopt; // peer closed before a full frame arrived
        }

        // n < 0: on a non-blocking socket EAGAIN/EWOULDBLOCK just means "not yet".
        if (errno != EAGAIN and errno != EWOULDBLOCK and errno != EINTR) {
            return std::nullopt;
        }

        if (clock::now() >= deadline) {
            return std::nullopt;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    return buffer;
}

} // namespace

SCENARIO("session_start guard check - invalid/closed fd") {
    iso15118::session::feedback::Callbacks callbacks;
    callbacks.signal = [](auto) {};

    auto controller = make_controller(false, callbacks);

    WHEN("start_session gets an invalid/closed fd") {
        const auto ok = controller.start_session(-1);

        THEN("Session not started") {
            REQUIRE(ok == iso15118::StartSessionResult::FdNotValid);
            REQUIRE_FALSE(controller.has_active_session());
        }
    }
}

SCENARIO("session_start guard check - enable_sdp_server: true") {
    iso15118::session::feedback::Callbacks callbacks;
    callbacks.signal = [](auto) {};

    auto controller = make_controller(true, callbacks);

    const auto fds = make_nonblocking_socketpair();

    WHEN("start_session - sdp_server is enabled") {
        const auto ok = controller.start_session(fds.at(0));

        THEN("Session not started") {
            REQUIRE(ok == iso15118::StartSessionResult::KeepFdOpen);
            REQUIRE_FALSE(controller.has_active_session());
            REQUIRE(fd_is_open(fds.at(0)));
        }
    }

    close(fds.at(0));
    close(fds.at(1));
}

SCENARIO("session_start guard check - session already started") {
    iso15118::session::feedback::Callbacks callbacks;
    callbacks.signal = [](auto) {};

    auto controller = make_controller(false, callbacks);

    controller.tick();

    REQUIRE(controller.has_active_session());

    const auto fds = make_nonblocking_socketpair();

    WHEN("start_session - session already started") {
        const auto ok = controller.start_session(fds.at(0));

        THEN("Start session does not end already started session") {
            REQUIRE(ok == iso15118::StartSessionResult::KeepFdOpen);
            REQUIRE(controller.has_active_session());
            REQUIRE(fd_is_open(fds.at(0)));
        }
    }

    close(fds.at(0));
    close(fds.at(1));
}

SCENARIO("session_start functionality") {
    iso15118::session::feedback::Callbacks callbacks;
    callbacks.signal = [](auto) {};

    auto controller = make_controller(false, callbacks);

    const auto fds = make_nonblocking_socketpair();

    iso15118::StartSessionResult ok{iso15118::StartSessionResult::KeepFdOpen};

    WHEN("start_session") {
        std::thread start_session_thread([&] { ok = controller.start_session(fds.at(0)); });

        // Watchdog: if start_session() never returns (e.g. the session fails to finish), force it out
        // with shutdown() so the test cannot hang forever. Stays armed across the join below.
        std::mutex watchdog_mutex;
        std::condition_variable watchdog_cv;
        bool watchdog_done{false};
        std::atomic_bool timed_out{false};
        std::thread watchdog_thread([&] {
            std::unique_lock<std::mutex> lock(watchdog_mutex);
            if (not watchdog_cv.wait_for(lock, std::chrono::seconds(20), [&] { return watchdog_done; })) {
                timed_out.store(true);
                controller.shutdown();
            }
        });

        // Create a V2GTP message (8-byte header + SupportedAppProtocolReq EXI payload) and send it
        // through the client end of the socketpair.
        const auto request_frame = make_v2gtp_frame(iso15118::io::v2gtp::PayloadType::SAP, sap_req, sizeof(sap_req));
        const auto written = ::write(fds.at(1), request_frame.data(), request_frame.size());

        // Wait for the SupportedAppProtocolRes the server writes back.
        const auto response = read_v2gtp_frame(fds.at(1), std::chrono::seconds(5));

        // Tear down before asserting: closing the client end signals EOF to the server, so its session
        // finishes and start_session() returns. shutdown() is a belt-and-suspenders unblock. Both
        // worker threads are joined here so no assertion below can throw with a thread still running.
        close(fds.at(1));
        controller.shutdown();
        start_session_thread.join();

        {
            std::lock_guard<std::mutex> lock(watchdog_mutex);
            watchdog_done = true;
        }
        watchdog_cv.notify_all();
        watchdog_thread.join();

        THEN("the server answers with a valid SupportedAppProtocolRes and the session ends cleanly") {
            REQUIRE_FALSE(timed_out.load());
            REQUIRE(written == static_cast<ssize_t>(request_frame.size()));

            // The response is a well-formed V2GTP SAP frame.
            constexpr std::size_t header_size = iso15118::io::SdpPacket::V2GTP_HEADER_SIZE;
            REQUIRE(response.has_value());
            REQUIRE(response->size() > header_size);
            REQUIRE(response->at(0) == iso15118::io::SDP_PROTOCOL_VERSION);
            REQUIRE(response->at(1) == iso15118::io::SDP_INVERSE_PROTOCOL_VERSION);

            uint16_t response_type{};
            std::memcpy(&response_type, response->data() + 2, sizeof(response_type));
            REQUIRE(ntohs(response_type) == static_cast<uint16_t>(iso15118::io::v2gtp::PayloadType::SAP));

            // Decode the payload to a SupportedAppProtocolResponse via the library Variant.
            const uint8_t* payload = response->data() + header_size;
            const std::size_t payload_len = response->size() - header_size;

            const iso15118::io::StreamInputView view{payload, payload_len};
            const iso15118::message_20::Variant variant(iso15118::io::v2gtp::PayloadType::SAP, view);
            REQUIRE(variant.get_type() == iso15118::message_20::Type::SupportedAppProtocolRes);

            const auto& sap_res = variant.get<iso15118::message_20::SupportedAppProtocolResponse>();
            REQUIRE(sap_res.response_code ==
                    iso15118::message_20::SupportedAppProtocolResponse::ResponseCode::OK_SuccessfulNegotiation);
            REQUIRE(sap_res.schema_id.has_value());

            // start_session() returned true and reaped the session; ConnectionPlain::close() closed the fd.
            REQUIRE(ok == iso15118::StartSessionResult::SessionComplete);
            REQUIRE_FALSE(controller.has_active_session());
            REQUIRE_FALSE(fd_is_open(fds.at(0)));
        }
    }
}
