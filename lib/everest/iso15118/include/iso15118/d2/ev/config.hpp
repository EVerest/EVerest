// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <iso15118/message_2/certificate_installation.hpp>
#include <iso15118/message_2/common_types.hpp>

namespace iso15118::d2::ev {

namespace dt = message_2::datatypes;

// EVCC Plug-and-Charge (Contract) configuration. When `prefer_contract` is set and the SECC offers the
// Contract payment option, the EV runs the PnC flow (optionally CertificateInstallation, then a signed
// PaymentDetails/Authorization exchange). The certificate material is sourced by the module (from
// EvseSecurity) and copied in here per session.
struct PnCConfig {
    // Prefer Contract (PnC) over EIM when the SECC offers it.
    bool prefer_contract{false};

    // Select Contract in PaymentServiceSelection even when the SECC does not offer it (deliberately
    // non-conformant, [V2G2-135] robustness testing: the SECC must reject with FAILED and terminate).
    bool enforce_contract{false};

    // OEM provisioning certificate (DER) + its EC private key (PEM). Signs CertificateInstallationReq and
    // is the static ECDH key that decrypts the delivered contract private key.
    std::vector<uint8_t> oem_prov_cert_der;
    std::string oem_prov_key_pem;
    std::optional<std::string> oem_prov_key_password;

    // Root certificates installed in the EVCC: declared in CertificateInstallationReq and used to verify
    // the CertificateInstallationRes CPS signature (`v2g_root_path` is the trusted-root bundle file).
    std::vector<message_2::RootCertificateId> root_certificate_ids;
    std::string v2g_root_path;

    // Pre-installed contract certificate. When present (and `force_cert_install` is false) the EV skips
    // CertificateInstallation and presents this chain directly in PaymentDetails.
    std::vector<uint8_t> contract_cert_der;
    std::vector<std::vector<uint8_t>> contract_sub_certs_der;
    std::string contract_key_pem;
    std::optional<std::string> contract_key_password;
    std::string contract_emaid;

    // Force a CertificateInstallation even when a contract certificate is already present.
    bool force_cert_install{false};

    bool has_contract_cert() const {
        return not contract_cert_der.empty();
    }
    // The EV needs to obtain a contract certificate (run CertificateInstallation) when none is installed
    // or a fresh one is forced.
    bool needs_cert_install() const {
        return force_cert_install or not has_contract_cert();
    }
};

// Session-scoped EVCC configuration for ISO 15118-2 (EIM plus optional Plug-and-Charge, see `pnc`).
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

    // Plug-and-Charge configuration (disabled by default -> EIM).
    PnCConfig pnc{};

    // See session::EvSetupConfig::has_cp_state_feedback: hold the first CableCheckReq until the module
    // reports CP state C/D.
    bool has_cp_state_feedback{false};
};

} // namespace iso15118::d2::ev
