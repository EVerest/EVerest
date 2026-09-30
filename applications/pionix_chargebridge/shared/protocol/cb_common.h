// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include "cb_platform.h"
#include <stdint.h>

// Structs

typedef union _SafetyErrorFlags {
    struct _flags {
        uint32_t cp_not_state_c : 1;
        uint32_t pwm_not_enabled : 1;
        uint32_t pp_invalid : 1;
        uint32_t plug_temperature_too_high : 1;
        uint32_t internal_temperature_too_high : 1;
        uint32_t emergency_input_latched : 1;
        uint32_t relay_health_latched : 1;
        uint32_t vdd_3v3_out_of_range : 1;
        uint32_t vdd_core_out_of_range : 1;
        uint32_t vdd_12V_out_of_range : 1;
        uint32_t vdd_N12V_out_of_range : 1;
        uint32_t vdd_refint_out_of_range : 1;
        uint32_t external_allow_power_on : 1;
        uint32_t config_mem_error : 1;
        uint32_t dc_hv_ov_emergency : 1;
        uint32_t dc_hv_ov_error : 1;
        uint32_t rcd_error : 1;
        // MCS (IEC 61851-23-3 configuration HH) only. Deliberately NOT folded into the CP/diode
        // fault bits: CE and ID are separate conductors with their own failure modes, and the host
        // must be able to attribute the emergency to the right one.
        // ce_fault: CE signal integrity lost -- CE voltage in no window defined for the current
        //   S S3 position, no X S2 supply (CE state E), or an unintended exit from CE state
        //   C/C_Aux while S S3 was closed (CC.4.3 emergency shutdown).
        // id_fault: insertion detection lost -- ID voltage < 0,3 V DC or not mated while in CE
        //   state C/C_Aux (CC.4.4 emergency shutdown).
        uint32_t ce_fault : 1;
        uint32_t id_fault : 1;
        // 19 named bits + 13 reserved = exactly 32, so this union is 4 bytes and `raw` covers all
        // of it.
        //
        // It used to be 33 bits ("reserved : 16" against 17 named), which made the union 8 bytes
        // wide with the top reserved bit unreachable through `raw`. That was parked rather than
        // fixed because correcting it moves every field after error_flags in
        // evse_bsp_cb_to_host -- so it was deliberately folded into the EV-role wire break rather
        // than shipped as a drive-by change of its own.
        uint32_t reserved : 13;
    } flags;
    uint32_t raw;
} SafetyErrorFlags;


typedef enum _CpState : uint8_t {
    CpState_A,
    CpState_B,
    CpState_C,
    CpState_D,
    CpState_E,
    CpState_F,
    CpState_DF,
    CpState_INVALID
} CpState;

// Connector lock position. Reported in evse_bsp_cb_to_host.lock_state, and consumed by the secure
// partition for the MCS EV-role interlock (Table CC.110 t5 -> t6: the EV locks the connector before
// closing S V3), which is why it lives here rather than in the BSP header.
//
// UNDEFINED is not "unknown yet" but "no lock hardware configured" -- distinct from UNLOCKED,
// because only the latter is a positive statement that the connector is open.
typedef enum _LockState {
    LockState_UNDEFINED = 0,
    LockState_UNLOCKED = 1,
    LockState_LOCKED = 2
} LockState;

// MCS basic signalling, IEC 61851-23-3 Annex CC (vehicle connector configuration HH). On an MCS
// Chargebridge the CP contact becomes Charge Enable (CE) and the PP contact becomes Insertion
// Detection (ID); both are DC-voltage-coded lines.
//
// These two states are DIAGNOSTICS ONLY. The functional interface towards the host stays
// evse_bsp_cb_to_host.cp_state, which the firmware synthesizes from the CE state (A -> A,
// B0/B0_Aux/B/B_Aux -> B, C/C_Aux -> C, EC/EC_Aux -> F, E and any integrity fault -> E) so the
// host's IEC 61851-1 CP event model keeps working unchanged. CCS boards always report
// CeState_NotApplicable / IdState_NotApplicable.

// CE system states of Table CC.103, classified from the CE voltage (Table CC.104) plus the EVSE's
// own S S3 position. Reachability note: the EVSE cannot distinguish C from C_Aux or EC from
// EC_Aux by CE voltage alone (Table CC.104 gives them one shared row, the S V4 aux request is not
// coded into those levels), so the firmware only ever reports C and EC; whether the EV requests
// auxiliary power is visible in id_state (IdState_Mated_EVAux) and, below state C, in the
// B0_Aux/B_Aux states. C_Aux/EC_Aux are kept in the enum so the value set matches the standard.
typedef enum _CeState : uint8_t {
    CeState_NotApplicable = 0, // not an MCS board, or CE not evaluated yet
    CeState_A = 1,             // not mated
    CeState_B0 = 2,            // mated, EVSE not ready (S S3 open)
    CeState_B0_Aux = 3,        // ... and the EV requests auxiliary power
    CeState_B = 4,             // mated, EVSE ready (S S3 closed), EV not ready
    CeState_B_Aux = 5,         // ... and the EV requests auxiliary power
    CeState_C = 6,             // ready for power transfer (also covers C_Aux, see above)
    CeState_C_Aux = 7,         // reserved: not distinguishable from C on the EVSE side
    CeState_EC = 8,            // emergency shutdown by the EVSE (S S3 opened while S V3 closed)
    CeState_EC_Aux = 9,        // reserved: not distinguishable from EC on the EVSE side
    CeState_E = 10,            // no X S2 voltage
    CeState_Invalid = 11       // CE voltage in no window defined for the current S S3 position
} CeState;

// ID states, Table CC.106 (EVSE view) and Table CC.107 (EV view). One enum serves both roles: each
// value names one physical condition, so no value needs the reader to know which role reported it.
// The VOLTAGE WINDOWS differ per role, though -- the EV drives the 5 V source X V1 itself, so an
// unmated line reads 2,47 V to the EV and 0 V to the EVSE. The windows live in the two classifiers
// (id_robust_filter.h, id_ev_robust_filter.h), not here.
//
// The EV's view is the richer one: only it can tell a dangling inlet from an unmated connector, and
// "mated to nothing" from "mated to a live EVSE". The three EV-only values below are why.
//
// Mated_EVAux means the EV feeds its own auxiliary supply (X V2, 9..32 V) onto the ID conductor
// and Mated_EVSEAux means the EVSE feeds X S1 (24 V). The Chargebridge detects and reports both but
// supplies neither: there is no EVSE-side X S1 source in the hardware, and whether it can source
// X V2 in EV role is an open hardware question.
typedef enum _IdState : uint8_t {
    IdState_NotApplicable = 0,     // not an MCS board, or ID not evaluated yet
    IdState_Unmated = 1,           // EVSE view -0,1..0,1 V; EV view 2,19..2,75 V
    IdState_Mated_EV = 2,          // EVSE view only: an EV is mated (0,74..1,37 V)
    IdState_Mated_EVAux = 3,       // 8,4 .. 32 V, the EV's auxiliary supply is present
    IdState_Mated_EVSEAux = 4,     // 21,0 .. 26,4 V, the EVSE's auxiliary supply is present
    IdState_Invalid = 5,           // ID voltage in no window defined for this role
    IdState_Inlet_Not_Present = 6, // EV view only: nothing plugged into the inlet (4,5..5,5 V)
    IdState_Mated_NoEVSE = 7,      // EV view only: mated, but no EVSE behind it (1,43..1,82 V)
    IdState_Mated_EVSE = 8         // EV view only: mated to a live EVSE (0,74..1,37 V)
} IdState;
