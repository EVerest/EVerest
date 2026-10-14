// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <ocpp/v2/connection_registry.hpp>

#include <stdexcept>
#include <utility>

namespace ocpp {
namespace v2 {

void ConnectionRegistry::add(std::unique_ptr<OcppConnection> connection) {
    if (connection == nullptr) {
        throw std::invalid_argument("Cannot register a null OcppConnection");
    }
    const auto id = connection->id();
    const auto [_, inserted] = this->connections.emplace(id, std::move(connection));
    if (!inserted) {
        throw std::invalid_argument("OCPP connection " + id.to_string() + " is already registered");
    }
}

OcppConnection& ConnectionRegistry::primary() {
    auto* connection = this->find(ConnectionId::primary());
    if (connection == nullptr) {
        throw std::logic_error("No primary OCPP connection has been registered");
    }
    return *connection;
}

OcppConnection* ConnectionRegistry::find(const ConnectionId& id) {
    const auto it = this->connections.find(id);
    if (it == this->connections.end()) {
        return nullptr;
    }
    return it->second.get();
}

std::vector<OcppConnection*> ConnectionRegistry::all() {
    std::vector<OcppConnection*> result;
    result.reserve(this->connections.size());
    for (const auto& entry : this->connections) {
        result.push_back(entry.second.get());
    }
    return result;
}

std::vector<OcppConnection*> ConnectionRegistry::secondaries() {
    std::vector<OcppConnection*> result;
    for (const auto& entry : this->connections) {
        if (!entry.first.is_primary()) {
            result.push_back(entry.second.get());
        }
    }
    return result;
}

std::size_t ConnectionRegistry::size() const {
    return this->connections.size();
}

bool ConnectionRegistry::may_set_variable(const ConnectionId& connection, const Component& component,
                                          const Variable& variable) const {
    const auto it = this->connections.find(connection);
    if (it == this->connections.end()) {
        return false;
    }
    return it->second->may_set_variable(component, variable);
}

std::vector<ConnectionId> ConnectionRegistry::connection_ids() const {
    std::vector<ConnectionId> result;
    result.reserve(this->connections.size());
    for (const auto& entry : this->connections) {
        result.push_back(entry.first);
    }
    return result;
}

} // namespace v2
} // namespace ocpp
