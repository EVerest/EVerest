// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Contributors to EVerest

#include <gtest/gtest.h>

#include <cctype>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <limits.h>
#include <ostream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

#ifdef __linux__
#include <sched.h>
#endif

namespace {

struct ProbeCase {
    std::string name;
    std::string executable;
    std::vector<std::string> arguments;
    int expected_exit{0};
};

void PrintTo(const ProbeCase& probe, std::ostream* output) {
    *output << probe.name;
}

struct ProbeResult {
    bool timed_out{false};
    int exit_code{-1};
    int signal{0};
};

std::filesystem::path probe_directory;

std::filesystem::path locate_probe(const std::string& executable) {
    return probe_directory / executable;
}

bool use_extended_matrix() {
    const char* enabled = std::getenv("EVEREST_THREAD_POOL_PROBE_STRESS");
    return enabled != nullptr && std::string(enabled) == "1";
}

void pin_to_first_allowed_cpu() {
#ifdef __linux__
    cpu_set_t available;
    if (sched_getaffinity(0, sizeof(available), &available) != 0) {
        _exit(126);
    }
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
        if (CPU_ISSET(cpu, &available)) {
            cpu_set_t selected;
            CPU_ZERO(&selected);
            CPU_SET(cpu, &selected);
            if (sched_setaffinity(0, sizeof(selected), &selected) != 0) {
                _exit(126);
            }
            return;
        }
    }
#endif
    _exit(126);
}

ProbeResult run_probe(const ProbeCase& probe, bool pin_to_cpu = false) {
    const auto path = locate_probe(probe.executable);
    if (!std::filesystem::is_regular_file(path)) {
        return {false, 127, 0};
    }

    const pid_t child = fork();
    if (child == 0) {
        if (pin_to_cpu) {
            pin_to_first_allowed_cpu();
        }
        std::vector<std::string> command;
        command.reserve(probe.arguments.size() + 1);
        command.push_back(path.string());
        command.insert(command.end(), probe.arguments.begin(), probe.arguments.end());
        std::vector<char*> argv;
        argv.reserve(command.size() + 1);
        for (auto& value : command) {
            argv.push_back(value.data());
        }
        argv.push_back(nullptr);
        execv(argv.front(), argv.data());
        _exit(127);
    }
    if (child < 0) {
        return {false, 127, 0};
    }

    int status = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (true) {
        const pid_t waited = waitpid(child, &status, WNOHANG);
        if (waited == child) {
            break;
        }
        if (waited < 0) {
            if (errno == EINTR) {
                continue;
            }
            return {false, 127, 0};
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            (void)kill(child, SIGKILL);
            while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
            }
            return {true, -1, 0};
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    if (WIFEXITED(status)) {
        return {false, WEXITSTATUS(status), 0};
    }
    if (WIFSIGNALED(status)) {
        return {false, -1, WTERMSIG(status)};
    }
    return {false, 127, 0};
}

void expect_probe_result(const ProbeCase& probe, bool pin_to_cpu = false) {
    const ProbeResult result = run_probe(probe, pin_to_cpu);
    EXPECT_FALSE(result.timed_out) << probe.name;
    EXPECT_EQ(result.signal, 0) << probe.name;
    EXPECT_EQ(result.exit_code, probe.expected_exit) << probe.name;
}

std::vector<ProbeCase> default_matrix() {
    std::vector<ProbeCase> probes;
    const auto add = [&](std::string name, std::string executable, std::vector<std::string> arguments,
                         int expected_exit = 0) {
        probes.push_back({std::move(name), std::move(executable), std::move(arguments), expected_exit});
    };

    for (const std::string mode : {"constructor", "submit", "future"}) {
        for (int ordinal = 1; ordinal <= 4; ++ordinal) {
            add("faults_" + mode + "_" + std::to_string(ordinal), "thread_pool_faults_probe",
                {mode, std::to_string(ordinal)});
        }
    }
    add("faults_retirement", "thread_pool_faults_probe", {"retirement", "1"});
    add("faults_cold_greedy", "thread_pool_faults_probe", {"cold-greedy", "1"});
    add("faults_cold_latency", "thread_pool_faults_probe", {"cold-latency", "1"});
    add("faults_bounded_control", "thread_pool_faults_probe", {"bounded-enqueue", "0"});
    add("faults_bounded_failure", "thread_pool_faults_probe", {"bounded-enqueue", "1"});
    add("faults_chaos", "thread_pool_faults_probe", {"chaos", "31", "0", "2", "50", "12345"});

    add("stress_constructor", "thread_pool_stress_fault_probe", {"constructor-failures", "1"});
    add("stress_supervisor", "thread_pool_stress_fault_probe", {"resource-supervisor"});
    add("stress_submit", "thread_pool_stress_fault_probe", {"resource-submit"});
    add("stress_cold_drain", "thread_pool_stress_fault_probe", {"cold-drain-retry", "1"});
    add("stress_drain_chain", "thread_pool_stress_fault_probe", {"drain-chain-failures", "1", "0"});

    for (const std::string mode :
         {"chain-control", "chain-failure", "serial-latency", "serial-greedy", "serial-control"}) {
        add("shutdown_" + mode, "thread_pool_shutdown_probe", {mode});
    }

    for (const std::string fault : {"control", "thread", "node", "state"}) {
        for (const std::string minimum : {"0", "1"}) {
            add("cold_chain_" + fault + "_" + minimum, "thread_pool_cold_chain_probe", {fault, minimum});
        }
    }

    for (const std::string policy : {"latency", "fixed"}) {
        for (const std::string minimum : {"0", "1"}) {
            add("drain_" + policy + "_" + minimum + "_control", "thread_pool_drain_bursts_probe",
                {policy, minimum, "control", "0"});
            for (const std::string fault : {"thread", "node", "state"}) {
                add("drain_" + policy + "_" + minimum + "_" + fault, "thread_pool_drain_bursts_probe",
                    {policy, minimum, fault, "1"});
            }
        }
    }

    for (const std::string policy : {"latency", "fixed"}) {
        for (const std::string state : {"queued", "completed"}) {
            add("late_" + policy + "_" + state + "_cold", "thread_pool_late_submit_probe",
                {policy, "2", "0", "1", state});
            add("late_" + policy + "_" + state + "_warm", "thread_pool_late_submit_probe",
                {policy, "4", "1", "1", state});
        }
    }

    for (const std::string mode : {"supervisor-control", "supervisor-failure", "worker-control", "worker-failure"}) {
        add("allocation_" + mode, "thread_pool_allocation_probe", {mode, "1"});
    }
    add("allocation_destructor_control", "thread_pool_allocation_probe", {"destructor-control"});
    add("allocation_destructor_failure", "thread_pool_allocation_probe", {"destructor-failure"});

    for (const std::string mode :
         {"greedy-control", "greedy-failure", "fixed-control", "fixed-failure", "latency-failure",
          "cold-greedy-control", "cold-greedy-failure", "cold-latency-failure", "unbounded-control",
          "larger-queue-control", "spare-worker-control"}) {
        add("liveness_" + mode, "thread_pool_liveness_probe", {mode});
    }
    add("liveness_bounded_stall", "thread_pool_liveness_probe", {"bounded-stall"}, 3);
    return probes;
}

std::vector<ProbeCase> extended_matrix() {
    auto probes = default_matrix();
    probes.push_back({"extended_allocation_supervisor_2", "thread_pool_allocation_probe", {"supervisor-failure", "2"}});
    for (int ordinal = 5; ordinal <= 11; ++ordinal) {
        probes.push_back({"extended_constructor_" + std::to_string(ordinal),
                          "thread_pool_faults_probe",
                          {"constructor", std::to_string(ordinal)}});
    }
    for (const std::string mode : {"submit", "future"}) {
        for (int ordinal = 5; ordinal <= 20; ++ordinal) {
            probes.push_back({"extended_" + mode + "_" + std::to_string(ordinal),
                              "thread_pool_faults_probe",
                              {mode, std::to_string(ordinal)}});
        }
    }
    for (const std::string failures : {"10", "100"}) {
        probes.push_back({"extended_retirement_" + failures, "thread_pool_faults_probe", {"retirement", failures}});
    }
    for (const std::string period : {"7", "31", "127"}) {
        for (const std::string capacity : {"0", "1", "4"}) {
            probes.push_back({"extended_chaos_p" + period + "_q" + capacity,
                              "thread_pool_faults_probe",
                              {"chaos", period, capacity, "2", "50", "12345"}});
        }
    }
    probes.push_back(
        {"extended_stress_drain_q1", "thread_pool_stress_fault_probe", {"drain-chain-failures", "1", "1"}});
    probes.push_back(
        {"extended_stress_drain_q4", "thread_pool_stress_fault_probe", {"drain-chain-failures", "1", "4"}});
    for (const std::string policy : {"latency", "fixed"}) {
        for (const std::string minimum : {"0", "1"}) {
            for (const std::string fault : {"thread", "node", "state"}) {
                for (const std::string count : {"5", "50"}) {
                    probes.push_back({"extended_drain_" + policy + "_" + minimum + "_" + fault + "_" + count,
                                      "thread_pool_drain_bursts_probe",
                                      {policy, minimum, fault, count}});
                }
            }
        }
    }
    for (const std::string policy : {"latency", "fixed"}) {
        for (const std::string minimum : {"0", "1"}) {
            for (const std::string state : {"queued", "completed"}) {
                probes.push_back({"extended_late_" + policy + "_min" + minimum + "_" + state,
                                  "thread_pool_late_submit_probe",
                                  {policy, "8", minimum, "7", state}});
            }
        }
    }
    return probes;
}

class ThreadPoolScalingProbeDriverTest : public ::testing::TestWithParam<ProbeCase> {};

TEST_P(ThreadPoolScalingProbeDriverTest, ExitsAsExpected) {
    expect_probe_result(GetParam());
}

TEST(ThreadPoolScalingProbeDriver, UsesAnAllowedCpuForAffinityCoverage) {
    expect_probe_result({"affinity_chaos", "thread_pool_faults_probe", {"chaos", "31", "0", "2", "50", "12345"}}, true);
    expect_probe_result({"affinity_shutdown", "thread_pool_shutdown_probe", {"chain-control"}}, true);
}

TEST(ThreadPoolScalingProbeDriver, DISABLED_ExtendedMatrix) {
    ASSERT_TRUE(use_extended_matrix());
    for (const auto& probe : extended_matrix()) {
        expect_probe_result(probe);
    }
}

INSTANTIATE_TEST_SUITE_P(DefaultMatrix, ThreadPoolScalingProbeDriverTest, ::testing::ValuesIn(default_matrix()),
                         [](const ::testing::TestParamInfo<ProbeCase>& info) {
                             std::string name = info.param.name;
                             for (char& character : name) {
                                 if (!std::isalnum(static_cast<unsigned char>(character))) {
                                     character = '_';
                                 }
                             }
                             return name;
                         });

class ProbeDirectoryEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        if (const char* runfiles = std::getenv("TEST_SRCDIR")) {
            const std::filesystem::path root(runfiles);
            if (const char* workspace = std::getenv("TEST_WORKSPACE")) {
                const auto candidate = root / workspace / "lib/everest/util";
                if (std::filesystem::is_directory(candidate)) {
                    probe_directory = candidate;
                    return;
                }
            }
            const auto bzlmod_candidate = root / "_main/lib/everest/util";
            if (std::filesystem::is_directory(bzlmod_candidate)) {
                probe_directory = bzlmod_candidate;
                return;
            }
        }

        char executable[PATH_MAX];
        const ssize_t size = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
        if (size > 0) {
            executable[size] = '\0';
            probe_directory = std::filesystem::path(executable).parent_path();
        }
    }
};

::testing::Environment* const probe_directory_environment =
    ::testing::AddGlobalTestEnvironment(new ProbeDirectoryEnvironment);

} // namespace
