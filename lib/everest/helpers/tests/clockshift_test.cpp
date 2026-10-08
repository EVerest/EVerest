// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

#include <sys/wait.h>
#include <unistd.h>

namespace {

namespace fs = std::filesystem;
using namespace std::chrono_literals;

constexpr const char* PROBE_NAME = "clockshift-probe";

struct ClockReadings {
    std::chrono::nanoseconds steady;
    std::chrono::nanoseconds monotonic_raw;
};

struct ProbeResult {
    std::chrono::duration<double> steady_elapsed;
    std::chrono::duration<double> monotonic_raw_elapsed;
};

std::optional<std::string> read_line(int fd) {
    std::string line;
    char c = 0;
    while (read(fd, &c, 1) == 1) {
        if (c == '\n') {
            return line;
        }
        line += c;
    }
    return std::nullopt;
}

std::optional<ClockReadings> read_clocks(int fd) {
    const auto line = read_line(fd);
    if (!line) {
        return std::nullopt;
    }
    std::int64_t steady = 0;
    std::int64_t monotonic_raw = 0;
    std::istringstream fields(*line);
    if (!(fields >> steady >> monotonic_raw)) {
        return std::nullopt;
    }
    return ClockReadings{std::chrono::nanoseconds(steady), std::chrono::nanoseconds(monotonic_raw)};
}

class ClockshiftTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto* test = ::testing::UnitTest::GetInstance()->current_test_info();
        offset_file = fs::temp_directory_path() / ("clockshift_" + std::to_string(getpid()) + "_" + test->name());
        std::ofstream(offset_file, std::ios::binary | std::ios::trunc).write(encode(0ns).data(), sizeof(std::int64_t));
    }

    void TearDown() override {
        fs::remove(offset_file);
    }

    // Overwrites in place: the probe has the file mapped, so it must never be truncated while it runs.
    void set_offset(std::chrono::nanoseconds offset) {
        std::fstream file(offset_file, std::ios::binary | std::ios::in | std::ios::out);
        file.write(encode(offset).data(), sizeof(std::int64_t));
    }

    // Starts the probe as argv[0], sets the offset between its two readings and returns the elapsed times.
    std::optional<ProbeResult> run_probe(const std::string& argv0, std::chrono::nanoseconds offset) {
        std::array<int, 2> to_probe{};
        std::array<int, 2> from_probe{};
        if (pipe(to_probe.data()) != 0 || pipe(from_probe.data()) != 0) {
            return std::nullopt;
        }

        const pid_t pid = fork();
        if (pid == 0) {
            dup2(to_probe[0], STDIN_FILENO);
            dup2(from_probe[1], STDOUT_FILENO);
            for (const int fd : {to_probe[0], to_probe[1], from_probe[0], from_probe[1]}) {
                close(fd);
            }
            setenv("LD_PRELOAD", CLOCKSHIFT_LIB, 1);
            setenv("CLOCKSHIFT_FILE", offset_file.c_str(), 1);
            setenv("CLOCKSHIFT_ONLY", PROBE_NAME, 1);
            execl(CLOCKSHIFT_PROBE, argv0.c_str(), static_cast<char*>(nullptr));
            _exit(127);
        }
        close(to_probe[0]);
        close(from_probe[1]);

        const auto before = read_clocks(from_probe[0]);
        set_offset(offset);
        const bool released = write(to_probe[1], "\n", 1) == 1;
        const auto after = read_clocks(from_probe[0]);
        close(to_probe[1]);
        close(from_probe[0]);

        int status = 0;
        waitpid(pid, &status, 0);
        if (!before || !after || !released || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            return std::nullopt;
        }
        return ProbeResult{after->steady - before->steady, after->monotonic_raw - before->monotonic_raw};
    }

    fs::path offset_file;

private:
    static std::array<char, sizeof(std::int64_t)> encode(std::chrono::nanoseconds offset) {
        const std::int64_t value = offset.count();
        std::array<char, sizeof(std::int64_t)> bytes{};
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            bytes[i] = static_cast<char>((value >> (8 * i)) & 0xff);
        }
        return bytes;
    }
};

TEST_F(ClockshiftTest, advances_steady_clock_of_matching_process) {
    const auto result = run_probe(PROBE_NAME, 1000s);

    ASSERT_TRUE(result.has_value());
    EXPECT_GE(result->steady_elapsed.count(), 1000.0);
    EXPECT_LT(result->steady_elapsed.count(), 1010.0);
}

TEST_F(ClockshiftTest, leaves_monotonic_raw_clock_untouched) {
    const auto result = run_probe(PROBE_NAME, 1000s);

    ASSERT_TRUE(result.has_value());
    EXPECT_LT(result->monotonic_raw_elapsed.count(), 10.0);
}

TEST_F(ClockshiftTest, ignores_processes_with_other_name) {
    const auto result = run_probe("other-process", 1000s);

    ASSERT_TRUE(result.has_value());
    EXPECT_LT(result->steady_elapsed.count(), 10.0);
}

TEST_F(ClockshiftTest, missing_offset_file_leaves_clock_unshifted) {
    fs::remove(offset_file);

    const auto result = run_probe(PROBE_NAME, 1000s);

    ASSERT_TRUE(result.has_value());
    EXPECT_LT(result->steady_elapsed.count(), 10.0);
}

} // namespace
