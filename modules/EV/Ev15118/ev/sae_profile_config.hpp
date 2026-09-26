// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>
#include <string>

#include <iso15118/ev/sae_inverter_profile.hpp>

namespace module {

// Reads a JSON object keyed by SaeInverterProfile field names, unit suffixes kept.
// supported_modes is an array of iso15118::sae function names.
//
// An empty path returns the default profile with error empty. A file that cannot be
// opened, read or parsed, a non-object document, a repeated or unknown key, a wrong JSON
// type, an integer or number outside its field type, an unknown mode name or an illegal
// enum string returns std::nullopt with error naming the key and the problem. An absent
// key keeps its default.
//
// Only shape and types are checked; iso15118::ev::validate_config checks the values.
// Never throws, never logs.
std::optional<iso15118::ev::SaeInverterProfile> parse_sae_inverter_profile(const std::string& path, std::string& error);

} // namespace module
