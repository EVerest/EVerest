// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/d20/ev/control_event.hpp>

// The EV control events are protocol-agnostic; reuse the d20 definitions verbatim.
namespace iso15118::din::ev {

using d20::ev::ControlEvent;
using d20::ev::PauseCharging;
using d20::ev::PresentVoltageCurrent;
using d20::ev::StopCharging;
using d20::ev::UpdateDcParameters;
using d20::ev::UpdateDcTargets;
using d20::ev::UpdateSoc;

} // namespace iso15118::din::ev
