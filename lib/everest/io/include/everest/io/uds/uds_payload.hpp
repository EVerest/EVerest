// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once

#include <cstddef>
#include <cstdint>
#include <everest/io/event/unique_fd.hpp>
#include <everest/io/socket/socket.hpp>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace everest::lib::io::uds {

/**
 * @var shared_fd
 * @brief File descriptor with shared ownership, closed when the last holder releases it.
 */
using shared_fd = std::shared_ptr<event::unique_fd>;

/**
 * @var uds_credentials
 * @brief Identity of a peer as verified by the kernel, see \ref socket::peer_credentials.
 */
using uds_credentials = socket::peer_credentials;

/**
 * @struct uds_info
 * @brief Address of a unix domain socket.
 */
struct uds_info {
    /** Filesystem path or abstract name */
    std::string path;
    /**
     * True for the abstract namespace: no file, gone with the last socket, and no access control:
     * every process in the network namespace may connect. A path with a mode is the option that
     * has one.
     */
    bool is_abstract{true};
};

/**
 * @struct uds_payload
 * @brief One unix domain socket message: bytes, optional file descriptors, optional sender
 * credentials.
 * @details Descriptors travel out of band (SCM_RIGHTS) in the same message as the bytes. They are
 * owned by the payload and shared between its copies, so a payload queued by an event client keeps
 * its descriptors open until sent, and a received payload closes them with its last copy.
 */
struct uds_payload {
    /**
     * @brief Empty payload.
     */
    uds_payload() = default;
    /**
     * @brief Payload holding \p msg.
     * @param[in] msg Message. Longer than \ref max_size leaves the buffer empty
     */
    uds_payload(std::string const& msg);
    /**
     * @brief Payload holding the null terminated \p msg.
     * @param[in] msg Message. Longer than \ref max_size leaves the buffer empty
     */
    uds_payload(const char* msg);

    /**
     * @brief Equal if both buffers hold the same bytes and both share the same descriptors.
     * @param[in] other Payload to compare with
     */
    bool operator==(uds_payload const& other) const;

    /**
     * @brief Size of \ref buffer in bytes.
     */
    size_t size() const;

    /**
     * @brief Replace \ref buffer. Descriptors and credentials stay.
     * @param[in] msg New message
     * @return True on success, false if \p msg exceeds \ref max_size
     */
    bool set_message(std::string const& msg);

    /**
     * @brief Replace \ref buffer. Descriptors and credentials stay.
     * @param[in] buffer New message
     * @param[in] size Bytes to copy
     * @return True on success, false if \p size exceeds \ref max_size
     */
    bool set_message(void const* buffer, size_t size);

    /**
     * @brief Attach a descriptor, taking ownership. It is closed with the last copy of the payload.
     * @param[in] fd Descriptor to take over
     * @return True on success, false for an empty \p fd or with \ref max_fds attached already
     */
    bool attach(event::unique_fd&& fd);

    /**
     * @brief Attach a close-on-exec duplicate of \p fd. The caller keeps \p fd.
     * @param[in] fd Descriptor to duplicate
     * @return True on success, false if \p fd cannot be duplicated or \ref max_fds are attached
     */
    bool attach_duplicate(int fd);

    /**
     * @brief Raw value of an attached descriptor. Valid while any copy of the payload lives.
     * @param[in] index Position in \ref fds
     * @return The descriptor, -1 if there is none at \p index
     */
    int fd(size_t index = 0) const;

    /**
     * @brief True if at least one descriptor is attached.
     */
    bool has_fds() const;

    /**
     * @brief Message bytes.
     */
    std::vector<uint8_t> buffer;

    /**
     * @brief Attached descriptors, in order. Usually empty.
     */
    std::vector<shared_fd> fds;

    /**
     * @brief Sender identity, set on receive if the socket was opened with peer credentials.
     * @details Filled by the kernel, datagram sockets only. A SEQPACKET connection has one peer for
     * its whole life and answers \ref uds_seqpacket_socket_base::peer_credentials instead; this
     * stays empty there. Set on an outgoing payload it is ignored.
     */
    std::optional<uds_credentials> credentials;

    /**
     * @brief The other end of this message.
     * @details Set on receive to the sender; an unnamed sender leaves an empty path. On a payload a
     * \ref uds_server_socket sends it is the destination, so a reply built from the request, or a
     * copy of it, reaches whoever asked even when another client has spoken since. A connected
     * socket ignores it on send. Empty on a payload that was never received.
     */
    std::optional<uds_info> peer;

    /**
     * @var max_size
     * @brief Upper bound of \ref buffer. A larger message is dropped by a datagram receiver and
     * fails a SEQPACKET connection with EMSGSIZE.
     */
    static constexpr size_t max_size = 64 * 1024;

    /**
     * @var max_fds
     * @brief Upper bound of \ref fds per message (SCM_MAX_FD).
     */
    static constexpr size_t max_fds = 253;
};

} // namespace everest::lib::io::uds
