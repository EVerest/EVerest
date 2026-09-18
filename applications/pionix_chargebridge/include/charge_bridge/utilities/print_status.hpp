// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace charge_bridge::utilities {

enum class status_output_mode {
    auto_mode,
    log,
    terminal,
    off,
};

// Numeric telemetry from the heartbeat reply packet. Used by the interactive terminal UI for
// live readouts and sparklines; not part of the key=value log output.
struct chargebridge_telemetry {
    int cp_hi_mV{};
    int cp_lo_mV{};
    int pp_mOhm{};
    int temperature_mcu_C{};
    int temperature_pcb_C{};
    int vdd_12V_mV{};
    int vdd_N12V_mV{};
    int vdd_3v3_mV{};
};

// Read-only network identity of an instance, surfaced by the interactive terminal UI only (not part
// of the key=value log output). mDNS fields are empty for fixed-IP configs. (No MAC: the MCU does
// not report one anywhere in the protocol.)
struct chargebridge_network_info {
    std::string ip;
    std::string mdns_hostname;
    std::string mdns_service;
    std::vector<std::pair<std::string, std::string>> mdns_txt;
};

// PLC/SPE link state of the plc bridge: the link status from the MCU's last heartbeat reply plus what
// the bridge did with it. Surfaced by the interactive terminal UI only (not part of the key=value log
// output). Plain types on purpose - this header is the UI-facing status contract and stays free of
// the wire protocol.
struct chargebridge_link_status {
    // "none" or "firmware" (config key plc.carrier).
    std::string carrier_mode;
    // False until the first reply arrives, and always false in carrier mode none - which records
    // nothing, so this line stays out of a plain-PLC dashboard. The fields below are meaningless then.
    bool have_report{false};
    // CbLinkTechnology: 0 unknown, 1 PLC, 2 SPE.
    int technology{0};
    bool phy_operational{false};
    bool plca_engaged{false};
    // Per-flag edge counter since the MCU booted.
    unsigned int transition_count{0};
    // The carrier the bridge last pushed to the kernel. The bridge's own tracked state, not a
    // kernel query: SIOCGIFFLAGS cannot see IFF_LOWER_UP and lags by up to a second.
    bool carrier_applied{true};
    // The kernel does not implement TUNSETCARRIER; carrier supervision is degraded.
    bool carrier_unsupported{false};
    // The MCU reports a technology the configured mode does not expect.
    bool technology_mismatch{false};
    // plc.carrier_gate: ce_mated is configured, and whether its input currently reads mated. When
    // active and unmated the carrier is held down regardless of the PHY flags - shown so a
    // "phy=1 but applied=DOWN" line explains itself.
    bool gate_active{false};
    bool gate_mated{false};
};

// Configured role vs the role the MCU reports it has latched. Surfaced by the interactive terminal
// UI only. Plain strings on purpose - this header is the UI-facing status contract and stays free of
// the wire protocol.
struct chargebridge_role_status {
    // charge_bridge.type as configured: "EVSE" or "EV".
    std::string configured;
    // What the MCU reports: "EVSE", "EV" or "not yet latched" before its first accepted config.
    std::string latched;
    // charge_bridge.type is absent, so no reported role can contradict it. Not a fault and not a
    // waiting state - it is the normal state of every config that does not mention a role.
    bool not_configured{false};
    // The MCU has not applied a config yet, so there is nothing to compare against. A waiting state,
    // not a fault: it clears as soon as a config is accepted. Only ever seen on an MCS board.
    bool awaiting_latch{false};
    // The MCU is running a different role than the configuration asks for. Needs an operator, and
    // stays marked until one acts.
    bool mismatch{false};
    // What will actually fix a mismatch. Depends on the board: an MCS board latches the role from the
    // first config after boot, a CCS board takes it from strapping resistors and no reboot can change
    // that. Empty unless mismatch is set.
    std::string remedy;
};

struct chargebridge_status {
    std::string cb_name;
    bool connected{false};
    std::optional<chargebridge_network_info> network;
    std::optional<bool> discovered;
    std::optional<bool> can0;
    std::optional<bool> serial1;
    std::optional<bool> serial2;
    std::optional<bool> serial3;
    std::optional<bool> plc;
    std::optional<bool> bsp;
    std::optional<bool> heartbeat;
    std::optional<bool> io;
    std::optional<int> mcu_resets;
    std::optional<chargebridge_telemetry> telemetry;
    // From the plc bridge; used by the interactive terminal UI only.
    std::optional<chargebridge_link_status> link_status;
    // Configured vs latched role; used by the interactive terminal UI only.
    std::optional<chargebridge_role_status> role;
    // From the BSP bridge / IO packet; used by the interactive terminal UI only.
    std::optional<std::string> cp_state;
    // MCS basic-signalling diagnostics from the BSP bridge (IEC 61851-23-3 Annex CC). Unset on a
    // CCS board, which reports these as NotApplicable. cp_state stays the functional interface -
    // the MCU synthesizes it from ce_state - so these are for a human reading the dashboard.
    std::optional<std::string> ce_state;
    std::optional<std::string> id_state;
    std::optional<std::string> lock_state;
    std::optional<std::vector<int>> gpio;
    std::optional<std::vector<int>> adc;
    std::optional<std::vector<std::pair<std::string, int>>> io_telemetry;
};

void print_status(const chargebridge_status& status, status_output_mode output_mode = status_output_mode::terminal);

} // namespace charge_bridge::utilities
