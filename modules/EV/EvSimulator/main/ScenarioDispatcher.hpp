// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include "FsmContext.hpp"

#include <everest_api_types/ev_simulator/API.hpp>

namespace module {

// Stub for T-B4 — T-D3 replaces this header-only inline with a real .cpp.
// `start_scenario` is invoked from `Unplugged::feed(RunScenario)`.
inline void start_scenario(everest::lib::API::V1_0::types::ev_simulator::ScenarioName, FsmContext& ctx) {
    ctx.publish_e2m_command_ack("run_scenario", "scenario dispatcher not implemented");
}

} // namespace module
