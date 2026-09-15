// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include "amp_map_file.hpp"

#include <exception>
#include <stdexcept>

#include <everest/utils/yaml_loader.hpp>
#include <fmt/core.h>

namespace module::main {

AmpMap default_amp_map() {
    return pack_amp_map(AMP_MAP_DEFAULT_CARRIERS, AMP_MAP_MAX_AMPLITUDE, {});
}

AmpMap pack_amp_map(int carriers, int default_amplitude, std::map<int, int> const& overrides) {
    AmpMap map;
    map.len = static_cast<std::uint16_t>(carriers);
    map.data.assign(static_cast<std::size_t>((carriers + 1) / 2), 0);
    for (int i = 0; i < carriers; ++i) {
        int amplitude = default_amplitude;
        if (auto const it = overrides.find(i); it != overrides.end()) {
            amplitude = it->second;
        }
        auto const nibble = static_cast<std::uint8_t>(amplitude & AMP_MAP_MAX_AMPLITUDE);
        if ((i % 2) == 0) {
            map.data[i / 2] |= nibble;
        } else {
            map.data[i / 2] |= static_cast<std::uint8_t>(nibble << 4);
        }
    }
    return map;
}

namespace {

bool read_int(nlohmann::json const& value, char const* name, int min, int max, int& out, std::string& error) {
    if (not value.is_number_integer()) {
        error = fmt::format("'{}' must be an integer", name);
        return false;
    }
    auto const v = value.get<long long>();
    if (v < min or v > max) {
        error = fmt::format("'{}' = {} is out of range {}..{}", name, v, min, max);
        return false;
    }
    out = static_cast<int>(v);
    return true;
}

} // namespace

std::optional<AmpMap> amp_map_from_json(nlohmann::json const& root, std::string& error) {
    if (not root.is_object()) {
        error = "top level must be a mapping";
        return std::nullopt;
    }
    int carriers = AMP_MAP_DEFAULT_CARRIERS;
    int default_amplitude = AMP_MAP_MAX_AMPLITUDE;
    std::map<int, int> overrides;
    for (auto const& [key, value] : root.items()) {
        if (key == "carriers") {
            if (not read_int(value, "carriers", 1, AMP_MAP_MAX_CARRIERS, carriers, error)) {
                return std::nullopt;
            }
        } else if (key == "default_amplitude") {
            if (not read_int(value, "default_amplitude", 0, AMP_MAP_MAX_AMPLITUDE, default_amplitude, error)) {
                return std::nullopt;
            }
        } else if (key == "overrides") {
            if (not value.is_object()) {
                error = "'overrides' must be a mapping of carrier index to amplitude";
                return std::nullopt;
            }
        } else {
            error = fmt::format("unknown key '{}'", key);
            return std::nullopt;
        }
    }
    if (root.contains("overrides")) {
        for (auto const& [key, value] : root.at("overrides").items()) {
            int carrier = 0;
            try {
                std::size_t consumed = 0;
                carrier = std::stoi(key, &consumed);
                if (consumed != key.size()) {
                    throw std::invalid_argument(key);
                }
            } catch (std::exception const&) {
                error = fmt::format("override carrier index '{}' is not an integer", key);
                return std::nullopt;
            }
            if (carrier < 0 or carrier >= carriers) {
                error = fmt::format("override carrier index {} is outside 0..{}", carrier, carriers - 1);
                return std::nullopt;
            }
            int amplitude = 0;
            if (not read_int(value, fmt::format("overrides[{}]", key).c_str(), 0, AMP_MAP_MAX_AMPLITUDE, amplitude,
                             error)) {
                return std::nullopt;
            }
            overrides[carrier] = amplitude;
        }
    }
    return pack_amp_map(carriers, default_amplitude, overrides);
}

std::optional<AmpMap> load_amp_map_file(std::string const& path, std::string& error) {
    nlohmann::json root;
    try {
        root = Everest::load_yaml(path);
    } catch (std::exception const& e) {
        error = fmt::format("cannot load '{}': {}", path, e.what());
        return std::nullopt;
    }
    return amp_map_from_json(root, error);
}

} // namespace module::main
