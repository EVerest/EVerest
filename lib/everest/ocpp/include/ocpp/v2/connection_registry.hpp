// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <map>
#include <memory>
#include <vector>

#include <ocpp/v2/connection_directory.hpp>
#include <ocpp/v2/connection_id.hpp>
#include <ocpp/v2/ocpp_connection.hpp>

namespace ocpp {
namespace v2 {

/// \brief Owns the charging station's OCPP connections.
///
/// There is always exactly one primary connection. Secondary connections are discovered from the
/// device model and may be absent, which is the normal case.
class ConnectionRegistry : public ConnectionDirectory {
public:
    /// \brief Takes ownership of \p connection.
    /// \throws std::invalid_argument if a connection with the same id is already registered.
    void add(std::unique_ptr<OcppConnection> connection);

    /// \brief The primary connection.
    /// \throws std::logic_error if no primary connection has been added.
    OcppConnection& primary();

    /// \brief The connection with this id, or nullptr if it is not registered.
    OcppConnection* find(const ConnectionId& id);

    /// \brief Every connection, primary first. Iterate this to act on all connections.
    std::vector<OcppConnection*> all();

    /// \brief Every connection except the primary, ordered by name.
    std::vector<OcppConnection*> secondaries();

    /// \brief Permanently suppresses the deferred websocket callbacks of every connection.
    void disarm_connection_callbacks();

    /// \brief Number of registered connections, at least one once the primary has been added.
    [[nodiscard]] std::size_t size() const;

    [[nodiscard]] std::vector<ConnectionId> connection_ids() const override;
    [[nodiscard]] bool may_set_variable(const ConnectionId& connection, const Component& component,
                                        const Variable& variable) const override;

private:
    // Ordered by ConnectionId, which sorts the primary before every secondary, so iteration order is
    // primary first and otherwise stable.
    std::map<ConnectionId, std::unique_ptr<OcppConnection>> connections;
};

} // namespace v2
} // namespace ocpp
