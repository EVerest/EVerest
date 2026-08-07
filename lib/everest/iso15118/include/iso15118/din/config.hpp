// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include <iso15118/d20/control_event.hpp>
#include <iso15118/message_din/common_types.hpp>

namespace iso15118::din {

namespace dt = message_din::datatypes;

// SECC-side, session-scoped configuration for a DIN SPEC 70121 charging session. Holds the static EVSE
// information the response messages need (EVSEID, DC limits, offered charge service). Runtime quantities
// (present voltage/current, isolation status, processing flags) live in the din::Context instead, as they
// are driven by control events during the session.
struct SessionConfig {
    // EVSEID as hexBinary (DIN uses a byte string, MaxLength 32).
    std::vector<uint8_t> evse_id;

    // Offered ChargeService.
    uint16_t charge_service_id{1};
    bool free_service{true};
    dt::SupportedEnergyTransferMode energy_transfer_mode{dt::SupportedEnergyTransferMode::DC_extended};

    // DC EVSE limits, in two flavours (EvseV2G parity): ChargeParameterDiscoveryRes advertises the
    // maximum the EVSE could ever deliver (hardware capabilities, the evse_capability_* values, which
    // also set the SAScheduleList PMax), while CurrentDemandRes reports what energy management currently
    // grants (the evse_maximum_* values, following the live limit updates). The minimums appear only in
    // the ChargeParameterDiscoveryRes, so they are capabilities as well.
    double evse_capability_maximum_current_limit{0.0};
    std::optional<double> evse_capability_maximum_power_limit{std::nullopt};
    double evse_capability_maximum_voltage_limit{0.0};
    double evse_minimum_current_limit{0.0};
    double evse_minimum_voltage_limit{0.0};
    double evse_maximum_current_limit{0.0};
    std::optional<double> evse_maximum_power_limit{std::nullopt};
    double evse_maximum_voltage_limit{0.0};
    double evse_peak_current_ripple{0.0};
    // Optional DC_EVSEChargeParameter elements; only sent when the module reported them via
    // set_charging_parameters.
    std::optional<double> evse_current_regulation_tolerance{std::nullopt};
    std::optional<double> evse_energy_to_be_delivered{std::nullopt};

    // No-energy pause requested by the charger (IEC 61851-23:2023 CC.3.5.3). When set, the
    // ChargeParameterDiscoveryRes signals EVSENotification StopCharging and, unless the EV is allowed to
    // ignore it, the SECC stops before the cable check.
    d20::NoEnergyPauseMode no_energy_pause{d20::NoEnergyPauseMode::None};

    // How long ContractAuthentication keeps answering EVSEProcessing=Ongoing before failing the session,
    // in MILLISECONDS; 0 means wait indefinitely. Only the EIM value applies -- DIN SPEC 70121 knows no
    // other payment option. Threaded from the module's auth_timeout_eim (seconds) by make_din_config.
    // EvseV2G never bounded this loop at all (din_server.cpp has no auth timer), so a DIN session there
    // could sit in Ongoing until the EV gave up.
    uint32_t auth_timeout_eim_ms{300000};
};

} // namespace iso15118::din
