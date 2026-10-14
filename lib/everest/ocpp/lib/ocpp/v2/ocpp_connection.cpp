// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <ocpp/v2/ocpp_connection.hpp>

#include <stdexcept>
#include <utility>

namespace ocpp {
namespace v2 {

OcppConnection::OcppConnection(ConnectionId id, std::unique_ptr<ocpp::ConnectivityManagerConfiguration> configuration,
                               std::shared_ptr<ConnectivityManagerInterface> connectivity_manager,
                               std::shared_ptr<MessageQueue<MessageType>> message_queue,
                               std::unique_ptr<MessageDispatcherInterface<MessageType>> dispatcher) :
    m_id(std::move(id)),
    m_configuration(std::move(configuration)),
    m_connectivity_manager(std::move(connectivity_manager)),
    m_message_queue(std::move(message_queue)),
    m_dispatcher(std::move(dispatcher)) {
    if (this->m_connectivity_manager == nullptr) {
        throw std::invalid_argument("OcppConnection " + this->m_id.to_string() + " requires a connectivity manager");
    }
    if (this->m_message_queue == nullptr) {
        throw std::invalid_argument("OcppConnection " + this->m_id.to_string() + " requires a message queue");
    }
    if (this->m_dispatcher == nullptr) {
        throw std::invalid_argument("OcppConnection " + this->m_id.to_string() + " requires a message dispatcher");
    }
}

const ConnectionId& OcppConnection::id() const {
    return this->m_id;
}

ConnectivityManagerInterface& OcppConnection::connectivity_manager() {
    return *this->m_connectivity_manager;
}

std::shared_ptr<ConnectivityManagerInterface> OcppConnection::connectivity_manager_ptr() const {
    return this->m_connectivity_manager;
}

MessageQueue<MessageType>& OcppConnection::message_queue() {
    return *this->m_message_queue;
}

std::shared_ptr<MessageQueue<MessageType>> OcppConnection::message_queue_ptr() const {
    return this->m_message_queue;
}

MessageDispatcherInterface<MessageType>& OcppConnection::dispatcher() {
    return *this->m_dispatcher;
}

bool OcppConnection::may_send(const MessageType message_type) const {
    if (this->m_id.is_primary()) {
        return true;
    }

    // Default deny for a secondary connection, per application note section 3.6: a secondary system
    // gets nothing it has not been granted. Only the messages that establish and keep the connection
    // itself alive are exempt, because they are about the pipe rather than about the charging
    // station's business. The UpstreamMsgs allow list will widen this.
    return message_type == MessageType::BootNotification or message_type == MessageType::Heartbeat;
}

void OcppConnection::start_heartbeat(std::function<void()> send, const std::chrono::seconds interval) {
    if (interval <= std::chrono::seconds(0)) {
        this->stop_heartbeat();
        return;
    }
    this->m_heartbeat_timer.interval(std::move(send), interval);
}

void OcppConnection::stop_heartbeat() {
    this->m_heartbeat_timer.stop();
}

} // namespace v2
} // namespace ocpp
