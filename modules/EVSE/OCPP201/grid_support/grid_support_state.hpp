// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef OCPP201_GRID_SUPPORT_STATE_HPP
#define OCPP201_GRID_SUPPORT_STATE_HPP

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

    /// \brief Drain the pending-capability buffer in ascending evse_id order, leaving it empty.
    std::vector<std::pair<int32_t, types::grid_support::DERCapability>> take_pending_capabilities();

    /// \brief Buffer an alarm that arrived before the charge point existed, preserving arrival order.
    void buffer_pending_alarm(const types::grid_support::GridAlarm& alarm);

    /// \brief Drain the pending-alarm buffer in arrival order, leaving it empty.
    std::vector<types::grid_support::GridAlarm> take_pending_alarms();

private:
    // Unguarded by design: every access goes through OCPP201's monitor<GridSupportState> handle (class doc).
    std::map<int32_t, types::grid_support::DERCapability> capability_by_evse;
    // Distinct from capability_by_evse: a pending entry (buffered pre-construction, last-wins) has not
    // yet enabled DER on its EVSE.
    std::map<int32_t, types::grid_support::DERCapability> pending_capability_by_evse;
    // Cache of the last active set libocpp pushed, filtered per-EVSE by build_active_set.
    std::vector<types::grid_support::Directive> last_active_directives;
    // Alarms buffered pre-construction, flushed at ready(); ordered vector preserves arrival order.
    std::vector<types::grid_support::GridAlarm> pending_alarms;
};

} // namespace module

#endif // OCPP201_GRID_SUPPORT_STATE_HPP
