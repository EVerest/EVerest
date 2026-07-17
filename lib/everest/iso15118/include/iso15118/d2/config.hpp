// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <string>

#include <iso15118/message_2/common_types.hpp>

#include <everest/util/vector/fixed_vector.hpp>

namespace iso15118::d2 {

namespace dt = message_2::datatypes;

// SECC-side session configuration for ISO 15118-2. EIM (ExternalPayment) only. Holds the EVSE AC and
// DC limits (as plain floats derived from the d20 EvseSetupConfig limits), the advertised energy
// transfer modes and the EVSE id.
struct SessionConfig {
    std::string evse_id{"DE*PNX*E00000*1"};

    // Advertised in the ServiceDiscoveryRes ChargeService.
    uint16_t charge_service_id{1};
    everest::lib::util::fixed_vector<dt::EnergyTransferMode, 6> supported_energy_transfer_modes{};

    // AC parameters (AC_EVSEChargeParameter / ChargingStatusRes).
    float ac_nominal_voltage{230.0f};
    float ac_max_current{32.0f};

    // DC parameters (DC_EVSEChargeParameter / CurrentDemandRes limits).
    float dc_max_current{300.0f};
    float dc_max_power{150000.0f};
    float dc_max_voltage{900.0f};
    float dc_min_current{0.0f};
    float dc_min_voltage{0.0f};
    float dc_peak_current_ripple{0.0f};

    // PMax advertised in the SAScheduleList entry (seconds); one day by default.
    uint32_t sa_schedule_duration{86400};

    // Plug-and-Charge (PnC / Contract payment). When enabled, ServiceDiscovery advertises the Contract
    // payment option and the SECC accepts a Contract PaymentServiceSelection, runs PaymentDetails
    // (contract chain validation to the MO/V2G root) and verifies the AuthorizationReq signature.
    bool pnc_enabled{false};
    std::string mo_root_cert_path{};
    std::string v2g_root_cert_path{};

    // Request a (signed) MeteringReceipt from the EV: set ReceiptRequired in the CurrentDemandRes (DC) /
    // ChargingStatusRes (AC) charge loop. Effective only for PnC sessions ([V2G2-691]).
    bool receipt_required{false};
};

// Retained across a paused session so a returning EV can re-join with OK_OldSessionJoined. For EIM the
// stored session id is the sole match criterion (mirrors EvseV2G hlc_pause_active id retention).
struct PauseContext {
    dt::SessionId old_session_id{};
};

} // namespace iso15118::d2
