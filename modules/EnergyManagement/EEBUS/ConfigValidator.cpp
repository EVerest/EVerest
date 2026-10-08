// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <ConfigValidator.hpp>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>

#include "EEBUS.hpp"

namespace {
/// \brief lowercase a SKI and strip embedded spaces
std::string normalize_ski(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    s.erase(std::remove(s.begin(), s.end(), ' '), s.end());
    return s;
}

/// \brief check that a normalized SKI is exactly 40 hex characters
bool is_valid_ski_format(const std::string& s) {
    if (s.size() != 40) {
        return false;
    }
    return std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isxdigit(c); });
}

// Mirrors ocpp::split_string from lib/everest/ocpp/lib/ocpp/common/utils.cpp.
// Kept local to avoid a runtime dep from EEBUS onto libocpp.

/// \brief strip leading and trailing whitespace
std::string trim_string(const std::string& s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

/// \brief split a string on a separator, optionally trimming each token; empty tokens are dropped
std::vector<std::string> split_string(const std::string& string_to_split, char sep, bool trim) {
    std::stringstream input(string_to_split);
    std::string token;
    std::vector<std::string> result;
    while (std::getline(input, token, sep)) {
        if (trim) {
            token = trim_string(token);
        }
        if (!token.empty()) {
            result.push_back(token);
        }
    }
    return result;
}
} // namespace

namespace module {

ConfigValidator::ConfigValidator(const Conf& config, std::filesystem::path etc_prefix,
                                 std::filesystem::path libexec_prefix) :
    m_config(config),
    m_etc_prefix(std::move(etc_prefix)),
    m_libexec_prefix(std::move(libexec_prefix)),
    m_certificate_path(std::filesystem::path(config.certificate_path)),
    m_private_key_path(std::filesystem::path(config.private_key_path)),
    m_eebus_grpc_api_binary_path(std::filesystem::path(config.eebus_grpc_api_binary_path)),
    m_eebus_service_port(config.eebus_service_port),
    m_grpc_port(config.grpc_port),
    m_manage_eebus_grpc_api_binary(config.manage_eebus_grpc_api_binary) {

    if (m_manage_eebus_grpc_api_binary) {
        if (m_certificate_path.is_relative()) {
            m_certificate_path = m_etc_prefix / "certs" / m_certificate_path;
            EVLOG_info << "Certificate path is relative, using etc prefix: " << m_certificate_path;
        }
        if (m_private_key_path.is_relative()) {
            m_private_key_path = m_etc_prefix / "certs" / m_private_key_path;
            EVLOG_info << "Key path is relative, using etc prefix: " << m_private_key_path;
        }
        if (m_eebus_grpc_api_binary_path.is_relative()) {
            m_eebus_grpc_api_binary_path = m_libexec_prefix / m_eebus_grpc_api_binary_path;
            EVLOG_info << "EEBUS GRPC API binary path is relative, using libexec prefix: "
                       << m_eebus_grpc_api_binary_path;
        }
    }

    // Build the effective allowlist as a deduplicated set of normalized SKIs.
    for (const auto& ski : split_string(m_config.eebus_ems_ski_allowlist, ',', /*trim=*/true)) {
        m_effective_allowlist.insert(normalize_ski(ski));
    }
}

bool ConfigValidator::validate() {
    bool valid = true;
    valid &= validate_eebus_service_port();
    valid &= validate_grpc_port();
    valid &= validate_eebus_ems_ski_allowlist();
    valid &= validate_accept_unknown_ems();
    valid &= validate_certificate_path();
    valid &= validate_private_key_path();
    valid &= validate_eebus_grpc_api_binary_path();
    valid &= validate_manage_eebus_grpc_api_binary();
    valid &= validate_vendor_code();
    valid &= validate_device_brand();
    valid &= validate_device_model();
    valid &= validate_serial_number();
    valid &= validate_failsafe_control_limit();
    valid &= validate_max_nominal_power();
    return valid;
}

std::filesystem::path ConfigValidator::get_certificate_path() const {
    return m_certificate_path;
}

std::filesystem::path ConfigValidator::get_private_key_path() const {
    return m_private_key_path;
}

std::filesystem::path ConfigValidator::get_eebus_grpc_api_binary_path() const {
    return m_eebus_grpc_api_binary_path;
}

int ConfigValidator::get_eebus_service_port() const {
    return m_eebus_service_port;
}

int ConfigValidator::get_grpc_port() const {
    return m_grpc_port;
}

std::string ConfigValidator::get_vendor_code() const {
    return m_config.vendor_code;
}

std::string ConfigValidator::get_device_brand() const {
    return m_config.device_brand;
}

std::string ConfigValidator::get_device_model() const {
    return m_config.device_model;
}

std::string ConfigValidator::get_serial_number() const {
    return m_config.serial_number;
}

int ConfigValidator::get_failsafe_control_limit() const {
    return m_config.failsafe_control_limit_W;
}

int ConfigValidator::get_max_nominal_power() const {
    return m_config.max_nominal_power_W;
}

int ConfigValidator::get_restart_delay_s() const {
    return m_config.restart_delay_s;
}

int ConfigValidator::get_reconnect_delay_s() const {
    return m_config.reconnect_delay_s;
}

bool ConfigValidator::validate_eebus_service_port() const {
    if (m_config.eebus_service_port < 0) {
        EVLOG_error << "eebus service port is negative";
        return false;
    }
    return true;
}

bool ConfigValidator::validate_grpc_port() const {
    if (m_config.grpc_port < 0) {
        EVLOG_error << "grpc port is negative";
        return false;
    }
    return true;
}

bool ConfigValidator::validate_certificate_path() const {
    if (!m_manage_eebus_grpc_api_binary) {
        return true;
    }
    if (std::filesystem::exists(m_certificate_path)) {
        return true;
    }
    // Sidecar will generate the cert; ensure parent directory exists.
    auto parent = m_certificate_path.parent_path();
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec) {
        EVLOG_error << "Failed to create certificate directory " << parent << ": " << ec.message();
        return false;
    }
    EVLOG_info << "Certificate not found at " << m_certificate_path
               << "; eebus_grpc_api will generate it on first run.";
    return true;
}

bool ConfigValidator::validate_private_key_path() const {
    if (!m_manage_eebus_grpc_api_binary) {
        return true;
    }
    if (std::filesystem::exists(m_private_key_path)) {
        return true;
    }
    // Sidecar will generate the key; ensure parent directory exists.
    auto parent = m_private_key_path.parent_path();
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec) {
        EVLOG_error << "Failed to create private key directory " << parent << ": " << ec.message();
        return false;
    }
    EVLOG_info << "Private key not found at " << m_private_key_path
               << "; eebus_grpc_api will generate it on first run.";
    return true;
}

bool ConfigValidator::validate_eebus_grpc_api_binary_path() const {
    if (!m_manage_eebus_grpc_api_binary) {
        return true;
    }
    if (!std::filesystem::exists(m_eebus_grpc_api_binary_path)) {
        EVLOG_error << "EEBUS GRPC API binary does not exist: " << m_eebus_grpc_api_binary_path;
        return false;
    }
    const auto perms = std::filesystem::status(m_eebus_grpc_api_binary_path).permissions();
    const auto owner_exec_perms = std::filesystem::perms::owner_exec;
    if ((perms & owner_exec_perms) == std::filesystem::perms::none) {
        EVLOG_error << "EEBUS GRPC API binary is not executable: " << m_eebus_grpc_api_binary_path;
        return false;
    }
    return true;
}

bool ConfigValidator::validate_manage_eebus_grpc_api_binary() const {
    return true;
}

bool ConfigValidator::validate_vendor_code() const {
    if (m_config.vendor_code.empty()) {
        EVLOG_error << "vendor_code is empty";
        return false;
    }
    return true;
}

bool ConfigValidator::validate_device_brand() const {
    if (m_config.device_brand.empty()) {
        EVLOG_error << "device_brand is empty";
        return false;
    }
    return true;
}

bool ConfigValidator::validate_device_model() const {
    if (m_config.device_model.empty()) {
        EVLOG_error << "device_model is empty";
        return false;
    }
    return true;
}

bool ConfigValidator::validate_serial_number() const {
    if (m_config.serial_number.empty()) {
        EVLOG_error << "serial_number is empty";
        return false;
    }
    return true;
}

bool ConfigValidator::validate_failsafe_control_limit() const {
    if (m_config.failsafe_control_limit_W < 0) {
        EVLOG_error << "failsafe_control_limit is negative";
        return false;
    }
    return true;
}

bool ConfigValidator::validate_max_nominal_power() const {
    if (m_config.max_nominal_power_W < 0) {
        EVLOG_error << "max_nominal_power is negative";
        return false;
    }
    return true;
}

bool ConfigValidator::validate_eebus_ems_ski_allowlist() const {
    for (const auto& ski : split_string(m_config.eebus_ems_ski_allowlist, ',', /*trim=*/true)) {
        const auto normalized = normalize_ski(ski);
        if (!is_valid_ski_format(normalized)) {
            EVLOG_error << "eebus_ems_ski_allowlist entry is not 40 hex chars: " << ski;
            return false;
        }
        EVLOG_info << "EEBUS: trusting allowlisted EMS SKI=" << normalized;
    }
    if (m_effective_allowlist.empty() && !m_config.accept_unknown_ems) {
        EVLOG_warning << "EEBUS: no EMS SKI configured and accept_unknown_ems=false; "
                      << "no EG will ever be trusted.";
    }
    return true;
}

bool ConfigValidator::validate_accept_unknown_ems() const {
    if (m_config.accept_unknown_ems) {
        EVLOG_info << "EEBUS: accept_unknown_ems=true — any discovered EG on the LAN "
                   << "will be auto-trusted. Only enable on isolated networks.";
    }
    return true;
}

const std::set<std::string>& ConfigValidator::get_effective_ems_ski_allowlist() const {
    return m_effective_allowlist;
}

bool ConfigValidator::get_accept_unknown_ems() const {
    return m_config.accept_unknown_ems;
}

} // namespace module
