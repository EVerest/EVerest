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

} // namespace v2
} // namespace ocpp
