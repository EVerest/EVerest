// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#pragma once

#include <ocpp/v16/charge_point_configuration_interface.hpp>

namespace ocpp::v16 {

/// \brief Implements the ConnectivityManagerConfiguration part of ChargePointConfigurationInterface for OCPP 1.6.
///
/// The behaviour is identical regardless of how the configuration is stored (JSON config file or device model):
/// it depends only on the OCPP 1.6 getters/setters declared on ChargePointConfigurationInterface, which are
/// resolved via virtual dispatch to the concrete implementation. This class remains abstract; it does not
/// implement those getters/setters.
class ChargePointConfigurationConnectivity : public ChargePointConfigurationInterface {
public:
    std::string get_network_configuration_priority() override;
    void set_network_configuration_priority(const std::string& priority, const std::string& source) override;
    std::optional<ocpp::v2::NetworkConnectionProfile> read_network_connection_profile(int32_t slot) override;
    bool write_network_connection_profile(int32_t slot, const ocpp::v2::NetworkConnectionProfile& profile,
                                          const std::string& source) override;
    void clear_network_connection_profile(int32_t slot) override;
    bool get_allow_security_level_zero_connections() override;
    int32_t get_security_profile() override;
    std::optional<int32_t> get_network_config_timeout() override;
    std::optional<WebsocketConnectionOptions> get_websocket_connection_options(int32_t slot) override;
    void set_active_security_profile(int32_t security_profile, const std::string& source) override;
    void set_active_network_profile_slot(int32_t slot, const std::string& source) override;
    void set_per_slot_ocpp_version(int32_t slot, const std::string& version, const std::string& source) override;
    void set_security_ctrl_security_profile(int32_t security_profile, const std::string& source) override;
    void set_security_ctrl_identity(const std::string& identity, const std::string& source) override;
};

} // namespace ocpp::v16
