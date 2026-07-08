// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "OCPPmulti.hpp"
#include "generic_ocpp.hpp"

namespace module {

// Note should any of these configuration items be read-write then
// add a mutex to protect access

std::string ConfigAccess::getChargePointConfigPath() const {
    return m_config.ChargePointConfigPath;
}
int ConfigAccess::getCompositeScheduleIntervalS() const {
    return m_config.CompositeScheduleIntervalS;
}
std::string ConfigAccess::getCoreDatabasePath() const {
    return m_config.CoreDatabasePath;
}
std::string ConfigAccess::getCustomMrecErrorMapPath() const {
    return m_config.CustomMrecErrorMapPath;
}
std::string ConfigAccess::getDatabasePath() const {
    return m_config.DatabasePath;
}
int ConfigAccess::getDelayOcppStart() const {
    return m_config.DelayOcppStart;
}
std::string ConfigAccess::getDeviceModelConfigMappings() const {
    return m_config.DeviceModelConfigMappings;
}
std::string ConfigAccess::getDeviceModelConfigPath() const {
    return m_config.DeviceModelConfigPath;
}
std::string ConfigAccess::getDeviceModelDatabasePath() const {
    return m_config.DeviceModelDatabasePath;
}
std::string ConfigAccess::getDeviceModelDatabaseMigrationPath() const {
    return m_config.DeviceModelDatabaseMigrationPath;
}
bool ConfigAccess::getEnableExternalWebsocketControl() const {
    return m_config.EnableExternalWebsocketControl;
}
bool ConfigAccess::getEnableLegacyConfigMigration() const {
    return m_config.EnableLegacyConfigMigration;
}
std::string ConfigAccess::getEverestDeviceModelDatabasePath() const {
    return m_config.EverestDeviceModelDatabasePath;
}
int ConfigAccess::getOcpp16NetworkConfigSlot() const {
    return m_config.Ocpp16NetworkConfigSlot;
}
std::string ConfigAccess::getMessageLogPath() const {
    return m_config.MessageLogPath;
}
int ConfigAccess::getMessageQueueResumeDelay() const {
    return m_config.MessageQueueResumeDelay;
}
int ConfigAccess::getRequestCompositeScheduleDurationS() const {
    return m_config.RequestCompositeScheduleDurationS;
}
std::string ConfigAccess::getRequestCompositeScheduleUnit() const {
    return m_config.RequestCompositeScheduleUnit;
}
int ConfigAccess::getResetStopDelay() const {
    return m_config.ResetStopDelay;
}
std::string ConfigAccess::getUserConfigPath() const {
    return m_config.UserConfigPath;
}

OCPPmulti::~OCPPmulti() {
    m_charge_point.shutdown();
}

void OCPPmulti::init() {
    EVLOG_warning << "This OCPPmulti module is currently experimental! Configuration parameters and the integration in "
                     "EVerest may change without further notice";
    // no code should be in the init methods
    // invoke_init(*p_ocpp16);
    // invoke_init(*p_auth_validator);
    // invoke_init(*p_auth_provider);
    // invoke_init(*p_data_transfer);
    // invoke_init(*p_ocpp_generic);
    // invoke_init(*p_session_cost);

    // support the everest_api inteface
    if (config.EnableExternalWebsocketControl) {
        mqtt.subscribe("everest_api/ocpp/cmd/connect", [this](const auto&) { m_ocpp.connect_websocket(); });
        mqtt.subscribe("everest_api/ocpp/cmd/disconnect", [this](const auto&) { m_ocpp.disconnect_websocket(); });
    }

    subscribe_global_all_errors([this](const auto& arg) { m_ocpp.cb_error_handler(arg); },
                                [this](const auto& arg) { m_ocpp.cb_error_cleared_handler(arg); });

    auto mode = ocpp_multi::GenericChargePointInterface::modes_t::prefer_ocpp_2;
    // - Prefer1.6
    // - Prefer2
    // - Only1.6
    // - Only2

    if (config.Mode == "Prefer1.6") {
        mode = ocpp_multi::GenericChargePointInterface::modes_t::prefer_ocpp_1_6;
    } else if (config.Mode == "Only1.6") {
        mode = ocpp_multi::GenericChargePointInterface::modes_t::ocpp_1_6_only;
    } else if (config.Mode == "Only2") {
        mode = ocpp_multi::GenericChargePointInterface::modes_t::ocpp_2_only;
    }

    m_ocpp.set_mode(mode);
    // both members are fully constructed here; safe to bind m_ocpp as the callback sink
    m_charge_point.set_callbacks(m_ocpp);
    m_ocpp.init();
}

void OCPPmulti::ready() {
    // no code should be in the ready methods
    // invoke_ready(*p_ocpp16);
    // invoke_ready(*p_auth_validator);
    // invoke_ready(*p_auth_provider);
    // invoke_ready(*p_data_transfer);
    // invoke_ready(*p_ocpp_generic);
    // invoke_ready(*p_session_cost);

    m_ocpp.ready(module::get_config_service_client());
}

} // namespace module
