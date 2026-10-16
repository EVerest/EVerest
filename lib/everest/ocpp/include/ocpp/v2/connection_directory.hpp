// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <vector>

#include <ocpp/v2/connection_id.hpp>
#include <ocpp/v2/ocpp_types.hpp>

namespace ocpp {
namespace v2 {

/// \brief What the functional blocks are allowed to know about the charging station's OCPP
///        connections: which ones exist, and what each may do.
///
/// Deliberately narrow and read only. A functional block should be able to answer "may this
/// connection do that" without being able to reach a connection's websocket or queue, and without
/// depending on how connections are built.
class ConnectionDirectory {
public:
    virtual ~ConnectionDirectory() = default;

    /// \brief Every configured connection, primary first, secondaries then ordered by name.
    [[nodiscard]] virtual std::vector<ConnectionId> connection_ids() const = 0;

    /// \brief Whether \p connection may write \p variable of \p component.
    ///
    /// A connection that is not registered may write nothing: an unknown connection is not a
    /// trusted one.
    [[nodiscard]] virtual bool may_set_variable(const ConnectionId& connection, const Component& component,
                                                const Variable& variable) const = 0;
};

/// \brief The connection view of a charging station that has only its primary connection.
///
/// Lets a functional block be constructed without a registry, which is what a single-connection
/// setup and most unit tests want, while keeping the interface non-nullable.
class PrimaryOnlyConnectionDirectory : public ConnectionDirectory {
public:
    static PrimaryOnlyConnectionDirectory& instance();

    [[nodiscard]] std::vector<ConnectionId> connection_ids() const override;
    [[nodiscard]] bool may_set_variable(const ConnectionId& connection, const Component& component,
                                        const Variable& variable) const override;
};

} // namespace v2
} // namespace ocpp
