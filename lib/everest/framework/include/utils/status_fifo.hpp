// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2023 Pionix GmbH and Contributors to EVerest
#ifndef STATUS_FIFO_HPP
#define STATUS_FIFO_HPP

#include <string>

namespace Everest {

class StatusFifo {
public:
    // defined messages
    static constexpr auto ALL_MODULES_STARTED = "ALL_MODULES_STARTED\n";
    static constexpr auto WAITING_FOR_STANDALONE_MODULES = "WAITING_FOR_STANDALONE_MODULES\n";

    // manager lifecycle state notifications (written on ManagerState transitions)
    static constexpr auto MANAGER_INITIALIZING = "MANAGER_INITIALIZING\n";
    static constexpr auto MANAGER_STARTING_MODULES = "MANAGER_STARTING_MODULES\n";
    static constexpr auto MANAGER_RUNNING = "MANAGER_RUNNING\n";
    static constexpr auto MANAGER_RESTART_REQUESTED = "MANAGER_RESTART_REQUESTED\n";
    static constexpr auto MANAGER_CRASH_SHUTDOWN_IN_PROGRESS = "MANAGER_CRASH_SHUTDOWN_IN_PROGRESS\n";
    static constexpr auto MANAGER_SHUTDOWN_REQUESTED = "MANAGER_SHUTDOWN_REQUESTED\n";
    static constexpr auto MANAGER_FORCE_TERMINATING = "MANAGER_FORCE_TERMINATING\n";
    static constexpr auto MANAGER_SHUTDOWN_FINALIZING = "MANAGER_SHUTDOWN_FINALIZING\n";
    static constexpr auto MANAGER_IDLE = "MANAGER_IDLE\n";
    static constexpr auto MANAGER_EXITING = "MANAGER_EXITING\n";

    // semantic lifecycle events (not 1:1 with ManagerState)
    static constexpr auto SIGINT_RECEIVED = "SIGINT_RECEIVED\n";
    static constexpr auto ALL_MODULES_STOPPED_CLEAN = "ALL_MODULES_STOPPED_CLEAN\n";
    static constexpr auto FORCE_SHUTDOWN_TIMEOUT = "FORCE_SHUTDOWN_TIMEOUT\n";
    static constexpr auto CRASH_RECOVERY_EXHAUSTED = "CRASH_RECOVERY_EXHAUSTED\n";

    static StatusFifo create_from_path(const std::string&);
    void update(const std::string&);

    StatusFifo(StatusFifo const&) = delete;
    StatusFifo& operator=(StatusFifo const&) = delete;
    // NOTE (aw): the move constructor could be implementented, but we don't need it for now
    StatusFifo(StatusFifo&&) = delete;
    StatusFifo& operator=(StatusFifo&&) = delete;
    ~StatusFifo();

private:
    StatusFifo() = default;
    explicit StatusFifo(int fd_) : fd(fd_), disabled(false), opened(true){};

    int fd{-1};
    bool disabled{true};
    bool opened{false};
};

} // namespace Everest

#endif // STATUS_FIFO_HPP
