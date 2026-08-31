// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/slac/protocol/format.hpp>

namespace everest::slac {

namespace {
constexpr char HEX[] = "0123456789ABCDEF";

void append_hex(std::string& out, std::uint8_t octet) {
    out.push_back(HEX[(octet >> 4U) & 0x0FU]);
    out.push_back(HEX[octet & 0x0FU]);
}
} // namespace

std::string format_mac_addr(std::uint8_t const* mac) {
    std::string out;
    if (mac == nullptr) {
        return out;
    }
    out.reserve(17);
    for (std::size_t i = 0; i < 6; ++i) {
        if (i != 0) {
            out.push_back(':');
        }
        append_hex(out, mac[i]);
    }
    return out;
}

std::string format_mac_addr(MacAddress const& mac) {
    return format_mac_addr(mac.data());
}

std::string format_nmk(Nmk const& nmk) {
    std::string out;
    out.reserve(nmk.size() * 2U);
    for (auto const octet : nmk) {
        append_hex(out, octet);
    }
    return out;
}

} // namespace everest::slac
