// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <everest/io/event/unique_fd.hpp>
#include <everest/io/shm/control_protocol.hpp>

namespace everest::lib::io::shm::control {

/// \brief Client-side protocol error categories.
enum class client_error_code {
    socket_error,
    malformed_response,
    invalid_response,
    fd_mismatch,
};

std::string to_string(client_error_code value);

/// \brief Address of the Manager-owned SHM control server.
struct client_options {
    std::string server_name;
    bool server_abstract_namespace{true};
};

/// \brief File descriptors received from an accepted handshake.
struct owned_fd_bundle {
    event::unique_fd publication;
    event::unique_fd release;
    event::unique_fd broadcast;
    event::unique_fd ack;
};

/// \brief Parsed and validated accepted handshake response.
struct accepted_handshake {
    handshake_response response;
    topic_mapping mapping;
    owned_fd_bundle fds;
    std::optional<join_state> state;
    std::optional<join_cursor> cursor;
    std::optional<std::string> retained_payload;
    /// Coordinator-assigned subscriber id for the shared-memory progress table.
    std::optional<std::uint32_t> subscriber_id;
};

/// \brief Parsed handshake rejection returned by the server.
struct rejected_handshake {
    handshake_response response;
    error_code error;
    std::string message;
};

/// \brief Local client-side protocol or socket error.
struct client_error {
    client_error_code code{client_error_code::invalid_response};
    std::string message;
};

/// \brief Result of a handshake request.
struct client_handshake_result {
    std::optional<accepted_handshake> accepted;
    std::optional<rejected_handshake> rejected;
    std::optional<client_error> error;

    bool is_accepted() const;
    bool is_rejected() const;
    bool is_error() const;
};

/// \brief Accepted topic-list response.
struct accepted_topic_list {
    list_topics_response response;
    std::vector<std::string> topics;
};

/// \brief Result of a topic-list request.
struct topic_list_result {
    std::optional<accepted_topic_list> accepted;
    std::optional<rejected_handshake> rejected;
    std::optional<client_error> error;

    bool is_accepted() const;
    bool is_rejected() const;
    bool is_error() const;
};

/// \brief Accepted unsubscribe response.
struct accepted_unsubscribe {
    unsubscribe_response response;
    std::string topic;
};

/// \brief Result of an unsubscribe request.
struct unsubscribe_result {
    std::optional<accepted_unsubscribe> accepted;
    std::optional<rejected_handshake> rejected;
    std::optional<client_error> error;

    bool is_accepted() const;
    bool is_rejected() const;
    bool is_error() const;
};

/// \brief Persistent control-plane session for one `shm_client` process.
///
/// Every handshake, topic-list, and unsubscribe request travels over one persistent UDS
/// `SOCK_SEQPACKET` stream. The server keeps the connection open for the lifetime of
/// the client; EOF/POLLHUP on the underlying FD is the server-loss signal. All
/// registrations made through this session are tied to the connection and released
/// together when the connection closes.
class session {
public:
    session();
    explicit session(const client_options& options);
    ~session();

    session(const session&) = delete;
    session& operator=(const session&) = delete;
    session(session&&) noexcept;
    session& operator=(session&&) noexcept;

    /// Open the persistent control connection. Equivalent to constructing with options.
    bool open(const client_options& options);
    void close();
    bool is_open() const;

    /// Raw connection FD; suitable for event-loop POLLHUP monitoring. Returns -1 when the
    /// session is closed. Caller MUST NOT close the FD; ownership stays with the session.
    int fd() const;

    /// Reason the session became closed (server-loss vs. clean shutdown), or empty when
    /// `is_open()` is true.
    const std::string& error_message() const;

    /// Send a handshake over the persistent connection and receive the response.
    client_handshake_result handshake(const handshake_request& request);
    client_handshake_result handshake(const std::string& client_id, const std::string& topic, role topic_role);

    /// Send a `list_topics` request over the persistent connection and receive the response.
    topic_list_result list_topics(const list_topics_request& request);
    topic_list_result list_topics(const std::string& client_id);

    /// Send an `unsubscribe` request over the persistent connection and receive the response.
    unsubscribe_result unsubscribe(const unsubscribe_request& request);
    unsubscribe_result unsubscribe(const std::string& client_id, const std::string& topic);

private:
    struct impl;
    std::unique_ptr<impl> m_impl;
};

/// \brief Convenience: open a one-shot session, send the handshake, close.
///
/// Closes the session immediately after the response is received, which means the server
/// will tear down the registration as soon as it notices the POLLHUP. Suitable for tests
/// or scratch tooling that only needs to verify the protocol; production callers should
/// keep a long-lived `session` so registrations survive the call.
client_handshake_result request_handshake(const client_options& options, const handshake_request& request);
client_handshake_result request_handshake(const client_options& options, const std::string& client_id,
                                          const std::string& topic, role topic_role);

topic_list_result request_topic_list(const client_options& options, const list_topics_request& request);
topic_list_result request_topic_list(const client_options& options, const std::string& client_id);

} // namespace everest::lib::io::shm::control
