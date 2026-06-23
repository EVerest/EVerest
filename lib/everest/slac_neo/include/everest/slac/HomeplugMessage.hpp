// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <cstring>
#include <cstdint>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <everest/slac/slac_defs.hpp>
#include <net/ethernet.h>

namespace everest::lib::slac::messages {

typedef struct {
    struct ether_header ethernet_header;
    struct homeplug_header {
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

inline constexpr std::size_t HOMEPLUG_PAYLOAD_OFFSET = offsetof(homeplug_message, payload);
inline constexpr std::size_t HOMEPLUG_HEADER_SIZE = HOMEPLUG_PAYLOAD_OFFSET - ETH_HLEN;

class HomeplugMessage {
public:
    using MacAddress = std::array<std::uint8_t, ETH_ALEN>;
    homeplug_message* get_raw_message_ptr();
    homeplug_message const* get_raw_message_ptr() const;

    void setup_payload(void const* payload, std::size_t len, std::uint16_t mmtype, const defs::MMV mmv);
    void mark_received_length(std::size_t len);

    void set_destination(MacAddress const& mac);
    void set_source(MacAddress const& mac);

    std::size_t frame_size() const;
    std::uint16_t get_mmtype() const;
    std::uint8_t const* get_src_mac() const;
    bool is_valid() const;

    template <typename T> const T& get_payload() const {
        if (not has_payload<T>()) {
            throw std::runtime_error("Homeplug payload too short for requested type");
        }
        return *reinterpret_cast<T const*>(get_raw_payload_ptr());
    }

    template <typename T> bool has_payload() const {
        auto const payload_end = frame_size();
        auto const payload_offset = static_cast<std::size_t>(get_raw_payload_ptr() - reinterpret_cast<std::uint8_t const*>(&raw_msg));
        return payload_end >= static_cast<std::size_t>(defs::MME_MIN_LENGTH) &&
               payload_end >= payload_offset + sizeof(T);
    }

    template <typename T> std::optional<T> payload_as() const {
        if (not has_payload<T>()) {
            return std::nullopt;
        }
        T payload{};
        std::memcpy(&payload, get_raw_payload_ptr(), sizeof(T));
        return payload;
    }

private:
    std::uint8_t const* get_raw_payload_ptr() const;
    homeplug_message raw_msg{};
    std::size_t raw_msg_len{0};
};

} // namespace everest::lib::slac::messages
