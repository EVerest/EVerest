// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <sys/types.h>

namespace Everest {
struct ManagerSettings;
} // namespace Everest

/// \brief Manages the (optional) admin-panel controller subprocess, which allows the manager
/// to be remotely controlled and monitored when the admin panel feature is enabled at build
/// time. Acts as a no-op when the admin panel is disabled.
struct ManagerAdminPanel {
    /// \brief Destructor. Does not signal or wait for the controller process; use
    /// \ref shutdown_controller to request its termination beforehand.
    ~ManagerAdminPanel();
    /// \brief Move constructor.
    ManagerAdminPanel(ManagerAdminPanel&&) noexcept;
    /// \brief Move assignment operator.
    ManagerAdminPanel& operator=(ManagerAdminPanel&&) noexcept;

    /// \brief ManagerAdminPanel is not copyable, only movable.
    ManagerAdminPanel(const ManagerAdminPanel&) = delete;
    ManagerAdminPanel& operator=(const ManagerAdminPanel&) = delete;

    /// \brief Creates a ManagerAdminPanel. When built with the admin panel enabled, this
    /// starts the controller subprocess and establishes the IPC channel to it; otherwise a
    /// disabled instance is returned.
    /// \param ms manager settings used to configure, and (if enabled) launch, the controller.
    /// \returns the created ManagerAdminPanel instance.
    static ManagerAdminPanel create(const Everest::ManagerSettings& ms);
    /// \brief Switches the current process to the configured `run_as_user`, if needed. Only
    /// takes effect when the admin panel is disabled; with the admin panel enabled, the user
    /// switch instead happens inside the separate controller subprocess.
    /// \param ms manager settings containing the optional `run_as_user`.
    /// \returns an error message if switching the user failed, or `nullopt` on success or when
    /// no switch was required.
    static std::optional<std::string> switch_manager_user_if_needed(const Everest::ManagerSettings& ms);
    /// \brief Requests the controller process to shut down by sending it SIGTERM. Does
    /// nothing if the admin panel is disabled or not initialized.
    void shutdown_controller() const;
    /// \brief Checks whether the given pid is the controller subprocess.
    /// \param pid the process id to check.
    /// \returns true if the admin panel is enabled, initialized, and pid matches the
    /// controller process; false otherwise.
    bool is_controller_process(pid_t pid) const;

    /// \brief Throws if the given exited process was the controller, or if the admin panel was
    /// never initialized. Used to detect an unexpected controller exit. Does nothing when the
    /// admin panel is disabled.
    /// \param pid the process id of the process that exited.
    /// \throws std::runtime_error if the admin panel is enabled but uninitialized, or if pid
    /// matches the controller process.
    void throw_if_controller_exited(pid_t pid) const;

    /// Process controller IPC for one main-loop iteration.
    /// Returns an exit code for `boot`, or `nullopt` to continue.
    std::optional<int> poll_controller_ipc(bool& restart_modules, bool& modules_started, const std::string& prefix_opt);

    /// File descriptor of the controller IPC socket, usable as an extra wakeup fd for the
    /// manager main-loop poll. nullopt when the admin panel is disabled or not initialized.
    std::optional<int> controller_ipc_fd() const;

private:
#ifdef ENABLE_ADMIN_PANEL
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    ManagerAdminPanel() = delete;
    explicit ManagerAdminPanel(std::unique_ptr<Impl> impl);
#else
    ManagerAdminPanel();
#endif
};
