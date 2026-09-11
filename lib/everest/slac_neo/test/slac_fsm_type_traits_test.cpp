// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 - 2026 Pionix GmbH and Contributors to EVerest

#include <type_traits>

#include <everest/slac/slac_fsm.hpp>

static_assert(!std::is_copy_constructible_v<everest::lib::slac::slac_fsm>);
static_assert(!std::is_copy_assignable_v<everest::lib::slac::slac_fsm>);
static_assert(!std::is_move_constructible_v<everest::lib::slac::slac_fsm>);
static_assert(!std::is_move_assignable_v<everest::lib::slac::slac_fsm>);
