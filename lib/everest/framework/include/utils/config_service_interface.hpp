// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
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
    Failed
};

enum class DeleteSlotStatus {
    Success,
    CannotDeleteActiveSlot,
    DoesNotExist,
    Failed
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
    ModulesInTransientState
};

struct SetConfigPerParameterResult {
    SetConfigParameterResultEnum status;
    std::string status_info;

    SetConfigPerParameterResult(SetConfigParameterResultEnum status_, std::string status_info_) :
        status(status_), status_info(status_info_){};
};

struct SetConfigParameterResult {
    SetConfigParameterStatus status{SetConfigParameterStatus::Error};
    std::string status_info;
    std::optional<std::vector<SetConfigPerParameterResult>> parameter_results;
};

enum class GetConfigurationStatus {
    Success,
    SlotDoesNotExist,
    Failed
};

enum class ActiveSlotStatus {
    Running,
    Stopped,
    Starting,
    Stopping,
    FailedToStart,
    RestartTriggered
};

/// \brief True when no module process is alive and no lifecycle transition is in flight.
constexpr bool modules_are_down(ActiveSlotStatus status) {
    return status == ActiveSlotStatus::Stopped or status == ActiveSlotStatus::FailedToStart;
}

/// \brief True while a module lifecycle transition is in flight.
constexpr bool modules_in_transient_state(ActiveSlotStatus status) {
    return not modules_are_down(status) and status != ActiveSlotStatus::Running;
}

/// \brief Human-readable name, for log messages and status payloads.
constexpr std::string_view to_string(ActiveSlotStatus status) {
    switch (status) {
    case ActiveSlotStatus::Running:
        return "Running";
    case ActiveSlotStatus::Stopped:
        return "Stopped";
    case ActiveSlotStatus::Starting:
        return "Starting";
    case ActiveSlotStatus::Stopping:
        return "Stopping";
    case ActiveSlotStatus::FailedToStart:
        return "FailedToStart";
    case ActiveSlotStatus::RestartTriggered:
        return "RestartTriggered";
    }
    return "Unknown";
}

/// \brief What produced an ActiveSlotUpdate - the axis consumers must discriminate on.
///
/// A SlotInfo event reports a new active/next-boot slot and repeats the current module status
/// unchanged, so a status-only consumer must ignore it. A ModuleStatus event is always meaningful
/// even when it repeats the previous status: a second failed start reports FailedToStart again, and
/// a second restart request reports RestartTriggered again. Comparing status values cannot tell the
/// two apart, which is why the cause is carried explicitly.
enum class ActiveSlotUpdateCause {
    ModuleStatus,
    SlotInfo
};

// ---------------------------------------------------------------------------
// Compound result types
// ---------------------------------------------------------------------------

using DuplicateSlotResult = everest::config::DuplicateSlotResult;

struct LoadFromYamlResult {
    bool success{false};
    std::optional<int> slot_id;
    std::string error_message;
};

// ---------------------------------------------------------------------------
// Input / event types
// ---------------------------------------------------------------------------

struct ConfigParameterUpdate {
    everest::config::ConfigurationParameterIdentifier identifier;
    std::string value;
};

struct ActiveSlotUpdate {
    std::string timestamp;
    int active_slot_id;
    std::optional<int> next_boot_slot_id;
    ActiveSlotStatus status;
    // Defaulted so a default-constructed update (test helpers) reads as a status event; every
    // production event gets its cause explicitly from publish_active_slot_update().
    ActiveSlotUpdateCause cause{ActiveSlotUpdateCause::ModuleStatus};
};

struct ConfigParameterUpdateNotice {
    everest::config::ConfigurationParameterIdentifier identifier;
    std::string value;
    SetConfigParameterResultEnum result;
};

struct Origin {
    bool external;
    std::optional<std::string> module_id;
};

struct ConfigurationUpdate {
    std::string timestamp;
    int slot_id;
    std::vector<ConfigParameterUpdateNotice> updates;
    Origin origin;
};

struct GetConfigurationResult {
    GetConfigurationStatus status{GetConfigurationStatus::SlotDoesNotExist};
    everest::config::ModuleConfigurations module_configurations;
};

struct GetConfigParametersResult {
    GetConfigurationStatus status{GetConfigurationStatus::SlotDoesNotExist};
    std::vector<std::optional<everest::config::ConfigurationParameter>> parameters;
};

// ---------------------------------------------------------------------------
// ConfigServiceInterface
// ---------------------------------------------------------------------------

/// \brief Abstract interface to EVerest's persisted configuration, exposing all domain operations on it.
///
/// The configuration is organized in numbered *slots*. Each slot holds a complete, independently
/// persisted set of ModuleConfigurations. Exactly one slot is the *active* slot: the one the running
/// modules are started from. A (possibly different) slot is marked as the *next boot* slot, which
/// becomes active on the next start. This allows a new configuration to be prepared and validated
/// while the current one keeps running.
///
/// Responsibilities grouped by the sections below:
///   * Slot management: list, create (load_from_yaml / duplicate_slot), describe, delete slots
///     and select which one boots next (mark_active_slot).
///   * Active-slot in-memory access: get_active_module_configurations() hands out the immutable
///     snapshot the running modules use.
///   * Slot-scoped configuration: read and write individual parameters in any slot. Writes to the
///     active slot may take effect immediately, or only after a restart, depending on the
///     parameter's mutability and on whether the modules are currently running. The return value
///     is therefore per-parameter (SetConfigParameterResultEnum).
///   * Push-event subscriptions: report active-slot and configuration changes to interested parties
///     (e.g. an external API) without polling.
///   * Module state: the owner of the module lifecycle (the manager) tells the service whether the
///     modules are stopped/starting/running/stopping. The service needs this to decide how a write
///     to the active slot can be applied, to reject writes while the modules are mid-transition
///     (SetConfigParameterStatus::ModulesInTransientState) and to report the state in
///     ActiveSlotUpdate. Note that only the statuses for which modules_in_transient_state() holds
///     are mid-transition: FailedToStart is a *resting* status (see modules_are_down()), so writing
///     to the active slot and reloading it stay possible after a failed start - that is what makes
///     such a failure recoverable.
///
/// Implementations are expected to be callable from several threads.
///
/// get_configuration() and get_config_parameters() are non-virtual wrappers around the protected
/// *_v() hooks, so that their default arguments are fixed here instead of per implementation.
/// Implementations override the *_v() hooks; callers use the public wrappers.
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
    virtual LoadFromYamlResult load_from_yaml(const std::string& raw_yaml, std::optional<std::string> description,
                                              std::optional<int> slot_id) = 0;
    virtual bool set_description(int slot_id, const std::string& description) = 0;

    // --- Active-slot in-memory access ---
    virtual std::shared_ptr<const everest::config::ModuleConfigurations> get_active_module_configurations() const = 0;

    // --- Slot-scoped configuration ---
    virtual SetConfigParameterResult
    set_config_parameters(int slot_id, const std::vector<ConfigParameterUpdate>& updates, const Origin& origin) = 0;
    GetConfigurationResult get_configuration(int slot_id, bool force_read_from_db = false) {
        return get_configuration_v(slot_id, force_read_from_db);
    }
    GetConfigParametersResult
    get_config_parameters(int slot_id, const std::vector<everest::config::ConfigurationParameterIdentifier>& parameters,
                          bool force_read_from_db = false) {
        return get_config_parameters_v(slot_id, parameters, force_read_from_db);
    }

    // --- Push-event subscriptions ---
    // Handlers may be invoked from an internal worker thread of the implementation and may block
    // further config-service processing while they run: keep them short and do not wait on
    // anything that itself needs the config service.
    virtual void register_active_slot_update_handler(std::function<void(const ActiveSlotUpdate&)> handler) = 0;
    virtual void register_config_update_handler(std::function<void(const ConfigurationUpdate&)> handler) = 0;

    // --- Module state ---
    virtual void set_modules_stopped() = 0;
    virtual void set_modules_running() = 0;
    virtual void set_modules_starting() = 0;
    virtual void set_modules_stopping() = 0;
    virtual void notice_cfg_validation_failed() = 0;
    virtual void notice_module_restart_triggered() = 0;

protected:
    virtual GetConfigurationResult get_configuration_v(int slot_id, bool force_read_from_db) = 0;

    virtual GetConfigParametersResult
    get_config_parameters_v(int slot_id,
                            const std::vector<everest::config::ConfigurationParameterIdentifier>& parameters,
                            bool force_read_from_db) = 0;
};

} // namespace Everest::config
