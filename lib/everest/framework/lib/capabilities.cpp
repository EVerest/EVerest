// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <utils/capabilities.hpp>

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>

#include <linux/capability.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <fmt/core.h>

namespace Everest {

std::optional<std::string> raise_ambient_capabilities() {
    if (geteuid() == 0) {
        return std::nullopt;
    }

    __user_cap_header_struct header{_LINUX_CAPABILITY_VERSION_3, 0};
    std::array<__user_cap_data_struct, _LINUX_CAPABILITY_U32S_3> data{};
    if (syscall(SYS_capget, &header, data.data()) != 0) {
        return fmt::format("capget failed: {}", std::strerror(errno));
    }

    bool has_permitted = false;
    for (auto& set : data) {
        has_permitted = has_permitted or set.permitted != 0;
        set.inheritable |= set.permitted;
    }
    if (not has_permitted) {
        return std::nullopt;
    }

    // a capability can only be raised to the ambient set if it is permitted and inheritable
    if (syscall(SYS_capset, &header, data.data()) != 0) {
        return fmt::format("capset failed: {}", std::strerror(errno));
    }

    for (unsigned int cap = 0; cap <= CAP_LAST_CAP; ++cap) {
        const std::uint32_t bit = 1U << (cap % 32);
        if ((data.at(cap / 32).permitted & bit) == 0) {
            continue;
        }
        if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, cap, 0, 0) != 0) {
            return fmt::format("Raising capability {} to the ambient set failed: {}", cap, std::strerror(errno));
        }
    }

    return std::nullopt;
}

} // namespace Everest
