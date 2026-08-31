// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <string>

#include <everest/slac/protocol/types.hpp>

/// Wire value formatting, for logs and telemetry only.
namespace everest::slac {

/// "AA:BB:CC:DD:EE:FF", uppercase.
std::string format_mac_addr(MacAddress const& mac);
std::string format_mac_addr(std::uint8_t const* mac);

/// 32 uppercase hex characters, no separators.
std::string format_nmk(Nmk const& nmk);

} // namespace everest::slac
