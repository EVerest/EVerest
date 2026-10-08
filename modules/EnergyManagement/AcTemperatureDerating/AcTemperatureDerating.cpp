// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "AcTemperatureDerating.hpp"

#include <date/date.h>
#include <everest/logging.hpp>
#include <utils/date.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <set>

namespace module {
namespace {

constexpr const char* EXTERNAL_LIMIT_SOURCE = "AcTemperatureDerating";

types::energy::ExternalLimits make_external_limits(double limit_A) {
    types::energy::ExternalLimits external_limits;
    types::energy::ScheduleReqEntry target_entry;
    target_entry.timestamp = Everest::Date::to_rfc3339(date::utc_clock::now());
    target_entry.limits_to_leaves.ac_max_current_A = {static_cast<float>(limit_A), EXTERNAL_LIMIT_SOURCE};
    external_limits.schedule_import = {target_entry};
    external_limits.schedule_export = {};
    external_limits.schedule_setpoints = {};
    return external_limits;
}

} // namespace

void AcTemperatureDerating::init() {
    try {
        m_derating_curves = ac_temperature_derating::parse_derating_curves_json(config.derating_curves_json);
        m_ignore_list =
            ac_temperature_derating::parse_temperature_provider_ignore_list(config.temperature_provider_ignore_list);

        std::vector<std::string> provider_module_ids;
        provider_module_ids.reserve(r_temperature.size());
        for (const auto& sensor : r_temperature) {
            provider_module_ids.push_back(sensor->module_id);
        }
        ac_temperature_derating::validate_curves_for_providers(m_derating_curves, provider_module_ids);
        ac_temperature_derating::validate_ignore_list_vs_curves(m_derating_curves, m_ignore_list);
    } catch (const std::exception& e) {
        EVLOG_error << "AcTemperatureDerating configuration error: " << e.what();
        throw;
    }

    m_provider_states.reserve(r_temperature.size());
    for (const auto& sensor : r_temperature) {
        ProviderState provider_state;
        provider_state.m_module_id = sensor->module_id;
        m_provider_states.push_back(provider_state);
    }
}

void AcTemperatureDerating::ready() {
    for (std::size_t index = 0; index < r_temperature.size(); ++index) {
        r_temperature[index]->subscribe_temperatures([this, index](const auto& temperatures) {
            const auto now = std::chrono::steady_clock::now();
            {
                std::scoped_lock lock(m_state_mutex);
                auto& provider_state = m_provider_states.at(index);

                for (const auto& reading : temperatures) {
                    if (!reading.identification.has_value() || reading.identification->empty()) {
                        provider_state.m_reading_without_identification =
                            ReadingState{static_cast<double>(reading.temperature), now, true};
                        continue;
                    }
                    auto& state = provider_state.m_readings_by_identification[reading.identification.value()];
                    state.m_temperature_C = static_cast<double>(reading.temperature);
                    state.m_last_update = now;
                    state.m_ever_received = true;
                }
            }
            update_and_publish_limits();
        });
    }

    // Re-evaluate periodically so a provider that stops publishing eventually trips the stale
    // timeout (and falls back), and so a debounced increase is retried instead of being lost.
    const auto tick =
        std::max(std::chrono::milliseconds(config.temperature_stale_timeout_ms) / 2, std::chrono::milliseconds(250));
    m_reevaluate_timer.interval([this]() { update_and_publish_limits(); }, tick);

    update_and_publish_limits();
}

void AcTemperatureDerating::report_missing_curves(const ac_temperature_derating::ComputeLimitResult& result) {
    std::set<std::string> currently_missing;

    for (const auto& module_id : result.m_missing_identification_curve_keys) {
        currently_missing.insert(module_id + " (missing Temperature.identification)");
    }
    for (const auto& curve_key : result.m_missing_curve_keys) {
        currently_missing.insert(curve_key);
    }

    for (const auto& key : currently_missing) {
        if (m_warned_missing_curve_keys.insert(key).second) {
            EVLOG_warning << "No derating curve configured for temperature reading '" << key
                          << "'; applying fallback_max_current_A=" << config.fallback_max_current_A;
        }
    }

    for (auto it = m_warned_missing_curve_keys.begin(); it != m_warned_missing_curve_keys.end();) {
        if (currently_missing.count(*it) == 0) {
            EVLOG_info << "Derating curve available again for temperature reading '" << *it << "'";
            it = m_warned_missing_curve_keys.erase(it);
        } else {
            ++it;
        }
    }
}

void AcTemperatureDerating::update_and_publish_limits() {
    const auto stale_timeout = std::chrono::milliseconds(config.temperature_stale_timeout_ms);
    const auto now = std::chrono::steady_clock::now();

    std::vector<ac_temperature_derating::SensorReadingInput> readings;

    {
        std::scoped_lock lock(m_state_mutex);
        for (auto& provider : m_provider_states) {
            bool any_non_ignored_reading_added = false;

            if (provider.m_reading_without_identification.has_value()) {
                const auto& state = provider.m_reading_without_identification.value();
                if (state.m_ever_received && (now - state.m_last_update) <= stale_timeout) {
                    ac_temperature_derating::SensorReadingInput reading;
                    reading.m_module_id = provider.m_module_id;
                    reading.m_identification = std::nullopt;
                    reading.m_temperature_C = state.m_temperature_C;
                    readings.push_back(reading);
                    any_non_ignored_reading_added = true;
                } else {
                    // The unidentified reading has lapsed; clear it so a transient or unmappable
                    // reading does not pin the provider to fallback forever once it resumes
                    // publishing valid (identified) readings.
                    provider.m_reading_without_identification.reset();
                }
            }

            for (const auto& [identification, state] : provider.m_readings_by_identification) {
                if (ac_temperature_derating::is_temperature_reading_ignored(m_ignore_list, provider.m_module_id,
                                                                            identification)) {
                    continue;
                }

                std::optional<double> temperature_C = std::nullopt;
                if (state.m_ever_received && (now - state.m_last_update) <= stale_timeout) {
                    temperature_C = state.m_temperature_C;
                }

                ac_temperature_derating::SensorReadingInput reading;
                reading.m_module_id = provider.m_module_id;
                reading.m_identification = identification;
                reading.m_temperature_C = temperature_C;
                readings.push_back(reading);
                any_non_ignored_reading_added = true;
            }

            if (!any_non_ignored_reading_added && provider.m_readings_by_identification.empty() &&
                !provider.m_reading_without_identification.has_value()) {
                ac_temperature_derating::SensorReadingInput reading;
                reading.m_module_id = provider.m_module_id;
                reading.m_identification = std::nullopt;
                reading.m_temperature_C = std::nullopt;
                readings.push_back(reading);
            }
        }
    }

    const auto result =
        ac_temperature_derating::compute_effective_limit_A(m_derating_curves, readings, config.fallback_max_current_A);

    {
        std::scoped_lock lock(m_state_mutex);
        report_missing_curves(result);
    }

    if (!result.m_effective_limit_A.has_value()) {
        return;
    }

    const double new_limit_A = result.m_effective_limit_A.value();

    {
        std::scoped_lock lock(m_state_mutex);
        if (m_last_published_limit_A.has_value()) {
            const double previous_limit_A = m_last_published_limit_A.value();
            if (std::abs(previous_limit_A - new_limit_A) < 0.01) {
                return;
            }
            // Decreases (more derating) are applied immediately for thermal safety; increases
            // (relaxing the limit) are debounced. A debounced increase is not lost: the periodic
            // re-evaluation retries it once the debounce window has elapsed.
            const bool is_increase = new_limit_A > previous_limit_A;
            if (is_increase) {
                const auto debounce = std::chrono::milliseconds(config.update_debounce_ms);
                if ((now - m_last_publish_time) < debounce) {
                    return;
                }
            }
        }
        m_last_published_limit_A = new_limit_A;
        m_last_publish_time = now;
    }

    apply_external_limit(new_limit_A);
}

void AcTemperatureDerating::apply_external_limit(double limit_A) {
    EVLOG_info << "Applying temperature derating limit: " << limit_A << " A";
    auto external_limits = make_external_limits(limit_A);
    r_energy_node->call_set_external_limits(external_limits);
}

} // namespace module
