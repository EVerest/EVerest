// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <everest/run_application/run_application.hpp>

#include <boost/version.hpp>
#if BOOST_VERSION >= 108600
#include <boost/process/v1/args.hpp>
#include <boost/process/v1/child.hpp>
#include <boost/process/v1/cmd.hpp>
#include <boost/process/v1/extend.hpp>
#include <boost/process/v1/io.hpp>
#include <boost/process/v1/pipe.hpp>
#include <boost/process/v1/search_path.hpp>
namespace everest_boost_process = boost::process::v1;
#else
#include <boost/process.hpp>
#include <boost/process/extend.hpp>
namespace everest_boost_process = boost::process;
#endif

#ifdef __linux__
#include <sys/prctl.h>
#endif

#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <deque>
#include <exception>
#include <iterator>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include <fmt/core.h>

#include <everest/logging.hpp>

namespace everest::run_application {

namespace {

// Applies an AccumulationPolicy to the child's stdout lines, retaining only what the policy permits.
// Never stores a partial line: a line that cannot fit under a cap is dropped whole.
class Accumulator {
public:
    explicit Accumulator(const AccumulationPolicy& policy) : policy(policy) {
    }

    void add(const std::string& line) {
        if (std::holds_alternative<RetainAll>(policy)) {
            head.push_back(line);
        } else if (std::holds_alternative<RetainNone>(policy)) {
            // discard
        } else if (const auto* p = std::get_if<RetainHead>(&policy)) {
            add_head(line, p->max_lines, p->max_bytes);
        } else if (const auto* p = std::get_if<RetainTail>(&policy)) {
            add_tail(line, p->max_lines, p->max_bytes);
        } else if (const auto* p = std::get_if<RetainHeadTail>(&policy)) {
            add_head_tail(line, *p);
        }
    }

    std::vector<std::string> take_retained() {
        std::vector<std::string> result = std::move(head);
        result.insert(result.end(), std::make_move_iterator(tail.begin()), std::make_move_iterator(tail.end()));
        return result;
    }

private:
    // Contiguous prefix: once a line breaches either cap, that line and every line after it are dropped.
    void add_head(const std::string& line, std::size_t max_lines, std::size_t max_bytes) {
        if (head_full) {
            return;
        }
        if (head.size() >= max_lines || head_bytes + line.size() > max_bytes) {
            head_full = true;
            return;
        }
        head.push_back(line);
        head_bytes += line.size();
    }

    // Newest lines: append, then evict from the front while over either cap.
    void add_tail(const std::string& line, std::size_t max_lines, std::size_t max_bytes) {
        tail.push_back(line);
        tail_bytes += line.size();
        while (tail.size() > max_lines || tail_bytes > max_bytes) {
            tail_bytes -= tail.front().size();
            tail.pop_front();
        }
    }

    // Contiguous head first; once full every subsequent line feeds a bounded tail deque (the dropped middle).
    void add_head_tail(const std::string& line, const RetainHeadTail& p) {
        if (!head_full) {
            if (head.size() >= p.head_lines || head_bytes + line.size() > p.head_bytes) {
                head_full = true; // fall through to the tail
            } else {
                head.push_back(line);
                head_bytes += line.size();
                return;
            }
        }
        add_tail(line, p.tail_lines, p.tail_bytes);
    }

    const AccumulationPolicy& policy;
    std::vector<std::string> head; // RetainAll/RetainHead result, or the head prefix of RetainHeadTail
    std::deque<std::string> tail;  // RetainTail result, or the tail suffix of RetainHeadTail
    std::size_t head_bytes = 0;
    std::size_t tail_bytes = 0;
    bool head_full = false;
};

} // namespace

CmdOutput run_application(const std::string& name, std::vector<std::string> args, RunOptions opts) {

    // search_path requires basename and not a full path
    boost::filesystem::path path = name;

    if (path.is_relative()) {
        path = everest_boost_process::search_path(name);
    }

    if (path.empty()) {
        EVLOG_debug << fmt::format("The application '{}' could not be found", name);
        return CmdOutput{"", {}, 1};
    }

    everest_boost_process::ipstream stream;
    const bool kill_child_on_parent_death = opts.kill_child_on_parent_death;
    auto on_child_setup = [kill_child_on_parent_death](auto&) {
#ifdef __linux__
        if (kill_child_on_parent_death) {
            ::prctl(PR_SET_PDEATHSIG, SIGKILL);
        }
#else
        (void)kill_child_on_parent_death;
#endif
    };
    everest_boost_process::child cmd(path, everest_boost_process::args(args), everest_boost_process::std_out > stream,
                                     everest_boost_process::extend::on_exec_setup(on_child_setup));

    // On cancel: SIGTERM, then SIGKILL after the terminate_grace, which closes stdout and unblocks the read loop.
    const auto terminate_grace = opts.terminate_grace;
    std::atomic<bool> read_finished{false};
    std::thread stop_watcher;
    if (opts.stop_requested != nullptr) {
        auto stop_requested = opts.stop_requested;
        stop_watcher = std::thread([&, stop_requested] {
            using namespace std::chrono_literals;
            try {
                while (!read_finished.load()) {
                    if (!stop_requested->load()) {
                        std::this_thread::sleep_for(100ms);
                        continue;
                    }
                    std::error_code ec;
                    const auto pid = cmd.id();
                    if (pid > 0 && ::kill(pid, SIGTERM) != 0) {
                        EVLOG_warning << fmt::format("run_application: failed to send SIGTERM to {}: {}", pid,
                                                     std::error_code(errno, std::generic_category()).message());
                    }
                    const auto deadline = std::chrono::steady_clock::now() + terminate_grace;
                    while (cmd.running(ec) && std::chrono::steady_clock::now() < deadline) {
                        std::this_thread::sleep_for(50ms);
                    }
                    if (cmd.running(ec)) {
                        cmd.terminate(ec); // SIGKILL
                        cmd.wait(ec);      // reap so exit_code() reflects the kill signal
                    }
                    return;
                }
            } catch (const std::exception& e) {
                EVLOG_error << "run_application stop watcher failed: " << e.what();
            }
        });
    }

    Accumulator accumulator{opts.accumulation};
    auto condition = CmdControl::Continue;
    {
        struct WatcherGuard {
            std::atomic<bool>& read_finished;
            std::thread& watcher;
            ~WatcherGuard() {
                read_finished.store(true);
                if (watcher.joinable()) {
                    watcher.join();
                }
            }
        } watcher_guard{read_finished, stop_watcher};

        std::string temp;
        while (std::getline(stream, temp)) {
            accumulator.add(temp);
            if (opts.callback != nullptr) {
                condition = opts.callback(temp);
            }
            if (condition != CmdControl::Continue) {
                break;
            }
        }
    }
    if (condition == CmdControl::Continue) {
        cmd.wait();
    } else {
        cmd.terminate();
    }

    CmdOutput result;
    result.split_output = accumulator.take_retained();
    for (const auto& line : result.split_output) {
        result.output += line + "\n";
    }
    result.exit_code = cmd.exit_code();
    return result;
}

CmdOutput run_application(const std::string& name, std::vector<std::string> args,
                          const std::function<CmdControl(const std::string& output_line)> output_callback) {
    RunOptions opts;
    opts.callback = output_callback;
    return run_application(name, std::move(args), std::move(opts));
}

} // namespace everest::run_application
