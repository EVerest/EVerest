// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 - 2023 Pionix GmbH and Contributors to EVerest
#ifndef EVSE_SLAC_MISC_HPP
#define EVSE_SLAC_MISC_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <everest/slac/slac_types.hpp>

std::string format_nmk(everest::lib::slac::Nmk const& nmk);
std::string format_nmk(const uint8_t* nmk);

std::string format_mac_addr(everest::lib::slac::MacAddress const& mac);
std::string format_mac_addr(const uint8_t* mac);

std::optional<everest::lib::slac::MacAddress> parse_mac_addr(std::string_view text);
bool parse_mac_addr(const std::string& mac_str, uint8_t* mac, size_t length);

std::string format_run_id(everest::lib::slac::RunId const& run_id);
std::string format_run_id(const uint8_t* run_id);

std::string format_mmtype(const uint16_t mmtype);

#endif // EVSE_SLAC_MISC_HPP
