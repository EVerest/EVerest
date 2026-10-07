// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <utility>
#include <vector>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cbv2g/app_handshake/appHand_Encoder.h>
#include <cbv2g/exi_v2gtp.h>

#include "connection.hpp"
#include "v2g.hpp"

namespace {

// bounds a connection handler that keeps reading after the peer is gone
constexpr int max_reads = 100;

const char* const iso20_dc_namespace = "urn:iso:std:iso:15118:-20:DC";

struct FakeEv {
    std::vector<uint8_t> data;
    std::size_t pos{0};
    int reads{0};
    std::function<void()> before_first_read;
    std::function<void()> while_proxied;
};

struct ProxyCall {
    bool called{false};
    bool selected_iso20{false};
    std::vector<uint8_t> forwarded;
};

std::map<const v2g_connection*, FakeEv> fake_evs;
std::map<const v2g_connection*, ProxyCall> proxy_calls;

ssize_t fake_read(v2g_connection* conn, unsigned char* buf, std::size_t count, bool /*read_complete*/) {
    auto& ev = fake_evs[conn];
    if (ev.before_first_read) {
        std::exchange(ev.before_first_read, nullptr)();
    }
    if (++ev.reads > max_reads) {
        conn->ctx->is_connection_terminated = true;
        return -2;
    }
    const auto n = std::min(count, ev.data.size() - ev.pos);
    std::memcpy(buf, ev.data.data() + ev.pos, n);
    ev.pos += n;
    return static_cast<ssize_t>(n);
}

int fake_proxy(v2g_connection* conn, int proxy_fd) {
    auto& call = proxy_calls[conn];
    call.called = true;
    call.selected_iso20 = conn->ctx->selected_iso20;
    const auto forwarded_len = std::min<std::size_t>(conn->payload_len + V2GTP_HEADER_LENGTH, DEFAULT_BUFFER_SIZE);
    call.forwarded.assign(conn->buffer, conn->buffer + forwarded_len);
    if (auto& while_proxied = fake_evs[conn].while_proxied) {
        std::exchange(while_proxied, nullptr)();
    }
    close(proxy_fd);
    return 0;
}

std::vector<uint8_t> supported_app_protocol_req(const char* protocol_namespace) {
    appHand_exiDocument doc{};
    init_appHand_exiDocument(&doc);
    doc.supportedAppProtocolReq_isUsed = 1;
    auto& protocol = doc.supportedAppProtocolReq.AppProtocol.array[0];
    std::strncpy(protocol.ProtocolNamespace.characters, protocol_namespace,
                 sizeof(protocol.ProtocolNamespace.characters) - 1);
    protocol.ProtocolNamespace.charactersLen = std::strlen(protocol.ProtocolNamespace.characters);
    protocol.VersionNumberMajor = 2;
    protocol.VersionNumberMinor = 0;
    protocol.SchemaID = 1;
    protocol.Priority = 1;
    doc.supportedAppProtocolReq.AppProtocol.arrayLen = 1;

    std::vector<uint8_t> message(256);
    exi_bitstream_t stream;
    exi_bitstream_init(&stream, message.data(), message.size(), V2GTP_HEADER_LENGTH, nullptr);
    EXPECT_EQ(encode_appHand_exiDocument(&stream, &doc), 0);
    const auto payload_len = exi_bitstream_get_length(&stream);
    V2GTP_WriteHeader(message.data(), payload_len);
    message.resize(payload_len + V2GTP_HEADER_LENGTH);
    return message;
}

std::vector<uint8_t> v2gtp_header(uint32_t payload_len) {
    return {0x01,
            0xFE,
            0x80,
            0x01,
            static_cast<uint8_t>(payload_len >> 24),
            static_cast<uint8_t>(payload_len >> 16),
            static_cast<uint8_t>(payload_len >> 8),
            static_cast<uint8_t>(payload_len)};
}

class ConnectionHandleTest : public ::testing::Test {
protected:
    void SetUp() override {
        fake_evs.clear();
        proxy_calls.clear();

        // stands in for the ISO-2 and ISO-20 stacks, so proxy_connect() succeeds
        listen_fd = socket(AF_INET6, SOCK_STREAM, 0);
        sockaddr_in6 addr{};
        addr.sin6_family = AF_INET6;
        addr.sin6_addr = in6addr_loopback;
        socklen_t addr_len = sizeof(addr);
        if (listen_fd < 0 or bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 or
            listen(listen_fd, 4) != 0 or getsockname(listen_fd, reinterpret_cast<sockaddr*>(&addr), &addr_len) != 0) {
            GTEST_SKIP() << "IPv6 loopback not available";
        }

        ctx.proxy_port_iso2 = ntohs(addr.sin6_port);
        ctx.proxy_port_iso20 = ntohs(addr.sin6_port);
        ctx.iso20_proxy_enabled = true;
    }

    void TearDown() override {
        if (listen_fd >= 0) {
            close(listen_fd);
        }
    }

    v2g_connection& new_connection() {
        auto& conn = conns.emplace_back();
        conn.ctx = &ctx;
        conn.read = &fake_read;
        conn.proxy = &fake_proxy;
        return conn;
    }

    void handle(v2g_connection& conn, std::vector<uint8_t> ev_data) {
        fake_evs[&conn].data = std::move(ev_data);
        connection_handle(&conn);
    }

    void handle(std::vector<uint8_t> ev_data) {
        handle(new_connection(), std::move(ev_data));
    }

    const FakeEv& first_ev() {
        return fake_evs[&conns.front()];
    }

    const ProxyCall& first_proxy_call() {
        return proxy_calls[&conns.front()];
    }

    int listen_fd{-1};
    v2g_context ctx{};
    std::deque<v2g_connection> conns;
};

TEST_F(ConnectionHandleTest, din_handshake_is_forwarded_to_iso2_stack) {
    const auto request = supported_app_protocol_req(DIN_70121_MSG_DEF);
    handle(request);

    ASSERT_TRUE(first_proxy_call().called);
    EXPECT_FALSE(first_proxy_call().selected_iso20);
    EXPECT_EQ(first_proxy_call().forwarded, request);
}

TEST_F(ConnectionHandleTest, iso20_handshake_is_forwarded_to_iso20_stack) {
    const auto request = supported_app_protocol_req(iso20_dc_namespace);
    handle(request);

    ASSERT_TRUE(first_proxy_call().called);
    EXPECT_TRUE(first_proxy_call().selected_iso20);
    EXPECT_EQ(first_proxy_call().forwarded, request);
}

TEST_F(ConnectionHandleTest, payload_length_beyond_buffer_is_not_forwarded) {
    auto ev_data = v2gtp_header(0xFFFFFF00);
    ev_data.resize(ev_data.size() + 16, 0x00);
    handle(ev_data);

    EXPECT_FALSE(first_proxy_call().called);
}

TEST_F(ConnectionHandleTest, truncated_payload_is_not_forwarded) {
    auto ev_data = v2gtp_header(100);
    ev_data.resize(ev_data.size() + 10, 0x00);
    handle(ev_data);

    EXPECT_FALSE(first_proxy_call().called);
}

TEST_F(ConnectionHandleTest, handshake_with_wrong_payload_type_is_not_forwarded) {
    auto ev_data = supported_app_protocol_req(DIN_70121_MSG_DEF);
    ev_data[3] = V2GTP20_MAINSTREAM_PAYLOAD_ID & 0xFF;
    handle(ev_data);

    EXPECT_FALSE(first_proxy_call().called);
}

TEST_F(ConnectionHandleTest, peer_close_before_handshake_ends_connection) {
    handle({});

    EXPECT_FALSE(first_proxy_call().called);
    EXPECT_LT(first_ev().reads, max_reads);
}

TEST_F(ConnectionHandleTest, second_connection_during_session_is_rejected_and_keeps_routing) {
    auto& session = new_connection();
    auto& intruder = new_connection();
    bool selected_iso20_after_intruder{true};
    fake_evs[&session].while_proxied = [&] {
        handle(intruder, supported_app_protocol_req(iso20_dc_namespace));
        selected_iso20_after_intruder = ctx.selected_iso20;
    };

    handle(session, supported_app_protocol_req(DIN_70121_MSG_DEF));

    EXPECT_TRUE(proxy_calls[&session].called);
    EXPECT_FALSE(proxy_calls[&intruder].called);
    EXPECT_FALSE(selected_iso20_after_intruder);
}

TEST_F(ConnectionHandleTest, session_is_released_when_proxy_ends) {
    auto& first = new_connection();
    auto& second = new_connection();

    handle(first, supported_app_protocol_req(DIN_70121_MSG_DEF));
    handle(second, supported_app_protocol_req(iso20_dc_namespace));

    EXPECT_TRUE(proxy_calls[&first].called);
    ASSERT_TRUE(proxy_calls[&second].called);
    EXPECT_TRUE(proxy_calls[&second].selected_iso20);
}

TEST_F(ConnectionHandleTest, buffer_is_released_when_connection_ends) {
    auto& proxied = new_connection();
    auto& rejected = new_connection();

    handle(proxied, supported_app_protocol_req(DIN_70121_MSG_DEF));
    handle(rejected, v2gtp_header(0xFFFFFF00));

    ASSERT_TRUE(proxy_calls[&proxied].called);
    EXPECT_EQ(proxied.buffer, nullptr);
    EXPECT_EQ(rejected.buffer, nullptr);
}

TEST_F(ConnectionHandleTest, peer_without_handshake_does_not_block_ev) {
    auto& silent = new_connection();
    auto& ev = new_connection();
    fake_evs[&silent].before_first_read = [&] { handle(ev, supported_app_protocol_req(DIN_70121_MSG_DEF)); };

    handle(silent, {});

    EXPECT_FALSE(proxy_calls[&silent].called);
    EXPECT_TRUE(proxy_calls[&ev].called);
}

} // namespace
