// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <ocpp/v2/connection_connectivity_configuration.hpp>

#include <everest/logging.hpp>

#include <ocpp/v2/ctrlr_component_variables.hpp>
#include <ocpp/v2/device_model_abstract.hpp>

namespace ocpp {
namespace v2 {

namespace {
/// Security profile reported for a secondary connection that has never connected. Zero means "no
/// confirmed profile yet", which makes the ConnectivityManager prune nothing: downgrade protection
/// only starts applying once the connection has actually been established at a known profile.
constexpr std::int32_t NO_CONFIRMED_SECURITY_PROFILE = 0;
} // namespace

ConnectionConnectivityConfiguration::ConnectionConnectivityConfiguration(DeviceModelAbstract& device_model,
                                                                         ConnectionId connection) :
    device_model(device_model), m_connection(std::move(connection)) {
}

const ConnectionId& ConnectionConnectivityConfiguration::connection() const {
    return this->m_connection;
}

std::string ConnectionConnectivityConfiguration::get_network_configuration_priority() {
    if (this->m_connection.is_primary()) {
        return this->device_model.get_network_configuration_priority();
    }
    const auto cv = ConnectionComponentVariables::get_component_variable(
        this->m_connection, ConnectionComponentVariables::NetworkConfigurationPriority);
    return this->device_model.get_optional_value<std::string>(cv).value_or("");
}

void ConnectionConnectivityConfiguration::set_network_configuration_priority(const std::string& priority,
                                                                             const std::string& source) {
    if (this->m_connection.is_primary()) {
        this->device_model.set_network_configuration_priority(priority, source);
        return;
    }
    const auto cv = ConnectionComponentVariables::get_component_variable(
        this->m_connection, ConnectionComponentVariables::NetworkConfigurationPriority);
    this->device_model.set_value(cv.component, cv.variable.value(), AttributeEnum::Actual, priority, source);
}

std::int32_t ConnectionConnectivityConfiguration::get_security_profile() {
    if (this->m_connection.is_primary()) {
        return this->device_model.get_security_profile();
    }
    const auto cv = ConnectionComponentVariables::get_component_variable(this->m_connection,
                                                                         ConnectionComponentVariables::SecurityProfile);
    return this->device_model.get_optional_value<std::int32_t>(cv).value_or(NO_CONFIRMED_SECURITY_PROFILE);
}

void ConnectionConnectivityConfiguration::set_active_security_profile(std::int32_t security_profile,
                                                                      const std::string& source) {
    if (this->m_connection.is_primary()) {
        this->device_model.set_active_security_profile(security_profile, source);
        return;
    }
    const auto cv = ConnectionComponentVariables::get_component_variable(this->m_connection,
                                                                         ConnectionComponentVariables::SecurityProfile);
    this->device_model.set_read_only_value(cv.component, cv.variable.value(), AttributeEnum::Actual,
                                           std::to_string(security_profile), source);
}

void ConnectionConnectivityConfiguration::set_active_network_profile_slot(std::int32_t slot,
                                                                          const std::string& source) {
    if (this->m_connection.is_primary()) {
        this->device_model.set_active_network_profile_slot(slot, source);
        return;
    }
    const auto cv = ConnectionComponentVariables::get_component_variable(
        this->m_connection, ConnectionComponentVariables::ActiveNetworkProfile);
    this->device_model.set_read_only_value(cv.component, cv.variable.value(), AttributeEnum::Actual,
                                           std::to_string(slot), source);
}

std::optional<std::int32_t> ConnectionConnectivityConfiguration::get_active_network_profile_slot() {
    if (this->m_connection.is_primary()) {
        return this->device_model.get_active_network_profile_slot();
    }
    const auto cv = ConnectionComponentVariables::get_component_variable(
        this->m_connection, ConnectionComponentVariables::ActiveNetworkProfile);
    return this->device_model.get_optional_value<std::int32_t>(cv);
}

void ConnectionConnectivityConfiguration::set_security_ctrl_security_profile(std::int32_t security_profile,
                                                                             const std::string& source) {
    if (!this->m_connection.is_primary()) {
        EVLOG_debug << this->m_connection.to_string()
                    << ": not writing SecurityCtrlr.SecurityProfile; the instance-less SecurityCtrlr describes the "
                       "primary connection";
        return;
    }
    this->device_model.set_security_ctrl_security_profile(security_profile, source);
}

void ConnectionConnectivityConfiguration::set_security_ctrl_identity(const std::string& identity,
                                                                     const std::string& source) {
    if (!this->m_connection.is_primary()) {
        EVLOG_debug << this->m_connection.to_string()
                    << ": not writing SecurityCtrlr.Identity; the instance-less SecurityCtrlr describes the primary "
                       "connection";
        return;
    }
    this->device_model.set_security_ctrl_identity(identity, source);
}

void ConnectionConnectivityConfiguration::set_active_message_timeout(std::int32_t message_timeout_s,
                                                                     const std::string& source) {
    if (!this->m_connection.is_primary()) {
        EVLOG_debug << this->m_connection.to_string()
                    << ": not writing OCPPCommCtrlr.MessageTimeout; the instance-less OCPPCommCtrlr describes the "
                       "primary connection";
        return;
    }
    this->device_model.set_active_message_timeout(message_timeout_s, source);
}

std::optional<ocpp::v2::NetworkConnectionProfile>
ConnectionConnectivityConfiguration::read_network_connection_profile(std::int32_t slot) {
    return this->device_model.read_network_connection_profile(slot);
}

bool ConnectionConnectivityConfiguration::write_network_connection_profile(
    std::int32_t slot, const ocpp::v2::NetworkConnectionProfile& profile, const std::string& source) {
    return this->device_model.write_network_connection_profile(slot, profile, source);
}

void ConnectionConnectivityConfiguration::clear_network_connection_profile(std::int32_t slot) {
    this->device_model.clear_network_connection_profile(slot);
}

bool ConnectionConnectivityConfiguration::get_allow_security_level_zero_connections() {
    return this->device_model.get_allow_security_level_zero_connections();
}

std::optional<std::int32_t> ConnectionConnectivityConfiguration::get_network_config_timeout() {
    return this->device_model.get_network_config_timeout();
}

std::optional<WebsocketConnectionOptions>
ConnectionConnectivityConfiguration::get_websocket_connection_options(std::int32_t slot) {
    // Delegated as-is. Everything that has to differ between connections is already slot scoped:
    // the URL, the security profile and, via DeviceModel::resolve_identity() /
    // resolve_basic_auth_password(), the credentials, all of which come from
    // NetworkConfiguration[<slot>] and only fall back to the SecurityCtrlr globals when the slot
    // leaves them unset. The remaining options are websocket tuning read from the instance-less
    // OCPPCommCtrlr; making those per-connection is deliberately left for later.
    return this->device_model.get_websocket_connection_options(slot);
}

void ConnectionConnectivityConfiguration::set_per_slot_ocpp_version(std::int32_t slot, const std::string& version,
                                                                    const std::string& source) {
    this->device_model.set_per_slot_ocpp_version(slot, version, source);
}

} // namespace v2
} // namespace ocpp
