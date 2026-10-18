// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <cerrno>
#include <cstddef>
#include <everest/slac/EvseSlacConfig.hpp>
#include <stdexcept>
#include <string_view>
#include <sys/random.h>
#include <system_error>

namespace everest::lib::slac::fsm::evse {

namespace {

void fill_random(std::uint8_t* data, std::size_t len) {
    std::size_t filled = 0;
    while (filled < len) {
        auto const result = getrandom(data + filled, len - filled, 0);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw std::system_error(errno, std::generic_category(), "getrandom failed to generate the NMK");
        }
        filled += static_cast<std::size_t>(result);
    }
}

} // namespace

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

    // No fallback to a predictable key: a failing fill_random throws. Inside the state machine the
    // throw leaves process_event (see msm_helpers.hpp) without a CM_SET_KEY.REQ being sent.
    if (nmk_generation_mode == NmkGenerationMode::full_byte_range) {
        fill_random(target_nmk, slac::defs::NMK_LEN);
        return;
    }

    static constexpr std::string_view kLegacyPrintableCharacters = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    // Rejection sampling keeps every character equally likely: 252 is the largest multiple of 36 below 256.
    static constexpr std::uint8_t kRejectFrom = 256 - (256 % kLegacyPrintableCharacters.size());

    std::size_t generated = 0;
    Nmk random_bytes{};
    while (generated < slac::defs::NMK_LEN) {
        fill_random(random_bytes.data(), random_bytes.size());
        for (auto const value : random_bytes) {
            if (value >= kRejectFrom) {
                continue;
            }
            target_nmk[generated++] =
                static_cast<std::uint8_t>(kLegacyPrintableCharacters[value % kLegacyPrintableCharacters.size()]);
            if (generated == slac::defs::NMK_LEN) {
                break;
            }
        }
    }
}

} // namespace everest::lib::slac::fsm::evse
