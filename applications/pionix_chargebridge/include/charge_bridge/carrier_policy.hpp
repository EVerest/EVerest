// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <protocol/cb_common.h>
#include <protocol/cb_management.h>

namespace charge_bridge {

// How the PLC tap device's carrier is driven (config key plc.carrier).
enum class carrier_mode {
    // Never issue TUNSETCARRIER; the kernel default (carrier on) stands. This is the HomePlug/PLC
    // setting and the default for every existing installation.
    none,
    // The carrier mirrors the state of the MCU's own SPE PHY, as reported by the link status
    // embedded in every heartbeat reply. This is the MCS setting.
    firmware,
};

// What to do when the running kernel does not implement TUNSETCARRIER (added in Linux v5.0), in
// carrier: firmware mode (config key plc.carrier_fallback).
enum class carrier_fallback {
    // Refuse to start the plc bridge. The default: MCS link supervision that has silently degraded
    // to a permanent "link up" is exactly what this feature exists to prevent, so an absent bridge
    // is the safer failure.
    fail,
    // One loud warning plus a status indicator, carrier permanently on, keep bridging. Trades
    // supervision for availability.
    warn,
};

// Whether the carrier is additionally gated on basic-signalling state (config key plc.carrier_gate).
enum class carrier_gate {
    // The carrier follows the PHY alone. Today's behavior and the default.
    none,
    // The carrier additionally requires a mated CE state, so the netdev's link exists exactly while
    // a vehicle is physically present. This is what removes the races between the data link and the
    // ISO 15118-20 stack: no session can start against a stale link from a previous plug-in, and an
    // unplug is a deterministic, immediate link-down instead of waiting for neighbour liveness (the
    // PLCA coordinator structurally cannot see its peer leave at the PHY level).
    ce_mated,
};

// True for every CE system state of Table CC.103 in which a vehicle is mated. Deliberately CE, not
// ID: the ID windows are dishonest on current hardware (missing R S2, see MCS_STATE_HANDLING_PLAN),
// while CE is honest raw - and on the EV side the published CE state already folds in the ID
// plausibility check that resolves the A/E ambiguity. B0 counts: the standard has the data link
// established after mating and BEFORE S S3 closes (CC.3.2 t0), and our own S S3 close is gated on
// phy_operational - gating the link on B instead would deadlock that. EC counts: during an
// EVSE-side emergency and the CC.5.2.3.2 restart guard the segment must survive, so restart and
// ISO-20 teardown messaging are not cut off mid-sequence. Everything unknown is unmated: fail-safe
// is link-down.
constexpr bool ce_state_mated(std::uint8_t ce_state) {
    switch (ce_state) {
    case CeState_B0:
    case CeState_B0_Aux:
    case CeState_B:
    case CeState_B_Aux:
    case CeState_C:
    case CeState_C_Aux:
    case CeState_EC:
    case CeState_EC_Aux:
        return true;
    default:
        // A (not mated), E (X S2 lost - no session possible), Invalid, NotApplicable (CCS board, or
        // MCS before the classifier's first vote), and any future value.
        return false;
    }
}

// Everything the carrier decision depends on. A plain value type on purpose: the decision is a pure
// function of these inputs, which is what makes the decision table testable without a kernel, a tap
// device or a ChargeBridge.
struct carrier_inputs {
    carrier_mode mode{carrier_mode::none};
    // Heartbeat-verified reachability of the ChargeBridge.
    bool cb_connected{false};
    // Last reported state of the MCU's own PHY (CbLinkStatusPacket).
    bool phy_operational{false};
    bool plca_engaged{false};
    // CbLinkTechnology as reported by the MCU; UNKNOWN until the first report arrives.
    std::uint8_t technology{CB_LINK_TECH_UNKNOWN};
    // The kernel does not implement TUNSETCARRIER.
    bool carrier_unsupported{false};
    carrier_fallback fallback{carrier_fallback::fail};
    // Basic-signalling gate (config key plc.carrier_gate) and its input, ce_state_mated() of the
    // last CE state reported in the BSP status packet. False until the first report: a link that
    // waits the few ms for the classifier's first vote is cheaper than one that exists before the
    // mate is proven.
    carrier_gate gate{carrier_gate::none};
    bool ce_mated{false};
};

struct carrier_decision {
    // Issue TUNSETCARRIER at all? False means "leave the device alone".
    bool apply{false};
    // The state to request; only meaningful when apply is set.
    bool carrier{true};
    // The MCU reports a technology the configured mode does not expect. Always a warning.
    bool technology_mismatch{false};
    // firmware mode on a board that reports PLC: the carrier is forced on and the reported flags are
    // ignored (fail-open, see below).
    bool plc_fail_open{false};
    // Unsupported kernel with the fail policy: the bridge must not run at all.
    bool refuse_bridge{false};
};

// The one place that decides what the tap's carrier should be. Called on every input change
// (link-status report, connection edge, tap reset) and once at construction to evaluate the
// unsupported-kernel policy.
inline carrier_decision decide_carrier(carrier_inputs const& in) {
    carrier_decision out;

    // The MCU's technology field is the runtime cross-check of the host's configuration: the MCU
    // knows PLC vs SPE from the hardware variant. It is latched sticky at plc_init on the MCU and the
    // bridge latches it again on this side, so it never flaps; UNKNOWN only means "no reply has
    // arrived yet" and is not a mismatch.
    const bool technology_known = in.technology != CB_LINK_TECH_UNKNOWN;
    const bool firmware_is_plc = in.technology == CB_LINK_TECH_PLC;
    if (technology_known) {
        out.technology_mismatch = (in.mode == carrier_mode::firmware) ? firmware_is_plc : not firmware_is_plc;
    }

    if (in.mode == carrier_mode::none) {
        // Byte for byte today's behavior. Mandatory for HomePlug: SLAC MMEs (CM_SET_KEY, sounding)
        // cross the tap before any AVLN exists, and an AF_PACKET send on a carrier-off tap reports
        // success while the frame is silently dropped - SLAC could not even detect the condition.
        return out;
    }

    if (in.carrier_unsupported) {
        // No TUNSETCARRIER in this kernel: either refuse the bridge, or let the carrier stay
        // permanently on and accept that supervision degrades to the old always-up stub.
        out.refuse_bridge = in.fallback == carrier_fallback::fail;
        return out;
    }

    if (firmware_is_plc) {
        // Fail-open. A HomePlug board reports technology = PLC with all other fields zero, so
        // applying those flags on a misconfigured installation would strangle SLAC. Force the carrier
        // on and stop looking at the flags; technology_mismatch keeps the warning coming.
        out.apply = true;
        out.carrier = true;
        out.plc_fail_open = true;
        return out;
    }

    // The MCS rule: "ChargeBridge reachable AND its own SPE PHY operational". Deliberately not EV
    // presence and not EV liveness - the SECC is the PLCA coordinator and structurally has no peer
    // signal at the PHY level (no autoneg per V2G10-019/-021, PLCA_STS.PST asserts without a peer).
    //
    // With plc.carrier_gate: ce_mated, additionally "AND a vehicle is mated per the CE state" - the
    // physical-presence signal the PHY cannot provide (see carrier_gate above). Note the ordering
    // against the fail-open branch: a PLC board can never be strangled by the gate, because
    // fail-open returned before this line.
    out.apply = true;
    out.carrier = in.cb_connected and in.phy_operational and in.plca_engaged and
                  (in.gate == carrier_gate::none or in.ce_mated);
    return out;
}

} // namespace charge_bridge
