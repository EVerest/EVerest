// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#include <utils/telemetry/transport.hpp>

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <stdexcept>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <everest/io/event/unique_fd.hpp>

namespace everest::telemetry {

namespace {

struct UnixAddress {
    sockaddr_un address{};
    socklen_t length{0};
};

[[noreturn]] void throw_errno(const std::string& message) {
    throw std::runtime_error(message + ": " + std::strerror(errno));
}

UnixAddress make_address(const std::string& path) {
    UnixAddress result;
    if (path.empty()) {
        throw std::runtime_error("Telemetry socket path is empty");
    }
    if (path.size() >= sizeof(result.address.sun_path)) {
        throw std::runtime_error("Telemetry socket path is too long: " + path);
    }
    result.address.sun_family = AF_UNIX;
    std::memcpy(result.address.sun_path, path.c_str(), path.size() + 1);
    result.length = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + path.size() + 1);
    return result;
}

class UdsDatagramSender : public DatagramSender {
public:
    explicit UdsDatagramSender(const std::string& path) :
        m_address(make_address(path)), m_fd(::socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)) {
        if (not m_fd.is_fd()) {
            throw_errno("Failed to create the telemetry socket");
        }
    }

    SendResult send(const std::uint8_t* data, std::size_t size) override {
        const auto sent = ::sendto(m_fd, data, size, MSG_DONTWAIT | MSG_NOSIGNAL,
                                   reinterpret_cast<const sockaddr*>(&m_address.address), m_address.length);
        if (sent >= 0) {
            return SendResult::Ok;
        }
        switch (errno) {
        case EAGAIN:
#if EAGAIN != EWOULDBLOCK
        case EWOULDBLOCK:
#endif
        case ENOBUFS:
            return SendResult::WouldBlock;
        case ENOENT:
        case ECONNREFUSED:
            return SendResult::NoReceiver;
        case EMSGSIZE:
            return SendResult::TooBig;
        default:
            return SendResult::Error;
        }
    }

private:
    UnixAddress m_address;
    lib::io::event::unique_fd m_fd;
};

} // namespace

std::string_view to_string(SendResult result) {
    switch (result) {
    case SendResult::Ok:
        return "ok";
    case SendResult::WouldBlock:
        return "receiver queue full";
    case SendResult::NoReceiver:
        return "no receiver";
    case SendResult::TooBig:
        return "datagram too big";
    case SendResult::Error:
        return "send error";
    }
    return "unknown";
}

void hand_over_to_receiver_fd(int socket_fd) {
    if (socket_fd == RECEIVER_FD) {
        if (::fcntl(socket_fd, F_SETFD, 0) != 0) {
            throw std::runtime_error(std::string("Failed to clear close-on-exec of the telemetry socket: ") +
                                     std::strerror(errno));
        }
        return;
    }
    if (::dup2(socket_fd, RECEIVER_FD) < 0) {
        throw std::runtime_error(std::string("Failed to hand over the telemetry socket: ") + std::strerror(errno));
    }
}

std::unique_ptr<DatagramSender> make_uds_datagram_sender(const std::string& path) {
    return std::make_unique<UdsDatagramSender>(path);
}

int bind_receiver_socket(const std::string& path, std::uint32_t mode) {
    const auto address = make_address(path);

    struct stat existing {};
    if (::lstat(path.c_str(), &existing) == 0) {
        if (not S_ISSOCK(existing.st_mode)) {
            throw std::runtime_error("Refusing to replace non-socket file " + path);
        }
        if (::unlink(path.c_str()) != 0) {
            throw_errno("Failed to remove the stale telemetry socket " + path);
        }
    }

    lib::io::event::unique_fd fd(::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0));
    if (not fd.is_fd()) {
        throw_errno("Failed to create the telemetry socket");
    }
    if (::bind(fd, reinterpret_cast<const sockaddr*>(&address.address), address.length) != 0) {
        throw_errno("Failed to bind the telemetry socket to " + path);
    }
    if (::chmod(path.c_str(), static_cast<mode_t>(mode)) != 0) {
        throw_errno("Failed to set the permissions of " + path);
    }
    return fd.release();
}

std::optional<std::string> bound_receiver_socket_path(int fd) {
    int type = 0;
    socklen_t type_length = sizeof(type);
    if (::getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &type_length) != 0 or type != SOCK_DGRAM) {
        return std::nullopt;
    }
    sockaddr_un address{};
    socklen_t length = sizeof(address);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
        return std::nullopt;
    }
    const auto path_offset = offsetof(sockaddr_un, sun_path);
    if (address.sun_family != AF_UNIX or length <= path_offset or address.sun_path[0] == '\0') {
        return std::nullopt;
    }
    return std::string(address.sun_path, ::strnlen(address.sun_path, length - path_offset));
}

} // namespace everest::telemetry
