// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <cstdint>
#include <everest/slac/slac_defs.hpp>
#include <net/ethernet.h>

namespace everest::lib::slac::messages {

typedef struct {
    struct ether_header ethernet_header;
    struct {
        std::uint8_t mmv;     // management message version
        std::uint16_t mmtype; // management message type

    } __attribute__((packed)) homeplug_header;

    // the rest of this message is potentially payload data
    std::uint8_t payload[ETH_FRAME_LEN - ETH_HLEN - sizeof(homeplug_header)];
} __attribute__((packed)) homeplug_message;

typedef struct {
    std::uint8_t fmni; // fragmentation management number information
    std::uint8_t fmsn; // fragmentation message sequence number
} __attribute__((packed)) homeplug_fragmentation_part;

class HomeplugMessage {
public:
    using MacAddress = std::array<std::uint8_t, ETH_ALEN>;
    homeplug_message* get_raw_message_ptr();
    homeplug_message const* get_raw_message_ptr() const;

    void setup_payload(void const* payload, int len, std::uint16_t mmtype, const defs::MMV mmv);
    void set_destination(MacAddress const& mac);
    void set_source(MacAddress const& mac);

    int get_raw_msg_len() const;
    std::uint16_t get_mmtype() const;
    std::uint8_t const* get_src_mac() const;
    bool is_valid() const;

    template <typename T> const T& get_payload() const {
        return *reinterpret_cast<T const*>(get_raw_payload_ptr());
    }

private:
    std::uint8_t const* get_raw_payload_ptr() const;
    homeplug_message raw_msg;

    int raw_msg_len{-1};
};

} // namespace everest::lib::slac::messages
