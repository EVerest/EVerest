// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest
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
    std::random_device rd;
    std::mt19937 generator(rd());

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
