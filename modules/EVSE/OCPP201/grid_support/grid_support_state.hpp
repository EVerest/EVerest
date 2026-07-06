// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <map>
#include <utility>
#include <vector>

#include <generated/types/grid_support.hpp>

namespace module {

/// \brief Per-EVSE DER capability registry plus the filter that derives a per-EVSE active set.
///
/// Module-independent (no OCPP201.hpp / charge_point / framework dependency) so it is unit-testable in
/// isolation. libocpp owns the active-directive set; this type does not track transitions, it only
/// remembers each EVSE's supported types and filters the cached active set down to them. No internal
/// synchronization: the owning module wraps it in a monitor<> and holds one handle per logical operation.
///
/// The capabilities_live / alarms_live flags gate the handoff from pre-construction buffering to live
/// delivery: each starts false (route to the buffer) and is flipped sticky-true once the charge point can
/// accept that kind, so routing decisions stay inside the monitor and never race the charge_point pointer.
class GridSupportState {
public:
    /// \brief Store the latest declared DER capability for \p evse_id, registering the EVSE.
    void set_capability(int32_t evse_id, const types::grid_support::DERCapability& capability);

    /// \brief Unregister \p evse_id, undoing a speculative registration when the enabling write is
    /// rejected. No-op if not registered.
    void unregister(int32_t evse_id);

    /// \brief Replace the cached active directive set (the last set libocpp pushed). Stored by move.
    void set_active_directives(std::vector<types::grid_support::Directive> directives);

    /// \brief Filter the cached active directive set down to \p evse_id's declared supported types. An
    /// unregistered EVSE supports no types (empty result). The returned set always has evse_id populated.
    ///
    /// No per-type dedup: two non-superseded controls of the same type both surface; consumers
    /// disambiguate via is_default/id.
    types::grid_support::ActiveDirectiveSet build_active_set(int32_t evse_id) const;

    /// \brief EVSEs that have declared a capability via set_capability.
    std::vector<int32_t> registered_evses() const;

    /// \brief Buffer a capability that arrived before the charge point existed. Does NOT register the
    /// EVSE (registration happens when the buffer is applied). Re-buffering the same evse_id is last-wins.
    void buffer_pending_capability(int32_t evse_id, const types::grid_support::DERCapability& capability);

    /// \brief Take and clear the pending-capability buffer in ascending evse_id order.
    std::vector<std::pair<int32_t, types::grid_support::DERCapability>> take_pending_capabilities();

    /// \brief Buffer an alarm that arrived before the charge point existed, preserving arrival order.
    void buffer_pending_alarm(const types::grid_support::GridAlarm& alarm);

    /// \brief Take and clear the pending-alarm buffer in arrival order.
    std::vector<types::grid_support::GridAlarm> take_pending_alarms();

    /// \brief True once capabilities deliver live instead of buffering. Default false, sticky.
    bool capabilities_live() const;

    /// \brief Mark capabilities as delivering live; idempotent.
    void set_capabilities_live();

    /// \brief True once alarms deliver live instead of buffering. Default false, sticky.
    bool alarms_live() const;

    /// \brief Mark alarms as delivering live; idempotent.
    void set_alarms_live();

private:
    // Unguarded by design: every access goes through OCPP201's monitor<GridSupportState> handle (class doc).
    std::map<int32_t, types::grid_support::DERCapability> capability_by_evse;
    // Capabilities buffered until flush_pending_grid_support() empties this at ready(); last write per EVSE
    // wins. Distinct from capability_by_evse: entries here have not yet enabled DER on their EVSE.
    std::map<int32_t, types::grid_support::DERCapability> pending_capability_by_evse;
    // The most recent active set libocpp pushed; build_active_set filters it per EVSE.
    std::vector<types::grid_support::Directive> last_active_directives;
    // Alarms buffered in arrival order until the first successful capability enable delivers them.
    std::vector<types::grid_support::GridAlarm> pending_alarms;
    // Sticky handoff flags: false = buffer, true = deliver live (see class doc).
    bool capabilities_are_live = false;
    bool alarms_are_live = false;
};

} // namespace module
