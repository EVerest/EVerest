// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <ocpp/common/connectivity_manager_configuration.hpp>
#include <ocpp/v2/connection_id.hpp>

namespace ocpp {
namespace v2 {

class DeviceModelAbstract;

/// \brief Presents the device model to a ConnectivityManager as the configuration of one OCPP
///        connection.
///
/// A ConnectivityManager owns exactly one websocket and reaches its configuration only through
/// ocpp::ConnectivityManagerConfiguration. Giving each connection its own adapter is therefore all
/// that is needed to give each connection its own set of network configuration slots: the manager
/// builds its slot cache purely from get_network_configuration_priority(), so slot selection,
/// failover and the security profile pruning required by B09.FR.04 / A05.FR.06 all become
/// per-connection without any change to ConnectivityManager itself.
///
/// For the primary connection every call delegates to the device model unchanged, so the existing
/// behaviour is preserved exactly. For a secondary connection the OCPPCommCtrlr reads and writes are
/// redirected to OCPPCommCtrlr[<instance>] (OCA application note "Secondary OCPP Connection on a
/// Charging Station", sections 3.3 and 3.4).
///
/// NetworkConfiguration[<slot>] access is never redirected. Slots are numbered globally and are
/// partitioned between connections by their NetworkConfigurationPriority lists, which the
/// application note requires to be non-overlapping (section 3.4.1).
class ConnectionConnectivityConfiguration : public ocpp::ConnectivityManagerConfiguration {
public:
    /// \param device_model Shared device model; must outlive this adapter.
    /// \param connection   The connection this adapter configures.
    ConnectionConnectivityConfiguration(DeviceModelAbstract& device_model, ConnectionId connection);

    // --- redirected to OCPPCommCtrlr[<instance>] for a secondary connection ---

    std::string get_network_configuration_priority() override;
    void set_network_configuration_priority(const std::string& priority, const std::string& source) override;
    std::int32_t get_security_profile() override;
    void set_active_security_profile(std::int32_t security_profile, const std::string& source) override;
    void set_active_network_profile_slot(std::int32_t slot, const std::string& source) override;
    std::optional<std::int32_t> get_active_network_profile_slot() override;

    // --- suppressed for a secondary connection ---

    /// \brief Records the negotiated security profile on the instance-less SecurityCtrlr.
    ///
    /// No-op for a secondary connection: SecurityCtrlr describes the primary connection to the CSMS,
    /// and a secondary connecting at a lower security profile must not appear to downgrade it. The
    /// per-connection value is kept by set_active_security_profile() instead.
    void set_security_ctrl_security_profile(std::int32_t security_profile, const std::string& source) override;

    /// \brief Records the identity on the instance-less SecurityCtrlr.
    ///
    /// No-op for a secondary connection, for the same reason as
    /// set_security_ctrl_security_profile(). A secondary's identity comes from the per-slot
    /// NetworkConfiguration[<slot>].Identity, which the device model already prefers over the
    /// SecurityCtrlr global.
    void set_security_ctrl_identity(const std::string& identity, const std::string& source) override;

    /// \brief Records the active slot's message timeout on the instance-less OCPPCommCtrlr.
    ///
    /// No-op for a secondary connection: the instance-less OCPPCommCtrlr.MessageTimeout reports the
    /// primary connection's active slot, and a secondary connecting with a different timeout must not
    /// overwrite it. A secondary's own queue still takes the timeout of its own slot on connect.
    void set_active_message_timeout(std::int32_t message_timeout_s, const std::string& source) override;

    // --- slot scoped or station wide, delegated unchanged ---

    std::optional<ocpp::v2::NetworkConnectionProfile> read_network_connection_profile(std::int32_t slot) override;
    bool write_network_connection_profile(std::int32_t slot, const ocpp::v2::NetworkConnectionProfile& profile,
                                          const std::string& source) override;
    void clear_network_connection_profile(std::int32_t slot) override;
    bool get_allow_security_level_zero_connections() override;
    std::optional<std::int32_t> get_network_config_timeout() override;
    std::optional<WebsocketConnectionOptions> get_websocket_connection_options(std::int32_t slot) override;
    void set_per_slot_ocpp_version(std::int32_t slot, const std::string& version, const std::string& source) override;

    /// \brief The connection this adapter configures.
    [[nodiscard]] const ConnectionId& connection() const;

private:
    DeviceModelAbstract& device_model;
    ConnectionId m_connection;
};

} // namespace v2
} // namespace ocpp
