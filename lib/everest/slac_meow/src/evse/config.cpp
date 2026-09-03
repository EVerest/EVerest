// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/slac/evse/config.hpp>

#include <limits>
#include <random>
#include <string_view>

namespace everest::slac::evse {

void Config::generate_nmk() {
    generate_nmk(session_nmk);
}

void Config::generate_nmk(Nmk& target_nmk) {
    static constexpr std::string_view LEGACY_PRINTABLE = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

    std::random_device rd;
    std::mt19937 generator(rd());

    if (nmk_generation_mode == NmkGenerationMode::legacy_printable) {
        std::uniform_int_distribution<std::size_t> pick{0, LEGACY_PRINTABLE.size() - 1};
        for (auto& octet : target_nmk) {
            octet = static_cast<std::uint8_t>(LEGACY_PRINTABLE[pick(generator)]);
        }
        return;
    }

    std::uniform_int_distribution<int> byte{0, std::numeric_limits<std::uint8_t>::max()};
    for (auto& octet : target_nmk) {
        octet = static_cast<std::uint8_t>(byte(generator));
    }
}

bool accepts_set_key_cnf_success_result(SetKeyCnfSuccessMode mode, std::uint8_t result) {
    switch (mode) {
    case SetKeyCnfSuccessMode::modem_compat_0x01:
        return result == defs::mme::set_key_cnf::RESULT_MODEM_COMPAT_SUCCESS;
    case SetKeyCnfSuccessMode::hpgp_standard_0x00:
        return result == defs::mme::set_key_cnf::RESULT_HPGP_SUCCESS;
    case SetKeyCnfSuccessMode::accept_0x00_or_0x01:
        return result == defs::mme::set_key_cnf::RESULT_HPGP_SUCCESS or
               result == defs::mme::set_key_cnf::RESULT_MODEM_COMPAT_SUCCESS;
    }
    return false;
}

} // namespace everest::slac::evse
