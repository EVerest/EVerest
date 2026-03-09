// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest
#include <everest/slac/EvseSlacConfig.hpp>
#include <random>
#include <string>

namespace everest::lib::slac::fsm::evse {

void EvseSlacConfig::generate_nmk() {
    const std::string CHARACTERS = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

    std::random_device random_device;
    std::mt19937 generator(random_device());
    std::uniform_int_distribution<> distribution(0, CHARACTERS.size() - 1);

    for (std::size_t i = 0; i < slac::defs::NMK_LEN; ++i) {
        session_nmk[i] = (uint8_t)CHARACTERS[distribution(generator)];
    }
}

} // namespace everest::lib::slac::fsm::evse
