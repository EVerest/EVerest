// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <type_traits>

#include <everest/slac/ev/fsm.hpp>
#include <everest/slac/evse/fsm.hpp>

// The facades hold the context the states reference; moving one would dangle them.
static_assert(!std::is_copy_constructible_v<everest::slac::evse::EvseFSM>);
static_assert(!std::is_copy_assignable_v<everest::slac::evse::EvseFSM>);
static_assert(!std::is_move_constructible_v<everest::slac::evse::EvseFSM>);
static_assert(!std::is_move_assignable_v<everest::slac::evse::EvseFSM>);

static_assert(!std::is_copy_constructible_v<everest::slac::ev::EvFSM>);
static_assert(!std::is_copy_assignable_v<everest::slac::ev::EvFSM>);
static_assert(!std::is_move_constructible_v<everest::slac::ev::EvFSM>);
static_assert(!std::is_move_assignable_v<everest::slac::ev::EvFSM>);
