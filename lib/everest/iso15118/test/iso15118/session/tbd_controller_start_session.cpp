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
#include <iso15118/io/logging.hpp>
#include <iso15118/io/sdp.hpp>
#include <iso15118/io/sdp_packet.hpp>
#include <iso15118/io/stream_view.hpp>
#include <iso15118/message/session_setup.hpp>
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
        callbacks, iso15118::session::EvseSetupConfig{}};
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

// Captured SessionSetupReq with a zeroed session id (starts a new session).
constexpr uint8_t session_setup_req[] = {0x80, 0x8c, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x9f,
                                         0x9c, 0x2b, 0xd0, 0x62, 0x0b, 0x2b, 0xa6, 0xa4, 0xab, 0x18, 0x99, 0x19, 0x9a,
                                         0x1a, 0x9b, 0x1b, 0x9c, 0x1c, 0x98, 0x20, 0xa1, 0x21, 0xa2, 0x22, 0xac, 0x00};

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

class SessionWatchdog {
public:
    SessionWatchdog(iso15118::TbdController& controller, std::chrono::seconds timeout) {
        thread = std::thread([this, &controller, timeout] {
            std::unique_lock<std::mutex> lock(mutex);
            if (not cv.wait_for(lock, timeout, [this] { return done; })) {
                timed_out_flag.store(true);
                controller.shutdown();
            }
        });
    }

    ~SessionWatchdog() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            done = true;
        }
        cv.notify_all();
        thread.join();
    }

    SessionWatchdog(const SessionWatchdog&) = delete;
    SessionWatchdog& operator=(const SessionWatchdog&) = delete;

    bool timed_out() const {
        return timed_out_flag.load();
    }

private:
    std::mutex mutex;
    std::condition_variable cv;
    bool done{false};
    std::atomic_bool timed_out_flag{false};
    std::thread thread;
};

struct RoundTripResult {
    iso15118::StartSessionResult ok{};
    ssize_t written{0};
    std::optional<std::vector<uint8_t>> response;
    bool timed_out{false};
};

RoundTripResult run_start_session_round_trip(iso15118::TbdController& controller, std::array<int, 2>& fds,
                                             iso15118::io::v2gtp::PayloadType payload_type, const uint8_t* req,
                                             std::size_t req_len, bool skip_app_protocol_negotiation) {
    RoundTripResult result;
    auto start_options = iso15118::StartSessionOptions{};

    std::thread start_session_thread([&] {
        if (skip_app_protocol_negotiation) {
            start_options.skip_app_protocol_negotiation = true;
        }
        result.ok = controller.start_session(fds.at(0), start_options);
    });

    SessionWatchdog watchdog(controller, std::chrono::seconds(20));

    const auto request_frame = make_v2gtp_frame(payload_type, req, req_len);
    result.written = ::write(fds.at(1), request_frame.data(), request_frame.size());

    result.response = read_v2gtp_frame(fds.at(1), std::chrono::seconds(5));

    close(fds.at(1));
    controller.shutdown();
    start_session_thread.join();

    result.timed_out = watchdog.timed_out();
    return result;
}

template <typename Response>
Response require_response(const std::optional<std::vector<uint8_t>>& response,
                          iso15118::io::v2gtp::PayloadType payload_type) {
    constexpr std::size_t header_size = iso15118::io::SdpPacket::V2GTP_HEADER_SIZE;

    REQUIRE(response.has_value());
    REQUIRE(response->size() > header_size);
    REQUIRE(response->at(0) == iso15118::io::SDP_PROTOCOL_VERSION);
    REQUIRE(response->at(1) == iso15118::io::SDP_INVERSE_PROTOCOL_VERSION);

    uint16_t response_type{};
    std::memcpy(&response_type, response->data() + 2, sizeof(response_type));
    REQUIRE(ntohs(response_type) == static_cast<uint16_t>(payload_type));

    const uint8_t* payload = response->data() + header_size;
    const std::size_t payload_len = response->size() - header_size;

    const iso15118::io::StreamInputView view{payload, payload_len};
    const iso15118::message_20::Variant variant(payload_type, view);
    const auto exptected_type = iso15118::message_20::TypeTrait<Response>::type;
    REQUIRE(variant.get_type() == exptected_type);

    return variant.get<Response>();
}

} // namespace

SCENARIO("session_start guard check - invalid/closed fd") {
    iso15118::session::feedback::Callbacks callbacks;
    callbacks.signal = [](auto) {};

    auto controller = make_controller(false, callbacks);
    const auto start_options = iso15118::StartSessionOptions{};

    WHEN("start_session gets an invalid/closed fd") {
        const auto ok = controller.start_session(-1, start_options);

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
    const auto start_options = iso15118::StartSessionOptions{};

    WHEN("start_session - sdp_server is enabled") {
        const auto ok = controller.start_session(fds.at(0), start_options);

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
    const auto start_options = iso15118::StartSessionOptions{};

    WHEN("start_session - session already started") {
        const auto ok = controller.start_session(fds.at(0), start_options);

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

    auto fds = make_nonblocking_socketpair();

    WHEN("start_session") {
        // Drive one full round-trip: send a SupportedAppProtocolReq and read the response.
        const auto result = run_start_session_round_trip(controller, fds, iso15118::io::v2gtp::PayloadType::SAP,
                                                         sap_req, sizeof(sap_req), false);

        THEN("the server answers with a valid SupportedAppProtocolRes and the session ends cleanly") {
            REQUIRE_FALSE(result.timed_out);
            REQUIRE(result.written ==
                    static_cast<ssize_t>(iso15118::io::SdpPacket::V2GTP_HEADER_SIZE + sizeof(sap_req)));

            const auto sap_res = require_response<iso15118::message_20::SupportedAppProtocolResponse>(
                result.response, iso15118::io::v2gtp::PayloadType::SAP);
            REQUIRE(sap_res.response_code ==
                    iso15118::message_20::SupportedAppProtocolResponse::ResponseCode::OK_SuccessfulNegotiation);
            REQUIRE(sap_res.schema_id.has_value());

            // start_session() returned true and reaped the session; ConnectionPlain::close() closed the fd.
            REQUIRE(result.ok == iso15118::StartSessionResult::SessionComplete);
            REQUIRE_FALSE(controller.has_active_session());
            REQUIRE_FALSE(fd_is_open(fds.at(0)));
        }
    }
}

SCENARIO("session_start functionality - skip sap") {
    iso15118::session::feedback::Callbacks callbacks;
    callbacks.signal = [](auto) {};

    auto controller = make_controller(false, callbacks);

    auto fds = make_nonblocking_socketpair();

    WHEN("start_session - skip sap") {
        // Drive one full round-trip with app-protocol negotiation skipped: send a SessionSetupReq
        // straight away and read the response.
        const auto result = run_start_session_round_trip(controller, fds, iso15118::io::v2gtp::PayloadType::Part20Main,
                                                         session_setup_req, sizeof(session_setup_req), true);

        THEN("the server answers with a valid SessionSetupRes and the session ends cleanly") {
            REQUIRE_FALSE(result.timed_out);
            REQUIRE(result.written ==
                    static_cast<ssize_t>(iso15118::io::SdpPacket::V2GTP_HEADER_SIZE + sizeof(session_setup_req)));

            const auto session_setup_res = require_response<iso15118::message_20::SessionSetupResponse>(
                result.response, iso15118::io::v2gtp::PayloadType::Part20Main);
            REQUIRE(session_setup_res.response_code ==
                    iso15118::message_20::datatypes::ResponseCode::OK_NewSessionEstablished);

            // start_session() returned true and reaped the session; ConnectionPlain::close() closed the fd.
            REQUIRE(result.ok == iso15118::StartSessionResult::SessionComplete);
            REQUIRE_FALSE(controller.has_active_session());
            REQUIRE_FALSE(fd_is_open(fds.at(0)));
        }
    }
}
