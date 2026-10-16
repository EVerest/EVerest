// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <ocpp/common/message_dispatcher.hpp>
#include <ocpp/v2/connection_directory.hpp>
#include <ocpp/v2/request_origin_registry.hpp>
#include <ocpp/v2/types.hpp>

namespace ocpp {
class EvseSecurity;
class ConnectivityManagerInterface;

namespace v2 {
class DeviceModelAbstract;
class EvseManagerInterface;
class DatabaseHandlerInterface;
class ComponentStateManagerInterface;

/// \brief Context / requirements for the functional blocks.
///
/// All functional blocks will get this context. The references and pointers in this struct are used by all or most
/// functional blocks.
struct FunctionalBlockContext {
    MessageDispatcherInterface<MessageType>& message_dispatcher;
    DeviceModelAbstract& device_model;
    ConnectivityManagerInterface& connectivity_manager;
    EvseManagerInterface& evse_manager;
    DatabaseHandlerInterface& database_handler;
    EvseSecurity& evse_security;
    ComponentStateManagerInterface& component_state_manager;
    std::atomic<OcppProtocolVersion>& ocpp_version;
    /// \brief What the charging station's OCPP connections are and what they are allowed to do.
    ///
    /// Defaults to a charging station with only its primary connection, so a functional block can
    /// be constructed without a registry.
    const ConnectionDirectory& connections;
    /// \brief Which connection asked for each outstanding request that is answered by later
    ///        messages, so those messages go back to it rather than to the CSMS.
    ///
    /// Defaults to a registry of its own, so a functional block can be constructed without one; a
    /// block that records an origin then simply finds it again later in its own registry.
    RequestOriginRegistry& request_origins;

    FunctionalBlockContext(MessageDispatcherInterface<MessageType>& message_dispatcher,
                           DeviceModelAbstract& device_model, ConnectivityManagerInterface& connectivity_manager,
                           EvseManagerInterface& evse_manager, DatabaseHandlerInterface& database_handler,
                           EvseSecurity& evse_security, ComponentStateManagerInterface& component_state_manager,
                           std::atomic<OcppProtocolVersion>& ocpp_version,
                           const ConnectionDirectory& connections = PrimaryOnlyConnectionDirectory::instance(),
                           RequestOriginRegistry& request_origins = default_request_origins()) :
        message_dispatcher(message_dispatcher),
        device_model(device_model),
        connectivity_manager(connectivity_manager),
        evse_manager(evse_manager),
        database_handler(database_handler),
        evse_security(evse_security),
        component_state_manager(component_state_manager),
        ocpp_version(ocpp_version),
        connections(connections),
        request_origins(request_origins) {
    }

private:
    /// A registry shared by every context that was not given one. Only reached when no charging
    /// point supplied its own, which in practice means unit tests.
    static RequestOriginRegistry& default_request_origins() {
        static RequestOriginRegistry registry;
        return registry;
    }
};
} // namespace v2
} // namespace ocpp
