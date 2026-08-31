// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>
#include <protocol/cb_management.h>

namespace charge_bridge {

// The role this ChargeBridge plays, from the charge_bridge.type config key. On an MCS board the MCU
// latches it from the first config heartbeat after boot and drives the role GPIO from it; on a CCS
// board the role is strapping-coded and the MCU reports what the straps say.
enum class cb_role {
    evse,
    ev,
    // The key is absent. Distinct from EVSE on purpose: a config that never mentions a role is not
    // claiming to be an EVSE, it is declining to claim anything - and on a CCS board, where the
    // straps decide, that is the only honest thing a config can say. Sent as its own wire value so
    // the MCU can tell "no preference" from "EVSE, definitely".
    unspecified,
};

// Wire encoding of CbConfig::cb_type and CbHeartbeatReplyPacket::latched_cb_type.
constexpr std::uint8_t cb_type_evse = 0;
constexpr std::uint8_t cb_type_ev = 1;
// Host -> MCU only: charge_bridge.type was not configured, so the MCU picks the role on its own - it
// latches EVSE, the fail-safe role, on an MCS board and keeps the strapped one on a CCS board. Never
// echoed back: the reply reports the role that was actually adopted, so it is one of the two roles.
// The point of a value distinct from EVSE is that it lets the MCU tell "the host asked for the other
// role" from "the host said nothing", and only object to the former.
constexpr std::uint8_t cb_type_unspecified = 2;
// Reply only: the MCU has not had an accepted config heartbeat since it booted, so it has nothing
// latched yet. Its pre-config behaviour is EVSE semantics, which is the fail-safe direction.
constexpr std::uint8_t cb_type_not_latched = 255;

constexpr std::uint8_t to_wire(cb_role role) {
    switch (role) {
    case cb_role::ev:
        return cb_type_ev;
    case cb_role::evse:
        return cb_type_evse;
    case cb_role::unspecified:
        return cb_type_unspecified;
    }
    return cb_type_unspecified;
}

// The MCU's rule for a cb_type it does not recognise: it treats anything the encoding does not
// define as unspecified and stays silent about it. Mirrored here so the host compares against what
// the MCU actually acted on - and, just as importantly, so the host stays as quiet as the MCU. A
// value neither side can interpret expresses no preference, so it can neither contradict a reported
// role nor be contradicted by one. The three defined values pass through unchanged, unspecified
// included: that one is a real instruction, not a fallback.
constexpr std::uint8_t normalize_cb_type(std::uint8_t configured) {
    return configured > cb_type_unspecified ? cb_type_unspecified : configured;
}

constexpr char const* to_string(cb_role role) {
    switch (role) {
    case cb_role::ev:
        return "EV";
    case cb_role::evse:
        return "EVSE";
    case cb_role::unspecified:
        return "not configured";
    }
    return "not configured";
}

// Spelling of a wire value, for logs and the dashboard. Anything the encoding does not define reads
// as unknown rather than being mapped to a role - the host must not invent a role it was not told.
constexpr char const* cb_type_name(std::uint8_t wire) {
    switch (wire) {
    case cb_type_evse:
        return "EVSE";
    case cb_type_ev:
        return "EV";
    case cb_type_unspecified:
        return "not configured";
    case cb_type_not_latched:
        return "not yet latched";
    default:
        return "unknown";
    }
}

// --- role latch cross-check ------------------------------------------------------------------

enum class role_latch_state {
    // charge_bridge.type is absent, so there is nothing to disagree with: whatever role the MCU runs
    // is the role this installation asked for. This is the normal state of every config that predates
    // the key, and the only correct state for a strapped CCS board.
    not_configured,
    // The MCU has had no ACCEPTED config heartbeat since it booted. Not an error: the first config
    // that reaches the MCU applies, so this normally resolves within a heartbeat interval. It only
    // appears on an MCS board - a CCS board reports its strapped role from the start - and if it
    // persists, the config is not being accepted at all (a config_version the MCU rejects is the
    // likely cause, which ship-together makes a development-time state).
    not_latched,
    // The MCU runs the role this host asked for.
    matched,
    // The MCU runs a different role than this host is configured for. Needs an operator; what will
    // actually fix it depends on the board (see role_mismatch_remedy).
    mismatched,
};

// The configured side is normalised, because that is what the MCU did with it before acting. The
// reported side is not: cb_type_not_latched is a meaningful value and must stay distinct from a role.
//
// An unconfigured type never mismatches. That is not leniency: the host has expressed no preference,
// so no reported role can contradict it - and on a CCS board, where the straps are authoritative and
// no reboot can change them, treating "absent" as "EVSE" would raise a permanent alarm on a correctly
// configured EV station.
constexpr role_latch_state evaluate_role_latch(std::uint8_t configured, std::uint8_t latched) {
    const std::uint8_t wanted = normalize_cb_type(configured);
    if (wanted == cb_type_unspecified) {
        return role_latch_state::not_configured;
    }
    if (latched == cb_type_not_latched) {
        return role_latch_state::not_latched;
    }
    return latched == wanted ? role_latch_state::matched : role_latch_state::mismatched;
}

// What an operator has to do about a mismatch, which depends on which side owns the role.
//
// On a CCS board (the MCU reports the PLC technology) the role comes from strapping resistors. The
// MCU cannot adopt a different one, so telling anybody to reboot would be false advice - the config
// is what has to change. On an MCS board the role is latched from the first config after boot, so a
// reboot is exactly what applies a changed type.
//
// technology is CbLinkTechnology as reported in the heartbeat reply's link status. UNKNOWN gets its
// own wording rather than borrowing either: it is a lasting state, not a transient one. A board whose
// plc block is disabled never reports a technology at all, so on a CCS board with an explicitly
// contradicting type the MCS advice would be confidently wrong - and reboot efficacy must never be
// claimed without knowing which side owns the role.
constexpr char const* role_mismatch_remedy(std::uint8_t technology) {
    switch (technology) {
    case CB_LINK_TECH_PLC:
        return "this board's role is strapping-coded; align or remove charge_bridge.type";
    case CB_LINK_TECH_SPE:
        return "an MCU reboot is required to apply the configured type";
    default:
        return "on a strapping-coded (CCS) board align or remove charge_bridge.type; "
               "on an MCS board an MCU reboot applies it";
    }
}

// --- station_id derivation -------------------------------------------------------------------

enum class station_id_issue {
    none,
    // EVSE with a station id other than 0. The SECC is the PLCA coordinator, station 0, per
    // ISO 15118-10; anything else makes it a follower on its own link. Honoured, with a warning.
    evse_not_coordinator,
    // EV on station 0, which is the coordinator's id - that is the EVSE's seat. Honoured, with a
    // warning.
    ev_is_coordinator,
    // Outside the PLCA node id range. Three bits carry the id, so 0..7 are the only node ids, and
    // -1 is the host's spelling for "no PLCA, use collision detection". Fatal, see is_fatal.
    out_of_range,
};

// An out-of-range id is refused rather than corrected. Any fallback would have to guess, and the
// guess for an EVSE is station 0 - the coordinator seat, which is precisely the outcome a typo must
// not be able to produce silently. Refusing also matches how charge_bridge.type treats a value it
// does not recognise, so the two keys have one severity between them.
constexpr bool is_fatal(station_id_issue issue) {
    return issue == station_id_issue::out_of_range;
}

struct station_id_decision {
    int station_id{0};
    station_id_issue issue{station_id_issue::none};
    // True when no station_id was configured and the value comes from the role.
    bool derived{false};
};

// charge_bridge.type derives the plc.station_id default: the EVSE is the PLCA coordinator (0) and an
// EV is the first follower (1). An unconfigured type derives like an EVSE - the MCU's own default is
// EVSE semantics, so the derived station id follows it.
//
// An explicit station_id overrides the default, and only the combinations that contradict the role
// are worth saying anything about. In particular an EV on stations 2..7 is a perfectly ordinary drop
// node and says nothing, and -1 is an explicit request for collision detection instead of PLCA, which
// is a legitimate choice for either role. A value that is no node id at all is fatal (see is_fatal);
// the station_id it reports is the offending value, for the message, and never reaches the MCU.
constexpr station_id_decision decide_station_id(cb_role role, std::optional<int> configured) {
    // unspecified derives and warns exactly like EVSE: the role it stands in for is EVSE semantics.
    const bool is_ev = role == cb_role::ev;
    const int role_default = is_ev ? 1 : 0;
    if (not configured.has_value()) {
        return {role_default, station_id_issue::none, true};
    }

    const int value = *configured;
    if (value == -1) {
        return {value, station_id_issue::none, false};
    }
    if (value < -1 or value > 7) {
        return {value, station_id_issue::out_of_range, false};
    }
    if (not is_ev) {
        return {value, value == 0 ? station_id_issue::none : station_id_issue::evse_not_coordinator, false};
    }
    return {value, value == 0 ? station_id_issue::ev_is_coordinator : station_id_issue::none, false};
}

} // namespace charge_bridge
