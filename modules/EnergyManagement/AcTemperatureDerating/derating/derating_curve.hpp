// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace ac_temperature_derating {

struct DeratingPoint {
    double m_temp_c{0.0};
    double m_max_current_a{0.0};
};

using DeratingCurve = std::vector<DeratingPoint>;
/// Nested map: module_id -> (identification -> curve). The two levels mirror the temperature provider
/// (module_id) and the reading's Temperature.identification.
using DeratingCurveMap = std::map<std::string, std::map<std::string, DeratingCurve>>;
using TemperatureProviderIgnoreList = std::set<std::string>;

/// Build a display/ignore-list key as module_id.identification.
std::string make_curve_key(const std::string& module_id, const std::string& identification);

/// True if curves contains any curve for module_id.
bool has_curve_for_provider(const DeratingCurveMap& curves, const std::string& module_id);

/// Parse derating curves from JSON. Format is a nested object {module_id: {identification: point_array}}.
DeratingCurveMap parse_derating_curves_json(const std::string& json);

/// Throws if any connected temperature provider has no configured curve.
void validate_curves_for_providers(const DeratingCurveMap& curves, const std::vector<std::string>& module_ids);

/// Parse comma-separated module_id.identification entries (whitespace trimmed). Empty string yields an empty set.
TemperatureProviderIgnoreList parse_temperature_provider_ignore_list(const std::string& csv);

/// Throws if any derating curve key is listed in the ignore list.
void validate_ignore_list_vs_curves(const DeratingCurveMap& curves, const TemperatureProviderIgnoreList& ignore_list);

/// True when module_id.identification is in the ignore list.
bool is_temperature_reading_ignored(const TemperatureProviderIgnoreList& ignore_list, const std::string& module_id,
                                    const std::string& identification);

/// Linearly interpolate max current for a temperature. Returns std::nullopt when temp_C is below the
/// curve's first point (no derating applies there); clamps at/above the last point.
std::optional<double> interpolate_max_current_A(const DeratingCurve& curve, double temp_C);

/// Returns nullptr if no curve exists for module_id.identification.
const DeratingCurve* find_derating_curve(const DeratingCurveMap& curves, const std::string& module_id,
                                         const std::string& identification);

struct SensorReadingInput {
    std::string m_module_id;
    std::optional<std::string> m_identification;
    std::optional<double> m_temperature_C;
};

struct ComputeLimitResult {
    std::optional<double> m_effective_limit_A;
    std::vector<std::string> m_missing_curve_keys;
    std::vector<std::string> m_missing_identification_curve_keys;
};

/// Computes the minimum limit across all readings. Missing/stale readings and missing curves use
/// fallback_max_current_A.
ComputeLimitResult compute_effective_limit_A(const DeratingCurveMap& curves,
                                             const std::vector<SensorReadingInput>& readings,
                                             double fallback_max_current_A);

} // namespace ac_temperature_derating
