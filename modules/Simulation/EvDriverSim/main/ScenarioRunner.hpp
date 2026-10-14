// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <chrono>
#include <everest_api_types/ev_driver_sim/API.hpp>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace module {
namespace API_scenario = everest::lib::API::V1_0::types::ev_driver_sim;

class ScenarioRunner {
public:
    using Clock = std::chrono::steady_clock;
    struct Command {
        std::string name;
        std::string payload;
    };
    struct Step {
        std::chrono::milliseconds at;
        Command command;
    };
    struct Actions {
        std::function<std::optional<std::string>(const std::string&, const std::string&)> execute;
        std::function<void(const API_scenario::ScenarioEvent&)> publish;
    };

    std::optional<std::string> start(const API_scenario::RunScenarioParams& params, bool controller_seen,
                                     bool pilot_passed, const Actions& actions, Clock::time_point now = Clock::now());
    void configuration_ack(const std::optional<std::string>& rejection, const Actions& actions,
                           Clock::time_point now = Clock::now());
    void on_timer(const Actions& actions, Clock::time_point now = Clock::now());
    std::optional<Clock::time_point> next_deadline() const;
    std::string current_step() const;
    void reject(const std::string& reason, const Actions& actions);
    bool active() const {
        return m_next < m_steps.size();
    }

private:
    bool m_pending_configuration{false};
    std::vector<Step> m_steps;
    std::size_t m_next{0};
    API_scenario::ScenarioName m_name{};
    Clock::time_point m_start{};
};
} // namespace module
