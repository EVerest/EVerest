// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include <iso15118/message_2/common_types.hpp>

namespace iso15118::d2::ev {

namespace dt = message_2::datatypes;

// Session-scoped EVCC configuration for ISO 15118-2. EIM (ExternalPayment) only.
struct EvSessionConfig {
    // EVCCID carried in SessionSetupReq (the EV MAC address).
    std::array<uint8_t, 6> evcc_mac{0x00, 0x7d, 0xfa, 0x00, 0x00, 0x00};

    // The energy transfer mode the EV requests in ChargeParameterDiscoveryReq. Its AC/DC family also
    // selects the charging branch. Default DC (DC_extended); use AC_three_phase_core for AC.
    dt::EnergyTransferMode requested_energy_transfer_mode{dt::EnergyTransferMode::DC_extended};

    // AC charge parameters (AC_EVChargeParameter).
    float ac_e_amount{60000.0f};
    float ac_ev_max_voltage{400.0f};
    float ac_ev_max_current{32.0f};
    float ac_ev_min_current{10.0f};

    // DC charge parameters (DC_EVChargeParameter / PreCharge / CurrentDemand targets).
    float dc_ev_max_voltage{900.0f};
    float dc_ev_max_current{300.0f};
    std::optional<float> dc_ev_max_power{std::nullopt};
    float dc_target_voltage{400.0f};
    float dc_target_current{20.0f};
    float dc_energy_capacity{60000.0f};
    std::optional<float> dc_energy_request{std::nullopt};
    std::optional<int8_t> dc_full_soc{std::nullopt};
    std::optional<int8_t> dc_bulk_soc{std::nullopt};

    // When set, the SessionSetupReq carries this id to re-join a paused session (expects
    // OK_OldSessionJoined).
    std::optional<std::array<uint8_t, 8>> resumed_session_id{std::nullopt};
};

} // namespace iso15118::d2::ev
