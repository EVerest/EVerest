// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include "../EvSimulator.hpp"
#include "EvSimRuntime.hpp"

namespace module {

// Subscribes the EvSimulator module to the 17 `m2e/<verb>` MQTT topics that
// drive the simulator from the outside. Fourteen verbs translate to `Event`s
// enqueued onto the FSM runtime queue; `communication_check` pokes the
// CommCheckHandler directly; `raise_error` / `clear_error` route to the
// ev_manager error machinery directly. Mirror of the
// `ev_board_support_API::generate_api_var_*` pattern.
void setup_command_router(EvSimRuntime& rt, EvSimulator& mod);

} // namespace module
