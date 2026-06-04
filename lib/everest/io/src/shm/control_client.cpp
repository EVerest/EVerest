// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/io/shm/control_client.hpp>

#include <array>
#include <cerrno>
#include <cstring>
#include <set>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>
#include <vector>

#include <everest/io/socket/socket.hpp>

namespace everest::lib::io::shm::control {

namespace {
constexpr std::size_t max_control_payload_size = 1024 * 1024;
constexpr std::size_t max_control_response_fds = 5;

bool is_publisher_capable(role topic_role) {
    return topic_role == role::publisher || topic_role == role::publisher_subscriber;
}

bool is_subscriber_capable(role topic_role) {
    return topic_role == role::subscriber || topic_role == role::publisher_subscriber;
}

std::string errno_message(const std::string& prefix) {
    return prefix + ": " + std::string(std::strerror(errno));
}

client_handshake_result make_error(client_error_code code, std::string message) {
    client_handshake_result result;
    result.error = client_error{code, std::move(message)};
    return result;
}

topic_list_result make_topic_list_error(client_error_code code, std::string message) {
    topic_list_result result;
    result.error = client_error{code, std::move(message)};
    return result;
}

unsubscribe_result make_unsubscribe_error(client_error_code code, std::string message) {
    unsubscribe_result result;
    result.error = client_error{code, std::move(message)};
    return result;
}

bool send_request_frame(int socket_fd, const nlohmann::json& payload_json, std::string& error) {
    const auto payload = payload_json.dump();

    iovec iov{};
    iov.iov_base = const_cast<char*>(payload.data());
    iov.iov_len = payload.size();

    msghdr message{};
    message.msg_iov = &iov;
    message.msg_iovlen = 1;

    const auto bytes = ::sendmsg(socket_fd, &message, MSG_NOSIGNAL);
    if (bytes < 0) {
        error = errno_message("Failed to send SHM control request");
        return false;
    }
    if (bytes != static_cast<ssize_t>(payload.size())) {
        error = "Failed to send complete SHM control request";
        return false;
    }
    return true;
}

struct received_response {
    handshake_response response;
    std::vector<event::unique_fd> fds;
};

client_handshake_result receive_response(int socket_fd, received_response& response) {
    std::array<char, max_control_payload_size> payload{};
    std::array<char, CMSG_SPACE(sizeof(int) * max_control_response_fds)> control{};

    iovec iov{};
    iov.iov_base = payload.data();
    iov.iov_len = payload.size();

    msghdr message{};
    message.msg_iov = &iov;
    message.msg_iovlen = 1;
    message.msg_control = control.data();
    message.msg_controllen = control.size();

    const auto bytes = ::recvmsg(socket_fd, &message, 0);
    if (bytes < 0) {
        return make_error(client_error_code::socket_error,
                          errno_message("Failed to receive SHM control handshake response"));
    }
    if (bytes == 0) {
        return make_error(client_error_code::socket_error,
                          "SHM control server closed the connection before responding");
    }

    for (auto* cmsg = CMSG_FIRSTHDR(&message); cmsg != nullptr; cmsg = CMSG_NXTHDR(&message, cmsg)) {
        if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS) {
            continue;
        }
        const auto fd_count = (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int);
        const auto* raw_fds = reinterpret_cast<const int*>(CMSG_DATA(cmsg));
        for (std::size_t i = 0; i < fd_count; ++i) {
            response.fds.emplace_back(raw_fds[i]);
        }
    }

    if (message.msg_flags & MSG_TRUNC) {
        return make_error(client_error_code::malformed_response,
                          "SHM control handshake response exceeded maximum payload size");
    }
    if (message.msg_flags & MSG_CTRUNC) {
        return make_error(client_error_code::fd_mismatch,
                          "SHM control handshake response file descriptors were truncated");
    }

    try {
        response.response = nlohmann::json::parse(std::string(payload.data(), static_cast<std::size_t>(bytes)))
                                .get<handshake_response>();
    } catch (const std::exception& e) {
        return make_error(client_error_code::malformed_response,
                          "Malformed SHM control handshake response: " + std::string(e.what()));
    }

    return {};
}

bool validate_required_fd(const char* name, const std::optional<std::uint32_t>& index, std::size_t fd_count,
                          std::set<std::uint32_t>& seen, std::string& error) {
    if (!index.has_value()) {
        error = "SHM control handshake response is missing required " + std::string(name) + " FD index";
        return false;
    }
    if (index.value() >= fd_count) {
        error = "SHM control handshake response " + std::string(name) + " FD index is out of range";
        return false;
    }
    if (!seen.insert(index.value()).second) {
        error = "SHM control handshake response reuses FD index " + std::to_string(index.value());
        return false;
    }
    return true;
}

bool validate_optional_fd(const char* name, const std::optional<std::uint32_t>& index, std::size_t fd_count,
                          std::set<std::uint32_t>& seen, std::string& error) {
    if (!index.has_value()) {
        return true;
    }
    if (index.value() >= fd_count) {
        error = "SHM control handshake response " + std::string(name) + " FD index is out of range";
        return false;
    }
    if (!seen.insert(index.value()).second) {
        error = "SHM control handshake response reuses FD index " + std::to_string(index.value());
        return false;
    }
    return true;
}

client_handshake_result build_accepted_result(const handshake_request& request, received_response&& received) {
    const auto& response = received.response;
    if (response.version != protocol_version) {
        return make_error(client_error_code::invalid_response,
                          "SHM control handshake response has incompatible protocol version");
    }
    if (response.topic != request.topic) {
        return make_error(client_error_code::invalid_response, "SHM control handshake response topic does not match");
    }
    if (response.topic_role != request.topic_role) {
        return make_error(client_error_code::invalid_response, "SHM control handshake response role does not match");
    }
    if (!response.mapping.has_value()) {
        return make_error(client_error_code::invalid_response, "SHM control handshake response is missing mapping");
    }
    if (!response.fds.has_value()) {
        return make_error(client_error_code::invalid_response, "SHM control handshake response is missing FD bundle");
    }
    if (is_subscriber_capable(request.topic_role) && (!response.state.has_value() || !response.cursor.has_value())) {
        return make_error(client_error_code::invalid_response,
                          "Subscriber SHM control handshake response is missing join state or cursor");
    }

    const auto& bundle = response.fds.value();
    std::set<std::uint32_t> seen;
    std::string fd_error;
    if (is_publisher_capable(request.topic_role)) {
        if (!validate_required_fd("publication", bundle.publication, received.fds.size(), seen, fd_error) ||
            !validate_required_fd("release", bundle.release, received.fds.size(), seen, fd_error)) {
            return make_error(client_error_code::fd_mismatch, fd_error);
        }
    }
    if (is_subscriber_capable(request.topic_role)) {
        if (!validate_required_fd("broadcast", bundle.broadcast, received.fds.size(), seen, fd_error) ||
            !validate_required_fd("ack", bundle.ack, received.fds.size(), seen, fd_error)) {
            return make_error(client_error_code::fd_mismatch, fd_error);
        }
    }
    if (!is_publisher_capable(request.topic_role)) {
        if (!validate_optional_fd("publication", bundle.publication, received.fds.size(), seen, fd_error) ||
            !validate_optional_fd("release", bundle.release, received.fds.size(), seen, fd_error)) {
            return make_error(client_error_code::fd_mismatch, fd_error);
        }
    }
    if (!is_subscriber_capable(request.topic_role)) {
        if (!validate_optional_fd("broadcast", bundle.broadcast, received.fds.size(), seen, fd_error) ||
            !validate_optional_fd("ack", bundle.ack, received.fds.size(), seen, fd_error)) {
            return make_error(client_error_code::fd_mismatch, fd_error);
        }
    }

    accepted_handshake accepted;
    accepted.mapping = response.mapping.value();
    accepted.state = response.state;
    accepted.cursor = response.cursor;
    accepted.retained_payload = response.retained_payload;
    accepted.subscriber_id = response.subscriber_id;
    accepted.response = response;

    if (bundle.publication.has_value()) {
        accepted.fds.publication = std::move(received.fds.at(bundle.publication.value()));
    }
    if (bundle.release.has_value()) {
        accepted.fds.release = std::move(received.fds.at(bundle.release.value()));
    }
    if (bundle.broadcast.has_value()) {
        accepted.fds.broadcast = std::move(received.fds.at(bundle.broadcast.value()));
    }
    if (bundle.ack.has_value()) {
        accepted.fds.ack = std::move(received.fds.at(bundle.ack.value()));
    }

    client_handshake_result result;
    result.accepted = std::move(accepted);
    return result;
}

client_handshake_result build_handshake_result(const handshake_request& request, received_response&& received) {
    if (!received.response.accepted) {
        if (!received.response.error.has_value()) {
            return make_error(client_error_code::malformed_response,
                              "Rejected SHM control handshake response is missing server error code");
        }
        client_handshake_result result;
        result.rejected = rejected_handshake{received.response, received.response.error.value(),
                                             received.response.message.value_or("")};
        return result;
    }
    return build_accepted_result(request, std::move(received));
}

topic_list_result receive_topic_list_response(int socket_fd) {
    std::array<char, max_control_payload_size> payload{};

    iovec iov{};
    iov.iov_base = payload.data();
    iov.iov_len = payload.size();

    msghdr message{};
    message.msg_iov = &iov;
    message.msg_iovlen = 1;

    const auto bytes = ::recvmsg(socket_fd, &message, 0);
    if (bytes < 0) {
        return make_topic_list_error(client_error_code::socket_error,
                                     errno_message("Failed to receive SHM control list_topics response"));
    }
    if (bytes == 0) {
        return make_topic_list_error(client_error_code::socket_error,
                                     "SHM control server closed the connection before list_topics response");
    }
    if (message.msg_flags & MSG_TRUNC) {
        return make_topic_list_error(client_error_code::malformed_response,
                                     "SHM control list_topics response exceeded maximum payload size");
    }

    list_topics_response response;
    try {
        const auto parsed = nlohmann::json::parse(std::string(payload.data(), static_cast<std::size_t>(bytes)));
        if (request_kind_of(parsed) != request_kind::list_topics) {
            return make_topic_list_error(client_error_code::malformed_response,
                                         "SHM control response is not a list_topics response");
        }
        response = parsed.get<list_topics_response>();
    } catch (const std::exception& e) {
        return make_topic_list_error(client_error_code::malformed_response,
                                     "Malformed SHM control list_topics response: " + std::string(e.what()));
    }

    if (response.version != protocol_version) {
        return make_topic_list_error(client_error_code::invalid_response,
                                     "SHM control list_topics response has incompatible protocol version");
    }

    topic_list_result result;
    if (!response.accepted) {
        if (!response.error.has_value()) {
            return make_topic_list_error(client_error_code::malformed_response,
                                         "Rejected SHM control list_topics response is missing server error code");
        }
        handshake_response error_response;
        error_response.version = response.version;
        error_response.accepted = false;
        error_response.error = response.error;
        error_response.message = response.message;
        result.rejected =
            rejected_handshake{std::move(error_response), response.error.value(), response.message.value_or("")};
        return result;
    }

    accepted_topic_list accepted;
    accepted.topics = response.topics;
    accepted.response = std::move(response);
    result.accepted = std::move(accepted);
    return result;
}

unsubscribe_result receive_unsubscribe_response(int socket_fd) {
    std::array<char, max_control_payload_size> payload{};

    iovec iov{};
    iov.iov_base = payload.data();
    iov.iov_len = payload.size();

    msghdr message{};
    message.msg_iov = &iov;
    message.msg_iovlen = 1;

    const auto bytes = ::recvmsg(socket_fd, &message, 0);
    if (bytes < 0) {
        return make_unsubscribe_error(client_error_code::socket_error,
                                      errno_message("Failed to receive SHM control unsubscribe response"));
    }
    if (bytes == 0) {
        return make_unsubscribe_error(client_error_code::socket_error,
                                      "SHM control server closed the connection before unsubscribe response");
    }
    if (message.msg_flags & MSG_TRUNC) {
        return make_unsubscribe_error(client_error_code::malformed_response,
                                      "SHM control unsubscribe response exceeded maximum payload size");
    }

    unsubscribe_response response;
    try {
        const auto parsed = nlohmann::json::parse(std::string(payload.data(), static_cast<std::size_t>(bytes)));
        if (request_kind_of(parsed) != request_kind::unsubscribe) {
            return make_unsubscribe_error(client_error_code::malformed_response,
                                          "SHM control response is not an unsubscribe response");
        }
        response = parsed.get<unsubscribe_response>();
    } catch (const std::exception& e) {
        return make_unsubscribe_error(client_error_code::malformed_response,
                                      "Malformed SHM control unsubscribe response: " + std::string(e.what()));
    }

    if (response.version != protocol_version) {
        return make_unsubscribe_error(client_error_code::invalid_response,
                                      "SHM control unsubscribe response has incompatible protocol version");
    }

    unsubscribe_result result;
    if (!response.accepted) {
        if (!response.error.has_value()) {
            return make_unsubscribe_error(client_error_code::malformed_response,
                                          "Rejected SHM control unsubscribe response is missing server error code");
        }
        handshake_response error_response;
        error_response.version = response.version;
        error_response.accepted = false;
        error_response.topic = response.topic;
        error_response.error = response.error;
        error_response.message = response.message;
        result.rejected =
            rejected_handshake{std::move(error_response), response.error.value(), response.message.value_or("")};
        return result;
    }

    const auto topic = response.topic;
    result.accepted = accepted_unsubscribe{std::move(response), topic};
    return result;
}
} // namespace

std::string to_string(client_error_code value) {
    switch (value) {
    case client_error_code::socket_error:
        return "socket_error";
    case client_error_code::malformed_response:
        return "malformed_response";
    case client_error_code::invalid_response:
        return "invalid_response";
    case client_error_code::fd_mismatch:
        return "fd_mismatch";
    }
    throw std::invalid_argument("Unknown SHM control client error code");
}

bool client_handshake_result::is_accepted() const {
    return accepted.has_value();
}

bool client_handshake_result::is_rejected() const {
    return rejected.has_value();
}

bool client_handshake_result::is_error() const {
    return error.has_value();
}

bool topic_list_result::is_accepted() const {
    return accepted.has_value();
}

bool topic_list_result::is_rejected() const {
    return rejected.has_value();
}

bool topic_list_result::is_error() const {
    return error.has_value();
}

bool unsubscribe_result::is_accepted() const {
    return accepted.has_value();
}

bool unsubscribe_result::is_rejected() const {
    return rejected.has_value();
}

bool unsubscribe_result::is_error() const {
    return error.has_value();
}

struct session::impl {
    event::unique_fd socket;
    std::string error_message;
};

session::session() = default;

session::session(const client_options& options) : m_impl(std::make_unique<impl>()) {
    open(options);
}

session::~session() = default;

session::session(session&&) noexcept = default;
session& session::operator=(session&&) noexcept = default;

bool session::open(const client_options& options) {
    if (m_impl == nullptr) {
        m_impl = std::make_unique<impl>();
    }
    m_impl->error_message.clear();
    try {
        m_impl->socket =
            socket::open_uds_seqpacket_client_socket(options.server_name, options.server_abstract_namespace);
    } catch (const std::exception& e) {
        m_impl->error_message = e.what();
        m_impl->socket.close();
        return false;
    }
    return m_impl->socket.is_fd();
}

void session::close() {
    if (m_impl == nullptr) {
        return;
    }
    m_impl->socket.close();
}

bool session::is_open() const {
    return m_impl != nullptr && m_impl->socket.is_fd();
}

int session::fd() const {
    if (m_impl == nullptr) {
        return -1;
    }
    return static_cast<int>(m_impl->socket);
}

const std::string& session::error_message() const {
    static const std::string empty;
    if (m_impl == nullptr) {
        return empty;
    }
    return m_impl->error_message;
}

client_handshake_result session::handshake(const handshake_request& request) {
    if (!is_open()) {
        return make_error(client_error_code::socket_error, m_impl != nullptr && !m_impl->error_message.empty()
                                                               ? m_impl->error_message
                                                               : "SHM control session is not open");
    }

    std::string send_error;
    if (!send_request_frame(m_impl->socket, nlohmann::json(request), send_error)) {
        m_impl->error_message = send_error;
        m_impl->socket.close();
        return make_error(client_error_code::socket_error, send_error);
    }

    received_response received;
    auto receive_status = receive_response(m_impl->socket, received);
    if (receive_status.is_error()) {
        m_impl->error_message = receive_status.error->message;
        m_impl->socket.close();
        return receive_status;
    }

    return build_handshake_result(request, std::move(received));
}

client_handshake_result session::handshake(const std::string& client_id, const std::string& topic, role topic_role) {
    return handshake(handshake_request{protocol_version, client_id, topic, topic_role});
}

topic_list_result session::list_topics(const list_topics_request& request) {
    if (!is_open()) {
        return make_topic_list_error(client_error_code::socket_error,
                                     m_impl != nullptr && !m_impl->error_message.empty()
                                         ? m_impl->error_message
                                         : "SHM control session is not open");
    }

    std::string send_error;
    if (!send_request_frame(m_impl->socket, nlohmann::json(request), send_error)) {
        m_impl->error_message = send_error;
        m_impl->socket.close();
        return make_topic_list_error(client_error_code::socket_error, send_error);
    }

    auto response = receive_topic_list_response(m_impl->socket);
    if (response.is_error()) {
        m_impl->error_message = response.error->message;
        m_impl->socket.close();
    }
    return response;
}

topic_list_result session::list_topics(const std::string& client_id) {
    return list_topics(list_topics_request{protocol_version, client_id});
}

unsubscribe_result session::unsubscribe(const unsubscribe_request& request) {
    if (!is_open()) {
        return make_unsubscribe_error(client_error_code::socket_error,
                                      m_impl != nullptr && !m_impl->error_message.empty()
                                          ? m_impl->error_message
                                          : "SHM control session is not open");
    }

    std::string send_error;
    if (!send_request_frame(m_impl->socket, nlohmann::json(request), send_error)) {
        m_impl->error_message = send_error;
        m_impl->socket.close();
        return make_unsubscribe_error(client_error_code::socket_error, send_error);
    }

    auto response = receive_unsubscribe_response(m_impl->socket);
    if (response.is_error()) {
        m_impl->error_message = response.error->message;
        m_impl->socket.close();
    }
    return response;
}

unsubscribe_result session::unsubscribe(const std::string& client_id, const std::string& topic) {
    return unsubscribe(unsubscribe_request{protocol_version, client_id, topic});
}

client_handshake_result request_handshake(const client_options& options, const handshake_request& request) {
    session sess;
    if (!sess.open(options)) {
        return make_error(client_error_code::socket_error,
                          sess.error_message().empty() ? "Failed to open SHM control session" : sess.error_message());
    }
    return sess.handshake(request);
}

client_handshake_result request_handshake(const client_options& options, const std::string& client_id,
                                          const std::string& topic, role topic_role) {
    return request_handshake(options, handshake_request{protocol_version, client_id, topic, topic_role});
}

topic_list_result request_topic_list(const client_options& options, const list_topics_request& request) {
    session sess;
    if (!sess.open(options)) {
        return make_topic_list_error(client_error_code::socket_error, sess.error_message().empty()
                                                                          ? "Failed to open SHM control session"
                                                                          : sess.error_message());
    }
    return sess.list_topics(request);
}

topic_list_result request_topic_list(const client_options& options, const std::string& client_id) {
    return request_topic_list(options, list_topics_request{protocol_version, client_id});
}

} // namespace everest::lib::io::shm::control
