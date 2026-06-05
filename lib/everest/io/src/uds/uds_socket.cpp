// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "everest/io/event/unique_fd.hpp"
#include <chrono>
#include <cstddef>
#include <cstring>
#include <everest/io/socket/socket.hpp>
#include <everest/io/uds/uds_socket.hpp>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

namespace {

struct unified_msg_context {
    std::array<struct iovec, 1> iov{};
    struct msghdr msg {};
    alignas(struct cmsghdr) char control_buffer[CMSG_SPACE(sizeof(int))]{};

    unified_msg_context(void* payload_base, size_t payload_len) {
        iov[0].iov_base = payload_base;
        iov[0].iov_len = payload_len;
        msg.msg_iov = iov.data();
        msg.msg_iovlen = iov.size();
        // Control buffers remain unused by default for standard data tx/rx
    }

    void attach_fd(int fd) {
        msg.msg_control = control_buffer;
        msg.msg_controllen = sizeof(control_buffer);
        struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_len = CMSG_LEN(sizeof(int));
        std::memcpy(CMSG_DATA(cmsg), &fd, sizeof(int));
    }

    void prepare_for_fd_receive() {
        msg.msg_control = control_buffer;
        msg.msg_controllen = sizeof(control_buffer);
    }

    int extract_fd() {
        if (msg.msg_controllen == 0)
            return -1;
        struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
        if (cmsg != nullptr && cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
            int fd = -1;
            std::memcpy(&fd, CMSG_DATA(cmsg), sizeof(int));
            return fd;
        }
        return -1;
    }
};
} // anonymous namespace

namespace everest::lib::io::uds {

bool uds_socket_base::open_as_client(std::string const& remote, bool remote_abstract, std::string const& local,
                                     bool local_abstract) {
    try {
        auto socket = socket::open_uds_client_socket(remote, remote_abstract, local, local_abstract);
        socket::set_non_blocking(socket);
        m_owned_uds_fd = std::move(socket);
        return socket::get_pending_error(m_owned_uds_fd) == 0;
    } catch (...) {
    }
    return false;
}

bool uds_socket_base::open_as_server(std::string const& path, bool is_abstract) {
    try {
        auto socket = socket::open_uds_server_socket(path, is_abstract);
        socket::set_non_blocking(socket);
        m_owned_uds_fd = std::move(socket);
        return socket::get_pending_error(m_owned_uds_fd) == 0;
    } catch (...) {
    }
    return false;
}

bool uds_socket_base::is_open() const {
    return m_owned_uds_fd.is_fd();
}
void uds_socket_base::close() {
    m_owned_uds_fd.close();
}
int uds_socket_base::get_fd() const {
    return m_owned_uds_fd;
}
int uds_socket_base::get_error() const {
    return socket::get_pending_error(m_owned_uds_fd);
}

bool uds_socket_base::tx_impl(void const* payload, size_t size, int fd_to_send) {
    if (not is_open()) {
        return false;
    }

    // sendmsg requires a non-const void*, but POSIX guarantees it won't mutate the buffer
    unified_msg_context ctx(const_cast<void*>(payload), size);
    if (fd_to_send >= 0) {
        ctx.attach_fd(fd_to_send);
    }

    ssize_t nbytes = ::sendmsg(m_owned_uds_fd, &ctx.msg, 0);
    return nbytes == static_cast<ssize_t>(size);
}

bool uds_socket_base::tx_impl(void const* payload, size_t size, uds_info const& destination, int fd_to_send) {
    if (not is_open() || destination.path.empty()) {
        return false;
    }

    unified_msg_context ctx(const_cast<void*>(payload), size);
    if (fd_to_send >= 0) {
        ctx.attach_fd(fd_to_send);
    }

    struct sockaddr_un peer_addr {};
    peer_addr.sun_family = AF_UNIX;
    socklen_t peer_addr_len = offsetof(struct sockaddr_un, sun_path);

    if (destination.is_abstract) {
        peer_addr.sun_path[0] = '\0';
        std::memcpy(peer_addr.sun_path + 1, destination.path.data(), destination.path.length());
        peer_addr_len += 1 + destination.path.length();
    } else {
        std::strncpy(peer_addr.sun_path, destination.path.c_str(), sizeof(peer_addr.sun_path) - 1);
        peer_addr_len += destination.path.length() + 1;
    }

    ctx.msg.msg_name = &peer_addr;
    ctx.msg.msg_namelen = peer_addr_len;

    ssize_t nbytes = ::sendmsg(m_owned_uds_fd, &ctx.msg, 0);
    return nbytes == static_cast<ssize_t>(size);
}

std::optional<uds_info> uds_socket_base::rx_impl(void* buffer, size_t buffer_size, ssize_t& payload_size,
                                                 int* received_fd) {
    if (not is_open())
        return std::nullopt;

    struct sockaddr_un peer_addr {};
    unified_msg_context ctx(buffer, buffer_size);

    if (received_fd != nullptr) {
        ctx.prepare_for_fd_receive();
    }

    ctx.msg.msg_name = &peer_addr;
    ctx.msg.msg_namelen = sizeof(peer_addr);

    payload_size = ::recvmsg(m_owned_uds_fd, &ctx.msg, 0);

    if (payload_size < 0) {
        return std::nullopt;
    }

    int extracted_fd = (received_fd != nullptr) ? ctx.extract_fd() : -1;

    // Truncation guard: prevent FD leaks if the payload exceeded the buffer
    if (ctx.msg.msg_flags & MSG_TRUNC) {
        if (extracted_fd >= 0)
            ::close(extracted_fd);
        payload_size = -1;
        if (received_fd)
            *received_fd = -1;
        return std::nullopt;
    }

    if (received_fd)
        *received_fd = extracted_fd;

    if (ctx.msg.msg_namelen <= offsetof(struct sockaddr_un, sun_path)) {
        return uds_info{"", false};
    }

    bool is_abstract = (peer_addr.sun_path[0] == '\0');
    std::string parsed_path =
        is_abstract
            ? std::string(peer_addr.sun_path + 1, ctx.msg.msg_namelen - offsetof(struct sockaddr_un, sun_path) - 1)
            : std::string(peer_addr.sun_path);

    return uds_info{parsed_path, is_abstract};
}

// ----------------------------------------------------------------------------
// STANDARD CLIENT SOCKET (Data Only)
// ----------------------------------------------------------------------------

bool uds_client_socket::setup(std::string const& remote, bool remote_abstract, std::string const& local,
                              bool local_abstract, int timeout_ms) {
    m_remote = remote;
    m_remote_abstract = remote_abstract;
    m_local = local;
    m_local_abstract = local_abstract;
    m_timeout_ms = timeout_ms;
    m_owned_uds_fd.close();
    return true;
}

void uds_client_socket::connect(std::function<void(bool, int)> const& setup_cb) {
    try {
        auto socket = socket::open_uds_client_socket(m_remote, m_remote_abstract, m_local, m_local_abstract);
        socket::set_non_blocking(socket);
        setup_cb(true, socket);
        m_owned_uds_fd = std::move(socket);
    } catch (...) {
        std::this_thread::sleep_for(std::chrono::milliseconds(m_timeout_ms));
        setup_cb(false, -1);
    }
}

bool uds_client_socket::open(std::string const& remote, bool remote_abstract, std::string const& local,
                             bool local_abstract) {
    return open_as_client(remote, remote_abstract, local, local_abstract);
}

bool uds_client_socket::tx(uds_payload const& payload) {
    return tx_impl(payload.buffer.data(), payload.size());
}

bool uds_client_socket::rx(uds_payload& payload) {
    ssize_t msg_size = 0;
    auto result = rx_impl(rx_buffer.data(), rx_buffer.size(), msg_size);
    if (result && msg_size >= 0) {
        payload.set_message(rx_buffer.data(), msg_size);
        return true;
    }
    return false;
}

// ----------------------------------------------------------------------------
// STANDARD SERVER SOCKET (Data Only)
// ----------------------------------------------------------------------------

bool uds_server_socket::open(std::string const& path, bool is_abstract) {
    return open_as_server(path, is_abstract);
}

bool uds_server_socket::tx(uds_payload const& payload) {
    if (not m_last_source)
        return false;
    return tx_impl(payload.buffer.data(), payload.size(), *m_last_source);
}

bool uds_server_socket::rx(uds_payload& payload) {
    ssize_t msg_size = 0;
    auto result = rx_impl(rx_buffer.data(), rx_buffer.size(), msg_size);
    if (not result || msg_size < 0) {
        return false;
    }

    payload.set_message(rx_buffer.data(), msg_size);
    m_last_source = result;
    return true;
}

// ----------------------------------------------------------------------------
// FD CLIENT SOCKET (Metadata + File Descriptor)
// ----------------------------------------------------------------------------

bool uds_fd_client_socket::setup(std::string const& remote, bool remote_abstract, std::string const& local,
                                 bool local_abstract, int timeout_ms) {
    m_remote = remote;
    m_remote_abstract = remote_abstract;
    m_local = local;
    m_local_abstract = local_abstract;
    m_timeout_ms = timeout_ms;
    m_owned_uds_fd.close();
    return true;
}

void uds_fd_client_socket::connect(std::function<void(bool, int)> const& setup_cb) {
    try {
        auto socket = socket::open_uds_client_socket(m_remote, m_remote_abstract, m_local, m_local_abstract);
        socket::set_non_blocking(socket);
        setup_cb(true, socket);
        m_owned_uds_fd = std::move(socket);
    } catch (...) {
        std::this_thread::sleep_for(std::chrono::milliseconds(m_timeout_ms));
        setup_cb(false, -1);
    }
}

bool uds_fd_client_socket::open(std::string const& remote, bool remote_abstract, std::string const& local,
                                bool local_abstract) {
    return open_as_client(remote, remote_abstract, local, local_abstract);
}

bool uds_fd_client_socket::tx(uds_fd_payload const& payload) {
    if (payload.metadata.size() > uds_fd_payload::max_size)
        return false;
    std::string data = payload.metadata.empty() ? "!" : payload.metadata;
    return tx_impl(data.data(), data.size(), payload.fd);
}

bool uds_fd_client_socket::rx(uds_fd_payload& payload) {
    std::array<char, uds_fd_payload::max_size> rx_buffer{};
    ssize_t msg_size = 0;
    int received_fd = -1;

    auto result = rx_impl(rx_buffer.data(), rx_buffer.size(), msg_size, &received_fd);
    if (result && msg_size >= 0) {
        payload.fd = received_fd;
        payload.metadata = std::string(rx_buffer.data(), msg_size);
        return true;
    }
    return false;
}

// ----------------------------------------------------------------------------
// FD SERVER SOCKET (Metadata + File Descriptor)
// ----------------------------------------------------------------------------

bool uds_fd_server_socket::open(std::string const& path, bool is_abstract) {
    return open_as_server(path, is_abstract);
}

bool uds_fd_server_socket::tx(uds_fd_payload const& payload) {
    if (not m_last_source || payload.metadata.size() > uds_fd_payload::max_size)
        return false;
    std::string data = payload.metadata.empty() ? "!" : payload.metadata;
    return tx_impl(data.data(), data.size(), *m_last_source, payload.fd);
}

bool uds_fd_server_socket::rx(uds_fd_payload& payload) {
    std::array<char, uds_fd_payload::max_size> rx_buffer{};
    ssize_t msg_size = 0;
    int received_fd = -1;

    auto result = rx_impl(rx_buffer.data(), rx_buffer.size(), msg_size, &received_fd);
    if (not result || msg_size < 0) {
        return false;
    }

    payload.fd = received_fd;
    payload.metadata = std::string(rx_buffer.data(), msg_size);
    m_last_source = result;
    return true;
}
} // namespace everest::lib::io::uds
