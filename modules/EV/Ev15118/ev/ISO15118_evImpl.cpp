// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "ISO15118_evImpl.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <everest/conversions/iso15118/v2g_message_id.hpp>
#include <everest/exceptions.hpp>

#include <iso15118/d20/ac_powers.hpp>
#include <iso15118/ev/config/validation.hpp>
#include <iso15118/ev/d2/pnc_material.hpp>
#include <iso15118/ev/d20/service_family.hpp>
#include <iso15118/io/logging.hpp>
#include <iso15118/message/type.hpp>
#include <iso15118/message/v2g_message_type.hpp>
#include <iso15118/sae_modes.hpp>
#include <iso15118/session/protocol.hpp>

#include "contract_store.hpp"
#include "der_publish_names.hpp"
#include "sae_profile_config.hpp"

namespace {
template <class F> class ScopeGuard {
public:
    explicit ScopeGuard(F f) : m_f(std::move(f)) {
    }
    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;
    ~ScopeGuard() {
        m_f();
    }

private:
    F m_f;
};

std::string resolve_path(const std::string& configured, const std::filesystem::path& fallback) {
    return configured.empty() ? fallback.string() : configured;
}

// Whole file, or empty when it cannot be opened.
std::string read_file_text(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (not file.is_open()) {
        return {};
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// Empty for a missing or blank file, which means an unencrypted private key.
std::string read_file_trimmed(const std::filesystem::path& path) {
    auto content = read_file_text(path);
    const auto not_space = [](unsigned char c) { return std::isspace(c) == 0; };
    content.erase(content.begin(), std::find_if(content.begin(), content.end(), not_space));
    content.erase(std::find_if(content.rbegin(), content.rend(), not_space).base(), content.end());
    return content;
}

// start_charging's DepartureTime (s from now) and EAmount (kWh), both validated finite and not
// negative; 0 states nothing.
iso15118::ev::params::EnergyRequest make_energy_request(double departure_time_s, double e_amount_kwh) {
    iso15118::ev::params::EnergyRequest request;
    request.energy_wh = static_cast<float>(e_amount_kwh * 1000.0);
    if (departure_time_s > 0) {
        const auto seconds = std::min(std::ceil(departure_time_s), double{std::numeric_limits<std::uint32_t>::max()});
        request.departure = std::chrono::steady_clock::now() + std::chrono::seconds{static_cast<std::int64_t>(seconds)};
    }
    return request;
}

// ISO 15118-2 / DIN energy transfer mode granularity. Neither generation has a BPT or a DER
// mode, so those collapse to the matching unidirectional mode.
iso15118::shared_datatypes::EnergyTransferMode to_iso2_transfer_mode(types::iso15118::EnergyTransferMode mode,
                                                                     bool pre_20_offered) {
    using In = types::iso15118::EnergyTransferMode;
    using Out = iso15118::shared_datatypes::EnergyTransferMode;

    // Called once per start_charging, so this warns once per session.
    const auto warn_unidirectional = [&] {
        if (pre_20_offered) {
            EVLOG_warning << "Ev15118: EnergyTransferMode '" << types::iso15118::energy_transfer_mode_to_string(mode)
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
    // ISO 15118-2 and DIN SPEC 70121 have no megawatt service. This mapping is only reached
    // when a pre-20 protocol is offered alongside MCS, and the nearest DC equivalent is then
    // the only thing that can be advertised.
    case In::MCS_BPT:
        warn_unidirectional();
        return Out::DC_extended;
    case In::DC_BPT:
        warn_unidirectional();
        return Out::DC_extended;
    case In::DC:
    case In::DC_extended:
    case In::MCS:
    // Rejected by start_charging before this runs; listed rather than folded into a default
    // arm so -Wswitch flags a new mode.
    case In::AC_two_phase:
    case In::AC_BPT_DER:
    case In::DC_ACDP:
    case In::DC_ACDP_BPT:
    case In::WPT:
        break;
    }
    return Out::DC_extended;
}

// 6 byte MAC of `device` from sysfs ("aa:bb:cc:dd:ee:ff"), or nullopt on any failure.
std::optional<std::array<uint8_t, 6>> read_interface_mac(const std::string& device) {
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
    std::array<uint8_t, 6> mac{};
    if (std::sscanf(line.c_str(), "%2hhx:%2hhx:%2hhx:%2hhx:%2hhx:%2hhx", &mac[0], &mac[1], &mac[2], &mac[3], &mac[4],
                    &mac[5]) != 6) {
        return std::nullopt;
    }
    return mac;
}

// ISO 15118-2 Plug&Charge material, sourced from config-path files with the committed PKI layout as
// the default. The trust root is the V2G root already used for TLS.
iso15118::ev::d2::PnCConfig build_pnc_config(const module::Conf& config, const std::filesystem::path& etc_path) {
    namespace material = iso15118::ev::d2::pnc_material;
    const auto certs = etc_path / "certs";
    const auto resolve = [](const std::string& configured, const std::filesystem::path& fallback) {
        return configured.empty() ? fallback : std::filesystem::path(configured);
    };

    iso15118::ev::d2::PnCConfig pnc;
    pnc.force_cert_install = config.pnc_force_cert_install;

    // Pre-installed contract (MO) chain, key and eMAID, presented in PaymentDetails. The repo
    // PKI ships no single-file MO chain, so the default is the leaf plus the intermediates
    // (MO_SUB_CA2, MO_SUB_CA1, in that order); a configured path is one PEM with the whole chain.
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
        EVLOG_warning << "Ev15118: no Plug&Charge contract certificate chain found in "
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
        EVLOG_warning << "Ev15118: no OEM provisioning certificate loaded; CertificateInstallation will fail if needed";
    }

    return pnc;
}

// Empty when no password file is configured or found: an unencrypted vehicle key.
std::string vehicle_key_password(const module::Conf& config, const std::filesystem::path& etc_path) {
    return read_file_trimmed(
        resolve_path(config.device_key_password_path, etc_path / "certs/client/vehicle/VEHICLE_LEAF_PASSWORD.txt"));
}

// The manager validates tls and tls_version against the manifest enums.
iso15118::ev::config::TlsMode tls_mode(const module::Conf& config) {
    using iso15118::ev::config::TlsMode;
    return (config.tls == "require") ? TlsMode::Require : (config.tls == "prefer") ? TlsMode::Prefer : TlsMode::Off;
}

iso15118::ev::config::TlsVersion tls_version(const module::Conf& config) {
    using iso15118::ev::config::TlsVersion;
    return (config.tls_version == "1.3")   ? TlsVersion::Tls1_3
           : (config.tls_version == "1.2") ? TlsVersion::Tls1_2
                                           : TlsVersion::Auto;
}

// PnC requires TLS [V2G2-632] unless allow_pnc_without_tls deviates deliberately.
bool pnc_offered(const module::Conf& config) {
    return config.enable_pnc and
           (tls_mode(config) != iso15118::ev::config::TlsMode::Off or config.allow_pnc_without_tls);
}

// The comma-separated list in SAP priority order; empty, unknown or repeated entries fail startup.
std::vector<iso15118::ProtocolId> supported_protocols(const std::string& list) {
    using iso15118::ProtocolId;
    constexpr std::array<std::pair<std::string_view, ProtocolId>, 3> names{{
        {"ISO15118_20", ProtocolId::ISO15118_20},
        {"ISO15118_2", ProtocolId::ISO15118_2},
        {"DIN70121", ProtocolId::DIN70121},
    }};
    const auto trim = [](std::string_view text) {
        const auto first = text.find_first_not_of(' ');
        return (first == std::string_view::npos)
                   ? std::string{}
                   : std::string(text.substr(first, text.find_last_not_of(' ') + 1 - first));
    };
    std::vector<ProtocolId> protocols;
    // A blank list has no entries; any other list has one more entry than it has commas.
    const std::string_view text(list);
    for (std::size_t begin = trim(text).empty() ? text.size() + 1 : 0; begin <= text.size();) {
        const auto end = std::min(text.find(',', begin), text.size());
        const auto entry = trim(text.substr(begin, end - begin));
        begin = end + 1;
        const auto name = std::find_if(names.begin(), names.end(), [&](const auto& n) { return n.first == entry; });
        if (name == names.end()) {
            EVLOG_AND_THROW(Everest::EverestConfigError("Ev15118: supported_protocols names unknown protocol '" +
                                                        entry + "'; use ISO15118_20, ISO15118_2 or DIN70121"));
        }
        if (std::find(protocols.begin(), protocols.end(), name->second) != protocols.end()) {
            EVLOG_AND_THROW(
                Everest::EverestConfigError("Ev15118: supported_protocols names " + entry + " more than once"));
        }
        protocols.push_back(name->second);
    }
    if (protocols.empty()) {
        EVLOG_AND_THROW(Everest::EverestConfigError(
            "Ev15118: no protocol enabled; list ISO15118_20, ISO15118_2 or DIN70121 in supported_protocols"));
    }
    return protocols;
}

std::string_view signal_to_string(iso15118::ev::feedback::Signal signal) {
    using Signal = iso15118::ev::feedback::Signal;
    switch (signal) {
    case Signal::DLINK_TERMINATE:
        return "DLINK_TERMINATE";
    case Signal::DLINK_PAUSE:
        return "DLINK_PAUSE";
    case Signal::DLINK_ERROR:
        return "DLINK_ERROR";
    }
    return "unknown";
}

} // namespace

namespace module {
namespace ev {

void ISO15118_evImpl::init() {
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

    // Read once, before any command can reach start_charging.
    std::string error;
    auto profile = parse_sae_inverter_profile(mod->config.sae_inverter_profile_path, error);
    if (not profile) {
        EVLOG_AND_THROW(Everest::EverestConfigError("Ev15118: sae_inverter_profile_path '" +
                                                    mod->config.sae_inverter_profile_path + "' is invalid: " + error));
    }
    if (const auto problems = iso15118::ev::config::validate_sae_profile(*profile); not problems.empty()) {
        std::string joined;
        for (const auto& problem : problems) {
            joined.append(joined.empty() ? "" : "; ").append(problem);
        }
        EVLOG_AND_THROW(Everest::EverestConfigError("Ev15118: sae_inverter_profile_path '" +
                                                    mod->config.sae_inverter_profile_path + "' is invalid: " + joined));
    }
    sae_profile = std::move(*profile);
}

void ISO15118_evImpl::ready() {
    protocols = supported_protocols(mod->config.supported_protocols);

    // Resolve the HLC interface once ("auto" -> first usable ipv6 interface) so the connection and
    // the EVCCID MAC use the same device.
    hlc_device = mod->config.device;
    if (not iso15118::ev::resolve_interface(hlc_device)) {
        if (mod->config.device == "auto") {
            // "auto" has no usable as-is value; the Controller would only fail to resolve it again.
            EVLOG_AND_THROW(Everest::EverestConfigError(
                "Ev15118: device 'auto' found no usable ipv6 interface; set device to the HLC interface"));
        }
        EVLOG_warning << "Ev15118: could not resolve HLC interface '" << mod->config.device << "'; using it as-is";
        hlc_device = mod->config.device;
    }

    // Only override the library EVCCID default when a real MAC was read.
    evcc_mac = read_interface_mac(hlc_device);
    if (evcc_mac) {
        EVLOG_info << "Ev15118: using the EVCC MAC of interface " << hlc_device;
    } else {
        EVLOG_warning << "Ev15118: could not read the MAC of interface '" << hlc_device
                      << "', keeping the library default EVCCID for ISO 15118-2 / DIN SPEC 70121";
    }

    // A pinned tls_version runs every enabled protocol over it; name each deviation [V2G2-067].
    iso15118::ev::config::EvConfig tls_policy;
    tls_policy.supported_protocols = protocols;
    tls_policy.tls.mode = tls_mode(mod->config);
    tls_policy.tls.version = tls_version(mod->config);
    for (const auto& deviation : iso15118::ev::config::tls_deviations(tls_policy)) {
        EVLOG_warning << "Ev15118: " << deviation;
    }

    // OEM and contract material are read once before starting the session worker.
    if (mod->config.enable_pnc) {
        pnc_material = build_pnc_config(mod->config, mod->info.paths.etc);
    }

    worker = std::thread([this] { session_worker(); });
    worker_started.store(true, std::memory_order_release);
}

// Idempotent, so the destructor still tears down cleanly if the framework never calls this.
void ISO15118_evImpl::shutdown() {
    {
        auto h = session.handle();
        (*h).shutting_down = true;
        if ((*h).current) {
            (*h).current->shutdown();
        }
    }
    session.notify_all();
    if (worker.joinable()) {
        worker.join();
    }
}

ISO15118_evImpl::~ISO15118_evImpl() {
    shutdown();
}

iso15118::ev::d2::PnCConfig ISO15118_evImpl::session_pnc_material() const {
    const auto& store_dir = mod->config.pnc_contract_store_dir;
    if (not pnc_offered(mod->config) or store_dir.empty()) {
        return pnc_material;
    }
    std::string error;
    const auto stored = load_stored_contract(store_dir, error);
    if (not stored and error.empty()) {
        EVLOG_info << "Ev15118: no Plug&Charge contract stored in " << store_dir
                   << " yet; this session uses the configured contract material";
        return pnc_material;
    }
    if (not stored) {
        EVLOG_warning << "Ev15118: no usable Plug&Charge contract stored in " << store_dir << " (" << error
                      << "); this session uses the configured contract material";
        return pnc_material;
    }
    EVLOG_info << "Ev15118: this session presents the Plug&Charge contract for eMAID " << stored->emaid << " stored in "
               << store_dir;
    auto pnc = pnc_material;
    pnc.contract = stored;
    return pnc;
}

iso15118::ev::config::EvConfig ISO15118_evImpl::make_ev_config(const SessionState& state,
                                                               const SessionFiles& files) const {
    iso15118::ev::config::EvConfig ev_config;

    // Resolved in ready(), before start_charging accepts a request.
    ev_config.interface_name = hlc_device;
    ev_config.evcc_id = mod->config.evcc_id;
    ev_config.response_timeout = std::chrono::milliseconds(mod->config.response_timeout_ms);
    ev_config.d20.authorization_timeout = std::chrono::milliseconds(mod->config.authorization_timeout_ms);

    ev_config.supported_protocols = protocols;

    auto& tls = ev_config.tls;
    tls.mode = tls_mode(mod->config);
    tls.version = tls_version(mod->config);
    tls.verify_server_certificate = mod->config.verify_server_certificate;
    tls.enable_key_logging = mod->config.enable_tls_key_logging;
    tls.key_logging_path = mod->config.tls_key_logging_path;
    const auto certs = std::filesystem::path(mod->info.paths.etc) / "certs";
    tls.v2g_root_cert_path = resolve_path(mod->config.v2g_root_cert_path, certs / "ca/v2g/V2G_ROOT_CA.pem");
    tls.client_cert_chain_path =
        resolve_path(mod->config.device_cert_chain_path, certs / "client/vehicle/VEHICLE_CERT_CHAIN.pem");
    tls.client_key_path = resolve_path(mod->config.device_key_path, certs / "client/vehicle/VEHICLE_LEAF.key");
    tls.client_key_password = files.key_password;

    namespace dt = iso15118::message_20::datatypes;

    // ISO 15118-2 / DIN SPEC 70121 session parameters.
    auto& params = ev_config.params;
    if (evcc_mac) {
        params.evcc_mac = *evcc_mac;
    }
    params.energy_transfer_mode = state.iso2_transfer_mode;
    params.energy_request = state.energy_request;
    params.ac.e_amount = static_cast<float>(mod->config.iso2_ac_e_amount_wh);
    params.ac.ev_max_voltage = static_cast<float>(mod->config.iso2_ac_ev_max_voltage_v);
    params.ac.ev_max_current = static_cast<float>(mod->config.iso2_ac_ev_max_current_a);
    params.ac.ev_min_current = static_cast<float>(mod->config.iso2_ac_ev_min_current_a);

    // PnC requires TLS [V2G2-632]; the -2 engine falls back to EIM whenever the SECC does not
    // offer Contract. enable_pnc alone prefers Contract, because EvManager's default payment
    // option is `auto`; an explicit ExternalPayment opts this one session out.
    // allow_pnc_without_tls accepts the deviation deliberately; the engine applies the same rule
    // again against the transport the SDP response actually named, which is the one that counts.
    if (pnc_offered(mod->config)) {
        ev_config.d20.supported_auth_options = {dt::Authorization::PnC, dt::Authorization::EIM};
        params.pnc = files.pnc;
        params.pnc.prefer_contract = not state.eim_requested;
    } else {
        if (mod->config.enable_pnc and not pnc_without_tls_warned) {
            EVLOG_warning << "Ev15118: enable_pnc is set but TLS is not active; Plug&Charge requires TLS, "
                             "falling back to EIM only (set allow_pnc_without_tls to deviate from the standard)";
            pnc_without_tls_warned = true;
        }
        ev_config.d20.supported_auth_options = {dt::Authorization::EIM};
        params.pnc.prefer_contract = false;
    }
    params.pnc.allow_insecure_pnc = mod->config.allow_pnc_without_tls;
    // Select Contract even without an offer or PnC material (negative testing).
    params.pnc.enforce_contract = state.enforce_contract;

    const auto energy_service = state.energy_service;
    ev_config.energy_service = energy_service;
    ev_config.d20.control_mode =
        (mod->config.control_mode == "Scheduled") ? dt::ControlMode::Scheduled : dt::ControlMode::Dynamic;
    ev_config.resume = state.paused;
    ev_config.has_cp_state_feedback = state.cp_c_or_d.has_value();

    if (energy_service == dt::ServiceCategory::AC_DER_IEC) {
        auto& functions = ev_config.d20.der_control_functions;
        functions.over_frequency_watt_mode = mod->config.der_over_frequency_watt_mode;
        functions.under_frequency_watt_mode = mod->config.der_under_frequency_watt_mode;
        functions.volt_watt_mode = mod->config.der_volt_watt_mode;
        functions.volt_var_mode = mod->config.der_volt_var_mode;
        functions.watt_var_mode = mod->config.der_watt_var_mode;
        functions.watt_cos_phi_mode = mod->config.der_watt_cos_phi_mode;
        functions.dso_q_setpoint_provision = mod->config.der_dso_q_setpoint_provision;
        functions.dso_cos_phi_setpoint_provision = mod->config.der_dso_cos_phi_setpoint_provision;
        functions.dc_injection_restriction = mod->config.der_dc_injection_restriction;
        functions.zero_current_mode = mod->config.der_zero_current_mode;
        functions.over_voltage_fault_ride_through_mode = mod->config.der_over_voltage_fault_ride_through_mode;
        functions.under_voltage_fault_ride_through_mode = mod->config.der_under_voltage_fault_ride_through_mode;
        ev_config.d20.der_stop_on_unsupported_functions = mod->config.der_stop_on_unsupported_functions;
    }

    ev_config.d20.sae_profile = sae_profile;
    // The manager validates config values against the manifest bounds, [1, 65535] here.
    ev_config.d20.cpd_rounds = static_cast<std::uint16_t>(mod->config.cpd_rounds);
    ev_config.d20.der_stop_on_invalid_control = mod->config.der_stop_on_invalid_control;

    return ev_config;
}

iso15118::ev::feedback::Callbacks
ISO15118_evImpl::make_callbacks(iso15118::message_20::datatypes::ServiceCategory energy_service) {
    iso15118::ev::feedback::Callbacks callbacks;

    callbacks.connected = [](const iso15118::io::Ipv6EndPoint&) { EVLOG_info << "Ev15118: connected to SECC"; };

    callbacks.v2g_message = [this](const iso15118::V2gMessageType& type) {
        publish_v2g_messages(types::iso15118::V2gMessages{.id = conversions::to_v2g_message_id(type)});
    };

    callbacks.signal = [](iso15118::ev::feedback::Signal signal) {
        EVLOG_debug << "Ev15118: signal " << signal_to_string(signal);
    };

    callbacks.selected_protocol = [](iso15118::ProtocolId protocol) {
        EVLOG_info << "Ev15118: selected protocol " << iso15118::protocol_id_to_string(protocol);
    };

    callbacks.evse_id = [](const std::string& evse_id) { EVLOG_info << "Ev15118: EVSE id " << evse_id; };

    callbacks.dc_evse_present_limits = [](const iso15118::ev::feedback::DcMaximumLimits& limits) {
        EVLOG_debug << "Ev15118: DC EVSE present limits: " << limits.voltage << " V, " << limits.current << " A, "
                    << limits.power.value_or(NAN) << " W";
    };

    callbacks.dc_evse_present_values = [this](float voltage, float current) {
        publish_dc_evse_present_voltage(voltage);
        publish_dc_evse_present_current(current);
    };

    callbacks.evse_session_info = [](const iso15118::ev::d20::EVSESessionInfo&) {
        EVLOG_debug << "Ev15118: EVSE session info received";
    };

    // The session continues with the in-memory contract whatever happens to the stored copy.
    callbacks.pnc_contract_installed = [this](const std::string& chain_pem, const std::string& key_pem,
                                              const std::string& emaid) {
        std::string error;
        auto installed = decode_contract(chain_pem, key_pem, error);
        if (not installed) {
            EVLOG_error << "Ev15118: Plug&Charge contract installed for eMAID " << emaid << " is unusable: " << error
                        << "; only this session presents it";
            return;
        }
        {
            auto h = session.handle();
            (*h).installed_contract = std::move(installed);
        }
        const auto& store_dir = mod->config.pnc_contract_store_dir;
        if (store_dir.empty()) {
            EVLOG_info << "Ev15118: Plug&Charge contract installed for eMAID " << emaid
                       << "; not stored, pnc_contract_store_dir is empty";
            return;
        }
        std::string warning;
        if (store_contract(store_dir, chain_pem, key_pem, emaid, error, warning)) {
            EVLOG_info << "Ev15118: Plug&Charge contract installed for eMAID " << emaid << ", stored in " << store_dir;
            if (not warning.empty()) {
                EVLOG_warning << "Ev15118: " << store_dir << ": " << warning;
            }
        } else {
            EVLOG_error << "Ev15118: Plug&Charge contract installed for eMAID " << emaid << " could not be stored in "
                        << store_dir << ": " << error << "; only this session and its resume present it";
        }
    };

    callbacks.pause_from_charger = [this] { publish_pause_from_charger(nullptr); };

    callbacks.timed_out = [] { EVLOG_warning << "Ev15118: response watchdog timed out"; };

    callbacks.power_delivery_stopped = [this] { publish_power_delivery_stopped(nullptr); };

    callbacks.welding_detection_failed = [this](float last_voltage_v, int cycles) {
        EVLOG_error << "Ev15118: welding detection failed after " << cycles << " cycles, last voltage "
                    << last_voltage_v << " V";
        types::iso15118::WeldingDetectionFailure failure;
        failure.cycles = cycles;
        if (not std::isnan(last_voltage_v)) {
            failure.last_voltage_V = last_voltage_v;
        }
        publish_welding_detection_failed(failure);
    };

    callbacks.stopped = [] { EVLOG_info << "Ev15118: session stopped"; };

    callbacks.ev_power_ready = [this] { publish_ev_power_ready(true); };

    callbacks.dc_power_on = [this] { publish_dc_power_on(nullptr); };

    callbacks.stop_from_charger = [this] { publish_stop_from_charger(nullptr); };

    callbacks.ac_evse_max_current = [this](float current) { publish_ac_evse_max_current(current); };

    callbacks.ac_limits = [](const iso15118::message_20::datatypes::AC_CPDResEnergyTransferMode& limits) {
        namespace dt = iso15118::message_20::datatypes;
        EVLOG_info << "Ev15118: AC EVSE limits: max charge power " << dt::from_RationalNumber(limits.max_charge_power)
                   << " W, min charge power " << dt::from_RationalNumber(limits.min_charge_power) << " W";
    };

    callbacks.ac_bpt_limits = [](const iso15118::message_20::datatypes::BPT_AC_CPDResEnergyTransferMode& limits) {
        namespace dt = iso15118::message_20::datatypes;
        EVLOG_info << "Ev15118: AC BPT EVSE limits: max discharge power "
                   << dt::from_RationalNumber(limits.max_discharge_power) << " W, min discharge power "
                   << dt::from_RationalNumber(limits.min_discharge_power) << " W";
    };

    callbacks.dc_bpt_limits = [](const iso15118::message_20::datatypes::BPT_DC_CPDResEnergyTransferMode& limits) {
        namespace dt = iso15118::message_20::datatypes;
        EVLOG_info << "Ev15118: DC BPT EVSE limits: max discharge power "
                   << dt::from_RationalNumber(limits.max_discharge_power) << " W, min discharge power "
                   << dt::from_RationalNumber(limits.min_discharge_power) << " W";
    };

    callbacks.ac_der_limits = [](const iso15118::message_20::datatypes::DER_AC_CPDResEnergyTransferMode& limits) {
        namespace dt = iso15118::message_20::datatypes;
        EVLOG_info << "Ev15118: AC DER EVSE limits: nominal charge power "
                   << dt::from_RationalNumber(limits.nominal_charge_power) << " W, nominal discharge power "
                   << dt::from_RationalNumber(limits.nominal_discharge_power) << " W, max discharge power "
                   << dt::from_RationalNumber(limits.max_discharge_power) << " W, operating mode "
                   << (limits.operating_mode == dt::OperatingMode::GridForming ? "GridForming" : "GridFollowing")
                   << ", grid connection mode "
                   << (limits.grid_connection_mode == dt::GridConnectionMode::GridIslanded ? "GridIslanded"
                                                                                           : "GridConnected");
    };

    const auto publish_target_power = [this](const iso15118::d20::AcTargetPower& control) {
        namespace dt = iso15118::message_20::datatypes;
        const auto convert = [](const std::optional<dt::RationalNumber>& value) -> std::optional<float> {
            return value.has_value() ? std::make_optional(dt::from_RationalNumber(*value)) : std::nullopt;
        };
        types::iso15118::AcTargetPower target;
        target.target_active_power = convert(control.target_active_power);
        target.target_active_power_L2 = convert(control.target_active_power_L2);
        target.target_active_power_L3 = convert(control.target_active_power_L3);
        target.target_reactive_power = convert(control.target_reactive_power);
        target.target_reactive_power_L2 = convert(control.target_reactive_power_L2);
        target.target_reactive_power_L3 = convert(control.target_reactive_power_L3);
        publish_ac_evse_target_power(target);
    };
    callbacks.ac_target_power = publish_target_power;

    // Every Dynamic response carries this block; the session's first is published, and a later one
    // only when its setpoints change. Callbacks run on the session worker only.
    auto last_der_control = std::make_shared<std::optional<types::iso15118::DerControlReceived>>();
    callbacks.der_control =
        [this, last_der_control](const iso15118::message_20::datatypes::DER_Dynamic_AC_CLResControlMode& control) {
            namespace dt = iso15118::message_20::datatypes;
            types::iso15118::DerControlReceived received;
            received.flavor = types::iso15118::DerFlavor::AC_DER_IEC;
            received.source = types::iso15118::DerControlSource::ChargeLoop;
            if (control.dso_q_setpoint) {
                received.dso_q_setpoint = dt::from_RationalNumber(control.dso_q_setpoint->dso_q_setpoint_value);
            }
            if (control.dso_cos_phi_setpoint) {
                received.dso_cos_phi_setpoint =
                    dt::from_RationalNumber(control.dso_cos_phi_setpoint->dso_cos_phi_setpoint_value);
            }
            const auto& last = *last_der_control;
            if (last and last->dso_q_setpoint == received.dso_q_setpoint and
                last->dso_cos_phi_setpoint == received.dso_cos_phi_setpoint) {
                return;
            }
            *last_der_control = received;
            publish_der_control_received(received);
        };

    // Dictated DER curves are observed, not applied. The published summary has no curve fields.
    callbacks.der_curves = [this](const iso15118::message_20::datatypes::DerControl& control) {
        std::ostringstream line;
        line << "Ev15118: DER curves dictated:";
        bool any = false;
        const auto append = [&](bool present, const char* name) {
            if (present) {
                line << ' ' << name;
                any = true;
            }
        };
        append(control.over_voltage_fault_ride_through.has_value(), "over_voltage_fault_ride_through");
        append(control.under_voltage_fault_ride_through.has_value(), "under_voltage_fault_ride_through");
        append(control.zero_current.has_value(), "zero_current");
        append(control.reactive_power_support.has_value(), "reactive_power_support");
        append(control.active_power_support.has_value(), "active_power_support");
        append(control.max_level_dc_injection.has_value(), "max_level_dc_injection");
        if (not any) {
            line << " none";
        }
        EVLOG_info << line.str();
        publish_der_control_received({.flavor = types::iso15118::DerFlavor::AC_DER_IEC,
                                      .source = types::iso15118::DerControlSource::ChargeParameterDiscovery});
    };

    callbacks.sae_cpd_limits =
        [this](const iso15118::message_20::datatypes::sae::DER_SAE_AC_CPDResEnergyTransferMode& limits,
               const iso15118::ev::d20::der::DerControlProblems& problems) {
            const auto& control = limits.der_control_cpd_res;
            publish_der_control_received(sae_der_control_received(
                types::iso15118::DerControlSource::ChargeParameterDiscovery,
                control.enter_service_cpd_res.permit_service, iso15118::sae::derive_enabled_modes(control), problems));
        };

    callbacks.sae_der_control = [this](
                                    const iso15118::message_20::datatypes::sae::DER_Dynamic_AC_CLResControlMode& mode,
                                    const iso15118::ev::d20::der::DerControlProblems& problems) {
        const auto& control = mode.der_control_cl_res;
        publish_der_control_received(sae_der_control_received(types::iso15118::DerControlSource::ChargeLoop,
                                                              control.enter_service_cl_res.permit_service,
                                                              iso15118::sae::derive_enabled_modes(control), problems));
    };

    if (const auto flavor = der_flavor_for(energy_service)) {
        callbacks.der_enabled_modes = [this, flavor = *flavor](std::uint32_t bitmap) {
            publish_der_negotiated_functions(der_negotiated_functions(flavor, bitmap));
        };
    }

    return callbacks;
}

// This is a thread body: an exception that leaves it calls std::terminate and takes the whole
// manager down, not just the session. Everything the loop does can throw, the publish included.
void ISO15118_evImpl::session_worker() {
    // A throw from the wait itself would otherwise spin this loop at full speed forever, so give
    // up after a few in a row: the module stops serving sessions, the manager keeps running.
    constexpr int MAX_CONSECUTIVE_FAILURES = 5;
    int consecutive_failures = 0;
    // A throw loses only this publication; leaving the iteration would also drop a pending start.
    const auto guarded = [](const char* variable, const auto& publish) {
        try {
            publish();
        } catch (const std::exception& e) {
            EVLOG_error << "Ev15118: publishing " << variable << " failed: " << e.what();
        } catch (...) {
            EVLOG_error << "Ev15118: publishing " << variable << " failed (non-std exception)";
        }
    };
    // Readiness for power ends with the session, before its finish is announced.
    const auto publish_finish = [this, &guarded] {
        guarded("ev_power_ready", [this] { publish_ev_power_ready(false); });
        guarded("v2g_session_finished", [this] { publish_v2g_session_finished(nullptr); });
    };
    while (true) {
        try {
            bool requested = false;
            std::uint32_t finishes = 0;
            {
                auto h = session.handle();
                h.wait([&] {
                    return (*h).phase == SessionPhase::requested || (*h).shutting_down || (*h).finishes_owed > 0;
                });
                if ((*h).shutting_down) {
                    return;
                }
                requested = ((*h).phase == SessionPhase::requested);
                finishes = std::exchange((*h).finishes_owed, 0);
            }
            // A session's own finish follows its phase returning to idle. A debt's finish can go out
            // while a newer start holds the phase at requested, and a start sent in response to it is
            // then rejected. Debts go out before the pending session runs: run_one_session() defers
            // to them.
            for (; finishes > 0; --finishes) {
                publish_finish();
            }
            if (requested and run_one_session()) {
                publish_finish();
            }
            consecutive_failures = 0;
            continue;
        } catch (const std::exception& e) {
            EVLOG_error << "Ev15118: session worker iteration failed: " << e.what();
        } catch (...) {
            EVLOG_error << "Ev15118: session worker iteration failed (non-std exception)";
        }
        recover_worker_state();
        if (++consecutive_failures >= MAX_CONSECUTIVE_FAILURES) {
            EVLOG_error << "Ev15118: session worker failed " << MAX_CONSECUTIVE_FAILURES
                        << " times in a row; stopping it. The module will not serve further sessions.";
            try {
                auto h = session.handle();
                (*h).worker_dead = true;
            } catch (...) {
                // start_charging still rejects: it publishes nothing and the phase stays put.
            }
            return;
        }
    }
}

// Leave the worker able to serve the next request: a throw part way through an iteration can
// leave the phase stuck on requested or running, which the phase guard would reject forever.
void ISO15118_evImpl::recover_worker_state() noexcept {
    try {
        auto h = session.handle();
        (*h).current = nullptr;
        (*h).phase = SessionPhase::idle;
        (*h).finishes_owed = 0;
        (*h).discard_pause = false;
        (*h).paused.reset();
    } catch (...) {
        // The monitor itself is unusable; the next wait() will fail the same way and log again.
    }
}

bool ISO15118_evImpl::run_one_session() {
    try {
        std::optional<iso15118::ev::Controller> controller;
        // Declared after controller so a throw still clears the off-thread pointer before
        // ~Controller runs.
        ScopeGuard clear_current{[this] {
            auto h = session.handle();
            (*h).current = nullptr;
        }};
        // A resume presents the contract its paused session installed instead of the stored one.
        bool resume_installed = false;
        {
            auto h = session.handle();
            resume_installed = (*h).paused and (*h).installed_contract;
        }
        // Per session, so a contract a previous session installed is presented by this one, and a
        // rotated key password is used.
        SessionFiles files{resume_installed ? pnc_material : session_pnc_material(),
                           vehicle_key_password(mod->config, mod->info.paths.etc)};
        {
            auto h = session.handle();
            // A stop in the requested window reset the phase to idle and owes its own finish.
            if ((*h).phase != SessionPhase::requested) {
                return false;
            }
            if ((*h).shutting_down) {
                (*h).phase = SessionPhase::idle;
                return true;
            }
            // A start cancelled after the worker took it is answered before this newer one runs;
            // the worker takes the debt and comes back for the request.
            if ((*h).finishes_owed > 0) {
                return false;
            }
            // The pause changed while the store was read; come back for the request as it is now.
            if (((*h).paused and (*h).installed_contract) != resume_installed) {
                return false;
            }
            // Running before construction: a stop arriving after a construction failure released
            // the lock must not cancel a start the failure path finishes itself.
            (*h).phase = SessionPhase::running;
            if (not(*h).paused) {
                (*h).installed_contract.reset();
            } else if ((*h).installed_contract) {
                files.pnc.contract = (*h).installed_contract;
                EVLOG_info << "Ev15118: this resumed session presents the Plug&Charge contract for eMAID "
                           << (*h).installed_contract->emaid << " its paused session installed";
            }
            // Config, parameters and registration in one lock hold: a cp_state_changed or a
            // parameter update landing in between would otherwise miss this session.
            controller.emplace(make_ev_config(*h, files), make_callbacks((*h).energy_service), (*h).dc_params,
                               (*h).ac_params);
            (*h).current = &controller.value();
            // The CP report is latched, so a session starting after it still gets the state.
            if ((*h).cp_c_or_d) {
                controller->set_cp_state(*(*h).cp_c_or_d);
            }
        }
        controller->loop();
        auto h = session.handle();
        // One hold: a stop or abort reaches current until here, so its discard_pause is seen,
        // and none lands after. paused_session() is empty unless the session ended with
        // SessionStop(Pause); the next session resumes it.
        (*h).paused = (*h).discard_pause ? std::nullopt : controller->paused_session();
        (*h).discard_pause = false;
        (*h).current = nullptr;
        (*h).phase = SessionPhase::idle;
        return true;
    } catch (const std::exception& e) {
        EVLOG_error << "Ev15118: session failed: " << e.what();
    } catch (...) {
        EVLOG_error << "Ev15118: session failed (non-std exception)";
    }
    // A failed session leaves nothing to resume.
    auto h = session.handle();
    (*h).paused.reset();
    (*h).discard_pause = false;
    (*h).phase = SessionPhase::idle;
    return true;
}

bool ISO15118_evImpl::handle_start_charging(types::iso15118::EnergyTransferMode& EnergyTransferMode,
                                            types::iso15118::SelectedPaymentOption& SelectedPaymentOption,
                                            double& DepartureTime, double& EAmount) {
    if (not worker_started.load(std::memory_order_acquire)) {
        EVLOG_error << "Ev15118: the session worker is not running yet; rejecting start_charging";
        return false;
    }

    EVLOG_info << "Ev15118: start_charging requested (DepartureTime " << DepartureTime << " s, EAmount " << EAmount
               << " kWh)";

    // EAmount goes on the wire as float Wh.
    constexpr double max_e_amount_kwh = std::numeric_limits<float>::max() / 1000.0;
    if (not std::isfinite(DepartureTime) or DepartureTime < 0 or not std::isfinite(EAmount) or EAmount < 0 or
        EAmount > max_e_amount_kwh) {
        EVLOG_error << "Ev15118: DepartureTime and EAmount must be finite and not negative; rejecting start_charging";
        return false;
    }

    {
        auto h = session.handle();
        if ((*h).worker_dead) {
            EVLOG_error << "Ev15118: the session worker stopped after repeated failures; rejecting start_charging";
            return false;
        }
    }

    // A device ready() kept as-is may still be missing, and the Controller refuses to construct without it.
    auto interface_name = hlc_device;
    if (not iso15118::ev::resolve_interface(interface_name)) {
        EVLOG_error << "Ev15118: HLC interface '" << hlc_device << "' not found; rejecting start_charging";
        return false;
    }

    // ISO 15118-2 Plug&Charge. enable_pnc prefers Contract whenever the SECC offers it; an
    // explicit ExternalPayment overrides that for this session, and enforcement selects Contract
    // regardless of the offer [V2G2-465].
    const bool contract_requested =
        SelectedPaymentOption.payment_option.has_value() and
        SelectedPaymentOption.payment_option.value() == types::iso15118::PaymentOption::Contract;
    const bool eim_requested =
        SelectedPaymentOption.payment_option.has_value() and
        SelectedPaymentOption.payment_option.value() == types::iso15118::PaymentOption::ExternalPayment;
    const bool enforce_contract = contract_requested and SelectedPaymentOption.enforce_payment_option.value_or(false);
    if (contract_requested and not mod->config.enable_pnc) {
        if (enforce_contract) {
            EVLOG_warning << "Ev15118: Contract (Plug&Charge) enforced without PnC configuration; selecting "
                             "Contract regardless of the SECC offer";
        } else {
            EVLOG_warning << "Ev15118: Contract (Plug&Charge) requested but enable_pnc is false; falling back to EIM";
        }
    }

    const bool pre_20_offered = std::any_of(protocols.begin(), protocols.end(), [](auto protocol) {
        return protocol != iso15118::ProtocolId::ISO15118_20;
    });

    auto energy_service = iso15118::message_20::datatypes::ServiceCategory::DC;
    switch (EnergyTransferMode) {
    case types::iso15118::EnergyTransferMode::DC:
    case types::iso15118::EnergyTransferMode::DC_core:
    case types::iso15118::EnergyTransferMode::DC_extended:
        energy_service = iso15118::message_20::datatypes::ServiceCategory::DC;
        break;
    // The advertised AC power limits are totals across ac_phase_count lines, and the SECC's
    // connector decides how they split, so the single-phase and three-phase modes select the
    // same service.
    case types::iso15118::EnergyTransferMode::AC_single_phase_core:
    case types::iso15118::EnergyTransferMode::AC_three_phase_core:
        energy_service = iso15118::message_20::datatypes::ServiceCategory::AC;
        break;
    case types::iso15118::EnergyTransferMode::AC_BPT:
        energy_service = iso15118::message_20::datatypes::ServiceCategory::AC_BPT;
        break;
    case types::iso15118::EnergyTransferMode::DC_BPT:
        energy_service = iso15118::message_20::datatypes::ServiceCategory::DC_BPT;
        break;
    case types::iso15118::EnergyTransferMode::AC_DER_IEC:
        energy_service = iso15118::message_20::datatypes::ServiceCategory::AC_DER_IEC;
        break;
    case types::iso15118::EnergyTransferMode::AC_DER_SAE:
        energy_service = iso15118::message_20::datatypes::ServiceCategory::AC_DER_SAE;
        break;
    // MCS is the megawatt DC service: same DC parameter discovery, cable check, pre-charge and
    // charge loop, a different service id on the wire. ISO 15118-20 only.
    case types::iso15118::EnergyTransferMode::MCS:
        energy_service = iso15118::message_20::datatypes::ServiceCategory::MCS;
        break;
    case types::iso15118::EnergyTransferMode::MCS_BPT:
        energy_service = iso15118::message_20::datatypes::ServiceCategory::MCS_BPT;
        break;
    // Listed rather than folded into a default arm so -Wswitch flags a new mode.
    case types::iso15118::EnergyTransferMode::AC_two_phase:
    case types::iso15118::EnergyTransferMode::AC_BPT_DER:
    case types::iso15118::EnergyTransferMode::DC_combo_core:
    case types::iso15118::EnergyTransferMode::DC_unique:
    case types::iso15118::EnergyTransferMode::DC_ACDP:
    case types::iso15118::EnergyTransferMode::DC_ACDP_BPT:
    case types::iso15118::EnergyTransferMode::WPT:
        EVLOG_warning << "Ev15118: rejecting start_charging with unsupported EnergyTransferMode '"
                      << types::iso15118::energy_transfer_mode_to_string(EnergyTransferMode)
                      << "'; only DC, DC BPT, MCS, MCS BPT, AC single/three-phase, AC BPT, "
                         "AC DER IEC and AC DER SAE are supported";
        return false;
    }
    {
        auto h = session.handle();
        if ((*h).phase != SessionPhase::idle) {
            EVLOG_warning << "Ev15118: a session is already active; ignoring start_charging";
            return false;
        }
        // Built into a copy and committed only once it validates. Writing into the live state and
        // returning on a problem would leave the rejected request's energy service, transfer mode
        // and any discharge limits behind, and the next accepted start would inherit whichever of
        // them it does not itself overwrite: the discharge limits are written only for BPT and
        // the DER services, so they survive a change of energy service with nothing resetting them.
        auto candidate = *h;
        candidate.energy_service = energy_service;
        candidate.iso2_transfer_mode = to_iso2_transfer_mode(EnergyTransferMode, pre_20_offered);
        candidate.eim_requested = eim_requested;
        candidate.enforce_contract = enforce_contract;
        candidate.energy_request = make_energy_request(DepartureTime, EAmount);
        // Measurements from an earlier session, a DC battery voltage above all, are not this grid's.
        candidate.ac_params.present_voltage.reset();
        candidate.ac_params.present_frequency.reset();
        candidate.ac_params.present_active_power = 0.0f;
        candidate.ac_params.der_alarm_status = 0;
        namespace dt = iso15118::message_20::datatypes;
        if (iso15118::ev::d20::is_ac_family(energy_service)) {
            candidate.ac_params.phase_count = static_cast<uint8_t>(mod->config.ac_phase_count);
            candidate.ac_params.max_charge_power = static_cast<float>(mod->config.ac_max_charge_power_w);
            candidate.ac_params.min_charge_power = static_cast<float>(mod->config.ac_min_charge_power_w);
        }
        // The DER services carry mandatory discharge limits on the wire just as AC_BPT does.
        if (energy_service == dt::ServiceCategory::AC_BPT or energy_service == dt::ServiceCategory::AC_DER_IEC or
            energy_service == dt::ServiceCategory::AC_DER_SAE) {
            candidate.ac_params.max_discharge_power = static_cast<float>(mod->config.ac_max_discharge_power_w);
            candidate.ac_params.min_discharge_power = static_cast<float>(mod->config.ac_min_discharge_power_w);
        }
        // The configured totals span three lines; a single-phase session asks for one and its share.
        if (EnergyTransferMode == types::iso15118::EnergyTransferMode::AC_single_phase_core and
            candidate.ac_params.phase_count == 3) {
            EVLOG_info << "Ev15118: AC_single_phase_core with ac_phase_count 3; this session asks for one phase "
                          "and advertises a third of the configured charge power";
            candidate.ac_params.phase_count = 1;
            candidate.ac_params.max_charge_power /= 3.0f;
            candidate.ac_params.min_charge_power /= 3.0f;
        }
        if (energy_service == dt::ServiceCategory::DC_BPT or energy_service == dt::ServiceCategory::MCS_BPT) {
            // set_bpt_dc_params discharge limits win; config settings are the fallback
            candidate.dc_params.max_discharge_power =
                candidate.cmd_max_discharge_power.value_or(static_cast<float>(mod->config.dc_max_discharge_power_w));
            candidate.dc_params.min_discharge_power = static_cast<float>(mod->config.dc_min_discharge_power_w);
            candidate.dc_params.max_discharge_current = candidate.cmd_max_discharge_current.value_or(
                static_cast<float>(mod->config.dc_max_discharge_current_a));
        }
        // Validate what actually goes on the wire: the merged params, not the raw config.
        // The session's files are read when it runs; nothing validated here depends on them.
        auto problems = iso15118::ev::config::validate_config(make_ev_config(candidate, SessionFiles{}));
        const auto append = [&problems](std::vector<std::string> more) {
            problems.insert(problems.end(), std::make_move_iterator(more.begin()), std::make_move_iterator(more.end()));
        };
        if (iso15118::ev::d20::is_ac_family(energy_service)) {
            append(iso15118::ev::config::validate_ac_charge_params(candidate.ac_params));
        }
        if (iso15118::ev::d20::is_dc_family(energy_service)) {
            append(iso15118::ev::config::validate_dc_charge_params(candidate.dc_params));
        }
        if (not problems.empty()) {
            for (const auto& problem : problems) {
                EVLOG_error << "Ev15118: invalid session parameter: " << problem;
            }
            EVLOG_error << "Ev15118: rejecting start_charging; the session parameters are invalid";
            return false;
        }
        candidate.phase = SessionPhase::requested;
        *h = std::move(candidate);
    }
    session.notify_all();
    return true;
}

bool ISO15118_evImpl::end_session_locked(SessionState& state, ControllerAction action, bool drop_paused) {
    bool cancelled = false;
    if (state.current) {
        (state.current->*action)();
    } else if (state.phase == SessionPhase::requested) {
        // the request arrived before the worker constructed the controller; cancel the
        // pending session so run_one_session() skips it under the lock
        state.phase = SessionPhase::idle;
        // The worker still publishes v2g_session_finished for this start_charging.
        ++state.finishes_owed;
        cancelled = true;
    }
    // Independent of whether a controller is running: a stop or an abort discards the stored
    // pause, or the next start_charging silently resumes a session the driver ended.
    if (drop_paused) {
        state.paused.reset();
        // The running session's loop may already have ended with a pause the request can no
        // longer prevent; run_one_session() drops it when it takes the result.
        if (state.current) {
            state.discard_pause = true;
        }
    }
    return cancelled;
}

void ISO15118_evImpl::end_session(ControllerAction action, bool drop_paused) {
    bool cancelled = false;
    {
        auto h = session.handle();
        cancelled = end_session_locked(*h, action, drop_paused);
    }
    if (cancelled) {
        session.notify_all();
    }
}

void ISO15118_evImpl::handle_stop_charging() {
    end_session(&iso15118::ev::Controller::request_stop, true);
}

void ISO15118_evImpl::handle_pause_charging() {
    end_session(&iso15118::ev::Controller::request_pause, false);
}

void ISO15118_evImpl::handle_abort_charging() {
    end_session(&iso15118::ev::Controller::terminate, true);
}

void ISO15118_evImpl::handle_cp_state_changed(types::iso15118::CpState& cp_state) {
    using types::iso15118::CpState;
    const bool c_or_d = (cp_state == CpState::C) or (cp_state == CpState::D);
    bool cancelled = false;
    {
        auto h = session.handle();
        // Latched: the report may arrive before a controller exists, and reporting it at all is
        // what enables the CP-dependent checks (EvConfig::has_cp_state_feedback).
        (*h).cp_c_or_d = c_or_d;
        if ((*h).current) {
            (*h).current->set_cp_state(c_or_d);
        }
        // [V2G2-737]: E or F ends the session without SessionStop ([V2G2-728]), and cancels one that
        // was accepted but whose Controller is not built yet. Under the same hold as the phase read,
        // so a session started after it is not the one aborted.
        if ((*h).phase != SessionPhase::idle and (cp_state == CpState::E or cp_state == CpState::F)) {
            cancelled = end_session_locked(*h, &iso15118::ev::Controller::terminate, true);
        }
    }
    if (cancelled) {
        session.notify_all();
    }
}

void ISO15118_evImpl::handle_set_fault() {
    EVLOG_warning << "Ev15118: set_fault is not supported";
}

void ISO15118_evImpl::handle_set_dc_params(types::iso15118::DcEvParameters& EvParameters) {
    EVLOG_info << "Ev15118: set_dc_params";

    std::string missing;
    const auto note_missing = [&missing](const char* name, bool present) {
        if (not present) {
            missing.append(missing.empty() ? "" : ", ").append(name);
        }
    };
    note_missing("max_power_limit", EvParameters.max_power_limit.has_value());
    note_missing("max_current_limit", EvParameters.max_current_limit.has_value());
    note_missing("max_voltage_limit", EvParameters.max_voltage_limit.has_value());
    note_missing("min_voltage_limit", EvParameters.min_voltage_limit.has_value());
    note_missing("energy_capacity", EvParameters.energy_capacity.has_value());
    note_missing("target_voltage", EvParameters.target_voltage.has_value());
    note_missing("target_current", EvParameters.target_current.has_value());
    if (not missing.empty()) {
        EVLOG_warning << "Ev15118: set_dc_params missing " << missing << "; keeping the previous values";
    }

    auto h = session.handle();
    // Merged into a copy and stored only once it validates: a rejected update changes nothing.
    auto params = (*h).dc_params;
    const auto merge = [](float& field, const auto& value) {
        if (value) {
            field = static_cast<float>(*value);
        }
    };
    merge(params.max_charge_power, EvParameters.max_power_limit);
    merge(params.max_charge_current, EvParameters.max_current_limit);
    merge(params.max_voltage, EvParameters.max_voltage_limit);
    merge(params.min_voltage, EvParameters.min_voltage_limit);
    merge(params.energy_capacity, EvParameters.energy_capacity);
    merge(params.target_voltage, EvParameters.target_voltage);
    merge(params.target_current, EvParameters.target_current);
    const auto problems = iso15118::ev::config::validate_dc_charge_params(params);
    if (not problems.empty()) {
        for (const auto& problem : problems) {
            EVLOG_error << "Ev15118: invalid set_dc_params: " << problem;
        }
        EVLOG_error << "Ev15118: rejecting set_dc_params; keeping the previous values";
        return;
    }
    (*h).dc_params = params;
    if ((*h).current) {
        // The targets steer the running Scheduled-mode charge loop.
        (*h).current->update_dc_params(params);
    }
}

void ISO15118_evImpl::handle_set_bpt_dc_params(types::iso15118::DcEvBPTParameters& EvBPTParameters) {
    EVLOG_info << "Ev15118: set_bpt_dc_params";

    std::string missing;
    const auto note_missing = [&missing](const char* name, bool present) {
        if (not present) {
            missing.append(missing.empty() ? "" : ", ").append(name);
        }
    };
    note_missing("discharge_max_power_limit", EvBPTParameters.discharge_max_power_limit.has_value());
    note_missing("discharge_max_current_limit", EvBPTParameters.discharge_max_current_limit.has_value());
    if (not missing.empty()) {
        EVLOG_warning << "Ev15118: set_bpt_dc_params missing " << missing << "; keeping the previous discharge limits";
    }

    // discharge_target_current / discharge_minimal_soc are not consumed by the -20
    // Dynamic BPT request (reverse power flow is driven by SECC targets); log only
    if (EvBPTParameters.discharge_target_current) {
        EVLOG_debug << "Ev15118: ignoring discharge_target_current " << *EvBPTParameters.discharge_target_current
                    << " A (SECC-target-driven)";
    }
    if (EvBPTParameters.discharge_minimal_soc) {
        EVLOG_debug << "Ev15118: ignoring discharge_minimal_soc " << *EvBPTParameters.discharge_minimal_soc
                    << " % (SECC-target-driven)";
    }

    // Command values override the config settings seeded at start_charging, for the active BPT
    // session and every later one. Merged into a copy and stored only once it validates, with or
    // without a session: a rejected update changes nothing.
    auto h = session.handle();
    auto cmd_power = (*h).cmd_max_discharge_power;
    auto cmd_current = (*h).cmd_max_discharge_current;
    if (EvBPTParameters.discharge_max_power_limit) {
        cmd_power = static_cast<float>(*EvBPTParameters.discharge_max_power_limit);
    }
    if (EvBPTParameters.discharge_max_current_limit) {
        cmd_current = static_cast<float>(*EvBPTParameters.discharge_max_current_limit);
    }
    namespace dt = iso15118::message_20::datatypes;
    const auto service = (*h).energy_service;
    const bool bpt_active = (*h).phase != SessionPhase::idle and
                            (service == dt::ServiceCategory::DC_BPT or service == dt::ServiceCategory::MCS_BPT);
    auto params = (*h).dc_params;
    std::vector<std::string> problems;
    if (bpt_active) {
        params.max_discharge_power = cmd_power.value_or(params.max_discharge_power);
        params.max_discharge_current = cmd_current.value_or(params.max_discharge_current);
        problems = iso15118::ev::config::validate_dc_charge_params(params);
    } else {
        // Only the limits the command carries: the charge limits may not be set yet, and
        // start_charging validates the whole set.
        const auto check = [&problems](const char* name, const std::optional<float>& value) {
            if (value and not(std::isfinite(*value) and *value >= 0.0f)) {
                problems.push_back(std::string(name) + " must be finite and not negative, got " +
                                   std::to_string(*value));
            }
        };
        const std::optional<float> power = EvBPTParameters.discharge_max_power_limit ? cmd_power : std::nullopt;
        const std::optional<float> current = EvBPTParameters.discharge_max_current_limit ? cmd_current : std::nullopt;
        check("discharge_max_power_limit", power);
        check("discharge_max_current_limit", current);
        const auto min_power = static_cast<float>(mod->config.dc_min_discharge_power_w);
        if (power and *power < min_power) {
            problems.push_back("discharge_max_power_limit " + std::to_string(*power) +
                               " is below dc_min_discharge_power_w " + std::to_string(min_power));
        }
    }
    if (not problems.empty()) {
        for (const auto& problem : problems) {
            EVLOG_error << "Ev15118: invalid set_bpt_dc_params: " << problem;
        }
        EVLOG_error << "Ev15118: rejecting set_bpt_dc_params; keeping the previous discharge limits";
        return;
    }
    if (bpt_active) {
        (*h).dc_params = params;
        // A requested session's Controller is built from dc_params, so it picks the update up.
        if ((*h).current) {
            (*h).current->update_dc_params(params);
        }
    }
    (*h).cmd_max_discharge_power = cmd_power;
    (*h).cmd_max_discharge_current = cmd_current;
}

void ISO15118_evImpl::handle_enable_sae_j2847_v2g_v2h() {
    EVLOG_info << "Ev15118: enable_sae_j2847_v2g_v2h is not supported";
}

void ISO15118_evImpl::handle_update_soc(double& SoC) {
    auto h = session.handle();
    (*h).dc_params.present_soc = SoC;
    if ((*h).current) {
        (*h).current->update_present_soc(SoC);
    }
}

void ISO15118_evImpl::handle_update_present_values(types::iso15118::EvPresentValues& PresentValues) {
    // Stored on session params (seeds the next session) as well as pushed to the live
    // Controller. Absent fields keep their last value rather than reset to 0, which
    // would read as a real measurement.
    auto h = session.handle();
    if (PresentValues.present_voltage.has_value()) {
        const auto voltage = PresentValues.present_voltage.value();
        (*h).dc_params.present_voltage = voltage;
        (*h).ac_params.present_voltage = voltage;
        if ((*h).current) {
            (*h).current->update_present_voltage(voltage);
        }
    }
    if (PresentValues.present_active_power.has_value()) {
        const auto power = PresentValues.present_active_power.value();
        (*h).ac_params.present_active_power = power;
        if ((*h).current) {
            (*h).current->update_present_active_power(power);
        }
    }
    if (PresentValues.present_frequency.has_value()) {
        const auto frequency = PresentValues.present_frequency.value();
        (*h).ac_params.present_frequency = frequency;
        if ((*h).current) {
            (*h).current->update_present_frequency(frequency);
        }
    }
    if (PresentValues.der_alarm_status.has_value()) {
        const auto alarm_status = PresentValues.der_alarm_status.value();
        // Command arguments are schema-checked only when the manager validates, so the minimum may not hold.
        if (alarm_status < 0) {
            EVLOG_warning << "Ev15118: ignoring negative der_alarm_status " << alarm_status;
        } else {
            (*h).ac_params.der_alarm_status = static_cast<std::uint32_t>(alarm_status);
            if ((*h).current) {
                (*h).current->update_der_alarm_status(static_cast<std::uint32_t>(alarm_status));
            }
        }
    }
}

} // namespace ev
} // namespace module
