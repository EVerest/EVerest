// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest

#include <everest/slac/slac_utils.hpp>
#include <everest/tls/openssl_util.hpp>

namespace everest::lib::slac::utils {

// note on byte order:
//   - sha256 takes the most significant byte first from the lowest
//     memory address
//   - for the generation of the aes-128, or NMK-HS, the first octet of
//     the sha256 output is taken as the zero octet for the NMK-HS
//   - for the generation of NID, the NMK is fed into sha256, so having
//     a const char* as input should be the proper byte ordering already
void generate_nmk_hs(std::uint8_t nmk_hs[slac::defs::NMK_LEN], const char* plain_password, int password_len) {
    // do pbkdf1 (use sha256 as hashing function, iterate 1000 times,
    // use salt)
    std::vector<std::uint8_t> input(plain_password, plain_password + password_len);
    input.insert(input.end(), slac::defs::NMK_HASH_ARR.begin(), slac::defs::NMK_HASH_ARR.end());
    openssl::sha_256_digest_t digest;
    openssl::sha_256(input.data(), input.size(), digest);
    for (int i = 0; i < 1000 - 1; ++i) {
        openssl::sha_256(digest.data(), openssl::sha_256_digest_size, digest);
    }

    memcpy(nmk_hs, digest.data(), slac::defs::NMK_LEN);
}

void generate_nid_from_nmk(std::uint8_t nid[slac::defs::NID_LEN], const std::uint8_t nmk[slac::defs::NMK_LEN]) {
    // msb of least significant octet of NMK should be the leftmost bit
    // of the input, which corresponds to the usual const char* order

    // do pbkdf1 (use sha256 as hashing function, iterate 5 times, no
    // salt)
    openssl::sha_256_digest_t digest;
    openssl::sha_256(nmk, slac::defs::NMK_LEN, digest);
    for (int i = 0; i < 5 - 1; ++i) {
        openssl::sha_256(digest.data(), openssl::sha_256_digest_size, digest);
    }

    // use leftmost 52 bits of the hash output
    // left most bit should be bit 7 of the nid
    memcpy(nid, digest.data(), slac::defs::NID_LEN - 1); // (bits 52 - 5)
    nid[slac::defs::NID_LEN - 1] =
        (slac::defs::NID_SECURITY_LEVEL_SIMPLE_CONNECT << slac::defs::NID_SECURITY_LEVEL_OFFSET) |
        ((static_cast<std::uint8_t>(digest.data()[6])) >> slac::defs::NID_MOST_SIGNIFANT_BYTE_SHIFT);
}

std::string device_info(messages::qualcomm::op_attr_cnf const& msg) {
    const auto get_string_view = [](auto const& raw) constexpr {
        static_assert(sizeof(uint8_t) == sizeof(char));
        return std::string_view(reinterpret_cast<char const*>(raw), sizeof(raw));
    };
    std::string result("Qualcomm PLC Device Attributes:");
    result += "\n  HW Platform: ";
    result += get_string_view(msg.hw_platform);
    result += "\n  SW Platform: ";
    result += get_string_view(msg.sw_platform);
    result += ("\n  Firmware: " + std::to_string(msg.version_major) + "." + std::to_string(msg.version_minor) + "." +
               std::to_string(msg.version_pib) + "." + std::to_string(msg.reserved) + "-" +
               std::to_string(msg.version_build));
    result += "\n  Build date: ";
    result += get_string_view(msg.build_date);

    result += "\n  ZC signal: ";

    // FIXME: no magic numbers
    const auto zc_signal = (msg.line_freq_zc >> 2) & 0x03;
    if (zc_signal == 0x01) {
        result += "Detected";
    } else if (zc_signal == 0x02) {
        result += "Missing";
    } else {
        result += ("Unknown (" + std::to_string(zc_signal) + ")");
    }

    result += "\n  Line frequency: ";

    const auto line_freq = (msg.line_freq_zc) & 0x03;
    if (line_freq == 0x01) {
        result += "50Hz";
    } else if (line_freq == 0x02) {
        result += "60Hz";
    } else {
        result += ("Unknown (" + std::to_string(line_freq) + ")");
    }

    return result;
}
std::string device_info(messages::lumissil::nscm_get_version_cnf const& msg) {
    return "Lumissil PLC Device Firmware version: " + std::to_string(msg.version_major) + "." +
           std::to_string(msg.version_minor) + "." + std::to_string(msg.version_patch) + "." +
           std::to_string(msg.version_build);
}

} // namespace everest::lib::slac::utils
