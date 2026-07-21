// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#ifndef AC_TEMPERATURE_DERATING_DERATING_CURVE_HPP
#define AC_TEMPERATURE_DERATING_DERATING_CURVE_HPP

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace ac_temperature_derating {

struct DeratingPoint {
    double temp_C{0.0};
    double max_current_A{0.0};
};

using DeratingCurve = std::vector<DeratingPoint>;
using DeratingCurveMap = std::map<std::string, DeratingCurve>;
using TemperatureProviderIgnoreList = std::set<std::string>;

/// Build curve key as module_id.identification.
std::string make_curve_key(const std::string& module_id, const std::string& identification);

/// True if curves contains at least one key with prefix module_id.
bool has_curve_for_provider(const DeratingCurveMap& curves, const std::string& module_id);

/// Parse derating curves from JSON. Keys must be module_id.identification, values are point arrays.
DeratingCurveMap parse_derating_curves_json(const std::string& json);

/// Throws if any connected temperature provider has no matching curve key prefix.
void validate_curves_for_providers(const DeratingCurveMap& curves, const std::vector<std::string>& module_ids);

/// Parse comma-separated module_id.identification entries (whitespace trimmed). Empty string yields an empty set.
TemperatureProviderIgnoreList parse_temperature_provider_ignore_list(const std::string& csv);

/// Throws if any derating curve key is listed in the ignore list.
void validate_ignore_list_vs_curves(const DeratingCurveMap& curves, const TemperatureProviderIgnoreList& ignore_list);

/// True when module_id.identification is in the ignore list.
bool is_temperature_reading_ignored(const TemperatureProviderIgnoreList& ignore_list, const std::string& module_id,
                                    const std::string& identification);

/// Linearly interpolate max current for a temperature. Clamps below/above the curve endpoints.
double interpolate_max_current_A(const DeratingCurve& curve, double temp_C);

/// Returns nullptr if no curve exists for curve_key.
const DeratingCurve* find_derating_curve(const DeratingCurveMap& curves, const std::string& curve_key);

struct SensorReadingInput {
    std::string module_id;
    std::optional<std::string> identification;
    std::optional<double> temperature_C;
};

struct ComputeLimitResult {
    std::optional<double> effective_limit_A;
    std::vector<std::string> missing_curve_keys;
    std::vector<std::string> missing_identification_curve_keys;
};

/// Computes the minimum limit across all readings. Missing/stale readings and missing curves use
/// fallback_max_current_A.
ComputeLimitResult compute_effective_limit_A(const DeratingCurveMap& curves,
                                             const std::vector<SensorReadingInput>& readings,
                                             double fallback_max_current_A);

} // namespace ac_temperature_derating

#endif // AC_TEMPERATURE_DERATING_DERATING_CURVE_HPP
