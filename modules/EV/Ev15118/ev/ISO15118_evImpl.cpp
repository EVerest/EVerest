// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "ISO15118_evImpl.hpp"

#include "session_logger.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include <iso15118/detail/io/socket_helper.hpp>
#include <iso15118/io/logging.hpp>

namespace module {
namespace ev {

namespace dt = iso15118::message_20::datatypes;

namespace {

// Flow-spec §3 minima the DC states expect from the EV, and Josev defaults for absent maxima.
constexpr float DC_MIN_CHARGE_POWER_W = 100.0f;
constexpr float DC_MIN_CHARGE_CURRENT_A = 10.0f;
constexpr float DC_MIN_VOLTAGE_V = 10.0f;
constexpr float DC_DEFAULT_MAX_CURRENT_A = 300.0f;
constexpr float DC_DEFAULT_MAX_POWER_W = 150000.0f;
constexpr float DC_DEFAULT_MAX_VOLTAGE_V = 900.0f;
constexpr float DC_DEFAULT_ENERGY_CAPACITY_WH = 60000.0f;
// DC SIL target overrides (see flow spec §0), only used when EvManager leaves them unset.
constexpr float DC_DEFAULT_TARGET_CURRENT_A = 20.0f;
constexpr float DC_DEFAULT_TARGET_VOLTAGE_V = 400.0f;

// BPT discharge defaults (flow spec §3 DC branch).
constexpr float DC_BPT_DEFAULT_MAX_DISCHARGE_POWER_W = 11000.0f;
constexpr float DC_BPT_MIN_DISCHARGE_POWER_W = 1000.0f;
constexpr float DC_BPT_DEFAULT_MAX_DISCHARGE_CURRENT_A = 25.0f;
constexpr float DC_BPT_MIN_DISCHARGE_CURRENT_A = 0.0f;

// AC parameters (flow spec §3 AC branch); EvManager does not push AC parameters, so use SIL defaults.
constexpr float AC_MAX_CHARGE_POWER_W = 11000.0f;
constexpr float AC_MIN_CHARGE_POWER_W = 100.0f;

iso15118::config::SSLConfig make_ssl_config(const module::Conf& config, const std::filesystem::path& etc_path) {
    const auto certs_path = etc_path / "certs";

    iso15118::config::SSLConfig ssl;
    ssl.backend = iso15118::config::CertificateBackend::EVEREST_LAYOUT;

    // The vehicle certificate (CN=WMIV1234567890ABCDEX) chains to the V2G root, which is what the
    // SECC verifies TLS 1.3 client certificates against (via EvseSecurity get_verify_file(V2G)).
    // The chain PEM is leaf-first (VEHICLE_LEAF, VehicleSubCA2, VehicleSubCA1), as
    // SSL_CTX_use_certificate_chain_file expects.
    ssl.path_certificate_chain = config.device_cert_chain_path.empty()
                                     ? (certs_path / "client/vehicle/VEHICLE_CERT_CHAIN.pem").string()
                                     : config.device_cert_chain_path;
    ssl.path_certificate_key = config.device_key_path.empty()
                                   ? (certs_path / "client/vehicle/VEHICLE_LEAF.key").string()
                                   : config.device_key_path;
    ssl.path_certificate_v2g_root = config.v2g_root_cert_path.empty()
                                        ? (certs_path / "ca/v2g/V2G_ROOT_CA.pem").string()
                                        : config.v2g_root_cert_path;

    // Resolve and read the private key password (trimmed). Absent/empty file -> no password.
    const auto password_path = config.device_key_password_path.empty()
                                   ? (certs_path / "client/vehicle/VEHICLE_LEAF_PASSWORD.txt")
                                   : std::filesystem::path(config.device_key_password_path);
    std::ifstream password_file(password_path);
    if (password_file.is_open()) {
        std::stringstream buffer;
        buffer << password_file.rdbuf();
        auto password = buffer.str();
        // trim surrounding whitespace / newlines
        const auto not_space = [](unsigned char c) { return not std::isspace(c); };
        password.erase(password.begin(), std::find_if(password.begin(), password.end(), not_space));
        password.erase(std::find_if(password.rbegin(), password.rend(), not_space).base(), password.end());
        if (not password.empty()) {
            ssl.private_key_password = password;
        }
    }

    ssl.enable_ssl_logging = config.enable_ssl_logging;
    ssl.enable_tls_key_logging = config.enable_tls_key_logging;
    ssl.enforce_tls_1_3 = config.enable_tls_1_3;
    ssl.tls_key_logging_path = config.tls_key_logging_path;

    return ssl;
}

iso15118::message_20::datatypes::ServiceCategory parse_service_category(const std::string& value) {
    using ServiceCategory = iso15118::message_20::datatypes::ServiceCategory;
    if (value == "AC") {
        return ServiceCategory::AC;
    }
    if (value == "AC_BPT") {
        return ServiceCategory::AC_BPT;
    }
    if (value == "DC") {
        return ServiceCategory::DC;
    }
    if (value == "DC_BPT") {
        return ServiceCategory::DC_BPT;
    }
    throw std::invalid_argument("Unsupported entry in supported_d20_energy_services: " + value);
}

std::vector<iso15118::message_20::datatypes::ServiceCategory> parse_supported_services(const std::string& csv) {
    std::vector<iso15118::message_20::datatypes::ServiceCategory> services;
    std::stringstream stream(csv);
    std::string token;
    while (std::getline(stream, token, ',')) {
        // trim whitespace
        const auto not_space = [](unsigned char c) { return not std::isspace(c); };
        token.erase(token.begin(), std::find_if(token.begin(), token.end(), not_space));
        token.erase(std::find_if(token.rbegin(), token.rend(), not_space).base(), token.end());
        if (token.empty()) {
            continue;
        }
        services.push_back(parse_service_category(token));
    }
    return services;
}

bool is_dc_service(iso15118::message_20::datatypes::ServiceCategory service) {
    using ServiceCategory = iso15118::message_20::datatypes::ServiceCategory;
    switch (service) {
    case ServiceCategory::DC:
    case ServiceCategory::DC_BPT:
    case ServiceCategory::DC_ACDP:
    case ServiceCategory::DC_ACDP_BPT:
    case ServiceCategory::MCS:
    case ServiceCategory::MCS_BPT:
        return true;
    default:
        return false;
    }
}

// Family of the requested EnergyTransferMode: true = DC family, false = AC family.
bool is_dc_energy_transfer_mode(types::iso15118::EnergyTransferMode mode) {
    using EnergyTransferMode = types::iso15118::EnergyTransferMode;
    switch (mode) {
    case EnergyTransferMode::DC:
    case EnergyTransferMode::DC_core:
    case EnergyTransferMode::DC_extended:
    case EnergyTransferMode::DC_combo_core:
    case EnergyTransferMode::DC_unique:
    case EnergyTransferMode::DC_BPT:
    case EnergyTransferMode::DC_ACDP:
    case EnergyTransferMode::DC_ACDP_BPT:
    case EnergyTransferMode::MCS:
    case EnergyTransferMode::MCS_BPT:
        return true;
    default:
        // Everything else (AC_single_phase_core, AC_two_phase, AC_three_phase_core, AC_BPT, ...) is AC.
        return false;
    }
}

std::optional<dt::RationalNumber> to_rational(const std::optional<float>& in) {
    return in.has_value() ? std::make_optional(dt::from_float(in.value())) : std::nullopt;
}

// Map a types::iso15118::EnergyTransferMode onto the ISO 15118-2 / DIN energy transfer mode
// granularity (AC single vs three phase, or a DC family value). ISO-2/DIN have no two-phase AC value,
// so AC_two_phase collapses to AC_single_phase_core.
iso15118::message_2::datatypes::EnergyTransferMode
to_iso2_transfer_mode(types::iso15118::EnergyTransferMode mode) {
    using In = types::iso15118::EnergyTransferMode;
    using Out = iso15118::message_2::datatypes::EnergyTransferMode;
    switch (mode) {
    case In::AC_single_phase_core:
    case In::AC_two_phase:
        return Out::AC_single_phase_core;
    case In::AC_three_phase_core:
    case In::AC_BPT:
    case In::AC_BPT_DER:
    case In::AC_DER_IEC:
    case In::AC_DER_SAE:
        return Out::AC_three_phase_core;
    case In::DC_core:
        return Out::DC_core;
    case In::DC_combo_core:
        return Out::DC_combo_core;
    case In::DC_unique:
        return Out::DC_unique;
    default:
        // DC, DC_extended, DC_BPT, DC_ACDP(_BPT), MCS(_BPT), WPT ...
        return Out::DC_extended;
    }
}

// Read the 6-byte MAC of the given network interface from sysfs (e.g. "aa:bb:cc:dd:ee:ff"). Returns
// nullopt on any failure.
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

    std::array<uint8_t, 6> mac{};
    unsigned int bytes[6];
    if (std::sscanf(line.c_str(), "%x:%x:%x:%x:%x:%x", &bytes[0], &bytes[1], &bytes[2], &bytes[3], &bytes[4],
                    &bytes[5]) != 6) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < mac.size(); ++i) {
        mac[i] = static_cast<uint8_t>(bytes[i]);
    }
    return mac;
}

types::iso15118::AcTargetPower convert_ac_target_power(const iso15118::d20::AcTargetPower& in) {
    types::iso15118::AcTargetPower out;
    const auto convert = [](const std::optional<dt::RationalNumber>& value) -> std::optional<float> {
        return value.has_value() ? std::make_optional(dt::from_RationalNumber(value.value())) : std::nullopt;
    };
    out.target_active_power = convert(in.target_active_power);
    out.target_active_power_L2 = convert(in.target_active_power_L2);
    out.target_active_power_L3 = convert(in.target_active_power_L3);
    out.target_reactive_power = convert(in.target_reactive_power);
    out.target_reactive_power_L2 = convert(in.target_reactive_power_L2);
    out.target_reactive_power_L3 = convert(in.target_reactive_power_L3);
    return out;
}

} // namespace

void ISO15118_evImpl::init() {
    EVLOG_info << "Ev15118: initializing EVCC (DIN SPEC 70121 / ISO 15118-2 / ISO 15118-20) implementation";

    // Route the library logging into the EVerest logger (mirrors Evse15118D20).
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

    try {
        supported_services = parse_supported_services(mod->config.supported_d20_energy_services);
    } catch (const std::exception& e) {
        EVLOG_error << "Ev15118: failed to parse supported_d20_energy_services: " << e.what();
    }

    // Build the priority-ordered protocol offer: ISO 15118-20 > ISO 15118-2 > DIN SPEC 70121.
    supported_protocols.clear();
    if (not supported_services.empty()) {
        supported_protocols.push_back(iso15118::ProtocolId::ISO15118_20);
    }
    if (mod->config.supported_ISO15118_2) {
        supported_protocols.push_back(iso15118::ProtocolId::ISO15118_2);
    }
    if (mod->config.supported_DIN70121) {
        supported_protocols.push_back(iso15118::ProtocolId::DIN70121);
    }

    if (supported_protocols.empty()) {
        EVLOG_error << "Ev15118: no protocol enabled (supported_d20_energy_services empty and "
                       "supported_ISO15118_2 / supported_DIN70121 both false); the EVCC cannot start a session";
        startup_error = true;
    }

    // TLS 1.3 is pinned (min == max) in the client SSL context, while ISO 15118-2 mandates TLS 1.2.
    // With both enabled every ISO 15118-2 TLS connection would fail the handshake, so refuse to start.
    if (mod->config.enable_tls_1_3 and mod->config.supported_ISO15118_2) {
        EVLOG_error << "Ev15118: enable_tls_1_3 and supported_ISO15118_2 are both set, but ISO 15118-2 requires "
                       "TLS 1.2; disable one of them. The EVCC will not start";
        startup_error = true;
    }

    // enforce_tls implies a TLS connection; surface a misconfiguration where tls_active is left false.
    if (mod->config.enforce_tls and not mod->config.tls_active) {
        EVLOG_warning << "Ev15118: enforce_tls is set but tls_active is false; forcing a TLS connection "
                         "(enforce_tls takes precedence)";
    }
}

iso15118::d20::ev::DcEvChargeParameters ISO15118_evImpl::build_dc_charge_parameters() const {
    iso15118::d20::ev::DcEvChargeParameters params;

    const auto max_current = dc_params.has_value() ? dc_params->max_current_limit : std::nullopt;
    const auto max_power = dc_params.has_value() ? dc_params->max_power_limit : std::nullopt;
    const auto max_voltage = dc_params.has_value() ? dc_params->max_voltage_limit : std::nullopt;
    const auto energy_capacity = dc_params.has_value() ? dc_params->energy_capacity : std::nullopt;
    const auto target_current = dc_params.has_value() ? dc_params->target_current : std::nullopt;
    const auto target_voltage = dc_params.has_value() ? dc_params->target_voltage : std::nullopt;

    params.max_charge_power = dt::from_float(max_power.value_or(DC_DEFAULT_MAX_POWER_W));
    params.min_charge_power = dt::from_float(DC_MIN_CHARGE_POWER_W);
    params.max_charge_current = dt::from_float(max_current.value_or(DC_DEFAULT_MAX_CURRENT_A));
    params.min_charge_current = dt::from_float(DC_MIN_CHARGE_CURRENT_A);
    params.max_voltage = dt::from_float(max_voltage.value_or(DC_DEFAULT_MAX_VOLTAGE_V));
    params.min_voltage = dt::from_float(DC_MIN_VOLTAGE_V);
    params.target_voltage = dt::from_float(target_voltage.value_or(DC_DEFAULT_TARGET_VOLTAGE_V));
    params.target_current = dt::from_float(target_current.value_or(DC_DEFAULT_TARGET_CURRENT_A));
    params.energy_capacity = dt::from_float(energy_capacity.value_or(DC_DEFAULT_ENERGY_CAPACITY_WH));

    // Leave the dynamic energy-request window unset: the library applies the flow-spec §3 SIL defaults
    // (ScheduleExchange target 40 kWh / max 60 kWh / min -20 kWh; ChargeLoop target = energy_capacity).

    return params;
}

std::optional<iso15118::d20::ev::DcEvBptChargeParameters>
ISO15118_evImpl::build_bpt_dc_charge_parameters() const {
    if (not bpt_dc_params.has_value()) {
        return std::nullopt;
    }

    iso15118::d20::ev::DcEvBptChargeParameters params;
    // Inherit the DC base fields.
    static_cast<iso15118::d20::ev::DcEvChargeParameters&>(params) = build_dc_charge_parameters();

    params.max_discharge_power =
        dt::from_float(bpt_dc_params->discharge_max_power_limit.value_or(DC_BPT_DEFAULT_MAX_DISCHARGE_POWER_W));
    params.min_discharge_power = dt::from_float(DC_BPT_MIN_DISCHARGE_POWER_W);
    params.max_discharge_current =
        dt::from_float(bpt_dc_params->discharge_max_current_limit.value_or(DC_BPT_DEFAULT_MAX_DISCHARGE_CURRENT_A));
    params.min_discharge_current = dt::from_float(DC_BPT_MIN_DISCHARGE_CURRENT_A);

    if (bpt_dc_params->discharge_minimal_soc.has_value()) {
        params.min_soc = static_cast<uint8_t>(std::floor(bpt_dc_params->discharge_minimal_soc.value()));
    }

    // V2X energy-request window left unset: library applies SIL defaults (max 5 kWh / min 0).

    return params;
}

std::optional<iso15118::message_20::datatypes::ServiceCategory>
ISO15118_evImpl::select_service_for(types::iso15118::EnergyTransferMode mode) const {
    const bool want_dc = is_dc_energy_transfer_mode(mode);
    for (const auto& service : supported_services) {
        if (is_dc_service(service) == want_dc) {
            return service;
        }
    }
    // No configured ISO 15118-20 service of the requested family. If a pre-20 protocol is offered, arm
    // with a generic AC/DC service so the ISO-2 / DIN branch (which only needs the AC/DC family) works.
    const bool has_pre20 =
        std::find(supported_protocols.begin(), supported_protocols.end(), iso15118::ProtocolId::ISO15118_2) !=
            supported_protocols.end() or
        std::find(supported_protocols.begin(), supported_protocols.end(), iso15118::ProtocolId::DIN70121) !=
            supported_protocols.end();
    if (has_pre20) {
        return want_dc ? dt::ServiceCategory::DC : dt::ServiceCategory::AC;
    }
    return std::nullopt;
}

iso15118::session::ev::feedback::Callbacks ISO15118_evImpl::create_callbacks() {
    namespace feedback = iso15118::session::ev::feedback;
    feedback::Callbacks callbacks;

    callbacks.signal = [this](feedback::Signal signal) {
        EVLOG_verbose << "Ev15118: session signal " << static_cast<int>(signal);
    };

    callbacks.ev_power_ready = [this](bool ready) { publish_ev_power_ready(ready); };

    callbacks.dc_power_on = [this]() { publish_dc_power_on(nullptr); };

    callbacks.ac_evse_target_power = [this](const iso15118::d20::AcTargetPower& target) {
        publish_ac_evse_target_power(convert_ac_target_power(target));
    };

    callbacks.stop_from_charger = [this]() { publish_stop_from_charger(nullptr); };

    callbacks.pause_from_charger = [this]() { publish_pause_from_charger(nullptr); };

    callbacks.v2g_session_finished = [this]() { publish_v2g_session_finished(nullptr); };

    callbacks.selected_protocol = [this](const std::string& protocol) {
        EVLOG_info << "Ev15118: selected protocol " << protocol;
    };

    callbacks.evse_id = [this](const std::string& evse_id) { EVLOG_info << "Ev15118: EVSE ID " << evse_id; };

    callbacks.dc_evse_present_limits = [this](const feedback::DcMaximumLimits& limits) {
        EVLOG_verbose << "Ev15118: EVSE present DC limits current=" << limits.current << " power=" << limits.power
                      << " voltage=" << limits.voltage;
    };

    callbacks.v2g_message = [this](const iso15118::V2gMessageType&) {};

    return callbacks;
}

void ISO15118_evImpl::ready() {
    EVLOG_info << "Ev15118: ready";

    if (startup_error) {
        EVLOG_error << "Ev15118: not starting the EVCC because of an invalid configuration (see init() errors)";
        return;
    }

    // Installs the global iso15118 session log callback for this process and writes
    // per-session yaml logs to logging_path (same mechanism as Evse15118D20).
    const auto session_logger = std::make_unique<SessionLogger>(mod->config.logging_path);

    iso15118::EvConfig ev_config;
    iso15118::session::EvSetupConfig setup_config;
    {
        std::scoped_lock lock(config_mutex);

        // Resolve the HLC interface ("auto" -> first ipv6 link-local interface) so the same device is
        // used for the connection and for reading the EVCC MAC.
        std::string device = mod->config.device;
        if (not iso15118::io::check_and_update_interface(device)) {
            if (mod->config.device == "auto") {
                // "auto" has no meaningful as-is value; feeding it back into the controller would only
                // fail interface resolution a second time. Bail cleanly instead.
                EVLOG_error << "Ev15118: could not resolve HLC interface 'auto' to a usable ipv6 interface; "
                               "not starting the EVCC";
                return;
            }
            EVLOG_warning << "Ev15118: could not resolve HLC interface '" << mod->config.device
                          << "'; using it as-is";
            device = mod->config.device;
        }

        // Only override the library EVCCID default when a real MAC was read; on failure keep the
        // library's deliberate non-zero default for ISO 15118-2 / DIN.
        bool mac_resolved = false;
        if (const auto mac = read_interface_mac(device)) {
            evcc_mac = mac.value();
            mac_resolved = true;
            EVLOG_info << "Ev15118: using EVCC MAC of interface " << device;
        } else {
            EVLOG_warning << "Ev15118: could not read MAC of interface '" << device
                          << "', keeping the library default EVCCID for ISO 15118-2 / DIN";
        }

        ev_config.ssl = make_ssl_config(mod->config, mod->info.paths.etc);
        ev_config.interface_name = device;
        ev_config.enable_sdp = true;
        ev_config.enforce_tls = mod->config.enforce_tls;
        // enforce_tls takes precedence over tls_active and always implies a TLS connection.
        ev_config.use_tls = mod->config.tls_active or mod->config.enforce_tls;
        ev_config.verify_server_certificate = mod->config.verify_server_certificate;

        setup_config.evcc_id = mod->config.evccid;
        setup_config.supported_energy_services = supported_services;
        setup_config.supported_protocols = supported_protocols;
        if (mac_resolved) {
            setup_config.evcc_mac = evcc_mac;
        }
        setup_config.preferred_control_mode = dt::ControlMode::Dynamic;
        setup_config.supported_auth_options = {dt::Authorization::EIM};
        setup_config.dc_charge_parameters = build_dc_charge_parameters();
        setup_config.dc_bpt_charge_parameters = build_bpt_dc_charge_parameters();

        setup_config.ac_charge_parameters.max_charge_power = dt::from_float(AC_MAX_CHARGE_POWER_W);
        setup_config.ac_charge_parameters.min_charge_power = dt::from_float(AC_MIN_CHARGE_POWER_W);
        // The ISO 15118-2 AC parameters (iso2_ac_e_amount / max voltage / max & min current) keep the
        // sane defaults from EvSetupConfig.

        try {
            controller = std::make_unique<iso15118::EvController>(std::move(ev_config), create_callbacks(),
                                                                 std::move(setup_config));
        } catch (const std::exception& e) {
            EVLOG_error << "Ev15118: failed to construct the EVCC controller: " << e.what()
                        << "; the EVCC will not start";
            return;
        }
    }

    // Blocking; runs until the controller is shut down.
    try {
        controller->loop();
    } catch (const std::exception& e) {
        EVLOG_error << "Ev15118: controller loop exited with exception: " << e.what();
    }
}

bool ISO15118_evImpl::handle_start_charging(types::iso15118::EnergyTransferMode& EnergyTransferMode,
                                            types::iso15118::SelectedPaymentOption& SelectedPaymentOption,
                                            double& DepartureTime, double& EAmount) {
    EVLOG_info << "handle_start_charging: EnergyTransferMode="
               << types::iso15118::energy_transfer_mode_to_string(EnergyTransferMode) << " payment_option="
               << (SelectedPaymentOption.payment_option.has_value()
                       ? types::iso15118::payment_option_to_string(SelectedPaymentOption.payment_option.value())
                       : std::string{"none"})
               << " DepartureTime=" << DepartureTime << " EAmount=" << EAmount;

    // Payment: only EIM (ExternalPayment) is supported by the SECC. Contract/PnC is unsupported.
    if (SelectedPaymentOption.payment_option.has_value() and
        SelectedPaymentOption.payment_option.value() == types::iso15118::PaymentOption::Contract) {
        EVLOG_warning << "Ev15118: Plug&Charge (Contract) is not supported, falling back to EIM";
    }

    // DepartureTime / EAmount are informational for now (the dynamic energy window uses SIL defaults).

    const auto service = select_service_for(EnergyTransferMode);
    if (not service.has_value()) {
        EVLOG_error << "Ev15118: no configured energy service matches requested EnergyTransferMode "
                    << types::iso15118::energy_transfer_mode_to_string(EnergyTransferMode);
        return false;
    }

    std::scoped_lock lock(config_mutex);
    if (not controller) {
        EVLOG_error << "Ev15118: start_charging called before the controller is ready";
        return false;
    }

    if (not controller->start_charging(service.value(), to_iso2_transfer_mode(EnergyTransferMode))) {
        EVLOG_warning << "Ev15118: start_charging requested while a session attempt is already active";
    }
    return true;
}

void ISO15118_evImpl::handle_stop_charging() {
    EVLOG_info << "handle_stop_charging";
    std::scoped_lock lock(config_mutex);
    if (controller) {
        controller->stop_charging();
    }
}

void ISO15118_evImpl::handle_pause_charging() {
    EVLOG_info << "handle_pause_charging";
    std::scoped_lock lock(config_mutex);
    if (controller) {
        controller->pause_charging();
    }
}

void ISO15118_evImpl::handle_set_fault() {
    EVLOG_info << "handle_set_fault";
}

void ISO15118_evImpl::handle_set_dc_params(types::iso15118::DcEvParameters& EvParameters) {
    EVLOG_info << "handle_set_dc_params: max_current_limit=" << EvParameters.max_current_limit.value_or(0)
               << " max_power_limit=" << EvParameters.max_power_limit.value_or(0)
               << " max_voltage_limit=" << EvParameters.max_voltage_limit.value_or(0)
               << " energy_capacity=" << EvParameters.energy_capacity.value_or(0)
               << " target_current=" << EvParameters.target_current.value_or(0)
               << " target_voltage=" << EvParameters.target_voltage.value_or(0);

    std::scoped_lock lock(config_mutex);
    dc_params = EvParameters;
    if (controller) {
        controller->update_dc_parameters(build_dc_charge_parameters());
        // Keep BPT parameters (which inherit the DC base) consistent with the updated DC set.
        if (const auto bpt = build_bpt_dc_charge_parameters()) {
            controller->update_bpt_dc_parameters(bpt.value());
        }
    }
}

void ISO15118_evImpl::handle_set_bpt_dc_params(types::iso15118::DcEvBPTParameters& EvBPTParameters) {
    EVLOG_info << "handle_set_bpt_dc_params: discharge_max_current_limit="
               << EvBPTParameters.discharge_max_current_limit.value_or(0)
               << " discharge_max_power_limit=" << EvBPTParameters.discharge_max_power_limit.value_or(0)
               << " discharge_target_current=" << EvBPTParameters.discharge_target_current.value_or(0)
               << " discharge_minimal_soc=" << EvBPTParameters.discharge_minimal_soc.value_or(0);

    std::scoped_lock lock(config_mutex);
    bpt_dc_params = EvBPTParameters;
    if (controller) {
        if (const auto bpt = build_bpt_dc_charge_parameters()) {
            controller->update_bpt_dc_parameters(bpt.value());
        }
    }
}

void ISO15118_evImpl::handle_enable_sae_j2847_v2g_v2h() {
    EVLOG_info << "handle_enable_sae_j2847_v2g_v2h";
}

void ISO15118_evImpl::handle_update_soc(double& SoC) {
    EVLOG_verbose << "handle_update_soc: SoC=" << SoC;
    std::scoped_lock lock(config_mutex);
    if (controller) {
        controller->update_soc(static_cast<uint8_t>(std::floor(SoC)));
    }
}

} // namespace ev
} // namespace module
