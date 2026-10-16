// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include <ocpp/v2/connection_id.hpp>

namespace ocpp {
namespace v2 {

class DeviceModelAbstract;

/// \brief How one OCPP connection is configured in the device model.
struct ConnectionInfo {
    ConnectionId id;
    /// \brief Whether the connection is switched on. Always true for the primary connection.
    bool enabled{false};
    /// \brief Network configuration slots this connection may dial, in priority order. Slots are
    ///        numbered station wide and are never shared between connections.
    std::vector<std::int32_t> network_configuration_slots;
    /// \brief Slot the connection last connected on, if it ever has.
    std::optional<std::int32_t> active_network_profile_slot;
};

/// \brief Reads which OCPP connections the charging station is configured for.
///
/// The primary connection always exists and comes first. A secondary connection exists because an
/// instance of the OCPPCommCtrlr component exists for it, which is how the OCA application note
/// "Secondary OCPP Connection on a Charging Station" (section 3.3) says a secondary connection is
/// declared. Instance names are free to choose; none is special-cased.
///
/// A secondary connection is only reported when it is switched on and has at least one network
/// configuration slot, since without a slot there is nothing to dial.
///
/// Misconfiguration never propagates: an offending secondary connection, or an offending slot of
/// one, is logged and left out. A broken secondary must not be able to stop the charging station
/// from reaching its CSMS.
///
/// Rejected, per section 3.4.1:
/// - an instance name outside what ConnectionId accepts,
/// - a slot with no matching NetworkConfiguration component,
/// - a slot already claimed by another connection, including the primary. Only the overlapping slot
///   is dropped, and the connection that claimed it first keeps it, the primary always winning.
///
/// \param device_model Device model to read; also the source of truth for which connections exist.
/// \return The configured connections, primary first, then secondaries ordered by name.
std::vector<ConnectionInfo> discover_connections(DeviceModelAbstract& device_model);

} // namespace v2
} // namespace ocpp
