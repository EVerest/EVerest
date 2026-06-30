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
        derating_curves_ = ac_temperature_derating::parse_derating_curves_json(config.derating_curves_json);
        ignore_list_ =
            ac_temperature_derating::parse_temperature_provider_ignore_list(config.temperature_provider_ignore_list);

        std::vector<std::string> provider_module_ids;
        provider_module_ids.reserve(r_temperature.size());
        for (const auto& sensor : r_temperature) {
            provider_module_ids.push_back(sensor->module_id);
        }
        ac_temperature_derating::validate_curves_for_providers(derating_curves_, provider_module_ids);
        ac_temperature_derating::validate_ignore_list_vs_curves(derating_curves_, ignore_list_);
    } catch (const std::exception& e) {
        EVLOG_error << "AcTemperatureDerating configuration error: " << e.what();
        throw;
    }

    provider_states_.reserve(r_temperature.size());
    for (const auto& sensor : r_temperature) {
        provider_states_.push_back(ProviderState{.module_id = sensor->module_id});
    }
}

void AcTemperatureDerating::ready() {
    for (std::size_t index = 0; index < r_temperature.size(); ++index) {
        r_temperature[index]->subscribe_temperatures([this, index](const auto& temperatures) {
            const auto now = std::chrono::steady_clock::now();
            {
                std::scoped_lock lock(state_mutex_);
                auto& provider_state = provider_states_.at(index);

                for (const auto& reading : temperatures) {
                    if (!reading.identification.has_value() || reading.identification->empty()) {
                        provider_state.reading_without_identification.temperature_C =
                            static_cast<double>(reading.temperature);
                        provider_state.reading_without_identification.last_update = now;
                        provider_state.reading_without_identification.ever_received = true;
                        provider_state.has_reading_without_identification = true;
                        continue;
                    }
                    auto& state = provider_state.readings_by_identification[reading.identification.value()];
                    state.temperature_C = static_cast<double>(reading.temperature);
                    state.last_update = now;
                    state.ever_received = true;
                }
            }
            update_and_publish_limits();
        });
    }

    // Re-evaluate periodically so a provider that stops publishing eventually trips the stale
    // timeout (and falls back), and so a debounced increase is retried instead of being lost.
    const auto tick =
        std::max(std::chrono::milliseconds(config.temperature_stale_timeout_ms) / 2, std::chrono::milliseconds(250));
    reevaluate_timer_.interval([this]() { update_and_publish_limits(); }, tick);

    update_and_publish_limits();
}

void AcTemperatureDerating::report_missing_curves(const ac_temperature_derating::ComputeLimitResult& result) {
    std::set<std::string> currently_missing;

    for (const auto& module_id : result.missing_identification_curve_keys) {
        currently_missing.insert(module_id + " (missing Temperature.identification)");
    }
    for (const auto& curve_key : result.missing_curve_keys) {
        currently_missing.insert(curve_key);
    }

    for (const auto& key : currently_missing) {
        if (warned_missing_curve_keys_.insert(key).second) {
            EVLOG_warning << "No derating curve configured for temperature reading '" << key
                          << "'; applying fallback_max_current_A=" << config.fallback_max_current_A;
        }
    }

    for (auto it = warned_missing_curve_keys_.begin(); it != warned_missing_curve_keys_.end();) {
        if (currently_missing.count(*it) == 0) {
            EVLOG_info << "Derating curve available again for temperature reading '" << *it << "'";
            it = warned_missing_curve_keys_.erase(it);
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
        std::scoped_lock lock(state_mutex_);
        for (auto& provider : provider_states_) {
            bool any_non_ignored_reading_added = false;

            if (provider.has_reading_without_identification) {
                const auto& state = provider.reading_without_identification;
                if (state.ever_received && (now - state.last_update) <= stale_timeout) {
                    readings.push_back(ac_temperature_derating::SensorReadingInput{
                        .module_id = provider.module_id,
                        .identification = std::nullopt,
                        .temperature_C = state.temperature_C,
                    });
                    any_non_ignored_reading_added = true;
                } else {
                    // The unidentified reading has lapsed; clear it so a transient or unmappable
                    // reading does not pin the provider to fallback forever once it resumes
                    // publishing valid (identified) readings.
                    provider.has_reading_without_identification = false;
                    provider.reading_without_identification = ReadingState{};
                }
            }

            for (const auto& [identification, state] : provider.readings_by_identification) {
                if (ac_temperature_derating::is_temperature_reading_ignored(ignore_list_, provider.module_id,
                                                                            identification)) {
                    continue;
                }

                std::optional<double> temperature_C = std::nullopt;
                if (state.ever_received && (now - state.last_update) <= stale_timeout) {
                    temperature_C = state.temperature_C;
                }

                readings.push_back(ac_temperature_derating::SensorReadingInput{
                    .module_id = provider.module_id,
                    .identification = identification,
                    .temperature_C = temperature_C,
                });
                any_non_ignored_reading_added = true;
            }

            if (!any_non_ignored_reading_added && provider.readings_by_identification.empty() &&
                !provider.has_reading_without_identification) {
                readings.push_back(ac_temperature_derating::SensorReadingInput{
                    .module_id = provider.module_id,
                    .identification = std::nullopt,
                    .temperature_C = std::nullopt,
                });
            }
        }
    }

    const auto result =
        ac_temperature_derating::compute_effective_limit_A(derating_curves_, readings, config.fallback_max_current_A);

    {
        std::scoped_lock lock(state_mutex_);
        report_missing_curves(result);
    }

    if (!result.effective_limit_A.has_value()) {
        return;
    }

    const double new_limit_A = result.effective_limit_A.value();

    {
        std::scoped_lock lock(state_mutex_);
        if (last_published_limit_A_.has_value()) {
            const double previous_limit_A = last_published_limit_A_.value();
            if (std::abs(previous_limit_A - new_limit_A) < 0.01) {
                return;
            }
            // Decreases (more derating) are applied immediately for thermal safety; increases
            // (relaxing the limit) are debounced. A debounced increase is not lost: the periodic
            // re-evaluation retries it once the debounce window has elapsed.
            const bool is_increase = new_limit_A > previous_limit_A;
            if (is_increase) {
                const auto debounce = std::chrono::milliseconds(config.update_debounce_ms);
                if ((now - last_publish_time_) < debounce) {
                    return;
                }
            }
        }
        last_published_limit_A_ = new_limit_A;
        last_publish_time_ = now;
    }

    apply_external_limit(new_limit_A);
}

void AcTemperatureDerating::apply_external_limit(double limit_A) {
    EVLOG_info << "Applying temperature derating limit: " << limit_A << " A";
    auto external_limits = make_external_limits(limit_A);
    r_energy_node->call_set_external_limits(external_limits);
}

} // namespace module
