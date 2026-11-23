// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef SUNSPEC_MAP_HPP
#define SUNSPEC_MAP_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sunspec {

inline constexpr std::uint16_t BASE_ADDRESS = 40000;

enum class AcType : std::uint16_t {
    SinglePhase = 0,
    SplitPhase = 1,
    ThreePhase = 2,
};

struct CommonInfo {
    std::string manufacturer;
    std::string model;
    std::string options;
    std::string version;
    std::string serial_number;
    std::uint16_t device_address{1};
};

// Largest magnitudes the device can report. They fix the scale factors once,
// so a client never combines a value with a scale factor from another update.
struct Ratings {
    double max_power_W{22000.0};
    double max_current_A{32.0};
    double max_voltage_V{480.0};
};

struct PerPhase {
    std::optional<double> l1;
    std::optional<double> l2;
    std::optional<double> l3;
};

struct TotalAndPerPhase {
    std::optional<double> total;
    std::optional<double> l1;
    std::optional<double> l2;
    std::optional<double> l3;
};

// Measurements in EVerest sign convention: power and energy import are flows
// from the grid into the vehicle.
struct Measurements {
    TotalAndPerPhase power_W;
    PerPhase current_A;
    PerPhase voltage_V;
    std::optional<double> frequency_Hz;
    TotalAndPerPhase energy_import_Wh;
    TotalAndPerPhase energy_export_Wh;
};

// Smallest power-of-ten exponent in [-3, 4] that represents max_abs_value in
// an int16 without exceeding 32767 counts.
std::int16_t choose_scale_factor(double max_abs_value);

// Register image of a SunSpec device with the Common model (1) and the DER AC
// Measurement model (701). Not thread-safe.
class RegisterMap {
public:
    RegisterMap(const CommonInfo& info, AcType ac_type, const Ratings& ratings);

    void update(const Measurements& measurements);

    // Throws std::out_of_range if any requested register lies outside the map.
    std::vector<std::uint16_t> read(std::uint16_t address, std::uint16_t count) const;

    std::uint16_t size() const;

private:
    std::vector<std::uint16_t> registers;
    std::int16_t w_sf;
    std::int16_t a_sf;
    std::int16_t v_sf;
};

} // namespace sunspec

#endif // SUNSPEC_MAP_HPP
