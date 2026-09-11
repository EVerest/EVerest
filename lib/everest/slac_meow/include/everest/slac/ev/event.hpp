// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <functional>
#include <variant>

#include <everest/slac/protocol/homeplug_message.hpp>

namespace everest::slac::ev {

namespace event {

struct Reset {};

/// The only event that advances a state's timeout.
struct Update {};

struct TriggerMatching {};

/// Borrowed for the duration of the feed() call and never stored: a state that needs to keep
/// something out of it copies that something out.
using Message = std::reference_wrapper<messages::HomeplugMessage const>;

} // namespace event

using SlacEvent = std::variant<event::Reset, event::Update, event::TriggerMatching, event::Message>;

/// The message itself rather than the event wrapping it, or nullptr for any other event.
inline messages::HomeplugMessage const* get_if_message(SlacEvent const& ev) {
    auto const* message = std::get_if<event::Message>(&ev);
    return (message != nullptr) ? &message->get() : nullptr;
}

} // namespace everest::slac::ev
