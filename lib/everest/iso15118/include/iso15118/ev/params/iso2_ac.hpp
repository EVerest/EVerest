// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <cstdint>
#include <string>

#include <iso15118/message_2/common_types.hpp>

#include <iso15118/ev/d2/pnc_config.hpp>
#include <iso15118/ev/params/energy_request.hpp>

namespace iso15118::ev::params {

// ISO 15118-2 AC_EVChargeParameter values.
struct Iso2AcParams {
    float e_amount{60000.0f};
    float ev_max_voltage{400.0f};
    float ev_max_current{32.0f};
    float ev_min_current{10.0f};
};

// Parameters of the pre-20 engines (ISO 15118-2, DIN SPEC 70121). DC limits and targets come from
// the DcChargeParams monitor shared with the -20 engine; energy_request reaches it too.
struct EvSessionParams {
    // EVCCID of SessionSetupReq (the vehicle MAC).
    std::array<uint8_t, 6> evcc_mac{0x02, 0x00, 0x00, 0x00, 0x00, 0x01};

    // Requested EnergyTransferMode; its AC/DC family selects the branch. DIN clamps to DC_extended.
    message_2::datatypes::EnergyTransferMode energy_transfer_mode{
        message_2::datatypes::EnergyTransferMode::DC_extended};

    Iso2AcParams ac{};

    EnergyRequest energy_request{};

    d2::PnCConfig pnc{};

    // The TLS V2G root, which the Controller copies here: its first certificate supplies the
    // CertificateInstallationReq root ID, and it verifies the CPS signature of the response.
    std::string v2g_root_cert_path{};

    // True when the data path is TLS. The Controller writes it once the SDP response has named the
    // transport security, which is after the Session exists and before any engine reads it.
    bool secure_transport{false};
};

} // namespace iso15118::ev::params
