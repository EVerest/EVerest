// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <utils/config_service_interface.hpp>

namespace Everest::tests {

/// \brief Minimal, configurable ConfigServiceInterface fake for the management-API handler tests.
///
/// The mutating commands record how often they were invoked (so tests can assert a read-only
/// command never reached the service) and return a value the test set beforehand. `throw_on_*`
/// lets a test exercise the API's exception -> failure-reply path; add a flag for another command
/// when a test needs one. The two push-event handlers registered by the APIs are captured so tests
/// can drive them directly.
struct FakeConfigService : public ::Everest::config::ConfigServiceInterface {
    using SlotInfo = ::Everest::config::SlotInfo;
    using SetActiveSlotStatus = ::Everest::config::SetActiveSlotStatus;
    using DeleteSlotStatus = ::Everest::config::DeleteSlotStatus;
    using DuplicateSlotResult = ::Everest::config::DuplicateSlotResult;
    using LoadFromYamlResult = ::Everest::config::LoadFromYamlResult;
    using SetConfigParameterResult = ::Everest::config::SetConfigParameterResult;
    using GetConfigurationResult = ::Everest::config::GetConfigurationResult;
    using GetConfigParametersResult = ::Everest::config::GetConfigParametersResult;
    using ConfigParameterUpdate = ::Everest::config::ConfigParameterUpdate;
    using Origin = ::Everest::config::Origin;
    using ActiveSlotUpdate = ::Everest::config::ActiveSlotUpdate;
    using ConfigurationUpdate = ::Everest::config::ConfigurationUpdate;

    // --- call counters ---
    int list_all_slots_calls = 0;
    int mark_active_slot_calls = 0;
    int delete_slot_calls = 0;
    int duplicate_slot_calls = 0;
    int load_from_yaml_calls = 0;
    int set_description_calls = 0;
    int set_config_parameters_calls = 0;
    int get_config_parameters_calls = 0;
    int get_configuration_calls = 0;

    // --- arguments recorded by the read commands, so a test can check what the API forwarded ---
    int last_get_config_parameters_slot_id = -1;
    std::vector<everest::config::ConfigurationParameterIdentifier> last_requested_parameters;
    bool last_get_config_parameters_force_read_from_db = false;
    int last_get_configuration_slot_id = -1;
    bool last_get_configuration_force_read_from_db = false;

    // --- configurable return values ---
    std::vector<SlotInfo> slots;
    int active_slot_id = 0;
    int next_boot_slot_id = 0;
    SetActiveSlotStatus mark_active_slot_result = SetActiveSlotStatus::Success;
    DeleteSlotStatus delete_slot_result = DeleteSlotStatus::Success;
    DuplicateSlotResult duplicate_slot_result{};
    LoadFromYamlResult load_from_yaml_result{};
    bool set_description_result = true;
    SetConfigParameterResult set_config_parameters_result{};
    GetConfigParametersResult get_config_parameters_result{};
    GetConfigurationResult get_configuration_result{};

    // --- exception controls ---
    bool throw_on_list_all_slots = false;
    bool throw_on_mark_active_slot = false;

    // --- captured push-event handlers ---
    std::function<void(const ActiveSlotUpdate&)> active_slot_handler;
    std::function<void(const ConfigurationUpdate&)> config_update_handler;

    // --- slot management ---
    std::vector<SlotInfo> list_all_slots() override {
        ++list_all_slots_calls;
        if (throw_on_list_all_slots) {
            throw std::runtime_error("fake list_all_slots failure");
        }
        return slots;
    }
    int get_active_slot_id() override {
        return active_slot_id;
    }
    int get_next_boot_slot_id() override {
        return next_boot_slot_id;
    }
    SetActiveSlotStatus mark_active_slot(int) override {
        ++mark_active_slot_calls;
        if (throw_on_mark_active_slot) {
            throw std::runtime_error("fake mark_active_slot failure");
        }
        return mark_active_slot_result;
    }
    DeleteSlotStatus delete_slot(int) override {
        ++delete_slot_calls;
        return delete_slot_result;
    }
    DuplicateSlotResult duplicate_slot(int, std::optional<std::string>) override {
        ++duplicate_slot_calls;
        return duplicate_slot_result;
    }
    LoadFromYamlResult load_from_yaml(const std::string&, std::optional<std::string>, std::optional<int>) override {
        ++load_from_yaml_calls;
        return load_from_yaml_result;
    }
    bool set_description(int, const std::string&) override {
        ++set_description_calls;
        return set_description_result;
    }

    // --- active-slot in-memory access ---
    std::shared_ptr<const everest::config::ModuleConfigurations> get_active_module_configurations() const override {
        return std::make_shared<const everest::config::ModuleConfigurations>();
    }

    // --- slot-scoped configuration ---
    SetConfigParameterResult set_config_parameters(int, const std::vector<ConfigParameterUpdate>&,
                                                   const Origin&) override {
        ++set_config_parameters_calls;
        return set_config_parameters_result;
    }

    // --- push-event subscriptions ---
    void register_active_slot_update_handler(std::function<void(const ActiveSlotUpdate&)> handler) override {
        active_slot_handler = std::move(handler);
    }
    void register_config_update_handler(std::function<void(const ConfigurationUpdate&)> handler) override {
        config_update_handler = std::move(handler);
    }

    // --- module state ---
    void set_modules_stopped() override {
    }
    void set_modules_running() override {
    }
    void set_modules_starting() override {
    }
    void set_modules_stopping() override {
    }
    void notice_cfg_validation_failed() override {
    }
    void notice_module_restart_triggered() override {
    }

protected:
    GetConfigurationResult get_configuration_v(int slot_id, bool force_read_from_db) override {
        ++get_configuration_calls;
        last_get_configuration_slot_id = slot_id;
        last_get_configuration_force_read_from_db = force_read_from_db;
        return get_configuration_result;
    }
    GetConfigParametersResult
    get_config_parameters_v(int slot_id,
                            const std::vector<everest::config::ConfigurationParameterIdentifier>& parameters,
                            bool force_read_from_db) override {
        ++get_config_parameters_calls;
        last_get_config_parameters_slot_id = slot_id;
        last_requested_parameters = parameters;
        last_get_config_parameters_force_read_from_db = force_read_from_db;
        return get_config_parameters_result;
    }
};

} // namespace Everest::tests
