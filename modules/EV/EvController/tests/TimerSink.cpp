// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "TimerSink.hpp"

// All members are inline in the header; this translation unit exists so the
// test CMake target has a real object file per-source and to anchor any
// future advance_state_timer / advance_tick methods.

namespace module::test {} // namespace module::test
