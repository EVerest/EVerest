// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace module::main {

// The CM_AMP_MAP amplitude map (ISO 15118-3 A.9.6): one 4-bit amplitude per OFDM carrier, packed two
// per byte with the even carrier in the low nibble. `len` is the number of carriers.
struct AmpMap {
    std::uint16_t len{0};
    std::vector<std::uint8_t> data{};
};

constexpr int AMP_MAP_DEFAULT_CARRIERS = 1155;
constexpr int AMP_MAP_MAX_CARRIERS = 4096;
constexpr int AMP_MAP_MAX_AMPLITUDE = 0x0F;

// All carriers at maximum amplitude: the map that means "no reduction".
AmpMap default_amp_map();

// Pack a map. `overrides` maps carrier index to amplitude; callers validate the ranges first.
AmpMap pack_amp_map(int carriers, int default_amplitude, std::map<int, int> const& overrides);

// Operator file schema:
//   carriers: <int>              # number of carriers, 1..4096 (default 1155)
//   default_amplitude: <0..15>   # amplitude for carriers without an override (default 15)
//   overrides: { <carrier>: <0..15>, ... }   # optional per-carrier amplitudes
// Strict: an unknown key, a value out of range, a non-numeric or out-of-range carrier index all
// reject the whole map. The map limits transmit power for EMC reasons, so nothing is clamped or
// silently dropped; the caller decides what to do without a map (it must not send full power while
// claiming the operator's file was applied). On rejection `error` says why.
std::optional<AmpMap> amp_map_from_json(nlohmann::json const& root, std::string& error);
std::optional<AmpMap> load_amp_map_file(std::string const& path, std::string& error);

} // namespace module::main
