// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

namespace iso15118::ev::session {

// Why the session stops. Chosen where the EV leaves a charge loop and carried through PowerDelivery(Stop) and
// welding detection to SessionStop; a failed welding check replaces it with EvStop. A state before
// PowerDelivery(Start) passes its reason straight to SessionStop instead: an EV stop or pause latched before
// PowerDelivery(Start) (stop_before_start), an EV abort at -2 or DIN ServiceDiscovery, a DIN EVSE stop at
// ChargeParameterDiscovery or CableCheck, or a -2 EV with no usable contract certificate and no ExternalPayment
// offer after ServiceDetail (EvStop). SessionStop may only raise a pause to a stop (raise_to_latched_stop).
// From SessionStopReq on the reason is fixed, so the request and the paused outcome cannot disagree.
// -20 sends a pause latched before PowerDelivery(Start) as EvStop instead [V2G20-2644].
enum class StopReason {
    EvStop,
    EvPause,
    EvseTerminate,
    EvsePause,
};

constexpr bool is_pause(StopReason reason) {
    switch (reason) {
    case StopReason::EvPause:
    case StopReason::EvsePause:
        return true;
    case StopReason::EvStop:
    case StopReason::EvseTerminate:
        return false;
    }
    return false;
}

// Any stop outranks any pause; between two of a kind the EVSE's wins. With neither side asking,
// the session terminates.
StopReason combine(std::optional<StopReason> ev, std::optional<StopReason> evse);

// The EV's latched request, a stop outranking a pause.
template <typename Context> std::optional<StopReason> requested_by_ev(const Context& ctx) {
    if (ctx.is_stop_charging_requested()) {
        return StopReason::EvStop;
    }
    if (ctx.is_pause_charging_requested()) {
        return StopReason::EvPause;
    }
    return std::nullopt;
}

// At SessionStop: an EV stop latched during the stop walk outranks the pause carried from the loop.
template <typename Context> StopReason raise_to_latched_stop(const Context& ctx, StopReason carried) {
    if (is_pause(carried) and ctx.is_stop_charging_requested()) {
        return StopReason::EvStop;
    }
    return carried;
}

} // namespace iso15118::ev::session
