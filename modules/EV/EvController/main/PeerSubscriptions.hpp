// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include "../EvController.hpp"
#include "EvCtlRuntime.hpp"

namespace module {

// Enqueues BSP, SLAC and ISO peer variables for the runtime to consume and republish.
void setup_peer_subscriptions(EvCtlRuntime& rt, EvController& mod);

} // namespace module
