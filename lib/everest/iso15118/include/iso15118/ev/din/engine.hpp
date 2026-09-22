// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/session/protocol.hpp>

#include <iso15118/ev/din/context.hpp>
#include <iso15118/ev/din/state/session_setup.hpp>
#include <iso15118/ev/din/states.hpp>
#include <iso15118/ev/din/timeouts.hpp>
#include <iso15118/ev/session/pre_iso20_engine.hpp>

namespace iso15118::ev::din {

namespace detail {

struct TimeoutTable {
    static constexpr auto MESSAGE = timeouts::MESSAGE;
    static constexpr auto MIN_REQUEST_INTERVAL = timeouts::MIN_REQUEST_INTERVAL;
    static std::chrono::milliseconds response_timeout(StateID state) {
        return timeouts::response_timeout(state);
    }
    static std::optional<std::chrono::milliseconds> ongoing_timeout(StateID state) {
        return timeouts::ongoing_timeout(state);
    }
};

} // namespace detail

// What DIN SPEC 70121 contributes to ev::session::PreIso20Engine. It supplies the same set ISO 15118-2 does,
// which is the whole point of the template: the engine itself is not duplicated.
struct EngineTraits {
    using Context = din::Context;
    using StateBase = din::StateBase;
    using StateID = din::StateID;
    using Variant = message_din::ResponseVariant;
    using MessageExchange = din::MessageExchange;
    using InitialState = state::SessionSetup;
    using Timeouts = detail::TimeoutTable;

    // SessionSetupReq goes out ahead of the initial state.
    static void send_first_request(Context& ctx);

    static constexpr ProtocolId PROTOCOL = ProtocolId::DIN70121;
    // No recovery from bad input: a malformed frame or a message that answers nothing closes the
    // connection [V2G-DC-169] [V2G-DC-173].
    static constexpr bool STRICT_FRAMING = true;
    static std::string_view name() {
        return "DIN SPEC 70121";
    }
};

using Engine = ev::session::PreIso20Engine<EngineTraits>;

} // namespace iso15118::ev::din
