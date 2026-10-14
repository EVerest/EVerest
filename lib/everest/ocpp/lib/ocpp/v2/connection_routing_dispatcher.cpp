// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <ocpp/v2/connection_routing_dispatcher.hpp>

#include <everest/logging.hpp>

#include <ocpp/common/call_types.hpp>
#include <ocpp/v2/connection_registry.hpp>
#include <ocpp/v2/connection_scope.hpp>
#include <ocpp/v2/ocpp_enums.hpp>

namespace ocpp {
namespace v2 {

ConnectionRoutingDispatcher::ConnectionRoutingDispatcher(ConnectionRegistry& registry) : registry(registry) {
}

ConnectionId ConnectionRoutingDispatcher::responding_connection() const {
    return ConnectionScope::current().value_or(ConnectionId::primary());
}

void ConnectionRoutingDispatcher::dispatch_call(const json& call, bool triggered) {
    const auto message_type = conversions::string_to_messagetype(call.at(CALL_ACTION));

    if (const auto target = ConnectionTargetScope::current(); target.has_value()) {
        auto* connection = this->registry.find(target.value());
        if (connection == nullptr) {
            EVLOG_warning << "Dropping message for unknown OCPP connection " << target->to_string();
            return;
        }
        // Pinning a message to a connection says where it goes, not that the connection is allowed
        // to be sent it. A connection never receives a message type it has not been granted.
        if (!connection->may_send(message_type)) {
            EVLOG_debug << "Not sending " << conversions::messagetype_to_string(message_type) << " to "
                        << target->to_string() << ": not in its allowed upstream messages";
            return;
        }
        connection->dispatcher().dispatch_call(call, triggered);
        return;
    }

    for (auto* connection : this->registry.all()) {
        if (!connection->may_send(message_type)) {
            continue;
        }
        if (connection->id().is_primary()) {
            // Verbatim, messageId included: the primary connection must see exactly what the
            // functional block produced.
            connection->dispatcher().dispatch_call(call, triggered);
            continue;
        }

        // A copy for a secondary connection gets its own messageId. The two endpoints number their
        // messages independently, and an endpoint that counts rather than using UUIDs could
        // otherwise produce a messageId that is already in flight on the other connection.
        json copy = call;
        copy[MESSAGE_ID] = create_message_id().get();
        connection->dispatcher().dispatch_call(copy, triggered);
    }
}

std::future<ocpp::EnhancedMessage<MessageType>> ConnectionRoutingDispatcher::dispatch_call_async(const json& call,
                                                                                                 bool triggered) {
    // Never fanned out: the caller is waiting for one answer.
    const auto id = this->responding_connection();
    auto* connection = this->registry.find(id);
    if (connection == nullptr) {
        EVLOG_warning << "Falling back to the primary connection for an asynchronous call: " << id.to_string()
                      << " is not registered";
        return this->registry.primary().dispatcher().dispatch_call_async(call, triggered);
    }
    return connection->dispatcher().dispatch_call_async(call, triggered);
}

void ConnectionRoutingDispatcher::dispatch_call_result(const json& call_result) {
    const auto id = this->responding_connection();
    auto* connection = this->registry.find(id);
    if (connection == nullptr) {
        EVLOG_warning << "Answering on the primary connection: " << id.to_string() << " is not registered";
        this->registry.primary().dispatcher().dispatch_call_result(call_result);
        return;
    }
    connection->dispatcher().dispatch_call_result(call_result);
}

void ConnectionRoutingDispatcher::dispatch_call_error(const json& call_error) {
    const auto id = this->responding_connection();
    auto* connection = this->registry.find(id);
    if (connection == nullptr) {
        EVLOG_warning << "Answering on the primary connection: " << id.to_string() << " is not registered";
        this->registry.primary().dispatcher().dispatch_call_error(call_error);
        return;
    }
    connection->dispatcher().dispatch_call_error(call_error);
}

} // namespace v2
} // namespace ocpp
