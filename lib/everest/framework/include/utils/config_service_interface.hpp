// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <utils/config/slot_manager.hpp>
#include <utils/config/types.hpp>

namespace Everest::config {

// ---------------------------------------------------------------------------
// Slot metadata
// ---------------------------------------------------------------------------

using SlotInfo = everest::config::SlotInfo;

// ---------------------------------------------------------------------------
// Status / result enums
// ---------------------------------------------------------------------------

enum class SetActiveSlotStatus {
    Success,
    NoChangeRequired,
    DoesNotExist,
    Rejected
};

enum class DeleteSlotStatus {
    Success,
    CannotDeleteActiveSlot,
    DoesNotExist,
    Rejected
};

enum class SetConfigParameterResultEnum {
    Applied,
    WillApplyOnRestart,
    DoesNotExist,
    RetryLater,
    AccessDenied,
    Rejected
};

enum class SetConfigParameterStatus {
    Ok,
    Error,
    ModulesInTransientState,
    AccessDenied
};

struct SetConfigPerParameterResult {
    SetConfigParameterResultEnum status;
    std::string status_info;

    SetConfigPerParameterResult(SetConfigParameterResultEnum status_, std::string status_info_) :
        status(status_), status_info(status_info_){};
};

struct SetConfigParameterResult {
    SetConfigParameterStatus status = SetConfigParameterStatus::Error;
    std::string status_info;
    std::optional<std::vector<SetConfigPerParameterResult>> parameter_results;
};

enum class GetConfigurationStatus {
    Success,
    SlotDoesNotExist
};

enum class ActiveSlotStatus {
    Running,
    Stopped,
    Starting,
    Stopping,
    FailedToStart,
    RestartTriggered
};

// ---------------------------------------------------------------------------
// Compound result types
// ---------------------------------------------------------------------------

using DuplicateSlotResult = everest::config::DuplicateSlotResult;

struct LoadFromYamlResult {
    bool success = false;
    std::optional<int> slot_id;
    std::string error_message;
};

// ---------------------------------------------------------------------------
// Input / event types
// ---------------------------------------------------------------------------

struct ConfigParameterUpdate {
    everest::config::ConfigurationParameterIdentifier identifier;
    std::string value;
    /// \brief When true the module confirmed the value took effect without a reboot
    /// When false (default) the value is only persisted to storage (WillApplyOnRestart).
    bool immediately_applied = false;
};

struct ActiveSlotUpdate {
    std::string timestamp;
    int active_slot_id;
    std::optional<int> next_boot_slot_id;
    ActiveSlotStatus status;
};

struct ConfigParameterUpdateNotice {
    everest::config::ConfigurationParameterIdentifier identifier;
    std::string value;
    SetConfigParameterResult result;
};

struct ConfigurationUpdate {
    std::string timestamp;
    int slot_id;
    std::vector<ConfigParameterUpdateNotice> updates;
};

struct GetConfigurationResult {
    GetConfigurationStatus status = GetConfigurationStatus::SlotDoesNotExist;
    everest::config::ModuleConfigurations module_configurations;
};

// ---------------------------------------------------------------------------
// ConfigServiceInterface
// ---------------------------------------------------------------------------

class ConfigServiceInterface {
public:
    virtual ~ConfigServiceInterface() = default;

    /// \brief Sentinel value for set_config_parameters() slot_id meaning the currently active slot.
    static constexpr int ACTIVE_SLOT = -1;

    // --- Slot management ---
    virtual std::vector<SlotInfo> list_all_slots() = 0;
    virtual int get_active_slot_id() = 0;
    virtual int get_next_boot_slot_id() = 0;
    virtual SetActiveSlotStatus mark_active_slot(int slot_id) = 0;
    virtual DeleteSlotStatus delete_slot(int slot_id) = 0;
    virtual DuplicateSlotResult duplicate_slot(int slot_id, std::optional<std::string> description) = 0;
    virtual LoadFromYamlResult load_from_yaml(const std::string& raw_yaml, std::optional<std::string> description, std::optional<int> slot_id) = 0;

    // --- Active-slot in-memory access ---
    virtual const everest::config::ModuleConfigurations& get_active_module_configurations() const = 0;

    // --- Slot-scoped configuration ---
    virtual GetConfigurationResult get_configuration(int slot_id) = 0;
    virtual std::vector<SetConfigParameterResult>
    set_config_parameters(int slot_id, const std::vector<ConfigParameterUpdate>& updates) = 0;

    // --- Push-event subscriptions ---
    virtual void register_active_slot_update_handler(std::function<void(const ActiveSlotUpdate&)> handler) = 0;
    virtual void register_config_update_handler(std::function<void(const ConfigurationUpdate&)> handler) = 0;

    // --- Module state ---
    virtual void set_modules_stopped() = 0;
    virtual void set_modules_running() = 0;
    virtual void set_modules_starting() = 0;
    virtual void set_modules_stopping() = 0;
};

} // namespace Everest::config
