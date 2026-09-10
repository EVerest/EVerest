// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "ISO15118_evImpl.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <iso15118/d20/ac_powers.hpp>
#include <iso15118/detail/io/socket_helper.hpp>
#include <iso15118/ev/config_validation.hpp>
#include <iso15118/ev/detail/d2/crypto.hpp>
#include <iso15118/ev/service_family.hpp>
#include <iso15118/io/logging.hpp>
#include <iso15118/message/type.hpp>
#include <iso15118/message/v2g_message_type.hpp>
#include <iso15118/session/protocol.hpp>

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
        warn_unidirectional();
        return Out::AC_three_phase_core;
    case In::DC_core:
        return Out::DC_core;
    case In::DC_combo_core:
        return Out::DC_combo_core;
    case In::DC_unique:
        return Out::DC_unique;
    case In::DC_BPT:
        warn_unidirectional();
        return Out::DC_extended;
    case In::DC:
    case In::DC_extended:
    // Rejected by start_charging before this runs; listed rather than folded into a default
    // arm so -Wswitch flags a new mode.
    case In::AC_two_phase:
    case In::AC_BPT_DER:
    case In::AC_DER_SAE:
    case In::DC_ACDP:
    case In::DC_ACDP_BPT:
    case In::WPT:
    case In::MCS:
    case In::MCS_BPT:
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
    namespace crypto = iso15118::ev::d2::crypto;
    const auto certs = etc_path / "certs";
    const auto resolve = [](const std::string& configured, const std::filesystem::path& fallback) {
        return configured.empty() ? fallback : std::filesystem::path(configured);
    };

    iso15118::ev::d2::PnCConfig pnc;
    pnc.force_cert_install = config.pnc_force_cert_install;
    pnc.v2g_root_path = resolve(config.v2g_root_cert_path, certs / "ca/v2g/V2G_ROOT_CA.pem").string();

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
    const auto chain_der = crypto::pem_chain_to_der(contract_chain_pem);
    if (not chain_der.empty()) {
        pnc.contract_cert_der = chain_der.front();
        pnc.contract_sub_certs_der.assign(chain_der.begin() + 1, chain_der.end());
        pnc.contract_emaid = crypto::emaid_from_contract_der(pnc.contract_cert_der);
        pnc.contract_key_pem = read_file_text(resolve(config.pnc_contract_key_path, certs / "client/mo/MO_LEAF.key"));
        auto password =
            read_file_trimmed(resolve(config.pnc_contract_key_password_path, certs / "client/mo/MO_LEAF_PASSWORD.txt"));
        if (not password.empty()) {
            pnc.contract_key_password = std::move(password);
        }
    } else {
        EVLOG_warning << "Ev15118: PnC enabled but no contract certificate chain found; a "
                         "CertificateInstallation will be required to obtain one";
    }

    // OEM provisioning certificate and key, only used when a CertificateInstallation runs.
    const auto oem_der = crypto::pem_chain_to_der(
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

    // ListOfRootCertificateIDs of the CertificateInstallationReq.
    const auto root_der = crypto::pem_chain_to_der(read_file_text(pnc.v2g_root_path));
    if (not root_der.empty()) {
        const auto id = crypto::root_cert_id_from_der(root_der.front());
        if (not id.issuer_name.empty()) {
            pnc.root_certificate_ids.push_back(id);
        }
    }

    if (pnc.needs_cert_install()) {
        if (pnc.oem_prov_cert_der.empty()) {
            EVLOG_warning << "Ev15118: a CertificateInstallation is required but no OEM provisioning certificate "
                             "was loaded; it will fail";
        }
        if (pnc.root_certificate_ids.empty()) {
            EVLOG_warning << "Ev15118: a CertificateInstallation is required but no root certificate id could be "
                             "read from "
                          << pnc.v2g_root_path << "; it will fail";
        }
    }

    return pnc;
}

const char* signal_to_string(iso15118::ev::feedback::Signal signal) {
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

constexpr types::iso15118::V2gMessageId message_20_type_to_id(iso15118::message_20::Type type) {
    using Type = iso15118::message_20::Type;
    using Id = types::iso15118::V2gMessageId;

    switch (type) {
    case Type::None:
        return Id::UnknownMessage;
    case Type::SupportedAppProtocolReq:
        return Id::SupportedAppProtocolReq;
    case Type::SupportedAppProtocolRes:
        return Id::SupportedAppProtocolRes;
    case Type::SessionSetupReq:
        return Id::SessionSetupReq;
    case Type::SessionSetupRes:
        return Id::SessionSetupRes;
    case Type::AuthorizationSetupReq:
        return Id::AuthorizationSetupReq;
    case Type::AuthorizationSetupRes:
        return Id::AuthorizationSetupRes;
    case Type::AuthorizationReq:
        return Id::AuthorizationReq;
    case Type::AuthorizationRes:
        return Id::AuthorizationRes;
    case Type::ServiceDiscoveryReq:
        return Id::ServiceDiscoveryReq;
    case Type::ServiceDiscoveryRes:
        return Id::ServiceDiscoveryRes;
    case Type::ServiceDetailReq:
        return Id::ServiceDetailReq;
    case Type::ServiceDetailRes:
        return Id::ServiceDetailRes;
    case Type::ServiceSelectionReq:
        return Id::ServiceSelectionReq;
    case Type::ServiceSelectionRes:
        return Id::ServiceSelectionRes;
    case Type::DC_ChargeParameterDiscoveryReq:
        return Id::DcChargeParameterDiscoveryReq;
    case Type::DC_ChargeParameterDiscoveryRes:
        return Id::DcChargeParameterDiscoveryRes;
    case Type::ScheduleExchangeReq:
        return Id::ScheduleExchangeReq;
    case Type::ScheduleExchangeRes:
        return Id::ScheduleExchangeRes;
    case Type::DC_CableCheckReq:
        return Id::DcCableCheckReq;
    case Type::DC_CableCheckRes:
        return Id::DcCableCheckRes;
    case Type::DC_PreChargeReq:
        return Id::DcPreChargeReq;
    case Type::DC_PreChargeRes:
        return Id::DcPreChargeRes;
    case Type::PowerDeliveryReq:
        return Id::PowerDeliveryReq;
    case Type::PowerDeliveryRes:
        return Id::PowerDeliveryRes;
    case Type::DC_ChargeLoopReq:
        return Id::DcChargeLoopReq;
    case Type::DC_ChargeLoopRes:
        return Id::DcChargeLoopRes;
    case Type::DC_WeldingDetectionReq:
        return Id::DcWeldingDetectionReq;
    case Type::DC_WeldingDetectionRes:
        return Id::DcWeldingDetectionRes;
    case Type::SessionStopReq:
        return Id::SessionStopReq;
    case Type::SessionStopRes:
        return Id::SessionStopRes;
    case Type::AC_ChargeParameterDiscoveryReq:
        return Id::AcChargeParameterDiscoveryReq;
    case Type::AC_ChargeParameterDiscoveryRes:
        return Id::AcChargeParameterDiscoveryRes;
    case Type::AC_ChargeLoopReq:
        return Id::AcChargeLoopReq;
    case Type::AC_ChargeLoopRes:
        return Id::AcChargeLoopRes;
    case Type::DER_AC_ChargeParameterDiscoveryReq:
        return Id::AcDerChargeParameterDiscoveryReq;
    case Type::DER_AC_ChargeParameterDiscoveryRes:
        return Id::AcDerChargeParameterDiscoveryRes;
    case Type::DER_AC_ChargeLoopReq:
        return Id::AcDerChargeLoopReq;
    case Type::DER_AC_ChargeLoopRes:
        return Id::AcDerChargeLoopRes;
    case Type::DER_SAE_AC_ChargeParameterDiscoveryReq:
        return Id::AcDerSaeChargeParameterDiscoveryReq;
    case Type::DER_SAE_AC_ChargeParameterDiscoveryRes:
        return Id::AcDerSaeChargeParameterDiscoveryRes;
    case Type::DER_SAE_AC_ChargeLoopReq:
        return Id::AcDerSaeChargeLoopReq;
    case Type::DER_SAE_AC_ChargeLoopRes:
        return Id::AcDerSaeChargeLoopRes;
    }
    return Id::UnknownMessage;
}

// ISO 15118-2 and DIN 70121 have no V2gMessageId mapping here yet.
types::iso15118::V2gMessageId v2g_message_id(const iso15118::V2gMessageType& type) {
    if (const auto* type_20 = std::get_if<iso15118::message_20::Type>(&type)) {
        return message_20_type_to_id(*type_20);
    }
    return types::iso15118::V2gMessageId::UnknownMessage;
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
}

void ISO15118_evImpl::ready() {
    // Resolve the HLC interface once ("auto" -> first usable ipv6 interface) so the connection and
    // the EVCCID MAC use the same device.
    hlc_device = mod->config.device;
    if (not iso15118::io::check_and_update_interface(hlc_device)) {
        if (mod->config.device == "auto") {
            // "auto" has no usable as-is value; the Controller would only fail to resolve it again.
            EVLOG_error << "Ev15118: could not resolve HLC interface 'auto' to a usable ipv6 interface; "
                           "not starting the EVCC";
            return;
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

    // -2 mandates TLS 1.2 [V2G2-602]; a 1.3-only client hello is rejected by a -2 SECC.
    if (mod->config.supported_ISO15118_2 and mod->config.enable_tls_1_3 and
        (mod->config.tls_active or mod->config.enforce_tls)) {
        EVLOG_warning << "Ev15118: supported_ISO15118_2 with enable_tls_1_3 and TLS: ISO 15118-2 requires TLS 1.2, "
                         "the handshake will fail for -2 sessions";
    }

    // Static material, read once: file IO must not run under the session monitor.
    if (mod->config.enable_pnc) {
        pnc_material = build_pnc_config(mod->config, mod->info.paths.etc);
    }

    worker = std::thread([this] { session_worker(); });
    worker_started = true;
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

iso15118::ev::EvConfig ISO15118_evImpl::make_ev_config(const SessionState& state) const {
    iso15118::ev::EvConfig ev_config;

    // Resolved in ready(); empty only when ready() bailed out before starting the worker.
    ev_config.interface_name = hlc_device;
    ev_config.evcc_id = mod->config.evcc_id;
    ev_config.response_timeout = std::chrono::milliseconds(mod->config.response_timeout_ms);

    // Priority order in the SAP offer.
    ev_config.supported_protocols = {iso15118::ProtocolId::ISO15118_20};
    if (mod->config.supported_ISO15118_2) {
        ev_config.supported_protocols.push_back(iso15118::ProtocolId::ISO15118_2);
    }
    if (mod->config.supported_DIN70121) {
        ev_config.supported_protocols.push_back(iso15118::ProtocolId::DIN70121);
    }

    auto& tls = ev_config.tls;
    // enforce_tls implies a TLS connection regardless of tls_active.
    tls.use_tls = mod->config.tls_active or mod->config.enforce_tls;
    tls.enforce_tls = mod->config.enforce_tls;
    tls.enable_tls_1_3 = mod->config.enable_tls_1_3;
    tls.verify_server_certificate = mod->config.verify_server_certificate;
    tls.enable_key_logging = mod->config.enable_tls_key_logging;
    tls.key_logging_path = mod->config.tls_key_logging_path;
    const auto certs = std::filesystem::path(mod->info.paths.etc) / "certs";
    tls.v2g_root_cert_path = resolve_path(mod->config.v2g_root_cert_path, certs / "ca/v2g/V2G_ROOT_CA.pem");
    tls.client_cert_chain_path =
        resolve_path(mod->config.device_cert_chain_path, certs / "client/vehicle/VEHICLE_CERT_CHAIN.pem");
    tls.client_key_path = resolve_path(mod->config.device_key_path, certs / "client/vehicle/VEHICLE_LEAF.key");
    tls.client_key_password = read_file_trimmed(
        resolve_path(mod->config.device_key_password_path, certs / "client/vehicle/VEHICLE_LEAF_PASSWORD.txt"));

    namespace dt = iso15118::message_20::datatypes;

    // ISO 15118-2 / DIN SPEC 70121 session parameters.
    auto& params = ev_config.params;
    if (evcc_mac) {
        params.evcc_mac = *evcc_mac;
    }
    params.energy_transfer_mode = state.iso2_transfer_mode;
    params.ac.e_amount = static_cast<float>(mod->config.iso2_ac_e_amount_wh);
    params.ac.ev_max_voltage = static_cast<float>(mod->config.iso2_ac_ev_max_voltage_v);
    params.ac.ev_max_current = static_cast<float>(mod->config.iso2_ac_ev_max_current_a);
    params.ac.ev_min_current = static_cast<float>(mod->config.iso2_ac_ev_min_current_a);

    // PnC requires TLS [V2G2-632]; the -2 engine falls back to EIM whenever the SECC does not
    // offer Contract. enable_pnc alone prefers Contract, because EvManager's default payment
    // option is `auto`; an explicit ExternalPayment opts this one session out.
    if (mod->config.enable_pnc and tls.use_tls) {
        ev_config.supported_auth_options = {dt::Authorization::PnC, dt::Authorization::EIM};
        params.pnc = pnc_material;
        params.pnc.prefer_contract = not state.eim_requested;
    } else {
        if (mod->config.enable_pnc and not pnc_without_tls_warned) {
            EVLOG_warning << "Ev15118: enable_pnc is set but TLS is not active; Plug&Charge requires TLS, "
                             "falling back to EIM only";
            pnc_without_tls_warned = true;
        }
        ev_config.supported_auth_options = {dt::Authorization::EIM};
        params.pnc.prefer_contract = false;
    }
    // Select Contract even without an offer or PnC material (negative testing).
    params.pnc.enforce_contract = state.enforce_contract;

    const auto energy_service = state.energy_service;
    ev_config.energy_service = energy_service;
    ev_config.control_mode =
        (mod->config.d20_control_mode == "Scheduled") ? dt::ControlMode::Scheduled : dt::ControlMode::Dynamic;
    ev_config.resume = state.paused;
    ev_config.has_cp_state_feedback = state.cp_c_or_d.has_value();

    if (energy_service == dt::ServiceCategory::AC_DER_IEC) {
        auto& functions = ev_config.der_control_functions;
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
        ev_config.der_stop_on_unsupported_functions = mod->config.der_stop_on_unsupported_functions;
    }

    return ev_config;
}

iso15118::ev::feedback::Callbacks ISO15118_evImpl::make_callbacks() {
    iso15118::ev::feedback::Callbacks callbacks;

    callbacks.connected = [](const iso15118::io::Ipv6EndPoint&) { EVLOG_info << "Ev15118: connected to SECC"; };

    callbacks.v2g_message = [this](const iso15118::V2gMessageType& type) {
        publish_v2g_messages(types::iso15118::V2gMessages{.id = v2g_message_id(type)});
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
                    << limits.power << " W";
    };

    callbacks.evse_session_info = [](const iso15118::ev::d20::EVSESessionInfo&) {
        EVLOG_debug << "Ev15118: EVSE session info received";
    };

    // Persisting the installed contract (e.g. to EvseSecurity) is future work; the session
    // continues with the in-memory contract.
    callbacks.pnc_contract_installed = [](const std::string& chain_pem, const std::string&, const std::string& emaid) {
        EVLOG_info << "Ev15118: Plug&Charge contract installed for eMAID " << emaid << " (" << chain_pem.size()
                   << " bytes of chain PEM)";
    };

    callbacks.pause_from_charger = [this] { publish_pause_from_charger(nullptr); };

    callbacks.timed_out = [] { EVLOG_warning << "Ev15118: response watchdog timed out"; };

    callbacks.stopped = [] { EVLOG_info << "Ev15118: session stopped"; };

    callbacks.ev_power_ready = [this] { publish_ev_power_ready(true); };

    callbacks.dc_power_on = [this] { publish_dc_power_on(nullptr); };

    callbacks.stop_from_charger = [this] { publish_stop_from_charger(nullptr); };

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

    callbacks.ac_target_power = [this](const iso15118::d20::AcTargetPower& control) {
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

    // ISO15118_ev has no DER variable; log the directive rather than publish it
    callbacks.der_control = [](const iso15118::message_20::datatypes::DER_Dynamic_AC_CLResControlMode& control) {
        namespace dt = iso15118::message_20::datatypes;
        std::ostringstream line;
        line << "Ev15118: DER directive: target active power " << dt::from_RationalNumber(control.target_active_power)
             << " W";
        if (control.dso_q_setpoint) {
            line << ", DSO Q setpoint " << dt::from_RationalNumber(control.dso_q_setpoint->dso_q_setpoint_value)
                 << " var";
        }
        if (control.dso_cos_phi_setpoint) {
            line << ", DSO cos phi setpoint "
                 << dt::from_RationalNumber(control.dso_cos_phi_setpoint->dso_cos_phi_setpoint_value);
        }
        EVLOG_info << line.str();
    };

    // Dictated DER curves are observed, not applied
    callbacks.der_curves = [](const iso15118::message_20::datatypes::DerControl& control) {
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
    };

    return callbacks;
}

void ISO15118_evImpl::session_worker() {
    while (true) {
        bool requested = false;
        {
            auto h = session.handle();
            h.wait([&] { return (*h).phase == SessionPhase::requested || (*h).shutting_down || (*h).finish_pending; });
            if ((*h).shutting_down) {
                return;
            }
            requested = ((*h).phase == SessionPhase::requested);
            // A cancelled request still owes its finish report; the publish below is it.
            (*h).finish_pending = false;
        }
        if (requested) {
            run_one_session();
            auto h = session.handle();
            // A cancel that landed during the session is answered by the same publish.
            (*h).finish_pending = false;
        }
        // Published after phase resets to idle, so a consumer starting a new session
        // in response isn't rejected by the phase guard.
        publish_v2g_session_finished(nullptr);
    }
}

void ISO15118_evImpl::run_one_session() {
    try {
        std::optional<iso15118::ev::Controller> controller;
        // Declared after controller so it clears the off-thread pointer before ~Controller runs.
        ScopeGuard clear_current{[this] {
            auto h = session.handle();
            (*h).current = nullptr;
        }};
        {
            auto h = session.handle();
            // teardown or a stop in the requested window (phase reset to idle) beat us here
            if ((*h).shutting_down || (*h).phase != SessionPhase::requested) {
                return;
            }
            // Config, parameters and registration in one lock hold: a cp_state_changed or a
            // parameter update landing in between would otherwise miss this session.
            controller.emplace(make_ev_config(*h), make_callbacks(), (*h).dc_params, (*h).ac_params);
            (*h).current = &controller.value();
            (*h).phase = SessionPhase::running;
            // The CP report is latched, so a session starting after it still gets the state.
            if ((*h).cp_c_or_d) {
                controller->set_cp_state(*(*h).cp_c_or_d);
            }
        }
        controller->loop();
        {
            auto h = session.handle();
            // Empty unless the session ended with SessionStop(Pause); the next session resumes it.
            (*h).paused = controller->paused_session();
        }
    } catch (const std::exception& e) {
        EVLOG_error << "Ev15118: session failed: " << e.what();
        // A failed session leaves nothing to resume.
        auto h = session.handle();
        (*h).paused.reset();
    }
    auto h = session.handle();
    (*h).phase = SessionPhase::idle;
}

bool ISO15118_evImpl::handle_start_charging(types::iso15118::EnergyTransferMode& EnergyTransferMode,
                                            types::iso15118::SelectedPaymentOption& SelectedPaymentOption,
                                            double& DepartureTime, double& EAmount) {
    EVLOG_info << "Ev15118: start_charging requested (DepartureTime and EAmount ignored)";

    if (not worker_started) {
        EVLOG_error << "Ev15118: the session worker was never started (HLC interface resolution failed); "
                       "rejecting start_charging";
        return false;
    }

    // ISO 15118-2 Plug&Charge. enable_pnc prefers Contract whenever the SECC offers it; an
    // explicit ExternalPayment overrides that for this session, and enforcement selects Contract
    // regardless of the offer [V2G2-135].
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

    const bool pre_20_offered = mod->config.supported_ISO15118_2 or mod->config.supported_DIN70121;

    auto energy_service = iso15118::message_20::datatypes::ServiceCategory::DC;
    switch (EnergyTransferMode) {
    case types::iso15118::EnergyTransferMode::DC:
    case types::iso15118::EnergyTransferMode::DC_core:
    case types::iso15118::EnergyTransferMode::DC_extended:
        energy_service = iso15118::message_20::datatypes::ServiceCategory::DC;
        break;
    // The advertised AC power limits are three-phase totals either way, so the
    // single-phase and three-phase modes select the same service.
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
    // Listed rather than folded into a default arm so -Wswitch flags a new mode.
    case types::iso15118::EnergyTransferMode::AC_two_phase:
    case types::iso15118::EnergyTransferMode::AC_BPT_DER:
    case types::iso15118::EnergyTransferMode::AC_DER_SAE:
    case types::iso15118::EnergyTransferMode::DC_combo_core:
    case types::iso15118::EnergyTransferMode::DC_unique:
    case types::iso15118::EnergyTransferMode::DC_ACDP:
    case types::iso15118::EnergyTransferMode::DC_ACDP_BPT:
    case types::iso15118::EnergyTransferMode::WPT:
    case types::iso15118::EnergyTransferMode::MCS:
    case types::iso15118::EnergyTransferMode::MCS_BPT:
        EVLOG_warning << "Ev15118: rejecting start_charging with unsupported EnergyTransferMode '"
                      << types::iso15118::energy_transfer_mode_to_string(EnergyTransferMode)
                      << "'; only DC, DC BPT, AC single/three-phase, AC BPT and AC DER IEC are supported";
        return false;
    }
    {
        auto h = session.handle();
        if ((*h).phase != SessionPhase::idle) {
            EVLOG_warning << "Ev15118: a session is already active; ignoring start_charging";
            return false;
        }
        (*h).energy_service = energy_service;
        (*h).iso2_transfer_mode = to_iso2_transfer_mode(EnergyTransferMode, pre_20_offered);
        (*h).eim_requested = eim_requested;
        (*h).enforce_contract = enforce_contract;
        namespace dt = iso15118::message_20::datatypes;
        if (iso15118::ev::is_ac_family(energy_service)) {
            (*h).ac_params.phase_count = static_cast<uint8_t>(mod->config.ac_phase_count);
            (*h).ac_params.max_charge_power = static_cast<float>(mod->config.ac_max_charge_power_w);
            (*h).ac_params.min_charge_power = static_cast<float>(mod->config.ac_min_charge_power_w);
        }
        // AC_DER_IEC carries mandatory discharge limits on the wire just as AC_BPT does.
        if (energy_service == dt::ServiceCategory::AC_BPT or energy_service == dt::ServiceCategory::AC_DER_IEC) {
            (*h).ac_params.max_discharge_power = static_cast<float>(mod->config.ac_max_discharge_power_w);
            (*h).ac_params.min_discharge_power = static_cast<float>(mod->config.ac_min_discharge_power_w);
        }
        if (energy_service == dt::ServiceCategory::DC_BPT) {
            // set_bpt_dc_params discharge limits win; config settings are the fallback
            (*h).dc_params.max_discharge_power =
                (*h).cmd_max_discharge_power.value_or(static_cast<float>(mod->config.dc_max_discharge_power_w));
            (*h).dc_params.min_discharge_power = static_cast<float>(mod->config.dc_min_discharge_power_w);
            (*h).dc_params.max_discharge_current =
                (*h).cmd_max_discharge_current.value_or(static_cast<float>(mod->config.dc_max_discharge_current_a));
        }
        // Validate what actually goes on the wire: the merged params, not the raw config.
        auto problems = iso15118::ev::validate_config(make_ev_config(*h));
        const auto append = [&problems](std::vector<std::string> more) {
            problems.insert(problems.end(), std::make_move_iterator(more.begin()), std::make_move_iterator(more.end()));
        };
        if (iso15118::ev::is_ac_family(energy_service)) {
            append(iso15118::ev::validate_ac_charge_params((*h).ac_params));
        }
        if (energy_service == dt::ServiceCategory::DC or energy_service == dt::ServiceCategory::DC_BPT) {
            append(iso15118::ev::validate_dc_charge_params((*h).dc_params));
        }
        if (not problems.empty()) {
            for (const auto& problem : problems) {
                EVLOG_error << "Ev15118: invalid session parameter: " << problem;
            }
            EVLOG_error << "Ev15118: rejecting start_charging; the session parameters are invalid";
            return false;
        }
        (*h).phase = SessionPhase::requested;
    }
    session.notify_all();
    return true;
}

void ISO15118_evImpl::end_session(ControllerAction action, bool drop_paused) {
    bool cancelled = false;
    {
        auto h = session.handle();
        if ((*h).current) {
            ((*h).current->*action)();
        } else {
            if ((*h).phase == SessionPhase::requested) {
                // the request arrived before the worker constructed the controller; cancel the
                // pending session so run_one_session() skips it under the lock
                (*h).phase = SessionPhase::idle;
                // The worker still publishes v2g_session_finished for this start_charging.
                (*h).finish_pending = true;
                cancelled = true;
            }
            if (drop_paused) {
                (*h).paused.reset();
            }
        }
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
    auto h = session.handle();
    // Latched: the report may arrive before a controller exists, and reporting it at all is
    // what enables the CP-dependent checks (EvConfig::has_cp_state_feedback).
    (*h).cp_c_or_d = c_or_d;
    if ((*h).current) {
        (*h).current->set_cp_state(c_or_d);
    }
}

void ISO15118_evImpl::handle_set_fault() {
    EVLOG_info << "Ev15118: set_fault";
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
        // absent fields fold to 0 and would advertise a 0 W limit to the SECC
        EVLOG_warning << "Ev15118: set_dc_params missing " << missing << "; defaulting to 0";
    }

    auto h = session.handle();
    auto& params = (*h).dc_params;
    params.max_charge_power = EvParameters.max_power_limit.value_or(0.0f);
    params.max_charge_current = EvParameters.max_current_limit.value_or(0.0f);
    params.max_voltage = EvParameters.max_voltage_limit.value_or(0.0f);
    params.min_voltage = EvParameters.min_voltage_limit.value_or(0.0f);
    params.energy_capacity = EvParameters.energy_capacity.value_or(0.0f);
    params.target_voltage = EvParameters.target_voltage.value_or(0.0f);
    params.target_current = EvParameters.target_current.value_or(0.0f);
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
        EVLOG_warning << "Ev15118: set_bpt_dc_params missing " << missing
                      << "; keeping the configured discharge settings";
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

    // command values override the config settings seeded at start_charging
    auto h = session.handle();
    if (EvBPTParameters.discharge_max_power_limit) {
        (*h).cmd_max_discharge_power = static_cast<float>(*EvBPTParameters.discharge_max_power_limit);
    }
    if (EvBPTParameters.discharge_max_current_limit) {
        (*h).cmd_max_discharge_current = static_cast<float>(*EvBPTParameters.discharge_max_current_limit);
    }
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
}

} // namespace ev
} // namespace module
