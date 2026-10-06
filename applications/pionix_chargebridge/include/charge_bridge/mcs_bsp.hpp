// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <protocol/cb_management.h>
#include <protocol/evse_bsp_cb_to_host.h>

namespace charge_bridge {

// Bit positions of SafetyErrorFlags (shared/protocol/cb_common.h), for the code that has to work on
// the flag word rather than on a named bitfield: change detection, edge publication, log rendering.
//
// This lived twice - once in evse_bsp_api.cpp and once in ev_bsp_api.cpp, copied - which is how both
// copies came to stop at bit 16 while the wire header grew bits 17 and 18. Two files disagreeing
// about a bit position is a silent misattribution of a live fault, so the positions now have exactly
// one definition. What each side DOES with a bit still differs and stays where it is: the EVSE API
// raises EVerest errors, the EV API only logs (its interface has no error surface yet).
enum class safety_error_mask : std::uint32_t {
    cp_not_state_c = (1u << 0),
    pwm_not_enabled = (1u << 1),
    pp_invalid = (1u << 2),
    plug_temperature_too_high = (1u << 3),
    internal_temperature_too_high = (1u << 4),
    emergency_input_latched = (1u << 5),
    relay_health_latched = (1u << 6),
    vdd_3v3_out_of_range = (1u << 7),
    vdd_core_out_of_range = (1u << 8),
    vdd_12V_out_of_range = (1u << 9),
    vdd_N12V_out_of_range = (1u << 10),
    vdd_refint_out_of_range = (1u << 11),
    external_allow_power_on = (1u << 12),
    config_mem_error = (1u << 13),
    dc_hv_ov_emergency = (1u << 14),
    // Bit 15 is dc_hv_ov_error. Deliberately absent: the over-voltage module owns both HV bits and
    // publishes them on the OVM interface from the named bitfields (ovm_api.cpp), so naming it here
    // would invite a second, duplicate publication on the BSP interface. Bit 14 is listed only
    // because the BSP table already published it before OVM existed, with a FIXME saying so.
    rcd_error = (1u << 16),
    // MCS basic signalling (IEC 61851-23-3 Annex CC). Both are emergency-shutdown causes on their
    // own conductor: CE is the Charge Enable line (the MCS analogue of CP) and ID is Insertion
    // Detection (the analogue of PP). A CCS board never sets either.
    ce_fault = (1u << 17),
    id_fault = (1u << 18),
};

constexpr std::uint32_t to_bit(safety_error_mask mask) {
    return static_cast<std::uint32_t>(mask);
}

// --- board class -----------------------------------------------------------------------------

// Is this an MCS board? Taken from the technology the MCU reports in its heartbeat link status,
// which it latches at plc_init and never changes, so it is stable from the first reply onwards.
//
// Deliberately not taken from the BSP packet's ce_state: that reads NotApplicable on an MCS board
// too, until the CE classifier has cast its first vote. A gate that flickers would publish CCS-shaped
// PP data for the first few packets of every session.
constexpr bool is_mcs_technology(std::uint8_t technology) {
    return technology == CB_LINK_TECH_SPE;
}

// One-way latch for the board class, mirroring the latch-until-reboot discipline cb_type already
// follows on the MCU side.
//
// The MCU latches its technology at plc_init, so in practice it never changes - but "the peer
// promises not to" is not a property this side can rely on. A swapped ChargeBridge behind an
// unchanged host process, or a single garbled heartbeat, would otherwise be enough to move the class
// from SPE back to PLC and quietly re-enable PP publication on an MCS connector: exactly the
// fabricated cable rating the gate exists to prevent. First real value wins; a later disagreeing one
// is refused and reported once.
//
// A retarget is the one legitimate way to a different board, so reset() re-opens the latch - the same
// reasoning that resets the role and uptime state when the heartbeat endpoint changes.
class technology_latch {
public:
    enum class result {
        // Nothing to say: UNKNOWN carries no claim, and the latched value repeated carries no news.
        ignored,
        // First real value: adopted.
        latched,
        // A different real value arrived. The latched one stands; report this, once.
        conflict,
    };

    result offer(std::uint8_t technology) {
        if (technology == CB_LINK_TECH_UNKNOWN) {
            return result::ignored;
        }
        if (m_value == CB_LINK_TECH_UNKNOWN) {
            m_value = technology;
            return result::latched;
        }
        if (technology == m_value) {
            return result::ignored;
        }
        if (m_conflict_reported) {
            return result::ignored;
        }
        m_conflict_reported = true;
        return result::conflict;
    }

    std::uint8_t value() const {
        return m_value;
    }

    bool latched() const {
        return m_value != CB_LINK_TECH_UNKNOWN;
    }

    // Retarget: the next endpoint may legitimately be a different board, so the latch - and the
    // once-only report that goes with it - starts over.
    void reset() {
        m_value = CB_LINK_TECH_UNKNOWN;
        m_conflict_reported = false;
    }

private:
    std::uint8_t m_value{CB_LINK_TECH_UNKNOWN};
    bool m_conflict_reported{false};
};

// --- MCS diagnostics naming ------------------------------------------------------------------

// CE system states, Table CC.103. Diagnostics only: the functional interface stays cp_state, which
// the MCU synthesizes from these.
constexpr char const* ce_state_name(std::uint8_t state) {
    switch (state) {
    case CeState_NotApplicable:
        return "n/a";
    case CeState_A:
        return "A";
    case CeState_B0:
        return "B0";
    case CeState_B0_Aux:
        return "B0_Aux";
    case CeState_B:
        return "B";
    case CeState_B_Aux:
        return "B_Aux";
    case CeState_C:
        return "C";
    case CeState_C_Aux:
        return "C_Aux";
    case CeState_EC:
        return "EC";
    case CeState_EC_Aux:
        return "EC_Aux";
    case CeState_E:
        return "E";
    case CeState_Invalid:
        return "Invalid";
    default:
        return "unknown";
    }
}

// ID states, Table CC.106 (EVSE view) and CC.107 (EV view). One enum serves both roles, so the
// spelling has to make clear which values only one role can report - an EVSE that ever showed
// "Mated_EVSE" would be reporting itself.
constexpr char const* id_state_name(std::uint8_t state) {
    switch (state) {
    case IdState_NotApplicable:
        return "n/a";
    case IdState_Unmated:
        return "Unmated";
    case IdState_Mated_EV:
        return "Mated_EV";
    case IdState_Mated_EVAux:
        return "Mated_EVAux";
    case IdState_Mated_EVSEAux:
        return "Mated_EVSEAux";
    case IdState_Invalid:
        return "Invalid";
    // EV-view only: the EV drives the 5 V source itself, so only it can tell a dangling inlet from
    // an unmated connector, and "mated to nothing" from "mated to a live EVSE".
    case IdState_Inlet_Not_Present:
        return "Inlet_Not_Present";
    case IdState_Mated_NoEVSE:
        return "Mated_NoEVSE";
    case IdState_Mated_EVSE:
        return "Mated_EVSE";
    default:
        return "unknown";
    }
}

// UNDEFINED means "no lock hardware configured", not "not known yet" - only UNLOCKED is a positive
// statement that the connector is open.
constexpr char const* lock_state_name(std::uint8_t state) {
    switch (state) {
    case LockState_UNDEFINED:
        return "n/a";
    case LockState_UNLOCKED:
        return "unlocked";
    case LockState_LOCKED:
        return "locked";
    default:
        return "unknown";
    }
}

} // namespace charge_bridge
