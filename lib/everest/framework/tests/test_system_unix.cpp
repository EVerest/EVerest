// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_all.hpp>

#include <array>
#include <cstdlib>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <sched.h>
#include <sys/wait.h>
#include <unistd.h>

#include <system_unix.hpp>

namespace {
constexpr int EXIT_USER_NAMESPACE_UNAVAILABLE = 77;
constexpr int EXIT_USER_NAMESPACE_WITHOUT_CAPABILITIES = 78;
constexpr std::size_t READ_BUFFER_SIZE = 4096;
// CAP_NET_RAW is capability 13, CAP_SYS_TIME is capability 25
constexpr const char* CAP_NET_RAW_ONLY = "0000000000002000";
constexpr const char* CAP_NET_RAW_AND_CAP_SYS_TIME = "0000000002002000";

std::string capability_set(const std::string& status_output, const std::string& name) {
    std::istringstream status(status_output);
    std::string line;
    while (std::getline(status, line)) {
        if (line.rfind(name + ":", 0) == 0) {
            return line.substr(line.find_first_not_of(" \t", name.size() + 1));
        }
    }
    return {};
}

/// Runs /bin/cat /proc/self/status as child of a process that called set_caps() in a new user namespace, where it
/// is not root but has all capabilities permitted, like the manager starting a module
std::pair<int, std::string> child_status_after_set_caps(const std::vector<std::string>& capabilities) {
    std::array<int, 2> pipe_fds{};
    REQUIRE(pipe(pipe_fds.data()) == 0);

    const auto pid = fork();
    REQUIRE(pid >= 0);
    if (pid == 0) {
        close(pipe_fds.at(0));
        if (unshare(CLONE_NEWUSER) != 0) {
            _exit(EXIT_USER_NAMESPACE_UNAVAILABLE);
        }
        // needs CAP_SYS_ADMIN in the new namespace, which the AppArmor user namespace restriction, e.g. of
        // Ubuntu 24.04, denies although unshare(CLONE_NEWUSER) succeeds
        if (unshare(CLONE_NEWUTS) != 0) {
            _exit(EXIT_USER_NAMESPACE_WITHOUT_CAPABILITIES);
        }
        if (not Everest::system::set_caps(capabilities).empty()) {
            _exit(EXIT_FAILURE);
        }
        dup2(pipe_fds.at(1), STDOUT_FILENO);
        execl("/bin/cat", "cat", "/proc/self/status", nullptr);
        _exit(EXIT_FAILURE);
    }

    close(pipe_fds.at(1));
    std::string output;
    std::array<char, READ_BUFFER_SIZE> buffer{};
    ssize_t bytes_read = 0;
    while ((bytes_read = read(pipe_fds.at(0), buffer.data(), buffer.size())) > 0) {
        output.append(buffer.data(), static_cast<std::size_t>(bytes_read));
    }
    close(pipe_fds.at(0));

    int status = 0;
    waitpid(pid, &status, 0);
    return {WIFEXITED(status) ? WEXITSTATUS(status) : -1, output};
}

void skip_without_user_namespace(int exit_code) {
    if (exit_code == EXIT_USER_NAMESPACE_UNAVAILABLE) {
        SKIP("Unprivileged user namespaces are not available");
    }
    if (exit_code == EXIT_USER_NAMESPACE_WITHOUT_CAPABILITIES) {
        SKIP("Unprivileged user namespaces do not grant capabilities");
    }
}
} // namespace

SCENARIO("Setting capabilities for a module process", "[capabilities]") {
    GIVEN("A process with permitted capabilities") {
        WHEN("It sets a single capability and executes a program without file capabilities") {
            const auto [exit_code, output] = child_status_after_set_caps({"CAP_NET_RAW"});
            skip_without_user_namespace(exit_code);
            REQUIRE(exit_code == EXIT_SUCCESS);

            THEN("The program gets exactly this capability") {
                CHECK(capability_set(output, "CapAmb") == CAP_NET_RAW_ONLY);
                CHECK(capability_set(output, "CapEff") == CAP_NET_RAW_ONLY);
            }
        }

        WHEN("It sets several capabilities and executes a program without file capabilities") {
            const auto [exit_code, output] = child_status_after_set_caps({"CAP_NET_RAW", "CAP_SYS_TIME"});
            skip_without_user_namespace(exit_code);
            REQUIRE(exit_code == EXIT_SUCCESS);

            THEN("The program gets exactly these capabilities") {
                CHECK(capability_set(output, "CapAmb") == CAP_NET_RAW_AND_CAP_SYS_TIME);
                CHECK(capability_set(output, "CapEff") == CAP_NET_RAW_AND_CAP_SYS_TIME);
            }
        }
    }
}
