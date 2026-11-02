// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "SessionLog.hpp"

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace module {
namespace {

struct TemporaryDirectory {
    std::filesystem::path path;

    explicit TemporaryDirectory(const std::string& prefix) {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() / (prefix + std::to_string(suffix));
    }

    ~TemporaryDirectory() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

} // namespace

TEST(SessionLogTest, StartSessionCreatesSessionDirectory) {
    TemporaryDirectory temp("everest-session-log-test-");

    SessionLog log;
    log.setPath(temp.path.string());
    log.setMqtt([](const nlohmann::json&) {});
    log.enable();

    const auto logpath = log.startSession("session");

    ASSERT_TRUE(logpath.has_value());
    EXPECT_TRUE(std::filesystem::is_directory(*logpath));
    EXPECT_EQ(logpath->parent_path(), std::filesystem::weakly_canonical(temp.path));

    log.stopSession();
}

TEST(SessionLogTest, StartSessionReturnsNulloptWhenDirectoryCannotBeCreated) {
    SessionLog log;
    log.setPath("/dev/null/everest-session-log-test");
    log.enable();

    std::optional<std::filesystem::path> logpath;
    EXPECT_NO_THROW(logpath = log.startSession("session"));
    EXPECT_FALSE(logpath.has_value());
}

TEST(SessionLogTest, InvalidV2gJsonIsLoggedWithoutThrowing) {
    TemporaryDirectory temp("everest-session-log-test-");

    SessionLog log;
    log.setPath(temp.path.string());
    log.setMqtt([](const nlohmann::json&) {});
    log.enable();
    log.xmlOutput(true);
    ASSERT_TRUE(log.startSession("session").has_value());

    const std::string deeply_nested = std::string(100000, '[') + std::string(100000, ']');
    EXPECT_NO_THROW(log.car(true, "V2G SessionSetupReq", "", "", "", R"({"V2G_Message":)"));
    EXPECT_NO_THROW(log.evse(true, "V2G SessionSetupRes", "", "", "", deeply_nested));

    log.stopSession();
}

} // namespace module
