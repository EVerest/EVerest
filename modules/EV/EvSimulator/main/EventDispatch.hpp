// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include "Events.hpp"
#include "FsmContext.hpp"
#include "StateBase.hpp"

#include <everest/util/fsm/fsm.hpp>

#include <memory>

namespace module {

// Feeds `ev` to `fsm` with exception isolation. If `fsm->feed` throws
// (most commonly `std::bad_variant_access` when a producer sets `kind`
// and `payload` to disagreeing alternatives), the exception is logged
// and the FSM is replaced with a fresh Faulted state carrying an
// Internal fault report so the operator sees the failure. The FSM's
// `feed` does not expose mid-handler state reseeding, so the current
// FSM is destroyed (running the prior state's leave()) and a new one
// is constructed rooted at Faulted. `fsm` must own a valid instance
// on entry; it owns a valid instance on return.
void feed_with_fault_isolation(std::unique_ptr<fsm::v2::FSM<StateBase>>& fsm, FsmContext& ctx, const Event& ev);

} // namespace module
