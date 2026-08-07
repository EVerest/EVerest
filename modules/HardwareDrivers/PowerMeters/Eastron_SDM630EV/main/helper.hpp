// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#ifndef EASTRON_SDM630EV_HELPER_HPP
#define EASTRON_SDM630EV_HELPER_HPP

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <generated/interfaces/powermeter/Implementation.hpp>

#include "transport.hpp"

namespace modbus_utils {

inline void check_bounds_or_throw(const transport::DataVector& data, std::size_t offset, std::size_t needed_bytes,
                                  const char* what) {
    if (offset > data.size() || needed_bytes > (data.size() - offset)) {
        throw std::out_of_range(std::string(what) + ": offset/length out of range (offset=" + std::to_string(offset) +
                                ", needed=" + std::to_string(needed_bytes) + ", size=" + std::to_string(data.size()) +
                                ")");
    }
}

inline std::uint16_t to_uint16(const transport::DataVector& data, std::size_t offset) {
    check_bounds_or_throw(data, offset, 2, "to_uint16");
    return static_cast<std::uint16_t>(data[offset] << 8 | data[offset + 1]);
}

inline std::uint32_t to_uint32(const transport::DataVector& data, std::size_t offset) {
    check_bounds_or_throw(data, offset, 4, "to_uint32");
    return static_cast<std::uint32_t>(data[offset]) << 24 | static_cast<std::uint32_t>(data[offset + 1]) << 16 |
           static_cast<std::uint32_t>(data[offset + 2]) << 8 | static_cast<std::uint32_t>(data[offset + 3]);
}

inline float to_float32(const transport::DataVector& data, std::size_t offset) {
    const std::uint32_t raw = to_uint32(data, offset);
    float value{};
    std::memcpy(&value, &raw, sizeof(value));
    return value;
}

inline std::vector<std::uint16_t> uint32_to_registers(std::uint32_t value) {
    return {static_cast<std::uint16_t>(value >> 16), static_cast<std::uint16_t>(value & 0xFFFF)};
}

inline std::vector<std::uint16_t> float_to_registers(float value) {
    std::uint32_t raw{};
    std::memcpy(&raw, &value, sizeof(raw));
    return uint32_to_registers(raw);
}

inline std::string to_hex_string(const transport::DataVector& data, std::size_t offset, std::size_t length) {
    check_bounds_or_throw(data, offset, length, "to_hex_string");
    std::stringstream ss;
    for (std::size_t index = 0; index < length; ++index) {
        ss << std::uppercase << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(data[offset + index]);
    }
    return ss.str();
}

// Fixed-length CHAR field: always emits all words, zero padded. Partial
// writes would leave stale bytes from a previous session in the signed data.
inline std::vector<std::uint16_t> string_to_fixed_registers(const std::string& str, std::size_t word_count) {
    std::vector<std::uint16_t> data(word_count, 0);
    const std::size_t used = std::min(str.size(), word_count * 2);
    for (std::size_t i = 0; i < used; ++i) {
        if ((i % 2) == 0) {
            data[i / 2] = static_cast<std::uint16_t>(static_cast<std::uint8_t>(str[i]) << 8);
        } else {
            data[i / 2] |= static_cast<std::uint8_t>(str[i]);
        }
    }
    return data;
}

} // namespace modbus_utils

namespace bcd_utils {

inline std::uint8_t to_bcd(unsigned value) {
    if (value > 99) {
        throw std::invalid_argument("to_bcd: value out of range: " + std::to_string(value));
    }
    return static_cast<std::uint8_t>(((value / 10) << 4) | (value % 10));
}

// Device time format: s-min-hour-week-Date-Month-Year-20, packed BCD.
inline std::vector<std::uint16_t> time_to_registers(const std::tm& local_time) {
    const unsigned weekday = (local_time.tm_wday == 0) ? 7 : static_cast<unsigned>(local_time.tm_wday);
    return {
        static_cast<std::uint16_t>(to_bcd(static_cast<unsigned>(local_time.tm_sec)) << 8 |
                                   to_bcd(static_cast<unsigned>(local_time.tm_min))),
        static_cast<std::uint16_t>(to_bcd(static_cast<unsigned>(local_time.tm_hour)) << 8 | to_bcd(weekday)),
        static_cast<std::uint16_t>(to_bcd(static_cast<unsigned>(local_time.tm_mday)) << 8 |
                                   to_bcd(static_cast<unsigned>(local_time.tm_mon) + 1)),
        static_cast<std::uint16_t>(to_bcd(static_cast<unsigned>(local_time.tm_year) % 100) << 8 | to_bcd(20)),
    };
}

} // namespace bcd_utils

namespace ocmf {

// DER SubjectPublicKeyInfo prefix for secp256r1, incl. the uncompressed
// point marker 0x04 (Transparenzsoftware expects this exact header).
constexpr const char* PUBLIC_KEY_DER_PREFIX = "3059301306072A8648CE3D020106082A8648CE3D03010703420004";
constexpr const char* PUBLIC_KEY_RAW_PREFIX = "04";

// Device semantics are inverted: 0x0000 = assigned/true, 0x0001 = false.
inline std::uint16_t is_to_value(types::powermeter::OCMFUserIdentificationStatus status) {
    return (status == types::powermeter::OCMFUserIdentificationStatus::ASSIGNED) ? 0x0000 : 0x0001;
}

inline std::uint32_t flag_to_bit(types::powermeter::OCMFIdentificationFlags flag) {
    using Flags = types::powermeter::OCMFIdentificationFlags;
    switch (flag) {
    case Flags::RFID_NONE:
        return 1U << 0;
    case Flags::RFID_PLAIN:
        return 1U << 1;
    case Flags::RFID_RELATED:
        return 1U << 2;
    case Flags::RFID_PSK:
        return 1U << 3;
    case Flags::OCPP_NONE:
        return 1U << 4;
    case Flags::OCPP_RS:
        return 1U << 5;
    case Flags::OCPP_AUTH:
        return 1U << 6;
    case Flags::OCPP_RS_TLS:
        return 1U << 7;
    case Flags::OCPP_AUTH_TLS:
        return 1U << 8;
    case Flags::OCPP_CACHE:
        return 1U << 9;
    case Flags::OCPP_WHITELIST:
        return 1U << 10;
    case Flags::OCPP_CERTIFIED:
        return 1U << 11;
    case Flags::ISO15118_NONE:
        return 1U << 12;
    case Flags::ISO15118_PNC:
        return 1U << 13;
    case Flags::PLMN_NONE:
        return 1U << 14;
    case Flags::PLMN_RING:
        return 1U << 15;
    case Flags::PLMN_SMS:
        return 1U << 16;
    }
    return 0;
}

inline std::uint32_t flags_to_bitfield(const std::vector<types::powermeter::OCMFIdentificationFlags>& flags) {
    std::uint32_t bitfield = 0;
    for (const auto flag : flags) {
        bitfield |= flag_to_bit(flag);
    }
    return bitfield;
}

inline std::uint16_t type_to_value(types::powermeter::OCMFIdentificationType type) {
    using Type = types::powermeter::OCMFIdentificationType;
    switch (type) {
    case Type::NONE:
        return 0x00;
    case Type::DENIED:
        return 0x01;
    case Type::UNDEFINED:
        return 0x02;
    case Type::ISO14443:
        return 0x03;
    case Type::ISO15693:
        return 0x04;
    case Type::EMAID:
        return 0x05;
    case Type::EVCCID:
        return 0x06;
    case Type::EVCOID:
        return 0x07;
    case Type::ISO7812:
        return 0x08;
    case Type::CARD_TXN_NR:
        return 0x09;
    case Type::CENTRAL:
        return 0x0A;
    case Type::CENTRAL_1:
        return 0x0B;
    case Type::CENTRAL_2:
        return 0x0C;
    case Type::LOCAL:
        return 0x0D;
    case Type::LOCAL_1:
        return 0x0E;
    case Type::LOCAL_2:
        return 0x0F;
    case Type::PHONE_NUMBER:
        return 0x10;
    case Type::KEY_CODE:
        return 0x11;
    }
    return 0x00;
}

inline std::uint16_t charge_point_id_type_to_value(const std::string& type) {
    if (type == "CBIDC") {
        return 2;
    }
    return 1; // EVSEID
}

// A complete OCMF document is "OCMF|<payload>|<signature section>". If the
// device output lacks the signature section, append it from the raw
// signature registers.
inline std::string assemble_ocmf(const std::string& device_output, const std::string& signature_hex) {
    const std::size_t first_separator = device_output.find('|');
    if (first_separator != std::string::npos && device_output.find('|', first_separator + 1) != std::string::npos) {
        return device_output;
    }
    std::string document;
    if (device_output.rfind("OCMF|", 0) != 0) {
        document = "OCMF|";
    }
    document += device_output;
    document += R"(|{"SA":"ECDSA-secp256r1-SHA256","SD":")" + signature_hex + R"("})";
    return document;
}

inline std::string signature_status_to_string(std::uint16_t status) {
    switch (status) {
    case 0x00:
        return "not initialised";
    case 0x01:
        return "idle";
    case 0x02:
        return "signature in progress";
    case 0x03:
        return "signature OK";
    case 0x04:
        return "invalid date time (time not synchronized before charging start?)";
    case 0x05:
        return "invalid measurement";
    case 0x06:
        return "signature state error";
    case 0x07:
        return "keypair generation error";
    case 0x08:
        return "SHA failed";
    case 0x09:
        return "public key error";
    case 0x10:
        return "invalid message format";
    case 0x11:
        return "invalid message size";
    case 0x12:
        return "signature error";
    case 0x13:
        return "undefined error";
    default:
        return "unknown (" + std::to_string(status) + ")";
    }
}

} // namespace ocmf

#endif // EASTRON_SDM630EV_HELPER_HPP
