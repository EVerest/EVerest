// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <map>
#include <memory>
#include <vector>

#include <ocpp/v2/connection_id.hpp>
#include <ocpp/v2/ocpp_connection.hpp>

namespace ocpp {
namespace v2 {

/// \brief Read only view of which OCPP connections exist.
///
/// Handed to the parts that need to reason about the set of connections without being able to
/// change it, such as the check that no two connections claim the same network configuration slot.
class ConnectionDirectory {
public:
    virtual ~ConnectionDirectory() = default;

    /// \brief Every configured connection, primary first, secondaries then ordered by name.
    [[nodiscard]] virtual std::vector<ConnectionId> connection_ids() const = 0;
};

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

    /// \brief Number of registered connections, at least one once the primary has been added.
    [[nodiscard]] std::size_t size() const;

    [[nodiscard]] std::vector<ConnectionId> connection_ids() const override;

private:
    // Ordered by ConnectionId, which sorts the primary before every secondary, so iteration order is
    // primary first and otherwise stable.
    std::map<ConnectionId, std::unique_ptr<OcppConnection>> connections;
};

} // namespace v2
} // namespace ocpp
