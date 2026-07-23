// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 - 2023 Pionix GmbH and Contributors to EVerest
#include "misc.hpp"

#include <algorithm>

namespace {

constexpr char hex_chars[] = "0123456789ABCDEF";

void append_hex_byte(std::string& out, uint8_t value) {
    out.push_back(hex_chars[(value >> 4U) & 0x0FU]);
    out.push_back(hex_chars[value & 0x0FU]);
}

std::optional<uint8_t> parse_hex_nibble(char digit) {
    if (digit >= '0' && digit <= '9') {
        return static_cast<uint8_t>(digit - '0');
    }
    if (digit >= 'a' && digit <= 'f') {
        return static_cast<uint8_t>(digit - 'a' + 10);
    }
    if (digit >= 'A' && digit <= 'F') {
        return static_cast<uint8_t>(digit - 'A' + 10);
    }
    return std::nullopt;
}

std::optional<everest::lib::slac::MacAddress> parse_mac_addr_impl(std::string_view mac_str) {
    if (mac_str.size() != 17) {
        return std::nullopt;
    }

    everest::lib::slac::MacAddress mac{};
    for (std::size_t i = 0; i < mac.size(); ++i) {
        auto const hi = parse_hex_nibble(mac_str[i * 3U]);
        auto const lo = parse_hex_nibble(mac_str[(i * 3U) + 1U]);
        if (!hi || !lo) {
            return std::nullopt;
        }

        if (i < mac.size() - 1U && mac_str[(i * 3U) + 2U] != ':') {
            return std::nullopt;
        }

        mac[i] = static_cast<uint8_t>((*hi << 4U) | *lo);
    }

    return mac;
}

} // namespace

std::string format_nmk(everest::lib::slac::Nmk const& nmk) {
    std::string out;
    out.reserve(nmk.size() * 3U);
    for (auto const octet : nmk) {
        append_hex_byte(out, octet);
        out.push_back(':');
    }
    return out;
}

std::string format_mac_addr(everest::lib::slac::MacAddress const& mac) {
    std::string out;
    out.reserve(18U);
    for (std::size_t i = 0; i < mac.size(); ++i) {
        append_hex_byte(out, mac[i]);
        if (i < mac.size() - 1U) {
            out.push_back(':');
        }
    }
    return out;
}

std::string format_run_id(everest::lib::slac::RunId const& run_id) {
    std::string out;
    out.reserve(run_id.size() * 2U);
    for (auto const octet : run_id) {
        append_hex_byte(out, octet);
    }
    return out;
}

std::string format_mmtype(const uint16_t mmtype) {
    std::string out;
    out.reserve(6U);
    out.push_back('0');
    out.push_back('x');
    append_hex_byte(out, static_cast<uint8_t>(mmtype >> 8U));
    append_hex_byte(out, static_cast<uint8_t>(mmtype));
    return out;
}

std::string format_nmk(const uint8_t* nmk) {
    everest::lib::slac::Nmk nmk_arr{};
    std::copy_n(nmk, nmk_arr.size(), nmk_arr.begin());
    return format_nmk(nmk_arr);
}

std::string format_mac_addr(const uint8_t* mac) {
    everest::lib::slac::MacAddress mac_arr{};
    std::copy_n(mac, mac_arr.size(), mac_arr.begin());
    return format_mac_addr(mac_arr);
}

std::string format_run_id(const uint8_t* run_id) {
    everest::lib::slac::RunId run_id_arr{};
    std::copy_n(run_id, run_id_arr.size(), run_id_arr.begin());
    return format_run_id(run_id_arr);
}

std::optional<everest::lib::slac::MacAddress> parse_mac_addr(std::string_view text) {
    return parse_mac_addr_impl(text);
}

bool parse_mac_addr(const std::string& mac_str, uint8_t* mac, size_t length) {
    if (mac == nullptr || length < everest::lib::slac::MacAddress{}.size()) {
        return false;
    }

    auto const parsed = parse_mac_addr(std::string_view{mac_str});
    if (!parsed) {
        return false;
    }

    std::copy_n(parsed->begin(), parsed->size(), mac);
    return true;
}
