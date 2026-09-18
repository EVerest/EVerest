// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <chrono>

namespace module {

class FsmContext;

// Advances an in-flight current ramp captured in ctx.vars.active_ramp.
//
// Invoked from EvSimRuntime::on_tick() before soc_step so that SoC
// integration sees the freshly commanded current. When now >= end_at the
// commanded current snaps to target_a and active_ramp is cleared; otherwise
// the commanded current is the linear interpolation between start_a and
// target_a parameterized by (now - start_at) / (end_at - start_at).
//
// `now` is injected so unit tests can drive the interpolator deterministically
// against a controlled time source.
void ramp_step(FsmContext& ctx, std::chrono::steady_clock::time_point now);

} // namespace module
