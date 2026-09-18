// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/session/protocol.hpp>

#include <iso15118/ev/d2/context.hpp>
#include <iso15118/ev/d2/state/session_setup.hpp>
#include <iso15118/ev/d2/states.hpp>
#include <iso15118/ev/d2/timeouts.hpp>
#include <iso15118/ev/pre_iso20_engine.hpp>

namespace iso15118::ev::d2 {

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

// What ISO 15118-2 contributes to ev::PreIso20Engine; everything else an engine does is the same
// as DIN SPEC 70121's and lives in the template.
struct EngineTraits {
    using Context = d2::Context;
    using StateBase = d2::StateBase;
    using StateID = d2::StateID;
    using Variant = message_2::Variant;
    using MessageExchange = d2::MessageExchange;
    using InitialState = state::SessionSetup;
    using Timeouts = detail::TimeoutTable;
    using ControlEvent = d2::ControlEvent;
    using StopCharging = d2::StopCharging;
    using PauseCharging = d2::PauseCharging;
    using CpState = d2::CpState;

    static constexpr ProtocolId PROTOCOL = ProtocolId::ISO15118_2;
    static const char* name() {
        return "ISO 15118-2";
    }
};

using Engine = ev::PreIso20Engine<EngineTraits>;

} // namespace iso15118::ev::d2
