// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef AC_TEMPERATURE_DERATING_HPP
#define AC_TEMPERATURE_DERATING_HPP

//
// AUTO GENERATED - MARKED REGIONS WILL BE KEPT
// template version 2
//

#include "ld-ev.hpp"

// headers for required interface implementations
#include <generated/interfaces/external_energy_limits/Interface.hpp>
#include <generated/interfaces/temperature_sensor/Interface.hpp>

// ev@4bf81b14-a215-475c-a1d3-0a484ae48918:v1
#include "derating/derating_curve.hpp"

#include <everest/timer.hpp>

#include <chrono>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
// ev@4bf81b14-a215-475c-a1d3-0a484ae48918:v1

namespace module {

struct Conf {
    std::string derating_curves_json;
    std::string temperature_provider_ignore_list;
    double fallback_max_current_A;
    int temperature_stale_timeout_ms;
    int update_debounce_ms;
};

class AcTemperatureDerating : public Everest::ModuleBase {
public:
    AcTemperatureDerating() = delete;
    AcTemperatureDerating(const ModuleInfo& info, std::vector<std::unique_ptr<temperature_sensorIntf>> r_temperature,
                          std::unique_ptr<external_energy_limitsIntf> r_energy_node, Conf& config) :
        ModuleBase(info),
        r_temperature(std::move(r_temperature)),
        r_energy_node(std::move(r_energy_node)),
        config(config){};

    const std::vector<std::unique_ptr<temperature_sensorIntf>> r_temperature;
    const std::unique_ptr<external_energy_limitsIntf> r_energy_node;
    const Conf& config;

    // ev@1fce4c5e-0ab8-41bb-90f7-14277703d2ac:v1
    // insert your public definitions here
    struct TestAccess {
        static void init(AcTemperatureDerating& self) {
            self.init();
        }

        static void set_identified_reading(AcTemperatureDerating& self, std::size_t provider_index,
                                           const std::string& identification, double temp_C,
                                           std::chrono::milliseconds age_before_now) {
            const auto now = std::chrono::steady_clock::now();
            auto& provider = self.provider_states_.at(provider_index);
            auto& state = provider.readings_by_identification[identification];
            state.temperature_C = temp_C;
            state.last_update = now - age_before_now;
            state.ever_received = true;
        }

        static void update_and_publish_limits(AcTemperatureDerating& self) {
            self.update_and_publish_limits();
        }

        static void set_last_publish_time_age(AcTemperatureDerating& self, std::chrono::milliseconds age_before_now) {
            self.last_publish_time_ = std::chrono::steady_clock::now() - age_before_now;
        }

        static void clear_last_published_limit(AcTemperatureDerating& self) {
            self.last_published_limit_A_.reset();
            self.last_publish_time_ = {};
        }
    };
    // ev@1fce4c5e-0ab8-41bb-90f7-14277703d2ac:v1

protected:
    // ev@4714b2ab-a24f-4b95-ab81-36439e1478de:v1
    // insert your protected definitions here
    // ev@4714b2ab-a24f-4b95-ab81-36439e1478de:v1

private:
    friend class LdEverest;
    void init();
    void ready();

    // ev@211cfdbe-f69a-4cd6-a4ec-f8aaa3d1b6c8:v1
    struct ReadingState {
        std::optional<double> temperature_C;
        std::chrono::steady_clock::time_point last_update{};
        bool ever_received{false};
    };

    struct ProviderState {
        std::string module_id;
        bool has_reading_without_identification{false};
        ReadingState reading_without_identification{};
        std::unordered_map<std::string, ReadingState> readings_by_identification;
    };

    ac_temperature_derating::DeratingCurveMap derating_curves_;
    ac_temperature_derating::TemperatureProviderIgnoreList ignore_list_;
    std::vector<ProviderState> provider_states_;
    std::mutex state_mutex_;

    std::optional<double> last_published_limit_A_;
    std::chrono::steady_clock::time_point last_publish_time_{};
    std::set<std::string> warned_missing_curve_keys_;

    void report_missing_curves(const ac_temperature_derating::ComputeLimitResult& result);
    void update_and_publish_limits();
    void apply_external_limit(double limit_A);

    // Periodically re-evaluates limits so staleness/fallback are enforced even when a provider
    // stops publishing (no subscription callback would otherwise fire). Declared last so it is
    // destroyed first and its callback cannot run while other members are torn down.
    Everest::SteadyTimer reevaluate_timer_;
    // ev@211cfdbe-f69a-4cd6-a4ec-f8aaa3d1b6c8:v1
};

// ev@087e516b-124c-48df-94fb-109508c7cda9:v1
// insert other definitions here
// ev@087e516b-124c-48df-94fb-109508c7cda9:v1

} // namespace module

#endif // AC_TEMPERATURE_DERATING_HPP
