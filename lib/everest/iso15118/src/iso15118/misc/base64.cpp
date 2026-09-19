// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/detail/base64.hpp>

#include <array>

namespace iso15118 {

namespace {
constexpr char BASE64_ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
} // namespace

std::string base64_encode(const std::vector<uint8_t>& data) {
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    size_t i = 0;
    for (; i + 2 < data.size(); i += 3) {
        const uint32_t triple = (static_cast<uint32_t>(data[i]) << 16) | (static_cast<uint32_t>(data[i + 1]) << 8) |
                                static_cast<uint32_t>(data[i + 2]);
        out.push_back(BASE64_ALPHABET[(triple >> 18) & 0x3f]);
        out.push_back(BASE64_ALPHABET[(triple >> 12) & 0x3f]);
        out.push_back(BASE64_ALPHABET[(triple >> 6) & 0x3f]);
        out.push_back(BASE64_ALPHABET[triple & 0x3f]);
    }
    const size_t remaining = data.size() - i;
    if (remaining == 1) {
        const uint32_t triple = static_cast<uint32_t>(data[i]) << 16;
        out.push_back(BASE64_ALPHABET[(triple >> 18) & 0x3f]);
        out.push_back(BASE64_ALPHABET[(triple >> 12) & 0x3f]);
        out.push_back('=');
        out.push_back('=');
    } else if (remaining == 2) {
        const uint32_t triple = (static_cast<uint32_t>(data[i]) << 16) | (static_cast<uint32_t>(data[i + 1]) << 8);
        out.push_back(BASE64_ALPHABET[(triple >> 18) & 0x3f]);
        out.push_back(BASE64_ALPHABET[(triple >> 12) & 0x3f]);
        out.push_back(BASE64_ALPHABET[(triple >> 6) & 0x3f]);
        out.push_back('=');
    }
    return out;
}

std::vector<uint8_t> base64_decode(const std::string& in) {
    std::array<int8_t, 256> lut{};
    lut.fill(-1);
    for (int i = 0; i < 64; ++i) {
        lut[static_cast<uint8_t>(BASE64_ALPHABET[i])] = static_cast<int8_t>(i);
    }

    std::vector<uint8_t> out;
    out.reserve((in.size() / 4) * 3);
    uint32_t buffer = 0;
    int bits = 0;
    for (const char c : in) {
        if (c == '=' or c == '\n' or c == '\r' or c == ' ') {
            continue;
        }
        const int8_t value = lut[static_cast<uint8_t>(c)];
        if (value < 0) {
            return {};
        }
        buffer = (buffer << 6) | static_cast<uint32_t>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((buffer >> bits) & 0xff));
        }
    }
    return out;
}

} // namespace iso15118
