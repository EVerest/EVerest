// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <functional>
#include <optional>
#include <vector>

#include <ocpp/common/message_dispatcher.hpp>
#include <ocpp/v2/connection_id.hpp>
#include <ocpp/v2/types.hpp>

namespace ocpp {
namespace v2 {

class ConnectionRegistry;
class OcppConnection;

/// \brief The dispatcher the functional blocks talk to, routing each message to the right OCPP
///        connection.
///
/// FunctionalBlockContext already holds a MessageDispatcherInterface, so putting the router behind
/// that interface is what lets the charging station serve several connections without any of the
/// functional blocks knowing: they keep calling dispatch_call and dispatch_call_result exactly as
/// before, and this decides where the bytes go.
///
/// Routing rules:
/// - A response goes back to whoever asked, that is the connection named by ConnectionScope. A
///   response is never withheld: if a request was accepted, answering it is not optional.
/// - An asynchronous request expects exactly one answer, so it is never fanned out. It follows the
///   inbound connection when one is being handled, and the primary otherwise.
/// - A charging station initiated request pinned with ConnectionTargetScope goes to that connection
///   alone. Otherwise it fans out to every connection.
///
/// The primary connection always receives the request exactly as the functional block built it,
/// same body and same messageId. Only copies bound for a secondary connection are given a fresh
/// messageId, because the two endpoints number their messages independently and would otherwise
/// risk colliding. With no secondary connection configured, nothing on the wire changes at all.
class ConnectionRoutingDispatcher : public MessageDispatcherInterface<MessageType> {
public:
    /// \param registry Connections to route between; must outlive this dispatcher.
    explicit ConnectionRoutingDispatcher(ConnectionRegistry& registry);

    void dispatch_call(const json& call, bool triggered = false) override;
    std::future<ocpp::EnhancedMessage<MessageType>> dispatch_call_async(const json& call,
                                                                        bool triggered = false) override;
    void dispatch_call_result(const json& call_result) override;
    void dispatch_call_error(const json& call_error) override;

    /// \brief Adjusts a request on its way to one connection, or drops it for that connection.
    ///
    /// Applied only to copies bound for a secondary connection; the primary connection always
    /// receives the request exactly as the functional block built it. Used where a secondary system
    /// must see less than the CSMS does, such as a TransactionEvent copy with the idToken removed.
    ///
    /// \return The message to send, or std::nullopt to send nothing to that connection.
    using OutboundTransform =
        std::function<std::optional<json>(const ConnectionId& connection, MessageType message_type, const json& call)>;

    /// \brief Registers a transform. Transforms are applied in registration order; the first one to
    ///        return std::nullopt suppresses the message for that connection.
    void add_outbound_transform(OutboundTransform transform);

private:
    /// \brief The connection a response or an asynchronous request belongs to: the one being handled
    ///        if any, else the primary.
    ConnectionId responding_connection() const;

    /// \brief Hands \p call to one connection: verbatim for the primary, and as a copy with its own
    ///        messageId and any registered transforms applied for a secondary.
    void deliver_to(OcppConnection& connection, MessageType message_type, const json& call, bool triggered);

    ConnectionRegistry& registry;
    std::vector<OutboundTransform> transforms;
};

/// \brief The transform the charging station installs on its router: a TransactionEvent copy bound
///        for a secondary connection carries no idToken.
///
/// Application note sections 4.2 and 6.2. A secondary system has no business knowing who is
/// charging, and could not return idTokenInfo for it anyway. Everything else about the message is
/// left as it is.
ConnectionRoutingDispatcher::OutboundTransform make_transaction_event_id_token_transform();

} // namespace v2
} // namespace ocpp
