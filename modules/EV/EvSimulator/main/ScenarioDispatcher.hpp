// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include "FsmContext.hpp"

#include <everest_api_types/ev_simulator/API.hpp>

namespace module {

// Invoked from `Unplugged::feed(RunScenario)`. Implementation in
// ScenarioDispatcher.cpp dispatches the 3 working presets and rejects the rest
// via `e2m/command_ack`.
void start_scenario(everest::lib::API::V1_0::types::ev_simulator::ScenarioName name, FsmContext& ctx);

} // namespace module
