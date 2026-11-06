// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <everest/iso15118_ev_session/session_setup.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <string_view>
#include <utility>

#include <everest/logging.hpp>

#include <iso15118/ev/config/validation.hpp>
#include <iso15118/ev/d2/pnc_material.hpp>
#include <iso15118/ev/d20/service_family.hpp>
#include <iso15118/io/logging.hpp>

#include <everest/iso15118_ev_session/contract_store.hpp>
#include <everest/iso15118_ev_session/der_publish_names.hpp>
#include <everest/iso15118_ev_session/sae_profile_config.hpp>

namespace everest::lib::iso15118_ev {

namespace {

namespace fs = std::filesystem;
namespace dt = iso15118::message_20::datatypes;

std::string resolve_path(const std::string& configured, const fs::path& fallback) {
    return configured.empty() ? fallback.string() : configured;
}

// The directory the certificate and key paths left empty resolve under: pki_path, else etc/certs.
fs::path pki_dir(const VehicleConfig& config) {
    return config.pki_path.empty() ? config.etc / "certs" : fs::path(config.pki_path);
}

// Whole file, or empty when it cannot be opened.
std::string read_file_text(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (not file.is_open()) {
        return {};
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// Empty for a missing or blank file, which means an unencrypted private key.
std::string read_file_trimmed(const fs::path& path) {
    auto content = read_file_text(path);
    const auto not_space = [](unsigned char c) { return std::isspace(c) == 0; };
    content.erase(content.begin(), std::find_if(content.begin(), content.end(), not_space));
    content.erase(std::find_if(content.rbegin(), content.rend(), not_space).base(), content.end());
    return content;
}

std::string trim(std::string_view text) {
    const auto first = text.find_first_not_of(' ');
    return (first == std::string_view::npos) ? std::string{}
                                             : std::string(text.substr(first, text.find_last_not_of(' ') + 1 - first));
}

// The entries of a comma-separated list, trimmed. A blank list has none; any other list has one more
// entry than it has commas.
std::vector<std::string> split_list(const std::string& list) {
    std::vector<std::string> entries;
    const std::string_view text(list);
    for (std::size_t begin = trim(text).empty() ? text.size() + 1 : 0; begin <= text.size();) {
        const auto end = std::min(text.find(',', begin), text.size());
        entries.push_back(trim(text.substr(begin, end - begin)));
        begin = end + 1;
    }
    return entries;
}

} // namespace

std::optional<std::vector<iso15118::ProtocolId>> parse_supported_protocols(const std::string& list,
                                                                           std::string& error) {
    using iso15118::ProtocolId;
    constexpr std::array<std::pair<std::string_view, ProtocolId>, 3> names{{
        {"ISO15118_20", ProtocolId::ISO15118_20},
        {"ISO15118_2", ProtocolId::ISO15118_2},
        {"DIN70121", ProtocolId::DIN70121},
    }};
    std::vector<ProtocolId> protocols;
    for (const auto& entry : split_list(list)) {
        const auto name = std::find_if(names.begin(), names.end(), [&](const auto& n) { return n.first == entry; });
        if (name == names.end()) {
            error =
                "supported_protocols names unknown protocol '" + entry + "'; use ISO15118_20, ISO15118_2 or DIN70121";
            return std::nullopt;
        }
        if (std::find(protocols.begin(), protocols.end(), name->second) != protocols.end()) {
            error = "supported_protocols names " + entry + " more than once";
            return std::nullopt;
        }
        protocols.push_back(name->second);
    }
    if (protocols.empty()) {
        error = "no protocol enabled; list ISO15118_20, ISO15118_2 or DIN70121 in supported_protocols";
        return std::nullopt;
    }
    return protocols;
}

std::optional<iso15118::ev::d20::der::DerControlFunctions> parse_der_iec_functions(const std::string& list,
                                                                                   std::string& error) {
    std::uint32_t bitmap = 0;
    for (const auto& entry : split_list(list)) {
        std::optional<std::uint32_t> bit;
        for (std::uint32_t candidate = 0; candidate < iso15118::ev::d20::der::DER_CONTROL_FUNCTION_COUNT; ++candidate) {
            const auto named = iec_der_functions(1U << candidate);
            if (named.size() == 1 and types::iso15118::der_iec_function_to_string(named.front()) == entry) {
                bit = candidate;
            }
        }
        if (not bit) {
            error = "unknown DER IEC function '" + entry + "'";
            return std::nullopt;
        }
        if ((bitmap & (1U << *bit)) != 0) {
            error = "DER IEC function " + entry + " named more than once";
            return std::nullopt;
        }
        bitmap |= 1U << *bit;
    }
    using iso15118::iec::DERControlName;
    const auto has = [bitmap](DERControlName name) { return (bitmap & (1U << static_cast<std::uint32_t>(name))) != 0; };
    iso15118::ev::d20::der::DerControlFunctions functions;
    functions.over_frequency_watt_mode = has(DERControlName::OverFrequencyWattMode);
    functions.under_frequency_watt_mode = has(DERControlName::UnderFrequencyWattMode);
    functions.volt_watt_mode = has(DERControlName::VoltWattMode);
    functions.volt_var_mode = has(DERControlName::VoltVarMode);
    functions.watt_var_mode = has(DERControlName::WattVarMode);
    functions.watt_cos_phi_mode = has(DERControlName::WattCosPhiMode);
    functions.dso_q_setpoint_provision = has(DERControlName::DSOQSetpointProvision);
    functions.dso_cos_phi_setpoint_provision = has(DERControlName::DSOCosPhiSetpointProvision);
    functions.dc_injection_restriction = has(DERControlName::DCInjectionRestriction);
    functions.zero_current_mode = has(DERControlName::ZeroCurrentMode);
    functions.over_voltage_fault_ride_through_mode = has(DERControlName::OverVoltageFaultRideThroughMode);
    functions.under_voltage_fault_ride_through_mode = has(DERControlName::UnderVoltageFaultRideThroughMode);
    return functions;
}

std::optional<iso15118::ev::d20::der::SaeInverterProfile> load_sae_profile(const std::string& path,
                                                                           std::string& error) {
    auto profile = parse_sae_inverter_profile(path, error);
    if (not profile) {
        return std::nullopt;
    }
    if (const auto problems = iso15118::ev::config::validate_sae_profile(*profile); not problems.empty()) {
        error.clear();
        for (const auto& problem : problems) {
            error.append(error.empty() ? "" : "; ").append(problem);
        }
        return std::nullopt;
    }
    return profile;
}

std::optional<std::array<std::uint8_t, 6>> read_interface_mac(const std::string& device) {
    if (device.empty()) {
        return std::nullopt;
    }
    std::ifstream file("/sys/class/net/" + device + "/address");
    if (not file.is_open()) {
        return std::nullopt;
    }
    std::string line;
    std::getline(file, line);

    // %2hhx: each field is exactly one byte, so an out-of-range value cannot fold into the array.
    std::array<std::uint8_t, 6> mac{};
    if (std::sscanf(line.c_str(), "%2hhx:%2hhx:%2hhx:%2hhx:%2hhx:%2hhx", &mac[0], &mac[1], &mac[2], &mac[3], &mac[4],
                    &mac[5]) != 6) {
        return std::nullopt;
    }
    return mac;
}

iso15118::ev::config::TlsMode tls_mode(const VehicleConfig& config) {
    using iso15118::ev::config::TlsMode;
    return (config.tls == "require") ? TlsMode::Require : (config.tls == "prefer") ? TlsMode::Prefer : TlsMode::Off;
}

iso15118::ev::config::TlsVersion tls_version(const VehicleConfig& config) {
    using iso15118::ev::config::TlsVersion;
    return (config.tls_version == "1.3")   ? TlsVersion::Tls1_3
           : (config.tls_version == "1.2") ? TlsVersion::Tls1_2
                                           : TlsVersion::Auto;
}

bool pnc_offered(const VehicleConfig& config) {
    return config.enable_pnc and
           (tls_mode(config) != iso15118::ev::config::TlsMode::Off or config.allow_pnc_without_tls);
}

void warn_tls_deviations(const VehicleConfig& config, const std::vector<iso15118::ProtocolId>& protocols,
                         const std::string& prefix) {
    iso15118::ev::config::EvConfig tls_policy;
    tls_policy.supported_protocols = protocols;
    tls_policy.tls.mode = tls_mode(config);
    tls_policy.tls.version = tls_version(config);
    for (const auto& deviation : iso15118::ev::config::tls_deviations(tls_policy)) {
        EVLOG_warning << prefix << ": " << deviation;
    }
}

iso15118::ev::d2::PnCConfig load_pnc_material(const VehicleConfig& config, const std::string& prefix) {
    namespace material = iso15118::ev::d2::pnc_material;
    const auto certs = pki_dir(config);
    const auto resolve = [](const std::string& configured, const fs::path& fallback) {
        return configured.empty() ? fallback : fs::path(configured);
    };

    iso15118::ev::d2::PnCConfig pnc;
    pnc.force_cert_install = config.pnc_force_cert_install;

    // Pre-installed contract (MO) chain, key and eMAID, presented in PaymentDetails. The repo PKI ships
    // no single-file MO chain, so the default is the leaf plus the intermediates (MO_SUB_CA2, MO_SUB_CA1,
    // in that order); a configured path is one PEM with the whole chain.
    const auto contract_chain_pem = [&] {
        if (not config.pnc_contract_cert_chain_path.empty()) {
            return read_file_text(config.pnc_contract_cert_chain_path);
        }
        auto leaf = read_file_text(certs / "client/mo/MO_LEAF.pem");
        if (not leaf.empty() and leaf.back() != '\n') {
            leaf.push_back('\n');
        }
        return leaf + read_file_text(certs / "ca/mo/INTERMEDIATE_MO_CA_CERTS.pem");
    }();
    const auto chain_der = material::pem_chain_to_der(contract_chain_pem);
    if (not chain_der.empty()) {
        auto& contract = pnc.contract.emplace();
        contract.cert_der = chain_der.front();
        contract.sub_certs_der.assign(chain_der.begin() + 1, chain_der.end());
        contract.emaid = material::emaid_from_contract_der(contract.cert_der);
        contract.key_pem = read_file_text(resolve(config.pnc_contract_key_path, certs / "client/mo/MO_LEAF.key"));
        auto password =
            read_file_trimmed(resolve(config.pnc_contract_key_password_path, certs / "client/mo/MO_LEAF_PASSWORD.txt"));
        if (not password.empty()) {
            contract.key_password = std::move(password);
        }
    } else {
        EVLOG_warning << prefix << ": no Plug&Charge contract certificate chain found in "
                      << (config.pnc_contract_cert_chain_path.empty()
                              ? (certs / "client/mo/MO_LEAF.pem").string() + " (the default)"
                              : config.pnc_contract_cert_chain_path);
    }

    // OEM provisioning certificate and key, only used when a CertificateInstallation runs.
    const auto oem_der = material::pem_chain_to_der(
        read_file_text(resolve(config.pnc_oem_prov_cert_path, certs / "client/oem/OEM_LEAF.pem")));
    if (not oem_der.empty()) {
        pnc.oem_prov_cert_der = oem_der.front();
        pnc.oem_prov_key_pem = read_file_text(resolve(config.pnc_oem_prov_key_path, certs / "client/oem/OEM_LEAF.key"));
        auto password = read_file_trimmed(
            resolve(config.pnc_oem_prov_key_password_path, certs / "client/oem/OEM_LEAF_PASSWORD.txt"));
        if (not password.empty()) {
            pnc.oem_prov_key_password = std::move(password);
        }
    }

    if (pnc.needs_cert_install() and pnc.oem_prov_cert_der.empty()) {
        EVLOG_warning << prefix
                      << ": no OEM provisioning certificate loaded; CertificateInstallation will fail if needed";
    }

    return pnc;
}

iso15118::ev::d2::PnCConfig session_pnc_material(const VehicleConfig& config,
                                                 const iso15118::ev::d2::PnCConfig& configured,
                                                 const std::string& prefix) {
    const auto& store_dir = config.pnc_contract_store_dir;
    if (not pnc_offered(config) or store_dir.empty()) {
        return configured;
    }
    std::string error;
    const auto stored = load_stored_contract(store_dir, error);
    if (not stored and error.empty()) {
        EVLOG_info << prefix << ": no Plug&Charge contract stored in " << store_dir
                   << " yet; this session uses the configured contract material";
        return configured;
    }
    if (not stored) {
        EVLOG_warning << prefix << ": no usable Plug&Charge contract stored in " << store_dir << " (" << error
                      << "); this session uses the configured contract material";
        return configured;
    }
    EVLOG_info << prefix << ": this session presents the Plug&Charge contract for eMAID " << stored->emaid
               << " stored in " << store_dir;
    auto pnc = configured;
    pnc.contract = stored;
    return pnc;
}

std::string vehicle_key_password(const VehicleConfig& config) {
    return read_file_trimmed(
        resolve_path(config.device_key_password_path, pki_dir(config) / "client/vehicle/VEHICLE_LEAF_PASSWORD.txt"));
}

std::optional<dt::ServiceCategory> energy_service(types::iso15118::EnergyTransferMode mode) {
    using In = types::iso15118::EnergyTransferMode;
    switch (mode) {
    case In::DC:
    case In::DC_core:
    case In::DC_extended:
        return dt::ServiceCategory::DC;
    // The advertised AC power limits are totals across ac_phase_count lines, and the SECC's connector
    // decides how they split, so the single-phase and three-phase modes select the same service.
    case In::AC_single_phase_core:
    case In::AC_three_phase_core:
        return dt::ServiceCategory::AC;
    case In::AC_BPT:
        return dt::ServiceCategory::AC_BPT;
    case In::DC_BPT:
        return dt::ServiceCategory::DC_BPT;
    case In::AC_DER_IEC:
        return dt::ServiceCategory::AC_DER_IEC;
    case In::AC_DER_SAE:
        return dt::ServiceCategory::AC_DER_SAE;
    // MCS is the megawatt DC service: same DC parameter discovery, cable check, pre-charge and charge
    // loop, a different service id on the wire. ISO 15118-20 only.
    case In::MCS:
        return dt::ServiceCategory::MCS;
    case In::MCS_BPT:
        return dt::ServiceCategory::MCS_BPT;
    // Listed rather than folded into a default arm so -Wswitch flags a new mode.
    case In::AC_two_phase:
    case In::AC_BPT_DER:
    case In::DC_combo_core:
    case In::DC_unique:
    case In::DC_ACDP:
    case In::DC_ACDP_BPT:
    case In::WPT:
        break;
    }
    return std::nullopt;
}

iso15118::shared_datatypes::EnergyTransferMode to_iso2_transfer_mode(types::iso15118::EnergyTransferMode mode,
                                                                     bool pre_20_offered, const std::string& prefix) {
    using In = types::iso15118::EnergyTransferMode;
    using Out = iso15118::shared_datatypes::EnergyTransferMode;

    // Called once per start, so this warns once per session.
    const auto warn_unidirectional = [&] {
        if (pre_20_offered) {
            EVLOG_warning << prefix << ": EnergyTransferMode '" << types::iso15118::energy_transfer_mode_to_string(mode)
                          << "' has no ISO 15118-2 / DIN SPEC 70121 equivalent; a pre-20 session charges "
                             "unidirectionally and without DER";
        }
    };

    switch (mode) {
    case In::AC_single_phase_core:
        return Out::AC_single_phase_core;
    case In::AC_three_phase_core:
        return Out::AC_three_phase_core;
    case In::AC_BPT:
    case In::AC_DER_IEC:
    case In::AC_DER_SAE:
        warn_unidirectional();
        return Out::AC_three_phase_core;
    case In::DC_core:
        return Out::DC_core;
    case In::DC_combo_core:
        return Out::DC_combo_core;
    case In::DC_unique:
        return Out::DC_unique;
    // ISO 15118-2 and DIN SPEC 70121 have no megawatt service. This mapping is only reached when a
    // pre-20 protocol is offered alongside MCS, and the nearest DC equivalent is then the only thing
    // that can be advertised.
    case In::MCS_BPT:
        warn_unidirectional();
        return Out::DC_extended;
    case In::DC_BPT:
        warn_unidirectional();
        return Out::DC_extended;
    case In::DC:
    case In::DC_extended:
    case In::MCS:
    // energy_service rejects these first; listed rather than folded into a default arm so -Wswitch
    // flags a new mode.
    case In::AC_two_phase:
    case In::AC_BPT_DER:
    case In::DC_ACDP:
    case In::DC_ACDP_BPT:
    case In::WPT:
        break;
    }
    return Out::DC_extended;
}

std::optional<SessionRequest> make_session_request(types::iso15118::EnergyTransferMode mode,
                                                   const std::vector<iso15118::ProtocolId>& protocols,
                                                   bool eim_requested, bool enforce_contract, double departure_time_s,
                                                   double e_amount_wh, const std::string& prefix) {
    const auto service = energy_service(mode);
    if (not service) {
        return std::nullopt;
    }
    const bool pre_20_offered = std::any_of(protocols.begin(), protocols.end(), [](auto protocol) {
        return protocol != iso15118::ProtocolId::ISO15118_20;
    });
    SessionRequest request;
    request.energy_service = *service;
    request.iso2_transfer_mode = to_iso2_transfer_mode(mode, pre_20_offered, prefix);
    request.eim_requested = eim_requested;
    request.enforce_contract = enforce_contract;
    request.energy_request.energy_wh = static_cast<float>(e_amount_wh);
    if (departure_time_s > 0) {
        const auto seconds = std::min(std::ceil(departure_time_s), double{std::numeric_limits<std::uint32_t>::max()});
        request.energy_request.departure =
            std::chrono::steady_clock::now() + std::chrono::seconds{static_cast<std::int64_t>(seconds)};
    }
    return request;
}

SessionFiles make_session_files(const VehicleConfig& config, const iso15118::ev::d2::PnCConfig& material,
                                const std::optional<iso15118::ev::d2::ContractMaterial>& resume_contract,
                                const std::string& prefix) {
    SessionFiles files;
    if (resume_contract) {
        files.pnc = material;
        files.pnc.contract = resume_contract;
        EVLOG_info << prefix << ": this resumed session presents the Plug&Charge contract for eMAID "
                   << resume_contract->emaid << " its paused session installed";
    } else {
        files.pnc = session_pnc_material(config, material, prefix);
    }
    files.key_password = vehicle_key_password(config);
    return files;
}

iso15118::ev::config::EvConfig make_ev_config(const VehicleConfig& config, const Vehicle& vehicle,
                                              const SessionRequest& request, const SessionFiles& files,
                                              bool& pnc_without_tls_warned, const std::string& prefix) {
    iso15118::ev::config::EvConfig ev_config;

    ev_config.interface_name = vehicle.device;
    ev_config.evcc_id = config.evcc_id;
    ev_config.response_timeout = std::chrono::milliseconds(config.response_timeout_ms);
    ev_config.d20.authorization_timeout = std::chrono::milliseconds(config.authorization_timeout_ms);

    ev_config.supported_protocols = vehicle.protocols;

    auto& tls = ev_config.tls;
    tls.mode = tls_mode(config);
    tls.version = tls_version(config);
    tls.verify_server_certificate = config.verify_server_certificate;
    tls.enable_key_logging = config.enable_tls_key_logging;
    tls.key_logging_path = config.tls_key_logging_path;
    const auto certs = pki_dir(config);
    tls.v2g_root_cert_path = resolve_path(config.v2g_root_cert_path, certs / "ca/v2g/V2G_ROOT_CA.pem");
    tls.client_cert_chain_path =
        resolve_path(config.device_cert_chain_path, certs / "client/vehicle/VEHICLE_CERT_CHAIN.pem");
    tls.client_key_path = resolve_path(config.device_key_path, certs / "client/vehicle/VEHICLE_LEAF.key");
    tls.client_key_password = files.key_password;

    // ISO 15118-2 / DIN SPEC 70121 session parameters.
    auto& params = ev_config.params;
    if (vehicle.evcc_mac) {
        params.evcc_mac = *vehicle.evcc_mac;
    }
    params.energy_transfer_mode = request.iso2_transfer_mode;
    params.energy_request = request.energy_request;
    params.ac.e_amount = static_cast<float>(config.iso2_ac_e_amount_wh);
    params.ac.ev_max_voltage = static_cast<float>(config.iso2_ac_ev_max_voltage_v);
    params.ac.ev_max_current = static_cast<float>(config.iso2_ac_ev_max_current_a);
    params.ac.ev_min_current = static_cast<float>(config.iso2_ac_ev_min_current_a);

    // PnC requires TLS [V2G2-632]; the -2 engine falls back to EIM whenever the SECC does not offer
    // Contract. enable_pnc alone prefers Contract; an explicit ExternalPayment opts this one session out.
    // allow_pnc_without_tls accepts the deviation deliberately; the engine applies the same rule again
    // against the transport the SDP response actually named, which is the one that counts.
    if (pnc_offered(config)) {
        ev_config.d20.supported_auth_options = {dt::Authorization::PnC, dt::Authorization::EIM};
        params.pnc = files.pnc;
        params.pnc.prefer_contract = not request.eim_requested;
    } else {
        if (config.enable_pnc and not pnc_without_tls_warned) {
            EVLOG_warning << prefix
                          << ": Plug&Charge is requested but TLS is not active; Plug&Charge requires TLS, "
                             "falling back to EIM only (set allow_pnc_without_tls to deviate from the standard)";
            pnc_without_tls_warned = true;
        }
        ev_config.d20.supported_auth_options = {dt::Authorization::EIM};
        params.pnc.prefer_contract = false;
    }
    params.pnc.allow_insecure_pnc = config.allow_pnc_without_tls;
    // Select Contract even without an offer or PnC material (negative testing).
    params.pnc.enforce_contract = request.enforce_contract;

    ev_config.energy_service = request.energy_service;
    ev_config.d20.control_mode =
        (config.control_mode == "Scheduled") ? dt::ControlMode::Scheduled : dt::ControlMode::Dynamic;
    ev_config.resume = request.resume;
    ev_config.has_cp_state_feedback = request.has_cp_state_feedback;

    if (request.energy_service == dt::ServiceCategory::AC_DER_IEC) {
        ev_config.d20.der_control_functions = config.der_functions;
        ev_config.d20.der_stop_on_unsupported_functions = config.der_stop_on_unsupported_functions;
    }

    ev_config.d20.sae_profile = vehicle.sae_profile;
    ev_config.d20.cpd_rounds = static_cast<std::uint16_t>(config.cpd_rounds);
    ev_config.d20.der_stop_on_invalid_control = config.der_stop_on_invalid_control;

    return ev_config;
}

void seed_session_params(const VehicleConfig& config, types::iso15118::EnergyTransferMode mode,
                         dt::ServiceCategory energy_service, std::optional<float> cmd_max_discharge_power,
                         std::optional<float> cmd_max_discharge_current, iso15118::ev::params::AcChargeParams& ac,
                         iso15118::ev::params::DcChargeParams& dc, const std::string& prefix) {
    // Measurements from an earlier session, a DC battery voltage above all, are not this grid's.
    ac.present_voltage.reset();
    ac.present_frequency.reset();
    ac.present_active_power = 0.0f;
    ac.der_alarm_status = 0;
    if (iso15118::ev::d20::is_ac_family(energy_service)) {
        ac.phase_count = static_cast<std::uint8_t>(config.ac_phase_count);
        ac.max_charge_power = static_cast<float>(config.ac_max_charge_power_w);
        ac.min_charge_power = static_cast<float>(config.ac_min_charge_power_w);
    }
    // The DER services carry mandatory discharge limits on the wire just as AC_BPT does.
    if (energy_service == dt::ServiceCategory::AC_BPT or energy_service == dt::ServiceCategory::AC_DER_IEC or
        energy_service == dt::ServiceCategory::AC_DER_SAE) {
        ac.max_discharge_power = static_cast<float>(config.ac_max_discharge_power_w);
        ac.min_discharge_power = static_cast<float>(config.ac_min_discharge_power_w);
    }
    // The configured totals span three lines; a single-phase session asks for one and its share.
    if (mode == types::iso15118::EnergyTransferMode::AC_single_phase_core and ac.phase_count == 3) {
        EVLOG_info << prefix
                   << ": AC_single_phase_core with ac_phase_count 3; this session asks for one phase "
                      "and advertises a third of the configured charge power";
        ac.phase_count = 1;
        ac.max_charge_power /= 3.0f;
        ac.min_charge_power /= 3.0f;
    }
    if (energy_service == dt::ServiceCategory::DC_BPT or energy_service == dt::ServiceCategory::MCS_BPT) {
        dc.max_discharge_power = cmd_max_discharge_power.value_or(static_cast<float>(config.dc_max_discharge_power_w));
        dc.min_discharge_power = static_cast<float>(config.dc_min_discharge_power_w);
        dc.max_discharge_current =
            cmd_max_discharge_current.value_or(static_cast<float>(config.dc_max_discharge_current_a));
    }
}

std::vector<std::string> validate_session(const iso15118::ev::config::EvConfig& ev_config,
                                          const iso15118::ev::params::AcChargeParams& ac,
                                          const iso15118::ev::params::DcChargeParams& dc) {
    auto problems = iso15118::ev::config::validate_config(ev_config);
    const auto append = [&problems](std::vector<std::string> more) {
        problems.insert(problems.end(), std::make_move_iterator(more.begin()), std::make_move_iterator(more.end()));
    };
    if (iso15118::ev::d20::is_ac_family(ev_config.energy_service)) {
        append(iso15118::ev::config::validate_ac_charge_params(ac));
    }
    if (iso15118::ev::d20::is_dc_family(ev_config.energy_service)) {
        append(iso15118::ev::config::validate_dc_charge_params(dc));
    }
    return problems;
}

std::optional<iso15118::ev::params::DcChargeParams> merge_dc_params(const iso15118::ev::params::DcChargeParams& current,
                                                                    const types::iso15118::DcEvParameters& update,
                                                                    std::vector<std::string>& problems) {
    auto params = current;
    const auto merge = [](float& field, const auto& value) {
        if (value) {
            field = static_cast<float>(*value);
        }
    };
    merge(params.max_charge_power, update.max_power_limit);
    merge(params.max_charge_current, update.max_current_limit);
    merge(params.max_voltage, update.max_voltage_limit);
    merge(params.min_voltage, update.min_voltage_limit);
    merge(params.energy_capacity, update.energy_capacity);
    merge(params.target_voltage, update.target_voltage);
    merge(params.target_current, update.target_current);
    problems = iso15118::ev::config::validate_dc_charge_params(params);
    if (not problems.empty()) {
        return std::nullopt;
    }
    return params;
}

void apply_present_values(const types::iso15118::EvPresentValues& values, iso15118::ev::params::AcChargeParams& ac,
                          iso15118::ev::params::DcChargeParams& dc, iso15118::ev::Controller* live,
                          const std::string& prefix) {
    if (values.present_voltage) {
        const auto voltage = static_cast<float>(*values.present_voltage);
        dc.present_voltage = voltage;
        ac.present_voltage = voltage;
        if (live) {
            live->update_present_voltage(voltage);
        }
    }
    if (values.present_active_power) {
        const auto power = static_cast<float>(*values.present_active_power);
        ac.present_active_power = power;
        if (live) {
            live->update_present_active_power(power);
        }
    }
    if (values.present_frequency) {
        const auto frequency = static_cast<float>(*values.present_frequency);
        ac.present_frequency = frequency;
        if (live) {
            live->update_present_frequency(frequency);
        }
    }
    if (values.der_alarm_status) {
        const auto alarm_status = *values.der_alarm_status;
        // Command arguments are schema-checked only when the manager validates, so the minimum may not hold.
        if (alarm_status < 0) {
            EVLOG_warning << prefix << ": ignoring negative der_alarm_status " << alarm_status;
        } else {
            ac.der_alarm_status = static_cast<std::uint32_t>(alarm_status);
            if (live) {
                live->update_der_alarm_status(static_cast<std::uint32_t>(alarm_status));
            }
        }
    }
}

void install_logging() {
    iso15118::io::set_logging_callback([](const iso15118::LogLevel& level, const std::string& msg) {
        switch (level) {
        case iso15118::LogLevel::Error:
            EVLOG_error << msg;
            break;
        case iso15118::LogLevel::Warning:
            EVLOG_warning << msg;
            break;
        case iso15118::LogLevel::Info:
            EVLOG_info << msg;
            break;
        case iso15118::LogLevel::Debug:
            EVLOG_debug << msg;
            break;
        case iso15118::LogLevel::Trace:
            EVLOG_verbose << msg;
            break;
        default:
            EVLOG_critical << "(Loglevel not defined) - " << msg;
            break;
        }
    });
}

} // namespace everest::lib::iso15118_ev
