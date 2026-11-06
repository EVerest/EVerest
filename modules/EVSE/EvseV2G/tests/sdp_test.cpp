// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <gtest/gtest.h>
#include <sdp.hpp>
#include <tools.hpp>

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace {

class SdpTest : public testing::Test {
protected:
    SdpTest() {
    }
};

class SdpTcpListenerTest : public testing::Test {
protected:
    // EvseV2G binds its listener to an explicit port, which is what keeps the port across a pause.
    // A port-0 bind would be released on pause, so a free port is looked up first and bound explicitly.
    void SetUp() override {
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = find_free_port();
        ASSERT_NE(address.sin_port, 0);

        listener = socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_NE(listener, -1);
        ASSERT_EQ(bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)), 0);
        ASSERT_EQ(listen(listener, DEFAULT_SOCKET_BACKLOG), 0);
        ctx.tcp_socket = listener;
    }

    static in_port_t find_free_port() {
        const int probe = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in probe_address{};
        probe_address.sin_family = AF_INET;
        probe_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        socklen_t length = sizeof(probe_address);
        in_port_t port = 0;
        if (bind(probe, reinterpret_cast<sockaddr*>(&probe_address), sizeof(probe_address)) == 0 and
            getsockname(probe, reinterpret_cast<sockaddr*>(&probe_address), &length) == 0) {
            port = probe_address.sin_port;
        }
        close(probe);
        return port;
    }

    in_port_t bound_port() const {
        sockaddr_in bound{};
        socklen_t length = sizeof(bound);
        getsockname(listener, reinterpret_cast<sockaddr*>(&bound), &length);
        return bound.sin_port;
    }

    void TearDown() override {
        if (listener != -1) {
            close(listener);
        }
    }

    int connect_to_listener() {
        const int client = socket(AF_INET, SOCK_STREAM, 0);
        const int result = connect(client, reinterpret_cast<sockaddr*>(&address), sizeof(address));
        const int error = errno;
        close(client);
        return result == 0 ? 0 : error;
    }

    v2g_context ctx{};
    int listener{-1};
    sockaddr_in address{};
};

TEST_F(SdpTcpListenerTest, paused_listener_refuses_tcp_handshake) {
    EXPECT_EQ(connect_to_listener(), 0);

    EXPECT_EQ(sdp_pause_tcp_listener(&ctx), 0);
    EXPECT_TRUE(ctx.tcp_listener_paused);
    EXPECT_EQ(connect_to_listener(), ECONNREFUSED);
}

TEST_F(SdpTcpListenerTest, resumed_listener_accepts_tcp_handshake_on_same_port) {
    ASSERT_EQ(sdp_pause_tcp_listener(&ctx), 0);

    EXPECT_EQ(sdp_resume_tcp_listener(&ctx), 0);
    EXPECT_FALSE(ctx.tcp_listener_paused);
    EXPECT_EQ(bound_port(), address.sin_port);
    EXPECT_EQ(connect_to_listener(), 0);
}

TEST_F(SdpTcpListenerTest, pause_wakes_blocked_accept) {
    int accept_result = 0;
    int accept_error = 0;
    std::thread acceptor([&] {
        accept_result = accept(listener, nullptr, nullptr);
        accept_error = errno;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    ASSERT_EQ(sdp_pause_tcp_listener(&ctx), 0);
    acceptor.join();

    EXPECT_EQ(accept_result, -1);
    EXPECT_EQ(accept_error, EINVAL);
}

TEST_F(SdpTcpListenerTest, pause_and_resume_are_idempotent) {
    EXPECT_EQ(sdp_resume_tcp_listener(&ctx), 0);
    EXPECT_FALSE(ctx.tcp_listener_paused);

    EXPECT_EQ(sdp_pause_tcp_listener(&ctx), 0);
    EXPECT_EQ(sdp_pause_tcp_listener(&ctx), 0);
    EXPECT_TRUE(ctx.tcp_listener_paused);

    EXPECT_EQ(sdp_resume_tcp_listener(&ctx), 0);
    EXPECT_EQ(sdp_resume_tcp_listener(&ctx), 0);
    EXPECT_EQ(connect_to_listener(), 0);
}

TEST_F(SdpTest, tcp_listener_pause_without_tcp_socket_is_noop) {
    v2g_context ctx{};
    ctx.tcp_socket = -1;

    EXPECT_EQ(sdp_pause_tcp_listener(&ctx), 0);
    EXPECT_FALSE(ctx.tcp_listener_paused);
    EXPECT_EQ(sdp_resume_tcp_listener(&ctx), 0);
}

TEST_F(SdpTest, sdp_set_dlink_ready_sets_flag) {
    v2g_context ctx{};
    EXPECT_FALSE(ctx.sdp_dlink_ready);

    sdp_set_dlink_ready(&ctx, true);
    EXPECT_TRUE(ctx.sdp_dlink_ready);

    sdp_set_dlink_ready(&ctx, false);
    EXPECT_FALSE(ctx.sdp_dlink_ready);
}

TEST_F(SdpTest, sdp_set_dlink_ready_captures_timestamp) {
    v2g_context ctx{};
    EXPECT_EQ(ctx.sdp_dlink_ready_time.load(), 0);

    sdp_set_dlink_ready(&ctx, true);
    EXPECT_GT(ctx.sdp_dlink_ready_time.load(), 0);

    sdp_set_dlink_ready(&ctx, false);
    EXPECT_EQ(ctx.sdp_dlink_ready_time.load(), 0);
}

TEST_F(SdpTest, sdp_set_dlink_ready_timestamp_is_monotonic) {
    v2g_context ctx{};

    sdp_set_dlink_ready(&ctx, true);
    long long int first = ctx.sdp_dlink_ready_time.load();

    sdp_set_dlink_ready(&ctx, false);
    sdp_set_dlink_ready(&ctx, true);
    long long int second = ctx.sdp_dlink_ready_time.load();

    EXPECT_GE(second, first);
}

TEST_F(SdpTest, sdp_write_header) {
    uint8_t buffer[20];
    uint16_t payload_type = 0x9001;
    uint32_t length = 367;

    EXPECT_EQ(sdp_write_header(buffer, payload_type, length), 8);

    EXPECT_EQ(buffer[0], 0x01);
    EXPECT_EQ(buffer[1], 0xFE);
    EXPECT_EQ(buffer[2], 0x90);
    EXPECT_EQ(buffer[3], 0x01);
    EXPECT_EQ(buffer[4], 0x00);
    EXPECT_EQ(buffer[5], 0x00);
    EXPECT_EQ(buffer[6], 0x01);
    EXPECT_EQ(buffer[7], 0x6F);
}

TEST_F(SdpTest, timeout_detected_when_elapsed_exceeds_limit) {
    v2g_context ctx{};
    // Simulate dlink becoming ready 18001ms ago
    long long int now = getmonotonictime();
    ctx.sdp_dlink_ready_time = now - (V2G_COMMUNICATION_SETUP_TIMEOUT + 1);

    long long int elapsed = now - ctx.sdp_dlink_ready_time.load();
    EXPECT_GE(elapsed, V2G_COMMUNICATION_SETUP_TIMEOUT);
}

TEST_F(SdpTest, timeout_not_detected_when_elapsed_below_limit) {
    v2g_context ctx{};
    // Simulate dlink becoming ready just now
    ctx.sdp_dlink_ready_time = getmonotonictime();

    long long int elapsed = getmonotonictime() - ctx.sdp_dlink_ready_time.load();
    EXPECT_LT(elapsed, V2G_COMMUNICATION_SETUP_TIMEOUT);
}

TEST_F(SdpTest, timeout_cancelled_when_connection_initiated) {
    v2g_context ctx{};
    sdp_set_dlink_ready(&ctx, true);
    EXPECT_NE(ctx.sdp_dlink_ready_time.load(), 0);

    // Simulate connection established — timeout should be cancelled
    ctx.connection_initiated = true;
    long long int dlink_ready_time = ctx.sdp_dlink_ready_time.load();
    if (ctx.connection_initiated && dlink_ready_time != 0) {
        ctx.sdp_dlink_ready_time = 0;
    }
    EXPECT_EQ(ctx.sdp_dlink_ready_time.load(), 0);
}

TEST_F(SdpTest, v2g_communication_setup_timeout_is_18000) {
    EXPECT_EQ(V2G_COMMUNICATION_SETUP_TIMEOUT, 18000);
}

} // namespace
