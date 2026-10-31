// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

// Drives WebsocketLibwebsockets against a raw TCP server on loopback that writes WebSocket frames byte by byte,
// so tests control fragmentation and can announce frame lengths without sending the payload.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <openssl/evp.h>

#include <ocpp/common/websocket/websocket_libwebsockets.hpp>

#include "evse_security_mock.hpp"

using namespace std::chrono_literals;

namespace ocpp {
namespace {

constexpr auto TIMEOUT = 5s;
constexpr std::uint8_t OPCODE_CONTINUATION = 0x0;
constexpr std::uint8_t OPCODE_TEXT = 0x1;
constexpr std::uint8_t OPCODE_CLOSE = 0x8;
constexpr std::uint16_t CLOSE_STATUS_MESSAGE_TOO_BIG = 1009;

std::string websocket_accept(const std::string& key) {
    const std::string input = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int digest_size = 0;
    EVP_Digest(input.data(), input.size(), digest.data(), &digest_size, EVP_sha1(), nullptr);
    std::array<unsigned char, std::size_t{4} * EVP_MAX_MD_SIZE> encoded{};
    const auto encoded_size = EVP_EncodeBlock(encoded.data(), digest.data(), static_cast<int>(digest_size));
    return {encoded.begin(), encoded.begin() + encoded_size};
}

std::string to_lower(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) { return std::tolower(c); });
    return str;
}

/// \brief Plain ws:// server for a single client connection at a time
class RawWebsocketServer {
public:
    RawWebsocketServer() {
        listen_fd = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): POSIX socket API
        bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        listen(listen_fd, 4);
        socklen_t len = sizeof(addr);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): POSIX socket API
        getsockname(listen_fd, reinterpret_cast<sockaddr*>(&addr), &len);
        listen_port = ntohs(addr.sin_port);
    }

    ~RawWebsocketServer() {
        close_client();
        ::close(listen_fd);
    }

    RawWebsocketServer(const RawWebsocketServer&) = delete;
    RawWebsocketServer& operator=(const RawWebsocketServer&) = delete;
    RawWebsocketServer(RawWebsocketServer&&) = delete;
    RawWebsocketServer& operator=(RawWebsocketServer&&) = delete;

    std::uint16_t port() const {
        return listen_port;
    }

    bool accept_and_handshake() {
        close_client();
        pollfd pfd{listen_fd, POLLIN, 0};
        if (poll(&pfd, 1, static_cast<int>(std::chrono::milliseconds(TIMEOUT).count())) <= 0) {
            return false;
        }
        client_fd = accept(listen_fd, nullptr, nullptr);
        if (client_fd < 0) {
            return false;
        }

        std::string request;
        while (request.find("\r\n\r\n") == std::string::npos) {
            const auto chunk = read_some();
            if (chunk.empty()) {
                return false;
            }
            request += chunk;
        }
        const std::string key_header = "sec-websocket-key:";
        const auto key_pos = to_lower(request).find(key_header);
        if (key_pos == std::string::npos) {
            return false;
        }
        const auto value_start = request.find_first_not_of(' ', key_pos + key_header.size());
        const auto value_end = request.find("\r\n", value_start);
        const auto key = request.substr(value_start, value_end - value_start);

        return send_raw("HTTP/1.1 101 Switching Protocols\r\n"
                        "Upgrade: websocket\r\n"
                        "Connection: Upgrade\r\n"
                        "Sec-WebSocket-Accept: " +
                        websocket_accept(key) +
                        "\r\n"
                        "Sec-WebSocket-Protocol: ocpp2.0.1\r\n\r\n");
    }

    /// \brief Sends a frame header announcing \p length payload bytes; the payload follows with send_raw()
    bool send_frame_header(std::uint8_t opcode, bool fin, std::uint64_t length) {
        std::string header;
        header += static_cast<char>((fin ? 0x80 : 0x00) | opcode);
        if (length < 126) {
            header += static_cast<char>(length);
        } else if (length <= 0xFFFF) {
            header += static_cast<char>(126);
            header += static_cast<char>((length >> 8) & 0xFF);
            header += static_cast<char>(length & 0xFF);
        } else {
            header += static_cast<char>(127);
            for (int shift = 56; shift >= 0; shift -= 8) {
                header += static_cast<char>((length >> shift) & 0xFF);
            }
        }
        return send_raw(header);
    }

    bool send_frame(std::uint8_t opcode, bool fin, const std::string& payload) {
        return send_frame_header(opcode, fin, payload.size()) and send_raw(payload);
    }

    bool send_raw(const std::string& data) {
        std::size_t sent = 0;
        while (sent < data.size()) {
            const auto n = ::send(client_fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
            if (n <= 0) {
                return false;
            }
            sent += static_cast<std::size_t>(n);
        }
        return true;
    }

    /// \brief Reads the client's frames until a close frame arrives and returns its status code
    std::optional<std::uint16_t> wait_for_close_status() {
        while (true) {
            if (not fill(2)) {
                return std::nullopt;
            }
            const auto opcode = static_cast<std::uint8_t>(buffer[0] & 0x0F);
            std::uint64_t length = static_cast<std::uint8_t>(buffer[1]) & 0x7F;
            std::size_t header_size = 2;
            if (length == 126 or length == 127) {
                const std::size_t extended = length == 126 ? 2 : 8;
                if (not fill(2 + extended)) {
                    return std::nullopt;
                }
                length = 0;
                for (std::size_t i = 0; i < extended; ++i) {
                    length = (length << 8) | static_cast<std::uint8_t>(buffer[2 + i]);
                }
                header_size += extended;
            }
            const std::size_t mask_offset = header_size;
            header_size += 4; // client frames are always masked
            if (not fill(header_size + length)) {
                return std::nullopt;
            }
            std::string payload = buffer.substr(header_size, length);
            for (std::size_t i = 0; i < payload.size(); ++i) {
                payload[i] = static_cast<char>(payload[i] ^ buffer[mask_offset + (i % 4)]);
            }
            buffer.erase(0, header_size + length);

            if (opcode == OPCODE_CLOSE) {
                if (payload.size() < 2) {
                    return std::nullopt;
                }
                return static_cast<std::uint16_t>((static_cast<std::uint8_t>(payload[0]) << 8) |
                                                  static_cast<std::uint8_t>(payload[1]));
            }
        }
    }

private:
    int listen_fd{-1};
    int client_fd{-1};
    std::uint16_t listen_port{0};
    std::string buffer;

    void close_client() {
        if (client_fd >= 0) {
            ::close(client_fd);
            client_fd = -1;
        }
        buffer.clear();
    }

    std::string read_some() {
        pollfd pfd{client_fd, POLLIN, 0};
        if (poll(&pfd, 1, static_cast<int>(std::chrono::milliseconds(TIMEOUT).count())) <= 0) {
            return {};
        }
        std::array<char, 4096> chunk{};
        const auto n = ::recv(client_fd, chunk.data(), chunk.size(), 0);
        if (n <= 0) {
            return {};
        }
        return {chunk.data(), static_cast<std::size_t>(n)};
    }

    bool fill(std::size_t size) {
        while (buffer.size() < size) {
            const auto chunk = read_some();
            if (chunk.empty()) {
                return false;
            }
            buffer += chunk;
        }
        return true;
    }
};

class WebsocketReceiveTest : public testing::Test {
protected:
    RawWebsocketServer server;
    std::shared_ptr<WebsocketLibwebsockets> websocket;
    std::mutex messages_mutex;
    std::condition_variable messages_cv;
    std::vector<std::string> messages;

    void TearDown() override {
        if (websocket) {
            websocket->disconnect(WebsocketCloseReason::Normal);
        }
    }

    void connect(std::size_t max_incoming_message_size) {
        WebsocketConnectionOptions options;
        options.csms_uri = Uri::parse_and_validate("ws://127.0.0.1:" + std::to_string(server.port()), "cp001", 1);
        options.security_profile = 1;
        options.ocpp_versions = {OcppProtocolVersion::v201};
        options.authorization_key = "DummyAuthorizationKey123";
        options.message_timeout = 5s;
        options.retry_backoff_random_range_s = 0;
        options.retry_backoff_repeat_times = 0;
        options.retry_backoff_wait_minimum_s = 0;
        options.max_connection_attempts = 10;
        options.ping_interval_s = 0;
        options.pong_timeout_s = 0;
        options.use_ssl_default_verify_paths = false;
        options.verify_csms_common_name = false;
        options.use_tpm_tls = false;
        options.verify_csms_allow_wildcards = false;
        options.max_incoming_message_size = max_incoming_message_size;

        auto evse_security = std::make_shared<testing::NiceMock<EvseSecurityMock>>();
        websocket = std::make_shared<WebsocketLibwebsockets>(options, evse_security);
        websocket->register_connected_callback([](OcppProtocolVersion) {});
        websocket->register_disconnected_callback([]() {});
        websocket->register_stopped_connecting_callback([](WebsocketCloseReason) {});
        websocket->register_message_callback([this](const std::string& message) {
            const std::lock_guard lock(messages_mutex);
            messages.push_back(message);
            messages_cv.notify_all();
        });
        ASSERT_TRUE(websocket->start_connecting());
        ASSERT_TRUE(server.accept_and_handshake());
    }

    std::vector<std::string> wait_for_messages(std::size_t count) {
        std::unique_lock lock(messages_mutex);
        messages_cv.wait_for(lock, TIMEOUT, [this, count] { return messages.size() >= count; });
        return messages;
    }
};

} // namespace

TEST_F(WebsocketReceiveTest, FragmentedMessageIsDeliveredWhole) {
    connect(DEFAULT_MAX_INCOMING_MESSAGE_SIZE);

    ASSERT_TRUE(server.send_frame(OPCODE_TEXT, false, R"([2,"1",)"));
    ASSERT_TRUE(server.send_frame(OPCODE_CONTINUATION, false, R"("Heartbeat",)"));
    ASSERT_TRUE(server.send_frame(OPCODE_CONTINUATION, true, "{}]"));
    ASSERT_TRUE(server.send_frame(OPCODE_TEXT, true, R"([2,"2","Heartbeat",{}])"));

    EXPECT_THAT(wait_for_messages(2), testing::ElementsAre(R"([2,"1","Heartbeat",{}])", R"([2,"2","Heartbeat",{}])"));
}

TEST_F(WebsocketReceiveTest, MessageAtMaximumSizeIsDelivered) {
    connect(1024);

    const std::string message(1024, 'x');
    ASSERT_TRUE(server.send_frame(OPCODE_TEXT, true, message));

    EXPECT_THAT(wait_for_messages(1), testing::ElementsAre(message));
}

TEST_F(WebsocketReceiveTest, AnnouncedOversizedFrameClosesWithMessageTooBig) {
    connect(1024);

    // Announces a 1 TiB frame but sends only a few bytes of it: the frame header alone must trigger the close
    ASSERT_TRUE(server.send_frame_header(OPCODE_TEXT, true, std::uint64_t{1} << 40));
    ASSERT_TRUE(server.send_raw(std::string(16, 'x')));

    EXPECT_EQ(server.wait_for_close_status(), CLOSE_STATUS_MESSAGE_TOO_BIG);
    EXPECT_TRUE(server.accept_and_handshake()) << "the client did not reconnect";
    const std::lock_guard lock(messages_mutex);
    EXPECT_TRUE(messages.empty());
}

TEST_F(WebsocketReceiveTest, FragmentsTogetherExceedingMaximumSizeCloseWithMessageTooBig) {
    connect(1024);

    ASSERT_TRUE(server.send_frame(OPCODE_TEXT, false, std::string(600, 'x')));
    ASSERT_TRUE(server.send_frame(OPCODE_CONTINUATION, true, std::string(600, 'x')));

    EXPECT_EQ(server.wait_for_close_status(), CLOSE_STATUS_MESSAGE_TOO_BIG);
    const std::lock_guard lock(messages_mutex);
    EXPECT_TRUE(messages.empty());
}

} // namespace ocpp
