// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include "../EvSimulator.hpp"
#include "EvSimRuntime.hpp"

#include <framework/everest.hpp>

#include <vector>

namespace module {

// Subscribes the EvSimulator module to the 17 `m2e/<verb>` MQTT topics that
// drive the simulator from the outside. Fourteen verbs translate to `Event`s
// enqueued onto the FSM runtime queue; `communication_check` pokes the
// CommCheckHandler directly; `raise_error` / `clear_error` route to the
// ev_manager error machinery directly. Mirror of the
// `ev_board_support_API::generate_api_var_*` pattern.
//
// Returns the UnsubscribeToken for each subscription so the caller can detach
// the MQTT handlers before the captured runtime references are torn down. The
// returned tokens must outlive nothing — invoking them stops further MQTT
// delivery into the captured `rt` reference.
std::vector<Everest::UnsubscribeToken> setup_command_router(EvSimRuntime& rt, EvSimulator& mod);

} // namespace module
