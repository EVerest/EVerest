// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <everest/run_application/run_application.hpp>

#ifdef __linux__
#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace everest::run_application;

TEST(RunApplication, StopRequestedTerminatesSilentChild) {
    auto stop = std::make_shared<std::atomic_bool>(false);

    std::thread setter([stop] {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        *stop = true;
    });

    RunOptions opts;
    opts.stop_requested = stop;

    const auto start = std::chrono::steady_clock::now();
    run_application("/usr/bin/sleep", {"60"}, opts);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    setter.join();

    EXPECT_LT(elapsed, std::chrono::seconds(5));
}

TEST(RunApplication, StopRequestedEscalatesToSigkill) {
    // The child ignores SIGTERM, so the 2 s grace must elapse and SIGKILL must be used. This
    // exercises the escalation path that a SIGTERM-responsive child would skip.
    auto stop = std::make_shared<std::atomic_bool>(false);

    std::thread setter([stop] {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        *stop = true;
    });

    RunOptions opts;
    opts.stop_requested = stop;

    const auto start = std::chrono::steady_clock::now();
    run_application("/usr/bin/python3",
                    {"-c", "import signal, time; signal.signal(signal.SIGTERM, signal.SIG_IGN); time.sleep(60)"}, opts);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    setter.join();

    // SIGTERM is ignored, so termination cannot happen before the ~2 s grace (proving SIGTERM alone
    // did not kill it), and must happen well before the child's own 60 s sleep.
    EXPECT_GE(elapsed, std::chrono::milliseconds(1900));
    EXPECT_LT(elapsed, std::chrono::seconds(5));
}

TEST(RunApplication, NullStopRequestedRunsToCompletion) {
    const auto result = run_application("/usr/bin/echo", {"hello"});

    EXPECT_NE(result.output.find("hello"), std::string::npos);
    EXPECT_EQ(result.exit_code, 0);
}

TEST(RunApplication, ArmedStopFlagNeverSetRunsToCompletion) {
    // A non-null flag that is never set spawns the watcher but lets the child finish normally,
    // exercising a clean watcher spawn + join on the completion path (the nullptr test does not
    // create a watcher at all).
    auto stop = std::make_shared<std::atomic_bool>(false);

    RunOptions opts;
    opts.stop_requested = stop;

    const auto result = run_application("/usr/bin/echo", {"hello"}, opts);

    EXPECT_NE(result.output.find("hello"), std::string::npos);
    EXPECT_EQ(result.exit_code, 0);
}

TEST(RunApplication, AccumulateOutputFalseStreamsWithoutRetaining) {
    // RetainNone must not buffer stdout in CmdOutput (the unbounded-growth case for long-lived
    // children), but the per-line callback must still fire and the exit code stay valid.
    std::vector<std::string> streamed;

    RunOptions opts;
    opts.accumulation = RetainNone{};
    opts.callback = [&streamed](const std::string& line) {
        streamed.push_back(line);
        return CmdControl::Continue;
    };

    const auto result = run_application("/usr/bin/echo", {"hello"}, opts);

    EXPECT_TRUE(result.output.empty());
    EXPECT_TRUE(result.split_output.empty());
    ASSERT_EQ(streamed.size(), 1u);
    EXPECT_EQ(streamed[0], "hello");
    EXPECT_EQ(result.exit_code, 0);
}

#ifdef __linux__
namespace {

// Return the pid of a running process whose cmdline contains needle, or -1 if none.
pid_t find_process_with_arg(const std::string& needle) {
    DIR* proc = opendir("/proc");
    if (proc == nullptr) {
        return -1;
    }
    pid_t found = -1;
    while (dirent* entry = readdir(proc)) {
        char* end = nullptr;
        const long pid = std::strtol(entry->d_name, &end, 10);
        if (end == entry->d_name || *end != '\0') {
            continue; // not a pid directory
        }
        std::ifstream cmdline(std::string("/proc/") + entry->d_name + "/cmdline", std::ios::binary);
        if (!cmdline) {
            continue;
        }
        std::string content((std::istreambuf_iterator<char>(cmdline)), std::istreambuf_iterator<char>());
        std::replace(content.begin(), content.end(), '\0', ' '); // cmdline is NUL-separated
        if (content.find(needle) != std::string::npos) {
            found = static_cast<pid_t>(pid);
            break;
        }
    }
    closedir(proc);
    return found;
}

bool wait_until(const std::function<bool()>& pred, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return pred();
}

} // namespace

TEST(RunApplication, KillChildOnParentDeathReapsChildWhenParentDies) {
    // A unique sleep duration lets us pinpoint exactly this grandchild in /proc.
    const std::string unique_arg = "314159";
    const std::string needle = "/usr/bin/sleep " + unique_arg;
    ASSERT_EQ(find_process_with_arg(needle), -1) << "a stale sleeper is present before the test";

    const pid_t child = fork();
    ASSERT_GE(child, 0) << "fork failed";
    if (child == 0) {
        // Intermediate child: spawn a long-lived grandchild that must die with us via PR_SET_PDEATHSIG.
        RunOptions opts;
        opts.accumulation = RetainNone{};
        opts.kill_child_on_parent_death = true;
        run_application("/usr/bin/sleep", {unique_arg}, opts);
        _exit(0);
    }

    ASSERT_TRUE(wait_until([&] { return find_process_with_arg(needle) > 0; }, std::chrono::seconds(5)))
        << "grandchild sleeper never started";

    // Kill the intermediate child abruptly — no destructors, no clean shutdown.
    ASSERT_EQ(kill(child, SIGKILL), 0);
    int status = 0;
    ASSERT_EQ(waitpid(child, &status, 0), child);

    const bool reaped = wait_until([&] { return find_process_with_arg(needle) == -1; }, std::chrono::seconds(5));

    // Safety net: never leak the sleeper into the test host even if the assertion fails.
    const pid_t leaked = find_process_with_arg(needle);
    if (leaked > 0) {
        kill(leaked, SIGKILL);
    }

    EXPECT_TRUE(reaped) << "grandchild outlived its parent — PR_SET_PDEATHSIG did not fire";
}
#endif

namespace {
constexpr std::size_t kUnbounded = std::numeric_limits<std::size_t>::max();

// Assert the CmdOutput invariant: output == concat(split_output[i] + "\n").
void expect_output_matches_split(const CmdOutput& result) {
    std::string expected;
    for (const auto& line : result.split_output) {
        expected += line + "\n";
    }
    EXPECT_EQ(result.output, expected);
}
} // namespace

TEST(RunApplication, RetainAllViaOptionsMatchesFullBuffer) {
    RunOptions opts;
    opts.accumulation = RetainAll{};

    const auto result = run_application("/usr/bin/seq", {"1", "5"}, opts);

    EXPECT_EQ(result.exit_code, 0);
    ASSERT_EQ(result.split_output.size(), 5u);
    EXPECT_EQ(result.split_output.front(), "1");
    EXPECT_EQ(result.split_output.back(), "5");
    EXPECT_EQ(result.output, "1\n2\n3\n4\n5\n");
    expect_output_matches_split(result);
}

TEST(RunApplication, RetainNoneViaOptionsStreamsButRetainsNothing) {
    std::vector<std::string> streamed;
    RunOptions opts;
    opts.accumulation = RetainNone{};
    opts.callback = [&streamed](const std::string& line) {
        streamed.push_back(line);
        return CmdControl::Continue;
    };

    const auto result = run_application("/usr/bin/seq", {"1", "5"}, opts);

    EXPECT_TRUE(result.output.empty());
    EXPECT_TRUE(result.split_output.empty());
    EXPECT_EQ(result.exit_code, 0);
    ASSERT_EQ(streamed.size(), 5u);
    EXPECT_EQ(streamed.front(), "1");
    EXPECT_EQ(streamed.back(), "5");
}

TEST(RunApplication, RetainHeadLineCapBinds) {
    RunOptions opts;
    opts.accumulation = RetainHead{/*max_lines=*/10, /*max_bytes=*/kUnbounded};

    const auto result = run_application("/usr/bin/seq", {"1", "100"}, opts);

    EXPECT_EQ(result.exit_code, 0);
    ASSERT_EQ(result.split_output.size(), 10u);
    EXPECT_EQ(result.split_output.front(), "1");
    EXPECT_EQ(result.split_output.back(), "10");
    expect_output_matches_split(result);
}

TEST(RunApplication, RetainHeadByteCapBindsBeforeLineCap) {
    // 100 lines of exactly 10 bytes each. max_bytes=25 admits only 2 lines (20 <= 25, 30 > 25),
    // even though max_lines is effectively unbounded.
    RunOptions opts;
    opts.accumulation = RetainHead{/*max_lines=*/kUnbounded, /*max_bytes=*/25};

    const auto result = run_application("/usr/bin/python3", {"-c", "for i in range(100): print('x'*10)"}, opts);

    EXPECT_EQ(result.exit_code, 0);
    ASSERT_EQ(result.split_output.size(), 2u);
    EXPECT_EQ(result.split_output[0], "xxxxxxxxxx");
    EXPECT_EQ(result.split_output[1], "xxxxxxxxxx");
    expect_output_matches_split(result);
}

TEST(RunApplication, RetainTailLineCapKeepsNewest) {
    RunOptions opts;
    opts.accumulation = RetainTail{/*max_lines=*/10, /*max_bytes=*/kUnbounded};

    const auto result = run_application("/usr/bin/seq", {"1", "100"}, opts);

    EXPECT_EQ(result.exit_code, 0);
    ASSERT_EQ(result.split_output.size(), 10u);
    EXPECT_EQ(result.split_output.front(), "91");
    EXPECT_EQ(result.split_output.back(), "100");
    expect_output_matches_split(result);
}

TEST(RunApplication, RetainTailSingleLineLargerThanByteCapEvicted) {
    // One line of 10 bytes, byte cap of 5: appended then immediately evicted, keeping memory bounded.
    RunOptions opts;
    opts.accumulation = RetainTail{/*max_lines=*/kUnbounded, /*max_bytes=*/5};

    const auto result = run_application("/usr/bin/python3", {"-c", "print('x'*10)"}, opts);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_TRUE(result.split_output.empty());
    EXPECT_TRUE(result.output.empty());
}

TEST(RunApplication, RetainHeadTailKeepsPrefixAndSuffix) {
    RunOptions opts;
    opts.accumulation = RetainHeadTail{/*head_lines=*/3, /*head_bytes=*/kUnbounded,
                                       /*tail_lines=*/3, /*tail_bytes=*/kUnbounded};

    const auto result = run_application("/usr/bin/seq", {"1", "100"}, opts);

    EXPECT_EQ(result.exit_code, 0);
    ASSERT_EQ(result.split_output.size(), 6u);
    const std::vector<std::string> expected{"1", "2", "3", "98", "99", "100"};
    EXPECT_EQ(result.split_output, expected);
    EXPECT_EQ(result.output, "1\n2\n3\n98\n99\n100\n");
    expect_output_matches_split(result);
}

TEST(RunApplication, TerminateGraceShortenPathIsFasterThanDefault) {
    // A SIGTERM-ignoring child forces the escalation-to-SIGKILL path. With a short grace the
    // kill must land well before the default 2 s grace would allow.
    auto stop = std::make_shared<std::atomic_bool>(false);
    std::thread setter([stop] {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        *stop = true;
    });

    RunOptions opts;
    opts.stop_requested = stop;
    opts.terminate_grace = std::chrono::milliseconds(300);

    const auto start = std::chrono::steady_clock::now();
    run_application("/usr/bin/python3",
                    {"-c", "import signal, time; signal.signal(signal.SIGTERM, signal.SIG_IGN); time.sleep(60)"}, opts);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    setter.join();

    // 200 ms until stop is set + 300 ms grace ~= 0.5 s; comfortably under the 2 s default grace path.
    EXPECT_LT(elapsed, std::chrono::milliseconds(1500));
}
