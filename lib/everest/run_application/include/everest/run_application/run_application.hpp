// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace everest::run_application {

/// \brief Used in the output_callback of run_application to indicate if the application should Continue to run or
/// Terminate
enum class CmdControl {
    Continue, ///< Continue application executiong
    Terminate ///< Terminate application
};

/// \brief Accumulation policy: retain every line of stdout (the default).
struct RetainAll {};

/// \brief Accumulation policy: retain nothing. output and split_output stay empty; the per-line callback still fires.
/// This is pure discard, so no omissions are counted.
struct RetainNone {};

/// \brief Accumulation policy: retain the newest lines, evicting the oldest once a cap is exceeded.
struct RetainTail {
    std::size_t max_lines; ///< Maximum number of retained lines
    std::size_t max_bytes; ///< Maximum retained content bytes (Σ line.size(), excluding the '\n' separator)
};

/// \brief Accumulation policy: retain the contiguous oldest prefix, dropping everything from the first line that would
/// breach a cap onward.
struct RetainHead {
    std::size_t max_lines; ///< Maximum number of retained lines
    std::size_t max_bytes; ///< Maximum retained content bytes (Σ line.size(), excluding the '\n' separator)
};

/// \brief Accumulation policy: retain a contiguous head prefix plus a newest-lines tail, silently dropping the middle.
struct RetainHeadTail {
    std::size_t head_lines; ///< Maximum number of retained head lines
    std::size_t head_bytes; ///< Maximum retained head content bytes (excluding '\n')
    std::size_t tail_lines; ///< Maximum number of retained tail lines
    std::size_t tail_bytes; ///< Maximum retained tail content bytes (excluding '\n')
};

/// \brief Selects how run_application retains child stdout. Illegal parameter combinations are unrepresentable.
using AccumulationPolicy = std::variant<RetainAll, RetainNone, RetainTail, RetainHead, RetainHeadTail>;

/// \brief Output and exit code of an application ran by run_application
struct CmdOutput {
    std::string output;                    ///< Σ(line + "\n") over the retained lines, in final order
    std::vector<std::string> split_output; ///< The retained stdout lines (no trailing newline), always whole lines
    int exit_code;                         ///< Exit code of the application
};

/// \brief Options controlling a run_application invocation.
struct RunOptions {
    /// Called after every line of stdout with that line. Returning CmdControl::Terminate breaks the read loop and
    /// terminates the child. Fires for every line under every accumulation policy (including RetainNone).
    std::function<CmdControl(const std::string& output_line)> callback = nullptr;
    /// How stdout is retained in CmdOutput. Independent of the callback.
    AccumulationPolicy accumulation = RetainAll{};
    /// Optional cancellation flag. When non-null, a watcher thread terminates the child (SIGTERM, then SIGKILL after
    /// terminate_grace) once it is set; exit_code then reflects the terminating signal.
    std::shared_ptr<std::atomic_bool> stop_requested = nullptr;
    /// When true, sets PR_SET_PDEATHSIG(SIGKILL) on the child (Linux only) so the kernel kills it when the calling
    /// THREAD dies. Note this is thread death, not process death (Linux semantics): a caller that invokes
    /// run_application on a transient worker thread will have the child killed when that worker thread exits, even if
    /// the process lives on. Use for long-lived children that must never outlive this process even when it is aborted
    /// without running destructors (e.g. a module process killed on shutdown), and only from a thread whose lifetime
    /// matches the process's.
    bool kill_child_on_parent_death = false;
    /// Grace period between SIGTERM and the escalating SIGKILL when stop_requested fires.
    std::chrono::milliseconds terminate_grace = std::chrono::seconds(2);
};

/// \brief Run the application specified by its \p name which can either be a full path or a binary name in PATH with
/// the provided \p args, governed by \p opts. This overload holds all of the process logic.
CmdOutput run_application(const std::string& name, std::vector<std::string> args, RunOptions opts);

/// \brief Run the application specified by its \p name which can either be a full path or a binary name in PATH with
/// the provided \p args.
/// A \p output_callback can be provided that gets called after every line of stdout that is also passed to this
/// callback. The callback must return a CmdContol of either Continue or Terminate, which decide if the application will
/// continue running or terminated early.
/// For cancellation, bounded output retention, or kill-on-parent-death, use the RunOptions overload above.
CmdOutput run_application(const std::string& name, std::vector<std::string> args,
                          const std::function<CmdControl(const std::string& output_line)> output_callback = nullptr);

} // namespace everest::run_application
