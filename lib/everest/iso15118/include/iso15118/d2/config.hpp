// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <iso15118/d20/control_event.hpp>
#include <iso15118/message_2/common_types.hpp>
#include <iso15118/message_2/service_discovery.hpp>

#include <everest/util/vector/fixed_vector.hpp>

namespace iso15118::d2 {

namespace dt = message_2::datatypes;

struct SessionConfig {
    std::string evse_id{"DE*EVR*E00000*1"};

    uint16_t charge_service_id{1};
    everest::lib::util::fixed_vector<dt::EnergyTransferMode, 6> supported_energy_transfer_modes{};

    // Advertised in the ServiceDiscoveryRes ServiceList (Table 105) after the built-in Certificate
    // service. Filtered by make_d2_config: never ServiceID 1 (charging) or 2 (Certificate).
    dt::ServiceList offered_vas_services{};

    // Two flavours (EvseV2G parity): ChargeParameterDiscoveryRes advertises the maximum the EVSE could
    // ever deliver, while the charge loop reports what is available right now. Every limit defaults to 0
    // and only reported data may raise it -- an invented value must never be advertised to the EV.

    // AC parameters. ac_capability_max_current feeds the AC_EVSEChargeParameter EVSEMaxCurrent and the
    // SAScheduleList PMax; ac_max_current is the live per-phase limit reported in ChargingStatusRes.
    float ac_nominal_voltage{230.0f};
    float ac_max_current{0.0f};
    float ac_capability_max_current{0.0f};

    // The minimums appear only in ChargeParameterDiscoveryRes, so they are capabilities as well.
    float dc_capability_max_current{0.0f};
    float dc_capability_max_power{0.0f};
    float dc_capability_max_voltage{0.0f};
    float dc_min_current{0.0f};
    float dc_min_voltage{0.0f};

    float dc_max_current{0.0f};
    float dc_max_power{0.0f};
    float dc_max_voltage{0.0f};

    float dc_peak_current_ripple{0.0f};
    // Only sent when the module reported them via set_charging_parameters.
    std::optional<float> dc_current_regulation_tolerance{std::nullopt};
    std::optional<float> dc_energy_to_be_delivered{std::nullopt};

    // PMax advertised in the SAScheduleList entry (seconds); one day by default.
    uint32_t sa_schedule_duration{86400};

    // Enables the Contract payment option, PaymentDetails chain validation and signature verification.
    bool pnc_enabled{false};
    // The SECC offers exactly the configured options (EvseV2G/Josev parity), so a Contract-only SECC is
    // possible -- ISO 15118-2 does not require EIM to always be offered. When the gating leaves no option
    // at all, the states fall back to ExternalPayment like EvseV2G.
    bool eim_enabled{true};
    std::string mo_root_cert_path{};
    std::string v2g_root_cert_path{};
    // Forwarded to the CSMS instead (OCPP CentralContractValidationAllowed, EvseV2G parity).
    bool central_contract_validation_allowed{false};

    // Advertised only when this is set AND the session is PnC-over-TLS. Independent of pnc_enabled, so a
    // Contract-auth-only SECC does not advertise a certificate service it does not provide.
    bool cert_install_service{false};

    // Contract is offered and accepted only when pnc_enabled && tls_active [V2G2-632]/[V2G2-634].
    bool tls_active{false};

    // Effective only for PnC sessions ([V2G2-691]).
    bool receipt_required{false};

    // IEC 61851-23:2023 CC.3.5.3. BeforeCableCheck additionally skips the cable check, and both stopping
    // modes reject a PowerDelivery(Start).
    d20::NoEnergyPauseMode no_energy_pause{d20::NoEnergyPauseMode::None};

    // In MILLISECONDS; 0 waits indefinitely. Selected per session by the payment option the EV chose;
    // see session::EvseSetupConfig for why EIM defaults well beyond the 55 s of [V2G2-712/713].
    uint32_t auth_timeout_eim_ms{300000};
    uint32_t auth_timeout_pnc_ms{55000};
};

// Retained across a paused session so a returning EV can re-join with OK_OldSessionJoined; for EIM
// the stored id is the sole match criterion. The payment option is retained too, because [V2G2-741]
// requires the resumed ServiceDiscoveryRes to offer only that one.
struct PauseContext {
    dt::SessionId old_session_id{};
    dt::PaymentOption selected_payment_option{dt::PaymentOption::ExternalPayment};
};

} // namespace iso15118::d2
