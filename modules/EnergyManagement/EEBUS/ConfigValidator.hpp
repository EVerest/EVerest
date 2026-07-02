// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

/// \file Validation and path resolution for the EEBUS module configuration

#pragma once

#include <filesystem>
#include <set>
#include <string>

namespace module {

class Conf;

/// \brief Validates the EEBUS module configuration and resolves relative paths
///        against the EVerest installation prefixes.
class ConfigValidator {
public:
    /// \brief construct a validator over the module configuration
    /// \param[in] config - the module configuration
    /// \param[in] etc_prefix - prefix for relative certificate/key paths
    /// \param[in] libexec_prefix - prefix for the relative eebus_grpc_api binary path
    ConfigValidator(const Conf& config, std::filesystem::path etc_prefix, std::filesystem::path libexec_prefix);

    /// \brief run all validation rules and log every violation
    /// \returns true when the configuration is valid
    bool validate();

    /// \brief resolved certificate path
    std::filesystem::path get_certificate_path() const;
    /// \brief resolved private key path
    std::filesystem::path get_private_key_path() const;
    /// \brief resolved path of the eebus_grpc_api binary
    std::filesystem::path get_eebus_grpc_api_binary_path() const;
    /// \brief port announced to the EEBUS control service
    int get_eebus_service_port() const;
    /// \brief port of the gRPC control service channel
    int get_grpc_port() const;
    /// \brief vendor code sent in the SetConfig call
    std::string get_vendor_code() const;
    /// \brief device brand sent in the SetConfig call
    std::string get_device_brand() const;
    /// \brief device model sent in the SetConfig call
    std::string get_device_model() const;
    /// \brief serial number sent in the SetConfig call
    std::string get_serial_number() const;
    /// \brief failsafe active power consumption limit in W
    int get_failsafe_control_limit() const;
    /// \brief maximum nominal power consumption in W
    int get_max_nominal_power() const;
    /// \brief delay between restarts of the managed eebus_grpc_api binary
    int get_restart_delay_s() const;
    /// \brief delay between reconnection attempts to the gRPC service
    int get_reconnect_delay_s() const;
    /// \brief deduplicated set of normalized (lowercase, no spaces) trusted EMS SKIs
    const std::set<std::string>& get_effective_ems_ski_allowlist() const;
    /// \brief whether unknown EMS SKIs discovered at runtime are auto-trusted
    bool get_accept_unknown_ems() const;

private:
    bool validate_eebus_service_port() const;
    bool validate_grpc_port() const;
    bool validate_eebus_ems_ski_allowlist() const;
    bool validate_accept_unknown_ems() const;
    bool validate_certificate_path() const;
    bool validate_private_key_path() const;
    bool validate_eebus_grpc_api_binary_path() const;
    bool validate_manage_eebus_grpc_api_binary() const;
    bool validate_vendor_code() const;
    bool validate_device_brand() const;
    bool validate_device_model() const;
    bool validate_serial_number() const;
    bool validate_failsafe_control_limit() const;
    bool validate_max_nominal_power() const;

    const Conf& m_config;

    std::filesystem::path m_etc_prefix;
    std::filesystem::path m_libexec_prefix;

    std::filesystem::path m_certificate_path;
    std::filesystem::path m_private_key_path;
    std::filesystem::path m_eebus_grpc_api_binary_path;
    int m_eebus_service_port{0};
    int m_grpc_port{0};
    bool m_manage_eebus_grpc_api_binary{false};
    std::set<std::string> m_effective_allowlist;
};

} // namespace module
