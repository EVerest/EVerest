// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef SUNSPEC_SOLAR_LIMIT_SUNSPEC_READER_HPP
#define SUNSPEC_SOLAR_LIMIT_SUNSPEC_READER_HPP

#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <vector>

namespace sunspec_solar {

// Reads `count` holding registers starting at `address`. Throws on any failure.
using ReadRegisters = std::function<std::vector<std::uint16_t>(std::uint16_t address, std::uint16_t count)>;

class DiscoveryError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Where the AC power output and its scale factor live on the device.
struct ProductionSource {
    std::uint16_t model_id;
    std::uint16_t w_address;
    std::uint16_t w_sf_address;
};

// Finds the SunSpec marker at base address 40000, 0 or 50000 and walks the
// model chain. Prefers Model 701 and falls back to Models 101, 102 or 103.
// Throws DiscoveryError if there is no marker or no supported model.
ProductionSource discover(const ReadRegisters& read);

// AC power output in watts, positive when the inverter produces. Returns
// std::nullopt when W or W_SF is "not implemented" or W_SF is implausible.
std::optional<double> read_production_W(const ReadRegisters& read, const ProductionSource& source);

} // namespace sunspec_solar

#endif // SUNSPEC_SOLAR_LIMIT_SUNSPEC_READER_HPP
