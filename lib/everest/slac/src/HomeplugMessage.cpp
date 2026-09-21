// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest
#include <everest/slac/slac.hpp>

#include <arpa/inet.h>
#include <cstring>
#include <stdexcept>

namespace everest::lib::slac::messages {

namespace {
static constexpr auto effective_payload_length(const defs::MMV mmv) {
    if (mmv == defs::MMV::AV_1_0) {
        return sizeof(homeplug_message::payload);
    } else {
        return sizeof(homeplug_message::payload) - sizeof(homeplug_fragmentation_part);
    }
}
} // namespace

homeplug_message const* HomeplugMessage::get_raw_message_ptr() const {
    return &raw_msg;
};

homeplug_message* HomeplugMessage::get_raw_message_ptr() {
    return &raw_msg;
}

std::size_t HomeplugMessage::frame_size() const {
    return raw_msg_len;
}

void HomeplugMessage::mark_received_length(std::size_t len) {
    if (len > sizeof(raw_msg)) {
        raw_msg_len = sizeof(raw_msg);
        return;
    }
    raw_msg_len = len;
}

void HomeplugMessage::setup_payload(void const* payload, std::size_t len, std::uint16_t mmtype, const defs::MMV mmv) {
    if (payload == nullptr && len != 0) {
        throw std::runtime_error("Homeplug payload pointer is null");
    }
    if (len > effective_payload_length(mmv)) {
        throw std::runtime_error("Homeplug Payload length too long");
    }
    raw_msg.homeplug_header.mmv = static_cast<std::underlying_type_t<defs::MMV>>(mmv);
    const auto encoded_mmtype = htole16(mmtype);
    std::memcpy(&raw_msg.homeplug_header.mmtype, &encoded_mmtype, sizeof(encoded_mmtype));
    auto const payload_base = reinterpret_cast<std::uint8_t*>(&raw_msg.payload);
    std::memset(payload_base, 0x00, effective_payload_length(mmv));

    std::uint8_t* dst = raw_msg.payload;

    if (mmv != defs::MMV::AV_1_0) {
        homeplug_fragmentation_part fragmentation_part{};
        fragmentation_part.fmni = 0; // not implemented
        fragmentation_part.fmsn = 0; // not implemented
        memcpy(dst, &fragmentation_part, sizeof(fragmentation_part));
        dst += sizeof(fragmentation_part); // adjust effective payload start
    }

    // copy payload into place
    if (len != 0) {
        std::memcpy(dst, payload, len);
    }

    // get pointer to the end of buffer
    std::uint8_t* dst_end = dst + len;

    // calculate raw message length
    raw_msg_len = dst_end - reinterpret_cast<std::uint8_t*>(&raw_msg);

    // do padding
    if (raw_msg_len < static_cast<std::size_t>(defs::MME_MIN_LENGTH)) {
        auto const padding_len = static_cast<std::size_t>(defs::MME_MIN_LENGTH) - raw_msg_len;
        if (padding_len > 0) {
            std::memset(dst_end, 0x00, padding_len);
        }
        raw_msg_len = defs::MME_MIN_LENGTH;
    }
}

void HomeplugMessage::set_destination(MacAddress const& mac) {
    const auto encoded_ether_type = htons(defs::ETH_P_HOMEPLUG_GREENPHY);
    std::memcpy(&raw_msg.ethernet_header.ether_type, &encoded_ether_type, sizeof(encoded_ether_type));
    std::memcpy(raw_msg.ethernet_header.ether_dhost, mac.data(), ETH_ALEN);
}

void HomeplugMessage::set_source(MacAddress const& mac) {
    std::memcpy(raw_msg.ethernet_header.ether_shost, mac.data(), ETH_ALEN);
}

std::uint16_t HomeplugMessage::get_mmtype() const {
    if (frame_size() < HOMEPLUG_PAYLOAD_OFFSET) {
        return 0;
    }
    std::uint16_t encoded_mmtype{};
    std::memcpy(&encoded_mmtype, &raw_msg.homeplug_header.mmtype, sizeof(encoded_mmtype));
    return le16toh(encoded_mmtype);
}

std::uint8_t const* HomeplugMessage::get_src_mac() const {
    return raw_msg.ethernet_header.ether_shost;
}

bool HomeplugMessage::is_valid() const {
    return frame_size() >= HOMEPLUG_PAYLOAD_OFFSET && frame_size() >= defs::MME_MIN_LENGTH;
}

std::uint8_t const* HomeplugMessage::get_raw_payload_ptr() const {
    if (raw_msg.homeplug_header.mmv == static_cast<std::underlying_type_t<defs::MMV>>(defs::MMV::AV_1_0)) {
        return raw_msg.payload;
    }

    // if not av 1.0 message, we need to shift by the fragmentation part
    return raw_msg.payload + sizeof(homeplug_fragmentation_part);
}

} // namespace everest::lib::slac::messages
