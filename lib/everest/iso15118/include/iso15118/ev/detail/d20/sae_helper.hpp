// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <iso15118/ev/d20/context.hpp>
#include <iso15118/sae_modes.hpp>

namespace iso15118::ev::d20 {

// The EnabledModes the EV echoes: what the SECC enabled, masked to what this EV supports.
inline std::uint32_t supported_enabled_modes(const Context& ctx,
                                             const message_20::datatypes::sae::DERControlCPDRes& control) {
    return sae::derive_enabled_modes(control) & ctx.sae_supported_modes();
}

// A charge loop response replaces the bits of the leaves it carries; the rest of `current` is kept.
inline std::uint32_t merge_supported_enabled_modes(const Context& ctx, std::uint32_t current,
                                                   const message_20::datatypes::sae::DERControlCLRes& control) {
    return sae::update_enabled_modes(current, control) & ctx.sae_supported_modes();
}

// The problems entry for `enabled` functions this EV does not support, none when it supports them all. The
// EV masks them out of its echo, which deviates from the SECC's control without making it invalid.
// PermitService sets the EnterService bit without being an Enable, so it is never named.
inline std::optional<std::string> unsupported_enable_problem(const Context& ctx, std::uint32_t enabled) {
    const auto unsupported =
        enabled & ~ctx.sae_supported_modes() & ~sae::sae_function_bit(sae::DerBitMapFunctions::EnterService);
    if (unsupported == 0) {
        return std::nullopt;
    }
    return "SECC enabled DER functions the EV does not support: " + sae::sae_function_names(unsupported) +
           "; the EV does not echo them";
}

} // namespace iso15118::ev::d20
