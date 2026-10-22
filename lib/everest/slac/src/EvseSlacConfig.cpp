// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <everest/slac/EvseSlacConfig.hpp>
#include <stdexcept>
#include <string_view>
#include <sys/random.h>

namespace everest::lib::slac::fsm::evse {

namespace {

bool fill_random(std::uint8_t* data, std::size_t len) {
    std::size_t filled = 0;
    while (filled < len) {
        auto const result = getrandom(data + filled, len - filled, 0);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        filled += static_cast<std::size_t>(result);
    }
    return true;
}

} // namespace

bool EvseSlacConfig::generate_nmk() {
    return generate_nmk(session_nmk);
}

bool EvseSlacConfig::generate_nmk(Nmk& target_nmk) {
    return generate_nmk(target_nmk.data());
}

bool EvseSlacConfig::generate_nmk(std::uint8_t* target_nmk) {
    if (target_nmk == nullptr) {
        throw std::invalid_argument("NMK target must not be null");
    }

    auto const fill = [this](std::uint8_t* data, std::size_t len) {
        return random_source ? random_source(data, len) : fill_random(data, len);
    };

    Nmk nmk{};
    if (nmk_generation_mode == NmkGenerationMode::full_byte_range) {
        if (not fill(nmk.data(), nmk.size())) {
            return false;
        }
        std::copy(nmk.begin(), nmk.end(), target_nmk);
        return true;
    }

    static constexpr std::string_view LEGACY_PRINTABLE_CHARACTERS = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    // 256 = 7 * 36 + 4, so mapping every byte with % 36 would make '0'-'3' more likely than the rest.
    // Bytes from 252 up are dropped instead; each character then has exactly 7 byte values mapping to it.
    static constexpr std::uint8_t UNBIASED_BYTE_LIMIT = 256 - (256 % LEGACY_PRINTABLE_CHARACTERS.size());

    std::size_t generated = 0;
    Nmk random_bytes{};
    while (generated < nmk.size()) {
        if (not fill(random_bytes.data(), random_bytes.size())) {
            return false;
        }
        for (auto const value : random_bytes) {
            if (value >= UNBIASED_BYTE_LIMIT) {
                continue;
            }
            nmk[generated++] =
                static_cast<std::uint8_t>(LEGACY_PRINTABLE_CHARACTERS[value % LEGACY_PRINTABLE_CHARACTERS.size()]);
            if (generated == nmk.size()) {
                break;
            }
        }
    }
    std::copy(nmk.begin(), nmk.end(), target_nmk);
    return true;
}

} // namespace everest::lib::slac::fsm::evse
