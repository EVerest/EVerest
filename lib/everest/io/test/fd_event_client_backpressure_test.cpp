// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
//
// pause_rx()/resume_rx(), tx_coalescing() and the tx drained action on a fake byte stream policy;
// the peer close case uses tcp_client on loopback.

// GCC -O3 false positive (-Wstringop-overflow) on std::vector<uint8_t> copies of the one-byte
// payloads below; the library itself builds with -Werror.
#if defined(__GNUC__) and not defined(__clang__)
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#endif

#include <everest/io/event/event_fd.hpp>
#include <everest/io/event/fd_event_client.hpp>
#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/tcp/tcp_client.hpp>
#include <everest/io/utilities/event_client_async_policy.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

using namespace std::chrono_literals;
using namespace everest::lib::io;

using everest::lib::io::event::fd_event_client;
using everest::lib::io::event::semaphore_fd;

namespace {

template <class Client, class Predicate>
bool pump_until(Client& client, std::chrono::milliseconds timeout, Predicate&& predicate) {
    auto const deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        client.sync(1ms);
        if (predicate()) {
            return true;
        }
    }
    return predicate();
}

template <class Client> void pump_for(Client& client, std::chrono::milliseconds timeout) {
    auto const deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        client.sync(1ms);
    }
}

// What the policy handed to the wire, and how often it was asked to.
struct wire {
    std::vector<std::uint8_t> bytes;
    std::size_t tx_calls{0};
    // Bytes one tx call writes. Anything short of the payload is a partial write.
    std::size_t bytes_per_tx{std::numeric_limits<std::size_t>::max()};
};

// Readable on demand: a semaphore fd stays readable for as many reads as pushes.
struct rx_source {
    semaphore_fd ready;
    std::atomic<int> pending{0};

    void push() {
        ++pending;
        ready.notify();
    }

    bool take() {
        if (pending.load() <= 0) {
            return false;
        }
        --pending;
        ready.read();
        return true;
    }
};

// A synchronous byte stream policy: opens at once, reads one byte per push, writes bytes_per_tx
// bytes per call and leaves the rest in the payload, the way tcp_socket and pty_handler do.
class byte_stream_policy {
public:
    using PayloadT = std::vector<std::uint8_t>;
    static constexpr bool supports_tx_coalescing{true};

    bool open(std::shared_ptr<wire> w, std::shared_ptr<rx_source> s) {
        m_wire = std::move(w);
        m_source = std::move(s);
        return static_cast<bool>(m_wire) and static_cast<bool>(m_source);
    }

    bool tx(PayloadT& payload) {
        ++m_wire->tx_calls;
        auto const n = std::min(m_wire->bytes_per_tx, payload.size());
        m_wire->bytes.insert(m_wire->bytes.end(), payload.begin(), payload.begin() + static_cast<std::ptrdiff_t>(n));
        payload.erase(payload.begin(), payload.begin() + static_cast<std::ptrdiff_t>(n));
        return payload.empty();
    }

    bool rx(PayloadT& data) {
        if (not m_source->take()) {
            return false;
        }
        data.assign(1, 0x2a);
        return true;
    }

    int get_fd() const {
        return m_source->ready.get_raw_fd();
    }

    int get_error() const {
        return 0;
    }

private:
    std::shared_ptr<wire> m_wire;
    std::shared_ptr<rx_source> m_source;
};

static_assert(not utilities::event_client_async_policy_v<byte_stream_policy>);
static_assert(utilities::policy_supports_tx_coalescing_v<byte_stream_policy>);

using byte_stream_client = fd_event_client<byte_stream_policy>::type;
using payload = byte_stream_policy::PayloadT;

struct bridge_end {
    std::shared_ptr<wire> w{std::make_shared<wire>()};
    std::shared_ptr<rx_source> source{std::make_shared<rx_source>()};
    byte_stream_client client{w, source};
    std::atomic<int> rx_count{0};

    bridge_end() {
        client.set_rx_handler([this](auto const&, auto&) { ++rx_count; });
        // The first sync registers the descriptor.
        client.sync(1ms);
    }
};

payload bytes(std::initializer_list<std::uint8_t> values) {
    return payload(values);
}

} // namespace

TEST(fd_event_client_coalescing_test, payloads_queued_back_to_back_are_written_as_one) {
    bridge_end end;
    EXPECT_TRUE(end.client.tx_coalescing(bytes({1, 2}), 8));
    EXPECT_TRUE(end.client.tx_coalescing(bytes({3, 4}), 8));
    EXPECT_EQ(end.client.tx_queue_depth(), 1U);

    ASSERT_TRUE(pump_until(end.client, 2s, [&] { return end.w->bytes.size() == 4; }));
    EXPECT_EQ(end.w->bytes, bytes({1, 2, 3, 4}));
    EXPECT_EQ(end.w->tx_calls, 1U);
    EXPECT_EQ(end.client.tx_queue_depth(), 0U);
}

TEST(fd_event_client_coalescing_test, the_size_bound_starts_a_new_payload) {
    bridge_end end;
    EXPECT_TRUE(end.client.tx_coalescing(bytes({1, 2}), 3));
    EXPECT_TRUE(end.client.tx_coalescing(bytes({3, 4}), 3));
    EXPECT_EQ(end.client.tx_queue_depth(), 2U);

    ASSERT_TRUE(pump_until(end.client, 2s, [&] { return end.w->bytes.size() == 4; }));
    EXPECT_EQ(end.w->bytes, bytes({1, 2, 3, 4}));
    EXPECT_EQ(end.w->tx_calls, 2U);
}

TEST(fd_event_client_coalescing_test, a_payload_beyond_the_bound_is_accepted_on_its_own) {
    bridge_end end;
    EXPECT_TRUE(end.client.tx_coalescing(bytes({1}), 2));
    EXPECT_TRUE(end.client.tx_coalescing(bytes({2, 3, 4, 5}), 2));
    EXPECT_EQ(end.client.tx_queue_depth(), 2U);

    ASSERT_TRUE(pump_until(end.client, 2s, [&] { return end.w->bytes.size() == 5; }));
    EXPECT_EQ(end.w->bytes, bytes({1, 2, 3, 4, 5}));
}

TEST(fd_event_client_coalescing_test, appending_to_a_payload_mid_partial_write_keeps_the_byte_order) {
    bridge_end end;
    end.w->bytes_per_tx = 2;
    EXPECT_TRUE(end.client.tx(bytes({1, 2, 3, 4})));
    // One tx call went out and left {3, 4} at the front of the buffer.
    ASSERT_TRUE(pump_until(end.client, 2s, [&] { return end.w->bytes.size() == 2; }));
    ASSERT_EQ(end.client.tx_queue_depth(), 1U);

    EXPECT_TRUE(end.client.tx_coalescing(bytes({5, 6}), 16));
    EXPECT_EQ(end.client.tx_queue_depth(), 1U);

    ASSERT_TRUE(pump_until(end.client, 2s, [&] { return end.w->bytes.size() == 6; }));
    EXPECT_EQ(end.w->bytes, bytes({1, 2, 3, 4, 5, 6}));
    EXPECT_EQ(end.client.tx_queue_depth(), 0U);
}

TEST(fd_event_client_coalescing_test, a_merge_is_accepted_once_the_payload_bound_is_reached) {
    bridge_end end;
    for (std::size_t i = 0; i < byte_stream_client::max_buffered_tx_payloads; ++i) {
        ASSERT_TRUE(end.client.tx(bytes({0})));
    }
    EXPECT_FALSE(end.client.tx(bytes({0})));
    EXPECT_TRUE(end.client.tx_coalescing(bytes({1}), 4));
    EXPECT_EQ(end.client.tx_queue_depth(), byte_stream_client::max_buffered_tx_payloads);
    EXPECT_FALSE(end.client.tx_coalescing(bytes({2, 3, 4, 5}), 4));
}

TEST(fd_event_client_tx_drain_test, the_drained_action_fires_once_when_the_last_payload_left) {
    bridge_end end;
    std::atomic<int> drained{0};
    end.client.set_tx_drained_action([&] { ++drained; });
    EXPECT_TRUE(end.client.tx(bytes({1})));
    EXPECT_TRUE(end.client.tx(bytes({2})));

    ASSERT_TRUE(pump_until(end.client, 2s, [&] { return drained.load() == 1; }));
    EXPECT_EQ(end.w->bytes, bytes({1, 2}));
    pump_for(end.client, 30ms);
    EXPECT_EQ(drained.load(), 1);
}

TEST(fd_event_client_tx_drain_test, a_reset_drops_the_buffer_without_draining) {
    bridge_end end;
    std::atomic<int> drained{0};
    end.client.set_tx_drained_action([&] { ++drained; });
    end.client.sync(1ms);

    EXPECT_TRUE(end.client.tx(bytes({1})));
    end.client.reset();
    pump_for(end.client, 50ms);
    EXPECT_EQ(drained.load(), 0);
    EXPECT_TRUE(end.w->bytes.empty());
    EXPECT_EQ(end.client.tx_queue_depth(), 0U);

    // The reopened connection drains as before.
    EXPECT_TRUE(end.client.tx(bytes({2})));
    ASSERT_TRUE(pump_until(end.client, 2s, [&] { return drained.load() == 1; }));
    EXPECT_EQ(end.w->bytes, bytes({2}));
}

TEST(fd_event_client_tx_drain_test, a_reset_from_the_drained_action_does_not_report_the_retired_connection) {
    bridge_end end;
    std::atomic<int> up_edges{0};
    std::atomic<int> drained{0};
    end.client.set_error_handler([&](int code, auto const&) {
        if (code == 0) {
            ++up_edges;
        }
    });
    // The first drain resets, the second only counts.
    end.client.set_tx_drained_action([&] {
        if (drained++ == 0) {
            end.client.reset();
        }
    });
    // Let the first connection's code 0 pass.
    pump_for(end.client, 50ms);
    up_edges = 0;

    EXPECT_TRUE(end.client.tx(bytes({1})));
    ASSERT_TRUE(pump_until(end.client, 2s, [&] { return drained.load() == 1; }));
    pump_for(end.client, 50ms);
    // Exactly one up edge: the reopened connection. The write that drained the retired one must not
    // report it.
    EXPECT_EQ(up_edges.load(), 1);

    // Already up: no further edge.
    EXPECT_TRUE(end.client.tx(bytes({2})));
    ASSERT_TRUE(pump_until(end.client, 2s, [&] { return drained.load() == 2; }));
    EXPECT_EQ(end.w->bytes, bytes({1, 2}));
    EXPECT_EQ(up_edges.load(), 1);
}

TEST(fd_event_client_rx_pause_test, pause_is_rejected_before_the_connection_is_monitored) {
    std::shared_ptr<wire> w = std::make_shared<wire>();
    std::shared_ptr<rx_source> source = std::make_shared<rx_source>();
    byte_stream_client client{w, source};
    // No monitored connection yet.
    EXPECT_FALSE(client.pause_rx());
    EXPECT_FALSE(client.resume_rx());
    EXPECT_FALSE(client.rx_paused());
    client.sync(1ms);
    EXPECT_TRUE(client.pause_rx());
    EXPECT_TRUE(client.rx_paused());
    EXPECT_TRUE(client.resume_rx());
    EXPECT_FALSE(client.rx_paused());
}

TEST(fd_event_client_rx_pause_test, paused_reads_hold_the_data_until_resume) {
    bridge_end end;
    ASSERT_TRUE(end.client.pause_rx());
    end.source->push();
    pump_for(end.client, 50ms);
    EXPECT_EQ(end.rx_count.load(), 0);

    ASSERT_TRUE(end.client.resume_rx());
    EXPECT_TRUE(pump_until(end.client, 2s, [&] { return end.rx_count.load() == 1; }));
}

TEST(fd_event_client_rx_pause_test, pause_and_resume_are_idempotent) {
    bridge_end end;
    EXPECT_TRUE(end.client.pause_rx());
    EXPECT_TRUE(end.client.pause_rx());
    end.source->push();
    pump_for(end.client, 30ms);
    EXPECT_EQ(end.rx_count.load(), 0);

    EXPECT_TRUE(end.client.resume_rx());
    EXPECT_TRUE(end.client.resume_rx());
    EXPECT_TRUE(pump_until(end.client, 2s, [&] { return end.rx_count.load() == 1; }));
}

TEST(fd_event_client_rx_pause_test, a_reset_while_paused_comes_back_unpaused) {
    bridge_end end;
    ASSERT_TRUE(end.client.pause_rx());
    end.client.reset();
    pump_for(end.client, 30ms);

    EXPECT_FALSE(end.client.rx_paused());
    end.source->push();
    EXPECT_TRUE(pump_until(end.client, 2s, [&] { return end.rx_count.load() == 1; }));
}

TEST(fd_event_client_rx_pause_test, the_pause_state_does_not_outlive_the_connection) {
    bridge_end end;
    ASSERT_TRUE(end.client.pause_rx());
    ASSERT_TRUE(end.client.rx_paused());

    // reset() runs on the next pass, the reopened descriptor is registered one pass later: in
    // between there is no monitored connection and the old pause is gone.
    end.client.reset();
    end.client.sync(1ms);
    EXPECT_FALSE(end.client.rx_paused());
    EXPECT_FALSE(end.client.pause_rx());
    EXPECT_FALSE(end.client.resume_rx());

    // Registered again.
    pump_for(end.client, 30ms);
    EXPECT_FALSE(end.client.rx_paused());
    ASSERT_TRUE(end.client.pause_rx());
    EXPECT_TRUE(end.client.rx_paused());
    end.source->push();
    pump_for(end.client, 30ms);
    EXPECT_EQ(end.rx_count.load(), 0);
    ASSERT_TRUE(end.client.resume_rx());
    EXPECT_TRUE(pump_until(end.client, 2s, [&] { return end.rx_count.load() == 1; }));
}

TEST(fd_event_client_rx_pause_test, writes_go_out_while_reads_are_paused) {
    bridge_end end;
    ASSERT_TRUE(end.client.pause_rx());
    EXPECT_TRUE(end.client.tx(bytes({7})));
    ASSERT_TRUE(pump_until(end.client, 2s, [&] { return end.w->bytes.size() == 1; }));
    EXPECT_EQ(end.w->bytes, bytes({7}));
}

TEST(fd_event_client_rx_pause_test, a_peer_close_while_paused_is_reported) {
    int srv = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(srv, 0);
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sa.sin_port = 0;
    ASSERT_EQ(::bind(srv, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)), 0);
    ASSERT_EQ(::listen(srv, 1), 0);
    socklen_t len = sizeof(sa);
    ASSERT_EQ(::getsockname(srv, reinterpret_cast<sockaddr*>(&sa), &len), 0);
    auto const port = ntohs(sa.sin_port);

    tcp::tcp_client client("127.0.0.1", port, 1000);
    std::atomic<bool> ready{false};
    std::atomic<int> failure{0};
    client.set_on_ready_action([&]() { ready = true; });
    client.set_error_handler([&](int code, auto const&) {
        if (code != 0 and failure.load() == 0) {
            failure = code;
        }
    });
    ASSERT_TRUE(pump_until(client, 5s, [&] { return ready.load(); }));
    // Handshake completed into the backlog; does not block.
    int const peer = ::accept(srv, nullptr, nullptr);
    ASSERT_GE(peer, 0);

    // Paused: the FIN below must still be reported.
    ASSERT_TRUE(client.pause_rx());
    ::close(peer);

    EXPECT_TRUE(pump_until(client, 2s, [&] { return failure.load() != 0; }))
        << "a peer close while paused was not reported";

    ::close(srv);
}
