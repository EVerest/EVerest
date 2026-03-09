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

int HomeplugMessage::get_raw_msg_len() const {
    return raw_msg_len;
}

void HomeplugMessage::setup_payload(void const* payload, int len, std::uint16_t mmtype, const defs::MMV mmv) {
    if (len > effective_payload_length(mmv)) {
        throw std::runtime_error("Homeplug Payload length too long");
    }
    raw_msg.homeplug_header.mmv = static_cast<std::underlying_type_t<defs::MMV>>(mmv);
    raw_msg.homeplug_header.mmtype = htole16(mmtype);

    std::uint8_t* dst = raw_msg.payload;

    if (mmv != defs::MMV::AV_1_0) {
        homeplug_fragmentation_part fragmentation_part{};
        fragmentation_part.fmni = 0; // not implemented
        fragmentation_part.fmsn = 0; // not implemented
        memcpy(dst, &fragmentation_part, sizeof(fragmentation_part));
        dst += sizeof(fragmentation_part); // adjust effective payload start
    }

    // copy payload into place
    memcpy(dst, payload, len);

    // get pointer to the end of buffer
    std::uint8_t* dst_end = dst + len;

    // calculate raw message length
    raw_msg_len = dst_end - reinterpret_cast<std::uint8_t*>(&raw_msg);

    // do padding
    auto padding_len = defs::MME_MIN_LENGTH - raw_msg_len;
    if (padding_len > 0) {
        memset(dst_end, 0x00, padding_len);
        raw_msg_len = defs::MME_MIN_LENGTH;
    }
}

void HomeplugMessage::set_destination(MacAddress const& mac) {
    raw_msg.ethernet_header.ether_type = htons(defs::ETH_P_HOMEPLUG_GREENPHY);
    std::memcpy(raw_msg.ethernet_header.ether_dhost, mac.data(), ETH_ALEN);
}

void HomeplugMessage::set_source(MacAddress const& mac) {
    std::memcpy(raw_msg.ethernet_header.ether_shost, mac.data(), ETH_ALEN);
}

std::uint16_t HomeplugMessage::get_mmtype() const {
    return le16toh(raw_msg.homeplug_header.mmtype);
}

std::uint8_t const* HomeplugMessage::get_src_mac() const {
    return raw_msg.ethernet_header.ether_shost;
}

bool HomeplugMessage::is_valid() const {
    return raw_msg_len >= defs::MME_MIN_LENGTH;
}

std::uint8_t const* HomeplugMessage::get_raw_payload_ptr() const {
    if (raw_msg.homeplug_header.mmv == static_cast<std::underlying_type_t<defs::MMV>>(defs::MMV::AV_1_0)) {
        return raw_msg.payload;
    }

    // if not av 1.0 message, we need to shift by the fragmentation part
    return raw_msg.payload + sizeof(homeplug_fragmentation_part);
}

} // namespace everest::lib::slac::messages
