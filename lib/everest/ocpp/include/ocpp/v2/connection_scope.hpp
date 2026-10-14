// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <optional>

#include <ocpp/v2/connection_id.hpp>

namespace ocpp {
namespace v2 {

/// \brief Marks, for the duration of the scope, which OCPP connection the message currently being
///        handled on this thread arrived on.
///
/// Every connection has its own websocket, and a websocket delivers on its own receive thread, so
/// "the connection this thread is currently serving" is well defined for as long as an inbound
/// message is being handled. ConnectionRoutingDispatcher reads it to send a CALLRESULT or CALLERROR
/// back to whoever asked, without any of the functional blocks having to know that more than one
/// connection exists.
///
/// Valid ONLY while an inbound message is being handled synchronously. Anything produced later - a
/// timer, a callback from EVerest, an asynchronous follow-up correlated by requestId - is outside
/// any scope and must name its target explicitly with ConnectionTargetScope.
///
/// Nesting is supported: the previous value is restored on destruction.
class ConnectionScope {
public:
    explicit ConnectionScope(const ConnectionId& connection);
    ~ConnectionScope();

    ConnectionScope(const ConnectionScope&) = delete;
    ConnectionScope& operator=(const ConnectionScope&) = delete;
    ConnectionScope(ConnectionScope&&) = delete;
    ConnectionScope& operator=(ConnectionScope&&) = delete;

    /// \brief The connection whose inbound message this thread is handling, or std::nullopt when
    ///        not inside inbound handling.
    static std::optional<ConnectionId> current();

private:
    std::optional<ConnectionId> previous;
};

/// \brief Directs charging station initiated requests to one specific connection instead of letting
///        them fan out to every connection that allows them.
///
/// Used where a message belongs to exactly one connection rather than to the station: a
/// per-connection BootNotification or Heartbeat, an asynchronous follow-up that has to go back to
/// the connection that requested it, a SecurityEvent about one connection's own security, or meter
/// values sampled specifically for one connection.
///
/// Nesting is supported: the previous value is restored on destruction.
class ConnectionTargetScope {
public:
    explicit ConnectionTargetScope(const ConnectionId& connection);
    ~ConnectionTargetScope();

    ConnectionTargetScope(const ConnectionTargetScope&) = delete;
    ConnectionTargetScope& operator=(const ConnectionTargetScope&) = delete;
    ConnectionTargetScope(ConnectionTargetScope&&) = delete;
    ConnectionTargetScope& operator=(ConnectionTargetScope&&) = delete;

    /// \brief The connection an outgoing request is pinned to, or std::nullopt when it should fan
    ///        out normally.
    static std::optional<ConnectionId> current();

private:
    std::optional<ConnectionId> previous;
};

} // namespace v2
} // namespace ocpp
