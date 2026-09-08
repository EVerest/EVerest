// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include <everest/io/event/unique_fd.hpp>
#include <everest/io/socket/socket.hpp>
#include <everest/io/uds/uds_socket.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

using everest::lib::io::uds::shared_fd;
using everest::lib::io::uds::uds_credentials;
using everest::lib::io::uds::uds_info;
using everest::lib::io::uds::uds_payload;

/**
 * @brief Fill a sockaddr_un naming \p destination and report its exact length.
 * @details An abstract name is a leading NUL followed by exactly the name, with no terminator,
 * so the length carries the name and cannot be recomputed from the bytes. A pathname is NUL
 * terminated, except that Linux lets it fill sun_path completely, in which case the length is the
 * whole structure. A name that does not fit yields a zero length, which no send accepts.
 */
socklen_t fill_uds_address(struct sockaddr_un& addr, uds_info const& destination) {
    addr = {};
    addr.sun_family = AF_UNIX;

    constexpr size_t sun_path_size = sizeof(addr.sun_path);
    const size_t max_name_len = destination.is_abstract ? sun_path_size - 1 : sun_path_size;
    if (destination.path.empty() or destination.path.length() > max_name_len) {
        return 0;
    }

    constexpr auto family_offset = offsetof(struct sockaddr_un, sun_path);
    if (destination.is_abstract) {
        std::memcpy(addr.sun_path + 1, destination.path.data(), destination.path.length());
        return static_cast<socklen_t>(family_offset + 1 + destination.path.length());
    }
    std::memcpy(addr.sun_path, destination.path.data(), destination.path.length());
    return static_cast<socklen_t>(family_offset + std::min(destination.path.length() + 1, sun_path_size));
}

/**
 * @brief One sendmsg/recvmsg call, with the control message buffer an SCM_RIGHTS transfer needs.
 * @details A descriptor cannot ride in the payload, so every transfer is a msghdr with one iovec
 * and, when descriptors are involved, one control message. The buffer lives in this object so it
 * outlives neither the message nor the call, and is sized for the most descriptors one message may
 * carry plus the credentials the kernel attaches when they were asked for.
 */
struct unified_msg_context {
    std::array<struct iovec, 1> iov{};
    struct msghdr msg {};
    alignas(struct cmsghdr) char control_buffer[CMSG_SPACE(uds_payload::max_fds * sizeof(int)) +
                                                CMSG_SPACE(sizeof(struct ucred))]{};

    unified_msg_context(void* payload_base, size_t payload_len) {
        iov[0].iov_base = payload_base;
        iov[0].iov_len = payload_len;
        msg.msg_iov = iov.data();
        msg.msg_iovlen = iov.size();
        // No control message unless descriptors are attached or expected.
    }

    /**
     * @brief Attach the live descriptors among \p fds as one SCM_RIGHTS control message.
     * @details An empty or dead handle in \p fds is skipped: it names nothing to send. Nothing is
     * attached at all when none is live, so a plain datagram stays a plain datagram.
     */
    void attach_fds(std::vector<shared_fd> const& fds) {
        std::array<int, uds_payload::max_fds> raw{};
        size_t count = 0;
        for (auto const& fd : fds) {
            if (fd and fd->is_fd() and count < raw.size()) {
                raw[count++] = static_cast<int>(*fd);
            }
        }
        if (count == 0) {
            return;
        }
        msg.msg_control = control_buffer;
        msg.msg_controllen = CMSG_SPACE(count * sizeof(int));
        struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_len = CMSG_LEN(count * sizeof(int));
        std::memcpy(CMSG_DATA(cmsg), raw.data(), count * sizeof(int));
    }

    void prepare_for_control_receive() {
        msg.msg_control = control_buffer;
        msg.msg_controllen = sizeof(control_buffer);
    }

    /**
     * @brief The sender's credentials, if the kernel attached them.
     * @details Present only on a socket that asked with SO_PASSCRED; the kernel fills them in, the
     * sender cannot.
     */
    std::optional<uds_credentials> extract_credentials() const {
        if (msg.msg_controllen == 0) {
            return std::nullopt;
        }
        auto* header = const_cast<struct msghdr*>(&msg);
        for (struct cmsghdr* cmsg = CMSG_FIRSTHDR(header); cmsg != nullptr; cmsg = CMSG_NXTHDR(header, cmsg)) {
            if (cmsg->cmsg_level == SOL_SOCKET and cmsg->cmsg_type == SCM_CREDENTIALS and
                cmsg->cmsg_len >= CMSG_LEN(sizeof(struct ucred))) {
                struct ucred credentials {};
                std::memcpy(&credentials, CMSG_DATA(cmsg), sizeof(credentials));
                return uds_credentials{credentials.pid, credentials.uid, credentials.gid};
            }
        }
        return std::nullopt;
    }

    /**
     * @brief The descriptors the kernel installed for this message.
     * @details Every descriptor found is now owned by this process and is returned, so the caller
     * accounts for all of them, whether it keeps or closes them. The kernel may pack more into the
     * buffer than \ref uds_payload::max_fds, because CMSG_SPACE pads to alignment; the caller
     * decides what a surplus means.
     */
    std::vector<int> extract_fds() const {
        std::vector<int> fds;
        if (msg.msg_controllen == 0) {
            return fds;
        }
        // The CMSG_* accessors read only, but take a non-const msghdr.
        auto* header = const_cast<struct msghdr*>(&msg);
        for (struct cmsghdr* cmsg = CMSG_FIRSTHDR(header); cmsg != nullptr; cmsg = CMSG_NXTHDR(header, cmsg)) {
            if (cmsg->cmsg_level != SOL_SOCKET or cmsg->cmsg_type != SCM_RIGHTS) {
                continue;
            }
            const size_t count = (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int);
            for (size_t i = 0; i < count; ++i) {
                int fd = -1;
                std::memcpy(&fd, CMSG_DATA(cmsg) + i * sizeof(int), sizeof(int));
                fds.push_back(fd);
            }
        }
        return fds;
    }
};

// Whether a payload can be sent at all. A message the receiver would drop as too large, in bytes
// or in descriptors, is not retryable: it fails with EMSGSIZE rather than being tried forever.
bool payload_fits(uds_payload const& payload) {
    return payload.size() <= uds_payload::max_size and payload.fds.size() <= uds_payload::max_fds;
}

} // namespace

namespace everest::lib::io::uds {

uds_socket_base::~uds_socket_base() {
    discard();
}

void uds_socket_base::release_bound_path() {
    // Unlinked while the socket is still open: a newcomer probing the path meanwhile is refused
    // with EADDRINUSE instead of finding a stale file, rebinding, and losing its file to our unlink.
    if (not m_bound_path.empty()) {
        ::unlink(m_bound_path.c_str());
        m_bound_path.clear();
    }
}

void uds_socket_base::adopt(event::unique_fd&& fd, std::string bound_path) {
    release_bound_path();
    m_owned_uds_fd = std::move(fd);
    m_bound_path = std::move(bound_path);
    m_connect_error = 0;
    m_io_error = 0;
}

void uds_socket_base::record_connect_failure(int error) {
    release_bound_path();
    m_owned_uds_fd.close();
    m_connect_error = error;
    m_io_error = 0;
}

void uds_socket_base::discard() {
    release_bound_path();
    m_owned_uds_fd.close();
    m_connect_error = 0;
    m_io_error = 0;
}

void uds_socket_base::record_io_error(int error) {
    if (error == EAGAIN or error == EWOULDBLOCK or error == EINTR) {
        return;
    }
    m_io_error = error;
}

bool uds_socket_base::is_open() const {
    return m_owned_uds_fd.is_fd();
}

void uds_socket_base::close() {
    discard();
}

int uds_socket_base::get_fd() const {
    return m_owned_uds_fd;
}

int uds_socket_base::get_error() const {
    // A failed send or receive is the freshest verdict on the connection, and the only one for a
    // vanished peer: AF_UNIX reports that synchronously and leaves SO_ERROR untouched.
    if (m_io_error != 0) {
        return m_io_error;
    }
    // Zero means "nothing recorded", not "healthy": falling through to the probe of
    // an unassigned descriptor yields EBADF, and that nonzero value is what makes
    // the client reset and reconnect. Reporting zero here would read as healthy.
    if (not m_owned_uds_fd.is_fd() and m_connect_error != 0) {
        return m_connect_error;
    }
    return socket::get_pending_error(m_owned_uds_fd);
}

bool uds_socket_base::tx_impl(uds_payload const& payload) {
    if (not is_open()) {
        return false;
    }
    if (not payload_fits(payload)) {
        // Can never be delivered, and no fault of the connection: dropped rather than retried or
        // charged to the connection.
        return true;
    }

    // sendmsg requires a non-const iov_base, but does not write through it. MSG_NOSIGNAL: a
    // connection oriented peer that went away answers EPIPE, which is recorded below like any other
    // failure; the SIGPIPE it would also raise is not.
    unified_msg_context ctx(const_cast<uint8_t*>(payload.buffer.data()), payload.size());
    ctx.attach_fds(payload.fds);

    ssize_t nbytes = ::sendmsg(m_owned_uds_fd, &ctx.msg, MSG_NOSIGNAL);
    if (nbytes == -1) {
        record_io_error(errno);
        return false;
    }
    return nbytes == static_cast<ssize_t>(payload.size());
}

bool uds_socket_base::tx_impl(uds_payload const& payload, uds_info const& destination) {
    if (not is_open()) {
        return false;
    }
    if (not payload_fits(payload)) {
        return true;
    }

    struct sockaddr_un peer_addr {};
    const auto peer_addr_len = fill_uds_address(peer_addr, destination);
    if (peer_addr_len == 0) {
        // Nowhere to send to: an unnamed sender cannot be answered. Dropped like any undeliverable
        // datagram; the socket itself is fine.
        return true;
    }

    unified_msg_context ctx(const_cast<uint8_t*>(payload.buffer.data()), payload.size());
    ctx.attach_fds(payload.fds);
    ctx.msg.msg_name = &peer_addr;
    ctx.msg.msg_namelen = peer_addr_len;

    ssize_t nbytes = ::sendmsg(m_owned_uds_fd, &ctx.msg, MSG_NOSIGNAL);
    if (nbytes == -1) {
        const int error = errno;
        if (error == ECONNREFUSED or error == ENOENT or error == ENOTCONN or error == EPERM or error == EACCES) {
            // The peer is gone, has connected elsewhere (EPERM), or its socket file denies us
            // (EACCES). Dropped like any datagram; the socket itself is fine.
            return true;
        }
        record_io_error(error);
        return false;
    }
    return nbytes == static_cast<ssize_t>(payload.size());
}

std::optional<uds_info> uds_socket_base::rx_impl(uds_payload& payload) {
    if (not is_open()) {
        return std::nullopt;
    }

    struct sockaddr_un peer_addr {};
    unified_msg_context ctx(m_rx_buffer.data(), m_rx_buffer.size());
    ctx.prepare_for_control_receive();
    ctx.msg.msg_name = &peer_addr;
    ctx.msg.msg_namelen = sizeof(peer_addr);

    // A received descriptor must not leak into a child of this process any more than one this
    // library opened itself would.
    const ssize_t payload_size = ::recvmsg(m_owned_uds_fd, &ctx.msg, MSG_CMSG_CLOEXEC);
    if (payload_size < 0) {
        record_io_error(errno);
        return std::nullopt;
    }

    // Owned by this process from here on, every one of them, kept or closed.
    auto received_fds = ctx.extract_fds();

    // A datagram that did not arrive whole is dropped rather than handed up in part: bytes past
    // the buffer, or descriptors past what one message may carry, whether the kernel packed them
    // in (CMSG_SPACE pads to alignment) or cut them off (MSG_CTRUNC). What did arrive would leak
    // with the payload, so it is closed here.
    const bool truncated = (ctx.msg.msg_flags & MSG_TRUNC) or (ctx.msg.msg_flags & MSG_CTRUNC) or
                           received_fds.size() > uds_payload::max_fds;
    if (truncated) {
        for (int fd : received_fds) {
            ::close(fd);
        }
        return std::nullopt;
    }

    // Replacing bytes, handles and credentials releases whatever the previous read left in the
    // payload.
    payload.set_message(m_rx_buffer.data(), static_cast<size_t>(payload_size));
    payload.credentials = ctx.extract_credentials();
    payload.fds.clear();
    payload.fds.reserve(received_fds.size());
    for (int fd : received_fds) {
        payload.fds.push_back(std::make_shared<event::unique_fd>(fd));
    }

    constexpr auto family_offset = offsetof(struct sockaddr_un, sun_path);
    // The kernel reports the length the address would have had, which for a pathname filling all
    // of sun_path is one more than fits: the terminator it could not store. Everything below stays
    // inside what was actually written.
    const size_t name_len = std::min<size_t>(ctx.msg.msg_namelen, sizeof(peer_addr));
    if (name_len <= family_offset) {
        // An unbound sender has no address, so nothing can be sent back to it.
        return uds_info{"", false};
    }
    const size_t path_capacity = name_len - family_offset;

    const bool is_abstract = peer_addr.sun_path[0] == '\0';
    // An abstract name is exactly the bytes the address length covers. A pathname is NUL terminated
    // unless it fills sun_path completely, which Linux allows, so its end is bounded, not assumed.
    auto parsed_path = is_abstract ? std::string(peer_addr.sun_path + 1, path_capacity - 1)
                                   : std::string(peer_addr.sun_path, ::strnlen(peer_addr.sun_path, path_capacity));

    return uds_info{std::move(parsed_path), is_abstract};
}

/////////////////////////////////////////////////

bool uds_client_socket::setup(std::string const& remote, bool remote_abstract, std::string const& local,
                              bool local_abstract, bool with_peer_credentials) {
    m_remote = remote;
    m_remote_abstract = remote_abstract;
    m_local = local;
    m_local_abstract = local_abstract;
    m_with_peer_credentials = with_peer_credentials;
    discard();
    return true;
}

void uds_client_socket::connect(std::function<void(bool, int)> const& setup_cb) {
    int error = 0;
    try {
        auto socket = socket::open_uds_client_socket(m_remote, m_remote_abstract, m_local, m_local_abstract,
                                                     /*client_autobind=*/m_local.empty());
        socket::set_non_blocking(socket);
        if (m_with_peer_credentials) {
            socket::request_peer_credentials(socket);
        }
        const auto fd = static_cast<int>(socket);
        adopt(std::move(socket), m_local_abstract ? std::string{} : m_local);
        setup_cb(true, fd);
        return;
    } catch (socket::socket_error const& e) {
        error = e.error();
    } catch (...) {
    }
    record_connect_failure(error);
    std::this_thread::sleep_for(std::chrono::milliseconds(socket::reconnect_delay_ms));
    setup_cb(false, -1);
}

bool uds_client_socket::open(std::string const& remote, bool remote_abstract, std::string const& local,
                             bool local_abstract, bool with_peer_credentials) {
    int error = 0;
    try {
        auto socket = socket::open_uds_client_socket(remote, remote_abstract, local, local_abstract,
                                                     /*client_autobind=*/local.empty());
        socket::set_non_blocking(socket);
        if (with_peer_credentials) {
            socket::request_peer_credentials(socket);
        }
        adopt(std::move(socket), local_abstract ? std::string{} : local);
        // SO_ERROR is read-and-clear. The pending error is read once and kept, so a
        // false return still carries the reason instead of a value already consumed.
        error = get_error();
        if (error == 0) {
            return true;
        }
    } catch (socket::socket_error const& e) {
        error = e.error();
    } catch (...) {
    }
    record_connect_failure(error);
    return false;
}

bool uds_client_socket::tx(uds_payload const& payload) {
    return tx_impl(payload);
}

bool uds_client_socket::rx(uds_payload& payload) {
    return rx_impl(payload).has_value();
}

/////////////////////////////////////////////////

bool uds_server_socket::open(std::string const& path, bool is_abstract, bool with_peer_credentials,
                             std::optional<mode_t> mode) {
    int error = 0;
    try {
        auto socket = socket::open_uds_server_socket(path, is_abstract, mode);
        socket::set_non_blocking(socket);
        if (with_peer_credentials) {
            socket::request_peer_credentials(socket);
        }
        adopt(std::move(socket), is_abstract ? std::string{} : path);
        error = get_error();
        if (error == 0) {
            return true;
        }
    } catch (socket::socket_error const& e) {
        error = e.error();
    } catch (...) {
    }
    record_connect_failure(error);
    return false;
}

bool uds_server_socket::tx(uds_payload const& payload) {
    if (not m_last_source) {
        // No client has spoken yet, so there is nobody to answer. Dropped; the socket is fine.
        return true;
    }
    return tx_impl(payload, *m_last_source);
}

bool uds_server_socket::rx(uds_payload& payload) {
    auto result = rx_impl(payload);
    if (not result) {
        return false;
    }
    // The sender, named or not. An unnamed one cannot be answered and the reply is dropped; keeping
    // the previous named sender instead would deliver that reply, descriptors included, to the
    // wrong client.
    m_last_source = result;
    return true;
}

} // namespace everest::lib::io::uds
