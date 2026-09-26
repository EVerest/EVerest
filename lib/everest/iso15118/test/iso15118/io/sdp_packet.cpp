// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include <iso15118/io/sdp_packet.hpp>

using namespace iso15118;

namespace {

// A real ISO 15118-2 CertificateInstallationRes carries six certificates: the contract
// leaf with its two MO sub-CAs, and the CPS leaf with its two CPS sub-CAs, plus the
// encrypted private key, the DH public key, the eMAID and the XML signature. Measured
// off the wire against the OCPP plug-and-charge fixtures, the EXI payload is this long.
constexpr std::size_t CERTIFICATE_INSTALLATION_RES_PAYLOAD = 4143;

std::vector<uint8_t> frame_header(std::size_t payload_length) {
    std::vector<uint8_t> header(io::SdpPacket::V2GTP_HEADER_SIZE);
    header[0] = io::SDP_PROTOCOL_VERSION;
    header[1] = io::SDP_INVERSE_PROTOCOL_VERSION;
    header[2] = 0x80; // PayloadType::SAP, which ISO 15118-2 EXI messages also use
    header[3] = 0x01;
    const auto len = static_cast<uint32_t>(payload_length);
    header[4] = static_cast<uint8_t>(len >> 24);
    header[5] = static_cast<uint8_t>(len >> 16);
    header[6] = static_cast<uint8_t>(len >> 8);
    header[7] = static_cast<uint8_t>(len);
    return header;
}

// Feed bytes the way Session::on_bytes_received does, one read at a time.
void feed(io::SdpPacket& packet, const std::vector<uint8_t>& bytes) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto wanted = packet.get_remaining_bytes_to_read();
        if (wanted == 0) {
            return;
        }
        const auto chunk = std::min(wanted, bytes.size() - offset);
        std::memcpy(packet.get_current_buffer_pos(), bytes.data() + offset, chunk);
        packet.update_read_bytes(chunk);
        offset += chunk;
    }
}

} // namespace

SCENARIO("The V2GTP accumulator takes a frame the size of a certificate installation response") {

    GIVEN("an empty accumulator") {
        io::SdpPacket packet;
        REQUIRE(packet.get_state() == io::SdpPacket::State::BUFFER_EMPTY);

        WHEN("a whole CertificateInstallationRes-sized frame arrives in one read") {
            auto frame = frame_header(CERTIFICATE_INSTALLATION_RES_PAYLOAD);
            frame.resize(io::SdpPacket::V2GTP_HEADER_SIZE + CERTIFICATE_INSTALLATION_RES_PAYLOAD, 0xAB);
            feed(packet, frame);

            THEN("the frame completes rather than being rejected as too long") {
                REQUIRE(packet.get_state() == io::SdpPacket::State::COMPLETE);
                REQUIRE(packet.get_payload_length() == CERTIFICATE_INSTALLATION_RES_PAYLOAD);
            }
        }

        WHEN("the same frame is split across two reads") {
            auto frame = frame_header(CERTIFICATE_INSTALLATION_RES_PAYLOAD);
            frame.resize(io::SdpPacket::V2GTP_HEADER_SIZE + CERTIFICATE_INSTALLATION_RES_PAYLOAD, 0xAB);
            const auto head = frame.size() / 2;
            feed(packet, std::vector<uint8_t>(frame.begin(), frame.begin() + head));
            REQUIRE(packet.get_state() == io::SdpPacket::State::HEADER_READ);
            feed(packet, std::vector<uint8_t>(frame.begin() + head, frame.end()));

            THEN("it still completes with the whole payload") {
                REQUIRE(packet.get_state() == io::SdpPacket::State::COMPLETE);
                REQUIRE(packet.get_payload_length() == CERTIFICATE_INSTALLATION_RES_PAYLOAD);
            }
        }
    }

    GIVEN("an accumulator and a frame declaring more than it can hold") {
        io::SdpPacket packet;
        const auto capacity = packet.get_remaining_buffer_capacity();
        REQUIRE(capacity > io::SdpPacket::V2GTP_HEADER_SIZE + CERTIFICATE_INSTALLATION_RES_PAYLOAD);

        WHEN("the declared length overruns the buffer by one byte") {
            feed(packet, frame_header(capacity - io::SdpPacket::V2GTP_HEADER_SIZE + 1));

            THEN("the accumulator still refuses it, so the ceiling is raised and not removed") {
                REQUIRE(packet.get_state() == io::SdpPacket::State::PAYLOAD_TOO_LONG);
            }
        }
    }
}
