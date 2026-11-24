// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <sunspec/sunspec_reader.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace sunspec_solar {

namespace {

constexpr std::array<std::uint16_t, 3> BASE_ADDRESSES{40000, 0, 50000};
constexpr std::uint16_t MARKER_HIGH = 0x5375;
constexpr std::uint16_t MARKER_LOW = 0x6E53;
constexpr std::uint16_t END_MODEL_ID = 0xFFFF;
constexpr std::uint16_t UNIMPLEMENTED = 0x8000;
constexpr int MAX_SCALE_FACTOR_MAGNITUDE = 10;

// Offsets of W and W_SF from the model start (the ID register), from the
// SunSpec model definitions.
struct ModelLayout {
    std::uint16_t w_offset;
    std::uint16_t w_sf_offset;
};

std::optional<ModelLayout> layout_of(std::uint16_t model_id) {
    switch (model_id) {
    case 701:
        return ModelLayout{10, 116};
    case 101:
    case 102:
    case 103:
        return ModelLayout{14, 15};
    default:
        return std::nullopt;
    }
}

bool has_marker(const ReadRegisters& read, std::uint16_t base) {
    try {
        const auto marker = read(base, 2);
        return marker.size() == 2 && marker[0] == MARKER_HIGH && marker[1] == MARKER_LOW;
    } catch (const std::exception&) {
        return false;
    }
}

std::optional<ProductionSource> walk_models(const ReadRegisters& read, std::uint16_t base) {
    std::optional<ProductionSource> inverter_model;
    std::uint32_t address = static_cast<std::uint32_t>(base) + 2;

    while (address + 1 <= 0xFFFF) {
        const auto header = read(static_cast<std::uint16_t>(address), 2);
        const auto model_id = header.at(0);
        const auto length = header.at(1);
        if (model_id == END_MODEL_ID) {
            break;
        }

        const auto layout = layout_of(model_id);
        // A model is usable only if it is long enough to hold W_SF.
        if (layout.has_value() && length + 2 > layout->w_sf_offset) {
            const ProductionSource source{model_id, static_cast<std::uint16_t>(address + layout->w_offset),
                                          static_cast<std::uint16_t>(address + layout->w_sf_offset)};
            if (model_id == 701) {
                return source;
            }
            if (!inverter_model.has_value()) {
                inverter_model = source;
            }
        }
        address += 2u + length;
    }
    return inverter_model;
}

} // namespace

ProductionSource discover(const ReadRegisters& read) {
    for (const auto base : BASE_ADDRESSES) {
        if (!has_marker(read, base)) {
            continue;
        }
        const auto source = walk_models(read, base);
        if (!source.has_value()) {
            throw DiscoveryError("SunSpec device has no Model 701, 101, 102 or 103");
        }
        return *source;
    }
    throw DiscoveryError("No SunSpec marker at base address 40000, 0 or 50000");
}

std::optional<double> read_production_W(const ReadRegisters& read, const ProductionSource& source) {
    const auto w_raw = read(source.w_address, 1).at(0);
    const auto sf_raw = read(source.w_sf_address, 1).at(0);
    if (w_raw == UNIMPLEMENTED || sf_raw == UNIMPLEMENTED) {
        return std::nullopt;
    }

    const auto scale_factor = static_cast<std::int16_t>(sf_raw);
    if (std::abs(scale_factor) > MAX_SCALE_FACTOR_MAGNITUDE) {
        return std::nullopt;
    }
    return static_cast<std::int16_t>(w_raw) * std::pow(10.0, scale_factor);
}

} // namespace sunspec_solar
