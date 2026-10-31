// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/controller.hpp>

void delete_controller(iso15118::ev::Controller* controller) {
    delete controller;
}

#include <iso15118/ev/d2/engine.hpp>
#include <iso15118/ev/d20/service_family.hpp>
#include <iso15118/ev/session/fsm.hpp>
#include <iso15118/ev/session/pre_iso20_engine.hpp>

#include <type_traits>

static_assert(
    std::is_same_v<iso15118::ev::session::PreIso20Engine<iso15118::ev::d2::EngineTraits>, iso15118::ev::d2::Engine>);
static_assert(iso15118::ev::session::FeedOutcome<int>{}.output == iso15118::ev::session::Disposition::Ignored);
static_assert(iso15118::ev::d20::is_ac_family(iso15118::message_20::datatypes::ServiceCategory::AC));
