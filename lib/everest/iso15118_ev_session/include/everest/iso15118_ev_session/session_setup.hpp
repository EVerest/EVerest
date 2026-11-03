// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <generated/types/iso15118.hpp>

#include <iso15118/ev/config/config.hpp>
#include <iso15118/ev/controller.hpp>
#include <iso15118/ev/d2/pnc_config.hpp>
#include <iso15118/ev/d20/der/iec_functions.hpp>
#include <iso15118/ev/d20/der/sae_profile.hpp>
#include <iso15118/ev/params/ac.hpp>
#include <iso15118/ev/params/dc.hpp>
#include <iso15118/ev/params/energy_request.hpp>
#include <iso15118/message/common_types.hpp>
#include <iso15118/message/shared_datatypes.hpp>
#include <iso15118/session/protocol.hpp>

// How an EVerest EV module builds a libiso15118 ev::Controller session from its configuration and a
// start request. Ev15118 and EvController both call this, so a session is set up one way.
namespace everest::lib::iso15118_ev {

// The vehicle configuration, as configured. The EV modules' manifest keys of the same names; an empty
// certificate or key path resolves under pki_path, or under etc/certs when pki_path is empty.
struct VehicleConfig {
    std::string evcc_id;
    int response_timeout_ms{0};
    int authorization_timeout_ms{0};
    // Scheduled, otherwise Dynamic.
    std::string control_mode;
    // off, prefer or require.
    std::string tls;
    bool allow_pnc_without_tls{false};
    // auto, 1.2 or 1.3.
    std::string tls_version;
    bool verify_server_certificate{false};
    bool enable_tls_key_logging{false};
    std::string tls_key_logging_path;
    std::string pki_path;
    std::string v2g_root_cert_path;
    std::string device_cert_chain_path;
    std::string device_key_path;
    std::string device_key_password_path;
    double iso2_ac_e_amount_wh{0};
    double iso2_ac_ev_max_voltage_v{0};
    double iso2_ac_ev_max_current_a{0};
    double iso2_ac_ev_min_current_a{0};
    bool enable_pnc{false};
    std::string pnc_contract_cert_chain_path;
    std::string pnc_contract_key_path;
    std::string pnc_contract_key_password_path;
    std::string pnc_oem_prov_cert_path;
    std::string pnc_oem_prov_key_path;
    std::string pnc_oem_prov_key_password_path;
    bool pnc_force_cert_install{false};
    std::string pnc_contract_store_dir;
    int ac_phase_count{3};
    double ac_max_charge_power_w{0};
    double ac_min_charge_power_w{0};
    iso15118::ev::d20::der::DerControlFunctions der_functions;
    bool der_stop_on_unsupported_functions{false};
    double ac_max_discharge_power_w{0};
    double ac_min_discharge_power_w{0};
    double dc_max_discharge_power_w{0};
    double dc_min_discharge_power_w{0};
    double dc_max_discharge_current_a{0};
    // In [1, 65535].
    int cpd_rounds{1};
    bool der_stop_on_invalid_control{false};
    // The module's etc directory; its certs/ holds the default PKI.
    std::filesystem::path etc;
};

// What the configuration resolves to once, before the first session.
struct Vehicle {
    // The HLC interface, "auto" resolved.
    std::string device;
    // The ISO 15118-2 / DIN EVCCID. Empty keeps the library default.
    std::optional<std::array<std::uint8_t, 6>> evcc_mac;
    // SAP priority order.
    std::vector<iso15118::ProtocolId> protocols;
    iso15118::ev::d20::der::SaeInverterProfile sae_profile;
    // Read when enable_pnc is set.
    iso15118::ev::d2::PnCConfig pnc_material;
};

// supported_protocols in SAP priority order. std::nullopt with error set when the list is empty, names
// an unknown protocol, or names one twice.
std::optional<std::vector<iso15118::ProtocolId>> parse_supported_protocols(const std::string& list, std::string& error);

// A comma-separated list of DerIecFunction names. std::nullopt with error set on an unknown or repeated
// name; an empty list declares none.
std::optional<iso15118::ev::d20::der::DerControlFunctions> parse_der_iec_functions(const std::string& list,
                                                                                   std::string& error);

// The parsed and validated profile at `path`, the default one for an empty path. std::nullopt with error
// set otherwise.
std::optional<iso15118::ev::d20::der::SaeInverterProfile> load_sae_profile(const std::string& path, std::string& error);

// 6 byte MAC of `device` from sysfs, or nullopt on any failure.
std::optional<std::array<std::uint8_t, 6>> read_interface_mac(const std::string& device);

iso15118::ev::config::TlsMode tls_mode(const VehicleConfig& config);
iso15118::ev::config::TlsVersion tls_version(const VehicleConfig& config);

// PnC requires TLS [V2G2-632] unless allow_pnc_without_tls deviates deliberately.
bool pnc_offered(const VehicleConfig& config);

// Warns once per deviation a pinned tls_version forces on an enabled protocol [V2G2-067].
void warn_tls_deviations(const VehicleConfig& config, const std::vector<iso15118::ProtocolId>& protocols,
                         const std::string& prefix);

// The configured Plug&Charge material, with the committed PKI layout as the default.
iso15118::ev::d2::PnCConfig load_pnc_material(const VehicleConfig& config, const std::string& prefix);

// `configured` with the contract in pnc_contract_store_dir presented instead of the configured one when
// that is complete and valid. Reads files.
iso15118::ev::d2::PnCConfig session_pnc_material(const VehicleConfig& config,
                                                 const iso15118::ev::d2::PnCConfig& configured,
                                                 const std::string& prefix);

// Empty when no password file is configured or found: an unencrypted vehicle key.
std::string vehicle_key_password(const VehicleConfig& config);

// The ISO 15118-20 service of a start mode, or nullopt for a mode no session runs.
std::optional<iso15118::message_20::datatypes::ServiceCategory>
energy_service(types::iso15118::EnergyTransferMode mode);

// The ISO 15118-2 / DIN granularity of a start mode. Neither generation has a BPT, DER or MCS mode, so
// those collapse to the matching unidirectional mode, with a warning when a pre-20 protocol is offered.
iso15118::shared_datatypes::EnergyTransferMode to_iso2_transfer_mode(types::iso15118::EnergyTransferMode mode,
                                                                     bool pre_20_offered, const std::string& prefix);

// A start request's per-session choices.
struct SessionRequest {
    iso15118::message_20::datatypes::ServiceCategory energy_service{
        iso15118::message_20::datatypes::ServiceCategory::DC};
    iso15118::shared_datatypes::EnergyTransferMode iso2_transfer_mode{
        iso15118::shared_datatypes::EnergyTransferMode::DC_extended};
    // ExternalPayment asked for explicitly, which overrides the enable_pnc Contract preference.
    bool eim_requested{false};
    // Contract selected even when the SECC does not offer it.
    bool enforce_contract{false};
    iso15118::ev::params::EnergyRequest energy_request;
    // The session to rejoin, left by one that ended with SessionStop(Pause).
    std::optional<iso15118::ev::config::PausedSession> resume;
    // The control pilot is reported at all.
    bool has_cp_state_feedback{false};
};

// Per-session files, read before the session is built.
struct SessionFiles {
    iso15118::ev::d2::PnCConfig pnc;
    std::string key_password;
};

// The request of a start: `mode` offered with `protocols`, DepartureTime in s from now and EAmount in
// Wh, both finite and not negative (0 states nothing). std::nullopt for a mode no session runs.
// resume and has_cp_state_feedback are left to the caller.
std::optional<SessionRequest> make_session_request(types::iso15118::EnergyTransferMode mode,
                                                   const std::vector<iso15118::ProtocolId>& protocols,
                                                   bool eim_requested, bool enforce_contract, double departure_time_s,
                                                   double e_amount_wh, const std::string& prefix);

// The files of a session. A resume presents `resume_contract`, the contract its paused session
// installed; any other session presents session_pnc_material(config, material). Reads files.
SessionFiles make_session_files(const VehicleConfig& config, const iso15118::ev::d2::PnCConfig& material,
                                const std::optional<iso15118::ev::d2::ContractMaterial>& resume_contract,
                                const std::string& prefix);

// The session's EvConfig. Warns about PnC without TLS once per `pnc_without_tls_warned`.
iso15118::ev::config::EvConfig make_ev_config(const VehicleConfig& config, const Vehicle& vehicle,
                                              const SessionRequest& request, const SessionFiles& files,
                                              bool& pnc_without_tls_warned, const std::string& prefix);

// Seeds the AC parameters of an AC family service and the discharge limits of a BPT or DER service from
// the configuration. A command's discharge limits win over the configured DC ones. Measurements from an
// earlier session are cleared.
void seed_session_params(const VehicleConfig& config, types::iso15118::EnergyTransferMode mode,
                         iso15118::message_20::datatypes::ServiceCategory energy_service,
                         std::optional<float> cmd_max_discharge_power, std::optional<float> cmd_max_discharge_current,
                         iso15118::ev::params::AcChargeParams& ac, iso15118::ev::params::DcChargeParams& dc,
                         const std::string& prefix);

// What a session built from these would put on the wire and the library would reject.
std::vector<std::string> validate_session(const iso15118::ev::config::EvConfig& ev_config,
                                          const iso15118::ev::params::AcChargeParams& ac,
                                          const iso15118::ev::params::DcChargeParams& dc);

// `current` with the fields `update` carries. Problems are set and the result is nullopt when the merged
// set does not validate.
std::optional<iso15118::ev::params::DcChargeParams> merge_dc_params(const iso15118::ev::params::DcChargeParams& current,
                                                                    const types::iso15118::DcEvParameters& update,
                                                                    std::vector<std::string>& problems);

// Stores the fields `values` carries in the parameters that seed the next session and forwards them
// to `live` when a session runs. An absent field keeps its last value: 0 would read as a measurement.
void apply_present_values(const types::iso15118::EvPresentValues& values, iso15118::ev::params::AcChargeParams& ac,
                          iso15118::ev::params::DcChargeParams& dc, iso15118::ev::Controller* live,
                          const std::string& prefix);

// Routes libiso15118's log lines to EVLOG.
void install_logging();

} // namespace everest::lib::iso15118_ev
