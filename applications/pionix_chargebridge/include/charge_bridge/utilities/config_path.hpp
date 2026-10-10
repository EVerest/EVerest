// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

namespace charge_bridge::utilities {

// Resolve a relative path the way the shell that started us sees it.
//
// The kernel resolves ".." against the physical working directory, the shell shows the logical one
// ($PWD, symlinks left in place). After `cd build/dist/bin` through a `dist -> TESTING/dist` link the
// prompt says build/dist/bin while the process sits in build/TESTING/dist/bin, so "../../../x" names
// a different file for each. A path with a ".." component is therefore resolved against $PWD, the
// view the user typed it in, provided $PWD really names the current directory. The physical view is
// not consulted at all: a path that only works physically is as wrong as it looks in the prompt.
// Absolute paths, paths without "..", and a stale or absent $PWD are returned as is.
inline std::filesystem::path resolve_from_shell_cwd(std::filesystem::path const& path,
                                                    char const* logical_cwd = std::getenv("PWD")) {
    namespace fs = std::filesystem;
    if (path.is_absolute() or logical_cwd == nullptr or logical_cwd[0] != '/') {
        return path;
    }
    bool has_parent_component = false;
    for (auto const& component : path) {
        if (component == "..") {
            has_parent_component = true;
            break;
        }
    }
    if (not has_parent_component) {
        return path;
    }
    std::error_code ec;
    if (not fs::equivalent(fs::path(logical_cwd), fs::current_path(ec), ec) or ec) {
        return path;
    }
    return (fs::path(logical_cwd) / path).lexically_normal();
}

} // namespace charge_bridge::utilities
