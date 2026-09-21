// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <algorithm>
#include <cstring>
#include <optional>
#include <vector>

#include <arpa/inet.h>

#include <iso15118/io/connection_abstract.hpp>
#include <iso15118/io/sdp.hpp>
#include <iso15118/io/sdp_packet.hpp>

namespace iso15118::test {

// Test double for io::IConnection. Records close() and lets the test drive
// connection events and read results without any real socket or sleep.
class MockConnection final : public io::IConnection {
public:
    void set_event_callback(const io::ConnectionEventCallback& callback) override {
        event_callback = callback;
    }

    io::Ipv6EndPoint get_public_endpoint() const override {
        return {};
    }

    void write(const uint8_t* buf, size_t len) override {
        written.insert(written.end(), buf, buf + len);
    }

    io::ReadResult read(uint8_t* buf, size_t len) override {
        if (read_pos < read_buffer.size()) {
            const auto available = read_buffer.size() - read_pos;
            const auto count = std::min(len, available);
            std::memcpy(buf, read_buffer.data() + read_pos, count);
            read_pos += count;
            // A short read blocks, like both real connections do.
            return {count < len, count, false};
        }
        return next_read_result;
    }

    void close() override {
        closed = true;
        fire(io::ConnectionEvent::CLOSED);
    }

    bool is_secure() const override {
        return false;
    }

    std::optional<io::sha512_hash_t> get_vehicle_cert_hash() const override {
        return std::nullopt;
    }

    // Inject a connection event through the stored callback.
    void fire(io::ConnectionEvent event) {
        if (event_callback) {
            event_callback(event);
        }
    }

    // Queue a full V2GTP frame (8-byte header + payload) for read() to deliver.
    void queue_v2gtp_packet(io::v2gtp::PayloadType payload_type, const uint8_t* payload, std::size_t payload_len) {
        read_buffer.resize(io::SdpPacket::V2GTP_HEADER_SIZE + payload_len);
        read_buffer[0] = io::SDP_PROTOCOL_VERSION;
        read_buffer[1] = io::SDP_INVERSE_PROTOCOL_VERSION;

        const uint16_t type = htons(static_cast<uint16_t>(payload_type));
        std::memcpy(read_buffer.data() + 2, &type, sizeof(type));

        const uint32_t len = htonl(static_cast<uint32_t>(payload_len));
        std::memcpy(read_buffer.data() + 4, &len, sizeof(len));

        std::memcpy(read_buffer.data() + io::SdpPacket::V2GTP_HEADER_SIZE, payload, payload_len);
        read_pos = 0;
    }

    // Append arbitrary bytes, so a test can queue a hand-built header or a second frame.
    void queue_raw(const uint8_t* data, std::size_t len) {
        read_buffer.insert(read_buffer.end(), data, data + len);
    }

    bool all_queued_data_read() const {
        return read_pos == read_buffer.size();
    }

    io::ReadResult next_read_result{};
    bool closed{false};
    std::vector<uint8_t> written;

private:
    io::ConnectionEventCallback event_callback;
    std::vector<uint8_t> read_buffer;
    std::size_t read_pos{0};
};

} // namespace iso15118::test
