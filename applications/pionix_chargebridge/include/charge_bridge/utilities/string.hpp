// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>
#include <set>
#include <string>

namespace charge_bridge::utilities {
bool string_starts_with(std::string_view const& str, std::string_view const& pattern);
bool string_ends_with(std::string const& str, std::string const& pattern);

std::string string_after_pattern(std::string_view const& str, std::string_view const& pattern);
std::string& replace_all_in_place(std::string& source, std::string const& placeholder, std::string const& substitute);
std::string replace_all(std::string const& source, std::string const& placeholder, std::string const& substitute);

std::set<std::string> csv_to_set(std::string const& str);

// The mDNS discovery sentinel ("ANY_EVSE", "ANY_EV" or "ANY") an `ip:` value starts with. A bare ANY
// counts only on its own or followed by an interface list, so a hostname like "ANYCB01.lan" is not one.
std::optional<std::string_view> discovery_sentinel(std::string_view const& endpoint);

// "[fd00::1]" -> "fd00::1"; anything else is returned unchanged (including a
// "%scope" suffix inside the brackets). Accepts the bracketed IPv6 spelling in
// config files without requiring it.
std::string strip_brackets(std::string const& host);

// Join host and port for display: IPv6 literals (host contains ':') are
// bracketed ("[fd00::1]:4444"), everything else is "host:port".
std::string format_host_port(std::string const& host, std::uint16_t port);

} // namespace charge_bridge::utilities
