// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <sys/socket.h>
#include <unordered_map>
#include <utility>
#include <vector>

#include <everest/io/event/event_fd.hpp>
#include <everest/io/event/unique_fd.hpp>
#include <everest/io/shm/control_protocol.hpp>
#include <everest/io/shm/coordinator.hpp>

namespace everest::lib::io::shm::control {

/// \brief Manager-side SHM control server.
///
/// The server listens on a UDS `SOCK_SEQPACKET` socket, accepts persistent client
/// sessions, serves topic-registration requests, transfers duplicated event file
/// descriptors with accepted handshakes, and cleans up all registrations owned by a
/// client when the control connection closes.
class server {
public:
    /// \brief Manager-owned endpoint data for one registered topic.
    struct topic_endpoint {
        std::string shm_name;
        std::uint64_t ring_offset{0};
        std::uint32_t total_slots{0};
        std::uint32_t slot_size{0};
        coordinator* topic_coordinator{nullptr};
    };

    /// Stable identifier for an accepted client connection. Each `shm_client` is one
    /// connection; all its subscriber/publisher registrations are owned by this id and
    /// released together when the connection closes (EOF/POLLHUP on the underlying
    /// SEQPACKET socket).
    using client_session_id = std::uint64_t;

    /// \brief Point-in-time server-side subscriber state.
    struct subscriber_snapshot {
        std::string topic;
        std::string client_id;
        coordinator::subscriber_id subscriber{0};
        join_state state{join_state::active};
        /// Persistent control connection FD that owns this subscriber registration. EOF/POLLHUP
        /// on this FD triggers removal of every snapshot that shares the same value.
        int connection_fd{event::unique_fd::NO_DESCRIPTOR_SENTINEL};
        client_session_id session_id{0};
        const coordinator* topic_coordinator{nullptr};
    };

    server() = default;
    ~server();

    server(const server&) = delete;
    server& operator=(const server&) = delete;
    server(server&&) = delete;
    server& operator=(server&&) = delete;

    bool open(const std::string& path, bool abstract_namespace = true);
    void close(bool unlink_filesystem_socket = true);
    bool is_open() const;
    const std::string& error_message() const;
    /// Listening UDS SEQPACKET socket FD. Caller-owned event loops watch this FD for
    /// `POLLIN` to detect newly-arriving client connections.
    int socket_fd() const;

    void register_topic(const std::string& topic, topic_endpoint endpoint);
    void set_retained_payload(const std::string& topic, std::string payload);
    void clear_retained_payload(const std::string& topic);
    void clear_retained_payloads();

    /// \brief Return a read-only snapshot of subscribers currently registered for one topic.
    std::vector<subscriber_snapshot> subscribers_for_topic(const std::string& topic) const;

    /// \brief Return read-only snapshots for subscribers grouped by their registered topic.
    std::unordered_map<std::string, std::vector<subscriber_snapshot>> subscriber_snapshots() const;

    /// \brief Response envelope produced by handle_client() and handle_next_message().
    struct handled_message {
        std::optional<handshake_response> handshake;
        std::optional<list_topics_response> list_topics;
        std::optional<unsubscribe_response> unsubscribe;
    };

    /// \brief Accept any pending client connections on the listening socket.
    /// \returns Raw FDs for newly accepted client connections. The server owns the FDs; the
    /// caller MUST register them for read/POLLHUP in its event loop so subsequent control
    /// messages on those connections drive `handle_client()` calls.
    std::vector<int> accept_pending_connections();

    /// \brief Drain one message from the given client connection FD.
    ///
    /// \returns The dispatched response envelope when one was sent, or std::nullopt if the
    /// connection has nothing pending. The session is closed (and the FD removed from the
    /// server's bookkeeping) when the peer hung up or sent malformed traffic; the caller
    /// should unregister the FD when this method reports `session_closed`.
    /// \brief State transition observed while handling one client message.
    enum class client_message_status {
        consumed,
        no_data,
        session_closed,
    };

    /// \brief Result of draining one client control message.
    struct client_message_result {
        client_message_status status{client_message_status::no_data};
        std::optional<handled_message> message;
    };

    client_message_result handle_client(int connection_fd);

    /// \brief Backwards-compatible single-step handle that polls the listener and all
    /// accepted client connections for one message.
    ///
    /// Used by unit tests that drive the server one message at a time. Production callers
    /// integrate the listener and per-connection FDs into an event loop and call
    /// `accept_pending_connections()` / `handle_client()` directly.
    std::optional<handled_message> handle_next_message();

    /// \brief Backwards-compatible accessor that returns only the handshake half of the
    /// dispatched envelope.
    std::optional<handshake_response> handle_next();

    /// \brief Return all topic names currently registered with this server.
    std::vector<std::string> registered_topics() const;

    /// \brief Wake each persistent client session that owns one of the given topic subscribers.
    ///
    /// The coordinator calls this after publishing SHM dispatched_count updates. All
    /// subscriptions owned by the same persistent shm_client session share one wake channel, so
    /// this writes at most one eventfd credit per affected client session even if multiple local
    /// subscriptions were dispatched in the same batch.
    std::size_t wake_subscriber_clients(const std::string& topic,
                                        const std::vector<coordinator::subscriber_id>& subscribers);

    /// \brief Active accepted-connection FDs, one per persistent client session.
    std::vector<int> active_client_fds() const;

    /// \brief Sweep accepted-connection FDs for POLLHUP/EOF and clean up the owning
    /// subscribers (and release any publishers blocked on those subscribers' acks).
    /// \returns Vector of closed connection FDs whose subscribers were removed. The caller
    /// should unregister these FDs from its event loop after this call.
    std::vector<int> cleanup_disconnected_clients();

    /// \brief Close a single client session by FD (used by event-loop hangup callbacks).
    /// \returns True if a session for the given FD existed and was closed.
    bool close_client(int connection_fd);

private:
    struct client_session {
        client_session_id id{0};
        event::unique_fd socket;
        std::shared_ptr<event::event_fd_base> broadcast_wake;
    };

    struct received_datagram {
        std::string payload;
        std::vector<event::unique_fd> fds;
        bool peer_hung_up{false};
        bool malformed{false};
    };

    struct subscriber_client {
        std::string client_id;
        std::string topic;
        coordinator::subscriber_id subscriber{0};
        join_state state{join_state::active};
        coordinator* topic_coordinator{nullptr};
        client_session_id session_id{0};
        int connection_fd{event::unique_fd::NO_DESCRIPTOR_SENTINEL};
    };

    struct handshake_transaction {
        handshake_response response;
        std::optional<coordinator::subscriber_id> added_subscriber;
        coordinator* subscriber_coordinator{nullptr};
    };

    std::optional<received_datagram> receive_request(int connection_fd) const;
    handshake_transaction build_response(const handshake_request& request, std::vector<event::unique_fd>& response_fds,
                                         const client_session& session);
    bool send_response(int connection_fd, const handshake_response& response,
                       const std::vector<event::unique_fd>& fds) const;
    list_topics_response build_list_topics_response(const list_topics_request& request) const;
    bool send_list_topics_response(int connection_fd, const list_topics_response& response) const;
    unsubscribe_response build_unsubscribe_response(const unsubscribe_request& request, const client_session& session);
    bool send_unsubscribe_response(int connection_fd, const unsubscribe_response& response) const;
    void remember_subscriber_client(const handshake_request& request, handshake_transaction& transaction,
                                    const client_session& session);
    void forget_all_subscriber_clients();
    void remove_session_registrations(const client_session& session);
    void close_session(client_session& session);
    static subscriber_snapshot make_subscriber_snapshot(const subscriber_client& client);
    static bool is_session_disconnected(int fd);

    event::unique_fd m_socket;
    std::string m_path;
    std::string m_error_message;
    bool m_abstract_namespace{true};
    std::unordered_map<std::string, topic_endpoint> m_topics;
    std::unordered_map<std::string, std::string> m_retained_payloads;
    std::unordered_map<std::string, std::vector<subscriber_client>> m_subscribers_by_topic;
    /// Persistent client sessions keyed by accepted-connection FD value. Each session owns
    /// the SCM_RIGHTS-passed FD plus a stable session id for tagging subscriber registrations.
    std::unordered_map<int, client_session> m_clients;
    client_session_id m_next_session_id{1};
};

} // namespace everest::lib::io::shm::control
