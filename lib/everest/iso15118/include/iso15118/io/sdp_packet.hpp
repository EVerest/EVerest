// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstddef>
#include <cstdint>

#include "sdp.hpp"

namespace iso15118::io {

// FIXME (aw): these shouldn't be necessary public - but nice to have
static constexpr uint8_t SDP_PROTOCOL_VERSION = 0x01;
static constexpr uint8_t SDP_INVERSE_PROTOCOL_VERSION = 0xFE;

// FIXME (aw): should be called V2GTP or SDP buffer
class SdpPacket {
public:
    static constexpr auto V2GTP_HEADER_SIZE = 8;
    enum class State {
        BUFFER_EMPTY,
        HEADER_READ,
        COMPLETE,

        // failed states
        INVALID_HEADER,
        PAYLOAD_TOO_LONG,
    };

    auto get_state() const {
        return state;
    }

    auto is_complete() const {
        return state == State::COMPLETE;
    }

    auto get_payload_length() const {
        return (state == State::COMPLETE) ? (length - V2GTP_HEADER_SIZE) : 0;
    }

    v2gtp::PayloadType get_payload_type() const;

    uint8_t const* get_payload_buffer() const {
        return buffer + V2GTP_HEADER_SIZE;
    }

    uint8_t const* get_buffer() const {
        return buffer;
    }

    uint8_t* get_current_buffer_pos() {
        return buffer + bytes_read;
    }

    size_t get_remaining_buffer_capacity() const {
        return sizeof(buffer) - bytes_read;
    }

    size_t get_remaining_bytes_to_read() const;

    void update_read_bytes(size_t len);

private:
    void parse_header();

    State state{State::BUFFER_EMPTY};
    // An ISO 15118-2 CertificateInstallationRes carries six certificates plus the encrypted
    // contract key, the DH public key, the eMAID and the XML signature, and does not fit in
    // 2048 bytes. 8192 is what EvseV2G serves the same protocol with (DEFAULT_BUFFER_SIZE),
    // and what the EV side already allows itself outbound (ev::MessageExchange, ev::d2::crypto).
    uint8_t buffer[8192];
    size_t bytes_read{0};
    size_t length; // length includes V2GTP_HEADER_SIZE
};

} // namespace iso15118::io
