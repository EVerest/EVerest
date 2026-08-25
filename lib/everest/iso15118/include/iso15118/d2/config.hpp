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

// SECC-side session configuration for ISO 15118-2. EIM (ExternalPayment) only. Holds the EVSE AC and
// DC limits (as plain floats derived from the d20 EvseSetupConfig limits), the advertised energy
// transfer modes and the EVSE id.
struct SessionConfig {
    std::string evse_id{"DE*EVR*E00000*1"};

    // Advertised in the ServiceDiscoveryRes ChargeService.
    uint16_t charge_service_id{1};
    everest::lib::util::fixed_vector<dt::EnergyTransferMode, 6> supported_energy_transfer_modes{};

    // Value-added services of external providers, advertised in the ServiceDiscoveryRes ServiceList
    // (Table 105) after the built-in Certificate service. Their parameter sets (ServiceDetailRes) and the
    // EV's selection (PaymentServiceSelectionReq) go through feedback.get_vas_parameters /
    // selected_vas_services. Filtered by make_d2_config: never ServiceID 1 (charging) or 2 (Certificate).
    dt::ServiceList offered_vas_services{};

    // The limits come in two flavours (EvseV2G parity): ChargeParameterDiscoveryRes advertises the
    // maximum the EVSE could ever deliver (hardware capabilities, independent of what energy management
    // currently grants), while the charge loop (CurrentDemandRes / ChargingStatusRes) reports what is
    // available right now and follows the live energy-management limit updates. Safety: every limit
    // defaults to 0 and only actually reported data may raise it -- an invented value must never be
    // advertised to the EV.

    // AC parameters. ac_capability_max_current feeds the AC_EVSEChargeParameter EVSEMaxCurrent and the
    // SAScheduleList PMax; ac_max_current is the live per-phase limit reported in ChargingStatusRes.
    float ac_nominal_voltage{230.0f};
    float ac_max_current{0.0f};
    float ac_capability_max_current{0.0f};

    // DC hardware capabilities (DC_EVSEChargeParameter / SAScheduleList PMax). The minimums appear only
    // in the ChargeParameterDiscoveryRes, so they are capabilities as well.
    float dc_capability_max_current{0.0f};
    float dc_capability_max_power{0.0f};
    float dc_capability_max_voltage{0.0f};
    float dc_min_current{0.0f};
    float dc_min_voltage{0.0f};

    // DC limits currently available (CurrentDemandRes EVSEMaximum*Limit and the limit-achieved flags).
    float dc_max_current{0.0f};
    float dc_max_power{0.0f};
    float dc_max_voltage{0.0f};

    float dc_peak_current_ripple{0.0f};
    // Optional DC_EVSEChargeParameter elements; only sent when the module reported them via
    // set_charging_parameters.
    std::optional<float> dc_current_regulation_tolerance{std::nullopt};
    std::optional<float> dc_energy_to_be_delivered{std::nullopt};

    // PMax advertised in the SAScheduleList entry (seconds); one day by default.
    uint32_t sa_schedule_duration{86400};

    // Plug-and-Charge (PnC / Contract payment). When enabled, ServiceDiscovery advertises the Contract
    // payment option and the SECC accepts a Contract PaymentServiceSelection, runs PaymentDetails
    // (contract chain validation to the MO/V2G root) and verifies the AuthorizationReq signature.
    bool pnc_enabled{false};
    // Offer ExternalPayment (EIM). Threaded from the module's session_setup payment_options: the SECC
    // offers exactly the configured options (EvseV2G/Josev parity), so a Contract-only (PnC-only) SECC
    // is possible -- ISO 15118-2 does not require EIM to always be offered (the -4 ATS models
    // single-option SECCs via PIXIT_SECC_CMN_PaymentOption). When the gating leaves no option at all
    // (Contract-only configuration on a plain-TCP session, or an empty configuration), the states fall
    // back to ExternalPayment like EvseV2G.
    bool eim_enabled{true};
    std::string mo_root_cert_path{};
    std::string v2g_root_cert_path{};
    // Accept a contract whose chain cannot be validated locally (missing MO root) and forward it to
    // the CSMS for central validation (OCPP CentralContractValidationAllowed, EvseV2G parity).
    bool central_contract_validation_allowed{false};

    // ISO 15118-2 certificate installation/update service (VAS, ServiceID 2, Table 105). When enabled AND
    // the session is PnC-over-TLS, ServiceDiscovery advertises the Certificate service and ServiceDetail
    // returns its Installation/Update parameter sets (Table 106). Threaded from
    // EvseSetupConfig::enable_certificate_install_service. Independent of pnc_enabled so a Contract-auth-only
    // SECC does not advertise a certificate service it does not provide.
    bool cert_install_service{false};

    // True when the underlying connection is TLS. ISO 15118-2 permits Plug-and-Charge (Contract payment)
    // only over TLS [V2G2-632]/[V2G2-634]; Contract is offered/accepted only when pnc_enabled && tls_active.
    bool tls_active{false};

    // Request a (signed) MeteringReceipt from the EV: set ReceiptRequired in the CurrentDemandRes (DC) /
    // ChargingStatusRes (AC) charge loop. Effective only for PnC sessions ([V2G2-691]).
    bool receipt_required{false};

    // No-energy pause requested by the charger (IEC 61851-23:2023 CC.3.5.3). When set, the
    // ChargeParameterDiscoveryRes signals EVSENotification StopCharging; BeforeCableCheck additionally
    // skips the cable check, and both stopping modes reject a PowerDelivery(Start).
    d20::NoEnergyPauseMode no_energy_pause{d20::NoEnergyPauseMode::None};

    // How long the Authorization state keeps answering EVSEProcessing=Ongoing before failing the session,
    // in MILLISECONDS; 0 means wait indefinitely. Selected per session by the payment option the EV
    // chose. Threaded from the module's auth_timeout_eim / auth_timeout_pnc (seconds) by make_d2_config;
    // see session::EvseSetupConfig for why EIM defaults well beyond the 55 s of [V2G2-712/713].
    uint32_t auth_timeout_eim_ms{300000};
    uint32_t auth_timeout_pnc_ms{55000};
};

// Retained across a paused session so a returning EV can re-join with OK_OldSessionJoined. For EIM the
// stored session id is the sole match criterion (mirrors EvseV2G hlc_pause_active id retention).
// The payment option selected in the paused session is retained as well: [V2G2-741] requires the
// resumed session's ServiceDiscoveryRes to offer only that option.
struct PauseContext {
    dt::SessionId old_session_id{};
    dt::PaymentOption selected_payment_option{dt::PaymentOption::ExternalPayment};
};

} // namespace iso15118::d2
