// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <chrono>
#include <everest/slac/EvseSlacConfig.hpp>
#include <limits>
#include <random>
#include <stdexcept>
#include <string_view>

namespace everest::lib::slac::fsm::evse {

void EvseSlacConfig::generate_nmk() {
    generate_nmk(session_nmk);
}

void EvseSlacConfig::generate_nmk(Nmk& target_nmk) {
    generate_nmk(target_nmk.data());
}

void EvseSlacConfig::generate_nmk(std::uint8_t* target_nmk) {
    if (target_nmk == nullptr) {
        throw std::invalid_argument("NMK target must not be null");
    }

    static constexpr std::string_view kLegacyPrintableCharacters = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    // Called from Reset's on_entry on every key regeneration, inside a transition: nothing here
    // may throw (see msm_helpers.hpp). std::random_device can, when no entropy source opens; the
    // clock then seeds the key, which keeps the keys apart between runs.
    auto const seed = []() -> std::mt19937::result_type {
        try {
            return std::random_device{}();
        } catch (...) {
            return static_cast<std::mt19937::result_type>(std::chrono::steady_clock::now().time_since_epoch().count());
        }
    }();
    std::mt19937 generator(seed);

    std::size_t generated = 0;
    if (nmk_generation_mode == NmkGenerationMode::legacy_printable) {
        std::uniform_int_distribution<std::size_t> byte_distribution{0, kLegacyPrintableCharacters.size() - 1};
        while (generated < slac::defs::NMK_LEN) {
            target_nmk[generated++] =
                static_cast<std::uint8_t>(kLegacyPrintableCharacters[byte_distribution(generator)]);
        }
    } else {
        std::uniform_int_distribution<int> byte_distribution(0, std::numeric_limits<std::uint8_t>::max());
        while (generated < slac::defs::NMK_LEN) {
            target_nmk[generated++] = static_cast<std::uint8_t>(byte_distribution(generator));
        }
    }
}

} // namespace everest::lib::slac::fsm::evse
