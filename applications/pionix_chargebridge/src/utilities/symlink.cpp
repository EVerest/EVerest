// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
#include <charge_bridge/utilities/symlink.hpp>

#include <charge_bridge/utilities/logging.hpp>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace charge_bridge::utilities {

namespace {
// perror() writes straight to stderr, which corrupts the terminal dashboard's screen and loses the
// message from its panel. Report through print_error instead, which routes into the UI when it is
// active and to stdout otherwise. errno is read once, before any formatting can clobber it.
void report_errno(std::string const& msg) {
    auto const error_code = errno;
    print_error("", "SYMLINK", error_code) << msg << ": " << std::strerror(error_code) << std::endl;
}
} // namespace

symlink::symlink() {
}

symlink::symlink(std::string const& src, std::string const& tar) {
    auto result = set_link(src, tar);
    if (not result) {
        report_errno("Cannot create symbolic link from '" + src + "' to '" + tar + "'");
    }
}

bool symlink::set_link(std::string const& src, std::string const& tar) {
    m_tar = tar;
    del_link();
    auto result = ::symlink(src.c_str(), tar.c_str()) == 0;
    if (result) {
        m_tar = tar;
    }
    return result;
}

bool symlink::del_link() {
    auto result = true;
    if (not m_tar.empty()) {
        auto code = ::unlink(m_tar.c_str());
        result = code == 0 or code == ENOENT;
        m_tar = "";
    }
    return result;
}

symlink::~symlink() {
    if (not m_tar.empty()) {
        auto result = del_link();
        if (not result) {
            report_errno("Cannot delete symbolic link '" + m_tar + "'");
        }
    }
}

} // namespace charge_bridge::utilities
