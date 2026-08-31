// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <variant>

#include <everest/slac/protocol/homeplug_message.hpp>

namespace everest::slac::evse {

namespace event {

struct Reset {};

/// The only event that advances a state's timeout.
struct Update {};

/// Control pilot entered state B, C or D.
struct EnterBcd {};

/// Control pilot left state B, C or D.
struct LeaveBcd {};

/// Borrowed for the duration of the feed() call and never stored: a state that needs to keep
/// something out of it copies that something out.
struct Message {
    messages::HomeplugMessage const& frame;
};

} // namespace event

using SlacEvent = std::variant<event::Reset, event::Update, event::EnterBcd, event::LeaveBcd, event::Message>;

/// The frame itself rather than the event wrapping it, or nullptr for any other event.
inline messages::HomeplugMessage const* as_frame(SlacEvent const& ev) {
    auto const* msg = std::get_if<event::Message>(&ev);
    return (msg != nullptr) ? &msg->frame : nullptr;
}

} // namespace everest::slac::evse
