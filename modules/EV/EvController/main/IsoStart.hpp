// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <everest_api_types/ev_controller/API.hpp>
#include <generated/types/iso15118.hpp>

#include <cstdint>
#include <optional>

namespace module {

// What a start asks of the ISO 15118 session, in the session's own terms.
struct IsoStart {
    everest::lib::API::V1_0::types::ev_controller::ChargeMode mode{
        everest::lib::API::V1_0::types::ev_controller::ChargeMode::DcIsoD20};
    everest::lib::API::V1_0::types::ev_controller::PaymentOption payment{
        everest::lib::API::V1_0::types::ev_controller::PaymentOption::ExternalPayment};
    // Contract selected even when the SECC does not offer it.
    bool force_payment_option{false};
    std::int32_t departure_time_s{0};
    std::int32_t e_amount_wh{0};
    bool three_phases{true};
    // Applies to AcIsoD20 and DcIsoD20 only.
    std::optional<everest::lib::API::V1_0::types::ev_controller::BptParams> bpt;
    // Applies to DcIsoD20 only.
    bool mcs{false};
};

// The energy transfer mode a start runs, or std::nullopt for a mode with no ISO 15118 session.
inline std::optional<::types::iso15118::EnergyTransferMode> transfer_mode(const IsoStart& start) {
    using CM = everest::lib::API::V1_0::types::ev_controller::ChargeMode;
    using ETM = ::types::iso15118::EnergyTransferMode;
    const bool d20 = start.mode == CM::AcIsoD20 or start.mode == CM::DcIsoD20;
    const bool bpt = d20 and start.bpt.has_value();
    switch (start.mode) {
    case CM::AcIec:
        return std::nullopt;
    case CM::AcIsoD20DerIec:
        return ETM::AC_DER_IEC;
    case CM::AcIsoD20DerSae:
        return ETM::AC_DER_SAE;
    case CM::AcIso2:
        return start.three_phases ? ETM::AC_three_phase_core : ETM::AC_single_phase_core;
    case CM::AcIsoD20:
        return bpt ? ETM::AC_BPT : (start.three_phases ? ETM::AC_three_phase_core : ETM::AC_single_phase_core);
    case CM::DcIso2:
        return ETM::DC_extended;
    case CM::DcIsoD20:
        if (start.mcs) {
            return bpt ? ETM::MCS_BPT : ETM::MCS;
        }
        return bpt ? ETM::DC_BPT : ETM::DC;
    }
    return std::nullopt;
}

} // namespace module
