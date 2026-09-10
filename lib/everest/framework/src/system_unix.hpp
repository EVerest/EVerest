// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <cstdint>
#include <sys/types.h>

namespace Everest::system {

/// \brief Fork helper that reports exec()-failures of the child back to the parent via a pipe.
class SubProcess {
public:
    /// \brief Fork a child process, optionally dropping to \p run_as_user and setting \p capabilities.
    /// \param run_as_user User name the child switches to; empty keeps the current user.
    /// \param capabilities Capability names the child keeps across the user switch.
    /// \return Handle for the pipe-based exec handshake; is_child() tells which side we are on.
    static SubProcess create(const std::string& run_as_user, const std::vector<std::string>& capabilities = {});

    /// \brief True when this handle belongs to the forked child process.
    bool is_child() const {
        return m_pid == 0;
    }

    /// \brief Child side only: write \p message to the parent and _exit(EXIT_FAILURE).
    void send_error_and_exit(const std::string& message);

    /// \brief Parent side only: block until the child either exec()s (pipe closes) or reports an error.
    /// \return Child pid on success.
    pid_t check_child_executed();

private:
    const std::size_t MAX_PIPE_MESSAGE_SIZE = 1024;
    SubProcess(int fd, pid_t pid) : m_fd(fd), m_pid(pid) {
    }
    int m_fd{};
    pid_t m_pid{0};
    bool m_check_child_executed_done{false};
};

/// \brief Keep capabilities across setuid/setgid (SECBIT_KEEP_CAPS).
/// \return true on success.
bool keep_caps();

/// \brief Add \p capabilities to the inheritable and ambient capability sets of this process.
/// \return Empty string on success, error description otherwise.
std::string set_caps(const std::vector<std::string>& capabilities);

/// \brief Switch the real user (groups, gid, uid) of this process to \p user_name.
/// \return Empty string on success, error description otherwise.
std::string set_real_user(const std::string& user_name);

/// \brief Combine keep_caps(), set_real_user() and set_caps() for a child process.
/// \return Empty string on success, error description otherwise.
std::string set_user_and_capabilities(const std::string& run_as_user, const std::vector<std::string>& capabilities);

/// \brief Poll SIGINT/SIGTERM/SIGCHLD via a signalfd (signals are blocked for regular delivery).
class SignalPolling {
public:
    SignalPolling();

    /// \brief Wait up to \p timeout_ms for a blocked signal (SIGINT/SIGTERM/SIGCHLD) to arrive.
    ///        When \p extra_wakeup_fd / \p extra_wakeup_fd2 are not -1, the poll also returns (with
    ///        std::nullopt) as soon as either fd becomes readable, so the caller can service it
    ///        without waiting for the timeout. Neither extra fd is read/drained here; the caller
    ///        owns draining them.
    /// \return The received signal number, or std::nullopt on timeout/extra fd wakeup.
    std::optional<uint32_t> poll_signal(int timeout_ms, int extra_wakeup_fd = -1, int extra_wakeup_fd2 = -1);

private:
    bool m_available{false};
    int m_signal_fd{-1};
};

} // namespace Everest::system
