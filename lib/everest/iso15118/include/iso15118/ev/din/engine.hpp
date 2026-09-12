// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/session/protocol.hpp>

#include <iso15118/ev/din/context.hpp>
#include <iso15118/ev/din/state/session_setup.hpp>
#include <iso15118/ev/din/states.hpp>
#include <iso15118/ev/din/timeouts.hpp>
#include <iso15118/ev/pre_iso20_engine.hpp>

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

// What DIN SPEC 70121 contributes to ev::PreIso20Engine. The list is the same four things
// ISO 15118-2 supplies, which is the whole point of the template.
struct EngineTraits {
    using Context = din::Context;
    using StateBase = din::StateBase;
    using StateID = din::StateID;
    using Variant = message_din::Variant;
    using MessageExchange = din::MessageExchange;
    using InitialState = state::SessionSetup;
    using Timeouts = detail::TimeoutTable;
    using ControlEvent = din::ControlEvent;
    using StopCharging = din::StopCharging;
    using PauseCharging = din::PauseCharging;
    using CpState = din::CpState;

    static constexpr ProtocolId PROTOCOL = ProtocolId::DIN70121;
    static const char* name() {
        return "DIN SPEC 70121";
    }
};

using Engine = ev::PreIso20Engine<EngineTraits>;

} // namespace iso15118::ev::din
