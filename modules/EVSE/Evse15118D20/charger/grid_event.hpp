// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>

#include <generated/types/grid_support.hpp>

namespace module {

/// \brief Map an EV-reported grid_event_condition code (from the DER AC charge-loop request) to a
/// GridEventFault. 0 = no event; any nonzero code maps to a single generic fault (LocalEmergency)
/// until the IEC 61851-1 code table is wired in.
std::optional<types::grid_support::GridEventFault> map_grid_event_condition(uint8_t condition);

/// \brief The kind of fault-state transition produced by GridEventEdgeDetector.
enum class Transition {
    None,    ///< No state change; nothing to publish.
    Rising,  ///< no-fault -> fault; publish a new alarm (alarm_ended=false).
    Falling, ///< fault -> no-fault; publish a clearing alarm (alarm_ended=true).
};

/// \brief The result of inspecting one condition sample against the edge detector.
///
/// Constructed only through the factories so illegal states (e.g. a Rising with
/// no fault) cannot be expressed: None always carries no fault, Rising/Falling
/// always carry one. The caller can therefore dereference \c fault unguarded
/// whenever \c transition is not None.
class EdgeResult {
public:
    const Transition transition;
    const std::optional<types::grid_support::GridEventFault> fault;

    static EdgeResult none() {
        return EdgeResult{Transition::None, std::nullopt};
    }
    static EdgeResult rising(types::grid_support::GridEventFault fault) {
        return EdgeResult{Transition::Rising, fault};
    }
    static EdgeResult falling(types::grid_support::GridEventFault fault) {
        return EdgeResult{Transition::Falling, fault};
    }

private:
    EdgeResult(Transition transition, std::optional<types::grid_support::GridEventFault> fault) :
        transition(transition), fault(fault) {
    }
};

/// \brief Rising/falling edge detector for EV grid-event faults.
///
/// Reports exactly one transition per edge; repeated or changed nonzero
/// conditions while already in fault are deduplicated. peek() reports the
/// transition without mutating, commit() advances state, so a caller can commit
/// only after a successful publish and retry otherwise. Not thread-safe (single
/// charger thread); call reset() at session start, as the detector outlives
/// individual sessions.
class GridEventEdgeDetector {
public:
    /// \brief Report the transition \p condition would cause, without mutating state.
    EdgeResult peek(uint8_t condition) const;
    /// \brief Advance the held fault state to reflect \p condition.
    void commit(uint8_t condition);
    /// \brief Clear to the no-fault baseline; call at session start.
    void reset();

private:
    std::optional<types::grid_support::GridEventFault> active_fault{std::nullopt};
};

} // namespace module
