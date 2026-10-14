// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <set>

#include <ocpp/common/connectivity_manager.hpp>
#include <ocpp/common/connectivity_manager_configuration.hpp>
#include <ocpp/common/message_dispatcher.hpp>
#include <ocpp/common/message_queue.hpp>
#include <ocpp/v2/connection_id.hpp>
#include <ocpp/v2/types.hpp>

#include <everest/timer.hpp>

namespace ocpp {
namespace v2 {

class DeviceModelAbstract;

/// \brief One OCPP connection of the charging station: everything that is per connection rather
///        than per charging station.
///
/// The charging station has exactly one device model, one database, one set of EVSEs and one set of
/// functional blocks, all shared. What is not shared is the pipe: each connection dials its own
/// endpoint over its own websocket, keeps its own queue of messages to that endpoint, and dispatches
/// into that queue. Bundling those together is what lets a secondary connection fail, retry or go
/// offline without the primary connection noticing, which the OCA application note "Secondary OCPP
/// Connection on a Charging Station" requires.
///
/// Held by ConnectionRegistry. Not copyable or movable: the parts point at each other, and the
/// message queue's send callback captures this connection's connectivity manager.
class OcppConnection {
public:
    /// \param id             Which connection this is.
    /// \param configuration  Connectivity configuration backing \p connectivity_manager. May be
    ///                       nullptr when the connectivity manager was supplied from outside and
    ///                       brings its own configuration; otherwise this connection owns it and
    ///                       must keep it alive for as long as the manager.
    /// \param connectivity_manager  Websocket and network profile handling for this connection.
    /// \param message_queue  Queue of messages bound for this connection's endpoint.
    /// \param dispatcher     Dispatcher writing into \p message_queue. This is the leaf dispatcher
    ///                       of a single connection, not the router that fans out across them.
    /// \param device_model  Shared device model, read for this connection's permissions. Must
    ///                      outlive this connection.
    OcppConnection(ConnectionId id, std::unique_ptr<ocpp::ConnectivityManagerConfiguration> configuration,
                   std::shared_ptr<ConnectivityManagerInterface> connectivity_manager,
                   std::shared_ptr<MessageQueue<MessageType>> message_queue,
                   std::unique_ptr<MessageDispatcherInterface<MessageType>> dispatcher,
                   DeviceModelAbstract& device_model);

    OcppConnection(const OcppConnection&) = delete;
    OcppConnection& operator=(const OcppConnection&) = delete;

    [[nodiscard]] const ConnectionId& id() const;

    ConnectivityManagerInterface& connectivity_manager();
    [[nodiscard]] std::shared_ptr<ConnectivityManagerInterface> connectivity_manager_ptr() const;

    MessageQueue<MessageType>& message_queue();
    [[nodiscard]] std::shared_ptr<MessageQueue<MessageType>> message_queue_ptr() const;

    /// \brief The dispatcher that writes into this connection's queue.
    MessageDispatcherInterface<MessageType>& dispatcher();

    /// \brief Whether the charging station may send \p message_type to this connection.
    ///
    /// The primary connection may send anything. A secondary connection may send only what its
    /// UpstreamMsgs grants it (application note section 3.5), plus the messages that establish and
    /// keep its own link alive, which are about the pipe rather than the charging station's
    /// business and so are never withheld.
    [[nodiscard]] bool may_send(MessageType message_type) const;

    /// \brief Whether this connection may send \p message_type to the charging station.
    ///
    /// The primary connection may send anything. A secondary connection may send only what its
    /// DownstreamMsgs grants it (application note section 3.5); anything else is refused with a
    /// CALLERROR. Responses are never subject to this: a response answers something the charging
    /// station itself sent.
    [[nodiscard]] bool may_receive(MessageType message_type) const;

    /// \brief Re-reads this connection's permissions from the device model.
    ///
    /// Called at startup and whenever the allow list changes, so that a CSMS granting or revoking a
    /// message takes effect without a restart.
    void reload_permissions();

    /// \brief Starts this connection's heartbeat.
    ///
    /// Each connection keeps its own heartbeat, at its own interval, so a secondary connection going
    /// quiet never affects the primary connection's liveness towards the CSMS.
    void start_heartbeat(std::function<void()> send, std::chrono::seconds interval);

    /// \brief Stops this connection's heartbeat. Safe to call when it was never started.
    void stop_heartbeat();

private:
    ConnectionId m_id;
    // Declared before m_connectivity_manager so it outlives it: the manager holds a reference to it.
    std::unique_ptr<ocpp::ConnectivityManagerConfiguration> m_configuration;
    std::shared_ptr<ConnectivityManagerInterface> m_connectivity_manager;
    // Declared after m_connectivity_manager: the queue's send callback uses the manager.
    std::shared_ptr<MessageQueue<MessageType>> m_message_queue;
    // Declared last: the dispatcher holds a reference to the queue.
    std::unique_ptr<MessageDispatcherInterface<MessageType>> m_dispatcher;
    Everest::SteadyTimer m_heartbeat_timer;

    DeviceModelAbstract& m_device_model;
    /// Guarded: reloaded from the device model on a variable listener thread while dispatch reads
    /// it from every connection's receive thread.
    mutable everest::lib::util::monitor<std::set<MessageType>> m_upstream_msgs;
    mutable everest::lib::util::monitor<std::set<MessageType>> m_downstream_msgs;
};

} // namespace v2
} // namespace ocpp
