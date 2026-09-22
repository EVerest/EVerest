// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>

#include <iso15118/ev/d20/context.hpp>
#include <iso15118/sae_modes.hpp>

namespace iso15118::ev::d20 {

// The EnabledModes the EV echoes: what the SECC enabled, masked to what this EV supports.
inline std::uint32_t supported_enabled_modes(const Context& ctx,
                                             const message_20::datatypes::sae::DERControlCPDRes& control) {
    return sae::derive_enabled_modes(control) & ctx.sae_supported_modes();
}

inline std::uint32_t supported_enabled_modes(const Context& ctx,
                                             const message_20::datatypes::sae::DERControlCLRes& control) {
    return sae::derive_enabled_modes(control) & ctx.sae_supported_modes();
}

} // namespace iso15118::ev::d20
