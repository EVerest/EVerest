// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_all.hpp>

#include <array>
#include <cstdlib>
#include <fstream>
#include <istream>
#include <sstream>
#include <string>
#include <utility>

#include <sched.h>
#include <sys/wait.h>
#include <unistd.h>

#include <utils/capabilities.hpp>

namespace {
constexpr int EXIT_USER_NAMESPACE_UNAVAILABLE = 77;
constexpr int EXIT_USER_NAMESPACE_WITHOUT_CAPABILITIES = 78;
constexpr const char* NO_CAPABILITIES = "0000000000000000";
constexpr std::size_t READ_BUFFER_SIZE = 4096;

std::string capability_set(std::istream& status, const std::string& name) {
    std::string line;
    while (std::getline(status, line)) {
        if (line.rfind(name + ":", 0) == 0) {
            return line.substr(line.find_first_not_of(" \t", name.size() + 1));
        }
    }
    return {};
}

std::string capability_set(const std::string& name) {
    std::ifstream status("/proc/self/status");
    return capability_set(status, name);
}

/// Runs /bin/cat /proc/self/status as child of a process that raised its ambient capabilities in a new user
/// namespace, where it is not root but has all capabilities permitted, like a module binary with file capabilities.
std::pair<int, std::string> child_status_after_raise() {
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
        if (Everest::raise_ambient_capabilities()) {
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
} // namespace

SCENARIO("Raising ambient capabilities", "[capabilities]") {
    GIVEN("A process without file capabilities") {
        const auto permitted = capability_set("CapPrm");
        const auto ambient = capability_set("CapAmb");

        WHEN("Raising the ambient capabilities") {
            const auto error = Everest::raise_ambient_capabilities();

            THEN("It succeeds") {
                CHECK_FALSE(error.has_value());
            }
            THEN("The ambient set stays empty if nothing is permitted") {
                if (geteuid() != 0 and permitted == NO_CAPABILITIES) {
                    CHECK(capability_set("CapAmb") == ambient);
                    CHECK(ambient == NO_CAPABILITIES);
                }
            }
        }
    }
}

SCENARIO("Passing ambient capabilities on to child processes", "[capabilities]") {
    GIVEN("A non-root process with permitted capabilities") {
        WHEN("It raises its ambient capabilities and executes a program without file capabilities") {
            const auto [exit_code, output] = child_status_after_raise();
            if (exit_code == EXIT_USER_NAMESPACE_UNAVAILABLE) {
                SKIP("Unprivileged user namespaces are not available");
            }
            if (exit_code == EXIT_USER_NAMESPACE_WITHOUT_CAPABILITIES) {
                SKIP("Unprivileged user namespaces do not grant capabilities");
            }
            REQUIRE(exit_code == EXIT_SUCCESS);

            THEN("The program keeps the capabilities") {
                std::istringstream ambient_status(output);
                std::istringstream effective_status(output);
                const auto child_ambient = capability_set(ambient_status, "CapAmb");
                CHECK(child_ambient != NO_CAPABILITIES);
                CHECK(capability_set(effective_status, "CapEff") == child_ambient);
            }
        }
    }
}
