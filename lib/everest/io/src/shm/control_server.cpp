// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/io/shm/control_server.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <poll.h>
#include <stdexcept>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <utility>

#include <everest/io/socket/socket.hpp>

namespace everest::lib::io::shm::control {

namespace {
constexpr std::size_t max_control_payload_size = 4096;
constexpr std::size_t max_control_request_fds = 4;

join_state to_control_join_state(coordinator::subscriber_join_state state) {
    switch (state) {
    case coordinator::subscriber_join_state::active:
        return join_state::active;
    case coordinator::subscriber_join_state::pending:
        return join_state::pending;
    }
    throw std::invalid_argument("Unknown SHM coordinator subscriber join state");
}

handshake_response make_error_response(const handshake_request& request, error_code error, std::string message) {
    handshake_response response;
    response.version = protocol_version;
    response.accepted = false;
    response.topic = request.topic;
    response.topic_role = request.topic_role;
    response.error = error;
    response.message = std::move(message);
    return response;
}

handshake_response make_parse_error_response(std::string message) {
    handshake_response response;
    response.version = protocol_version;
    response.accepted = false;
    response.topic_role = role::subscriber;
    response.error = error_code::invalid_role;
    response.message = std::move(message);
    return response;
}

list_topics_response make_list_topics_error_response(error_code error, std::string message) {
    list_topics_response response;
    response.version = protocol_version;
    response.accepted = false;
    response.error = error;
    response.message = std::move(message);
    return response;
}

unsubscribe_response make_unsubscribe_error_response(const unsubscribe_request& request, error_code error,
                                                     std::string message) {
    unsubscribe_response response;
    response.version = protocol_version;
    response.accepted = false;
    response.topic = request.topic;
    response.error = error;
    response.message = std::move(message);
    return response;
}

event::unique_fd duplicate_fd(int fd) {
    const int dup_fd = ::dup(fd);
    if (dup_fd < 0) {
        throw std::runtime_error("Failed to duplicate SHM control eventfd");
    }
    return event::unique_fd{dup_fd};
}
} // namespace

server::~server() {
    close();
}

bool server::open(const std::string& path, bool abstract_namespace) {
    close();
    m_error_message.clear();
    try {
        m_socket = socket::open_uds_seqpacket_server_socket(path, abstract_namespace);
    } catch (const std::exception& e) {
        m_error_message = e.what();
        m_socket = event::unique_fd{};
    }
    m_path = path;
    m_abstract_namespace = abstract_namespace;
    return m_socket.is_fd();
}

void server::close(bool unlink_filesystem_socket) {
    forget_all_subscriber_clients();
    m_clients.clear();
    m_socket.close();
    if (unlink_filesystem_socket && !m_abstract_namespace && !m_path.empty()) {
        ::unlink(m_path.c_str());
    }
    m_path.clear();
}

bool server::is_open() const {
    return m_socket.is_fd();
}

const std::string& server::error_message() const {
    return m_error_message;
}

int server::socket_fd() const {
    return static_cast<int>(m_socket);
}

void server::register_topic(const std::string& topic, topic_endpoint endpoint) {
    if (endpoint.topic_coordinator == nullptr) {
        throw std::invalid_argument("SHM control topic endpoint needs a coordinator");
    }
    if (const auto previous = m_topics.find(topic);
        previous != m_topics.end() && previous->second.topic_coordinator != endpoint.topic_coordinator) {
        auto subscribers = m_subscribers_by_topic.find(topic);
        if (subscribers != m_subscribers_by_topic.end()) {
            for (auto& client : subscribers->second) {
                if (client.topic_coordinator != nullptr) {
                    client.topic_coordinator->remove_subscriber(client.subscriber);
                }
            }
            subscribers->second.clear();
        }
    }
    m_topics[topic] = std::move(endpoint);
    m_subscribers_by_topic.try_emplace(topic);
}

void server::set_retained_payload(const std::string& topic, std::string payload) {
    if (m_topics.find(topic) == m_topics.end()) {
        return;
    }
    m_retained_payloads[topic] = std::move(payload);
}

void server::clear_retained_payload(const std::string& topic) {
    m_retained_payloads.erase(topic);
}

void server::clear_retained_payloads() {
    m_retained_payloads.clear();
}

std::vector<server::subscriber_snapshot> server::subscribers_for_topic(const std::string& topic) const {
    std::vector<subscriber_snapshot> snapshots;
    const auto subscribers = m_subscribers_by_topic.find(topic);
    if (subscribers == m_subscribers_by_topic.end()) {
        return snapshots;
    }

    snapshots.reserve(subscribers->second.size());
    for (const auto& client : subscribers->second) {
        snapshots.push_back(make_subscriber_snapshot(client));
    }
    return snapshots;
}

std::unordered_map<std::string, std::vector<server::subscriber_snapshot>> server::subscriber_snapshots() const {
    std::unordered_map<std::string, std::vector<subscriber_snapshot>> snapshots;
    snapshots.reserve(m_subscribers_by_topic.size());
    for (const auto& [topic, subscribers] : m_subscribers_by_topic) {
        auto& topic_snapshots = snapshots[topic];
        topic_snapshots.reserve(subscribers.size());
        for (const auto& client : subscribers) {
            topic_snapshots.push_back(make_subscriber_snapshot(client));
        }
    }
    return snapshots;
}

std::vector<int> server::accept_pending_connections() {
    std::vector<int> accepted_fds;
    if (!is_open()) {
        return accepted_fds;
    }

    while (true) {
        const int accepted = ::accept4(m_socket, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
        if (accepted < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                break;
            }
            // Persistent error: stop trying for this batch.
            break;
        }
        client_session session;
        session.id = m_next_session_id++;
        session.socket = event::unique_fd{accepted};
        session.broadcast_wake = std::make_shared<event::event_fd_base>(0, EFD_NONBLOCK);
        m_clients.emplace(accepted, std::move(session));
        accepted_fds.push_back(accepted);
    }
    return accepted_fds;
}

std::optional<server::handled_message> server::handle_next_message() {
    if (!is_open()) {
        return std::nullopt;
    }

    accept_pending_connections();

    // Poll every active client connection until one has a message ready.
    for (auto fd : active_client_fds()) {
        if (m_clients.find(fd) == m_clients.end()) {
            continue;
        }
        pollfd descriptor{};
        descriptor.fd = fd;
        descriptor.events = POLLIN;
        const auto ready = ::poll(&descriptor, 1, 0);
        if (ready <= 0) {
            continue;
        }
        if (descriptor.revents & (POLLERR | POLLNVAL)) {
            close_client(fd);
            continue;
        }
        const auto result = handle_client(fd);
        if (result.status == client_message_status::consumed) {
            return result.message;
        }
        if (result.status == client_message_status::session_closed) {
            continue;
        }
    }

    // Nothing on existing sessions — accept another wave in case a connection arrived
    // mid-poll, then return the first message available.
    auto accepted = accept_pending_connections();
    for (auto fd : accepted) {
        const auto result = handle_client(fd);
        if (result.status == client_message_status::consumed) {
            return result.message;
        }
        if (result.status == client_message_status::session_closed) {
            continue;
        }
    }
    return std::nullopt;
}

std::optional<handshake_response> server::handle_next() {
    auto message = handle_next_message();
    if (!message.has_value()) {
        return std::nullopt;
    }
    return message->handshake;
}

server::client_message_result server::handle_client(int connection_fd) {
    client_message_result result;
    auto session = m_clients.find(connection_fd);
    if (session == m_clients.end()) {
        result.status = client_message_status::no_data;
        return result;
    }

    auto datagram = receive_request(connection_fd);
    if (!datagram.has_value()) {
        result.status = client_message_status::no_data;
        return result;
    }
    if (datagram->peer_hung_up) {
        close_client(connection_fd);
        result.status = client_message_status::session_closed;
        return result;
    }

    handled_message handled;
    request_kind kind = request_kind::handshake;
    nlohmann::json parsed;
    bool parse_ok = false;
    std::string parse_error_message;
    if (!datagram->malformed) {
        try {
            parsed = nlohmann::json::parse(datagram->payload);
            kind = request_kind_of(parsed);
            parse_ok = true;
        } catch (const std::exception& e) {
            parse_error_message = e.what();
        }
    } else {
        parse_error_message = "malformed SHM control frame";
    }

    if (!parse_ok || kind == request_kind::handshake) {
        std::vector<event::unique_fd> response_fds;
        handshake_transaction transaction;
        std::optional<handshake_request> request;
        try {
            if (!parse_ok) {
                throw std::runtime_error(parse_error_message);
            }
            request = parsed.get<handshake_request>();
            transaction = build_response(request.value(), response_fds, session->second);
        } catch (const std::exception& e) {
            transaction.response = make_parse_error_response(e.what());
        }

        if (!send_response(connection_fd, transaction.response, response_fds)) {
            if (transaction.added_subscriber.has_value() && transaction.subscriber_coordinator != nullptr) {
                transaction.subscriber_coordinator->remove_subscriber(transaction.added_subscriber.value());
            }
            // Sending failed — assume connection is broken and close it.
            close_client(connection_fd);
            handled.handshake = transaction.response;
            result.message = std::move(handled);
            result.status = client_message_status::session_closed;
            return result;
        }
        if (request.has_value()) {
            remember_subscriber_client(request.value(), transaction, session->second);
        }

        handled.handshake = transaction.response;
        result.message = std::move(handled);
        result.status = client_message_status::consumed;
        return result;
    }

    if (kind == request_kind::list_topics) {
        list_topics_response response;
        try {
            const auto request = parsed.get<list_topics_request>();
            response = build_list_topics_response(request);
        } catch (const std::exception& e) {
            response = make_list_topics_error_response(error_code::invalid_role, e.what());
        }

        if (!send_list_topics_response(connection_fd, response)) {
            close_client(connection_fd);
            handled.list_topics = response;
            result.message = std::move(handled);
            result.status = client_message_status::session_closed;
            return result;
        }
        handled.list_topics = response;
        result.message = std::move(handled);
        result.status = client_message_status::consumed;
        return result;
    }

    unsubscribe_response response;
    try {
        const auto request = parsed.get<unsubscribe_request>();
        response = build_unsubscribe_response(request, session->second);
    } catch (const std::exception& e) {
        unsubscribe_request request;
        if (parsed.contains("topic")) {
            request.topic = parsed.at("topic").get<std::string>();
        }
        response = make_unsubscribe_error_response(request, error_code::invalid_role, e.what());
    }

    if (!send_unsubscribe_response(connection_fd, response)) {
        close_client(connection_fd);
        handled.unsubscribe = response;
        result.message = std::move(handled);
        result.status = client_message_status::session_closed;
        return result;
    }
    handled.unsubscribe = response;
    result.message = std::move(handled);
    result.status = client_message_status::consumed;
    return result;
}

list_topics_response server::build_list_topics_response(const list_topics_request& request) const {
    if (request.version != protocol_version) {
        return make_list_topics_error_response(error_code::incompatible_version, "incompatible SHM control version");
    }

    list_topics_response response;
    response.version = protocol_version;
    response.accepted = true;
    response.topics = registered_topics();
    return response;
}

bool server::send_list_topics_response(int connection_fd, const list_topics_response& response) const {
    const auto payload = nlohmann::json(response).dump();

    iovec iov{};
    iov.iov_base = const_cast<char*>(payload.data());
    iov.iov_len = payload.size();

    msghdr message{};
    message.msg_iov = &iov;
    message.msg_iovlen = 1;

    const auto bytes = ::sendmsg(connection_fd, &message, MSG_NOSIGNAL | MSG_DONTWAIT);
    return bytes == static_cast<ssize_t>(payload.size());
}

unsubscribe_response server::build_unsubscribe_response(const unsubscribe_request& request,
                                                        const client_session& session) {
    if (request.version != protocol_version) {
        return make_unsubscribe_error_response(request, error_code::incompatible_version,
                                               "incompatible SHM control version");
    }
    if (m_topics.find(request.topic) == m_topics.end()) {
        return make_unsubscribe_error_response(request, error_code::unknown_topic, "SHM topic is not registered");
    }

    auto& topic_subscribers = m_subscribers_by_topic[request.topic];
    auto subscriber = topic_subscribers.begin();
    while (subscriber != topic_subscribers.end()) {
        if (subscriber->session_id != session.id || subscriber->client_id != request.client_id) {
            ++subscriber;
            continue;
        }
        if (subscriber->topic_coordinator != nullptr) {
            subscriber->topic_coordinator->remove_subscriber(subscriber->subscriber);
        }
        subscriber = topic_subscribers.erase(subscriber);
    }

    unsubscribe_response response;
    response.version = protocol_version;
    response.accepted = true;
    response.topic = request.topic;
    return response;
}

bool server::send_unsubscribe_response(int connection_fd, const unsubscribe_response& response) const {
    const auto payload = nlohmann::json(response).dump();

    iovec iov{};
    iov.iov_base = const_cast<char*>(payload.data());
    iov.iov_len = payload.size();

    msghdr message{};
    message.msg_iov = &iov;
    message.msg_iovlen = 1;

    const auto bytes = ::sendmsg(connection_fd, &message, MSG_NOSIGNAL | MSG_DONTWAIT);
    return bytes == static_cast<ssize_t>(payload.size());
}

std::vector<std::string> server::registered_topics() const {
    std::vector<std::string> topics;
    topics.reserve(m_topics.size());
    for (const auto& [name, endpoint] : m_topics) {
        (void)endpoint;
        topics.push_back(name);
    }
    std::sort(topics.begin(), topics.end());
    return topics;
}

std::size_t server::wake_subscriber_clients(const std::string& topic,
                                            const std::vector<coordinator::subscriber_id>& subscribers) {
    const auto topic_subscribers = m_subscribers_by_topic.find(topic);
    if (topic_subscribers == m_subscribers_by_topic.end() || subscribers.empty()) {
        return 0;
    }

    std::vector<client_session_id> woken_sessions;
    woken_sessions.reserve(subscribers.size());
    std::size_t woken = 0;
    for (const auto& subscriber : topic_subscribers->second) {
        if (!std::binary_search(subscribers.begin(), subscribers.end(), subscriber.subscriber)) {
            continue;
        }
        if (std::find(woken_sessions.begin(), woken_sessions.end(), subscriber.session_id) != woken_sessions.end()) {
            continue;
        }
        woken_sessions.push_back(subscriber.session_id);
        const auto session = std::find_if(m_clients.begin(), m_clients.end(),
                                          [&](const auto& item) { return item.second.id == subscriber.session_id; });
        if (session == m_clients.end() || session->second.broadcast_wake == nullptr) {
            continue;
        }
        if (session->second.broadcast_wake->notify()) {
            ++woken;
        }
    }
    return woken;
}

std::vector<int> server::active_client_fds() const {
    std::vector<int> fds;
    fds.reserve(m_clients.size());
    for (const auto& [fd, session] : m_clients) {
        (void)session;
        fds.push_back(fd);
    }
    std::sort(fds.begin(), fds.end());
    return fds;
}

std::vector<int> server::cleanup_disconnected_clients() {
    std::vector<int> removed_fds;
    std::vector<int> candidate_fds;
    candidate_fds.reserve(m_clients.size());
    for (const auto& [fd, session] : m_clients) {
        (void)session;
        candidate_fds.push_back(fd);
    }
    for (auto fd : candidate_fds) {
        auto it = m_clients.find(fd);
        if (it == m_clients.end()) {
            continue;
        }
        if (is_session_disconnected(fd)) {
            remove_session_registrations(it->second);
            it->second.socket.close();
            m_clients.erase(it);
            removed_fds.push_back(fd);
        }
    }
    return removed_fds;
}

bool server::close_client(int connection_fd) {
    auto it = m_clients.find(connection_fd);
    if (it == m_clients.end()) {
        return false;
    }
    remove_session_registrations(it->second);
    it->second.socket.close();
    m_clients.erase(it);
    return true;
}

std::optional<server::received_datagram> server::receive_request(int connection_fd) const {
    if (!is_open()) {
        return std::nullopt;
    }

    std::array<char, max_control_payload_size> buffer{};

    iovec iov{};
    iov.iov_base = buffer.data();
    iov.iov_len = buffer.size();

    msghdr message{};
    message.msg_iov = &iov;
    message.msg_iovlen = 1;
    std::array<char, CMSG_SPACE(sizeof(int) * max_control_request_fds)> control{};
    message.msg_control = control.data();
    message.msg_controllen = control.size();

    const auto bytes = ::recvmsg(connection_fd, &message, MSG_DONTWAIT);
    if (bytes < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return std::nullopt;
        }
        received_datagram closed;
        closed.peer_hung_up = true;
        return closed;
    }
    if (bytes == 0) {
        received_datagram closed;
        closed.peer_hung_up = true;
        return closed;
    }

    std::vector<event::unique_fd> fds;
    for (auto* cmsg = CMSG_FIRSTHDR(&message); cmsg != nullptr; cmsg = CMSG_NXTHDR(&message, cmsg)) {
        if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS) {
            continue;
        }
        const auto fd_count = (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int);
        const auto* raw_fds = reinterpret_cast<const int*>(CMSG_DATA(cmsg));
        for (std::size_t i = 0; i < fd_count; ++i) {
            fds.emplace_back(raw_fds[i]);
        }
    }

    received_datagram result;
    if (message.msg_flags & MSG_TRUNC) {
        result.malformed = true;
    }
    if (message.msg_flags & MSG_CTRUNC) {
        result.malformed = true;
    }
    result.payload = std::string(buffer.data(), static_cast<std::size_t>(bytes));
    result.fds = std::move(fds);
    return result;
}

server::handshake_transaction server::build_response(const handshake_request& request,
                                                     std::vector<event::unique_fd>& response_fds,
                                                     const client_session& session) {
    handshake_transaction transaction;

    if (request.version != protocol_version) {
        transaction.response =
            make_error_response(request, error_code::incompatible_version, "incompatible SHM control version");
        return transaction;
    }

    // Handshakes always target an exact registered topic. Wildcard expansion happens above
    // this layer (framework transport, bridge) after discovering the registry via the
    // list_topics control message.
    const auto topic = m_topics.find(request.topic);
    if (topic == m_topics.end()) {
        transaction.response = make_error_response(request, error_code::unknown_topic, "SHM topic is not registered");
        return transaction;
    }

    const auto& endpoint = topic->second;
    auto& response = transaction.response;
    response.version = protocol_version;
    response.accepted = true;
    response.topic = request.topic;
    response.topic_role = request.topic_role;
    response.mapping = topic_mapping{endpoint.shm_name, endpoint.ring_offset, endpoint.total_slots, endpoint.slot_size};
    response.fds = fd_bundle{};

    if (request.topic_role == role::publisher || request.topic_role == role::publisher_subscriber) {
        response.fds->publication = static_cast<std::uint32_t>(response_fds.size());
        response_fds.push_back(endpoint.topic_coordinator->make_publication_fd());
        response.fds->release = static_cast<std::uint32_t>(response_fds.size());
        response_fds.push_back(endpoint.topic_coordinator->make_release_fd());
    }

    if (request.topic_role == role::subscriber || request.topic_role == role::publisher_subscriber) {
        coordinator::subscriber_registration registration;
        try {
            registration = endpoint.topic_coordinator->add_subscriber();
        } catch (const std::exception& e) {
            // Cap exhaustion (shm_max_subscribers_per_topic) reaches us as a runtime_error from
            // the coordinator. Convert to a clean handshake rejection so a misbehaving module
            // that keeps subscribing cannot leak the exception past the per-handshake boundary.
            response_fds.clear();
            transaction = handshake_transaction{};
            transaction.response =
                make_error_response(request, error_code::resource_error,
                                    std::string("SHM coordinator could not register subscriber: ") + e.what());
            return transaction;
        }
        transaction.added_subscriber = registration.id;
        transaction.subscriber_coordinator = endpoint.topic_coordinator;
        response.state = to_control_join_state(registration.state);
        response.cursor = join_cursor{registration.cursor.write_idx, registration.cursor.sequence};
        response.subscriber_id = static_cast<std::uint32_t>(registration.id);
        if (const auto retained = m_retained_payloads.find(request.topic); retained != m_retained_payloads.end()) {
            response.retained_payload = retained->second;
        }
        try {
            response.fds->broadcast = static_cast<std::uint32_t>(response_fds.size());
            response_fds.push_back(duplicate_fd(session.broadcast_wake->get_raw_fd()));
            response.fds->ack = static_cast<std::uint32_t>(response_fds.size());
            response_fds.push_back(endpoint.topic_coordinator->make_ack_fd(registration.id));
        } catch (...) {
            endpoint.topic_coordinator->remove_subscriber(registration.id);
            throw;
        }
    }

    return transaction;
}

bool server::send_response(int connection_fd, const handshake_response& response,
                           const std::vector<event::unique_fd>& fds) const {
    const auto payload = nlohmann::json(response).dump();

    iovec iov{};
    iov.iov_base = const_cast<char*>(payload.data());
    iov.iov_len = payload.size();

    msghdr message{};
    message.msg_iov = &iov;
    message.msg_iovlen = 1;

    std::vector<char> control_buffer;
    if (!fds.empty()) {
        control_buffer.resize(CMSG_SPACE(sizeof(int) * fds.size()));
        message.msg_control = control_buffer.data();
        message.msg_controllen = control_buffer.size();
        auto* cmsg = CMSG_FIRSTHDR(&message);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_len = CMSG_LEN(sizeof(int) * fds.size());

        std::vector<int> raw_fds;
        raw_fds.reserve(fds.size());
        for (const auto& fd : fds) {
            raw_fds.push_back(fd);
        }
        std::memcpy(CMSG_DATA(cmsg), raw_fds.data(), sizeof(int) * raw_fds.size());
    }

    const auto bytes = ::sendmsg(connection_fd, &message, MSG_NOSIGNAL | MSG_DONTWAIT);
    return bytes == static_cast<ssize_t>(payload.size());
}

void server::remember_subscriber_client(const handshake_request& request, handshake_transaction& transaction,
                                        const client_session& session) {
    if (!transaction.added_subscriber.has_value() || transaction.subscriber_coordinator == nullptr) {
        return;
    }

    auto& topic_subscribers = m_subscribers_by_topic[request.topic];
    topic_subscribers.push_back(
        subscriber_client{request.client_id, request.topic, transaction.added_subscriber.value(),
                          transaction.response.state.value_or(join_state::active), transaction.subscriber_coordinator,
                          session.id, static_cast<int>(session.socket)});
}

void server::forget_all_subscriber_clients() {
    for (auto& topic_subscribers : m_subscribers_by_topic) {
        for (auto& client : topic_subscribers.second) {
            if (client.topic_coordinator != nullptr) {
                client.topic_coordinator->remove_subscriber(client.subscriber);
            }
        }
        topic_subscribers.second.clear();
    }
}

void server::remove_session_registrations(const client_session& session) {
    for (auto& topic_subscribers : m_subscribers_by_topic) {
        auto subscriber = topic_subscribers.second.begin();
        while (subscriber != topic_subscribers.second.end()) {
            if (subscriber->session_id != session.id) {
                ++subscriber;
                continue;
            }
            if (subscriber->topic_coordinator != nullptr) {
                subscriber->topic_coordinator->remove_subscriber(subscriber->subscriber);
            }
            subscriber = topic_subscribers.second.erase(subscriber);
        }
    }
}

void server::close_session(client_session& session) {
    remove_session_registrations(session);
    session.socket.close();
}

server::subscriber_snapshot server::make_subscriber_snapshot(const subscriber_client& client) {
    auto state = client.state;
    if (client.topic_coordinator != nullptr) {
        state = to_control_join_state(client.topic_coordinator->subscriber_join_status(client.subscriber));
    }
    return subscriber_snapshot{client.topic,         client.client_id,  client.subscriber,       state,
                               client.connection_fd, client.session_id, client.topic_coordinator};
}

bool server::is_session_disconnected(int fd) {
    // A live idle SOCK_SEQPACKET connection returns EAGAIN here. A peer that closed the
    // persistent control session returns EOF. Probe first so cleanup does not depend on a
    // specific poll() revents shape for UDS EOF.
    std::array<char, 16> probe{};
    const auto peeked = ::recv(fd, probe.data(), probe.size(), MSG_PEEK | MSG_DONTWAIT);
    if (peeked == 0) {
        return true;
    }
    if (peeked < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            return true;
        }
    }

    return false;
}

} // namespace everest::lib::io::shm::control
