// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <ocpp/v2/connection_directory.hpp>

namespace ocpp {
namespace v2 {

PrimaryOnlyConnectionDirectory& PrimaryOnlyConnectionDirectory::instance() {
    static PrimaryOnlyConnectionDirectory directory;
    return directory;
}

std::vector<ConnectionId> PrimaryOnlyConnectionDirectory::connection_ids() const {
    return {ConnectionId::primary()};
}

bool PrimaryOnlyConnectionDirectory::may_set_variable(const ConnectionId& connection, const Component&,
                                                      const Variable&) const {
    // Only the primary connection exists here, and it may write anything it has the mutability for.
    return connection.is_primary();
}

} // namespace v2
} // namespace ocpp
