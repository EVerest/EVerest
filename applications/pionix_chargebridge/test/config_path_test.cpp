// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <charge_bridge/utilities/config_path.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using charge_bridge::utilities::resolve_from_shell_cwd;

namespace {

// root/
//   applications/config.yaml
//   build/TESTING/dist/bin/      <- the physical working directory
//   build/dist -> TESTING/dist   <- the shell's view: $PWD = root/build/dist/bin
class ConfigPathTest : public ::testing::Test {
protected:
    void SetUp() override {
        root = fs::temp_directory_path() / fs::path("cb_config_path_" + std::to_string(::getpid()));
        fs::remove_all(root);
        fs::create_directories(root / "applications");
        fs::create_directories(root / "build/TESTING/dist/bin");
        fs::create_directory_symlink("TESTING/dist", root / "build/dist");
        std::ofstream(root / "applications/config.yaml") << "charge_bridge:\n";
        std::ofstream(root / "build/TESTING/dist/bin/local.yaml") << "charge_bridge:\n";
        previous_cwd = fs::current_path();
        fs::current_path(root / "build/TESTING/dist/bin");
        logical_cwd = (root / "build/dist/bin").string();
    }
    void TearDown() override {
        fs::current_path(previous_cwd);
        fs::remove_all(root);
    }
    fs::path root;
    fs::path previous_cwd;
    std::string logical_cwd;
};

TEST_F(ConfigPathTest, dotdot_path_resolves_against_logical_cwd) {
    auto const resolved = resolve_from_shell_cwd("../../../applications/config.yaml", logical_cwd.c_str());
    EXPECT_EQ(resolved, (root / "applications/config.yaml").lexically_normal());
    EXPECT_TRUE(fs::exists(resolved));
}

TEST_F(ConfigPathTest, path_that_only_works_physically_is_not_rescued) {
    // Four levels up is right for the physical directory and wrong for the prompt; the prompt wins.
    auto const resolved = resolve_from_shell_cwd("../../../../applications/config.yaml", logical_cwd.c_str());
    EXPECT_EQ(resolved, (root.parent_path() / "applications/config.yaml").lexically_normal());
    EXPECT_FALSE(fs::exists(resolved));
}

TEST_F(ConfigPathTest, missing_dotdot_path_resolves_to_the_logical_location) {
    auto const resolved = resolve_from_shell_cwd("../../../nowhere.yaml", logical_cwd.c_str());
    EXPECT_EQ(resolved, (root / "nowhere.yaml").lexically_normal());
}

TEST_F(ConfigPathTest, absolute_path_is_kept) {
    auto const path = root / "applications/config.yaml";
    EXPECT_EQ(resolve_from_shell_cwd(path, logical_cwd.c_str()), path);
}

TEST_F(ConfigPathTest, path_without_dotdot_is_kept) {
    EXPECT_EQ(resolve_from_shell_cwd("local.yaml", logical_cwd.c_str()), fs::path("local.yaml"));
    EXPECT_EQ(resolve_from_shell_cwd("missing.yaml", logical_cwd.c_str()), fs::path("missing.yaml"));
}

TEST_F(ConfigPathTest, stale_or_absent_pwd_is_ignored) {
    auto const path = fs::path("../../../applications/config.yaml");
    EXPECT_EQ(resolve_from_shell_cwd(path, nullptr), path);
    EXPECT_EQ(resolve_from_shell_cwd(path, ""), path);
    EXPECT_EQ(resolve_from_shell_cwd(path, "relative/pwd"), path);
    auto const elsewhere = (root / "applications").string();
    EXPECT_EQ(resolve_from_shell_cwd(path, elsewhere.c_str()), path);
}

} // namespace
