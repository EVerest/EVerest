// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef EVSE_SLAC_BACKEND_HPP
#define EVSE_SLAC_BACKEND_HPP

// The SLAC library this module is built against, behind one set of names. Chosen with the CMake
// cache variable EVEREST_SLAC_BACKEND; the adapters at the bottom absorb the differences, so no #if
// appears anywhere else in the module.

#include <chrono>
#include <cstdint>
#include <functional>
#include <utility>

#if defined(EVEREST_SLAC_BACKEND_MEOW)

#include <everest/slac/evse/context.hpp>
#include <everest/slac/evse/fsm.hpp>
#include <everest/slac/io/event.hpp>

namespace slac_backend {

using Context = everest::slac::evse::Context;
using ContextCallbacks = everest::slac::evse::ContextCallbacks;
using Config = everest::slac::evse::Config;
using Fsm = everest::slac::evse::FSM;
using SlacEvent = everest::slac::io::SlacEvent;
using HomeplugMessage = everest::slac::messages::HomeplugMessage;
using D3State = everest::slac::D3State;

using SetKeyHandlingMode = everest::slac::evse::SetKeyHandlingMode;
using SetKeyCnfSuccessMode = everest::slac::evse::SetKeyCnfSuccessMode;
using NmkGenerationMode = everest::slac::evse::NmkGenerationMode;

/// Unsolicited Qualcomm vendor frames are dropped before they reach the state machine.
constexpr std::uint16_t VENDOR_ATTENUATION_MMTYPE = everest::slac::defs::mmtype::qualcomm::ATTENUATION_CHARACTERISTICS;

/// Time is injected I/O here: the library reads no clock of its own.
inline void install_time_source(ContextCallbacks& callbacks) {
    callbacks.now = []() { return std::chrono::steady_clock::now(); };
}

/// This library samples the count, so it takes a source and nothing is pushed.
inline void install_bc_transition_count(ContextCallbacks& callbacks, std::function<int()> source) {
    callbacks.bc_transition_count = std::move(source);
}
inline void publish_bc_transition_count(Context&, int) {
}

/// Events carry their own payload, so there is nothing to stash.
inline void stash_payload(Context&, HomeplugMessage const&) {
}

} // namespace slac_backend

#else

#include <everest/slac/fsm/context.hpp>
#include <everest/slac/slac_event.hpp>
#include <everest/slac/slac_fsm.hpp>

namespace slac_backend {

using Context = everest::lib::slac::fsm::evse::Context;
using ContextCallbacks = everest::lib::slac::fsm::evse::ContextCallbacks;
using Config = everest::lib::slac::fsm::evse::EvseSlacConfig;
using Fsm = everest::lib::slac::slac_fsm;
using SlacEvent = everest::lib::slac::SlacEvent;
using HomeplugMessage = everest::lib::slac::messages::HomeplugMessage;
using D3State = everest::lib::slac::D3State;

using SetKeyHandlingMode = everest::lib::slac::fsm::evse::SetKeyHandlingMode;
using SetKeyCnfSuccessMode = everest::lib::slac::fsm::evse::SetKeyCnfSuccessMode;
using NmkGenerationMode = everest::lib::slac::fsm::evse::NmkGenerationMode;

/// Unsolicited Qualcomm vendor frames are dropped before they reach the state machine.
constexpr std::uint16_t VENDOR_ATTENUATION_MMTYPE =
    everest::lib::slac::defs::qualcomm::MMTYPE_QCA_VS_ATTENUATION_CHARACTERISTICS;

// The three points where the two libraries differ.

/// This library reads the clock itself.
inline void install_time_source(ContextCallbacks&) {
}

/// This library keeps the count as an atomic on the context, so it is pushed rather than sampled.
inline void install_bc_transition_count(ContextCallbacks&, std::function<int()>) {
}
inline void publish_bc_transition_count(Context& ctx, int count) {
    ctx.bc_transition_count.store(count);
}

/// This library reads the frame off the context rather than off the event.
inline void stash_payload(Context& ctx, HomeplugMessage const& message) {
    ctx.slac_message_payload = message;
}

} // namespace slac_backend

#endif

#endif // EVSE_SLAC_BACKEND_HPP
