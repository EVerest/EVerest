// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2024 Pionix GmbH and Contributors to EVerest

#pragma once

#include <ocpp/common/websocket/websocket_base.hpp>
#include <ocpp/v2/ocpp_types.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ocpp {

/// \brief Configuration interface used by ConnectivityManager to persist and retrieve all network-configuration
///        data it needs, independently of the OCPP version in use.
class ConnectivityManagerConfiguration {
public:
    virtual ~ConnectivityManagerConfiguration() = default;

    /// \brief Get the comma-separated network-slot priority string (e.g. "1,2,0").
    /// \return The priority string, or an empty string when none is configured.
    virtual std::string get_network_configuration_priority() = 0;

    /// \brief Persist an updated priority string.
    virtual void set_network_configuration_priority(const std::string& priority, const std::string& source) = 0;

    /// \brief Read the NetworkConnectionProfile stored for \p slot.
    /// \return The profile, or std::nullopt if the slot is absent or malformed.
    virtual std::optional<ocpp::v2::NetworkConnectionProfile> read_network_connection_profile(std::int32_t slot) = 0;

    /// \brief Persist a NetworkConnectionProfile for \p slot.
    /// \return true on success.
    virtual bool write_network_connection_profile(std::int32_t slot, const ocpp::v2::NetworkConnectionProfile& profile,
                                                  const std::string& source) = 0;

    /// \brief Erase all persisted data for \p slot.
    virtual void clear_network_connection_profile(std::int32_t slot) = 0;

    /// \brief Return true if security-profile 0 connections are permitted.
    virtual bool get_allow_security_level_zero_connections() = 0;

    /// \brief Return the currently active security profile level.
    virtual std::int32_t get_security_profile() = 0;

    /// \brief Return the timeout (in seconds) to wait for a network-configuration callback result,
    ///        or std::nullopt to use the built-in default (60 s).
    virtual std::optional<std::int32_t> get_network_config_timeout() = 0;

    /// \brief Build and return the WebsocketConnectionOptions for the given \p slot.
    ///
    /// The implementation is responsible for reading the network connection profile for \p slot,
    /// resolving the identity and basic-auth password (applying any per-slot overrides), parsing
    /// and validating the CSMS URI, and populating all connection parameters (ciphers, ping/pong
    /// intervals, TLS flags, retry settings, etc.).
    ///
    /// \return The populated options, or std::nullopt if the slot has no valid profile or the CSMS
    ///         URL is malformed.
    virtual std::optional<WebsocketConnectionOptions> get_websocket_connection_options(std::int32_t slot) = 0;

    /// \brief Persist the security profile of the currently active connection.
    virtual void set_active_security_profile(std::int32_t security_profile, const std::string& source) = 0;

    /// \brief Persist the currently active network-profile slot.
    virtual void set_active_network_profile_slot(std::int32_t slot, const std::string& source) = 0;

    /// \brief Read the persisted active network-profile slot, or std::nullopt if none is stored.
    ///        Survives reboot, used to seed the last-successful profile for B10.FR.07 fallback.
    virtual std::optional<std::int32_t> get_active_network_profile_slot() = 0;

    /// \brief Persist the negotiated OCPP protocol version for a specific slot.
    virtual void set_per_slot_ocpp_version(std::int32_t slot, const std::string& version,
                                           const std::string& source) = 0;

    /// \brief Persist the security profile on the SecurityCtrlr component after a successful connect.
    virtual void set_security_ctrl_security_profile(std::int32_t security_profile, const std::string& source) = 0;

    /// \brief Persist the identity on the SecurityCtrlr component after a successful connect.
    virtual void set_security_ctrl_identity(const std::string& identity, const std::string& source) = 0;
};

} // namespace ocpp
