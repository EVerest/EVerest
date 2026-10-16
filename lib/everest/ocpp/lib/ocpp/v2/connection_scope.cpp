// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <ocpp/v2/connection_scope.hpp>

namespace ocpp {
namespace v2 {

namespace {
// Thread local rather than passed along the call chain: an inbound message is handled synchronously
// on the receive thread of the connection it arrived on, all the way down to the functional block
// that answers it, so the thread already identifies the connection. Threading a parameter through
// instead would touch every handler and every dispatch site in all functional blocks.
thread_local std::optional<ConnectionId> current_inbound_connection;
thread_local std::optional<ConnectionId> current_target_connection;
} // namespace

ConnectionScope::ConnectionScope(const ConnectionId& connection) : previous(current_inbound_connection) {
    current_inbound_connection = connection;
}

ConnectionScope::~ConnectionScope() {
    current_inbound_connection = this->previous;
}

std::optional<ConnectionId> ConnectionScope::current() {
    return current_inbound_connection;
}

ConnectionTargetScope::ConnectionTargetScope(const ConnectionId& connection) : previous(current_target_connection) {
    current_target_connection = connection;
}

ConnectionTargetScope::~ConnectionTargetScope() {
    current_target_connection = this->previous;
}

std::optional<ConnectionId> ConnectionTargetScope::current() {
    return current_target_connection;
}

} // namespace v2
} // namespace ocpp
