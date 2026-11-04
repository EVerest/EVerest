// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include <everest/io/uds/uds_payload.hpp>

#include <fcntl.h>
#include <vector>

namespace everest::lib::io::uds {

uds_payload::uds_payload(std::string const& msg) {
    set_message(msg);
}

uds_payload::uds_payload(const char* msg) {
    set_message(msg);
}

bool uds_payload::operator==(uds_payload const& other) const {
    return buffer == other.buffer and fds == other.fds;
}

size_t uds_payload::size() const {
    return buffer.size();
}

bool uds_payload::set_message(std::string const& msg) {
    if (msg.size() > max_size) {
        return false;
    }
    buffer.assign(msg.begin(), msg.end());
    return true;
}

bool uds_payload::set_message(void const* data, size_t size) {
    if (size > max_size) {
        return false;
    }
    auto ptr = reinterpret_cast<uint8_t const*>(data);
    buffer.assign(ptr, ptr + size);
    return true;
}

bool uds_payload::attach(event::unique_fd&& descriptor) {
    if (not descriptor.is_fd() or fds.size() >= max_fds) {
        return false;
    }
    fds.push_back(std::make_shared<event::unique_fd>(std::move(descriptor)));
    return true;
}

bool uds_payload::attach_duplicate(int descriptor) {
    if (descriptor < 0 or fds.size() >= max_fds) {
        return false;
    }
    const int duplicate = ::fcntl(descriptor, F_DUPFD_CLOEXEC, 0);
    if (duplicate < 0) {
        return false;
    }
    fds.push_back(std::make_shared<event::unique_fd>(duplicate));
    return true;
}

int uds_payload::fd(size_t index) const {
    if (index >= fds.size() or not fds[index] or not fds[index]->is_fd()) {
        return -1;
    }
    return static_cast<int>(*fds[index]);
}

bool uds_payload::has_fds() const {
    return not fds.empty();
}

} // namespace everest::lib::io::uds
