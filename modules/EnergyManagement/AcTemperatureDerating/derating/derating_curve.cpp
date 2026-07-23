// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "derating_curve.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>

namespace ac_temperature_derating {

namespace {

DeratingCurve parse_curve_points(const nlohmann::json& points_json) {
    if (!points_json.is_array()) {
        throw std::invalid_argument("derating curve must be a JSON array");
    }
    if (points_json.empty()) {
        throw std::invalid_argument("derating curve must contain at least one point");
    }

    DeratingCurve curve;
    curve.reserve(points_json.size());
    for (const auto& point_json : points_json) {
        if (!point_json.is_object() || !point_json.contains("temp_C") || !point_json.contains("max_current_A")) {
            throw std::invalid_argument("derating curve point must contain temp_C and max_current_A");
        }
        DeratingPoint point;
        point.m_temp_c = point_json.at("temp_C").get<double>();
        point.m_max_current_a = point_json.at("max_current_A").get<double>();
        curve.push_back(point);
    }

    // Sort by ascending temp_C so interpolation can assume ordering; this also makes any duplicate
    // temperatures adjacent, so a single adjacent_find pass detects them.
    std::sort(curve.begin(), curve.end(),
              [](const DeratingPoint& lhs, const DeratingPoint& rhs) { return lhs.m_temp_c < rhs.m_temp_c; });

    const auto duplicate =
        std::adjacent_find(curve.begin(), curve.end(), [](const DeratingPoint& lhs, const DeratingPoint& rhs) {
            return lhs.m_temp_c == rhs.m_temp_c;
        });
    if (duplicate != curve.end()) {
        throw std::invalid_argument("derating curve contains duplicate temp_C values");
    }

    return curve;
}

void validate_curve_key_format(const std::string& key) {
    const auto dot = key.find('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= key.size()) {
        throw std::invalid_argument("derating curve key must be module_id.identification, got: " + key);
    }
}

void trim_inplace(std::string& value) {
    const auto not_space = [](unsigned char character) { return !std::isspace(character); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
}

} // namespace

std::string make_curve_key(const std::string& module_id, const std::string& identification) {
    return module_id + "." + identification;
}

bool has_curve_for_provider(const DeratingCurveMap& curves, const std::string& module_id) {
    return curves.count(module_id) != 0;
}

DeratingCurveMap parse_derating_curves_json(const std::string& json) {
    const auto parsed = nlohmann::json::parse(json);
    if (!parsed.is_object()) {
        throw std::invalid_argument("derating_curves_json must be a JSON object");
    }

    DeratingCurveMap curves;
    for (const auto& [module_id, identifications] : parsed.items()) {
        if (module_id.empty()) {
            throw std::invalid_argument("derating_curves_json contains an empty module_id");
        }
        if (!identifications.is_object()) {
            throw std::invalid_argument("derating_curves_json entry for module_id '" + module_id +
                                        "' must be an object of identification -> point array");
        }
        if (identifications.empty()) {
            throw std::invalid_argument("derating_curves_json entry for module_id '" + module_id +
                                        "' must define at least one identification");
        }
        for (const auto& [identification, points] : identifications.items()) {
            if (identification.empty()) {
                throw std::invalid_argument("derating_curves_json contains an empty identification for module_id '" +
                                            module_id + "'");
            }
            curves[module_id].emplace(identification, parse_curve_points(points));
        }
    }
    return curves;
}

void validate_curves_for_providers(const DeratingCurveMap& curves, const std::vector<std::string>& module_ids) {
    if (curves.empty()) {
        throw std::invalid_argument("derating_curves_json must define at least one curve");
    }

    for (const auto& module_id : module_ids) {
        if (!has_curve_for_provider(curves, module_id)) {
            throw std::invalid_argument("no derating curve configured for temperature provider: " + module_id);
        }
    }
}

TemperatureProviderIgnoreList parse_temperature_provider_ignore_list(const std::string& csv) {
    TemperatureProviderIgnoreList ignore_list;
    if (csv.empty()) {
        return ignore_list;
    }

    std::stringstream stream(csv);
    std::string entry;
    while (std::getline(stream, entry, ',')) {
        trim_inplace(entry);
        if (entry.empty()) {
            continue;
        }
        validate_curve_key_format(entry);
        ignore_list.insert(entry);
    }
    return ignore_list;
}

void validate_ignore_list_vs_curves(const DeratingCurveMap& curves, const TemperatureProviderIgnoreList& ignore_list) {
    for (const auto& [module_id, identifications] : curves) {
        for (const auto& [identification, curve] : identifications) {
            (void)curve;
            const auto curve_key = make_curve_key(module_id, identification);
            if (ignore_list.count(curve_key) != 0) {
                throw std::invalid_argument("derating curve configured for ignored temperature reading: " + curve_key);
            }
        }
    }
}

bool is_temperature_reading_ignored(const TemperatureProviderIgnoreList& ignore_list, const std::string& module_id,
                                    const std::string& identification) {
    return ignore_list.count(make_curve_key(module_id, identification)) != 0;
}

std::optional<double> interpolate_max_current_A(const DeratingCurve& curve, double temp_C) {
    if (curve.empty()) {
        return std::nullopt;
    }
    if (temp_C < curve.front().m_temp_c) {
        return std::nullopt;
    }
    if (temp_C == curve.front().m_temp_c) {
        return curve.front().m_max_current_a;
    }
    if (temp_C >= curve.back().m_temp_c) {
        return curve.back().m_max_current_a;
    }

    const auto upper_it =
        std::upper_bound(curve.begin(), curve.end(), temp_C,
                         [](double value, const DeratingPoint& point) { return value < point.m_temp_c; });
    const DeratingPoint& upper = *upper_it;
    const DeratingPoint& lower = *std::prev(upper_it);

    const double span = upper.m_temp_c - lower.m_temp_c;
    if (span <= 0.0) {
        return upper.m_max_current_a;
    }

    const double ratio = (temp_C - lower.m_temp_c) / span;
    return lower.m_max_current_a + ratio * (upper.m_max_current_a - lower.m_max_current_a);
}

const DeratingCurve* find_derating_curve(const DeratingCurveMap& curves, const std::string& module_id,
                                         const std::string& identification) {
    const auto provider_it = curves.find(module_id);
    if (provider_it == curves.end()) {
        return nullptr;
    }
    const auto curve_it = provider_it->second.find(identification);
    if (curve_it == provider_it->second.end()) {
        return nullptr;
    }
    return &curve_it->second;
}

ComputeLimitResult compute_effective_limit_A(const DeratingCurveMap& curves,
                                             const std::vector<SensorReadingInput>& readings,
                                             double fallback_max_current_A) {
    ComputeLimitResult result;
    if (readings.empty()) {
        return result;
    }

    double effective_limit = std::numeric_limits<double>::infinity();
    bool any_limit_applied = false;

    for (const auto& reading : readings) {
        double sensor_limit = fallback_max_current_A;

        if (!reading.m_temperature_C.has_value()) {
            any_limit_applied = true;
            effective_limit = std::min(effective_limit, sensor_limit);
            continue;
        }

        if (!std::isfinite(reading.m_temperature_C.value())) {
            any_limit_applied = true;
            effective_limit = std::min(effective_limit, sensor_limit);
            continue;
        }

        if (!reading.m_identification.has_value() || reading.m_identification->empty()) {
            result.m_missing_identification_curve_keys.push_back(reading.m_module_id);
            any_limit_applied = true;
            effective_limit = std::min(effective_limit, sensor_limit);
            continue;
        }

        const DeratingCurve* curve = find_derating_curve(curves, reading.m_module_id, reading.m_identification.value());
        if (curve == nullptr || curve->empty()) {
            result.m_missing_curve_keys.push_back(
                make_curve_key(reading.m_module_id, reading.m_identification.value()));
            any_limit_applied = true;
            effective_limit = std::min(effective_limit, sensor_limit);
            continue;
        }

        const auto limit = interpolate_max_current_A(*curve, reading.m_temperature_C.value());
        if (!limit.has_value()) {
            continue; // below the curve's first point -> this sensor imposes no limit
        }
        sensor_limit = limit.value();
        any_limit_applied = true;
        effective_limit = std::min(effective_limit, sensor_limit);
    }

    if (!any_limit_applied || !std::isfinite(effective_limit)) {
        return result;
    }

    result.m_effective_limit_A = effective_limit;
    return result;
}

} // namespace ac_temperature_derating
