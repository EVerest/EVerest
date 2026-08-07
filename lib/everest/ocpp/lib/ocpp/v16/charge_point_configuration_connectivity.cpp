// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include <everest/logging.hpp>
#include <ocpp/common/websocket/websocket_uri.hpp>
#include <ocpp/v16/charge_point_configuration_connectivity.hpp>
#include <ocpp/v2/device_model_interface.hpp>
#include <ocpp/v2/ocpp_enums.hpp>
#include <ocpp/v2/ocpp_types.hpp>

#include <chrono>
#include <exception>

namespace ocpp::v16 {

std::string ChargePointConfigurationConnectivity::get_network_configuration_priority() {
    return "1";
}

void ChargePointConfigurationConnectivity::set_network_configuration_priority(const std::string& priority,
                                                                              const std::string& /*source*/) {
    EVLOG_warning << "OCPP 1.6 does not support multiple network connection profiles, ignoring priority " << priority;
}

std::optional<ocpp::v2::NetworkConnectionProfile>
ChargePointConfigurationConnectivity::read_network_connection_profile(int32_t slot) {
    if (slot != 1) {
        return std::nullopt;
    }
    ocpp::v2::NetworkConnectionProfile profile;
    profile.ocppCsmsUrl = getCentralSystemURI();
    profile.securityProfile = getSecurityProfile();
    profile.ocppInterface = ocpp::v2::OCPPInterfaceEnum::Any; // not used for OCPP1.6
    profile.ocppTransport = ocpp::v2::OCPPTransportEnum::JSON;
    profile.messageTimeout = 10;
    profile.identity = getChargePointId();
    profile.basicAuthPassword = getAuthorizationKey();
    return profile;
}

bool ChargePointConfigurationConnectivity::write_network_connection_profile(
    int32_t slot, const ocpp::v2::NetworkConnectionProfile& /* profile */, const std::string& /*source*/) {
    EVLOG_warning << "OCPP 1.6 does not support multiple network connection profiles, ignoring write to slot " << slot;
    return true;
}

void ChargePointConfigurationConnectivity::clear_network_connection_profile(int32_t /*slot*/) {
    EVLOG_warning << "OCPP 1.6 does not support multiple network connection profiles, ignoring clear request";
}

bool ChargePointConfigurationConnectivity::get_allow_security_level_zero_connections() {
    return true; // v1.6 explicitly allows security profile 0
}

int32_t ChargePointConfigurationConnectivity::get_security_profile() {
    return getSecurityProfile();
}

std::optional<int32_t> ChargePointConfigurationConnectivity::get_network_config_timeout() {
    return std::nullopt; // ConnectivityManager will apply its built-in 60 s default
}

std::optional<WebsocketConnectionOptions>
ChargePointConfigurationConnectivity::get_websocket_connection_options(int32_t /*slot*/) {
    try {
        const auto charge_point_id = getChargePointId();
        if (charge_point_id.find(':') != std::string::npos) {
            // Returning std::nullopt lets the caller fall back to another profile instead.
            EVLOG_error << "ChargePointId must not contain ':'";
            return std::nullopt;
        }
        const auto security_profile = getSecurityProfile();
        auto uri = Uri::parse_and_validate(getCentralSystemURI(), charge_point_id, security_profile);

        WebsocketConnectionOptions opts{{OcppProtocolVersion::v16},
                                        uri,
                                        security_profile,
                                        getAuthorizationKey(),
                                        std::chrono::seconds(10),
                                        getRetryBackoffRandomRange(),
                                        getRetryBackoffRepeatTimes(),
                                        getRetryBackoffWaitMinimum(),
                                        -1, // unlimited connection attempts
                                        getSupportedCiphers12(),
                                        getSupportedCiphers13(),
                                        getWebsocketPingInterval().value_or(0),
                                        getWebsocketPingPayload(),
                                        getWebsocketPongTimeout(),
                                        getUseSslDefaultVerifyPaths(),
                                        getAdditionalRootCertificateCheck().value_or(false),
                                        getHostName(),
                                        getVerifyCsmsCommonName(),
                                        getUseTPM(),
                                        getVerifyCsmsAllowWildcards(),
                                        getIFace(),
                                        getEnableTLSKeylog(),
                                        getTLSKeylogFile()};
        return opts;
    } catch (const ocpp::v2::DeviceModelError& e) {
        EVLOG_error << "Could not configure v1.6 connection options, device model error: " << e.what();
        return std::nullopt;
    } catch (const std::invalid_argument& e) {
        EVLOG_error << "Could not configure v1.6 connection options: " << e.what();
        return std::nullopt;
    }
}

void ChargePointConfigurationConnectivity::set_active_security_profile(int32_t security_profile,
                                                                       const std::string& /*source*/) {
    setSecurityProfile(security_profile);
}

void ChargePointConfigurationConnectivity::set_active_network_profile_slot(int32_t slot,
                                                                           const std::string& /*source*/) {
    EVLOG_warning << "OCPP 1.6 does not support multiple network connection profiles, ignoring set active slot "
                  << slot;
}

std::optional<int32_t> ChargePointConfigurationConnectivity::get_active_network_profile_slot() {
    // OCPP 1.6 does not support multiple network connection profiles, so there is no fallback target.
    return std::nullopt;
}

void ChargePointConfigurationConnectivity::set_per_slot_ocpp_version(int32_t /*slot*/, const std::string& /*version*/,
                                                                     const std::string& /*source*/) {
    EVLOG_warning
        << "OCPP 1.6 does not support multiple network connection profiles, ignoring set OCPP version for slot";
}

void ChargePointConfigurationConnectivity::set_security_ctrl_security_profile(int32_t security_profile,
                                                                              const std::string& /*source*/) {
    setSecurityProfile(security_profile);
}

void ChargePointConfigurationConnectivity::set_security_ctrl_identity(const std::string& /*identity*/,
                                                                      const std::string& /*source*/) {
    EVLOG_warning << "OCPP 1.6 does not support setting identity, ignoring";
}

} // namespace ocpp::v16
