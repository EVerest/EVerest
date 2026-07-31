// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include <iso15118/message_din/common_types.hpp>

namespace iso15118::din::ev {

namespace dt = message_din::datatypes;

// Terminate vs. pause intent captured when a stop/pause control event arrives. DIN SPEC has no
// dedicated field for this; a pause is signalled to the SECC only by later re-joining the session.
enum class ChargingSession {
    Terminate,
    Pause,
};

// DC charge parameters offered by the EV (DIN SPEC 70121 uses plain physical quantities as doubles).
struct DcChargeParameters {
    double max_current_limit{0.0};
    std::optional<double> max_power_limit{std::nullopt};
    double max_voltage_limit{0.0};
    double target_voltage{0.0};
    double target_current{0.0};
    std::optional<double> energy_capacity{std::nullopt};
    std::optional<double> energy_request{std::nullopt};
    std::optional<int8_t> full_soc{std::nullopt};
    std::optional<int8_t> bulk_soc{std::nullopt};
};

// Session-scoped EV configuration for a DIN SPEC 70121 session.
struct EvSessionConfig {
    // EVCCID (6-byte MAC). A placeholder default is used until the module wires a real value.
    std::array<uint8_t, 6> evcc_mac{0x02, 0x00, 0x00, 0x00, 0x00, 0x01};

    dt::EnergyTransferMode requested_energy_transfer_type{dt::EnergyTransferMode::DC_extended};

    DcChargeParameters dc{};

    // When set, the SessionSetupReq carries this id and an OK_OldSessionJoined response is accepted
    // (pause/resume re-join).
    std::optional<dt::SessionId> resumed_session_id{std::nullopt};

    // See session::EvSetupConfig::has_cp_state_feedback: hold the first CableCheckReq until the module
    // reports CP state C/D.
    bool has_cp_state_feedback{false};
};

} // namespace iso15118::din::ev
